#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <functional>
#include <iostream>
#include <stdexcept>

// Moving and copying Session clips: between slots of a track and across tracks of their kind,
// replacing what is there, each one undo step. A playing clip moved within its track plays on, and
// the render is the same as without the move; moved to another track it stops. Duplicate fills the
// slot below or inserts a scene for the copy. The Session clipboard copies, cuts and pastes slot
// clips, into another project too, with the audio collected there.
namespace halftrack
{
namespace te = tracktion::engine;
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS slot clips: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
te::Engine& engineOf (Session& s) { return dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (s.audioDeviceManager()).engine; }
te::HostedAudioDeviceInterface::Parameters parameters()
{
    te::HostedAudioDeviceInterface::Parameters p; p.sampleRate = 48000; p.blockSize = 128; p.inputChannels = 0; p.outputChannels = 2; p.useMidiDevices = false;
    return p;
}
void dispatch (Session& s) { for (auto* edit : engineOf (s).getActiveEdits().getEdits()) edit->dispatchPendingUpdatesSynchronously(); }
juce::File tone (const juce::File& root)
{
    const auto file = root.getChildFile ("Tone.wav");
    juce::AudioBuffer<float> wave (2, 24000);
    for (int c = 0; c < 2; ++c) for (int n = 0; n < wave.getNumSamples(); ++n) wave.setSample (c, n, .2f * (float) std::sin (juce::MathConstants<double>::twoPi * 440 * n / 48000));
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    check (writer && writer->writeFromAudioSampleBuffer (wave, 0, wave.getNumSamples()), "write a tone");
    return file;
}
// A MIDI track's clip holding one whole-clip note, played for a time; `edit` runs part way through.
juce::AudioBuffer<float> perform (const std::function<void (Session&, int track)>& edit)
{
    Session s (false);
    te::test_utilities::EnginePlayer player (engineOf (s), parameters());
    ok (s.setLaunchQuantisation (Session::LaunchQuantisation::none), "launch at once");
    while (s.snapshot().scenes.size() < 4) ok (s.addScene(), "a scene");
    ok (s.addMidiTrack ("Keys"), "a MIDI track");
    const int track = (int) s.snapshot().tracks.size() - 1;
    juce::StringArray added;
    ok (s.addMidiClipToSlot (s.snapshot().tracks[(size_t) track].id, 0, 4), "a clip");
    ok (s.addMidiNotes (s.snapshot().tracks[(size_t) track].slots[0].clip.id, { { "", 57, 0, 4, .8f } }, added), "holding a note");
    ok (s.launchSlot (track, 0), "play it");
    dispatch (s); player.process (12000); dispatch (s);
    if (edit) { edit (s, track); dispatch (s); }
    auto heard = player.process (19200);
    s.stop(); dispatch (s);
    return heard;
}
}

