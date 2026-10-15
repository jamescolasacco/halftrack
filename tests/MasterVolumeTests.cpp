#include "Session.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace halftrack
{
namespace
{
void check(bool value,const juce::String& message)
{if(!value)throw std::runtime_error(message.toStdString());std::cout<<"PASS master volume: "<<message<<std::endl;}
void ok(const juce::Result& result,const juce::String& message)
{check(result.wasOk(),message+(result.failed()?": "+result.getErrorMessage():""));}
juce::AudioBuffer<float> read(const juce::File& file)
{
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    check(reader&&reader->sampleRate==48000&&reader->numChannels==2&&reader->lengthInSamples==48000,"read exact stereo export extent");
    juce::AudioBuffer<float> result(2,48000);check(reader->read(&result,0,48000,0,true,true),"decode every exported frame");return result;
}
void compare(Session& session,const juce::File& output,const juce::AudioBuffer<float>& source,double gain)
{
    ok(session.exportWav(output),"export through native master output");const auto actual=read(output);double worst=0;
    for(int c=0;c<2;++c)for(int n=0;n<48000;++n)
    {
        const double value=actual.getSample(c,n);if(!std::isfinite(value))throw std::runtime_error("nonfinite master output");
        worst=std::max(worst,std::abs(value-source.getSample(c,n)*gain));
    }
    check(gain==0?worst==0:worst<3e-6,"full stereo mix follows independent master-gain arithmetic; error="+juce::String(worst,9));
}
}
int runMasterVolumeTests(const juce::File& artifacts)
{
    try
    {
        const auto root=artifacts.getChildFile("master-volume-"+juce::Uuid().toString());ok(root.createDirectory(),"create isolated fixtures");
        juce::AudioBuffer<float> source(2,48000);source.clear();
        for(int n=8192;n<16384;++n)source.setSample(0,n,.03125f);
        for(int n=24576;n<32768;++n)source.setSample(1,n,-.0625f);
        const auto file=root.getChildFile("source.wav"),output=root.getChildFile("master.wav"),project=root.getChildFile("master.halftrack");
        {
            juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
            auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
            check(writer&&writer->writeFromAudioSampleBuffer(source,0,48000),"write exactly representable stereo test signal");
        }
        Session session(false);check(std::abs(session.snapshot().masterGainDb)<1e-4,"new project master defaults to unity");
        ok(session.importAudio(file,0),"import generated master fixture");ok(session.addReturnTrack("Master test return"),"add a return to the full mix");
        const auto returnId=session.snapshot().tracks[2].id;ok(session.setTrackSendGain(0,returnId,-6),"feed a known return contribution");
        const double mix=1+std::pow(10.0,-6.0/20);
        for(float db:{-100.f,-24.f,-6.f,0.f,6.f})
        {
            const float previous=session.snapshot().masterGainDb;ok(session.setMasterGain(db),"set master level");
            check(std::abs(session.snapshot().masterGainDb-db)<1e-4,"snapshot reports actual master level");
            compare(session,output,source,db<=-100?0:mix*std::pow(10.0,db/20));
            session.undo();check(std::abs(session.snapshot().masterGainDb-previous)<1e-4,"master level undoes once");
            session.redo();check(std::abs(session.snapshot().masterGainDb-db)<1e-4,"master level redoes exactly");
        }
        session.beginEditGesture("Master volume");ok(session.setMasterGain(-3),"begin continuous fader edit");ok(session.setMasterGain(-6),"continue fader edit");ok(session.setMasterGain(-12),"finish fader edit");session.endEditGesture();
        session.undo();check(std::abs(session.snapshot().masterGainDb-6)<1e-4,"continuous master movement is one undo gesture");compare(session,output,source,mix*std::pow(10.0,6.0/20));
        session.redo();check(std::abs(session.snapshot().masterGainDb+12)<1e-4,"redo restores final drag level");compare(session,output,source,mix*std::pow(10.0,-12.0/20));
        ok(session.saveProject(project),"save native master level");ok(session.loadProject(project),"reopen native master level");
        check(std::abs(session.snapshot().masterGainDb+12)<1e-4,"master level persists");compare(session,output,source,mix*std::pow(10.0,-12.0/20));
        ok(session.setMasterGain(-12),"unchanged master level is a no-op");check(!session.snapshot().dirty&&!session.canUndo(),"unchanged level does not dirty project or add history");
        ok(session.setMasterGain(-6),"prepare redo before invalid load");session.undo();
        for(const auto& value:juce::StringArray{"nan","inf","-0.1","1.1","0.5 trailing",""})
        {
            auto xml=juce::XmlDocument::parse(project);auto* master=xml->getChildByName("EDIT")->getChildByName("MASTERVOLUME")->getChildByName("PLUGIN");
            check(master!=nullptr,"locate native master state");master->setAttribute("volume",value);const auto bad=root.getChildFile("invalid-master.halftrack");check(xml->writeTo(bad),"write invalid master fixture");
            check(session.loadProject(bad).failed()&&std::abs(session.snapshot().masterGainDb+12)<1e-4&&session.canRedo(),"invalid master cannot replace active state or redo");
        }
        auto duplicate=juce::XmlDocument::parse(project);auto* edit=duplicate->getChildByName("EDIT");edit->addChildElement(new juce::XmlElement(*edit->getChildByName("MASTERVOLUME")));
        const auto bad=root.getChildFile("duplicate-master.halftrack");check(duplicate->writeTo(bad),"write duplicate master fixture");check(session.loadProject(bad).failed()&&session.canRedo(),"ambiguous master state is rejected before mutation");
        Session limits(false);ok(limits.setMasterGain(100),"clamp finite master gain to native headroom");check(std::abs(limits.snapshot().masterGainDb-6)<1e-4,"native master maximum is +6 dB");
        const auto maximum=root.getChildFile("maximum.halftrack");ok(limits.saveProject(maximum),"save maximum master");ok(limits.loadProject(maximum),"reopen maximum master");
        ok(limits.setMasterGain(100),"repeated clamped level is unchanged");check(!limits.canUndo()&&!limits.snapshot().dirty,"unchanged clamped value preserves clean state");
        check(limits.setMasterGain(std::numeric_limits<float>::quiet_NaN()).failed()&&limits.setMasterGain(std::numeric_limits<float>::infinity()).failed()&&!limits.canUndo(),"nonfinite requests preserve master and history");
        ok(limits.setMasterGain(-1000),"clamp master to silence");check(limits.snapshot().masterGainDb==-100,"bottom of master fader is silence");limits.undo();check(std::abs(limits.snapshot().masterGainDb-6)<1e-4,"silence edit undoes once");
        ok(limits.newProject(),"create another project");check(std::abs(limits.snapshot().masterGainDb)<1e-4,"new project resets master to unity");
        std::cout<<"Master volume tests passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL master volume: "<<error.what()<<std::endl;return 1;}
}
}
