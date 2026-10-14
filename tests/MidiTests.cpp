#include "Session.h"
#include "SubtractiveSynth.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace halftrack
{
namespace
{
void check (bool condition, const juce::String& message)
{
    if (! condition) throw std::runtime_error (message.toStdString());
    std::cout << "PASS MIDI " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{
    check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : ""));
}
Session::TrackInfo instrument (const Session& session)
{
    for (const auto& track : session.snapshot().tracks) if (track.isMidi) return track;
    throw std::runtime_error ("MIDI track missing");
}
Session::SynthSettings flatSettings()
{
    Session::SynthSettings s;
    s.waveform = 1; s.cutoffHz = 4800; s.resonance = 0;
    s.attackSeconds = s.decaySeconds = 0; s.sustain = 1; s.level = 1; s.releaseSeconds = 0.01f;
    return s;
}
std::unique_ptr<juce::AudioFormatReader> readerFor (const juce::File& file)
{
    juce::AudioFormatManager manager; manager.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader> (manager.createReaderFor (file));
}
bool sameNotes (const std::vector<Session::MidiNoteInfo>& a, const std::vector<Session::MidiNoteInfo>& b)
{
    if (a.size() != b.size()) return false;
    for (const auto& note : a)
    {
        const auto found = std::find_if (b.begin(), b.end(), [&] (const auto& other) { return other.id == note.id; });
        if (found == b.end() || note.pitch != found->pitch || note.startBeat != found->startBeat
            || note.lengthBeats != found->lengthBeats || note.velocity != found->velocity) return false;
    }
    return true;
}
void batchEditTests (const juce::File& directory)
{
    Session session (false);
    ok (session.addMidiTrack ("Batch edits"), "create batch edit instrument");
    const auto trackId = instrument (session).id;
    ok (session.addMidiClip (trackId, 0, 4), "create batch edit clip");
    const auto clipId = instrument (session).clips[0].id;
    ok (session.addMidiNote (clipId, 60, 0, 1, 0.5f), "add first batch note");
    ok (session.addMidiNote (clipId, 64, 2, 1, 1), "add second batch note");
    const auto project = directory.getChildFile ("Batch.halftrack");
    ok (session.saveProject (project), "save batch baseline");
    ok (session.loadProject (project), "load clean batch baseline");
    auto current = [&] { return instrument (session).clips[0].notes; };
    const auto original = current();
    check (! session.canUndo() && ! session.canRedo() && ! session.snapshot().dirty, "batch baseline is clean without history");
    auto unchanged = original; unchanged[0].velocity = 0.5f;
    ok (session.updateMidiNotes (clipId, {}), "empty batch succeeds without editing");
    ok (session.updateMidiNotes (clipId, unchanged), "native-quantized unchanged batch succeeds without editing");
    check (sameNotes (current(), original) && ! session.canUndo() && ! session.snapshot().dirty,
           "empty and quantized unchanged batches preserve clean state");

    auto edited = original;
    edited[0].pitch = 65; edited[0].startBeat = 0.25; edited[0].lengthBeats = 0.5; edited[0].velocity = 80 / 127.0f;
    edited[1].pitch = 67; edited[1].startBeat = 2.25; edited[1].lengthBeats = 1.5; edited[1].velocity = 100 / 127.0f;
    ok (session.updateMidiNotes (clipId, edited), "edit all note fields in one batch");
    check (sameNotes (current(), edited), "valid batch changes both notes");
    session.undo();
    check (sameNotes (current(), original) && ! session.canUndo() && session.canRedo(), "one undo restores whole batch without extra history");
    auto reject = [&] (const std::vector<Session::MidiNoteInfo>& values, const juce::String& message)
    {
        const auto before = current(); const auto dirty = session.snapshot().dirty;
        const auto undo = session.canUndo(), redo = session.canRedo();
        check (session.updateMidiNotes (clipId, values).failed() && sameNotes (current(), before)
               && session.snapshot().dirty == dirty && session.canUndo() == undo && session.canRedo() == redo, message);
    };
    auto invalid = edited; invalid[1].lengthBeats = 5;
    reject (invalid, "invalid later note leaves earlier note, dirty state and redo untouched");
    invalid = edited; invalid[1].id = "missing";
    reject (invalid, "missing later ID preserves whole batch and history");
    invalid = edited; invalid[1].id = invalid[0].id;
    reject (invalid, "duplicate requested ID preserves whole batch and history");
    invalid = edited; invalid[1].id.clear();
    reject (invalid, "empty requested ID preserves whole batch and history");
    invalid = edited; invalid[1].velocity = std::numeric_limits<float>::quiet_NaN();
    reject (invalid, "nonfinite later value preserves whole batch and history");
    ok (session.updateMidiNotes (clipId, {}), "empty batch preserves pending redo");
    ok (session.updateMidiNotes (clipId, unchanged), "quantized no-op preserves pending redo");
    check (session.updateMidiNotes ("missing", {}).failed(), "empty batch still validates target clip");
    session.redo();
    check (sameNotes (current(), edited) && session.canUndo() && ! session.canRedo(), "redo still reapplies valid batch after rejected and no-op calls");
    session.undo();
    session.beginEditGesture ("Group note drag");
    ok (session.updateMidiNotes (clipId, edited), "begin multi-note gesture");
    auto dragged = edited; dragged[0].startBeat += 0.125; dragged[1].startBeat += 0.125;
    ok (session.updateMidiNotes (clipId, dragged), "continue multi-note gesture");
    session.endEditGesture(); session.undo();
    check (sameNotes (current(), original) && ! session.canUndo(), "one undo restores every batch in the gesture");
    session.redo(); check (sameNotes (current(), dragged), "redo restores complete multi-batch gesture");

    auto xml = juce::parseXML (project);
    check (xml != nullptr, "read saved batch fixture");
    auto state = juce::ValueTree::fromXml (*xml);
    std::vector<juce::ValueTree> noteStates;
    std::function<void (juce::ValueTree)> collect = [&] (juce::ValueTree value)
    {
        if (value.hasType ("NOTE")) noteStates.push_back (value);
        for (auto child : value) collect (child);
    };
    collect (state); check (noteStates.size() == 2, "find both serialized fixture notes");
    noteStates[1].setProperty ("l", 4.0, nullptr);
    const auto overhang = directory.getChildFile ("Overhang.halftrack");
    check (state.createXml()->writeTo (overhang), "write accepted overhanging note fixture");
    ok (session.loadProject (overhang), "load existing overhanging MIDI note");
    auto velocities = current(); velocities[0].velocity = 0.25f; velocities[1].velocity = 0.5f;
    reject (velocities, "overhanging later note cannot partially apply a group velocity edit");
    const auto overhangingNotes = current();
    ok (session.removeMidiNotes (clipId, { overhangingNotes[0].id, overhangingNotes[1].id }), "batch delete can remove existing overhanging notes");
    session.undo(); check (sameNotes (current(), overhangingNotes), "undo overhanging note removal restores exact values");
    noteStates[1].setProperty ("l", 1.0, nullptr);
    noteStates[1].setProperty ("halftrackNoteId", noteStates[0].getProperty ("halftrackNoteId"), nullptr);
    const auto duplicate = directory.getChildFile ("Duplicate-ID.halftrack");
    check (state.createXml()->writeTo (duplicate), "write ambiguous existing note ID fixture");
    ok (session.loadProject (duplicate), "load existing duplicate note IDs");
    auto ambiguous = current()[0]; ambiguous.pitch = 72;
    const auto before = current();
    check (session.updateMidiNotes (clipId, { ambiguous }).failed() && current()[0].pitch == before[0].pitch
           && current()[1].pitch == before[1].pitch && ! session.canUndo() && ! session.snapshot().dirty,
           "one-item batch rejects an ambiguous native ID without editing either note");
    check (session.removeMidiNotes (clipId, { ambiguous.id }).failed()
           && current()[0].pitch == before[0].pitch && current()[1].pitch == before[1].pitch
           && ! session.canUndo() && ! session.snapshot().dirty,
           "one-item removal rejects ambiguous loaded IDs without changing history");
    juce::StringArray validIds;
    ok (session.addMidiNotes (clipId, { { {}, 76, 0, 1, 0.5f } }, validIds), "add unambiguous note beside loaded duplicate IDs");
    const auto beforeMixedRemoval = current();
    const auto dirtyBeforeRemoval = session.snapshot().dirty;
    const auto mixedRemoval = session.removeMidiNotes (clipId, { validIds[0], ambiguous.id });
    const auto afterMixedRemoval = current();
    check (mixedRemoval.failed()
           && afterMixedRemoval.size() == beforeMixedRemoval.size()
           && std::any_of (afterMixedRemoval.begin(), afterMixedRemoval.end(), [&] (const auto& note) { return note.id == validIds[0]; })
           && session.snapshot().dirty == dirtyBeforeRemoval && session.canUndo() && ! session.canRedo(),
           "ambiguous later removal cannot partially delete an earlier valid target");
    session.undo(); check (current().size() == 2, "rejected ambiguous deletion preserves original insertion undo");
}
void insertRemoveBatchTests (const juce::File& directory)
{
    Session session (false);
    ok (session.addMidiTrack ("Clipboard edits"), "create insertion test instrument");
    const auto trackId = instrument (session).id;
    ok (session.addMidiClip (trackId, 0, 4), "create insertion test clip");
    const auto clipId = instrument (session).clips[0].id;
    const auto emptyProject = directory.getChildFile ("Empty-insertion.halftrack");
    ok (session.saveProject (emptyProject), "save empty insertion fixture");
    ok (session.loadProject (emptyProject), "load clean insertion fixture");
    const std::vector<Session::MidiNoteInfo> source {
        { "copied-source-id", 60, 2, 0.5, 0.5f }, { "copied-source-id", 67, 0, 1, 0.001f }
    };
    auto current = [&] { return instrument (session).clips[0].notes; };
    juce::StringArray ids { "prior-output" };
    ok (session.addMidiNotes (clipId, {}, ids), "empty insertion is a successful no-op");
    ok (session.removeMidiNotes (clipId, {}), "empty removal is a successful no-op");
    check (ids.isEmpty() && current().empty() && ! session.snapshot().dirty && ! session.canUndo(),
           "empty batches preserve clean project and return no inserted IDs");
    ok (session.addMidiNotes (clipId, source, ids), "insert complete batch with copied IDs ignored");
    auto expected = source;
    check (ids.size() == 2 && ids[0].isNotEmpty() && ids[1].isNotEmpty() && ids[0] != ids[1]
           && ! ids.contains (source[0].id), "insertion returns distinct fresh IDs in source order");
    expected[0].id = ids[0]; expected[0].velocity = 64 / 127.0f;
    expected[1].id = ids[1]; expected[1].velocity = 1 / 127.0f;
    check (sameNotes (current(), expected), "inserted IDs identify exact pitches, times and native-quantized velocities");
    session.undo();
    check (current().empty() && ! session.canUndo() && session.canRedo(), "one undo removes entire inserted batch");
    auto rejectInsert = [&] (const juce::String& target, const std::vector<Session::MidiNoteInfo>& values, const juce::String& message)
    {
        const auto before = current(); const auto dirty = session.snapshot().dirty;
        const auto undo = session.canUndo(), redo = session.canRedo();
        juce::StringArray output { "preserved-output" };
        check (session.addMidiNotes (target, values, output).failed() && sameNotes (current(), before)
               && output == juce::StringArray { "preserved-output" } && session.snapshot().dirty == dirty
               && session.canUndo() == undo && session.canRedo() == redo, message);
    };
    auto invalid = source; invalid[1].lengthBeats = 5;
    rejectInsert (clipId, invalid, "later out-of-clip insertion rejects whole batch without dirty/history/output changes");
    invalid = source; invalid[1].pitch = 128;
    rejectInsert (clipId, invalid, "later invalid pitch rejects whole insertion");
    invalid = source; invalid[1].velocity = std::numeric_limits<float>::quiet_NaN();
    rejectInsert (clipId, invalid, "later nonfinite velocity rejects whole insertion");
    invalid = source; invalid[1].startBeat = std::numeric_limits<double>::infinity();
    rejectInsert (clipId, invalid, "later nonfinite time rejects whole insertion");
    rejectInsert ("missing", source, "insertion resolves target clip before editing");
    rejectInsert ("missing", {}, "empty insertion still requires a current clip");
    juce::StringArray noIds;
    ok (session.addMidiNotes (clipId, {}, noIds), "empty insertion preserves redo");
    ok (session.removeMidiNotes (clipId, {}), "empty removal preserves redo");
    session.redo();
    check (sameNotes (current(), expected), "redo after rejected and empty calls restores original inserted identities");
    std::vector<Session::MidiNoteInfo> oversized (99999, source[0]);
    rejectInsert (clipId, oversized, "batch plus existing notes enforces existing100000-note limit before insertion");

    auto rejectRemove = [&] (const juce::StringArray& values, const juce::String& message)
    {
        const auto before = current(); const auto dirty = session.snapshot().dirty;
        const auto undo = session.canUndo(), redo = session.canRedo();
        check (session.removeMidiNotes (clipId, values).failed() && sameNotes (current(), before)
               && session.snapshot().dirty == dirty && session.canUndo() == undo && session.canRedo() == redo, message);
    };
    rejectRemove ({ ids[0], "missing" }, "later missing removal ID cannot delete earlier target");
    rejectRemove ({ ids[0], ids[0] }, "duplicate removal ID rejects whole batch");
    rejectRemove ({ ids[0], juce::String() }, "empty later removal ID rejects whole batch");
    const auto populated = directory.getChildFile ("Inserted.halftrack");
    ok (session.saveProject (populated), "save inserted note IDs");
    ok (session.newProject(), "replace insertion session");
    juce::StringArray staleOutput { "untouched" };
    check (session.addMidiNotes (clipId, source, staleOutput).failed()
           && staleOutput == juce::StringArray { "untouched" } && ! session.canUndo() && ! session.snapshot().dirty,
           "old clip ID cannot insert into a replacement project");
    ok (session.loadProject (populated), "reload populated insertion fixture");
    check (sameNotes (current(), expected), "inserted stable IDs and note values persist after reopen");
    juce::StringArray copies;
    ok (session.addMidiNotes (clipId, expected, copies), "insert copied values into current reopened clip");
    check (copies.size() == 2 && copies[0] != copies[1] && ! ids.contains (copies[0]) && ! ids.contains (copies[1]),
           "reopened clip gets fresh IDs distinct from source and existing notes");
    session.undo(); check (sameNotes (current(), expected) && ! session.canUndo(), "one undo removes reopened batch only");
    rejectRemove ({ ids[0], "missing" }, "invalid removal preserves pending insertion redo");
    session.redo(); check (current().size() == 4, "actual insertion redo survives failed removal"); session.undo();
    ok (session.removeMidiNotes (clipId, { ids[1], ids[0] }), "remove multiple current notes in requested order");
    check (current().empty(), "valid removal removes whole selected group");
    session.undo(); check (sameNotes (current(), expected) && ! session.canUndo(), "one undo restores removed group identities and values");
    session.redo(); check (current().empty(), "redo removes whole group"); session.undo();

    session.beginEditGesture ("Clipboard edit gesture");
    ok (session.addMidiNotes (clipId, source, copies), "insert within existing gesture");
    ok (session.removeMidiNotes (clipId, ids), "remove within existing gesture");
    session.endEditGesture();
    const auto gestureResult = current();
    session.undo(); check (sameNotes (current(), expected) && ! session.canUndo(), "one undo groups insertion and removal in existing gesture");
    session.redo(); check (sameNotes (current(), gestureResult), "redo restores combined insert/remove gesture");
}
void controllerReleaseTests()
{
    SubtractiveSynthDSP actual, reference;
    auto prepare = [&]
    {
        actual.prepare (48000); reference.prepare (48000);
        actual.setSettings (flatSettings()); reference.setSettings (flatSettings());
    };
    auto note = [&] (int channel, int pitch)
    {
        actual.noteOn (channel, pitch, 0.75f); reference.noteOn (channel, pitch, 0.75f);
    };
    auto compare = [&] (int samples, const juce::String& label)
    {
        std::array<float, 1024> left {}, right {}, expectedLeft {}, expectedRight {};
        actual.render (left.data(), right.data(), samples);
        reference.render (expectedLeft.data(), expectedRight.data(), samples);
        check (left == expectedLeft && right == expectedRight
               && std::all_of (left.begin(), left.end(), [] (float v) { return std::isfinite (v); }), label);
    };
    prepare(); note (1, 60); note (1, 64); note (16, 67);
    compare (79, "controller fixture starts with identical nonzero synth state");
    actual.controlChange (1, 123, 0); reference.noteOff (1, 60); reference.noteOff (1, 64);
    compare (137, "All Notes Off retains the same release audio as individual note-offs and isolates channels");
    actual.controlChange (1, 123, 0);
    compare (1024, "repeated All Notes Off does not restart an existing release");
    check (actual.activeVoices() == 1, "All Notes Off leaves another channel sounding");
    actual.controlChange (16, 123, 0); reference.noteOff (16, 67);
    compare (1024, "All Notes Off supports the final MIDI channel");
    check (actual.activeVoices() == 0, "All Notes Off eventually reaches exact silence");

    prepare(); note (1, 60);
    actual.controlChange (1, 64, 127); reference.controlChange (1, 64, 127);
    compare (79, "sustain fixture starts with matching held note");
    actual.controlChange (1, 123, 0); reference.noteOff (1, 60);
    compare (1024, "All Notes Off honors the sustain pedal like an ordinary note-off");
    check (actual.activeVoices() == 1, "sustained All Notes Off leaves its voice active");
    actual.controlChange (1, 64, 0); reference.controlChange (1, 64, 0);
    compare (1024, "pedal up releases notes retained after All Notes Off");
    check (actual.activeVoices() == 0, "pedal up completes sustained All Notes Off");

    prepare(); note (1, 60); note (1, 64); note (16, 67);
    for (auto* synth : { &actual, &reference })
    {
        synth->controlChange (1, 64, 127); synth->controlChange (16, 64, 127);
    }
    compare (79, "controller reset fixture starts with matching audio");
    for (auto* synth : { &actual, &reference }) { synth->noteOff (1, 60); synth->noteOff (16, 67); }
    actual.controlChange (1, 121, 0); reference.controlChange (1, 64, 0);
    compare (137, "Reset All Controllers releases only its channel's pedal-held notes");
    actual.controlChange (1, 121, 0);
    compare (1024, "repeated controller reset preserves release timing and held keys");
    check (actual.activeVoices() == 2, "controller reset preserves a held key and another channel's sustain");
    actual.noteOff (1, 64); reference.noteOff (1, 64);
    compare (1024, "later note-offs see the reset sustain state");
    check (actual.activeVoices() == 1, "controller reset leaves the other channel sustained");

    prepare(); note (1, 69); compare (79, "repeated pedal-up fixture starts with matching audio");
    actual.noteOff (1, 69); reference.noteOff (1, 69);
    for (int step = 0; step < 20; ++step)
    {
        actual.controlChange (1, 64, 0);
        compare (31, "repeated pedal-up preserves the original release sample sequence");
    }
    check (actual.activeVoices() == 0, "repeated pedal-up cannot prolong the release");

    prepare(); note (1, 60); note (16, 67); compare (79, "All Sound Off fixture starts with matching audio");
    actual.controlChange (1, 64, 127); actual.noteOff (1, 60);
    actual.controlChange (1, 120, 0);
    reference.reset(); reference.setSettings (flatSettings()); reference.noteOn (16, 67, 0.75f);
    std::array<float, 79> warmup {}; reference.render (warmup.data(), nullptr, 79);
    compare (1024, "All Sound Off immediately removes its channel even with sustain down");
    check (actual.activeVoices() == 1, "All Sound Off preserves other channels");
    actual.controlChange (16, 120, 0); reference.reset();
    compare (1024, "All Sound Off has exact silence without a release tail");
    check (actual.activeVoices() == 0, "All Sound Off kills the final channel immediately");
}

void pitchWheelStateTests()
{
    SubtractiveSynthDSP actual, reference;
    auto prepare = [&]
    {
        actual.prepare (48000); reference.prepare (48000);
        actual.setSettings (flatSettings()); reference.setSettings (flatSettings());
    };
    auto compare = [&] (int samples, const juce::String& label)
    {
        std::array<float, 1024> left {}, right {}, expected {};
        actual.render (left.data(), right.data(), samples);
        reference.render (expected.data(), nullptr, samples);
        bool matches = left == right;
        for (int i = 0; i < samples; ++i)
            matches = matches && std::isfinite (left[static_cast<size_t> (i)])
                      && std::abs (left[static_cast<size_t> (i)] - expected[static_cast<size_t> (i)]) < 1.0e-6f;
        check (matches, label);
    };
    for (const int channel : { 1, 16 })
        for (const auto [wheel, pitch] : { std::pair { 0, 67 }, { 4096, 68 }, { 8192, 69 }, { 16383, 71 } })
        {
            prepare();
            actual.pitchWheel (channel, wheel);
            actual.noteOn (channel, 69, 0.75f); reference.noteOn (channel, pitch, 0.75f);
            const int other = channel == 1 ? 16 : 1;
            actual.noteOn (other, 60, 0.5f); reference.noteOn (other, 60, 0.5f);
            compare (1024, "per-channel wheel tunes future notes while leaving the other channel unchanged");
            actual.noteOff (channel, 69); reference.noteOff (channel, pitch);
            compare (1024, "bent note retains its original note-off identity and ordinary release");
            check (actual.activeVoices() == 1, "bent note-off leaves only the other channel active");
        }

    // Round trips with no intervening samples must preserve every voice's
    // phase, filter and envelope, including held, pedal-held and releasing notes.
    prepare();
    for (auto* synth : { &actual, &reference })
    {
        synth->noteOn (1, 60, 0.5f); synth->noteOn (1, 64, 0.5f); synth->noteOn (16, 67, 0.5f);
        synth->controlChange (1, 64, 127);
    }
    compare (79, "bend continuity fixture warms matching nonzero voice state");
    for (auto* synth : { &actual, &reference })
    {
        synth->noteOff (1, 60); synth->noteOff (16, 67);
    }
    actual.pitchWheel (1, 0); actual.pitchWheel (16, 16383);
    actual.pitchWheel (1, 8192); actual.pitchWheel (16, 8192);
    compare (113, "wheel round trips preserve held, sustained and releasing voice histories");
    for (const auto [channel, value] : { std::pair { 0, 0 }, { 17, 16383 }, { 1, -1 }, { 16, 16384 } })
        actual.pitchWheel (channel, value);
    compare (113, "invalid wheel channels and values leave sounding voices unchanged");

    // Compare reset behavior to the explicit equivalent controller operations.
    for (auto* synth : { &actual, &reference })
    {
        synth->pitchWheel (1, 16383); synth->pitchWheel (16, 0);
        synth->noteOn (16, 69, 0.5f);
    }
    compare (79, "controller reset fixture has distinct wheel states on both endpoint channels");
    actual.controlChange (1, 121, 0);
    reference.pitchWheel (1, 8192); reference.controlChange (1, 64, 0);
    compare (137, "Reset All Controllers centers only its channel and releases its pedal-held notes");
    actual.controlChange (1, 121, 0);
    compare (1024, "repeated reset preserves ongoing envelope timing and the other channel's bend");
    actual.noteOn (1, 72, 0.5f); reference.noteOn (1, 72, 0.5f);
    compare (79, "subsequent notes use the centered wheel after controller reset");

    for (const int controller : { 120, 123 })
    {
        prepare(); actual.pitchWheel (1, 16383);
        actual.noteOn (1, 60, 1); actual.controlChange (1, controller, 0);
        // This note has never sounded, so All Notes Off has a zero envelope.
        std::array<float, 1> discard {}; actual.render (discard.data(), nullptr, 1);
        actual.noteOn (1, 69, 1); reference.noteOn (1, 71, 1);
        compare (1024, "sound-off and notes-off preserve the channel's wheel for subsequent notes");
    }
    actual.controlChange (1, 64, 127); actual.reset(); reference.reset();
    actual.noteOn (1, 69, 1); reference.noteOn (1, 69, 1);
    compare (79, "full host reset clears voices and centers pitch wheel");
    actual.noteOff (1, 69); reference.noteOff (1, 69);
    compare (1024, "full host reset clears sustain as well as pitch wheel");
    check (actual.activeVoices() == 0, "reset instrument releases to silence");

    for (const int state : { 0, 1, 2 })
    {
        SubtractiveSynthDSP bent;
        bent.prepare (48000);
        auto settings = flatSettings(); settings.releaseSeconds = 2;
        bent.setSettings (settings); bent.noteOn (16, 69, 1);
        std::array<float, 48000> audio {};
        bent.render (audio.data(), nullptr, 97);
        if (state == 1) bent.controlChange (16, 64, 127);
        if (state != 0) bent.noteOff (16, 69);
        bent.pitchWheel (16, 16383); audio.fill (0);
        bent.render (audio.data(), nullptr, static_cast<int> (audio.size()));
        int crossings = 0;
        for (size_t i = 24001; i < audio.size(); ++i)
            if (audio[i - 1] <= 0 && audio[i] > 0) ++crossings;
        check (crossings >= 246 && crossings <= 248,
               "wheel retunes an already sounding held/sustained/releasing A4 voice to B4");
    }
    actual.prepare (8000); actual.setSettings (flatSettings()); actual.pitchWheel (16, 16383);
    actual.noteOn (16, 127, 1);
    std::array<float, 1024> high {};
    actual.render (high.data(), nullptr, static_cast<int> (high.size()));
    check (std::all_of (high.begin(), high.end(), [] (float v) { return std::isfinite (v) && std::abs (v) <= 2; }),
           "upper pitch bend at the lowest supported sample rate remains finite and bounded");
}

void pitchWheelCallbackTests()
{
    Session session (false);
    ok (session.addMidiTrack ("Pitch-wheel fixture"), "create pitch-wheel callback instrument without audio devices");
    SubtractiveSynthPlugin* plugin = nullptr;
    for (auto* engine : tracktion::Engine::getEngines())
        if (&engine->getDeviceManager().deviceManager == &session.audioDeviceManager())
            for (auto* edit : engine->getActiveEdits().getEdits())
                for (auto* track : tracktion::getAudioTracks (*edit))
                    for (auto* candidate : track->pluginList.getPlugins())
                        if (auto* synth = dynamic_cast<SubtractiveSynthPlugin*> (candidate)) plugin = synth;
    check (plugin != nullptr, "find the real built-in synth for callback renders");
    plugin->setSettings (flatSettings());
    plugin->prepareForOfflineRender();
    plugin->baseClassInitialise ({ {}, 48000, 512 });
    struct Deinitialise
    {
        SubtractiveSynthPlugin& synth;
        ~Deinitialise() { synth.baseClassDeinitialise(); }
    } deinitialise { *plugin };

    for (const auto [wheel, pitch] : { std::pair { 8192, 69 }, { 16383, 71 }, { 0, 67 } })
    {
        plugin->reset();
        juce::AudioBuffer<float> output (2, 528);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 528; ++sample) output.setSample (channel, sample, 0.25f);
        tracktion::MidiMessageArray midi;
        midi.addMidiMessage (juce::MidiMessage::pitchWheel (16, wheel), 0.0, {});
        midi.addMidiMessage (juce::MidiMessage::noteOn (16, 69, 1.0f), 0.0, {});
        plugin->applyToBuffer ({ &output, 8, 512, &midi, 0.0,
                                { {}, tracktion::TimePosition::fromSeconds (512 / 48000.0) },
                                true, false, true, false });
        SubtractiveSynthDSP reference;
        reference.prepare (48000); reference.setSettings (flatSettings()); reference.noteOn (16, pitch, 1);
        std::array<float, 512> expected {};
        reference.render (expected.data(), nullptr, 512);
        bool matches = true, boundsPreserved = true;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 528; ++sample)
            {
                const float actual = output.getSample (channel, sample);
                if (sample >= 8 && sample < 520)
                    matches = matches && std::isfinite (actual)
                              && std::abs (actual - expected[static_cast<size_t> (sample - 8)]) < 1.0e-6f;
                else boundsPreserved = boundsPreserved && actual == 0.25f;
            }
        check (matches, "real MIDI callback wheel " + juce::String (wheel) + " matches the full unbent reference-note buffer");
        check (boundsPreserved, "synth callback preserves samples outside its destination range");
    }
    struct Event { int sample; juce::MidiMessage message; };
    const std::array<Event, 10> events {{
        { 0, juce::MidiMessage::pitchWheel (1, 16383) },
        { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) },
        { 117, juce::MidiMessage::pitchWheel (1, 0) },
        { 211, juce::MidiMessage::noteOn (16, 64, 1.0f) },
        { 255, juce::MidiMessage::pitchWheel (16, 16383) },
        { 511, juce::MidiMessage::controllerEvent (1, 121, 0) },
        { 640, juce::MidiMessage::noteOff (1, 69) },
        { 1024, juce::MidiMessage::pitchWheel (16, 0) },
        { 1300, juce::MidiMessage::noteOff (16, 64) },
        { 1500, juce::MidiMessage::pitchWheel (16, 8192) }
    }};
    std::array<float, 2048> expected {};
    SubtractiveSynthDSP reference;
    reference.prepare (48000); reference.setSettings (flatSettings());
    int position = 0;
    for (const auto& event : events)
    {
        reference.render (expected.data() + position, nullptr, event.sample - position);
        position = event.sample;
        const auto& message = event.message;
        if (message.isNoteOn()) reference.noteOn (message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isNoteOff()) reference.noteOff (message.getChannel(), message.getNoteNumber());
        else if (message.isPitchWheel()) reference.pitchWheel (message.getChannel(), message.getPitchWheelValue());
        else if (message.isController()) reference.controlChange (message.getChannel(), message.getControllerNumber(), message.getControllerValue());
    }
    reference.render (expected.data() + position, nullptr, static_cast<int> (expected.size()) - position);
    for (const int blockSize : { 1, 31, 128, 512, 2048 })
    {
        plugin->reset();
        size_t nextEvent = 0;
        bool matches = true, boundsPreserved = true;
        for (int start = 0; start < 2048; start += blockSize)
        {
            const int count = std::min (blockSize, 2048 - start);
            juce::AudioBuffer<float> output (2, count + 16);
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < count + 16; ++i) output.setSample (channel, i, 0.25f);
            tracktion::MidiMessageArray midi;
            constexpr double offset = 8 / 48000.0;
            // Last-quarter-sample events round onto a boundary, including the
            // end of a block. They must affect the next sample, not be lost.
            while (nextEvent < events.size())
            {
                const auto& event = events[nextEvent];
                const double arrival = event.sample == 0 ? 0.0 : event.sample - 0.25;
                if (arrival >= start + count) break;
                midi.addMidiMessage (event.message, (arrival - start) / 48000.0 - offset, {});
                ++nextEvent;
            }
            plugin->applyToBuffer ({ &output, 8, count, &midi, offset,
                                    { tracktion::TimePosition::fromSeconds (start / 48000.0),
                                      tracktion::TimePosition::fromSeconds ((start + count) / 48000.0) },
                                    true, false, true, false });
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < count + 16; ++i)
                {
                    const float actual = output.getSample (channel, i);
                    if (i >= 8 && i < count + 8)
                        matches = matches && std::isfinite (actual)
                                  && std::abs (actual - expected[static_cast<size_t> (start + i - 8)]) < 1.0e-6f;
                    else boundsPreserved = boundsPreserved && actual == 0.25f;
                }
        }
        check (matches && nextEvent == events.size(),
               "timestamped bend/note/reset sequence matches complete stereo PCM at block size " + juce::String (blockSize));
        check (boundsPreserved, "timestamped callback preserves destination range boundaries");
    }

}

