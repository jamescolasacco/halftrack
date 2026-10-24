#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Group tracks sum their members through their own mixer. Renders compare with
// the ungrouped mix and with independent gain/pan arithmetic.
namespace halftrack
{
namespace
{
constexpr double rate = 48000;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS groups: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
void write (const juce::File& f, float left, float right, int frames)
{
    juce::AudioBuffer<float> a (2, frames);
    for (int n = 0; n < frames; ++n) { a.setSample (0, n, left * (float) std::sin (n * .05)); a.setSample (1, n, right * (float) std::cos (n * .031)); }
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (a, 0, frames), "write generated source");
}
juce::AudioBuffer<float> render (Session& s, const juce::File& f)
{
    ok (s.exportWav (f, rate, 32), "render mix");
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    check (r != nullptr, "decode mix"); juce::AudioBuffer<float> a (2, (int) r->lengthInSamples); r->read (&a, 0, a.getNumSamples(), 0, true, true); return a;
}
// Compare inside the sources, leaving the engine's start/end settling outside the window.
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, float leftScale = 1, float rightScale = 1)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1e9;
    double worst = 0;
    for (int n = 2000; n < a.getNumSamples() - 2000; ++n)
    {
        worst = std::max (worst, (double) std::abs (a.getSample (0, n) - leftScale * b.getSample (0, n)));
        worst = std::max (worst, (double) std::abs (a.getSample (1, n) - rightScale * b.getSample (1, n)));
    }
    return worst;
}
double peak (const juce::AudioBuffer<float>& a) { return std::max (a.getMagnitude (0, 0, a.getNumSamples()), a.getMagnitude (1, 0, a.getNumSamples())); }
}
int runGroupTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("groups-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto a = root.getChildFile ("a.wav"), b = root.getChildFile ("b.wav"), c = root.getChildFile ("c.wav"), mix = root.getChildFile ("mix.wav");
        write (a, .2f, .1f, 24000); write (b, .1f, .15f, 24000); write (c, .05f, .05f, 24000);
        Session s (false);
        ok (s.importAudio (a, 0), "import first member"); ok (s.importAudio (b, 1), "import second member"); ok (s.addAudioTrack ("Outside"), "add an outside track");
        ok (s.importAudio (c, 2), "import the outside source");
        const auto outside = s.snapshot().tracks[2].id;
        ok (s.setTrackMute (2, true), "mute the outside track for the member-only reference");
        const auto reference = render (s, mix);
        check (peak (reference) > .1, "reference mix is audible");
        const auto before = s.snapshot();
        juce::String group;
        check (s.groupTracks ({}, group).failed() && s.groupTracks ({ before.tracks[0].id, before.tracks[0].id }, group).failed(), "empty and duplicate group requests are rejected");
        ok (s.groupTracks ({ before.tracks[1].id, before.tracks[0].id }, group), "group the first two tracks");
        auto snapshot = s.snapshot();
        check (snapshot.groups.size() == 1 && snapshot.groups[0].id == group && snapshot.groups[0].memberIds == juce::StringArray { before.tracks[0].id, before.tracks[1].id },
               "the group lists its members in track order");
        check (snapshot.tracks.size() == before.tracks.size() && snapshot.tracks[0].id == before.tracks[0].id && snapshot.tracks[2].id == outside
               && snapshot.tracks[0].groupId == group && snapshot.tracks[1].groupId == group && snapshot.tracks[2].groupId.isEmpty(),
               "track order and indices are unchanged, with membership reported per track");
        check (difference (render (s, mix), reference) < 1e-6, "a unity group renders the same sum as the ungrouped tracks");
        {
            // As in the reference, a grouped track groups into a group inside its group.
            juce::String inner;
            ok (s.groupTracks ({ before.tracks[0].id }, inner), "group a grouped track");
            const auto nested = s.snapshot();
            check (nested.groups.size() == 2 && nested.tracks[0].groupId == inner && nested.groups[1].parentId == group && nested.groups[0].parentId.isEmpty()
                       && nested.groups[0].memberIds == juce::StringArray { before.tracks[1].id }, "it makes a group inside the group, holding the track");
            check (difference (render (s, mix), reference) < 1e-6, "the nested group plays the same mix");
            s.undo(); check (s.snapshot().groups.size() == 1 && s.snapshot().tracks[0].groupId == group, "one undo removes the inner group");
        }
        ok (s.setGroupGain (group, -6), "lower the group");
        const auto gain = std::pow (10.0f, -6.0f / 20);
        check (difference (render (s, mix), reference, gain, gain) < 1e-6, "group gain scales the summed members");
        s.undo(); check (std::abs (s.snapshot().groups[0].gainDb) < 1e-4, "undo restores the group gain");
        s.redo(); check (std::abs (s.snapshot().groups[0].gainDb + 6) < 1e-4, "redo lowers the group again");
        ok (s.setGroupGain (group, 0), "restore group gain"); ok (s.setGroupPan (group, -1), "pan the group hard left");
        s.undo(); check (std::abs (s.snapshot().groups[0].pan) < 1e-4, "undo centres the group pan"); s.redo();
        check (difference (render (s, mix), reference, std::sqrt (2.0f), 0) < 1e-5, "the group pan follows the constant-power law with unity centre");
        ok (s.setGroupPan (group, 0), "centre the group");
        ok (s.setGroupMute (group, true), "mute the group");
        check (peak (render (s, mix)) < 1e-7 && ! s.snapshot().tracks[0].mute && s.snapshot().groups[0].mute, "muting the group silences its members without muting them");
        ok (s.setGroupMute (group, false), "unmute the group");
        ok (s.setTrackMute (2, false), "unmute the outside track");
        ok (s.setGroupSolo (group, true), "solo the group");
        check (difference (render (s, mix), reference) < 1e-6, "soloing the group leaves only its members audible");
        ok (s.setGroupSolo (group, false), "unsolo the group");
        ok (s.setTrackGain (1, -12), "index-based track commands still address grouped tracks");
        check (std::abs (s.snapshot().tracks[1].gainDb + 12) < 1e-4, "grouped track gain changes");
        s.undo();
        ok (s.renameGroup (group, "Drums"), "rename the group");
        ok (s.setGroupFolded (group, true), "fold the group");
        check (s.snapshot().groups[0].name == "Drums" && s.snapshot().groups[0].folded, "name and fold state are reported");
        const auto project = root.getChildFile ("groups.halftrack");
        ok (s.setGroupGain (group, -3), "set a group level to save");
        ok (s.saveProject (project), "save grouped project"); ok (s.loadProject (project), "reopen grouped project");
        snapshot = s.snapshot();
        check (snapshot.groups.size() == 1 && snapshot.groups[0].name == "Drums" && snapshot.groups[0].folded && std::abs (snapshot.groups[0].gainDb + 3) < 1e-4
               && snapshot.groups[0].memberIds.size() == 2, "groups reopen with name, fold, level and members");
        group = snapshot.groups[0].id;
        ok (s.setGroupGain (group, 0), "restore unity after reopening"); ok (s.setTrackMute (2, true), "mute outside track again");
        ok (s.ungroupTracks (group), "ungroup");
        snapshot = s.snapshot();
        check (snapshot.groups.empty() && snapshot.tracks.size() == 3 && snapshot.tracks[0].groupId.isEmpty() && snapshot.tracks[0].id == before.tracks[0].id,
               "ungrouping restores top-level tracks in order");
        check (difference (render (s, mix), reference) < 1e-6, "ungrouped tracks render the original mix");
        s.undo(); check (s.snapshot().groups.size() == 1, "undo restores the group");
        s.redo(); check (s.snapshot().groups.empty(), "redo ungroups again");
        ok (s.addReturnTrack ("Room"), "add a return");
        check (s.groupTracks ({ s.snapshot().tracks.back().id }, group).failed(), "returns cannot be grouped");
        Session midi (false); ok (midi.addMidiTrack ("Keys"), "add MIDI track");
        const auto keys = midi.snapshot().tracks.back().id;
        ok (midi.addMidiClip (keys, 0, 4), "add clip"); { const auto snap = midi.snapshot(); ok (midi.addMidiNote (snap.tracks.back().clips[0].id, 60, 0, 2, .8f), "add note"); }
        const auto alone = render (midi, mix);
        ok (midi.groupTracks ({ keys }, group), "group the MIDI track");
        check (difference (render (midi, mix), alone) < 1e-6 && peak (alone) > .01, "an instrument renders identically through its group");
        {
            // Device chains on a group and on the Main, as the reference's tracks have them: audio
            // effects only, before the mixer.
            Session d (false);
            ok (d.importAudio (a, 0), "import a member"); ok (d.importAudio (b, 1), "import another");
            const auto plain = render (d, mix);
            juce::String chained;
            ok (d.groupTracks ({ d.snapshot().tracks[0].id, d.snapshot().tracks[1].id }, chained), "group them");
            check (d.snapshot().groups[0].plugins.empty() && d.snapshot().mainPlugins.empty(), "the group's and the Main's chains start empty");
            ok (d.addUtility (chained), "add a Utility to the group");
            const auto groupUtility = d.snapshot().groups[0].plugins.at (0);
            check (groupUtility.isUtility && d.snapshot().tracks[0].plugins.empty(), "the group lists its Utility, and its members do not");
            auto settings = groupUtility.utility; settings.gainDb = -6;
            ok (d.setUtilitySettings (groupUtility.id, settings), "turn the group's Utility down 6 dB");
            const auto sixDown = std::pow (10.0, -6.0 / 20);
            check (difference (render (d, mix), plain, (float) sixDown, (float) sixDown) < 1e-5, "the group's chain plays its members 6 dB down");
            ok (d.addUtility (Session::mainChain), "add a Utility to the Main");
            const auto mainUtility = d.snapshot().mainPlugins.at (0);
            settings = mainUtility.utility; settings.gainDb = -6;
            ok (d.setUtilitySettings (mainUtility.id, settings), "turn the Main's Utility down 6 dB");
            check (difference (render (d, mix), plain, (float) (sixDown * sixDown), (float) (sixDown * sixDown)) < 1e-5, "the Main's chain follows, before its fader");
            ok (d.setPluginEnabled (Session::mainChain, 0, false), "switch the Main's Utility off");
            check (difference (render (d, mix), plain, (float) sixDown, (float) sixDown) < 1e-5 && ! d.snapshot().mainPlugins[0].enabled, "a switched-off device passes the sound");
            check (d.addNativeEffect (chained, Session::EffectKind::arpeggiator).failed() && d.addNativeEffect (Session::mainChain, Session::EffectKind::sampler).failed()
                       && d.addUtility ("missing").failed(), "MIDI effects, instruments and unknown chains are refused");
            ok (d.addNativeEffect (chained, Session::EffectKind::channelEq), "add a Channel EQ to the group");
            check (d.snapshot().groups[0].plugins.size() == 2, "after the Utility");
            ok (d.movePlugin (chained, 1, 0), "move the Channel EQ first");
            check (! d.snapshot().groups[0].plugins[0].isUtility && d.snapshot().groups[0].plugins[1].isUtility, "the group's devices reorder");
            d.undo(); check (d.snapshot().groups[0].plugins[0].isUtility, "one undo puts it back");
            const auto project = root.getChildFile ("Chains.halftrack");
            ok (d.saveProject (project), "save");
            Session reopened (false);
            ok (reopened.loadProject (project), "reopen");
            check (reopened.snapshot().groups.at (0).plugins.size() == 2 && reopened.snapshot().mainPlugins.size() == 1, "the group's and the Main's devices reopen");
            check (difference (render (reopened, mix), plain, (float) sixDown, (float) sixDown) < 1e-5, "and play as before");
            ok (reopened.removePlugin (Session::mainChain, 0), "remove the Main's Utility");
            check (reopened.snapshot().mainPlugins.empty(), "it is gone");
        }
        {
            // Nesting: groups inside groups, grouping groups, and ungrouping one level at a time.
            Session n (false);
            ok (n.importAudio (a, 0), "import a"); ok (n.importAudio (b, 1), "import b"); ok (n.addAudioTrack ("C"), "add c"); ok (n.importAudio (c, 2), "import c");
            const auto plain = render (n, mix);
            const auto ids = [&n] { juce::StringArray result; for (const auto& t : n.snapshot().tracks) result.add (t.id); return result; }();
            juce::String outer, inner, top;
            ok (n.groupTracks ({ ids[0], ids[1] }, outer), "group a and b");
            ok (n.groupTracks ({ ids[0] }, inner), "group a inside it");
            ok (n.groupTracks ({ outer, ids[2] }, top), "group the outer group with c");
            const auto tree = n.snapshot();
            const auto groupNamed = [&tree] (const juce::String& id) { for (const auto& g : tree.groups) if (g.id == id) return g; return Session::GroupInfo {}; };
            check (groupNamed (outer).parentId == top && groupNamed (inner).parentId == outer && groupNamed (top).parentId.isEmpty()
                       && tree.tracks[0].groupId == inner && tree.tracks[1].groupId == outer && tree.tracks[2].groupId == top,
                   "three levels: a in the inner group, b in the outer, c and the outer group in the top one");
            check (difference (render (n, mix), plain) < 1e-6, "the nested groups play the same mix");
            ok (n.setGroupGain (top, -6), "the top group 6 dB down");
            const auto sixDown = (float) std::pow (10.0, -6.0 / 20);
            check (difference (render (n, mix), plain, sixDown, sixDown) < 1e-5, "the top group's fader moves everything inside it");
            ok (n.setGroupGain (top, 0), "back to unity");
            {
                // Clear All Envelopes clears the chosen tracks' and groups' own lanes, not those of the
                // tracks inside a chosen group, in one undo step.
                const Session::AutomationLane lane { true, { { 0, -6 }, { 4, -12 } } };
                ok (n.setTrackAutomation (outer, Session::AutomationTarget::volume, lane), "automate the outer group's volume");
                ok (n.setTrackAutomation (ids[1], Session::AutomationTarget::pan, { true, { { 0, -.5f }, { 4, .5f } } }), "and b's pan");
                const auto groupVolume = [&n, &outer] { for (const auto& g : n.snapshot().groups) if (g.id == outer) return g.volumeAutomation; return Session::AutomationLane {}; };
                check (n.canClearAllEnvelopes ({ outer }) && ! n.canClearAllEnvelopes ({ ids[0] }), "the group has envelopes to clear, a not");
                ok (n.clearAllEnvelopes ({ outer }), "clear the outer group's envelopes");
                check (groupVolume().points.empty() && n.snapshot().tracks[1].panAutomation.points.size() == 2, "its lane goes, and b, inside it, keeps its own");
                ok (n.clearAllEnvelopes ({ ids[1], ids[0] }), "clear b's, with a chosen too");
                check (n.snapshot().tracks[1].panAutomation.points.empty(), "b's lane goes");
                n.undo();
                check (n.snapshot().tracks[1].panAutomation.points.size() == 2 && groupVolume().points.empty(), "one undo brings b's back alone");
                n.undo();
                check (groupVolume().points.size() == 2, "and another the group's");
                check (n.clearAllEnvelopes ({ ids[0] }).failed() && n.clearAllEnvelopes ({ "missing" }).failed(), "nothing to clear, or an unknown track, is refused");
                n.undo(); n.undo();
                check (groupVolume().points.empty() && n.snapshot().tracks[1].panAutomation.points.empty(), "two more undo steps remove the lanes set up here");
            }
            ok (n.setInfoText (top, "Everything"), "give the top group an info text"); ok (n.setInfoText (ids[0], "  The first take  "), "and a one too");
            const auto project = root.getChildFile ("Nested.halftrack");
            ok (n.saveProject (project), "save");
            Session reopened (false);
            ok (reopened.loadProject (project), "reopen");
            const auto again = reopened.snapshot();
            check (again.groups.size() == 3 && again.tracks[0].groupId == inner && again.tracks[1].groupId == outer && again.tracks[2].groupId == top, "the nesting reopens");
            check (again.tracks[0].infoText == "The first take" && [&] { for (const auto& g : again.groups) if (g.id == top) return g.infoText == "Everything"; return false; }(),
                   "with the info texts, trimmed, as Edit Info Text set them");
            ok (n.setInfoText (top, ""), "clear the group's"); n.undo(); n.undo();
            check (n.snapshot().tracks[0].infoText.isEmpty(), "two undo steps take both texts back");
            check (n.setInfoText ("missing", "x").failed(), "an unknown track has no info text");
            ok (n.ungroupTracks (outer), "ungroup the outer group");
            const auto flatter = n.snapshot();
            check (flatter.groups.size() == 2 && flatter.tracks[0].groupId == inner && flatter.tracks[1].groupId == top
                       && [&] { for (const auto& g : flatter.groups) if (g.id == inner) return g.parentId == top; return false; }(),
                   "ungrouping moves its tracks and groups into the group around it");
            check (difference (render (n, mix), plain) < 1e-6, "and the mix is the same");
            n.undo(); check (n.snapshot().groups.size() == 3, "undo restores the outer group");
            // Moving tracks into and out of groups, as dragging headers does.
            {
                Session pair (false);
                const auto x = pair.snapshot().tracks[0].id, y = pair.snapshot().tracks[1].id;
                juce::String first, second, both;
                ok (pair.groupTracks ({ x }, first), "group x alone"); ok (pair.groupTracks ({ y }, second), "group y alone");
                check (pair.groupTracks ({ x, y }, both).failed() && ! pair.canGroupTracks ({ x, y }), "grouping that would empty another group is refused, as in the reference");
                check (pair.canGroupTracks ({ x }) && pair.canGroupTracks ({ first, second }) && ! pair.canGroupTracks ({}) && ! pair.canGroupTracks ({ "missing" }),
                       "a group's only track can be grouped inside it, and two groups together, but nothing and unknown tracks cannot");
                ok (pair.moveTracks ({ y }, {}, {}), "move a group's only track out to the end");
                const auto emptied = pair.snapshot();
                const auto infoOf = [] (const Session::Snapshot& shot, const juce::String& id) { for (const auto& g : shot.groups) if (g.id == id) return g; return Session::GroupInfo {}; };
                check (emptied.groups.size() == 2 && infoOf (emptied, second).memberIds.isEmpty() && emptied.tracks[1].id == y && emptied.tracks[1].groupId.isEmpty(),
                       "the group stays, empty, as the reference's does");
                check (infoOf (emptied, first).position == 0 && infoOf (emptied, second).position == 1, "its header sits after the first track, before the one moved out");
                ok (pair.moveTracks ({ y }, second, {}), "move the track back into the empty group");
                check (pair.snapshot().tracks[1].groupId == second, "it is in it again");
                ok (pair.removeTracks ({ first }), "delete the first group");
                check (pair.snapshot().groups.size() == 1 && pair.snapshot().tracks.size() == 1 && pair.snapshot().tracks[0].id == y, "it goes with its track");
                pair.undo();
                check (pair.snapshot().groups.size() == 2 && pair.snapshot().tracks.size() == 2 && pair.snapshot().tracks[0].groupId == first, "one undo brings both back");
                check (pair.removeTracks ({ "missing" }).failed() && pair.removeTracks ({}).failed(), "unknown and empty deletions are refused");
            }
            ok (n.moveTracks ({ ids[2] }, outer, ids[1]), "move c into the outer group, before b");
            const auto moved = n.snapshot();
            check (moved.tracks[1].id == ids[2] && moved.tracks[1].groupId == outer && moved.tracks[2].id == ids[1] && moved.tracks[2].groupId == outer,
                   "c now plays in the outer group, before b");
            check (difference (render (n, mix), plain) < 1e-6, "and the mix is the same");
            check (n.moveTracks ({ top }, outer, {}).failed() && n.moveTracks ({ outer }, inner, {}).failed(), "a group cannot move into itself or a group inside it");
            ok (n.moveTracks ({ ids[1] }, {}, {}), "move b out to the top level");
            const auto out = n.snapshot();
            check (out.tracks.back().id == ids[1] && out.tracks.back().groupId.isEmpty(), "b plays at the top level, after the groups");
            n.undo(); check (n.snapshot().tracks[2].id == ids[1] && n.snapshot().tracks[2].groupId == outer, "one undo puts it back");
            check (n.moveTracks ({ "missing" }, {}, {}).failed() && n.moveTracks ({ ids[1] }, "missing", {}).failed() && n.moveTracks ({ ids[1] }, outer, ids[0]).failed(),
                   "unknown tracks, unknown groups and a place outside the group are refused");
            ok (n.moveTracks ({ ids[2] }, outer, ids[2]), "drop c where it is, before itself");
            check (n.snapshot().tracks[1].id == ids[2] && n.snapshot().tracks[1].groupId == outer, "c stays put");
            n.undo(); check (n.snapshot().tracks[2].id == ids[2] && n.snapshot().tracks[2].groupId == top, "which made no undo step: one undo takes c back out of the outer group");
            // Deleting, as the reference's Delete does: a group goes with everything inside it. As its
            // Delete is enabled, a deletion may not leave a group without tracks, nor the project.
            check (! n.canRemoveTracks ({ top }) && n.removeTracks ({ top }).failed(), "the top group, the project's only item, stays");
            check (! n.canRemoveTracks ({ ids[0] }) && n.removeTracks ({ ids[0], ids[2] }).failed(), "a, the inner group's only track, stays, alone or with c");
            check (n.snapshot().tracks.size() == 3 && n.snapshot().groups.size() == 3, "the refusals change nothing");
            ok (n.addAudioTrack(), "add a track after the tree");
            const auto added = n.snapshot().tracks[3].id;
            check (n.canRemoveTracks ({ top }), "with it there the top group can go");
            ok (n.removeTracks ({ top }), "delete the top group");
            check (n.snapshot().tracks.size() == 1 && n.snapshot().tracks[0].id == added && n.snapshot().groups.empty(), "its groups and tracks, at every level, go with it");
            n.undo();
            check (n.snapshot().tracks.size() == 4 && n.snapshot().groups.size() == 3 && n.snapshot().tracks[0].groupId == inner, "one undo restores the whole tree");
            check (difference (render (n, mix), plain) < 1e-6, "playing as before");
            ok (n.removeTracks ({ inner, ids[2] }), "delete the inner group and c together");
            check (n.snapshot().tracks.size() == 2 && n.snapshot().tracks[0].id == ids[1] && n.snapshot().groups.size() == 2, "b, the added track and the other groups stay");
            n.undo(); check (n.snapshot().tracks.size() == 4, "one undo restores both");
            ok (n.removeTracks ({ added }), "delete the added track, the top group staying");
            {
                // Duplicating, as the reference's Duplicate does: one block right after the last of them
                // (after a group's last track), inside the group that one is in, a group with everything in it.
                juce::StringArray copies;
                ok (n.duplicateTracks ({ outer }, copies), "duplicate the outer group");
                const auto twice = n.snapshot();
                const auto groupIn = [&twice] (const juce::String& id) { for (const auto& g : twice.groups) if (g.id == id) return g; return Session::GroupInfo {}; };
                check (copies.size() == 1 && groupIn (copies[0]).parentId == top && twice.groups.size() == 5 && twice.tracks.size() == 5,
                       "a copy of the outer group, in the top group, with what is inside it");
                check (twice.tracks[0].id == ids[0] && twice.tracks[1].id == ids[1] && twice.tracks[4].id == ids[2]
                           && twice.tracks[2].groupId != inner && groupIn (twice.tracks[2].groupId).parentId == copies[0] && twice.tracks[3].groupId == copies[0],
                       "after b, the outer group's last track, and before c: a copy of a in a copy of the inner group, then a copy of b");
                ok (n.setGroupMute (outer, true), "mute the original");
                check (difference (render (n, mix), plain) < 1e-6, "the copies play as the originals did");
                n.undo(); n.undo();
                check (n.snapshot().groups.size() == 3 && n.snapshot().tracks.size() == 3, "two undo steps remove the mute and the whole copy");
                ok (n.duplicateTracks ({ ids[2], ids[0] }, copies), "duplicate a and c together");
                const auto pair = n.snapshot();
                check (copies.size() == 2 && pair.tracks.size() == 5 && pair.tracks[2].id == ids[2] && pair.tracks[3].id == copies[0] && pair.tracks[4].id == copies[1]
                           && pair.tracks[3].groupId == top && pair.tracks[4].groupId == top,
                       "their copies go together after c, in its group, in track order, a's leaving the inner group");
                n.undo();
                ok (n.duplicateTracks ({ ids[1] }, copies), "duplicate b alone");
                check (n.snapshot().tracks.size() == 4 && n.snapshot().tracks[2].id == copies[0] && n.snapshot().tracks[2].groupId == outer, "its copy goes right after it, in the outer group");
                n.undo();
                juce::StringArray kept { "keep" };
                check (n.duplicateTracks ({ "missing" }, kept).failed() && n.duplicateTracks ({}, kept).failed() && kept == juce::StringArray { "keep" } && n.canRedo(),
                       "unknown and empty duplications are refused, leaving the result and redo alone");
                check (n.snapshot().tracks.size() == 3 && n.snapshot().groups.size() == 3, "and the tree as it was");
                // Copy, Cut and Paste, as the reference's headers have them: Paste puts the copies right
                // before the first chosen track, in its group.
                check (! n.hasTrackClipboard(), "nothing is copied at first");
                ok (n.copyTracks ({ outer }), "copy the outer group");
                check (n.hasTrackClipboard() && n.snapshot().tracks.size() == 3 && n.snapshot().groups.size() == 3, "copying changes nothing in the project");
                juce::StringArray pasted;
                ok (n.pasteTracks ({ ids[2], ids[1] }, pasted), "paste with b and c chosen");
                const auto after = n.snapshot();
                const auto parentOf = [&after] (const juce::String& id) { for (const auto& g : after.groups) if (g.id == id) return g.parentId; return juce::String ("?"); };
                check (pasted.size() == 1 && after.tracks.size() == 5 && after.tracks[0].id == ids[0] && after.tracks[1].groupId != inner && parentOf (after.tracks[1].groupId) == pasted[0]
                           && after.tracks[2].groupId == pasted[0] && after.tracks[3].id == ids[1] && after.tracks[4].id == ids[2] && parentOf (pasted[0]) == outer,
                       "the copy of the outer group goes right before b, the first chosen, inside the outer group, with copies of what was inside");
                n.undo();
                ok (n.pasteTracks ({ ids[2] }, pasted), "paste with c chosen");
                check (n.snapshot().tracks.size() == 5 && n.snapshot().tracks[4].id == ids[2] && n.snapshot().tracks[3].groupId == pasted[0], "the copy goes right before c, beside the outer group");
                ok (n.setGroupMute (outer, true), "mute the original");
                check (difference (render (n, mix), plain) < 1e-6, "the pasted tracks play as the originals did");
                n.undo(); n.undo();
                check (n.snapshot().tracks.size() == 3, "two undo steps remove the mute and the paste");
                ok (n.pasteTracks ({}, pasted), "paste with nothing chosen");
                check (n.snapshot().tracks.size() == 5 && parentOf (pasted[0]) == "?" && [&] { for (const auto& g : n.snapshot().groups) if (g.id == pasted[0]) return g.parentId.isEmpty(); return false; }(),
                       "it goes after the other tracks, at the top level");
                n.undo();
                check (n.cutTracks ({ ids[0] }).failed() && ! n.canCutTracks ({ ids[0] }) && n.snapshot().tracks.size() == 3, "a, the inner group's only track, cannot be cut");
                ok (n.cutTracks ({ ids[1] }), "cut b");
                check (n.snapshot().tracks.size() == 2 && n.snapshot().tracks[1].id == ids[2], "b goes");
                ok (n.pasteTracks ({ ids[2] }, pasted), "paste it with c chosen");
                check (n.snapshot().tracks.size() == 3 && n.snapshot().tracks[1].id == pasted[0] && n.snapshot().tracks[1].groupId == top && n.snapshot().tracks[2].id == ids[2]
                           && n.snapshot().tracks[1].userName == tree.tracks[1].userName && n.snapshot().tracks[1].clips.size() == tree.tracks[1].clips.size(),
                       "b's copy comes back right before c, with its own name (automatic, numbered by its place) and clip: " + n.snapshot().tracks[1].name + " / " + tree.tracks[1].name);
                n.undo(); n.undo();
                check (n.snapshot().tracks.size() == 3 && n.snapshot().tracks[1].id == ids[1], "two undo steps undo the paste and the cut");
                // A copying drag puts copies where a move would go; a group can be copied into itself.
                juce::StringArray copied;
                ok (n.copyTracksTo ({ ids[1] }, inner, ids[0], copied), "copy b into the inner group, before a");
                check (copied.size() == 1 && n.snapshot().tracks.size() == 4 && n.snapshot().tracks[0].id == copied[0] && n.snapshot().tracks[0].groupId == inner
                           && n.snapshot().tracks[2].id == ids[1] && n.snapshot().tracks[2].groupId == outer,
                       "the copy goes first in the inner group, and b stays in the outer one");
                n.undo();
                ok (n.copyTracksTo ({ outer }, outer, {}, copied), "copy the outer group into itself, at its end");
                const auto inside = n.snapshot();
                const auto parentIn = [&inside] (const juce::String& id) { for (const auto& g : inside.groups) if (g.id == id) return g.parentId; return juce::String ("?"); };
                check (inside.groups.size() == 5 && parentIn (copied[0]) == outer && inside.tracks.size() == 5 && inside.tracks[1].id == ids[1] && inside.tracks[4].id == ids[2]
                           && parentIn (inside.tracks[2].groupId) == copied[0] && inside.tracks[3].groupId == copied[0],
                       "its copy, with copies of what was inside, goes after b, inside the outer group, before c");
                n.undo();
                ok (n.copyTracksTo ({ ids[2] }, {}, {}, copied), "copy c to the top level's end");
                check (n.snapshot().tracks.size() == 4 && n.snapshot().tracks[3].id == copied[0] && n.snapshot().tracks[3].groupId.isEmpty(), "its copy is the last track, outside the groups");
                n.undo();
                check (n.copyTracksTo ({ ids[1] }, "missing", {}, copied).failed() && n.copyTracksTo ({ ids[1] }, inner, ids[2], copied).failed()
                           && n.snapshot().tracks.size() == 3,
                       "an unknown group, or a place outside the group, is refused");
            }
        }
        {
            // Returns move among the returns, and other tracks stay before them, as the reference's drop
            // rules keep them.
            Session r (false);
            ok (r.addReturnTrack ("A"), "a first return"); ok (r.addReturnTrack ("B"), "a second"); ok (r.addReturnTrack ("C"), "a third");
            const auto order = [&r] { juce::StringArray ids; for (const auto& t : r.snapshot().tracks) ids.add (t.id); return ids; };
            const auto start = order();
            const auto t0 = start[0], t1 = start[1], ra = start[2], rb = start[3], rc = start[4];
            ok (r.moveTracks ({ rc }, {}, ra), "move the third return before the first");
            check (order() == juce::StringArray { t0, t1, rc, ra, rb }, "the returns reorder among themselves");
            ok (r.moveTracks ({ rc }, {}, {}), "move it to the end of the returns");
            check (order() == start, "it is last again");
            r.undo(); check (order() == juce::StringArray { t0, t1, rc, ra, rb }, "one undo moves it back");
            r.redo();
            check (r.moveTracks ({ ra }, {}, t0).failed() && r.moveTracks ({ t0 }, {}, rb).failed() && r.moveTracks ({ t0, ra }, {}, {}).failed(),
                   "a return cannot go among the other tracks, nor a track among the returns, nor both at once");
            ok (r.moveTracks ({ t0 }, {}, ra), "a track can go before the first return, the other tracks' end");
            check (order() == juce::StringArray { t1, t0, ra, rb, rc }, "it is the last track before the returns");
            for (int i = 0; i < 9; ++i) ok (r.addReturnTrack(), "returns up to twelve");
            check (r.addReturnTrack().failed() && r.snapshot().tracks.size() == 14, "a thirteenth is refused, as the reference's Insert Return Track allows twelve");
        }
        {
            // Group automation and mapping: a group's mixer is automated and mapped as a track's.
            Session m (false);
            ok (m.importAudio (a, 0), "import a for group automation");
            const auto dry = render (m, mix);
            juce::String bus; ok (m.groupTracks ({ m.snapshot().tracks[0].id }, bus), "group it");
            Session::AutomationLane lane; lane.points = { { 0, -6.0206f, 0 }, { 64, -6.0206f, 0 } };
            ok (m.setTrackAutomation (bus, Session::AutomationTarget::volume, lane), "automate the group's volume at -6 dB");
            check (m.snapshot().groups[0].volumeAutomation.points.size() == 2 && m.snapshot().groups[0].volumeAutomation.enabled, "the group's volume lane holds its points");
            check (difference (render (m, mix), dry, .5f, .5f) < 1e-4, "the automated group plays its member 6 dB down");
            const auto project = root.getChildFile ("GroupAutomation.halftrack"); ok (m.saveProject (project), "save");
            Session reopened (false); ok (reopened.loadProject (project), "reopen the group's automation");
            check (reopened.snapshot().groups[0].volumeAutomation.points.size() == 2 && difference (render (reopened, mix), dry, .5f, .5f) < 1e-4, "and it plays as before");
            ok (m.setGroupGain (bus, 0), "move the automated group fader");
            check (! m.snapshot().groups[0].volumeAutomation.enabled && m.snapshot().automationOverridden, "moving it overrides the lane, as the reference does");
            check (difference (render (m, mix), dry) < 1e-4, "and the group plays at the fader's level");
            ok (m.reEnableAutomation(), "re-enable automation");
            check (m.snapshot().groups[0].volumeAutomation.enabled && difference (render (m, mix), dry, .5f, .5f) < 1e-4, "the lane plays again");
            Session::AutomationLane pans; pans.points = { { 0, -1, 0 }, { 64, -1, 0 } };
            ok (m.setTrackAutomation (bus, Session::AutomationTarget::pan, pans), "automate the group's pan hard left");
            check (m.snapshot().groups[0].panAutomation.points.size() == 2, "the group's pan lane holds its points");
            ok (m.setTrackAutomation (bus, Session::AutomationTarget::pan, {}), "clear it");
            for (const auto* control : { "volume", "pan", "leftsplit", "rightsplit" })
                check (m.isMappableTarget ("mixer:" + bus + ":" + control), juce::String ("the group's ") + control + " is a MIDI and key mapping target");
            check (! m.isMappableTarget ("mixer:missing:volume"), "an unknown group's is not");
        }
        {
            // Group sends, as the reference's group mixers have one per return: the group's sum feeds the return.
            Session g (false);
            ok (g.importAudio (a, 0), "import a for the group send"); ok (g.addReturnTrack ("Room"), "add a return");
            const auto dry = render (g, mix);
            const auto aId = g.snapshot().tracks[0].id, room = g.snapshot().tracks.back().id;
            juce::String bus; ok (g.groupTracks ({ aId }, bus), "group a");
            check (g.snapshot().groups[0].sends.size() == 1 && g.snapshot().groups[0].sends[0].returnTrackId == room && g.snapshot().groups[0].sends[0].gainDb <= -100,
                   "the group has a send to the return, Off");
            ok (g.setGroupSendGain (bus, room, -6.0206f), "send the group to the return at half level");
            check (std::abs (g.snapshot().groups[0].sends[0].gainDb + 6.0206f) < 1e-3, "the send keeps its level");
            check (difference (render (g, mix), dry, 1.5f, 1.5f) < 1e-5, "the mix gains the group's sum at half level, through the return");
            g.undo(); check (g.snapshot().groups[0].sends[0].gainDb <= -100, "one undo turns it Off"); g.redo();
            check (g.setGroupSendGain ("missing", room, -6).failed() && g.setGroupSendGain (bus, aId, -6).failed() && g.setGroupSendGain (bus, room, 3).failed(),
                   "unknown groups, a track that is not a return and levels above 0 dB are refused");
            ok (g.setReturnSendMode (room, Session::SendMode::pre), "make the return's sends pre-fader");
            ok (g.setGroupGain (bus, -100), "pull the group's fader down");
            check (difference (render (g, mix), dry, .5f, .5f) < 1e-5, "the pre-fader send still feeds the return");
            ok (g.setGroupGain (bus, 0), "the group's fader back up"); ok (g.setReturnSendMode (room, Session::SendMode::post), "post-fader again");
            ok (g.importAudio (b, -1), "add an outside track, b");
            const auto withOutside = render (g, mix);
            ok (g.setGroupSolo (bus, true), "solo the group");
            check (difference (render (g, mix), dry, 1.5f, 1.5f) < 1e-5,
                   "soloing the group silences the outside track and keeps the return the group feeds audible, as Solo in Place does for tracks");
            ok (g.setGroupSolo (bus, false), "unsolo it");
            ok (g.setGroupMute (bus, true), "mute the group");
            ok (g.setGroupSendGain (bus, room, -100), "and turn its send Off, for b alone");
            const auto bAlone = render (g, mix);
            ok (g.setGroupSendGain (bus, room, -6.0206f), "turn the send back on");
            check (difference (render (g, mix), bAlone) < 1e-6, "a muted group's post-fader send is silent");
            ok (g.setGroupMute (bus, false), "unmute the group");
            check (g.isMappableTarget ("send:" + bus + ":" + room), "the group's send is a MIDI and key mapping target");
            Session::AutomationLane lane; lane.points = { { 0, -100, 0 }, { 4, 0, 0 } };
            ok (g.setSendAutomation (bus, room, lane), "automate the group's send");
            check (g.snapshot().groups[0].sends[0].automation.points.size() == 2, "the group's send has its automation");
            ok (g.setSendAutomation (bus, room, {}), "clear it");
            const auto project = root.getChildFile ("GroupSend.halftrack"); ok (g.saveProject (project), "save");
            Session reopened (false); ok (reopened.loadProject (project), "reopen the group's send");
            check (std::abs (reopened.snapshot().groups[0].sends[0].gainDb + 6.0206f) < 1e-3, "the group's send reopens");
            check (difference (render (reopened, mix), withOutside) < 1e-5, "and plays as before");
            ok (g.removeTracks ({ room }), "delete the return");
            check (g.snapshot().groups[0].sends.empty() && difference (render (g, mix), bAlone, 1, 1) > 1e-3, "the group's send goes with it, the group still playing");
            g.undo(); check (g.snapshot().groups[0].sends.size() == 1 && std::abs (g.snapshot().groups[0].sends[0].gainDb + 6.0206f) < 1e-3, "one undo brings the return and the send back");
        }
        std::cout << "Groups passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL groups: " << e.what() << std::endl; return 1; }
}
}
