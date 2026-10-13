#include "Session.h"
#include "SubtractiveSynth.h"
#include <algorithm>
#include <cmath>
#include <iostream>
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
    synth.controlChange (1, 123, 0);
    check (synth.activeVoices() == 0, "all-notes-off clears voices");
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
        coreTests();
        auto directory = requestedDirectory.getChildFile ("midi-" + juce::Uuid().toString());
        ok (directory.createDirectory(), "create isolated MIDI artifacts");
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
            const std::array<juce::Result, 10> attempts {
                session.newProject(), session.loadProject (project), session.saveProject (rejectedSave),
                session.removeTrack (2), session.addMidiNote (clipId, 60, 0, 1, 1),
                session.setSynthSettings (trackId, flatSettings()), session.setTrackGain (2, -20),
                session.addAudioTrack ("Reentrant track"), session.removeClip (2, 0),
                session.exportWav (nestedOutput)
            };
            mutationsRejected = std::all_of (attempts.begin(), attempts.end(), [] (const auto& result)
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
