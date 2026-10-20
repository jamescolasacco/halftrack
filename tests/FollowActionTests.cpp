#include "Session.h"
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
namespace te = tracktion;
using Action = Session::FollowAction;
void check (bool condition, const juce::String& message)
{
    if (! condition) throw std::runtime_error (message.toStdString());
    std::cout << "PASS follow action: " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{ check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : "")); }
te::Engine& engineFor (Session& session)
{
    for (auto* engine : te::Engine::getEngines())
        if (&engine->getDeviceManager().deviceManager == &session.audioDeviceManager()) return *engine;
    throw std::runtime_error ("Missing test engine");
}
te::HostedAudioDeviceInterface::Parameters parameters()
{
    te::HostedAudioDeviceInterface::Parameters result;
    result.sampleRate = 48000; result.blockSize = 128; result.inputChannels = 0; result.outputChannels = 2; result.useMidiDevices = false;
    return result;
}
void level (const juce::AudioBuffer<float>& output, int channel, int begin, int count, float expected, const juce::String& message)
{
    double error = 0;
    for (int i = begin; i < begin + count; ++i)
    {
        const double actual = output.getSample (channel, i);
        if (! std::isfinite (actual)) throw std::runtime_error ("Nonfinite follow output");
        error = std::max (error, std::abs (actual - expected));
    }
    check (error < 1e-5, message + "; error=" + juce::String (error, 9));
}
struct Fixture
{
    // Registered native reader storage must outlive every graph and the engine.
    std::vector<std::unique_ptr<choc::buffer::InterleavedBuffer<float>>> buffers;
    Session session { false };
    te::Engine& engine = engineFor (session);
    te::test_utilities::EnginePlayer player { engine, parameters() };
    std::array<juce::String, 3> ids;
    static constexpr std::array<int, 3> scenes { 0, 2, 5 };
    static constexpr std::array<float, 3> values { .125f, .25f, .375f };
    juce::File firstFile;
    explicit Fixture (const juce::File& root, bool patterned = false)
    {
        for (int n = 0; n < 3; ++n)
        {
            const auto file = root.getChildFile (juce::Uuid().toString() + ".wav");
            juce::AudioBuffer<float> pcm (2, 12000);
            auto memory = std::make_unique<choc::buffer::InterleavedBuffer<float>> (2, 12000);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 12000; ++i)
            {
                const float value = patterned && n == 0 && i >= 6000 ? .0625f : values[(size_t) n];
                pcm.setSample (c, i, value); memory->getSample (c, i) = value;
            }
            {
                juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
                check (writer && writer->writeFromAudioSampleBuffer (pcm, 0, 12000), "write generated transition source");
            }
            ok (session.importAudioToSlot (file, 0, scenes[(size_t) n]), "populate separated Session clips");
            const auto clip = session.snapshot().tracks[0].slots[(size_t) scenes[(size_t) n]].clip;
            ids[(size_t) n] = clip.id;
            engine.getAudioFileManager().registerMemoryBuffer (clip.file.getFullPathName().toStdString(), memory->getView(), 48000);
            buffers.push_back (std::move (memory));
            if (n == 0) firstFile = file;
        }
        ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "use immediate initial launch");
    }
    Session::FollowSettings settings (int index = 0) const
    { return session.snapshot().tracks[0].slots[(size_t) scenes[(size_t) index]].clip.follow; }
};
void transitions (const juce::File& root)
{
    struct Case { Action action; int from, to; };
    for (const auto test : { Case { Action::off, 0, 0 }, { Action::stop, 0, -1 }, { Action::replay, 0, 0 },
        { Action::previous, 1, 0 }, { Action::next, 0, 1 }, { Action::first, 2, 0 }, { Action::last, 0, 2 },
        { Action::cycle, 2, 0 }, { Action::next, 2, -1 }, { Action::previous, 0, -1 } })
    {
        Fixture f (root);
        ok (f.session.setClipFollowAction (f.ids[(size_t) test.from], { test.action, test.action == Action::off ? 0.0 : 1.0 }), "configure deterministic transition");
        ok (f.session.launchSlot (0, Fixture::scenes[(size_t) test.from]), "launch source through production facade");
        const auto output = f.player.process (48000);
        level (output, 0, 1024, 22000, Fixture::values[(size_t) test.from], "source remains audible before one beat");
        level (output, 0, 25000, 22000, test.to < 0 ? 0 : Fixture::values[(size_t) test.to], "correct exclusive destination after one beat");
        const auto state = f.session.snapshot();
        for (int i = 0; i < 3; ++i)
            check (state.tracks[0].slots[(size_t) Fixture::scenes[(size_t) i]].playing == (i == test.to), "native launch state agrees with measured audio");
        check (state.playing && state.tracks[0].launcherActive, "follow transition preserves transport and Session ownership");
        f.session.stop();
    }
    Fixture replay (root, true);
    ok (replay.session.setClipFollowAction (replay.ids[0], { Action::replay, .75 }), "retrigger at a fractional beat distinct from natural loop boundary");
    ok (replay.session.launchSlot (0, 0), "launch phase-sensitive replay fixture");
    const auto output = replay.player.process (33000);
    level (output, 0, 19000, 4000, .125f, "replay restarts source phase at the chosen beat");
    level (output, 0, 25000, 4000, .0625f, "replayed second half has the expected source phase");
    replay.session.stop();
}
void runningEditsAndTempo (const juce::File& root)
{
    Fixture f (root);
    ok (f.session.importAudioToSlot (f.firstFile, 1, 0), "add independent sounding track");
    const auto companionFile = f.session.snapshot().tracks[1].slots[0].clip.file;
    f.engine.getAudioFileManager().registerMemoryBuffer (companionFile.getFullPathName().toStdString(), f.buffers[0]->getView(), 48000);
    ok (f.session.setTrackPan (0, -1), "isolate follow track left");
    ok (f.session.setTrackPan (1, 1), "isolate independent track right");
    ok (f.session.setClipFollowAction (f.ids[0], { Action::next, 2 }), "initial running follow target");
    ok (f.session.launchSlot (1, 0), "launch unaffected companion");
    ok (f.session.launchSlot (0, 0), "launch editable source");
    const auto baseline = f.player.process (4096);
    level (baseline, 0, 1024, 3000, .125f * std::sqrt (2.0f), "known native full-pan law before follow edit");
    level (baseline, 1, 1024, 3000, .125f * std::sqrt (2.0f), "independent native reader is audible before editing");
    ok (f.session.setClipFollowAction (f.ids[0], { Action::stop, 1 }), "change follow while playing");
    f.session.undo(); f.session.redo();
    auto output = f.player.process (50000);
    level (output, 0, 1024, 12000, .125f * std::sqrt (2.0f), "running history keeps source before edited deadline");
    level (output, 0, 30000, 18000, 0, "running redo publishes new stop action to the graph");
    level (output, 1, 1024, 48000, .125f * std::sqrt (2.0f), "running follow history leaves the other track audible");
    f.session.stop(); f.session.seek (0);
    ok (f.session.launchSlot (0, 0), "restart stopped source"); f.player.process (4096);
    ok (f.session.setClipFollowAction (f.ids[0], {}), "disable follow while playing");
    f.session.undo(); f.session.redo(); output = f.player.process (60000);
    level (output, 0, 1024, 58000, .125f * std::sqrt (2.0f), "disabling and redoing cancels the prior automatic stop");
    f.session.stop();
    Fixture queued (root);
    ok (queued.session.setClipFollowAction (queued.ids[0], { Action::next, 1 }), "one-beat follow after a quantized start");
    ok (queued.session.setLaunchQuantisation (Session::LaunchQuantisation::bar), "queue initial launch on the next bar");
    ok (queued.session.play(), "start transport before queuing"); queued.player.process (4800);
    ok (queued.session.launchSlot (0, 0), "queue future bar launch");
    const auto audio = queued.player.process (136000);
    level (audio, 0, 0, 90000, 0, "no source sound before quantized initial launch");
    level (audio, 0, 93000, 20000, .125f, "source begins at queued bar");
    level (audio, 0, 117000, 18000, .25f, "follow counts from actual launch and does not round to another bar");
    queued.session.stop();
}
void historyAndPersistence (const juce::File& root)
{
    Fixture f (root);
    const auto original = f.session.snapshot().tracks[0].slots[0].clip;
    ok (f.session.setClipFollowAction (f.ids[0], { Action::next, 1.5 }), "set follow timing");
    check (f.settings() == Session::FollowSettings { Action::next, 1.5 }, "snapshot exposes stored action and beats");
    f.session.undo(); check (f.settings() == Session::FollowSettings {}, "one undo restores disabled follow");
    check (f.session.canRedo(), "follow redo is available");
    ok (f.session.setClipFollowAction (f.ids[0], {}), "already-disabled follow is a no-op");
    check (f.session.setClipFollowAction (f.ids[0], { Action::next, std::numeric_limits<double>::quiet_NaN() }).failed()
        && f.session.setClipFollowAction (f.ids[0], { Action::next, 0 }).failed()
        && f.session.setClipFollowAction (f.ids[0], { Action::off, 1 }).failed()
        && f.session.setClipFollowAction (f.ids[0], { static_cast<Action> (99), 1 }).failed()
        && f.session.setClipFollowAction ("missing", { Action::next, 1 }).failed()
        && f.session.canRedo(), "invalid changes and no-op preserve redo");
    f.session.redo(); check (f.settings() == Session::FollowSettings { Action::next, 1.5 }, "redo restores exact settings");
    f.session.undo();
    ok (f.session.setClipFollowAction (f.ids[0], { Action::last, 2 }), "new edit after undo uses attached native action state");
    check (f.settings() == Session::FollowSettings { Action::last, 2 } && ! f.session.canRedo(), "branched history retains actual attached action");
    const auto current = f.session.snapshot().tracks[0].slots[0].clip;
    check (current.id == original.id && current.file == original.file && current.length == original.length
        && current.active == original.active, "follow edits preserve identity, source, extent and activation");
    juce::String copied; ok (f.session.duplicateScene (f.session.snapshot().scenes[0].id, copied), "copy scene with follow settings");
    check (f.session.snapshot().tracks[0].slots[1].clip.follow == current.follow, "independent copied clip retains follow behavior");
    f.session.undo();
    const auto project = root.getChildFile ("follow.halftrack");
    ok (f.session.saveProject (project), "save follow settings"); ok (f.session.loadProject (project), "reopen follow settings");
    check (f.settings() == Session::FollowSettings { Action::last, 2 }, "follow settings survive portable project reopening");
    ok (f.session.renameTrack (0, "Preserve undo"), "prepare history before invalid loads"); f.session.undo();
    const auto mutate = [&] (auto change)
    {
        auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "read disposable saved project");
        const auto find = [&] (auto&& self, juce::XmlElement& node) -> juce::XmlElement*
        {
            if (node.hasAttribute ("followActionBeats") && node.getDoubleAttribute ("followActionBeats") > 0) return &node;
            for (auto* child = node.getFirstChildElement(); child; child = child->getNextElement()) if (auto* found = self (self, *child)) return found;
            return nullptr;
        };
        auto* clip = find (find, *xml); check (clip != nullptr, "locate follow state in versioned project"); change (*clip);
        const auto bad = root.getChildFile ("invalid-follow.halftrack"); check (xml->writeTo (bad), "write malformed disposable project");
        check (f.session.loadProject (bad).failed() && f.session.canRedo()
            && f.settings() == Session::FollowSettings { Action::last, 2 }, "invalid follow state preserves project and redo");
    };
    mutate ([] (auto& clip) { clip.setAttribute ("followActionBeats", "nan"); });
    mutate ([] (auto& clip) { clip.setAttribute ("followActionBeats", "1oops"); });
    mutate ([] (auto& clip) { clip.setAttribute ("followActionDurationType", "unknown"); });
    mutate ([] (auto& clip) { clip.getChildByName ("FOLLOWACTIONS")->getFirstChildElement()->setAttribute ("type", "unknown"); });
    mutate ([] (auto& clip) { clip.getChildByName ("FOLLOWACTIONS")->getFirstChildElement()->setAttribute ("weight", "nan"); });
    mutate ([] (auto& clip) { auto* actions = clip.getChildByName ("FOLLOWACTIONS"); actions->addChildElement (new juce::XmlElement (*actions->getFirstChildElement())); });
    ok (f.session.importAudio (f.firstFile, 1), "prepare Arrangement rejection");
    check (f.session.setClipFollowAction (f.session.snapshot().tracks[1].clips[0].id, { Action::next, 1 }).failed(), "Arrangement clips reject Session-only commands");
    bool guarded = false;
    juce::Timer::callAfterDelay (10, [&] { guarded = f.session.setClipFollowAction (f.ids[0], {}).failed(); f.session.cancelExport(); });
    const auto cancelled = f.session.exportWav (root.getChildFile ("cancelled.wav"), 48000, 24, 60);
    check (cancelled.failed() && guarded && f.settings() == Session::FollowSettings { Action::last, 2 }, "export protects active follow configuration");
}
void midiTransition (double tempo)
{
    Session s (false); s.setTempo (tempo); auto& engine = engineFor (s); te::test_utilities::EnginePlayer player (engine, parameters());
    ok (s.addMidiTrack(), "create MIDI follow track"); const auto track = s.snapshot().tracks.back();
    auto synth = track.synth; synth.waveform = 2; synth.attackSeconds = synth.decaySeconds = 0;
    synth.sustain = 1; synth.releaseSeconds = .01f; synth.cutoffHz = 20000;
    ok (s.setSynthSettings (track.id, synth), "configure measured MIDI follow synth");
    ok (s.addMidiClipToSlot (track.id, 0, 4), "create held-note source");
    ok (s.addMidiClipToSlot (track.id, 1, 4), "create silent destination");
    const auto id = s.snapshot().tracks[2].slots[0].clip.id;
    ok (s.addMidiNote (id, 69, 0, 4, 1), "hold note across follow boundary");
    ok (s.setClipFollowAction (id, { Action::next, 1 }), "follow from held MIDI note to empty MIDI clip");
    ok (s.setLaunchQuantisation (Session::LaunchQuantisation::none), "immediate MIDI start");
    ok (s.launchSlot (2, 0), "launch held MIDI source");
    const int boundary = (int) std::round (48000 * 60 / tempo);
    const auto output = player.process (boundary + 24000);
    check (output.getMagnitude (0, 1024, boundary - 2048) > .01f, "source MIDI voice is audible before the project-tempo transition");
    level (output, 0, boundary + 2000, 21000, 0, "follow boundary releases held source voice at project tempo without a stuck note");
    check (s.snapshot().tracks[2].slots[1].playing, "silent MIDI destination genuinely owns playback");
    s.stop();
}
}
// Actions A and B with a chance: 0% and 100% are deterministic, 50% must reach both
// destinations over repeated launches, and Other never replays the same clip.
void chances (const juce::File& root)
{
    const auto destination = [] (Fixture& f)
    {
        const auto state = f.session.snapshot();
        for (int i = 0; i < 3; ++i) if (state.tracks[0].slots[(size_t) Fixture::scenes[(size_t) i]].playing) return i;
        return -1;
    };
    for (const double chance : { 0.0, 1.0 })
    {
        Fixture f (root);
        ok (f.session.setClipFollowAction (f.ids[0], { Action::next, 1, Action::last, chance }), "next as A and last as B");
        check (f.settings() == Session::FollowSettings { Action::next, 1, Action::last, chance }, "actions A and B and the chance are reported");
        ok (f.session.launchSlot (0, Fixture::scenes[0]), "launch the first clip");
        const auto output = f.player.process (48000);
        const int expected = chance == 1 ? 1 : 2;
        level (output, 0, 25000, 22000, Fixture::values[(size_t) expected], chance == 1 ? "a 100% chance always plays A" : "a 0% chance always plays B");
        f.session.stop();
    }
    {
        Fixture f (root);
        ok (f.session.setClipFollowAction (f.ids[0], { Action::next, 1, Action::last, .5 }), "an even chance between next and last");
        int nexts = 0, lasts = 0;
        for (int trial = 0; trial < 24; ++trial)
        {
            ok (f.session.launchSlot (0, Fixture::scenes[0]), "relaunch the first clip");
            f.player.process (36000);
            const int reached = destination (f); nexts += reached == 1; lasts += reached == 2;
        }
        check (nexts > 0 && lasts > 0 && nexts + lasts == 24, "an even chance reaches both destinations (" + juce::String (nexts) + " next, " + juce::String (lasts) + " last)");
        f.session.stop();
    }
    {
        Fixture f (root);
        ok (f.session.setClipFollowAction (f.ids[0], { Action::other, 1 }), "Other as the only action");
        std::array<int, 3> reached {};
        for (int trial = 0; trial < 12; ++trial)
        {
            ok (f.session.launchSlot (0, Fixture::scenes[0]), "relaunch the first clip");
            f.player.process (36000);
            if (const int d = destination (f); d >= 0) ++reached[(size_t) d];
        }
        check (reached[0] == 0 && reached[1] + reached[2] == 12, "Other always moves to a different clip");
        f.session.stop();
    }
    {
        Fixture f (root);
        ok (f.session.setClipFollowAction (f.ids[0], { Action::stop, 1, Action::off, .5 }), "stop or nothing at an even chance");
        int stopped = 0, kept = 0;
        for (int trial = 0; trial < 24; ++trial)
        {
            ok (f.session.launchSlot (0, Fixture::scenes[0]), "relaunch the first clip");
            f.player.process (36000);
            const int d = destination (f); stopped += d < 0; kept += d == 0;
        }
        check (stopped > 0 && kept > 0, "No action as B sometimes leaves the clip playing (" + juce::String (stopped) + " stopped, " + juce::String (kept) + " kept)");
        check (f.session.setClipFollowAction (f.ids[0], { Action::next, 1, Action::last, 1.5 }).failed()
               && f.session.setClipFollowAction (f.ids[0], { Action::off, 0, Action::next, .5 }).failed(), "chances outside 0-1 and B without A are rejected");
        const auto project = root.getChildFile ("Chances").getChildFile ("Chances.halftrack");
        ok (f.session.setClipFollowAction (f.ids[0], { Action::any, 2, Action::previous, .25 }), "Any as A and previous as B");
        ok (f.session.saveProject (project), "save the chance settings"); ok (f.session.loadProject (project), "reopen them");
        const auto reopened = f.settings();
        check (reopened.action == Action::any && reopened.actionB == Action::previous && std::abs (reopened.chanceA - .25) < 1e-9 && reopened.afterBeats == 2,
               "actions A and B and the chance reopen");
        auto text = project.loadFileAsString();
        check (text.contains ("weight=\"0.25\""), "the chance is stored as the actions' weights");
        const auto broken = project.getSiblingFile ("Broken.halftrack");
        check (broken.replaceWithText (text.replace ("weight=\"0.25\"", "weight=\"0.9\"")), "write weights that do not sum to one");
        check (f.session.loadProject (broken).failed() && f.settings() == reopened, "inconsistent chances reject the load and keep the open project");
        f.session.stop();
    }
}
int runFollowActionTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("follow-actions-" + juce::Uuid().toString()); ok (root.createDirectory(), "create isolated fixtures");
        transitions (root); runningEditsAndTempo (root); historyAndPersistence (root); for (double tempo : { 60.0, 120.0, 240.0 }) midiTransition (tempo);
        chances (root); return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL follow action: " << error.what() << std::endl; return 1; }
}
}
