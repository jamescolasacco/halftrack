#include "NativeEffects.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace halftrack
{
namespace
{
using Kind = Session::EffectKind;
using Settings = Session::EffectSettings;
constexpr double pi = 3.14159265358979323846;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS native effects: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": "+r.getErrorMessage() : "")); }
juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& source, const Settings& s, double rate = 48000)
{
    auto out = source; NativeEffectDSP dsp; dsp.prepare (rate, s.kind); dsp.configure (s);
    dsp.processSamples (out.getWritePointer (0), out.getWritePointer (1), out.getNumSamples()); return out;
}
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1e10;
    double worst = 0;
    for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n)
    { if (! std::isfinite (a.getSample(c,n)) || ! std::isfinite (b.getSample(c,n))) return 1e10; worst = std::max (worst, std::abs ((double)a.getSample(c,n)-b.getSample(c,n))); }
    return worst;
}
juce::AudioBuffer<float> signal (int frames)
{
    juce::AudioBuffer<float> a (2, frames);
    for (int n = 0; n < frames; ++n) { a.setSample(0,n,(float)((n*37)%113-56)/256); a.setSample(1,n,(float)((n*17)%97-48)/256); }
    return a;
}
void equalizerTests()
{
    const auto input = signal (4096); auto s = defaultEffectSettings (Kind::equalizer);
    check (difference (run(input,s),input) == 0, "eight flat bands preserve every sample exactly");
    for (int band = 0; band < 8; ++band) for (float gain : { -18.0f, -6.0f, 6.0f, 18.0f })
    {
        s = defaultEffectSettings (Kind::equalizer); s.values[4*band+1] = 1000; s.values[4*band+2] = gain;
        juce::AudioBuffer<float> sine (2, 48000);
        for (int n = 0; n < sine.getNumSamples(); ++n) { sine.setSample(0,n,(float)(.01*std::sin(2*pi*1000*n/48000))); sine.setSample(1,n,sine.getSample(0,n)*-.5f); }
        const auto out = run (sine,s); double error = 0;
        for (int c = 0; c < 2; ++c) for (int n = 12000; n < 48000; ++n)
            error = std::max (error,std::abs(out.getSample(c,n)-sine.getSample(c,n)*std::pow(10.0,gain/20)));
        check (error < 1e-7, "bell center gain, stereo isolation, band "+juce::String(band+1)+" / "+juce::String(gain)+" dB");
    }
    for (int type = 2; type <= 7; ++type)
    {
        s = defaultEffectSettings (Kind::equalizer); s.values[0] = (float)type; s.values[1] = 1000; s.values[2] = 12;
        juce::AudioBuffer<float> probe (2, 48000);
        for (int n=0;n<48000;++n) { probe.setSample(0,n,.125f); probe.setSample(1,n,(n%2 ? -.125f : .125f)); }
        const auto out=run(probe,s);
        const double dc = type==2 ? std::pow(10.0,.6) : (type==4 || type==7 ? 0 : 1);
        const double nyquist = type==3 ? std::pow(10.0,.6) : (type==5 || type==7 ? 0 : 1);
        double error=0;for(int n=40000;n<48000;++n) { error=std::max(error,std::abs(out.getSample(0,n)-.125*dc));error=std::max(error,std::abs(out.getSample(1,n)-probe.getSample(1,n)*nyquist)); }
        check(error<1e-7,"independent DC/Nyquist limits for filter type "+juce::String(type));
    }
    for (int type : {4,5,6,7})
    {
        s=defaultEffectSettings(Kind::equalizer);s.values[0]=(float)type;s.values[1]=1000;
        juce::AudioBuffer<float> probe(2,48000);for(int n=0;n<48000;++n)for(int c=0;c<2;++c)probe.setSample(c,n,(float)(.01*std::sin(2*pi*1000*n/48000)));
        auto out=run(probe,s);double power=0;for(int n=24000;n<48000;++n)power+=out.getSample(0,n)*out.getSample(0,n);
        const auto amplitude=std::sqrt(2*power/24000)/.01;
        const double target=type==6 ? 0 : type==7 ? 1 : std::sqrt(.5);
        check(std::abs(amplitude-target)<1e-6,"cutoff/notch/bandpass center magnitude "+juce::String(type));
    }
}
void compressorTests()
{
    for (int detector=0;detector<2;++detector) for(float ratio : {1.0f,2.0f,4.0f,20.0f})
    {
        auto s=defaultEffectSettings(Kind::compressor);s.values[0]=-24;s.values[1]=ratio;s.values[2]=3;s.values[3]=80;s.values[4]=0;s.values[7]=(float)detector;
        juce::AudioBuffer<float> a(2,24000);for(int n=0;n<24000;++n){a.setSample(0,n,.5f);a.setSample(1,n,-.125f);}
        auto out=run(a,s);double error=0;
        for(int n=0;n<24000;++n)
        {
            const double fraction=-std::expm1(-(n+1)/(48000*.003));
            const auto level=.5*(detector ? std::sqrt(fraction) : fraction);
            const auto over=std::max(0.0,20*std::log10(level)+24);
            const auto gain=std::pow(10.0,over*(1/ratio-1)/20);
            error=std::max(error,std::abs(out.getSample(0,n)-.5*gain));error=std::max(error,std::abs(out.getSample(1,n)+.125*gain));
        }
        check(error<4e-8,"closed-form attack and linked stereo gain for detector/ratio "+juce::String(detector)+"/"+juce::String(ratio));
    }
    for(float db : {-36.0f,-24.0f,-21.0f,-18.0f,-15.0f,-12.0f,-6.0f})
    {
        auto s=defaultEffectSettings(Kind::compressor);s.values[0]=-18;s.values[1]=4;s.values[4]=12;
        const auto amplitude=std::pow(10.0,db/20);juce::AudioBuffer<float>a(2,48000);for(int c=0;c<2;++c)juce::FloatVectorOperations::fill(a.getWritePointer(c),(float)amplitude,48000);
        const auto out=run(a,s);const double x=db+18;const double reduction=x<=-6?0:x>=6?-.75*x:-.75*(x+6)*(x+6)/24;
        check(std::abs(out.getSample(0,47999)-amplitude*std::pow(10.0,reduction/20))<1e-7,"soft-knee transfer at "+juce::String(db)+" dBFS");
    }
}
void delayTests()
{
    for(double rate : {8000.0,44100.0,48000.0,96000.0})
    {
        auto s=defaultEffectSettings(Kind::delay);s.values[0]=10;s.values[1]=20;s.values[2]=0;s.values[3]=1;
        const int frames=(int)(rate*.05);juce::AudioBuffer<float>a(2,frames);a.clear();a.setSample(0,0,.5f);a.setSample(1,0,-.25f);
        const auto out=run(a,s,rate);bool exact=true;
        for(int n=0;n<frames;++n){exact=exact&&std::abs(out.getSample(0,n)-(n==(int)std::lround(rate*.01)?.5f:0))<1e-7f;exact=exact&&std::abs(out.getSample(1,n)-(n==(int)std::lround(rate*.02)?-.25f:0))<1e-7f;}
        check(exact,"independent left/right impulse timing at "+juce::String(rate));
    }
    auto s=defaultEffectSettings(Kind::delay);s.values[0]=s.values[1]=10;s.values[2]=.5f;s.values[3]=1;s.values[4]=1;
    juce::AudioBuffer<float>a(2,1800);a.clear();a.setSample(0,0,1);const auto out=run(a,s);
    const auto tone=std::exp(-2*pi*12000/48000.0);
    check(std::abs(out.getSample(0,480)-.5)<1e-7 && out.getSample(1,480)==0,"ping pong injects mono into first side");
    check(std::abs(out.getSample(1,960)-.25*(1-tone))<1e-7 && out.getSample(0,960)==0,"first feedback crosses channels with measured filter response");
    s.values[4]=0;s.values[0]=10.01f;s.values[2]=0;const auto fractional=run(a,s);const double t=s.values[0]*48;
    check(std::abs(fractional.getSample(0,(int)t)-(1-(t-std::floor(t)))) < 1e-6,"fractional delay distributes impulse between adjacent samples");
    s.values[0]=s.values[1]=10;s.values[6]=1;check(run(a,s).getMagnitude(0,1800)==0,"freeze rejects fresh input into an empty buffer");
}
void reverbTests()
{
    auto s=defaultEffectSettings(Kind::reverb);s.values[0]=0;s.values[5]=1;
    juce::AudioBuffer<float>a(2,8000);a.clear();a.setSample(0,0,1);auto out=run(a,s,8000);
    const int first=238;
    bool silent=true;for(int n=0;n<first;++n)silent=silent&&out.getSample(0,n)==0&&out.getSample(1,n)==0;
    check(silent && std::abs(out.getSample(0,first)-.0625)<1e-8 && std::abs(out.getSample(1,first)-.0625)<1e-8,"independent first room reflection timing and stereo weights");
    const double loss=std::pow(10.0,-3.0*first/(8000*2));const double tone=std::exp(-2*pi*.45);
    check(std::abs(out.getSample(0,2*first)-.046875*(1-tone)*loss)<1e-7,"first recirculation follows orthogonal feedback and T60 damping");
    s.values[0]=25;auto delayed=run(a,s,8000);bool shifted=true;for(int n=0;n<7800;++n)for(int c=0;c<2;++c)shifted=shifted&&std::abs(delayed.getSample(c,n+200)-out.getSample(c,n))<1e-7;
    check(shifted,"predelay shifts every reflection by the requested samples");
    s.values[0]=0;s.values[4]=0;auto mono=run(a,s,8000);bool equal=true;for(int n=0;n<8000;++n)equal=equal&&mono.getSample(0,n)==mono.getSample(1,n);check(equal,"zero room width is exactly mono");
    s.values[4]=1;s.values[1]=.1f;auto shortTail=run(a,s,8000);
    check(shortTail.getRMSLevel(0,4000,4000)<out.getRMSLevel(0,4000,4000)*.01f,"short decay measurably reduces late energy");
}
void lifecycleTests()
{
    auto input=signal(14000);
    for(int kind=0;kind<4;++kind)
    {
        auto s=defaultEffectSettings(static_cast<Kind>(kind));if(kind==0){s.values[2]=8;s.values[1]=400;}
        const auto expected=run(input,s);auto split=input;NativeEffectDSP dsp;dsp.prepare(48000,s.kind);dsp.configure(s);
        for(int offset=0;offset<split.getNumSamples();){const auto count=std::min(1+offset%311,split.getNumSamples()-offset);dsp.processSamples(split.getWritePointer(0,offset),split.getWritePointer(1,offset),count);offset+=count;}
        check(difference(split,expected)==0,"block partition invariance for "+juce::String(effectName(s.kind)));
        dsp.reset();split=input;dsp.processSamples(split.getWritePointer(0),split.getWritePointer(1),split.getNumSamples());check(difference(split,expected)==0,"reset restores deterministic output");
        auto bad=input;bad.setSample(0,0,std::numeric_limits<float>::quiet_NaN());bad.setSample(1,10,std::numeric_limits<float>::infinity());check(difference(run(bad,s),run(bad,s))==0,"nonfinite input does not poison state");
        s.values[kind==0?33:kind==1?6:kind==2?3:5]=0;check(difference(run(input,s),input)==0,"zero wet is exact dry signal");
    }
}
void writeAudio(const juce::File& file,const juce::AudioBuffer<float>& audio)
{
    juce::WavAudioFormat format;std::unique_ptr<juce::OutputStream>stream(file.createOutputStream());auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));check(writer&&writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"write generated audio");
}
juce::AudioBuffer<float> readAudio(const juce::File& file)
{
    juce::AudioFormatManager f;f.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>r(f.createReaderFor(file));check(r&&r->sampleRate==48000&&r->numChannels==2,"read rendered stereo audio");juce::AudioBuffer<float>a(2,(int)r->lengthInSamples);check(r->read(&a,0,a.getNumSamples(),0,true,true),"decode all output frames");return a;
}
juce::ValueTree effectState(juce::ValueTree root)
{
    if(root.getProperty("type").toString()==NativeEffectPlugin::xmlTypeName)return root;
    for(auto child:root)if(auto found=effectState(child);found.isValid())return found;return {};
}
void projectTests(const juce::File& root)
{
    const auto input=signal(20000);const auto file=root.getChildFile("source.wav");writeAudio(file,input);
    for(int type=0;type<4;++type)
    {
        const auto kind=static_cast<Kind>(type);Session s(false);ok(s.importAudio(file,0),"import effect fixture");ok(s.addNativeEffect(0,kind),"insert "+juce::String(effectName(kind)));
        const auto id=s.snapshot().tracks[0].plugins[0].id;auto settings=defaultEffectSettings(kind);
        const auto project=root.getChildFile("effect-"+juce::String(type)+".halftrack");
        ok(s.saveProject(project),"save default effect");ok(s.loadProject(project),"reopen default effect");
        check(s.snapshot().tracks[0].plugins[0].effect==settings,"default packet survives reopen");
        ok(s.setNativeEffectSettings(id,settings),"unchanged packet accepted");check(!s.canUndo()&&!s.snapshot().dirty,"unchanged packet does not dirty or invent history");
        auto invalid=settings;invalid.values[0]=std::numeric_limits<float>::quiet_NaN();check(s.setNativeEffectSettings(id,invalid).failed()&&!s.canUndo(),"invalid command preserves history");
        const auto fields=effectParameters(kind);
        for(size_t i=0;i<fields.size();++i)settings.values[i]=fields[i].choices?fields[i].maximum:fields[i].minimum+(fields[i].maximum-fields[i].minimum)*.37f;
        ok(s.setNativeEffectSettings(id,settings),"edit every device property");s.undo();check(s.snapshot().tracks[0].plugins[0].effect==defaultEffectSettings(kind),"one undo restores all properties");s.redo();
        ok(s.saveProject(project),"save all properties");ok(s.loadProject(project),"reopen all properties");check(s.snapshot().tracks[0].plugins[0].effect==settings,"every property round trips exactly");
        auto xml=juce::XmlDocument::parse(project);check(xml!=nullptr,"read disposable project XML");auto tree=juce::ValueTree::fromXml(*xml);effectState(tree).setProperty(fields[0].key,"nan",nullptr);const auto bad=root.getChildFile("bad.halftrack");check(tree.createXml()->writeTo(bad),"write malformed effect fixture");
        ok(s.setNativeEffectSettings(id,defaultEffectSettings(kind)),"make redo checkpoint");s.undo();check(s.loadProject(bad).failed()&&s.canRedo()&&s.snapshot().tracks[0].plugins[0].effect==settings,"malformed load retains current edit and redo");
        settings=defaultEffectSettings(kind);if(type==0)settings.values[2]=6;if(type==2){settings.values[0]=10;settings.values[1]=17;}
        ok(s.setNativeEffectSettings(id,settings),"configure audible effect");
        const auto expected=run(input,settings);const auto render=root.getChildFile("render-"+juce::String(type)+".wav");ok(s.exportWav(render),"render effect through engine");check(difference(readAudio(render),expected)<2e-6,"every exported sample matches the separately exercised processor");
        if (type == 2 || type == 3)
        {
            juce::AudioBuffer<float> padded (2, input.getNumSamples()+48000); padded.clear();
            for (int c=0;c<2;++c) padded.copyFrom(c,0,input,c,0,input.getNumSamples());
            const auto expectedTail=run(padded,settings);
            ok(s.exportWav(render,48000,24,1),"export full effect tail after clip end");
            const auto actualTail=readAudio(render);
            check(difference(actualTail,expectedTail)<2e-6 && actualTail.getMagnitude(input.getNumSamples(),48000)>1e-6,
                  "delay/reverb tail remains active beyond the last source frame");
        }
        ok(s.setPluginEnabled(0,0,false),"bypass effect");ok(s.exportWav(render),"render bypass");check(difference(readAudio(render),input)<2e-6,"bypass restores every dry sample");s.undo();
        ok(s.removePlugin(0,0),"remove effect");s.undo();ok(s.exportWav(render),"render undo removal");check(difference(readAudio(render),expected)<2e-6,"undo removal restores signal and identity");
    }
    Session chain(false);ok(chain.importAudio(file,0),"import chain source");ok(chain.addNativeEffect(0,Kind::equalizer),"insert EQ");ok(chain.addNativeEffect(0,Kind::compressor),"insert compressor");
    auto first=chain.snapshot().tracks[0].plugins[0];auto gain=first.effect;gain.values[32]=12;ok(chain.setNativeEffectSettings(first.id,gain),"set pre-compression gain");
    const auto one=root.getChildFile("gain-first.wav"),two=root.getChildFile("gain-last.wav");ok(chain.exportWav(one),"render gain before compressor");ok(chain.movePlugin(0,0,1),"move actual effect order");ok(chain.exportWav(two),"render gain after compressor");check(difference(readAudio(one),readAudio(two))>.01,"order changes nonlinear output");chain.undo();ok(chain.exportWav(two),"undo device order");check(difference(readAudio(one),readAudio(two))==0,"undo order restores bit-identical output");
    Session midi(false);ok(midi.addMidiTrack("Effect instrument"),"add MIDI track");const auto tracks=midi.snapshot().tracks;const int track=(int)tracks.size()-1;ok(midi.addMidiClip(tracks.back().id,0,1),"add MIDI fixture");const auto clip=midi.snapshot().tracks.back().clips[0].id;ok(midi.addMidiNote(clip,60,0,1,.6f),"add generated note");
    for(int k=0;k<4;++k)ok(midi.addNativeEffect(track,static_cast<Kind>(k)),"insert device after instrument");const auto result=root.getChildFile("midi-effects.wav");ok(midi.exportWav(result,48000,24,1),"render instrument through all four devices and tail");auto rendered=readAudio(result);check(rendered.getMagnitude(0,rendered.getNumSamples())>.001,"instrument chain remains audible");
    ok(midi.addReturnTrack("Effect return"),"create return");const int ret=(int)midi.snapshot().tracks.size()-1;for(int k=0;k<4;++k)ok(midi.addNativeEffect(ret,static_cast<Kind>(k)),"insert device after return input");
}
}
int runNativeEffectsTests(const juce::File& artifacts)
{
    try {const auto root=artifacts.getChildFile("effects-"+juce::Uuid().toString());ok(root.createDirectory(),"create fixtures");equalizerTests();compressorTests();delayTests();reverbTests();lifecycleTests();projectTests(root);runNativeEffectEditorTests(root);std::cout<<"Native effects passed: "<<root.getFullPathName()<<std::endl;return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL native effects: "<<e.what()<<std::endl;return 1;}
}
}
