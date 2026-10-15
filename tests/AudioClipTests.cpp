#include "Session.h"
#include "AudioClipEditor.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace halftrack
{
namespace
{
void check(bool value,const juce::String& text){if(!value)throw std::runtime_error(text.toStdString());std::cout<<"PASS audio clips: "<<text<<std::endl;}
void ok(const juce::Result& r,const juce::String& text){check(r.wasOk(),text+(r.failed()?": "+r.getErrorMessage():""));}
juce::AudioBuffer<float> read(const juce::File& file)
{
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>r(formats.createReaderFor(file));
    check(r&&r->sampleRate==48000&&r->numChannels==2,"read rendered dimensions");juce::AudioBuffer<float>b(2,(int)r->lengthInSamples);
    check(r->read(&b,0,b.getNumSamples(),0,true,true),"decode complete render");return b;
}
void compare(const juce::AudioBuffer<float>& a,const juce::AudioBuffer<float>& b,const juce::String& text)
{
    check(a.getNumSamples()==b.getNumSamples(),text+" duration");double worst=0;
    for(int c=0;c<2;++c)for(int n=0;n<a.getNumSamples();++n)
    {if(!std::isfinite(a.getSample(c,n)))throw std::runtime_error("nonfinite rendered sample");worst=std::max(worst,std::abs((double)a.getSample(c,n)-b.getSample(c,n)));}
    check(worst<2e-6,text+" error "+juce::String(worst,9));
}
double shape(double x,int type)
{
    const auto a=x*juce::MathConstants<double>::halfPi;
    if(type==2)return std::sin(a);if(type==3)return 1-std::cos(a);if(type==4)return (1-x)*(1-std::cos(a))+x*std::sin(a);return x;
}
juce::AudioBuffer<float> expected(const juce::AudioBuffer<float>& source,const Session::AudioClipSettings& s,int offset=0,int frames=48000,int leading=0)
{
    juce::AudioBuffer<float>result(2,leading+frames);result.clear();const auto gain=std::pow(10.0,s.gainDb/20.0);
    for(int n=0;n<frames;++n)
    {
        double envelope=1;
        if(s.fadeInSeconds>0&&n<s.fadeInSeconds*48000)envelope*=shape(n/(s.fadeInSeconds*48000),(int)s.fadeInCurve);
        if(s.fadeOutSeconds>0&&n>frames-s.fadeOutSeconds*48000)envelope*=shape((frames-n)/(s.fadeOutSeconds*48000),(int)s.fadeOutCurve);
        for(int c=0;c<2;++c)result.setSample(c,leading+n,(float)(source.getSample(c,offset+n)*gain*envelope));
    }
    return result;
}
void clipboardTests(const juce::File& root,const juce::File& sourceFile,const juce::AudioBuffer<float>& source)
{
    Session session(false);
    const auto project=root.getChildFile("clipboard.halftrack"),render=root.getChildFile("clipboard-render.wav");
    ok(session.importAudio(sourceFile,0),"import clipboard source");
    const auto original=session.snapshot().tracks[0].clips[0];
    Session::AudioClipSettings settings{-6,.1,.05,Session::FadeCurve::concave,Session::FadeCurve::convex};
    ok(session.setAudioClipSettings(original.id,settings),"set clipboard gain and curves");
    ok(session.setAudioClipRange(original.id,.125,.5,.25),"set clipboard source range");
    ok(session.addAudioTrack("Destination"),"create clipboard destination track");
    ok(session.saveProject(project),"save clipboard baseline");ok(session.loadProject(project),"load clean clipboard baseline");
    const auto target=session.snapshot().tracks.back().id;
    juce::String pasted="unchanged";
    check(!session.hasAudioClipClipboard()&&session.pasteAudioClip(target,1,pasted).failed()&&pasted=="unchanged"&&!session.canUndo(),
        "empty clipboard fails without editing or publishing an ID");
    ok(session.copyAudioClip(original.id),"copy native audio state and independent media");
    check(session.hasAudioClipClipboard()&&!session.snapshot().dirty&&!session.canUndo(),"Copy leaves clean project and undo untouched");
    ok(session.setAudioClipSettings(original.id,{}),"change original after Copy");
    ok(session.pasteAudioClip(target,.75,pasted),"paste copied state on another audio track");
    auto copy=session.snapshot().tracks.back().clips[0];
    check(copy.id==pasted&&copy.id!=original.id&&copy.start==.75&&copy.length==.5&&copy.sourceOffset==.25&&copy.audio==settings,
        "Paste preserves the copied snapshot with fresh ID and selected placement");
    auto oracle=expected(source,settings,12000,24000,36000);
    const auto first=expected(source,{},12000,24000,6000);
    for(int c=0;c<2;++c)oracle.addFrom(c,0,first,c,0,first.getNumSamples());
    ok(session.exportWav(render),"render copied and edited originals");compare(read(render),oracle,"clipboard full-output oracle");
    session.undo();check(session.snapshot().tracks.back().clips.empty(),"one undo removes just the pasted clip");
    session.redo();check(session.snapshot().tracks.back().clips[0].id==pasted,"redo restores pasted identity");
    // A failed Cut must leave the successful copied snapshot available.
    check(session.copyAudioClip("missing",true).failed()&&session.hasAudioClipClipboard(),"failed Cut keeps prior clipboard");
    ok(session.copyAudioClip(pasted,true),"Cut retains media before removing clip");
    check(session.snapshot().tracks.back().clips.empty(),"Cut removes selected source only");
    session.undo();check(session.snapshot().tracks.back().clips[0].id==pasted,"Cut is one reversible edit");
    session.redo();check(session.snapshot().tracks.back().clips.empty(),"redo Cut removes the same clip");
    ok(session.addMidiTrack(),"create invalid MIDI paste target");ok(session.addReturnTrack(),"create invalid return paste target");
    session.undo(); // preserve a pending Redo while attempting invalid operations.
    const auto before=session.snapshot();juce::String unchanged="keep";
    for(double time:{-1.0,1e9,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        check(session.pasteAudioClip(target,time,unchanged).failed()&&session.canRedo()&&unchanged=="keep", "invalid paste position preserves redo and output ID");
    check(session.pasteAudioClip(before.tracks.back().id,0,unchanged).failed()&&session.pasteAudioClip("missing",0,unchanged).failed()&&session.canRedo(),
        "invalid track paste preserves redo");
    session.redo();check(session.pasteAudioClip(session.snapshot().tracks.back().id,0,unchanged).failed(),"return rejects audio paste");
    // Remove only this test's collected media, then replace the entire project.
    const auto oldMedia=session.snapshot().tracks[0].clips[0].file;
    ok(session.newProject(),"replace project with clipboard retained");check(oldMedia.deleteFile(),"remove obsolete generated project media");
    ok(session.pasteAudioClip(session.snapshot().tracks[0].id,.25,pasted),"paste after original project and media are gone");
    const auto crossProject=expected(source,settings,12000,24000,12000);
    ok(session.exportWav(render),"render cross-project paste");compare(read(render),crossProject,"retained clipboard media preserves source samples");
    const auto saved=root.getChildFile("Clipboard moved").getChildFile("portable.halftrack");
    ok(session.saveProject(saved),"collect clipboard media on Save As");
    const auto portableMedia=session.snapshot().tracks[0].clips[0].file;
    check(portableMedia.isAChildOf(saved.getSiblingFile("portable Assets")),"pasted media belongs to destination assets");
    ok(session.copyAudioClip(pasted),"replace clipboard without breaking pasted media");
    ok(session.loadProject(saved),"reopen pasted project");ok(session.exportWav(render),"render portable clipboard project");
    compare(read(render),crossProject,"save and reopen preserve pasted sound");
    ok(session.copyAudioClip(pasted,true),"cut reopened clip");
    ok(session.saveProject(saved),"save cut project with retained undo state");session.undo();
    check(session.snapshot().tracks[0].clips[0].id==pasted,"undo after saving restores cut clip identity");
    ok(session.exportWav(render),"render restored Cut after Save");compare(read(render),crossProject,"cut media survives save and undo");
}
juce::ValueTree audioState(juce::ValueTree tree)
{
    if(tree.hasType("AUDIOCLIP"))return tree;for(auto child:tree)if(auto found=audioState(child);found.isValid())return found;return {};
}
}
int runAudioClipTests(const juce::File& artifacts)
{
    try
    {
        const auto root=artifacts.getChildFile("audio-clips-"+juce::Uuid().toString());ok(root.createDirectory(),"create fixtures");
        const auto file=root.getChildFile("source.wav"),render=root.getChildFile("render.wav"),project=root.getChildFile("session.halftrack");
        juce::AudioBuffer<float>source(2,48000);
        for(int n=0;n<48000;++n)for(int c=0;c<2;++c)source.setSample(c,n,(float)((n*(37+c*12))%193-96)/1024);
        {juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream>stream(file.createOutputStream());auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));check(writer&&writer->writeFromAudioSampleBuffer(source,0,48000),"write original signal");}
        juce::MemoryBlock originalBytes;check(file.loadFileAsData(originalBytes),"read original source bytes");source=read(file);
        Session s(false);ok(s.importAudio(file,0),"import immutable source");const auto id=s.snapshot().tracks[0].clips[0].id;
        ok(s.saveProject(project),"save baseline");ok(s.loadProject(project),"reopen clean baseline");
        auto defaults=s.snapshot().tracks[0].clips[0].audio;ok(s.setAudioClipSettings(id,defaults),"identical settings no-op");check(!s.snapshot().dirty&&!s.canUndo(),"identical settings preserve clean history");
        Session::AudioClipSettings settings;
        for(int type=1;type<=4;++type)for(float gain:{-12.0f,12.0f})
        {
            settings={gain,.25,.125,(Session::FadeCurve)type,(Session::FadeCurve)type};ok(s.setAudioClipSettings(id,settings),"set fade curves and gain");
            ok(s.exportWav(render),"render every fade curve");compare(read(render),expected(source,settings),"independent complete-file fade/gain oracle");
        }
        s.undo();s.redo();check(s.snapshot().tracks[0].clips[0].audio==settings,"undo/redo restores complete packet");
        const auto beforeSwap=settings;settings.fadeInSeconds=.125;settings.fadeOutSeconds=.25;
        ok(s.setAudioClipSettings(id,settings),"swap fade lengths without rescaling");check(s.snapshot().tracks[0].clips[0].audio==settings,"both requested fade durations retained exactly");s.undo();check(s.snapshot().tracks[0].clips[0].audio==beforeSwap,"one undo restores both fades");s.redo();
        ok(s.saveProject(project),"save complete clip settings");ok(s.loadProject(project),"reopen all gain/fade properties");check(s.snapshot().tracks[0].clips[0].audio==settings,"all clip properties round trip");
        ok(s.setAudioClipSettings(id,defaults),"make redo checkpoint");s.undo();check(s.canRedo(),"redo checkpoint available");
        for(int bad=0;bad<6;++bad)
        {
            auto invalid=settings;
            if(bad==0)invalid.gainDb=std::numeric_limits<float>::quiet_NaN();if(bad==1)invalid.gainDb=25;
            if(bad==2)invalid.fadeInSeconds=-1;if(bad==3)invalid.fadeOutSeconds=1;if(bad==4)invalid.fadeInCurve=(Session::FadeCurve)0;if(bad==5)invalid.fadeOutSeconds=std::numeric_limits<double>::infinity();
            check(s.setAudioClipSettings(id,invalid).failed()&&s.canRedo()&&s.snapshot().tracks[0].clips[0].audio==settings,"invalid settings preserve audio state and redo");
        }
        for(const auto& key:juce::StringArray{"gain","fadeIn","fadeOut","fadeInType","fadeOutType"})
        {
            auto xml=juce::XmlDocument::parse(project);auto tree=juce::ValueTree::fromXml(*xml);auto clip=audioState(tree);check(clip.isValid(),"find saved audio state");clip.setProperty(key,"nan",nullptr);
            const auto malformed=root.getChildFile("malformed.halftrack");check(tree.createXml()->writeTo(malformed),"write invalid saved state");
            check(s.loadProject(malformed).failed()&&s.canRedo()&&s.snapshot().tracks[0].clips[0].audio==settings,"malformed load preserves existing edit and history");
        }
        settings={-6,.125,.125,Session::FadeCurve::linear,Session::FadeCurve::convex};ok(s.setAudioClipSettings(id,settings),"set trim envelope");
        ok(s.setAudioClipRange(id,.25,.5,.125),"trim and slip without modifying media");ok(s.exportWav(render),"export trimmed source");
        compare(read(render),expected(source,settings,6000,24000,12000),"trim uses exact source offset and duration");
        s.undo();auto clip=s.snapshot().tracks[0].clips[0];check(clip.start==0&&clip.length==1&&clip.sourceOffset==0,"one undo restores entire range");s.redo();
        check(s.setAudioClipRange(id,.25,.1,.125).failed()&&s.setAudioClipRange(id,0,.5,.75).failed(),"range rejects fade truncation and source overrun");
        const auto trimmed=read(render);juce::String right="unchanged";
        check(s.splitAudioClip(id,.3,right).failed()&&right=="unchanged","split through fade rejected before publishing identity");
        ok(s.splitAudioClip(id,.5,right),"split valid interior");check(right!=id&&s.snapshot().tracks[0].clips.size()==2,"split creates distinct native identity");
        ok(s.exportWav(render),"render split clips");compare(read(render),trimmed,"split preserves every rendered sample including cut");
        s.undo();check(s.snapshot().tracks[0].clips.size()==1,"one undo restores unsplit clip");s.redo();check(s.snapshot().tracks[0].clips[1].id==right,"redo restores split identity");
        ok(s.saveProject(project),"save split source references");ok(s.loadProject(project),"reopen split project");ok(s.exportWav(render),"render reopened split");compare(read(render),trimmed,"reopened split is identical");
        juce::String duplicate;const auto before=s.snapshot().tracks[0].clips[1];ok(s.duplicateAudioClip(right,duplicate),"duplicate selected half");auto clips=s.snapshot().tracks[0].clips;
        check(clips.size()==3&&clips[2].id==duplicate&&duplicate!=right&&clips[2].start==.75&&clips[2].sourceOffset==before.sourceOffset&&clips[2].audio==before.audio,"duplicate preserves source offset, gain, fades and fresh identity");
        auto duplicated=expected(source,before.audio,18000,12000,36000);for(int c=0;c<2;++c)duplicated.copyFrom(c,0,trimmed,c,0,trimmed.getNumSamples());
        ok(s.exportWav(render),"render duplicate");compare(read(render),duplicated,"duplicate adds the expected source region");s.undo();check(s.snapshot().tracks[0].clips.size()==2,"duplicate undoes separately");
        const auto moved=root.getChildFile("Relocated").getChildFile("copy.halftrack");ok(s.saveProject(moved),"save split with duplicate retained in undo history");s.redo();ok(s.exportWav(render),"redo duplicate after Save As");compare(read(render),duplicated,"redo keeps immutable media available after Save As");
        choc::buffer::InterleavedBuffer<float> memory(2,48000);
        for(unsigned int c=0;c<2;++c)for(unsigned int n=0;n<48000;++n)memory.getSample(c,n)=.125f;
        Session slots(false);ok(slots.importAudioToSlot(file,0,0),"import audio slot");auto slot=slots.snapshot().tracks[0].slots[0].clip;
        check(!slot.canEditAudioFades&&slots.setAudioClipSettings(slot.id,settings).failed(),"slot rejects manual fades that its playback graph does not process");
        settings=slot.audio;settings.gainDb=-6;ok(slots.setAudioClipSettings(slot.id,settings),"edit actual slot gain");juce::String rejected="keep";
        check(slots.setAudioClipRange(slot.id,0,.5,0).failed()&&slots.splitAudioClip(slot.id,.5,rejected).failed()&&slots.duplicateAudioClip(slot.id,rejected).failed()&&rejected=="keep","structural edits cannot evict slot clips");
        const auto slotFile=root.getChildFile("slot.halftrack");ok(slots.saveProject(slotFile),"save slot gain");ok(slots.loadProject(slotFile),"load slot gain");check(slots.snapshot().tracks[0].slots[0].clip.audio==settings,"slot settings round trip");
        // Independent overlap oracle sums two source regions with complementary linear envelopes.
        Session overlap(false);ok(overlap.importAudio(file,0),"import crossfade first clip");ok(overlap.importAudio(file,0,.75),"import overlapping clip");
        auto pair=overlap.snapshot().tracks[0].clips;Session::AudioClipSettings fadeA,fadeB;fadeA.fadeOutSeconds=.25;fadeB.fadeInSeconds=.25;
        ok(overlap.setAudioClipSettings(pair[0].id,fadeA),"set crossfade outgoing edge");ok(overlap.setAudioClipSettings(pair[1].id,fadeB),"set crossfade incoming edge");
        auto sum=expected(source,fadeB,0,48000,36000);const auto first=expected(source,fadeA);
        for(int c=0;c<2;++c)sum.addFrom(c,0,first,c,0,48000);
        ok(overlap.exportWav(render),"export overlapping fades");compare(read(render),sum,"overlap crossfade matches full-buffer summation");
        // Exercise actual Session graph gain with synchronous generated source readers.
        tracktion::Engine* engine=nullptr;for(auto* e:tracktion::Engine::getEngines())if(&e->getDeviceManager().deviceManager==&slots.audioDeviceManager())engine=e;
        check(engine!=nullptr,"find slot test engine");
        auto slotPath=slots.snapshot().tracks[0].slots[0].clip.file;
        engine->getAudioFileManager().registerMemoryBuffer(slotPath.getFullPathName().toStdString(),memory.getView(),48000);
        tracktion::HostedAudioDeviceInterface::Parameters params;params.sampleRate=48000;params.blockSize=128;params.inputChannels=0;params.outputChannels=2;params.useMidiDevices=false;
        {
            tracktion::test_utilities::EnginePlayer player(*engine,params);
            for(auto* edit:engine->getActiveEdits().getEdits())edit->dispatchPendingUpdatesSynchronously();
            ok(slots.setLaunchQuantisation(Session::LaunchQuantisation::none),"set immediate slot launch");ok(slots.play(),"start slot transport");ok(slots.launchSlot(0,0),"launch gain-adjusted slot");
            const auto captured=player.process(64000);double worst=0;const auto target=.125*std::pow(10.0,-6.0/20);
            // Allow the native first-block startup fade, but never a gain overshoot.
            for(int c=0;c<2;++c)for(int n=0;n<captured.getNumSamples();++n)
            {
                const auto sample=captured.getSample(c,n);
                if(!std::isfinite(sample)||std::abs(sample)>target+2e-6)throw std::runtime_error("slot startup exceeds its saved gain");
                if(n>=128)worst=std::max(worst,std::abs(sample-target));
            }
            check(worst<2e-6,"saved slot gain applies from the first complete block through source-loop boundaries");slots.stop();
        }
        // Native warp metadata must never be treated as ordinary source seconds.
        auto warpTree=juce::ValueTree::fromXml(*juce::XmlDocument::parse(project));
        auto warped=audioState(warpTree);warped.setProperty("warpTime",true,nullptr);
        const auto warpedFile=root.getChildFile("warped.halftrack");check(warpTree.createXml()->writeTo(warpedFile),"write generated native warp fixture");
        ok(s.loadProject(warpedFile),"load native warp metadata");
        const auto warpedId=s.snapshot().tracks[0].clips[0].id;juce::String untouched="keep";
        check(!s.snapshot().tracks[0].clips[0].canEditAudioRange&&s.setAudioClipRange(warpedId,0,.25,0).failed()
            &&s.splitAudioClip(warpedId,.125,untouched).failed()&&untouched=="keep"&&!s.canUndo(),"warped range operations preserve state and history");
        juce::MemoryBlock finalBytes;check(file.loadFileAsData(finalBytes)&&finalBytes==originalBytes,"all edits leave source file bytes unchanged");
        clipboardTests(root,file,source);
        runAudioClipEditorTests(file,root);std::cout<<"Audio clip tests passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL audio clips: "<<e.what()<<std::endl;return 1;}
}
}