// Several slots at once: a group moves without landing on itself, copies, refuses to leave the grid
// or change kind, clears, and copies and pastes keeping its shape; each is one undo step.
void groupTests()
{
    Session s (false);
    while (s.snapshot().scenes.size() < 6) ok (s.addScene(), "a scene");
    ok (s.addMidiTrack ("One"), "a MIDI track"); ok (s.addMidiTrack ("Two"), "another"); ok (s.addAudioTrack ("Three"), "an audio track");
    const auto index = [&] (const juce::String& name) { const auto tracks = s.snapshot().tracks; for (int t = 0; t < (int) tracks.size(); ++t) if (tracks[(size_t) t].name == name) return t; return -1; };
    const int one = index ("One"), two = index ("Two"), three = index ("Three");
    juce::StringArray added;
    const auto pitch = [&] (int track, int scene) { const auto slot = s.snapshot().tracks[(size_t) track].slots[(size_t) scene]; return slot.hasClip && ! slot.clip.notes.empty() ? slot.clip.notes[0].pitch : -1; };
    for (const auto& [track, scene, note] : { std::tuple { one, 0, 60 }, std::tuple { one, 1, 61 }, std::tuple { two, 0, 62 } })
    {
        ok (s.addMidiClipToSlot (s.snapshot().tracks[(size_t) track].id, scene, 4), "a clip");
        ok (s.addMidiNotes (s.snapshot().tracks[(size_t) track].slots[(size_t) scene].clip.id, { { "", note, 0, 1, .8f } }, added), "its note");
    }
    const std::vector<Session::SlotRef> group { { one, 0 }, { one, 1 }, { two, 0 } };
    ok (s.moveSlotClips (group, 0, 1), "move the three clips down a scene");
    check (pitch (one, 0) == -1 && pitch (one, 1) == 60 && pitch (one, 2) == 61 && pitch (two, 0) == -1 && pitch (two, 1) == 62,
           "they move together, none landing on another before it moved");
    s.undo();
    check (pitch (one, 0) == 60 && pitch (one, 1) == 61 && pitch (two, 0) == 62 && pitch (one, 2) == -1, "one undo moves them all back");
    ok (s.moveSlotClips (group, 0, 3, true), "copy them three scenes down");
    check (pitch (one, 0) == 60 && pitch (one, 3) == 60 && pitch (one, 4) == 61 && pitch (two, 3) == 62, "the copies keep their shape and the originals stay");
    s.undo();
    check (s.moveSlotClips (group, 0, -1).failed() && s.moveSlotClips (group, 1, 0).failed() && pitch (one, 0) == 60,
           "a move off the grid, or putting MIDI on the audio track, is refused and changes nothing");
    ok (s.copySlotClips (group), "copy the group");
    ok (s.pasteSlotClip (one, 4), "paste it with its top-left clip at the fifth scene");
    check (pitch (one, 4) == 60 && pitch (one, 5) == 61 && pitch (two, 4) == 62, "the paste keeps the group's shape");
    s.undo();
    check (pitch (one, 4) == -1 && pitch (two, 4) == -1, "one undo removes the whole paste");
    const int last = (int) s.snapshot().scenes.size() - 1;
    check (s.pasteSlotClip (one, last).failed() && s.pasteSlotClip (two, 0).failed(), "a paste that would leave the grid, or land on the audio track, is refused");
    ok (s.clearSlots (group), "clear the group");
    check (pitch (one, 0) == -1 && pitch (one, 1) == -1 && pitch (two, 0) == -1, "all three are cleared");
    s.undo();
    check (pitch (one, 0) == 60 && pitch (one, 1) == 61 && pitch (two, 0) == 62, "and one undo brings them back");
    ok (s.copySlotClips ({ { one, 0 }, { one, 1 } }, true), "cut two clips");
    check (pitch (one, 0) == -1 && pitch (one, 1) == -1 && s.hasSlotClipboard(), "the cut empties both");
    ok (s.pasteSlotClip (two, 2), "paste them on the other MIDI track");
    check (pitch (two, 2) == 60 && pitch (two, 3) == 61 && three >= 0, "they paste there in order");
}
int runSlotClipTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("slot-clips-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        {
            // A playing clip moved within its track plays on uninterrupted.
            const auto still = perform ({});
            const auto moved = perform ([] (Session& s, int track)
            {
                const auto id = s.snapshot().tracks[(size_t) track].slots[0].clip.id;
                ok (s.moveSlotClip (track, 0, track, 3), "move the playing clip down its track");
                const auto slots = s.snapshot().tracks[(size_t) track].slots;
                check (! slots[0].hasClip && slots[3].hasClip && slots[3].clip.id == id && slots[3].playing, "it is the same clip, playing on in its new slot");
            });
            double worst = 0;
            for (int c = 0; c < 2; ++c) for (int n = 0; n < still.getNumSamples(); ++n) worst = std::max (worst, (double) std::abs (still.getSample (c, n) - moved.getSample (c, n)));
            check (still.getMagnitude (0, 0, still.getNumSamples()) > .05f && worst < 1e-6,
                   "what is heard is the same as without the move, its note held (" + juce::String (worst, 8) + " at worst)");
        }

        Session s (false);
        te::test_utilities::EnginePlayer player (engineOf (s), parameters());
        ok (s.setLaunchQuantisation (Session::LaunchQuantisation::none), "launch at once");
        while (s.snapshot().scenes.size() < 4) ok (s.addScene(), "a scene");
        ok (s.addMidiTrack ("Bass"), "a bass track"); ok (s.addMidiTrack ("Keys"), "a keys track"); ok (s.addAudioTrack ("Loop"), "an audio track");
        const int bass = (int) s.snapshot().tracks.size() - 3, keys = bass + 1, loop = bass + 2;
        const auto slots = [&] (int track) { return s.snapshot().tracks[(size_t) track].slots; };
        const auto pitch = [&] (int track, int scene) { const auto slot = slots (track)[(size_t) scene]; return slot.hasClip && slot.clip.notes.size() == 1 ? slot.clip.notes[0].pitch : -1; };
        juce::StringArray added;
        for (const auto& [scene, note] : { std::pair { 0, 36 }, std::pair { 1, 38 } })
        {
            ok (s.addMidiClipToSlot (s.snapshot().tracks[(size_t) bass].id, scene, 4), "a bass clip");
            ok (s.addMidiNotes (slots (bass)[(size_t) scene].clip.id, { { "", note, 0, 1, .8f } }, added), "its note");
        }
        ok (s.importAudioToSlot (tone (root), loop, 0), "a tone in the audio track's first slot");

        // Moving within a track, and onto a clip.
        const auto first = slots (bass)[0].clip.id;
        ok (s.moveSlotClip (bass, 0, bass, 2), "move the first bass clip to the third slot");
        check (! slots (bass)[0].hasClip && pitch (bass, 2) == 36 && slots (bass)[2].clip.id == first && pitch (bass, 1) == 38, "it moves, keeping its identity");
        s.undo();
        check (pitch (bass, 0) == 36 && ! slots (bass)[2].hasClip && slots (bass)[0].clip.id == first, "one undo puts it back");
        ok (s.moveSlotClip (bass, 0, bass, 1), "move it onto the second bass clip");
        check (! slots (bass)[0].hasClip && pitch (bass, 1) == 36 && slots (bass)[1].clip.id == first, "it replaces the clip there");
        s.undo();
        check (pitch (bass, 0) == 36 && pitch (bass, 1) == 38, "and undo brings that clip back");

        // Copying and moving across tracks.
        ok (s.moveSlotClip (bass, 1, keys, 0, true), "copy the second bass clip to the keys track");
        check (pitch (bass, 1) == 38 && pitch (keys, 0) == 38 && slots (keys)[0].clip.id != slots (bass)[1].clip.id
                   && slots (keys)[0].clip.notes[0].id != slots (bass)[1].clip.notes[0].id, "the copy is a clip of its own, the original kept");
        ok (s.moveSlotClip (bass, 1, bass, 3, true), "copy it within its track");
        check (pitch (bass, 3) == 38 && pitch (bass, 1) == 38 && slots (bass)[3].clip.id != slots (bass)[1].clip.id, "a copy within the track is its own too");
        check (s.moveSlotClip (bass, 0, loop, 1).failed() && s.moveSlotClip (loop, 0, bass, 2).failed() && s.moveSlotClip (bass, 2, keys, 1).failed(),
               "MIDI clips go only to MIDI tracks and audio only to audio tracks; an empty slot has nothing to move");
        ok (s.moveSlotClip (loop, 0, loop, 3), "move the tone to the audio track's last slot");
        check (! slots (loop)[0].hasClip && slots (loop)[3].hasClip && slots (loop)[3].clip.isAudio, "audio clips move too");
        ok (s.launchSlot (bass, 0), "play the first bass clip");
        dispatch (s); player.process (4096); dispatch (s);
        ok (s.moveSlotClip (bass, 0, keys, 1), "move the playing clip to the keys track");
        dispatch (s); player.process (1024); dispatch (s);
        check (! slots (bass)[0].hasClip && pitch (keys, 1) == 36 && ! slots (keys)[1].playing && ! slots (bass)[0].playing, "moved to another track it stops");
        s.stop(); dispatch (s);

        // Duplicate: the slot below, or a scene inserted for it.
        int to = -1;
        const auto scenes = s.snapshot().scenes.size();
        ok (s.duplicateSlotClip (keys, 1, to), "duplicate the keys clip with an empty slot below");
        check (to == 2 && pitch (keys, 2) == 36 && s.snapshot().scenes.size() == scenes && slots (keys)[2].clip.id != slots (keys)[1].clip.id, "the copy goes in the slot below");
        ok (s.duplicateSlotClip (keys, 1, to), "duplicate it again, the slot below now taken");
        check (to == 2 && s.snapshot().scenes.size() == scenes + 1 && pitch (keys, 2) == 36 && pitch (keys, 3) == 36 && pitch (bass, 4) == 38 && ! slots (bass)[2].hasClip,
               "a scene is inserted below for the copy, the rows below moving down");
        s.undo();
        check (s.snapshot().scenes.size() == scenes && pitch (keys, 2) == 36 && pitch (bass, 3) == 38, "one undo removes the copy and its scene");
        const auto last = (int) s.snapshot().scenes.size() - 1;
        ok (s.moveSlotClip (bass, 3, bass, last), "move a bass clip to the last scene");
        ok (s.duplicateSlotClip (bass, last, to), "duplicate it there");
        check (to == last + 1 && s.snapshot().scenes.size() == scenes + 1 && pitch (bass, last + 1) == 38, "a scene is added for it");
        s.undo();

        // The Session clipboard.
        check (! s.hasSlotClipboard() && s.copySlotClip (bass, 0).failed(), "an empty slot copies nothing");
        ok (s.copySlotClip (keys, 1), "copy the keys clip");
        check (s.hasSlotClipboard() && pitch (keys, 1) == 36, "it is on the clipboard, the clip kept");
        ok (s.pasteSlotClip (bass, 0), "paste it into the first bass slot");
        check (pitch (bass, 0) == 36 && slots (bass)[0].clip.id != slots (keys)[1].clip.id && slots (bass)[0].clip.notes[0].id != slots (keys)[1].clip.notes[0].id,
               "the paste is a clip of its own");
        ok (s.pasteSlotClip (bass, 1), "paste it onto the second bass clip");
        check (pitch (bass, 1) == 36, "a paste replaces the clip there");
        s.undo(); check (pitch (bass, 1) == 38, "one undo brings the clip back");
        check (s.pasteSlotClip (loop, 1).failed(), "MIDI is not pasted into an audio track");
        ok (s.copySlotClip (bass, 1, true), "cut the second bass clip");
        check (! slots (bass)[1].hasClip && s.hasSlotClipboard(), "the cut empties its slot");
        ok (s.pasteSlotClip (keys, 2), "paste it over the keys track's third clip");
        check (pitch (keys, 2) == 38, "the cut clip pastes");
        s.undo(); s.undo();
        check (pitch (bass, 1) == 38 && pitch (keys, 2) == 36, "undo reverses the paste and the cut");
        // One clipboard, as in the reference: an Arrangement clip's copy pastes into a slot, keeping its
        // beats and notes, and a slot clip's copy pastes into the Arrangement.
        const auto bassId = s.snapshot().tracks[(size_t) bass].id, keysId = s.snapshot().tracks[(size_t) keys].id, loopId = s.snapshot().tracks[(size_t) loop].id;
        ok (s.addMidiClip (bassId, 8, 3), "a three-beat Arrangement clip at beat 9");
        const auto arrangementClip = [&] (const juce::String& trackId, double beat)
        {
            const auto tracks = s.snapshot().tracks;
            for (const auto& t : tracks) if (t.id == trackId) for (const auto& c : t.clips) if (std::abs (c.startBeat - beat) < 1e-6) return c;
            return Session::ClipInfo {};
        };
        const auto arrangementId = arrangementClip (bassId, 8).id;
        ok (s.addMidiNotes (arrangementId, { { "", 41, 1, .5, .8f } }, added), "a note on its second beat");
        ok (s.copyMidiClip (arrangementId), "copy it");
        check (s.hasSlotClipboard() && s.hasMidiClipClipboard(), "the copy pastes in either view");
        const int lastScene = (int) s.snapshot().scenes.size() - 1;
        check (! slots (keys)[(size_t) lastScene].hasClip, "the keys track's last slot is empty");
        ok (s.pasteSlotClip (keys, lastScene), "paste it into that slot");
        {
            const auto slot = slots (keys)[(size_t) lastScene];
            check (slot.hasClip && slot.clip.id != arrangementId && slot.clip.notes.size() == 1 && slot.clip.notes[0].pitch == 41 && std::abs (slot.clip.notes[0].startBeat - 1) < 1e-9
                       && std::abs (slot.clip.lengthBeats - 3) < 1e-6,
                   "it becomes a Session clip of its own, its note and three beats kept");
        }
        ok (s.copySlotClip (keys, 1), "copy a four-beat Session clip");
        check (s.hasMidiClipClipboard() && s.hasSlotClipboard(), "it too pastes in either view");
        juce::String pastedId;
        ok (s.pasteMidiClip (keysId, s.secondsAt (16), pastedId), "paste it into the Arrangement at beat 17");
        {
            const auto pasted = arrangementClip (keysId, 16);
            check (pasted.id == pastedId && std::abs (pasted.lengthBeats - 4) < 1e-6 && pasted.notes.size() == 1 && pasted.notes[0].pitch == 36,
                   "it becomes an Arrangement clip there, its four beats and note kept");
        }

        // Dragged between the views, a clip is copied across, the clipboard left as it was.
        {
            const auto undoSteps = s.undoSteps().size();
            juce::String draggedId;
            ok (s.copySlotClipToArrangement (keys, 1, keysId, s.secondsAt (24), draggedId), "drag the four-beat Session clip into the Arrangement at beat 25");
            const auto dragged = arrangementClip (keysId, 24);
            check (dragged.id == draggedId && std::abs (dragged.lengthBeats - 4) < 1e-6 && dragged.notes.size() == 1 && slots (keys)[1].hasClip && s.undoSteps().size() == undoSteps + 1,
                   "it is copied there in one undo step, its beats and note kept, the slot keeping its clip");
            check (s.hasMidiClipClipboard() && s.hasSlotClipboard() && ! s.hasAudioClipClipboard(), "the clipboard keeps the clip copied before");
            check (s.copySlotClipToArrangement (keys, 1, loopId, 0, draggedId).failed(), "a MIDI clip is refused on an audio track");
            const int emptyScene = lastScene - 1;
            check (! slots (bass)[(size_t) emptyScene].hasClip, "an empty bass slot");
            ok (s.copyClipToSlot (arrangementId, bass, emptyScene), "drag the three-beat Arrangement clip into it");
            const auto slot = slots (bass)[(size_t) emptyScene];
            check (slot.hasClip && slot.clip.notes.size() == 1 && slot.clip.notes[0].pitch == 41 && std::abs (slot.clip.lengthBeats - 3) < 1e-6 && arrangementClip (bassId, 8).id == arrangementId,
                   "it becomes a Session clip there, the Arrangement keeping its own");
            s.undo(); s.undo();
            check (! slots (bass)[(size_t) emptyScene].hasClip && arrangementClip (keysId, 24).id.isEmpty(), "two undo steps take both copies back");
        }
        // Audio pastes into the Arrangement, and into another project, collected there.
        ok (s.copySlotClip (loop, 3), "copy the tone's clip");
        check (s.hasSlotClipboard() && s.hasAudioClipClipboard() && ! s.hasMidiClipClipboard(), "an audio copy replaces the MIDI one");
        ok (s.pasteAudioClip (loopId, 1, pastedId), "paste the tone into the Arrangement at one second");
        {
            const auto tracks = s.snapshot().tracks;
            bool found = false;
            for (const auto& t : tracks) if (t.id == loopId) for (const auto& c : t.clips) found = found || (c.id == pastedId && c.isAudio && c.file.existsAsFile() && std::abs (c.start - 1) < 1e-6);
            check (found, "it is an Arrangement audio clip there, its audio collected");
        }
        ok (s.newProject(), "a new project");
        const auto project = root.getChildFile ("Other").getChildFile ("Other.halftrack");
        ok (s.saveProject (project), "saved");
        int audioTrack = -1;
        for (int t = 0; t < (int) s.snapshot().tracks.size() && audioTrack < 0; ++t)
            if (! s.snapshot().tracks[(size_t) t].isMidi && ! s.snapshot().tracks[(size_t) t].isReturn) audioTrack = t;
        if (audioTrack < 0) { ok (s.addAudioTrack ("Audio"), "an audio track"); audioTrack = (int) s.snapshot().tracks.size() - 1; }
        ok (s.pasteSlotClip (audioTrack, 0), "paste the tone into it");
        const auto pasted = slots (audioTrack)[0].clip;
        check (slots (audioTrack)[0].hasClip && pasted.isAudio && pasted.file.existsAsFile() && pasted.file.isAChildOf (project.getParentDirectory()),
               "its audio is collected into the new project");
        groupTests();
        root.deleteRecursively();
        std::cout << "Slot clips passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL slot clips: " << e.what() << std::endl; return 1; }
}
}
