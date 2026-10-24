#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Clip loops and edges, rendered. A four-beat MIDI clip holds a loud note on its first beat and a
// quiet one on its second, so each beat of a render tells which part of the content played there:
// a loop repeating inside the clip, a clip made longer than its content, a start inside the loop,
// and a start edge trimmed in. A warped audio clip of alternating loud and quiet clicks loops
// likewise, keeps its loop across a tempo change and ends its loop when unwarped. Each edit is one
// undo step, invalid ones are refused, and loops reopen with the project.
namespace halftrack
{
void useHardClipEdges();
namespace te = tracktion::engine;
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS clip loops: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int beatSamples = 24000; // A beat at 120 BPM and 48 kHz.

// Each beat's level over its first eighth of a beat, from a render.
std::vector<float> beatLevels (Session& s, const juce::File& file, int beats)
{
    file.deleteFile();
    ok (s.exportWav (file), "render");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr) throw std::runtime_error ("could not read the render");
    juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    std::vector<float> levels;
    for (int b = 0; b < beats; ++b)
        levels.push_back (b * beatSamples + beatSamples / 8 <= audio.getNumSamples() ? audio.getMagnitude (b * beatSamples, beatSamples / 8) : 0.0f);
    return levels;
}
// Loud, quiet or silent beats as L, q and -, given the loud level.
juce::String pattern (const std::vector<float>& levels, float loud)
{
    juce::String result;
    for (auto level : levels) result << (level > loud * .7f ? "L" : level > loud * .1f ? "q" : "-");
    return result;
}
}

