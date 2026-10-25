#include "Session.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace halftrack
{
void useHardClipEdges();
namespace
{
void check (bool value, const juce::String& message)
{
    if (! value) throw std::runtime_error (message.toStdString());
    std::cout << "PASS track order: " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{ check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : "")); }
std::vector<juce::String> order (const Session::Snapshot& state)
{
    std::vector<juce::String> ids;
    for (const auto& track : state.tracks) ids.push_back (track.id);
    return ids;
}
const Session::TrackInfo& find (const Session::Snapshot& state, const juce::String& id)
{
    for (const auto& track : state.tracks) if (track.id == id) return track;
    throw std::runtime_error ("Track identity disappeared");
}
void sameClip (const Session::ClipInfo& a, const Session::ClipInfo& b)
{
    check (a.id == b.id && a.name == b.name && a.start == b.start && a.length == b.length
        && a.sourceOffset == b.sourceOffset && a.isMidi == b.isMidi && a.isAudio == b.isAudio
        && a.audio == b.audio && a.notes.size() == b.notes.size(), "clip identity and settings stay with their track");
    for (size_t n = 0; n < a.notes.size(); ++n)
        check (a.notes[n].id == b.notes[n].id && a.notes[n].pitch == b.notes[n].pitch
            && a.notes[n].startBeat == b.notes[n].startBeat && a.notes[n].lengthBeats == b.notes[n].lengthBeats
            && a.notes[n].velocity == b.notes[n].velocity, "slot note identity and content survive ordering");
}
void retained (const Session::Snapshot& before, const Session::Snapshot& after)
{
    check (before.tracks.size() == after.tracks.size(), "reordering retains every track");
    for (const auto& a : before.tracks)
    {
        const auto& b = find (after, a.id);
        // Automatic names renumber as tracks move, as the reference's do; a name of its own stays.
        check (a.userName == b.userName && a.colour == b.colour && a.isMidi == b.isMidi && a.isReturn == b.isReturn
            && a.gainDb == b.gainDb && a.pan == b.pan && a.mute == b.mute && a.solo == b.solo && a.armed == b.armed
            && a.monitor == b.monitor && a.inputName == b.inputName && a.sendMode == b.sendMode
            && a.plugins.size() == b.plugins.size() && a.clips.size() == b.clips.size()
            && a.sends.size() == b.sends.size() && a.slots.size() == b.slots.size(), "track controls and routing dimensions survive ordering");
        for (size_t i = 0; i < a.plugins.size(); ++i)
            check (a.plugins[i].id == b.plugins[i].id && a.plugins[i].name == b.plugins[i].name
                && a.plugins[i].enabled == b.plugins[i].enabled, "device identities and order stay with their track");
        for (const auto& send : a.sends)
        {
            const auto match = std::find_if (b.sends.begin(), b.sends.end(), [&] (const auto& other) { return other.returnTrackId == send.returnTrackId; });
            check (match != b.sends.end() && match->gainDb == send.gainDb, "send destination identity and gain survive ordering");
        }
        for (size_t i = 0; i < a.clips.size(); ++i) sameClip (a.clips[i], b.clips[i]);
        for (size_t i = 0; i < a.slots.size(); ++i)
        {
            check (a.slots[i].id == b.slots[i].id && a.slots[i].hasClip == b.slots[i].hasClip, "Session slot identity stays with its track");
            if (a.slots[i].hasClip) sameClip (a.slots[i].clip, b.slots[i].clip);
        }
    }
}
juce::AudioBuffer<float> read (const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader && reader->numChannels == 2 && reader->sampleRate == 48000 && reader->lengthInSamples == 4800,
        "render has expected complete stereo extent");
    juce::AudioBuffer<float> audio (2, 4800);
    check (reader->read (&audio, 0, 4800, 0, true, true), "read complete generated render"); return audio;
}
void render (Session& session, const juce::File& file, const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    ok (session.exportWav (file), "render reordered signal graph"); const auto actual = read (file);
    double worst = 0;
    // Dry gains 1/2 and 1/4. Post sends 1/4 and 1/8 feed a return at 1/2.
    for (int c = 0; c < 2; ++c) for (int n = 0; n < 4800; ++n)
    {
        const double value = actual.getSample (c, n), expected = a.getSample (c, n) * .5625 + b.getSample (c, n) * .265625;
        if (! std::isfinite (value)) throw std::runtime_error ("Nonfinite reordered graph sample");
        worst = std::max (worst, std::abs (value - expected));
    }
    check (worst < 2e-6, "every sample matches the independent dry-plus-return mix after reordering");
}
}
int runTrackOrderTests (const juce::File& artifacts)
{
    try
    {
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
        const auto root = artifacts.getChildFile ("track-order-" + juce::Uuid().toString()); ok (root.createDirectory(), "create generated fixture directory");
        const auto a = root.getChildFile ("a.wav"), b = root.getChildFile ("b.wav"), output = root.getChildFile ("mix.wav"), project = root.getChildFile ("order.halftrack");
        for (int file = 0; file < 2; ++file)
        {
            juce::AudioBuffer<float> samples (2, 4800);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 4800; ++n)
                samples.setSample (c, n, ((n * (17+file*4) + c*13) % 97 - 48) / 1024.0f);
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream ((file == 0 ? a : b).createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
            check (writer && writer->writeFromAudioSampleBuffer (samples, 0, 4800), "write independently generated stereo media");
        }
        const auto first = read (a), second = read (b); Session session (false);
        ok (session.importAudio (a, 0), "import first source"); ok (session.importAudio (b, 1), "import second source");
        ok (session.addMidiTrack ("MIDI slots"), "add MIDI track"); ok (session.addReturnTrack ("Shared return"), "add return track");
        auto state = session.snapshot(); const auto midi = state.tracks[2].id, ret = state.tracks[3].id;
        ok (session.addMidiClipToSlot (midi, 0, 4), "create Session MIDI content"); state = session.snapshot();
        ok (session.addMidiNote (state.tracks[2].slots[0].clip.id, 67, .5, 1, .7f), "add persistent Session note");
        ok (session.importAudioToSlot (a, 0, 1), "create Session audio content");
        const auto db = [] (float value) { return 20.0f * std::log10 (value); };
        ok (session.setTrackGain (0, db (.5f)), "set first dry level"); ok (session.setTrackGain (1, db (.25f)), "set second dry level");
        ok (session.setTrackGain (3, db (.5f)), "set return level"); ok (session.setTrackSendGain (0, ret, db (.25f)), "send first source");
        ok (session.setTrackSendGain (1, ret, db (.125f)), "send second source");
        ok (session.saveProject (project), "save initial order"); ok (session.loadProject (project), "reopen clean initial order");
        const auto baseline = session.snapshot(); const auto original = order (baseline);
        ok (session.moveTrack (original[0], 0), "same-position no-op"); check (!session.canUndo() && !session.snapshot().dirty, "no-op keeps clean history");
        render (session, output, first, second);
        for (int from = 0; from < 4; ++from) for (int to = 0; to < 4; ++to)
        {
            if (from == to) continue;
            // The return, last, stays after the other tracks, as in the reference.
            if (from == 3 || to == 3)
            {
                check (session.moveTrack (original[(size_t) from], to).failed() && order (session.snapshot()) == original,
                       "a move across the return is refused, keeping the order");
                continue;
            }
            auto expected = original; const auto id = expected[(size_t)from]; expected.erase (expected.begin()+from); expected.insert (expected.begin()+to, id);
            ok (session.moveTrack (id, to), "move between arbitrary top-level positions");
            check (order (session.snapshot()) == expected, "native track order matches requested final index"); retained (baseline, session.snapshot());
            session.undo(); check (order (session.snapshot()) == original, "one undo restores all original positions");
            check (session.moveTrack ("missing", 0).failed() && session.moveTrack (id, -1).failed() && session.moveTrack (id, 4).failed() && session.canRedo(),
                "invalid identities and indices preserve redo");
            session.redo(); check (order (session.snapshot()) == expected, "redo restores reordered identities"); session.undo();
        }
        session.beginEditGesture ("Reorder multiple tracks");
        ok (session.moveTrack (original[0], 2), "move the first audio source last before the return");
        ok (session.moveTrack (midi, 0), "move MIDI first"); session.endEditGesture();
        const auto finalOrder = order (session.snapshot()); retained (baseline, session.snapshot()); render (session, output, first, second);
        session.undo(); check (order (session.snapshot()) == original, "grouped reorder is one undo"); session.redo();
        check (order (session.snapshot()) == finalOrder, "grouped reorder redo retains order");
        ok (session.saveProject (project), "save reordered tracks and routing"); ok (session.loadProject (project), "reopen reordered tracks and routing");
        check (order (session.snapshot()) == finalOrder, "saved order is persistent"); retained (baseline, session.snapshot()); render (session, output, first, second);
        // Track heights, saved with the project outside the undo history, and moving with their tracks.
        {
            const auto id = session.snapshot().tracks[1].id;
            const bool couldUndo = session.canUndo();
            check (session.setTrackHeight (id, Session::minimumTrackHeight - 1).failed() && session.setTrackHeight (id, Session::maximumTrackHeight + 1).failed()
                       && session.setTrackHeight ("missing", 100).failed(), "heights outside 20 to 600 pixels, or for no track, are refused");
            ok (session.setTrackHeight (id, 140), "make a track 140 pixels high");
            check (session.snapshot().tracks[1].height == 140 && session.snapshot().tracks[0].height == 0 && session.canUndo() == couldUndo && ! session.snapshot().dirty,
                   "it is that high, the others default, and it is neither an undo step nor an unsaved change");
            ok (session.moveTrack (id, 0), "move it first");
            check (session.snapshot().tracks[0].id == id && session.snapshot().tracks[0].height == 140, "its height moves with it");
            ok (session.saveProject (project), "save the height"); ok (session.loadProject (project), "reopen");
            check (session.snapshot().tracks[0].id == id && session.snapshot().tracks[0].height == 140, "the height is saved with the project");
            ok (session.setTrackHeight (id, 0), "back to the default");
            check (session.snapshot().tracks[0].height == 0, "0 restores the default height");
            ok (session.setTrackFolded (id, true), "fold it");
            check (session.snapshot().tracks[0].folded && ! session.snapshot().tracks[1].folded && session.canUndo() == couldUndo, "it is folded, outside the undo history");
            ok (session.saveProject (project), "save the fold"); ok (session.loadProject (project), "reopen");
            check (session.snapshot().tracks[0].folded && session.setTrackFolded ("missing", true).failed(), "the fold is saved; a missing track is refused");
            ok (session.setTrackFolded (id, false), "unfold it");
            // And Session column widths, likewise.
            check (session.setSessionWidth (id, Session::minimumSessionWidth - 1).failed() && session.setSessionWidth (id, Session::maximumSessionWidth + 1).failed()
                       && session.setSessionWidth ("missing", 100).failed(), "widths outside 60 to 400 pixels, or for no track, are refused");
            ok (session.setSessionWidth (id, 180), "make its Session column 180 pixels wide");
            check (session.snapshot().tracks[0].sessionWidth == 180 && session.snapshot().tracks[1].sessionWidth == 0 && session.canUndo() == couldUndo && ! session.snapshot().dirty,
                   "it is that wide, the others default, outside the undo history");
            ok (session.saveProject (project), "save the width"); ok (session.loadProject (project), "reopen");
            check (session.snapshot().tracks[0].sessionWidth == 180, "the width is saved with the project");
            ok (session.setSessionWidth (id, 0), "back to the default");
            check (session.snapshot().tracks[0].sessionWidth == 0, "0 restores the default width");
        }
        {
            // Return tracks stay after every other track, as in the reference: new tracks go before them,
            // and a project holding a return among the other tracks opens with it last.
            Session s (false);
            ok (s.addReturnTrack ("Return"), "a return"); ok (s.addMidiTrack ("Later MIDI"), "then a MIDI track"); ok (s.addAudioTrack ("Later audio"), "and an audio track");
            auto tracks = s.snapshot().tracks;
            check (tracks.size() == 5 && tracks[2].name == "Later MIDI" && tracks[3].name == "Later audio" && tracks[4].isReturn,
                   "new tracks go before the return");
            s.undo(); s.undo();
            check (s.snapshot().tracks.size() == 3 && s.snapshot().tracks.back().isReturn, "and each undoes in one step");
            const auto mixed = root.getChildFile ("Mixed").getChildFile ("Mixed.halftrack");
            ok (s.saveProject (mixed), "save a project with the return last");
            auto xml = juce::XmlDocument::parse (mixed);
            juce::XmlElement* edit = xml != nullptr ? xml->getChildByName ("EDIT") : nullptr;
            juce::XmlElement *firstTrack = nullptr, *returnTrack = nullptr;
            if (edit != nullptr)
                for (auto* child : edit->getChildIterator())
                    if (child->hasTagName ("TRACK")) { if (firstTrack == nullptr) firstTrack = child; if (child->getBoolAttribute ("halftrackReturn")) returnTrack = child; }
            check (firstTrack != nullptr && returnTrack != nullptr && firstTrack != returnTrack, "find the saved tracks");
            auto moved = std::make_unique<juce::XmlElement> (*returnTrack);
            edit->removeChildElement (returnTrack, true);
            int at = 0; for (auto* child : edit->getChildIterator()) { if (child == firstTrack) break; ++at; }
            edit->insertChildElement (moved.release(), at);
            check (xml->writeTo (mixed), "write the return first, as an earlier version could have");
            ok (s.loadProject (mixed), "open it");
            tracks = s.snapshot().tracks;
            check (tracks.size() == 3 && ! tracks[0].isReturn && ! tracks[1].isReturn && tracks[2].isReturn && ! s.canUndo(), "the return opens last, outside the undo history");
            // The Session mixer's sections are kept with the project as view state.
            check (s.mixerSections() == Session::MixerSections {}, "the mixer's sections start as the reference's default Set has them");
            Session::MixerSections shown; shown.inOut = false; shown.crossfader = true; shown.extraVolumeHeight = 90;
            s.setMixerSections (shown);
            check (s.snapshot().mixerSections == shown && ! s.canUndo() && s.snapshot().dirty, "choosing them marks the project changed, outside the undo history");
            ok (s.saveProject (mixed), "save them"); ok (s.newProject(), "a new project");
            check (s.mixerSections() == Session::MixerSections {}, "which has the defaults");
            ok (s.loadProject (mixed), "reopen"); check (s.mixerSections() == shown, "and the saved sections come back");
            // So is the Arrangement's overview, shown at first as in the reference's default Set.
            ok (s.newProject(), "another new project");
            check (s.arrangementOverview() && s.snapshot().arrangementOverview, "the overview shows at first");
            s.setArrangementOverview (false);
            check (! s.snapshot().arrangementOverview && ! s.canUndo() && s.snapshot().dirty, "hiding it marks the project changed, outside the undo history");
            ok (s.saveProject (mixed), "save it hidden"); ok (s.newProject(), "a new project again");
            check (s.arrangementOverview(), "which shows it");
            ok (s.loadProject (mixed), "reopen once more"); check (! s.arrangementOverview(), "and the saved project keeps it hidden");
            // And the time ruler's format: minutes and seconds at first, as in the reference's default Set.
            check (s.timeRulerFormat() == Session::TimeFormat::minutesSeconds, "the time ruler counts minutes and seconds at first");
            s.setTimeRulerFormat (Session::TimeFormat::smpte30Drop);
            check (s.snapshot().timeRulerFormat == Session::TimeFormat::smpte30Drop && ! s.canUndo(), "choosing SMPTE 30 Drop is view state, outside the undo history");
            ok (s.saveProject (mixed), "save the format"); ok (s.newProject(), "a new project for the format");
            check (s.timeRulerFormat() == Session::TimeFormat::minutesSeconds, "which counts minutes and seconds");
            ok (s.loadProject (mixed), "reopen with the format"); check (s.timeRulerFormat() == Session::TimeFormat::smpte30Drop, "and the format comes back");
            // And whether the scenes show their tempo and time signature: hidden at first, as in the default Set.
            check (! s.sceneSettingsShown(), "the scenes' tempo and time signature are hidden at first");
            s.setSceneSettingsShown (true);
            check (s.snapshot().sceneSettingsShown && ! s.canUndo(), "showing them is view state, outside the undo history");
            ok (s.saveProject (mixed), "save them shown"); ok (s.newProject(), "a new project for the scenes");
            check (! s.sceneSettingsShown(), "which hides them");
            ok (s.loadProject (mixed), "reopen with them shown"); check (s.sceneSettingsShown(), "and the saved project shows them");
            // And the Arrangement's track controls, as the reference's default Set has them at first.
            check (s.arrangementControls() == Session::ArrangementControls {} && s.snapshot().arrangementControls.returns && ! s.snapshot().arrangementControls.trackOptions,
                   "the Arrangement shows In/Out, Volume and Returns at first, without Track Options");
            Session::ArrangementControls controls; controls.inOut = false; controls.trackOptions = true; controls.returns = false;
            s.setArrangementControls (controls);
            check (s.snapshot().arrangementControls == controls && ! s.canUndo() && s.snapshot().dirty, "choosing them is view state, outside the undo history");
            ok (s.saveProject (mixed), "save the Arrangement's controls"); ok (s.newProject(), "a new project for the controls");
            check (s.arrangementControls() == Session::ArrangementControls {}, "which has the defaults");
            ok (s.loadProject (mixed), "reopen with the controls"); check (s.arrangementControls() == controls, "and the saved controls come back");
            // And the Arrangement's mixer: hidden at first, with its own sections, as the reference's default Set has them.
            {
                const auto first = s.snapshot().arrangementMixerSections;
                check (! first.shown && first.inOut && first.sends && first.mixer && ! first.trackDelay && ! first.crossfader && s.arrangementMixerSections() == first,
                       "the Arrangement's mixer is hidden at first, with In/Out, Sends and Volume and without Track Delay or the crossfader");
                const auto sessionSections = s.mixerSections();
                check (first.extraVolumeHeight == 0 && sessionSections.extraVolumeHeight == 0, "both mixers' faders start at their usual height");
                auto taller = sessionSections; taller.extraVolumeHeight = Session::MixerSections::maximumExtraVolume + 50;
                s.setMixerSections (taller);
                check (s.mixerSections().extraVolumeHeight == Session::MixerSections::maximumExtraVolume, "a taller fader than allowed stops at the limit");
                s.setMixerSections (sessionSections);
                auto arranged = first; arranged.shown = true; arranged.sends = false; arranged.trackDelay = true; arranged.returns = false; arranged.extraVolumeHeight = 40;
                ok (s.saveProject (mixed), "save before showing it");
                s.setArrangementMixerSections (arranged);
                const auto kept = s.snapshot().arrangementMixerSections;
                check (kept.shown && ! kept.sends && kept.trackDelay && kept.returns && kept.extraVolumeHeight == 40 && s.mixerSections() == sessionSections && ! s.canUndo() && s.snapshot().dirty,
                       "showing it and choosing its sections is view state apart from the Session mixer's, its returns following the Arrangement's");
                ok (s.saveProject (mixed), "save the Arrangement's mixer"); ok (s.newProject(), "a new project for the mixer");
                check (s.arrangementMixerSections() == first, "which hides it again");
                ok (s.loadProject (mixed), "reopen with the mixer"); check (s.arrangementMixerSections() == kept, "and the saved mixer comes back");
            }
            // And Lock Envelopes, off at first: an application setting, as the reference's is.
            check (! s.lockEnvelopes(), "Lock Envelopes is off at first");
            ok (s.saveProject (mixed), "save the project");
            s.setLockEnvelopes (true);
            check (s.lockEnvelopes() && s.preferences().lockEnvelopes && ! s.canUndo() && ! s.snapshot().dirty,
                   "turning it on sets a preference, outside the undo history, leaving the project saved");
            ok (s.newProject(), "a new project"); check (s.lockEnvelopes(), "keeps it on");
            {
                Session next (false);
                check (next.lockEnvelopes(), "and so does the next Session");
            }
            s.setLockEnvelopes (false);
            check (! s.lockEnvelopes(), "off again");
            // And Snap Automation to Grid, on at first as in the reference's default Set, kept with the project.
            check (s.snapAutomationToGrid(), "Snap Automation to Grid is on at first");
            s.setSnapAutomationToGrid (false);
            check (! s.snapAutomationToGrid() && ! s.canUndo() && s.snapshot().dirty, "switching it off is view state, outside the undo history");
            ok (s.saveProject (mixed), "save it off"); ok (s.newProject(), "a new project for Snap Automation to Grid");
            check (s.snapAutomationToGrid(), "which has it on");
            ok (s.loadProject (mixed), "reopen with it off"); check (! s.snapAutomationToGrid(), "and the saved project keeps it off");
            // And Delay Compensation, on at first as in the reference's default Set, kept with the project.
            check (s.delayCompensation(), "Delay Compensation is on at first");
            s.setDelayCompensation (false);
            check (! s.delayCompensation() && ! s.canUndo() && s.snapshot().dirty, "switching it off is view state, outside the undo history");
            ok (s.saveProject (mixed), "save it off"); ok (s.newProject(), "a new project for Delay Compensation");
            check (s.delayCompensation(), "which has it on");
            ok (s.loadProject (mixed), "reopen with it off"); check (! s.delayCompensation(), "and the saved project keeps it off");
        }
        {
            // Where Insert puts a track, and automatic names, as the reference's (static reading of its code).
            Session s (false);
            const auto names = [&] { juce::StringArray n; for (const auto& t : s.snapshot().tracks) n.add (t.name); return n; };
            const auto at = [&] (int i) { return s.snapshot().tracks[(size_t) i]; };
            check (names() == juce::StringArray { "1-Audio", "2-Audio" } && at (0).userName.isEmpty(), "a new project's tracks are named automatically");
            ok (s.addMidiTrack ({}, at (0).id), "insert a MIDI track after the first");
            const auto midi = at (1).id;
            check (names() == juce::StringArray { "1-Audio", "2-Subtractive", "3-Audio" }, "it goes right after it, named by its instrument, and the track after it renumbers");
            ok (s.removeTrackInstrument (midi), "remove its instrument");
            check (at (1).name == "2-MIDI", "without an instrument it is 2-MIDI");
            ok (s.addReturnTrack(), "a return"); ok (s.addReturnTrack(), "and another");
            const auto firstReturn = at (3).id, secondReturn = at (4).id;
            ok (s.addReturnTrack ({}, firstReturn), "insert a return after the first");
            check (names() == juce::StringArray { "1-Audio", "2-MIDI", "3-Audio", "A-Return", "B-Return", "C-Return" } && at (3).id == firstReturn && at (5).id == secondReturn,
                   "it goes right after it, and returns are lettered in order");
            ok (s.addAudioTrack ({}, firstReturn), "insert an audio track with a return selected");
            check (names() == juce::StringArray { "1-Audio", "2-MIDI", "3-Audio", "4-Audio", "A-Return", "B-Return", "C-Return" }, "it goes after the other tracks");
            s.undo();
            check (names().size() == 6, "an insertion is one undo step");
            ok (s.addNativeEffect (3, Session::EffectKind::reverb), "give return A a device");
            ok (s.addNativeEffect (3, Session::EffectKind::delay), "and another");
            const auto devices = at (3).plugins;
            check (at (3).name == "A-" + devices[0].name + " | " + devices[1].name, "a return without its own name shows its devices' names (" + at (3).name + ")");
            // Renaming.
            ok (s.renameTrack (0, "Bass"), "rename the first track");
            check (at (0).name == "Bass" && at (0).userName == "Bass", "a name of its own shows without a number");
            s.undo(); check (at (0).name == "1-Audio", "renaming is one undo step"); s.redo();
            ok (s.renameTrack (2, "## Keys"), "a name starting with two #");
            check (at (2).name == "03 Keys", "a leading run of # becomes the number, padded to its length");
            ok (s.renameTrack (2, "  "), "rename to nothing");
            check (at (2).name == "3-Audio" && at (2).userName.isEmpty(), "an empty name makes it automatic again");
            ok (s.renameTrack (2, "3-Audio"), "type the automatic name");
            check (at (2).userName.isEmpty() && s.renameTrack (2, " Lead ").wasOk() && at (2).name == "Lead", "the automatic name stays automatic; a typed one is trimmed");
            ok (s.renameTrack (3, "Verb"), "rename return A");
            check (at (3).name == "A-Verb", "a return keeps its letter before its own name");
            ok (s.renameTrack (3, "A-Hall"), "type the letter too");
            check (at (3).userName == "Hall" && at (3).name == "A-Hall", "the letter is not kept twice");
            ok (s.renameTrack (3, devices[0].name + " | " + devices[1].name), "type its automatic part");
            check (at (3).userName.isEmpty(), "which makes it automatic again");
            // A track's first clip names it, as the reference's does.
            ok (s.renameTrack (2, {}), "track 3 automatic again");
            ok (s.importAudio (a, 2), "give it a clip from a.wav");
            check (at (2).name == "3-a", "its first clip names it");
            ok (s.renameClip (at (2).clips[0].id, "Riff"), "rename the clip");
            check (at (2).name == "3-Riff", "the name follows its only clip");
            ok (s.importAudio (b, 2, 4), "add a second clip");
            ok (s.renameClip (at (2).clips[0].id, "Changed"), "rename the first clip again");
            check (at (2).name == "3-Riff", "with two clips the name stays");
            ok (s.removeClip (2, 1), "remove one"); ok (s.removeClip (2, 0), "and the other");
            check (at (2).name == "3-Audio", "with none it is gone");
            ok (s.importAudio (a, 2), "a new clip");
            ok (s.renameClip (at (2).clips[0].id, "3-Audio 2"), "named only for its track");
            check (at (2).name == "3-Audio", "a clip named only for its track does not name it");
            ok (s.renameClip (at (2).clips[0].id, "Hook"), "name it");
            // Groups count, and grouped tracks after them renumber.
            juce::String group;
            ok (s.groupTracks ({ at (0).id, midi }, group), "group the first two tracks");
            check (names() == juce::StringArray { "Bass", "3-MIDI", "4-Hook", "A-" + devices[0].name + " | " + devices[1].name, "B-Return", "C-Return" }
                       && s.snapshot().groups.size() == 1 && s.snapshot().groups[0].name == "1-Group", "the group is 1-Group and the tracks after it renumber");
            {
                const auto grouped = s.snapshot();
                juce::StringArray labels; for (const auto& t : grouped.tracks) labels.add (t.numberLabel);
                check (labels == juce::StringArray { "2", "3", "4", "A", "B", "C" } && grouped.groups[0].numberLabel == "1",
                       "activators show the tracks' numbers, groups counted, and the returns' letters: " + labels.joinIntoString (" "));
            }
            ok (s.renameGroup (group, "Drums"), "rename the group");
            check (s.snapshot().groups[0].name == "Drums" && s.renameGroup (group, "").wasOk() && s.snapshot().groups[0].name == "1-Group", "a group renames, and back to automatic");
            // Insert after a grouped track goes into the group; after the group, after everything in it.
            ok (s.addAudioTrack ({}, midi), "insert after the grouped MIDI track");
            check (at (2).groupId == group && at (2).name == "4-Audio", "it joins the group after it");
            ok (s.addAudioTrack ({}, group), "insert after the group");
            check (at (3).groupId.isEmpty() && at (3).name == "5-Audio" && at (4).name == "6-Hook", "it goes after the group and everything in it");
            // Automatic names and first clips are saved.
            const auto saved = names();
            const auto named = root.getChildFile ("Named").getChildFile ("Named.halftrack");
            ok (s.saveProject (named), "save"); ok (s.loadProject (named), "reopen");
            check (names() == saved && ! s.snapshot().dirty, "the names reopen as they were, unchanged");
        }
        {
            // The built-in default project, like the reference's built-in default Set.
            Session s (false);
            ok (s.newBuiltInDefaultProject(), "a new project from the built-in default set");
            const auto state = s.snapshot();
            juce::StringArray names; for (const auto& t : state.tracks) names.add (t.name);
            check (state.tracks.size() == 6 && names[0] == "1-MIDI" && names[1] == "2-MIDI" && names[2] == "3-Audio" && names[3] == "4-Audio"
                       && state.tracks[0].isMidi && ! state.tracks[0].hasInstrument && state.tracks[1].isMidi && ! state.tracks[1].hasInstrument
                       && state.tracks[4].isReturn && state.tracks[5].isReturn && state.tracks[4].plugins.size() == 1 && state.tracks[5].plugins.size() == 1
                       && state.tracks[4].plugins[0].effect.kind == Session::EffectKind::reverb && state.tracks[5].plugins[0].effect.kind == Session::EffectKind::delay
                       && names[4] == "A-" + state.tracks[4].plugins[0].name && names[5] == "B-" + state.tracks[5].plugins[0].name,
                   "two MIDI tracks without instruments, two audio tracks, and returns with a reverb and a delay: " + names.joinIntoString (", "));
            check (state.scenes.size() == 8 && std::abs (state.tempo - 120) < 1e-9 && state.timeSignature.numerator == 4 && state.timeSignature.denominator == 4
                       && ! state.dirty && ! s.canUndo() && state.projectFile == juce::File(), "eight scenes, 120 BPM in 4/4, untitled, unchanged and without history");
        }
        std::cout << "Track order tests passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL track order: " << e.what() << std::endl; return 1; }
}
}
