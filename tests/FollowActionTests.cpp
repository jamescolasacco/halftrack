#include "FollowActions.h"
#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

// Follow Actions, as the reference's (static reading; see the follow actions contract): the draws, A or B and what each
// action plays checked against the research model's vectors, then clips and scenes following in rendered audio.
namespace halftrack
{
namespace
{
namespace te = tracktion;
using Action = Session::FollowAction;
using Settings = Session::FollowSettings;
void check (bool condition, const juce::String& message)
{
    if (! condition) throw std::runtime_error (message.toStdString());
    std::cout << "PASS follow action: " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{ check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : "")); }
Settings follow (Action a, double beats = 1, Action b = Action::noAction, float chanceA = 100, int jumpA = 1)
{
    Settings s; s.enabled = true; s.linked = false; s.timeBeats = beats; s.actionA = a; s.actionB = b;
    s.chanceA = chanceA; s.chanceB = 100 - chanceA; s.jumpA = jumpA; return s;
}

void model()
{
    {
        std::uint32_t state = 0;
        const float first = followactions::draw (state), second = followactions::draw (state), third = followactions::draw (state), fourth = followactions::draw (state);
        check (first == 0.7336041331291199f && second == 0.2525143325328827f && third == 0.20257018506526947f && fourth == 0.8957032561302185f,
               "the draws from state 0 are the research model's, to the bit");
        std::uint32_t wide = 0;
        float lowest = 1, highest = 0;
        for (int i = 0; i < 200000; ++i) { const auto r = followactions::draw (wide); lowest = std::min (lowest, r); highest = std::max (highest, r); }
        check (lowest >= 0 && highest < 1, "every draw is from 0 up to 1");
    }
    {
        const auto picks = [] (float r, Settings s) { return followactions::choose (s, r).first; };
        check (picks (0, follow (Action::next)) == Action::next && picks (0.99999994f, follow (Action::next)) == Action::next,
               "A at 100 % and B at 0 never plays B");
        check (picks (0, follow (Action::next, 1, Action::stop, 0)) == Action::stop, "A at 0 % always plays B");
        check (picks (0.5f, follow (Action::next, 1, Action::stop, 50)) == Action::stop, "a draw landing on A's share goes to B");
        auto none = follow (Action::next); none.chanceA = none.chanceB = 0;
        check (picks (0.3f, none) == Action::noAction, "both chances at 0 play nothing");
    }
    {
        const followactions::Group group { 2, 7, { 2, 3, 4, 7 } };
        const std::vector<int> excluded { 6 };
        const auto target = [&] (int current, Action action, int jump = 0, float r = 0) { return followactions::target (current, action, jump, &group, excluded, r); };
        check (target (2, Action::previous) == 7 && target (7, Action::next) == 2, "Previous and Next wrap round the group");
        check (target (3, Action::other) == 2 && target (2, Action::other) == 3, "Other skips the current clip");
        check (target (5, Action::jump, -1) == followactions::stopTarget && ! target (5, Action::jump, 6) && target (5, Action::jump, 9) == 9,
               "Jump to no scene stops, to an excluded slot does nothing, and goes past the group");
        check (target (3, Action::first) == 2 && target (3, Action::last) == 7 && target (3, Action::again) == 3 && target (3, Action::stop) == followactions::stopTarget,
               "First, Last, Again and Stop");
        check (! followactions::target (2, Action::stop, 0, nullptr, {}, 0), "without a group nothing happens, not even Stop");
    }
    {
        const auto chain = [] (std::uint32_t seed, Settings s, int start, bool relaunch, int count)
        {
            const followactions::Group group { 0, 3, { 0, 1, 2, 3 } };
            std::vector<int> played;
            int current = start;
            for (int i = 0; i < count; ++i)
            {
                const auto picked = followactions::evaluate (seed, relaunch ? start : current, s, &group);
                played.push_back (picked.value_or (-1));
                if (picked) current = *picked;
            }
            return played;
        };
        check (chain (12345, follow (Action::next, 1, Action::any, 70), 0, false, 16) == std::vector<int> { 1, 3, 0, 2, 3, 0, 1, 2, 1, 2, 3, 0, 1, 2, 3, 0 },
               "Next at 70 % and Any at 30 % from seed 12345 play the research model's sequence");
        check (chain (7, follow (Action::other), 0, true, 12) == std::vector<int> { 2, 1, 3, 2, 2, 3, 1, 3, 3, 2, 1, 1 }, "Other's picks from seed 7 match the model");
        check (chain (5, follow (Action::next, 1, Action::last, 50), 0, true, 12) == std::vector<int> { 3, 1, 3, 1, 1, 1, 3, 3, 1, 3, 3, 3 },
               "an even chance between Next and Last from seed 5 matches the model");
    }
    {
        const auto loaded = [] (float a, float b) { auto s = follow (Action::next); s.chanceA = a; s.chanceB = b; return followactions::normalised (s); };
        check (loaded (1, 0).chanceA == 100 && loaded (3, 1).chanceA == 75 && loaded (3, 1).chanceB == 25 && loaded (1, 2).chanceA == 33 && loaded (1, 2).chanceB == 67,
               "a load turns weights into percentages, A rounded");
        check (loaded (0, 0).chanceA == 100 && ! loaded (0, 0).enabled, "both chances at 0 load as A at 100 %, switched off");
        check (followactions::coupled (62.5f) == std::pair { 63.0f, 37.0f } && followactions::coupled (140) == std::pair { 100.0f, 0.0f },
               "editing a chance rounds it half away from zero and gives the other the rest");
    }
    {
        const std::vector<bool> held { true, true, false, true, true, true, false, false, true }, plays { true, true, false, true, false, true, false, false, true };
        const auto groups = followactions::groups (held, plays);
        check (groups.size() == 3 && groups[1].first == 3 && groups[1].last == 5 && groups[1].members == std::vector<int> { 3, 5 },
               "empty slots split groups, and a deactivated clip keeps its run whole but is left out");
        check (followactions::groupOf (groups, 4) == &groups[1] && followactions::groupOf (groups, 6) == nullptr, "a row's group is the run holding it");
    }
    {
        Settings s;
        check (followactions::valid (s) && ! s.enabled && s.linked && s.multiplier == 1 && s.timeBeats == 4 && s.actionA == Action::next
                   && s.actionB == Action::noAction && s.chanceA == 100 && s.jumpA == 1, "the defaults are the reference's");
        auto bad = s; bad.timeBeats = .2; auto nan = s; nan.timeBeats = std::numeric_limits<double>::quiet_NaN();
        auto none = s; none.multiplier = 0; auto over = s; over.chanceB = 101; auto jump = s; jump.jumpA = -2;
        auto kind = s; kind.actionA = static_cast<Action> (99);
        check (! followactions::valid (bad) && ! followactions::valid (nan) && ! followactions::valid (none) && ! followactions::valid (over)
                   && ! followactions::valid (jump) && ! followactions::valid (kind), "times under a quarter beat, multipliers under 1 and other values out of range are invalid");
        check (followactions::tick (1.3 / 3843840) == 1.0 / 3843840 && followactions::tick (1.7 / 3843840) == 2.0 / 3843840, "times round to the reference's tick");
    }
}

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
// Track 1's clips at scenes 1 to 4, one group, and a clip alone at scene 6, each a quarter second of its own level, which
// the Session loops. At 120 BPM a beat is 24000 samples.
struct Fixture
{
    // Registered native reader storage must outlive every graph and the engine.
    std::vector<std::unique_ptr<choc::buffer::InterleavedBuffer<float>>> buffers;
    Session session { false };
    te::Engine& engine = engineFor (session);
    te::test_utilities::EnginePlayer player { engine, parameters() };
    static constexpr std::array<int, 5> scenes { 0, 1, 2, 3, 5 };
    static constexpr std::array<float, 5> values { .125f, .25f, .375f, .5f, .625f };
    std::array<juce::String, 5> ids;
    explicit Fixture (const juce::File& root, bool patterned = false)
    {
        for (size_t n = 0; n < scenes.size(); ++n)
        {
            const auto file = root.getChildFile (juce::Uuid().toString() + ".wav");
            juce::AudioBuffer<float> pcm (2, 12000);
            auto memory = std::make_unique<choc::buffer::InterleavedBuffer<float>> (2, 12000);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 12000; ++i)
            {
                const float value = patterned && n == 0 && i >= 6000 ? .0625f : values[n];
                pcm.setSample (c, i, value); memory->getSample (c, i) = value;
            }
            {
                juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
                check (writer && writer->writeFromAudioSampleBuffer (pcm, 0, 12000), "write generated transition source");
            }
            ok (session.importAudioToSlot (file, 0, scenes[n]), "populate Session clips");
            const auto clip = session.snapshot().tracks[0].slots[(size_t) scenes[n]].clip;
            ids[n] = clip.id;
            engine.getAudioFileManager().registerMemoryBuffer (clip.file.getFullPathName().toStdString(), memory->getView(), 48000);
            buffers.push_back (std::move (memory));
        }
        ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "launch at once");
    }
    // Plays on, letting the Session's follow watch run between short stretches, as it would beside a device.
    juce::AudioBuffer<float> run (int samples)
    {
        juce::AudioBuffer<float> all (2, samples);
        for (int done = 0; done < samples;)
        {
            const int count = std::min (1024, samples - done);
            const auto part = player.process (count);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, done, part, c, 0, count);
            done += count;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (7);
        }
        return all;
    }
    Settings settings (size_t index = 0) const { return session.snapshot().tracks[0].slots[(size_t) scenes[index]].clip.follow; }
    // The fixture clip playing, by its index, or -1.
    int playing() const
    {
        const auto state = session.snapshot();
        for (size_t i = 0; i < scenes.size(); ++i) if (state.tracks[0].slots[(size_t) scenes[i]].playing) return (int) i;
        return -1;
    }
};

