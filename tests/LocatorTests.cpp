#include "Session.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace halftrack
{
void useHardClipEdges();
namespace
{
void check(bool condition,const juce::String& message)
{
    if(!condition)throw std::runtime_error(message.toStdString());
    std::cout<<"PASS locators: "<<message<<std::endl;
}
void ok(const juce::Result& r,const juce::String& message){check(r.wasOk(),message+(r.failed()?": "+r.getErrorMessage():""));}
juce::ValueTree firstMarker(juce::ValueTree tree)
{
    if(tree.hasType("MARKERCLIP"))return tree;
    for(auto child:tree)if(auto found=firstMarker(child);found.isValid())return found;
    return {};
}
}
int runLocatorTests(const juce::File& artifacts)
{
    try
    {
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
        const auto root=artifacts.getChildFile("locators-"+juce::Uuid().toString());ok(root.createDirectory(),"create isolated fixtures");
        const auto project=root.getChildFile("song.halftrack"),render=root.getChildFile("mix.wav");
        Session s(false);juce::String start,verse,chorus;
        ok(s.addLocator(0,"Start",start),"add locator at zero");
        ok(s.addLocator(4,"Chorus",chorus),"add later locator");ok(s.addLocator(1,"Verse",verse),"insert earlier locator out of order");
        auto locators=s.snapshot().locators;
        check(locators.size()==3&&locators[0].id==start&&locators[1].id==verse&&locators[2].id==chorus,"snapshots sort locators with distinct stable identities");
        s.undo();check(s.snapshot().locators.size()==2,"one undo removes added locator");s.redo();check(s.snapshot().locators[1].id==verse,"redo restores locator identity");
        ok(s.updateLocator(verse,"Verse A",2.5),"edit name and time together");s.undo();
        check(s.snapshot().locators[1].name=="Verse"&&s.snapshot().locators[1].position==1,"one undo restores both name and position");s.redo();
        ok(s.removeLocator(chorus),"remove locator");s.undo();check(s.snapshot().locators.back().id==chorus,"delete undo restores marker identity");
        s.setTempo(240);check(s.snapshot().locators[1].position==2.5&&s.snapshot().locators[2].position==4,"absolute locators do not move with tempo");
        ok(s.saveProject(project),"save native locator state");ok(s.loadProject(project),"reopen named locators");
        check(s.snapshot().locators[1].id==verse&&s.snapshot().locators[1].name=="Verse A"&&s.snapshot().locators[1].position==2.5,"locator data round trips");
        ok(s.updateLocator(verse,"Verse A",2.5),"identical locator edit no-op");check(!s.canUndo()&&!s.snapshot().dirty,"identical edit preserves clean history");
        s.seek(0);ok(s.navigateLocator(true),"navigate forward");check(s.snapshot().position==2.5,"next excludes current locator");
        ok(s.navigateLocator(true),"navigate to final locator");check(s.snapshot().position==4,"next reaches following locator");
        ok(s.navigateLocator(true),"navigation at end no-op");check(s.snapshot().position==4,"navigation does not wrap");
        ok(s.navigateLocator(false),"navigate backward");check(s.snapshot().position==2.5,"previous excludes current locator");
        ok(s.seekLocator(start),"seek locator by identity");check(s.snapshot().position==0&&!s.canUndo()&&!s.snapshot().dirty,"navigation changes no document history");
        ok(s.navigateLocator(false),"navigation before first no-op");check(s.snapshot().position==0,"previous clamps without wrapping");
        ok(s.updateLocator(verse,"Changed",3),"prepare redo checkpoint");s.undo();
        for(double time:{-1.0,172800.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        {
            juce::String unchanged="keep";
            check(s.addLocator(time,"Invalid",unchanged).failed()&&unchanged=="keep"&&s.updateLocator(verse,"Invalid",time).failed()&&s.canRedo(),"invalid time preserves state, redo and result identity");
        }
        check(s.updateLocator(verse,"   ",1).failed()&&s.updateLocator(verse,juce::String::repeatedString("x",129),1).failed()
            &&s.updateLocator(verse,"Two\nlines",1).failed()&&s.removeLocator("missing").failed()&&s.seekLocator("missing").failed()&&s.canRedo(),"invalid names and missing IDs preserve redo");
        for(int bad=0;bad<9;++bad)
        {
            auto tree=juce::ValueTree::fromXml(*juce::XmlDocument::parse(project));auto marker=firstMarker(tree);check(marker.isValid(),"find native marker state");
            if(bad==0)marker.setProperty("start","nan",nullptr);
            if(bad==1)marker.setProperty("start",-1,nullptr);
            if(bad==2)marker.setProperty("start",172800,nullptr);
            if(bad==3)marker.setProperty("name","",nullptr);
            if(bad==4)marker.getParent().addChild(marker.createCopy(),-1,nullptr);
            if(bad==5)marker.setProperty("sync",0,nullptr);
            if(bad==6)marker.setProperty("length","inf",nullptr);
            if(bad==7)
            {
                auto alias=marker.createCopy();alias.setProperty("id","0"+marker["id"].toString(),nullptr);
                marker.getParent().addChild(alias,-1,nullptr);
            }
            if(bad==8)marker.setProperty("id","invalid",nullptr);
            const auto invalid=root.getChildFile("invalid.halftrack");check(tree.createXml()->writeTo(invalid),"write generated malformed marker state");
            check(s.loadProject(invalid).failed()&&s.canRedo()&&s.snapshot().locators[1].id==verse&&s.snapshot().locators[1].name=="Verse A","malformed locator load preserves existing project and undo");
        }
        check(s.exportWav(render).failed(),"locators alone do not create an audio export");
        juce::AudioBuffer<float> source(2,4800);
        for(int c=0;c<2;++c)for(int n=0;n<4800;++n)source.setSample(c,n,(float)((n*(c+7))%128-64)/1024);
        const auto audio=root.getChildFile("original.wav");
        {juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream(audio.createOutputStream());
        auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        check(writer&&writer->writeFromAudioSampleBuffer(source,0,4800),"write original audio fixture");}
        ok(s.importAudio(audio,0),"import audio beside locators");juce::String distant;ok(s.addLocator(300,"Later section",distant),"add distant locator");
        ok(s.exportWav(render),"export audio with distant locator");
        juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(render));
        check(reader&&reader->lengthInSamples==4800&&reader->numChannels==2,"locator does not extend exported duration");
        juce::AudioBuffer<float> result(2,4800);check(reader->read(&result,0,4800,0,true,true),"read complete locator export");
        double error=0;for(int c=0;c<2;++c)for(int n=0;n<4800;++n)
        {const auto value=result.getSample(c,n);if(!std::isfinite(value))throw std::runtime_error("nonfinite rendered sample");error=std::max(error,std::abs((double)value-source.getSample(c,n)));}
        check(error<2e-6,"locators preserve every rendered source sample");
        ok(s.saveProject(project),"save audio and locators together");ok(s.loadProject(project),"reopen combined project");
        check(s.snapshot().locators.back().id==distant&&s.snapshot().tracks[0].clips.size()==1,"combined project retains independent marker and audio ownership");
        std::cout<<"Locator tests passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL locators: "<<e.what()<<std::endl;return 1;}
}
}
