#include "Session.h"
#include "MetronomeSounds.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace halftrack
{
void useHardClipEdges();
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice(Session&,int);
int64_t syntheticTestSamplePosition(Session&);
juce::AudioBuffer<float> captureSyntheticTestDevice(Session&,int);
namespace
{
namespace te=tracktion::engine;
void check(bool value,const juce::String& message)
{
    if(!value)throw std::runtime_error(message.toStdString());std::cout<<"PASS metronome: "<<message<<std::endl;
}
void ok(const juce::Result& result,const juce::String& message)
{check(result.wasOk(),message+(result.failed()?": "+result.getErrorMessage():""));}
void impulse(const juce::File& file,float gain)
{
    juce::AudioBuffer<float> audio(1,256);audio.clear();audio.setSample(0,0,gain);
    juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> output(file.createOutputStream());
    auto writer=wav.createWriterFor(output,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(1).withBitsPerSample(24));
    check(writer&&writer->writeFromAudioSampleBuffer(audio,0,256),"write generated click impulse");
}
struct Fixture
{
    Session session {false,makeSyntheticTestDeviceType()};
    te::Engine& engine=dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&>(session.audioDeviceManager()).engine;
    juce::String oldBig=engine.getPropertyStorage().getProperty(te::SettingID::clickTrackSampleBig).toString(),oldLittle=engine.getPropertyStorage().getProperty(te::SettingID::clickTrackSampleSmall).toString();
    void setClickFile(bool big,const juce::String& path)
    {
        engine.getPropertyStorage().setProperty(big?te::SettingID::clickTrackSampleBig:te::SettingID::clickTrackSampleSmall,path);
        te::TransportControl::restartAllTransports(engine,false);
    }
    explicit Fixture(const juce::File& root)
    {
        const auto big=root.getChildFile("big.wav"),little=root.getChildFile("little.wav");impulse(big,.5f);impulse(little,.25f);
        setClickFile(true,big.getFullPathName());setClickFile(false,little.getFullPathName());
        pumpSyntheticTestDevice(session,4);
    }
    ~Fixture(){session.stop();setClickFile(true,oldBig);setClickFile(false,oldLittle);}
};
void compareClicks(const juce::AudioBuffer<float>& audio,double tempo,float gain,bool accent,double loopSeconds=0,int countInBeats=-1,double leadSeconds=0,int beatsPerBar=4,double tickBeats=1)
{
    juce::AudioBuffer<float> expected(2,audio.getNumSamples());expected.clear();
    const double step=48000*60/tempo*tickBeats; // beatsPerBar and loops count ticks.
    for(int beat=0;beat*step+leadSeconds*48000<audio.getNumSamples();++beat)
    {
        if(countInBeats>=0 && beat>=countInBeats)break;
        const int sample=(int)std::llround(beat*step+leadSeconds*48000);
        if(sample>=audio.getNumSamples())break;
        const int musicalBeat=loopSeconds>0?beat%(int)std::llround(loopSeconds*tempo/60):beat;
        for(int c=0;c<2;++c)expected.setSample(c,sample,(accent && musicalBeat%beatsPerBar==0?.5f:.25f)*gain);
    }
    double worst=0;int worstAt=0;
    for(int c=0;c<2;++c)for(int n=0;n<audio.getNumSamples();++n)
    {
        if(!std::isfinite(audio.getSample(c,n)))throw std::runtime_error("Nonfinite click sample");
        const auto error=std::abs((double)audio.getSample(c,n)-expected.getSample(c,n));
        if(error>worst){worst=error;worstAt=n;}
    }
    check(worst<2e-6,"complete click buffer matches independent beat/accent/gain arithmetic; error="+juce::String(worst,8)+" at "+juce::String(worstAt));
}
void playback(const juce::File& root)
{
    Fixture f(root);auto& session=f.session;auto settings=session.snapshot().metronome;
    check(!settings.enabled && settings.accent && settings.gain==.6f && settings.countInBars==0,"new-project click defaults are explicit");
    ok(session.play(),"play with metronome disabled");check(captureSyntheticTestDevice(session,200).getMagnitude(0,51200)==0,"disabled metronome is silent");session.stop();
    for(double tempo:{120.0,137.0,187.5,500.0,999.0})
    {
        session.setTempo(tempo);settings.enabled=true;settings.accent=true;settings.gain=.6f;ok(session.setMetronome(settings),"enable accented metronome");
        session.seek(0);ok(session.play(),"start click playback");const auto audio=captureSyntheticTestDevice(session,400);session.stop();
        compareClicks(audio,tempo,.6f,true);
    }
    // Odd meters accent every bar of quarter-note beats.
    for(int beats:{3,5})
    {
        session.setTempo(120);ok(session.setTimeSignature(beats,4),"choose "+juce::String(beats)+"/4");
        session.seek(0);ok(session.play(),"play the click in "+juce::String(beats)+"/4");
        compareClicks(captureSyntheticTestDevice(session,400),120,.6f,true,0,-1,0,beats);session.stop();
    }
    ok(session.setTimeSignature(4,4),"return to 4/4");
    session.setTempo(120);settings.accent=false;settings.gain=.2f;ok(session.setMetronome(settings),"set unaccented quiet click");
    session.seek(0);ok(session.play(),"play unaccented click");compareClicks(captureSyntheticTestDevice(session,400),120,.2f,false);session.stop();
    settings.accent=true;settings.gain=1;ok(session.setMetronome(settings),"set accented unity click");session.setLoop(true,0,1);session.seek(0);ok(session.play(),"play across loop boundaries");
    compareClicks(captureSyntheticTestDevice(session,400),120,1,true,1);session.stop();session.setLoop(false,0,1);
    settings.recordingOnly=true;ok(session.setMetronome(settings),"select recording-only click");session.seek(0);ok(session.play(),"play with record-only setting");
    check(captureSyntheticTestDevice(session,200).getMagnitude(0,51200)==0,"record-only click is silent during ordinary playback");session.stop();
    // Rhythm, as the reference's: a division ticks at its length, Auto at the meter's denominator,
    // a bar once a bar, accenting each bar's first tick; a division that does not divide the bar
    // plays as Auto.
    settings.recordingOnly=false;settings.gain=1;settings.accent=true;
    const auto play=[&](float rhythm,int numerator,int denominator,double tick,int ticksPerBar,const juce::String& what)
    {
        ok(session.setTimeSignature(numerator,denominator),"choose "+juce::String(numerator)+"/"+juce::String(denominator));
        settings.rhythm=rhythm;ok(session.setMetronome(settings),"choose a rhythm");
        session.seek(0);ok(session.play(),"play "+what);
        compareClicks(captureSyntheticTestDevice(session,800),120,1,true,0,-1,0,ticksPerBar,tick);session.stop();
    };
    play(.5f,4,4,.5,8,"eighths in 4/4");
    play(1.0f/3,4,4,1.0/3,12,"eighth triplets in 4/4");
    play(-1,4,4,4,1,"one tick a bar in 4/4");
    play(0,6,8,.5,6,"Auto in 6/8, ticking eighths");
    play(1,6,8,1,3,"crotchets in 6/8");
    play(1,7,8,.5,7,"crotchets in 7/8, which do not divide its bar and play as Auto");
    ok(session.setTimeSignature(4,4),"back to 4/4");
    settings.rhythm=.3f;check(session.setMetronome(settings).failed(),"a value off the reference's list is refused");
    settings.rhythm=0;ok(session.setMetronome(settings),"back to Auto");
    settings.rhythm=.25f;ok(session.setMetronome(settings),"sixteenths");session.undo();
    check(session.snapshot().metronome.rhythm==0,"choosing a rhythm is one undo step");
}
// The Rhythm list and how it resolves, from the reference's code.
void rhythms()
{
    const auto list=Session::metronomeRhythms();
    juce::StringArray names;for(const auto& r:list)names.add(r.name);
    check(names.joinIntoString(",")=="Auto,1 Bar,1/2,1/2T,1/4,1/4T,1/8,1/8T,1/16,1/16T" && list[0].value==0 && list[1].value==-1 && list[3].value==4.0f/3,
          "the Rhythm menu lists Auto, 1 Bar and the divisions to sixteenth triplets, in crotchets");
    const auto fitting=[&](int n,int d){juce::StringArray f;for(const auto& r:list)if(r.value>0&&Session::metronomeRhythmFits(r.value,{n,d}))f.add(r.name);return f.joinIntoString(",");};
    check(fitting(6,8)=="1/4,1/8,1/8T,1/16,1/16T" && fitting(7,8)=="1/8,1/16,1/16T" && fitting(3,2)=="1/2,1/4,1/4T,1/8,1/8T,1/16,1/16T" && fitting(4,4)=="1/2,1/2T,1/4,1/4T,1/8,1/8T,1/16,1/16T",
          "only divisions that divide the bar fit: in 6/8, 7/8, 3/2 and 4/4 as the reference allows");
    check(Session::metronomeTickBeats(0,{6,8})==.5 && Session::metronomeTickBeats(0,{3,2})==2 && Session::metronomeTickBeats(-1,{6,8})==3 && Session::metronomeTickBeats(.5f,{7,8})==.5
              && Session::metronomeTickBeats(2,{6,8})==.5,
          "Auto ticks on the denominator, a bar on the bar, and a division that does not fit as Auto");
}
void exportExcludesClick(const juce::File& root)
{
    Fixture f(root);auto& session=f.session;ok(session.importAudio(root.getChildFile("big.wav"),0),"prepare audible export source");
    auto settings=session.snapshot().metronome;settings.enabled=true;settings.recordingOnly=false;settings.gain=1;
    ok(session.setMetronome(settings),"enable metronome before export");
    const auto withClick=root.getChildFile("click-on.wav"),withoutClick=root.getChildFile("click-off.wav");
    ok(session.exportWav(withClick),"export arrangement with monitoring click enabled");
    settings.enabled=false;ok(session.setMetronome(settings),"disable metronome");ok(session.exportWav(withoutClick),"export same arrangement without click");
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> a(formats.createReaderFor(withClick)),b(formats.createReaderFor(withoutClick));
    check(a&&b&&a->lengthInSamples==256&&b->lengthInSamples==256&&a->numChannels==2&&b->numChannels==2,"metronome does not extend export duration or channels");
    juce::AudioBuffer<float> actual(2,256),expected(2,256);check(a->read(&actual,0,256,0,true,true)&&b->read(&expected,0,256,0,true,true),"decode complete click-on/off exports");
    double worst=0;for(int c=0;c<2;++c)for(int n=0;n<256;++n)worst=std::max(worst,std::abs((double)actual.getSample(c,n)-expected.getSample(c,n)));
    check(actual.getMagnitude(0,256)>.1 && worst==0,"metronome is excluded from every exported sample");
}

void persistence(const juce::File& root)
{
    Session session(false);const auto project=root.getChildFile("click.halftrack");auto settings=session.snapshot().metronome;
    settings.enabled=true;settings.accent=false;settings.recordingOnly=true;settings.gain=.4f;settings.countInBars=2;
    ok(session.setMetronome(settings),"edit all metronome settings in one transaction");session.undo();
    check(!session.snapshot().metronome.enabled&&session.snapshot().metronome.countInBars==2,"one undo restores the click settings; the count-in, an application setting as the reference's, stays");
    auto invalid=settings;invalid.gain=std::numeric_limits<float>::quiet_NaN();check(session.setMetronome(invalid).failed()&&session.canRedo(),"nonfinite click level preserves redo");
    invalid=settings;invalid.countInBars=3;check(session.setMetronome(invalid).failed()&&session.canRedo(),"unsupported count-in preserves redo");
    session.redo();check(session.snapshot().metronome==settings,"redo restores exact click settings");
    ok(session.saveProject(project),"save click settings");ok(session.loadProject(project),"reopen click settings");check(session.snapshot().metronome==settings,"all click settings persist");
    check(!project.loadFileAsString().contains("halftrackCountInBars"),"the count-in is not kept with the project");
    ok(session.newProject(),"a new project");check(session.snapshot().metronome.countInBars==2,"and a new project keeps the application's count-in");
    {Session other(false);check(other.snapshot().metronome.countInBars==2&&other.preferences().countInBars==2,"as does the next launch");}
    ok(session.loadProject(project),"reopen the project");
    ok(session.setMetronome(settings),"unchanged click settings are a no-op");check(!session.canUndo()&&!session.snapshot().dirty,"unchanged settings leave clean history");
    for(const auto& value:juce::StringArray{"-1","3","nan","1.5"})
    {
        auto xml=juce::XmlDocument::parse(project);xml->getChildByName("EDIT")->setAttribute("halftrackCountInBars",value);
        const auto bad=root.getChildFile("bad-count-in.halftrack");check(xml->writeTo(bad),"write invalid count-in fixture");
        check(session.loadProject(bad).failed()&&session.snapshot().metronome==settings,"invalid count-in cannot replace the active project");
    }
    auto changed=settings;changed.enabled=false;ok(session.setMetronome(changed),"prepare redo for malformed-load check");session.undo();
    for(const auto& value:juce::StringArray{"nan","0.1","1.1","0.5 trailing"})
    {
        auto xml=juce::XmlDocument::parse(project);auto* edit=xml->getChildByName("EDIT");auto* click=edit->getChildByName("CLICKTRACK");
        check(click!=nullptr,"saved metronome uses native state");click->setAttribute("level",value);const auto bad=root.getChildFile("bad-click.halftrack");check(xml->writeTo(bad),"write invalid click fixture");
        check(session.loadProject(bad).failed()&&session.snapshot().metronome==settings&&session.canRedo(),"invalid click project preserves active settings and redo");
    }
}
// The metronome's own sounds: each tick is deterministic, peaks at its designed level, rings at its
// fundamental, starts and ends in silence, and the accent is higher; choosing one in the
// preferences points the engine's click track at its ticks.
double goertzel(const juce::AudioBuffer<float>& x,double hz,double rate)
{
    const double w=juce::MathConstants<double>::twoPi*hz/rate;double re=0,im=0;
    for(int n=0;n<x.getNumSamples();++n){re+=x.getSample(0,n)*std::cos(w*n);im-=x.getSample(0,n)*std::sin(w*n);}
    return std::sqrt(re*re+im*im);
}
void sounds(const juce::File& root)
{
    juce::ignoreUnused(root);
    for(int i=0;i<Session::metronomeSoundCount;++i)
    {
        const auto sound=static_cast<Session::MetronomeSound>(i);const auto name=Session::metronomeSoundName(sound);
        for(const bool accent:{false,true})
        {
            const auto a=metronome::tick(sound,accent,48000),b=metronome::tick(sound,accent,48000);
            bool same=a.getNumSamples()==b.getNumSamples();for(int n=0;same&&n<a.getNumSamples();++n)same=a.getSample(0,n)==b.getSample(0,n);
            const auto hz=metronome::tickFrequency(sound,accent);
            check(same && std::abs(a.getMagnitude(0,a.getNumSamples())-metronome::tickPeak(sound,accent))<1e-6f && a.getSample(0,0)==0 && a.getSample(0,a.getNumSamples()-1)==0,
                  name+(accent?" accent":"")+" is the same every time, peaks at its level, and starts and ends in silence");
            check(goertzel(a,hz,48000)>3*goertzel(a,hz*.6,48000) && goertzel(a,hz,48000)>3*goertzel(a,hz*1.7,48000),name+(accent?" accent":"")+" rings at "+juce::String(hz,0)+" Hz");
        }
        check(metronome::tickFrequency(sound,true)>metronome::tickFrequency(sound,false),name+"'s accent is higher than its plain tick");
    }
    Session s(false);
    te::Engine& engine=dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&>(s.audioDeviceManager()).engine;
    check(s.preferences().metronomeSound==Session::MetronomeSound::classic && engine.getPropertyStorage().getProperty(te::SettingID::clickTrackSampleBig).toString().contains("Halftrack Classic Accent"),
          "the metronome plays Classic at first, as the reference's does");
    auto p=s.preferences();p.metronomeSound=Session::MetronomeSound::wood;ok(s.setPreferences(p),"choose Wood");
    const juce::File accent(engine.getPropertyStorage().getProperty(te::SettingID::clickTrackSampleBig).toString()),plain(engine.getPropertyStorage().getProperty(te::SettingID::clickTrackSampleSmall).toString());
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    for(const auto& [file,isAccent]:{std::pair{accent,true},std::pair{plain,false}})
    {
        std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(file));
        check(r!=nullptr && file.getFileName().contains("Wood"),"the click track plays "+file.getFileName());
        juce::AudioBuffer<float> x(1,(int)r->lengthInSamples);r->read(&x,0,x.getNumSamples(),0,true,false);
        const auto expected=metronome::tick(Session::MetronomeSound::wood,isAccent,48000);double worst=x.getNumSamples()==expected.getNumSamples()?0:1;
        for(int n=0;worst<1&&n<x.getNumSamples();++n)worst=std::max(worst,(double)std::abs(x.getSample(0,n)-expected.getSample(0,n)));
        check(worst<1e-6,"its samples are the Wood tick's (error "+juce::String(worst,8)+")");
    }
    {
        Session reopened(false);
        check(reopened.preferences().metronomeSound==Session::MetronomeSound::wood,"the sound is kept with the preferences");
    }
    p.metronomeSound=Session::MetronomeSound::classic;ok(s.setPreferences(p),"back to Classic");
}
void countIn(const juce::File& root)
{
    for(double start:{0.0,6.0}) for(int bars:{0,1,2,4})
    {
        Fixture f(root);auto& session=f.session;auto settings=session.snapshot().metronome;settings.enabled=bars==0;settings.recordingOnly=bars==0;settings.countInBars=bars;settings.gain=1;
        ok(session.setMetronome(settings),"select count-in without ongoing metronome");
        const auto inputs=session.inputNames();check(!inputs.isEmpty(),"generated audio capture input exists");ok(session.setTrackInput(0,inputs[0]),"assign generated count-in recording input");ok(session.setTrackArmed(0,true),"arm count-in recording");
        session.seek(start);const auto inputStart=syntheticTestSamplePosition(session);ok(session.record(),"record with count-in");
        check(bars==0?session.snapshot().position==start:std::abs(session.snapshot().position-start+bars*2+.25)<1e-9,"recording starts at the exact native count-in boundary");
        check(session.snapshot().countInBeatsRemaining==bars*4,"countdown is available at zero and nonzero timeline locations");
        auto forbidden=settings;forbidden.countInBars=bars==0?1:0;check(session.setMetronome(forbidden).failed(),"active count-in cannot be changed mid-take");
        const int lead=bars>0?12000:0;
        const auto audio=captureSyntheticTestDevice(session,(bars*2*48000+lead+24000+255)/256);
        compareClicks(audio,120,1,true,0,bars>0?bars*4:-1,bars>0?.25:0);session.stop();
        const auto state=session.snapshot();check(state.tracks[0].clips.size()==1,"count-in produces one completed audio take");
        check(std::abs(state.tracks[0].clips[0].start-start)<1.0/48000 && state.tracks[0].clips[0].length>.45 && state.tracks[0].clips[0].length<.55,
            "pre-roll is excluded from recorded take placement and duration; start="+juce::String(state.tracks[0].clips[0].start,9)+" length="+juce::String(state.tracks[0].clips[0].length,9));
        juce::AudioFormatManager formats;formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(state.tracks[0].clips[0].file));
        check(reader && reader->sampleRate==48000 && reader->numChannels==2 && reader->bitsPerSample>=16,"decode generated count-in take");
        juce::AudioBuffer<float> recorded(2,(int)reader->lengthInSamples);check(reader->read(&recorded,0,recorded.getNumSamples(),0,true,true),"read entire recorded take");
        // The take begins with the input arriving as the recording's first block plays.
        const auto firstSample=inputStart+bars*2*48000+lead;double worst=0; int worstAt=0;
        for(int c=0;c<2;++c)for(int n=0;n<recorded.getNumSamples();++n)
        {
            const double expected=.2f*std::sin(juce::MathConstants<double>::twoPi*(firstSample+n)*(c==0?440.0:660.0)/48000);
            const double actual=recorded.getSample(c,n);if(!std::isfinite(actual))throw std::runtime_error("nonfinite recorded sample");
            const auto error=std::abs(actual-expected);if(error>worst){worst=error;worstAt=n;}
        }
        check(worst<std::ldexp(1.0,1-(int)reader->bitsPerSample)+1e-7,"every recorded frame starts at the intended input sample; error="+juce::String(worst,9)+" at "+juce::String(worstAt));
        session.undo();check(session.snapshot().tracks[0].clips.empty(),"count-in audio take undoes once");session.redo();
        check(session.snapshot().tracks[0].clips[0].id==state.tracks[0].clips[0].id,"count-in audio redo retains identity");
        ok(session.newProject(),"prepare canceled count-in");settings.countInBars=1;ok(session.setMetronome(settings),"enable count-in for early stop");
        ok(session.setTrackInput(0,inputs[0]),"reassign generated capture input");ok(session.setTrackArmed(0,true),"arm early-stop take");
        ok(session.record(),"begin count-in before early stop");pumpSyntheticTestDevice(session,100);session.stop();
        check(session.snapshot().tracks[0].clips.empty(),"stopping during count-in creates no audio clip");
        settings.countInBars=0;ok(session.setMetronome(settings),"no count-in for what follows");
    }
    // A bar counts in as long as the meter's bar: three crotchets in 6/8 and three and a half in 7/8.
    for(const auto [numerator,denominator,beats]:{std::tuple{6,8,3.0},std::tuple{7,8,3.5}})
    {
        Fixture f(root);auto& session=f.session;ok(session.setTimeSignature(numerator,denominator),"set the meter");
        auto settings=session.snapshot().metronome;settings.countInBars=1;ok(session.setMetronome(settings),"count in one bar");
        const auto inputs=session.inputNames();ok(session.setTrackInput(0,inputs[0]),"assign an input");ok(session.setTrackArmed(0,true),"arm");
        session.seek(0);ok(session.record(),"record with a one-bar count-in");
        check(std::abs(session.snapshot().position+beats*.5+.25)<1e-9 && session.snapshot().countInBeatsRemaining==beats,
              "a bar of "+juce::String(numerator)+"/"+juce::String(denominator)+" counts in "+juce::String(beats,1)+" beats ("+juce::String(session.snapshot().position,4)+" s)");
        session.stop();settings.countInBars=0;ok(session.setMetronome(settings),"no count-in for what follows");
    }
    {
        Fixture f(root);auto& session=f.session;auto settings=session.snapshot().metronome;
        settings.countInBars=3;check(session.setMetronome(settings).failed(),"three bars is not one of the reference's count-ins");
        settings.countInBars=4;ok(session.setMetronome(settings),"four bars is");
        settings.countInBars=0;ok(session.setMetronome(settings),"and none again");
    }
}
}
int runMetronomeTests(const juce::File& artifacts)
{
    try
    {
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
        const auto root=artifacts.getChildFile("metronome-"+juce::Uuid().toString());ok(root.createDirectory(),"create isolated metronome fixtures");
        for(double seconds:{-2.25,-1.0,-.25,0.0,.25,1.0,2.25})
            check(tracktion::graph::timeToSample(seconds,48000)==std::llround(seconds*48000),"graph sample conversion rounds signed time symmetrically");
        playback(root);persistence(root);countIn(root);exportExcludesClick(root);sounds(root);rhythms();
        std::cout<<"Metronome tests passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL metronome: "<<e.what()<<std::endl;return 1;}
}
}