void transitions (const juce::File& root)
{
    struct Case { const char* what; Settings settings; int from, to; };
    auto disabled = follow (Action::next); disabled.enabled = false;
    for (const auto& test : { Case { "Next plays the clip below", follow (Action::next), 0, 1 }, Case { "Next wraps from the group's last clip", follow (Action::next), 3, 0 },
                              Case { "Previous wraps from the first", follow (Action::previous), 0, 3 }, Case { "First", follow (Action::first), 2, 0 },
                              Case { "Last", follow (Action::last), 0, 3 }, Case { "Stop stops the track", follow (Action::stop), 0, -1 },
                              Case { "No Action plays on", follow (Action::noAction), 0, 0 }, Case { "switched off, a clip plays on", disabled, 0, 0 },
                              Case { "Jump plays its scene, outside the group", follow (Action::jump, 1, Action::noAction, 100, 5), 0, 4 },
                              Case { "Jump to an empty slot stops the track", follow (Action::jump, 1, Action::noAction, 100, 4), 0, -1 },
                              Case { "Next from a clip alone in its group plays it again", follow (Action::next), 4, 4 } })
    {
        Fixture f (root);
        ok (f.session.setClipFollowAction (f.ids[(size_t) test.from], test.settings), juce::String ("configure: ") + test.what);
        ok (f.session.launchSlot (0, Fixture::scenes[(size_t) test.from]), "launch the source");
        const auto output = f.run (48000);
        level (output, 0, 1024, 22000, Fixture::values[(size_t) test.from], "the source plays for its beat");
        level (output, 0, 25000, 22000, test.to < 0 ? 0 : Fixture::values[(size_t) test.to], test.what);
        check (f.playing() == test.to, juce::String ("the launch state agrees: ") + test.what);
        f.session.stop();
    }
    {
        Fixture f (root, true);
        ok (f.session.setClipFollowAction (f.ids[0], follow (Action::again, .75)), "Play Again at a fractional beat, apart from the clip's own loop");
        ok (f.session.launchSlot (0, 0), "launch the patterned clip");
        const auto output = f.run (33000);
        level (output, 0, 19000, 4000, .125f, "Play Again restarts the clip at the time");
        level (output, 0, 25000, 4000, .0625f, "and plays on from its start");
        f.session.stop();
    }
    {
        // Linked, a clip follows at the end of its loop's passes: the clip loops each half beat.
        Fixture f (root);
        auto linked = follow (Action::next); linked.linked = true; linked.multiplier = 2;
        ok (f.session.setClipFollowAction (f.ids[0], linked), "Next, linked, twice round the loop");
        ok (f.session.launchSlot (0, 0), "launch it");
        const auto output = f.run (36000);
        level (output, 0, 1024, 22000, .125f, "the clip plays both passes");
        level (output, 0, 25000, 10000, .25f, "and the next follows at the end of the second");
        f.session.stop();
    }
}
void chains (const juce::File& root)
{
    {
        // The whole chain, drawn as the reference draws it from the track's state.
        Fixture f (root);
        for (size_t i = 0; i < 4; ++i) ok (f.session.setClipFollowAction (f.ids[i], follow (Action::next, 1, Action::any, 70)), "Next at 70 % and Any at 30 %");
        f.session.seedFollowActions (12345);
        ok (f.session.launchSlot (0, 0), "launch the first clip");
        f.run (12000);
        std::vector<int> played;
        for (int step = 0; step < 6; ++step) { f.run (24000); played.push_back (f.playing()); }
        check (played == std::vector<int> { 1, 3, 0, 2, 3, 0 }, "the clips follow the research model's sequence from seed 12345");
        f.session.stop();
    }
    {
        // Planned at a clip's start, a Follow Action keeps its plan through an edit, and a launch replaces it.
        Fixture f (root);
        ok (f.session.setClipFollowAction (f.ids[0], follow (Action::next)), "Next after a beat");
        ok (f.session.launchSlot (0, 0), "launch it");
        f.run (6000);
        ok (f.session.setClipFollowAction (f.ids[0], follow (Action::stop)), "change it to Stop while it plays");
        auto output = f.run (42000);
        level (output, 0, 22000, 18000, .25f, "the plan made at its start still plays the next clip");
        f.session.stop();
        ok (f.session.launchSlot (0, 0), "launch the first again, now set to stop");
        f.run (6000);
        ok (f.session.launchSlot (0, 2), "launch the third by hand before the first's time");
        output = f.run (48000);
        level (output, 0, 24000, 22000, .375f, "the launch replaced the plan to stop");
        f.session.stop();
    }
    {
        // Enable Follow Actions Globally: off, nothing new is planned; a plan already made still plays.
        Fixture f (root);
        for (size_t i = 0; i < 2; ++i) ok (f.session.setClipFollowAction (f.ids[i], follow (Action::next)), "Next after a beat");
        check (f.session.snapshot().followActionsEnabled && f.session.snapshot().followActionsInUse, "the switch is on, and in use");
        f.session.setFollowActionsEnabled (false);
        check (! f.session.snapshot().followActionsEnabled && ! f.session.canRedo(), "it turns off outside the undo history");
        ok (f.session.launchSlot (0, 0), "launch with it off");
        auto output = f.run (48000);
        level (output, 0, 25000, 22000, .125f, "off, a clip does not follow");
        f.session.stop();
        f.session.setFollowActionsEnabled (true);
        ok (f.session.launchSlot (0, 0), "launch with it on");
        f.run (6000);
        f.session.setFollowActionsEnabled (false);
        output = f.run (90000);
        level (output, 0, 22000, 40000, .25f, "a plan made before it went off still plays, and the clip it plays plans nothing");
        f.session.setFollowActionsEnabled (true);
        f.session.stop();
    }
}
void scenes (const juce::File& root)
{
    Fixture f (root);
    const auto sceneId = [&] (int row) { return f.session.snapshot().scenes[(size_t) row].id; };
    ok (f.session.setSceneFollowAction (sceneId (0), follow (Action::next)), "scene 1 follows to the next scene after a beat");
    check (f.session.snapshot().scenes[0].follow == follow (Action::next) && f.session.snapshot().scenes[0].hasClips && f.session.snapshot().followActionsInUse,
           "the scene reports its Follow Action, and that it holds clips");
    ok (f.session.setClipFollowAction (f.ids[0], follow (Action::last, 1.5)), "its clip would follow later on its own");
    ok (f.session.launchScene (0), "launch scene 1");
    auto output = f.run (48000);
    level (output, 0, 1024, 22000, .125f, "the scene's clip plays");
    level (output, 0, 25000, 22000, .25f, "and the next scene launches after a beat, over the clip's own plan");
    f.session.stop();
    auto linked = follow (Action::next); linked.linked = true; linked.multiplier = 3;
    ok (f.session.setSceneFollowAction (sceneId (0), linked), "linked, three times its longest clip");
    ok (f.session.setClipFollowAction (f.ids[0], {}), "the clip without one");
    ok (f.session.launchScene (0), "launch scene 1 again");
    output = f.run (48000);
    level (output, 0, 1024, 33000, .125f, "the scene plays its longest clip three times round");
    level (output, 0, 37000, 10000, .25f, "and the next scene follows at a beat and a half");
    f.session.stop();
    ok (f.session.setSceneFollowAction (sceneId (0), follow (Action::next)), "back to a beat");
    ok (f.session.launchScene (0), "launch scene 1");
    f.run (6000);
    ok (f.session.stopAllLauncherClips(), "Stop All");
    output = f.run (48000);
    level (output, 0, 25000, 22000, 0, "Stop All ends the scene's Follow Action too");
    f.session.stop();
}
// Create Follow Action Chain, as the reference's (static reading): each track's selected clips in scene order, Next to a
// clip in the next row, else Jump, the last back to the first, a lone clip Play Again.
void followActionChain (const juce::File& root)
{
    Fixture f (root);
    const auto chained = [&] (size_t index) { return f.settings (index); };
    auto expected = [] (Action a, int jump = 1) { Settings s; s.enabled = true; s.actionA = a; if (a == Action::jump) s.jumpA = jump; return s; };
    ok (f.session.createFollowActionChain ({ { 0, 0 }, { 0, 1 }, { 0, 3 }, { 0, 5 }, { 0, 4 } }), "chain the clips at rows 0, 1, 3 and 5, and an empty slot");
    check (chained (0) == expected (Action::next) && chained (1) == expected (Action::jump, 3) && chained (3) == expected (Action::jump, 5)
               && chained (4) == expected (Action::jump, 0) && chained (2) == Settings {},
           "Next to the next row, Jump past a gap, and the last jumps back to the first; the rest stays as it was");
    check (f.session.undoDescription().containsIgnoreCase ("chain"), "as one undo step");
    f.session.undo();
    check (chained (0) == Settings {} && chained (4) == Settings {}, "which undo takes back");
    ok (f.session.importAudioToSlot (f.session.snapshot().tracks[0].slots[0].clip.file, 1, 4), "a clip on another track");
    ok (f.session.createFollowActionChain ({ { 0, 1 }, { 0, 2 }, { 1, 4 } }), "chain clips on two tracks");
    check (chained (1) == expected (Action::next) && chained (2) == expected (Action::jump, 1) && f.session.snapshot().tracks[1].slots[4].clip.follow == expected (Action::again),
           "each track chains its own clips, and a track's lone clip plays again");
    check (f.session.createFollowActionChain ({ { 0, 4 } }).failed(), "a selection without clips is refused");
    f.session.undo(); f.session.undo();
    ok (f.session.createFollowActionChain ({ { 0, 0 }, { 0, 1 }, { 0, 3 } }), "chain rows 0, 1 and 3");
    ok (f.session.launchSlot (0, 0), "launch the first");
    std::vector<int> played;
    f.run (6000);
    for (int step = 0; step < 4; ++step) { f.run (12000); played.push_back (f.playing()); }
    check (played == std::vector<int> { 1, 3, 0, 1 }, "linked, each plays its loop once and the chain goes round");
    f.session.stop();
}
void historyAndPersistence (const juce::File& root)
{
    Fixture f (root);
    const auto original = f.session.snapshot().tracks[0].slots[0].clip;
    check (! f.session.snapshot().followActionsInUse, "nothing uses Follow Actions at first");
    const auto next = follow (Action::next, 1.5);
    ok (f.session.setClipFollowAction (f.ids[0], next), "set a Follow Action");
    check (f.settings() == next && f.session.undoDescription().containsIgnoreCase ("follow"), "the snapshot reports it, one undo step");
    f.session.undo(); check (f.settings() == Settings {}, "one undo restores the defaults");
    ok (f.session.setClipFollowAction (f.ids[0], {}), "the defaults again are a no-op");
    auto bad = next; bad.timeBeats = .1; auto nan = next; nan.timeBeats = std::numeric_limits<double>::quiet_NaN();
    auto chance = next; chance.chanceA = 101; auto jump = next; jump.jumpB = -3;
    check (f.session.setClipFollowAction (f.ids[0], bad).failed() && f.session.setClipFollowAction (f.ids[0], nan).failed()
               && f.session.setClipFollowAction (f.ids[0], chance).failed() && f.session.setClipFollowAction (f.ids[0], jump).failed()
               && f.session.setClipFollowAction ("missing", next).failed() && f.session.canRedo(), "invalid changes are refused, keeping redo");
    f.session.redo(); check (f.settings() == next, "redo restores it exactly");
    const auto current = f.session.snapshot().tracks[0].slots[0].clip;
    check (current.id == original.id && current.file == original.file && current.length == original.length && current.active == original.active,
           "Follow Action edits keep the clip's identity, source, extent and activation");
    auto scene = follow (Action::jump, 2, Action::stop, 25, 3); scene.linked = true; scene.multiplier = 4; scene.jumpB = -1;
    ok (f.session.setSceneFollowAction (f.session.snapshot().scenes[1].id, scene), "give scene 2 a Follow Action");
    check (f.session.snapshot().scenes[1].follow == scene, "the scene reports it");
    f.session.undo(); check (f.session.snapshot().scenes[1].follow == Settings {}, "one undo removes it"); f.session.redo();
    check (f.session.setSceneFollowAction ("missing", scene).failed(), "an unknown scene is refused");
    juce::String copied; ok (f.session.duplicateScene (f.session.snapshot().scenes[0].id, copied), "duplicate scene 1");
    check (f.session.snapshot().tracks[0].slots[1].clip.follow == next, "the copied clip keeps its Follow Action");
    f.session.undo();
    const auto project = root.getChildFile ("Follow").getChildFile ("Follow.halftrack");
    ok (f.session.saveProject (project), "save"); ok (f.session.loadProject (project), "reopen");
    check (f.settings() == next && f.session.snapshot().scenes[1].follow == scene, "clip and scene Follow Actions reopen");
    const auto text = project.loadFileAsString();
    const auto rewrite = [&] (const juce::String& from, const juce::String& to, const juce::String& name)
    {
        check (text.contains (from), "the saved project holds " + from);
        const auto changed = root.getChildFile (name + ".halftrack");
        check (changed.replaceWithText (text.replace (from, to)), "write " + name);
        return changed;
    };
    // A load leaves the chances as percentages, as the reference's does.
    ok (f.session.loadProject (rewrite ("chanceA=\"25.0\"", "chanceA=\"3.0\"", "weights")), "load chances 3 and 75");
    check (f.session.snapshot().scenes[1].follow.chanceA == 4 && f.session.snapshot().scenes[1].follow.chanceB == 96, "they load as their shares in percent");
    ok (f.session.loadProject (project), "reopen the saved project");
    ok (f.session.renameTrack (0, "Keep redo"), "history before the invalid loads"); f.session.undo();
    for (const auto& [from, to] : { std::pair { "multiplier=\"4\"", "multiplier=\"0\"" }, std::pair { "time=\"1.5\"", "time=\"nan\"" },
                                    std::pair { "actionA=\"9\"", "actionA=\"12\"" }, std::pair { "jumpB=\"-1\"", "jumpB=\"-2\"" } })
        check (f.session.loadProject (rewrite (from, to, "invalid")).failed() && f.session.canRedo() && f.settings() == next,
               juce::String ("an invalid Follow Action (") + to + ") rejects the load, keeping the project and redo");
    {
        // A project saved before 2026-10-26 kept the engine's follow actions; loading converts them.
        auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "read the saved project");
        const std::function<juce::XmlElement* (juce::XmlElement&)> find = [&] (juce::XmlElement& node) -> juce::XmlElement*
        {
            if (auto* f2 = node.getChildByName ("HALFTRACKFOLLOWACTION"); f2 != nullptr && node.getTagName() == "AUDIOCLIP") return &node;
            for (auto* child : node.getChildIterator()) if (auto* found = find (*child)) return found;
            return nullptr;
        };
        auto* clip = find (*xml); check (clip != nullptr, "find the clip's Follow Action");
        clip->removeChildElement (clip->getChildByName ("HALFTRACKFOLLOWACTION"), true);
        clip->setAttribute ("followActionBeats", 2.0); clip->setAttribute ("followActionDurationType", "beats");
        auto* actions = clip->createNewChildElement ("FOLLOWACTIONS");
        auto* first = actions->createNewChildElement ("ACTION"); first->setAttribute ("type", "trackNext"); first->setAttribute ("weight", 0.7);
        auto* second = actions->createNewChildElement ("ACTION"); second->setAttribute ("type", "trackLast"); second->setAttribute ("weight", 0.3);
        const auto legacy = root.getChildFile ("Legacy.halftrack"); check (xml->writeTo (legacy), "write an older project");
        ok (f.session.loadProject (legacy), "open it");
        auto expected = follow (Action::next, 2, Action::last, 70);
        check (f.settings() == expected, "its follow actions convert: on, unlinked, after their beats, the weights as chances");
        const auto resaved = root.getChildFile ("Resaved.halftrack");
        ok (f.session.saveProject (resaved), "save it again");
        check (! resaved.loadFileAsString().contains ("FOLLOWACTIONS") && ! resaved.loadFileAsString().contains ("followActionBeats"), "saved, only Halftrack's form remains");
        first->setAttribute ("weight", 0.9);
        check (xml->writeTo (legacy) && f.session.loadProject (legacy).failed(), "older weights that do not add up are refused");
        ok (f.session.loadProject (project), "reopen the saved project");
    }
    ok (f.session.importAudio (f.session.snapshot().tracks[0].slots[0].clip.file, 1), "an Arrangement clip");
    check (f.session.setClipFollowAction (f.session.snapshot().tracks[1].clips[0].id, next).failed(), "Arrangement clips have no Follow Action");
    bool guarded = false;
    juce::Timer::callAfterDelay (10, [&] { guarded = f.session.setClipFollowAction (f.ids[0], {}).failed(); f.session.cancelExport(); });
    const auto cancelled = f.session.exportWav (root.getChildFile ("cancelled.wav"), 48000, 24, 60);
    check (cancelled.failed() && guarded && f.settings() == next, "an export protects the Follow Actions");
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
    ok (s.setClipFollowAction (id, follow (Action::next)), "follow from held MIDI note to empty MIDI clip");
    ok (s.setLaunchQuantisation (Session::LaunchQuantisation::none), "immediate MIDI start");
    ok (s.launchSlot (2, 0), "launch held MIDI source");
    const int boundary = (int) std::round (48000 * 60 / tempo);
    juce::AudioBuffer<float> output (2, boundary + 24000);
    for (int done = 0; done < output.getNumSamples();)
    {
        const int count = std::min (1024, output.getNumSamples() - done);
        const auto part = player.process (count);
        for (int c = 0; c < 2; ++c) output.copyFrom (c, done, part, c, 0, count);
        done += count;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (7);
    }
    check (output.getMagnitude (0, 1024, boundary - 2048) > .01f, "source MIDI voice is audible before the project-tempo transition");
    level (output, 0, boundary + 2000, 21000, 0, "follow boundary releases held source voice at project tempo without a stuck note");
    check (s.snapshot().tracks[2].slots[1].playing, "silent MIDI destination genuinely owns playback");
    s.stop();
}
}
int runFollowActionTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("follow-actions-" + juce::Uuid().toString()); ok (root.createDirectory(), "create isolated fixtures");
        model(); transitions (root); chains (root); followActionChain (root); scenes (root); historyAndPersistence (root);
        for (double tempo : { 60.0, 120.0, 240.0 }) midiTransition (tempo);
        std::cout << "Follow actions passed." << std::endl; return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL follow action: " << error.what() << std::endl; return 1; }
}
}