int runClipLoopTests (const juce::File& artifacts)
{
    try
    {
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
        const auto root = artifacts.getChildFile ("clip-loops-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto render = root.getChildFile ("render.wav");
        Session s (false);
        ok (s.saveProject (root.getChildFile ("Loops").getChildFile ("Loops.halftrack")), "save a project");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        auto track = s.snapshot().tracks.back();
        auto synth = track.synth; synth.attackSeconds = synth.decaySeconds = 0; synth.sustain = 1; synth.releaseSeconds = .01f;
        ok (s.setSynthSettings (track.id, synth), "a synth with instant attack and a short release");
        ok (s.addMidiClip (track.id, 0, 4), "a four-beat MIDI clip");
        const auto clip = [&] { return s.snapshot().tracks.back().clips[0]; };
        const auto id = clip().id;
        juce::StringArray added;
        ok (s.addMidiNotes (id, { { "", 60, 0, .25, 1.0f }, { "", 60, 1, .25, .25f } }, added), "a loud note on beat 1 and a quiet one on beat 2");
        check (clip().canLoop && ! clip().loop.on && clip().contentStartBeat == 0 && clip().loop.start == 0 && std::abs (clip().loop.length - 4) < 1e-9,
               "an unlooped clip plays its content from its start for its length");
        auto levels = beatLevels (s, render, 8);
        const float loud = levels[0];
        check (loud > .05f && pattern (levels, loud) == "Lq------", "unlooped, the notes play once: " + pattern (levels, loud));

        ok (s.setClipLoop (id, { true, 0, 2 }, 0), "loop its first two beats");
        check (clip().loop == Session::ClipLoop { true, 0, 2 } && clip().contentStartBeat == 0 && std::abs (clip().lengthBeats - 4) < 1e-9, "the clip reports its loop and keeps its length");
        check (pattern (beatLevels (s, render, 8), loud) == "LqLq----", "the loop repeats for as long as the clip lasts");
        ok (s.setClipBeats (id, 0, 8, 0), "make the clip eight beats long");
        check (pattern (beatLevels (s, render, 10), loud) == "LqLqLqLq--", "a longer clip repeats its loop further");
        ok (s.setClipLoop (id, { true, 0, 2 }, 1), "start the clip on its loop's second beat");
        check (std::abs (clip().contentStartBeat - 1) < 1e-9 && pattern (beatLevels (s, render, 10), loud) == "qLqLqLqL--", "it plays from there into the loop");
        ok (s.setClipLoop (id, { true, 1, 1 }, 1), "loop only the second beat");
        check (pattern (beatLevels (s, render, 10), loud) == "qqqqqqqq--", "the one-beat loop repeats the quiet note");
        s.undo();
        check (clip().loop == Session::ClipLoop { true, 0, 2 } && std::abs (clip().contentStartBeat - 1) < 1e-9, "one undo returns the two-beat loop and its start");
        s.undo(); s.undo();
        check (clip().loop == Session::ClipLoop { true, 0, 2 } && clip().contentStartBeat == 0 && std::abs (clip().lengthBeats - 4) < 1e-9, "each edit is one undo step");

        // Refusals.
        check (s.setClipLoop (id, { true, 0, 0 }, 0).failed() && s.setClipLoop (id, { true, 0, 2 }, 2).failed() && s.setClipLoop (id, { true, -1, 2 }, 0).failed()
                   && s.setClipLoop ("missing", { true, 0, 2 }, 0).failed() && s.setClipBeats (id, 0, 4, 3).failed() && s.setClipBeats (id, -1, 4, 0).failed()
                   && s.setClipBeats (id, 0, 0, 0).failed(),
               "an empty loop, a start outside the loop, negative beats, a missing clip and a trim off the loop are refused");
        check (clip().loop == Session::ClipLoop { true, 0, 2 } && clip().contentStartBeat == 0, "and change nothing");

        // Unlooped: the start edge trims the clip without moving its content.
        ok (s.setClipLoop (id, { false, 0, 0 }, 0), "turn the loop off");
        check (! clip().loop.on && clip().contentStartBeat == 0 && pattern (beatLevels (s, render, 6), loud) == "Lq----", "unlooped again, the notes play once");
        ok (s.setClipBeats (id, 1, 3, 1), "trim the start edge to the second beat");
        check (std::abs (clip().startBeat - 1) < 1e-9 && std::abs (clip().lengthBeats - 3) < 1e-9 && std::abs (clip().contentStartBeat - 1) < 1e-9
                   && pattern (beatLevels (s, render, 6), loud) == "-q----",
               "the trimmed clip starts on its second beat with its quiet note in place");
        ok (s.setClipBeats (id, 0, 4, 1), "move it to the start, still playing from its second beat");
        check (pattern (beatLevels (s, render, 6), loud) == "q-----", "its content moves with it");
        ok (s.setClipBeats (id, 0, 4, 0), "untrim it");

        // Reopened.
        ok (s.setClipLoop (id, { true, 0, 2 }, 1), "loop it again, starting on the second beat");
        ok (s.setClipBeats (id, 0, 6, 1), "six beats long");
        ok (s.saveProject (s.snapshot().projectFile), "save");
        {
            Session reopened (false);
            ok (reopened.loadProject (s.snapshot().projectFile), "reopen");
            const auto again = reopened.snapshot().tracks.back().clips[0];
            check (again.loop == Session::ClipLoop { true, 0, 2 } && std::abs (again.contentStartBeat - 1) < 1e-9 && std::abs (again.lengthBeats - 6) < 1e-9
                       && pattern (beatLevels (reopened, root.getChildFile ("reopened.wav"), 8), loud) == "qLqLqL--",
                   "the loop, start and length reopen and play the same");
        }

        // Duplicated, copied and pasted, a clip keeps its loop and start.
        juce::String copyId;
        ok (s.duplicateMidiClip (id, copyId), "duplicate the looped clip");
        check (pattern (beatLevels (s, render, 14), loud) == "qLqLqLqLqLqL--", "the duplicate plays the same loop from the same start");
        s.undo();
        ok (s.copyMidiClip (id), "copy it");
        ok (s.pasteMidiClip (track.id, 8, copyId), "paste it four bars later");
        check (pattern (beatLevels (s, render, 24), loud) == "qLqLqL----------qLqLqL--", "the pasted clip plays the same loop from the same start");
        s.undo();

        // A Session clip loops over its content; unlooped, it plays once.
        {
            auto& engine = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (s.audioDeviceManager()).engine;
            te::HostedAudioDeviceInterface::Parameters p; p.sampleRate = 48000; p.blockSize = 128; p.inputChannels = 0; p.outputChannels = 2; p.useMidiDevices = false;
            te::test_utilities::EnginePlayer player (engine, p);
            const auto dispatch = [&] { for (auto* edit : engine.getActiveEdits().getEdits()) edit->dispatchPendingUpdatesSynchronously(); };
            ok (s.setLaunchQuantisation (Session::LaunchQuantisation::none), "launch at once");
            ok (s.addMidiClipToSlot (track.id, 0, 4), "a Session clip");
            const auto slotClip = [&] { return s.snapshot().tracks.back().slots[0].clip; };
            check (slotClip().canLoop && slotClip().loop == Session::ClipLoop { true, 0, 4 }, "a Session clip loops over its content");
            ok (s.addMidiNotes (slotClip().id, { { "", 60, 0, .25, 1.0f }, { "", 60, 1, .25, .25f } }, added), "the same two notes in it");
            // Each beat's level from a fresh launch, the Arrangement's clip moved out of the way.
            const auto launched = [&] (int beats)
            {
                s.stop(); dispatch(); player.process (4096);
                ok (s.launchScene (0), "launch it"); dispatch();
                std::vector<float> levels;
                for (int b = 0; b < beats; ++b) { const auto out = player.process (beatSamples); levels.push_back (out.getMagnitude (0, 0, beatSamples / 8)); }
                const bool still = s.snapshot().tracks.back().slots[0].playing;
                s.stop(); dispatch();
                return std::pair { levels, still };
            };
            ok (s.setClipBeats (id, 100, 6, 1), "move the Arrangement clip far away");
            ok (s.setClipLoop (slotClip().id, { true, 0, 2 }, 0), "loop the Session clip's first two beats");
            auto [looped, loopedPlaying] = launched (8);
            const auto slotLoud = *std::max_element (looped.begin(), looped.end());
            check (pattern (looped, slotLoud) == "LqLqLqLq" && loopedPlaying, "launched, it loops: " + pattern (looped, slotLoud));
            ok (s.setClipLoop (slotClip().id, { false, 0, 0 }, 0), "turn its loop off");
            check (! slotClip().loop.on && std::abs (slotClip().lengthBeats - 2) < 1e-6, "unlooped, it ends where its loop did");
            auto [once, oncePlaying] = launched (8);
            check (pattern (once, slotLoud) == "Lq------" && ! oncePlaying, "launched, it plays once and stops: " + pattern (once, slotLoud));
            check (s.setClipBeats (slotClip().id, 1, 4, 0).failed(), "a Session clip cannot leave its slot's start");
            ok (s.setClipBeats (slotClip().id, 0, 1, 1), "make it one beat long from its second beat");
            auto [short1, shortPlaying] = launched (4);
            check (pattern (short1, slotLoud) == "q---" && ! shortPlaying, "it plays just that beat: " + pattern (short1, slotLoud));
            ok (s.setClipLoop (slotClip().id, { true, 0, 2 }, 1), "loop it again");
            check (slotClip().loop.on, "and it loops again");
            // Crop keeps only what the clip plays, which then begins it: here its loop over beats 1 to 3,
            // holding the quiet note; the loud one before it goes.
            ok (s.setClipLoop (slotClip().id, { true, 1, 2 }, 1), "loop beats 1 to 3");
            auto [beforeCrop, beforePlaying] = launched (4);
            check (s.canCropSessionClips ({ slotClip().id }), "the Session clip can be cropped");
            ok (s.cropSessionClips ({ slotClip().id }), "crop it");
            check (slotClip().notes.size() == 1 && std::abs (slotClip().notes[0].startBeat) < 1e-9 && slotClip().loop == Session::ClipLoop { true, 0, 2 }
                       && std::abs (slotClip().contentStartBeat) < 1e-9, "only the quiet note is left, at the start of a loop of two beats");
            auto [afterCrop, afterPlaying] = launched (4);
            check (pattern (afterCrop, slotLoud) == pattern (beforeCrop, slotLoud) && afterPlaying, "launched, it sounds as before: " + pattern (afterCrop, slotLoud));
            s.undo();
            check (slotClip().notes.size() == 2 && slotClip().loop == Session::ClipLoop { true, 1, 2 }, "one undo restores the notes and the loop");
            check (! s.canCropSessionClips ({ id }), "an Arrangement clip is cropped with a time selection instead");
        }

        // Warped audio: four seconds of clicks on the beat at 120 BPM, loud on even beats.
        const auto clicks = root.getChildFile ("clicks.wav");
        {
            juce::AudioBuffer<float> audio (1, 4 * 48000);
            audio.clear();
            for (int b = 0; b < 8; ++b)
                for (int n = 0; n < 480; ++n) audio.setSample (0, b * beatSamples + n, (b % 2 == 0 ? .8f : .2f) * std::exp (-n / 80.0f) * (n % 2 == 0 ? 1.0f : -1.0f));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (clicks.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (24));
            check (writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "write eight beats of clicks");
        }
        ok (s.removeClip ((int) s.snapshot().tracks.size() - 1, 0), "clear the MIDI clip from the Arrangement");
        ok (s.addAudioTrack ("Clicks"), "add an audio track");
        const int clickTrack = (int) s.snapshot().tracks.size() - 1;
        ok (s.importAudio (clicks, clickTrack, 0), "import the clicks");
        const auto audioClip = [&] { return s.snapshot().tracks[(size_t) clickTrack].clips[0]; };
        const auto audioId = audioClip().id;
        check (! audioClip().canLoop && s.setClipLoop (audioId, { true, 0, 2 }, 0).failed(), "an unwarped audio clip cannot loop");
        ok (s.setAudioClipWarp (audioId, { Session::WarpMode::beats, 120 }), "warp it at 120 BPM in Beats");
        const float click = beatLevels (s, render, 1)[0];
        check (audioClip().canLoop && ! audioClip().loop.on && std::abs (audioClip().loop.length - 8) < 1e-6
                   && pattern (beatLevels (s, render, 10), click) == "LqLqLqLq--", "warped, it plays its eight beats once");
        check (s.setClipLoop (audioId, { true, 6, 4 }, 6).failed(), "its loop must end within its audio");
        ok (s.setClipLoop (audioId, { true, 3, 2 }, 3), "loop its fourth and fifth beats");
        check (pattern (beatLevels (s, render, 10), click) == "qLqLqLqL--", "the loop repeats in place of the rest");
        ok (s.setClipBeats (audioId, 0, 12, 3), "make it twelve beats long, past its audio");
        check (pattern (beatLevels (s, render, 14), click) == "qLqLqLqLqLqL--", "the loop carries on past the end of the audio");
        ok (s.setAudioClipWarp (audioId, { Session::WarpMode::beats, 60 }), "call the audio 60 BPM");
        check (audioClip().loop.on && std::abs (audioClip().loop.start - 1.5) < 1e-6 && std::abs (audioClip().loop.length - 1) < 1e-6,
               "the loop covers the same audio at the new tempo");
        s.undo();
        ok (s.setClipLoop (audioId, { false, 0, 0 }, 3), "turn the loop off, starting on the fourth beat");
        {
            const auto unlooped = pattern (beatLevels (s, render, 8), click);
            check (std::abs (audioClip().lengthBeats - 5) < 1e-6 && unlooped == "qLqLq---",
                   "unlooped, it ends with its audio (" + juce::String (audioClip().lengthBeats, 3) + " beats, " + unlooped + ", from " + juce::String (audioClip().contentStartBeat, 3) + ")");
        }
        s.undo();
        ok (s.setAudioClipWarp (audioId, { Session::WarpMode::off, 120 }), "turn Warp off while it loops");
        check (! audioClip().loop.on && ! audioClip().canLoop && std::abs (audioClip().sourceOffset - 1.5) < 1e-6 && audioClip().canEditAudioRange,
               "the loop ends, the clip still starting on its fourth beat");
        // Crop for a Session audio clip: a new sample holding only the audio it plays.
        ok (s.importAudioToSlot (clicks, clickTrack, 0), "the clicks in a Session slot");
        const auto sessionAudio = [&] { return s.snapshot().tracks[(size_t) clickTrack].slots[0].clip; };
        ok (s.setAudioClipWarp (sessionAudio().id, { Session::WarpMode::beats, 120 }), "at 120 BPM");
        ok (s.setClipLoop (sessionAudio().id, { true, 2, 2 }, 2), "looping its third and fourth beats");
        const auto uncropped = sessionAudio().file;
        ok (s.cropSessionClips ({ sessionAudio().id }), "crop it");
        {
            const auto cropped = sessionAudio();
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (cropped.file)), source (formats.createReaderFor (clicks));
            check (reader != nullptr && reader->lengthInSamples == 48000 && cropped.file != uncropped && cropped.loop == Session::ClipLoop { true, 0, 2 },
                   "it plays a new one-second sample, looping over it");
            juce::AudioBuffer<float> got (1, 48000), want (1, 48000);
            reader->read (&got, 0, 48000, 0, true, false); source->read (&want, 0, 48000, 48000, true, false);
            double worst = 0;
            for (int n = 0; n < 48000; ++n) worst = std::max (worst, (double) std::abs (got.getSample (0, n) - want.getSample (0, n)));
            check (worst < 1e-6, "holding the clicks' second second exactly (max difference " + juce::String (worst, 8) + ")");
        }
        s.undo();
        check (sessionAudio().file == uncropped && sessionAudio().loop == Session::ClipLoop { true, 2, 2 }, "one undo returns the clip to its sample and loop");
        root.deleteRecursively();
        std::cout << "Clip loops passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL clip loops: " << e.what() << std::endl; return 1; }
}
}