void coreTests()
{
    // Independent reference: bilinear-transform second-order analog low-pass,
    // H(s)=1/(s^2+2s+1), prewarped4800Hz at48000Hz. A phase-zero square is
    // zero at sample0 and +1 over these following15samples. Reference evaluated
    // as a direct-form biquad step response, not the synth's state-variable form.
    constexpr std::array<float, 16> reference {
        0.0000000000f, 0.0601413212f, 0.2417110309f, 0.4712674678f,
        0.6580587260f, 0.7888119595f, 0.8735621261f, 0.9259812040f,
        0.9573963977f, 0.9758012381f, 0.9864008146f, 0.9924241281f,
        0.9958103695f, 0.9976973723f, 0.9987412010f, 0.9993150192f
    };
    SubtractiveSynthDSP synth;
    synth.prepare (48000); synth.setSettings (flatSettings()); synth.noteOn (1, 69, 1);
    std::array<float, 16> left {}, right {};
    synth.render (left.data(), right.data(), 16);
    float referenceError = 0;
    for (size_t i = 0; i < left.size(); ++i)
        referenceError = std::max (referenceError, std::abs (left[i] - reference[i]));
    check (referenceError < 0.000001f && left == right, "nonzero offline samples match independent filter step reference");

    auto settings = flatSettings(); settings.level = 0.2f; settings.attackSeconds = 0.005f;
    SubtractiveSynthDSP whole, split;
    whole.prepare (48000); split.prepare (48000); whole.setSettings (settings); split.setSettings (settings);
    whole.noteOn (1, 69, 0.8f); split.noteOn (1, 69, 0.8f);
    std::array<float, 48000> a {}, b {};
    whole.render (a.data(), nullptr, static_cast<int> (a.size()));
    for (int offset = 0; offset < 48000; offset += 127)
        split.render (b.data() + offset, nullptr, std::min (127, 48000 - offset));
    check (a == b, "synth output is invariant to block partitioning");
    int crossings = 0;
    for (int i = 24001; i < 48000; ++i) if (a[static_cast<size_t> (i - 1)] <= 0 && a[static_cast<size_t> (i)] > 0) ++crossings;
    check (crossings >= 219 && crossings <= 221, "MIDI note69 produces a440Hz fundamental");

    synth.reset(); synth.setSettings (flatSettings());
    for (int pitch = 40; pitch < 57; ++pitch) synth.noteOn (1, pitch, 1);
    check (synth.activeVoices() == 16, "seventeenth note steals within fixed sixteen-voice storage");
    synth.controlChange (1, 120, 0);
    check (synth.activeVoices() == 0, "all-sound-off clears voices");
    synth.noteOn (1, 60, 1); synth.render (left.data(), nullptr, 16);
    synth.controlChange (1, 64, 127); synth.noteOff (1, 60);
    std::array<float, 1024> tail {};
    synth.render (tail.data(), nullptr, 1024);
    check (synth.activeVoices() == 1, "sustain pedal retains released key");
    synth.controlChange (1, 64, 0); tail.fill (0); synth.render (tail.data(), nullptr, 1024);
    check (synth.activeVoices() == 0 && std::all_of (tail.begin() + 512, tail.end(), [] (float value) { return value == 0; }),
           "pedal release finishes envelope and produces exact silence");

    SynthParameterQueue queue;
    for (int i = 0; i < 63; ++i) { settings.cutoffHz = float (100 + i); check (queue.push (settings), "enqueue parameter packet" + juce::String (i)); }
    check (! queue.push (settings), "full parameter queue has bounded backpressure");
    for (int i = 0; i < 63; ++i)
    {
        Session::SynthSettings packet;
        check (queue.pop (packet) && packet.cutoffHz == float (100 + i), "FIFO preserves complete parameter packet" + juce::String (i));
    }
    check (! queue.pop (settings), "empty parameter queue returns without waiting");
}
}

int runMidiTests (const juce::File& requestedDirectory)
{
    try
    {
        pitchWheelCallbackTests();
        pitchWheelStateTests();
        controllerReleaseTests();
        coreTests();
        auto directory = requestedDirectory.getChildFile ("midi-" + juce::Uuid().toString());
        ok (directory.createDirectory(), "create isolated MIDI artifacts");
        batchEditTests (directory);
        insertRemoveBatchTests (directory);
        Session session (false);
        ok (session.addMidiTrack ("Synth notes"), "add real MIDI instrument track");
        auto trackId = instrument (session).id;
        session.undo(); check (session.snapshot().tracks.size() == 2, "undo MIDI track creation");
        session.redo(); check (instrument (session).id == trackId, "redo MIDI track preserves identity");
        ok (session.addMidiClip (trackId, 0, 4), "create four-beat clip");
        auto clipId = instrument (session).clips[0].id;
        session.undo(); check (instrument (session).clips.empty(), "undo MIDI clip creation");
        session.redo(); check (instrument (session).clips[0].id == clipId, "redo MIDI clip preserves identity");
        ok (session.addMidiNote (clipId, 69, 1, 0.5, 1), "add MIDI note");
        const auto noteId = instrument (session).clips[0].notes[0].id;
        check (noteId.isNotEmpty(), "note has persistent identifier");
        session.undo(); check (instrument (session).clips[0].notes.empty(), "undo note addition");
        session.redo(); check (instrument (session).clips[0].notes[0].id == noteId, "redo note addition preserves identifier");
        session.beginEditGesture ("Move MIDI note");
        ok (session.updateMidiNote (clipId, noteId, 70, 1.25, 0.75, 0.8f), "move and resize MIDI note");
        ok (session.updateMidiNote (clipId, noteId, 72, 1.5, 1, 0.6f), "continue MIDI note gesture");
        session.endEditGesture(); session.undo();
        auto note = instrument (session).clips[0].notes[0];
        check (note.id == noteId && note.pitch == 69 && note.startBeat == 1 && note.lengthBeats == 0.5 && note.velocity == 1,
               "one undo restores note pitch/time/length/velocity gesture");
        session.redo(); note = instrument (session).clips[0].notes[0];
        check (note.pitch == 72 && note.startBeat == 1.5 && note.lengthBeats == 1, "redo MIDI gesture");
        session.undo();
        ok (session.removeMidiNote (clipId, noteId), "delete MIDI note");
        session.undo(); check (instrument (session).clips[0].notes[0].id == noteId, "undo note deletion preserves identifier");
        session.redo(); check (instrument (session).clips[0].notes.empty(), "redo note deletion"); session.undo();
        check (session.updateMidiNote (clipId, "missing", 60, 0, 1, 1).failed()
               && session.addMidiNote (clipId, 200, 0, 1, 1).failed()
               && session.addMidiNote (clipId, 60, 3.5, 1, 1).failed(), "invalid note edits return errors without mutation");
        auto settings = flatSettings(); settings.level = 0.2f; settings.releaseSeconds = 0.1f;
        ok (session.setSynthSettings (trackId, settings), "set actual synth parameters");
        session.undo(); check (instrument (session).synth.waveform == 0, "undo synth parameter state");
        session.redo(); check (instrument (session).synth.waveform == 1 && instrument (session).synth.level == settings.level, "redo synth parameter state");
        session.setTempo (60);
        check (std::abs (instrument (session).clips[0].length - 4) < 0.0001
               && instrument (session).clips[0].notes[0].startBeat == 1, "MIDI clip follows tempo while notes retain musical time");
        session.undo(); check (std::abs (session.snapshot().tempo - 120) < 0.001, "undo tempo edit");
        session.redo(); check (std::abs (session.snapshot().tempo - 60) < 0.001, "redo tempo edit");
        session.setTempo (120);
        // With no physical processing callback, these exceed the FIFO capacity.
        // The final state must be used from the first offline block, not after a
        // message-thread retry happens to run during the export.
        auto queued = settings;
        session.beginEditGesture ("Many synth edits before playback");
        for (int edit = 0; edit < 128; ++edit)
        {
            queued.level = float (edit % 2) * 0.01f;
            queued.waveform = 0;
            auto result = session.setSynthSettings (trackId, queued);
            if (result.failed()) throw std::runtime_error (result.getErrorMessage().toStdString());
        }
        ok (session.setSynthSettings (trackId, settings), "final settings after more edits than the parameter FIFO can hold");
        session.endEditGesture();
        auto output = directory.getChildFile ("midi.wav");
        ok (session.exportWav (output), "offline render actual MIDI clip through built-in instrument");
        auto reader = readerFor (output);
        check (reader && reader->sampleRate == 48000 && reader->lengthInSamples == 96000, "musical four-beat clip exports two seconds at120BPM");
        juce::AudioBuffer<float> audio (2, 96000); reader->read (&audio, 0, 96000, 0, true, true);
        int onset = -1;
        for (int i = 0; i < audio.getNumSamples(); ++i) if (std::abs (audio.getSample (0, i)) > 0.000001f) { onset = i; break; }
        check (onset >= 24000 && onset <= 24002, "MIDI onset is sample-timed at half a second: " + juce::String (onset));
        check (audio.getMagnitude (0, 25000, 10000) > 0.1f && audio.getMagnitude (0, 42000, 50000) < 0.000001f,
               "note sounds at velocity and releases after its programmed duration");
        auto project = directory.getChildFile ("Notes.halftrack");
        ok (session.saveProject (project), "save note and synth state"); ok (session.newProject(), "clear before note reload");
        ok (session.loadProject (project), "reload note and synth state");
        auto restored = instrument (session);
        check (restored.id == trackId && restored.clips[0].id == clipId && restored.clips[0].notes[0].id == noteId
               && restored.synth.waveform == 1 && restored.synth.level == settings.level, "MIDI IDs and built-in parameters survive save/reopen");
        auto secondOutput = directory.getChildFile ("midi-restored.wav");
        ok (session.exportWav (secondOutput), "render restored MIDI project");
        auto secondReader = readerFor (secondOutput); juce::AudioBuffer<float> restoredAudio (2, 96000);
        check (secondReader && secondReader->read (&restoredAudio, 0, 96000, 0, true, true), "read restored MIDI render");
        float difference = 0;
        for (int i = 0; i < 96000; ++i) difference = std::max (difference, std::abs (audio.getSample (0, i) - restoredAudio.getSample (0, i)));
        check (difference < 0.00001f, "reopened MIDI render matches original output");
        // Export deliberately pumps native events while a worker owns the graph.
        // Recreate native-menu reentrancy without opening hardware or a window.
        ok (session.setTrackGain (2, -3), "create undoable edit before export reentrancy test");
        const auto beforeExport = session.snapshot();
        check (session.canUndo() && ! session.isExporting(), "editing is available before export");
        const auto protectedOutput = directory.getChildFile ("protected-export.wav");
        const auto nestedOutput = directory.getChildFile ("nested-export.wav");
        const auto rejectedSave = directory.getChildFile ("reentrant-save.halftrack");
        const juce::String originalDestination = "Original destination survives cancelled rendering";
        check (protectedOutput.replaceWithText (originalDestination), "prepare protected export destination");
        bool eventDispatched = false, busyDuringDispatch = false, mutationsRejected = false;
        bool undoIgnored = false, navigationIgnored = false, identitiesPreserved = false;
        juce::MessageManager::callAsync ([&]
        {
            eventDispatched = true;
            busyDuringDispatch = session.isExporting() && ! session.canUndo() && ! session.canRedo();
            juce::StringArray inserted { "export-preserved" };
            const std::array<juce::Result, 13> attempts {
                session.newProject(), session.loadProject (project), session.saveProject (rejectedSave),
                session.removeTrack (2), session.addMidiNote (clipId, 60, 0, 1, 1),
                session.setSynthSettings (trackId, flatSettings()), session.setTrackGain (2, -20),
                session.addAudioTrack ("Reentrant track"), session.removeClip (2, 0),
                session.exportWav (nestedOutput), session.updateMidiNotes (clipId, restored.clips[0].notes),
                session.addMidiNotes (clipId, restored.clips[0].notes, inserted), session.removeMidiNotes (clipId, { noteId })
            };
            mutationsRejected = inserted == juce::StringArray { "export-preserved" } && std::all_of (attempts.begin(), attempts.end(), [] (const auto& result)
            {
                return result.failed() && result.getErrorMessage() == "Finish or cancel the current export first.";
            });
            auto state = session.snapshot();
            identitiesPreserved = state.projectFile == beforeExport.projectFile
                && state.tracks.size() == beforeExport.tracks.size()
                && state.tracks[2].id == trackId && state.tracks[2].clips.size() == 1
                && state.tracks[2].clips[0].id == clipId && state.tracks[2].clips[0].notes.size() == 1
                && state.tracks[2].clips[0].notes[0].id == noteId
                && state.tracks[2].synth.waveform == settings.waveform
                && state.tracks[2].synth.level == settings.level;
            session.undo();
            undoIgnored = session.snapshot().tracks[2].gainDb == beforeExport.tracks[2].gainDb;
            session.redo();
            undoIgnored = undoIgnored && session.snapshot().tracks[2].gainDb == beforeExport.tracks[2].gainDb;
            const auto position = session.snapshot().position;
            session.stop(); session.seek (100); session.setTempo (81); session.setLoop (true, 5, 10);
            session.beginEditGesture ("Blocked gesture"); session.endEditGesture();
            state = session.snapshot();
            navigationIgnored = state.position == position && state.tempo == beforeExport.tempo
                && state.looping == beforeExport.looping && state.loopStart == beforeExport.loopStart
                && state.loopEnd == beforeExport.loopEnd;
            session.cancelExport();
        });
        const auto cancelled = session.exportWav (protectedOutput, 48000, 24, 60);
        check (eventDispatched && busyDuringDispatch && mutationsRejected,
               "nested native events reject project replacement, edits and recursive export");
        check (identitiesPreserved && undoIgnored && navigationIgnored,
               "rendering preserves model identity, parameters, undo history and navigation");
        check (cancelled.failed() && cancelled.getErrorMessage() == "Export cancelled."
               && protectedOutput.loadFileAsString() == originalDestination
               && ! nestedOutput.exists() && ! rejectedSave.exists(),
               "reentrant cancellation preserves target and creates no nested output");
        check (! session.isExporting() && session.canUndo(), "export guard releases after worker cancellation");
        session.undo();
        check (std::abs (session.snapshot().tracks[2].gainDb) < 0.001f,
               "original edit can still be undone after cancelled export");
        ok (session.setTrackGain (2, -1), "editing resumes after cancelled export");
        std::cout << "Halftrack MIDI tests passed. Artifacts: " << directory.getFullPathName() << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL MIDI " << error.what() << std::endl;
        return 1;
    }
}
}
