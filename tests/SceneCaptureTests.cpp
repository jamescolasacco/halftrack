#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

// Capture and Insert Scene: two MIDI tracks play held notes from clips in different scenes and an
// audio track plays a sine. Capturing makes a scene after the selected one holding exactly those
// clips, which play on in it at once, with copies left in their old scenes. The same performance
// rendered with and without the capture is identical: nothing is interrupted, no held note is cut.
// Nothing playing, nothing is captured; the capture is one undo step.
namespace halftrack
{
namespace te = tracktion::engine;
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS scene capture: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int captureAt = 13696, after = 19200;

struct Fixture { int bass = -1, keys = -1, loop = -1; };
// Plays the fixture's clips and returns what is heard from `captureAt` samples on, calling
// `capture` there when given one.
juce::AudioBuffer<float> perform (const juce::File& root, const std::function<void (Session&, const Fixture&)>& capture)
{
    // The loop's sine is read from memory, so the engine never waits on the disk. It outlives the
    // Session and its graph's readers.
    choc::buffer::InterleavedBuffer<float> memory (2, 48000);
    Session s (false);
    auto& engine = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (s.audioDeviceManager()).engine;
    te::HostedAudioDeviceInterface::Parameters p; p.sampleRate = 48000; p.blockSize = 128; p.inputChannels = 0; p.outputChannels = 2; p.useMidiDevices = false;
    te::test_utilities::EnginePlayer player (engine, p);
    const auto dispatch = [&] { for (auto* edit : engine.getActiveEdits().getEdits()) edit->dispatchPendingUpdatesSynchronously(); };
    ok (s.setLaunchQuantisation (Session::LaunchQuantisation::none), "launch at once");
    while (s.snapshot().scenes.size() < 3) ok (s.addScene(), "a scene");
    ok (s.addMidiTrack ("Bass"), "a bass track"); ok (s.addMidiTrack ("Keys"), "a keys track"); ok (s.addAudioTrack ("Loop"), "an audio track");
    Fixture f;
    f.bass = (int) s.snapshot().tracks.size() - 3; f.keys = f.bass + 1; f.loop = f.bass + 2;
    // Bass and keys have clips in scenes 1 and 2, each holding a whole-clip note of its own pitch.
    juce::StringArray added;
    for (const auto& [track, scene, pitch] : { std::tuple { f.bass, 0, 36 }, std::tuple { f.bass, 1, 38 }, std::tuple { f.keys, 0, 60 }, std::tuple { f.keys, 1, 64 } })
    {
        ok (s.addMidiClipToSlot (s.snapshot().tracks[(size_t) track].id, scene, 4), "a clip");
        ok (s.addMidiNotes (s.snapshot().tracks[(size_t) track].slots[(size_t) scene].clip.id, { { "", pitch, 0, 4, .8f } }, added), "its note");
    }
    const auto sine = root.getChildFile ("Sine.wav");
    juce::AudioBuffer<float> wave (2, 48000);
    for (int n = 0; n < wave.getNumSamples(); ++n)
        for (int c = 0; c < 2; ++c) wave.setSample (c, n, .25f * (float) std::sin (juce::MathConstants<double>::twoPi * 330 * n / 48000));
    if (! sine.existsAsFile())
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (sine.createOutputStream());
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
        check (writer && writer->writeFromAudioSampleBuffer (wave, 0, wave.getNumSamples()), "write the loop's sine");
    }
    ok (s.importAudioToSlot (sine, f.loop, 0), "the sine in the audio track's first slot");
    for (unsigned int c = 0; c < 2; ++c) for (unsigned int n = 0; n < 48000; ++n) memory.getSample (c, n) = wave.getSample ((int) c, (int) n);
    engine.getAudioFileManager().registerMemoryBuffer (s.snapshot().tracks[(size_t) f.loop].slots[0].clip.file.getFullPathName().toStdString(), memory.getView(), 48000);

    if (capture)
    {
        juce::String none;
        check (s.captureAndInsertScene (1, none).failed(), "with nothing playing, nothing is captured");
    }
    ok (s.launchSlot (f.bass, 0), "play the first bass clip"); ok (s.launchSlot (f.keys, 1), "and the second keys clip"); ok (s.launchSlot (f.loop, 0), "and the sine");
    dispatch(); player.process (captureAt); dispatch();
    if (capture) { capture (s, f); dispatch(); }
    auto heard = player.process (after);
    s.stop(); dispatch();
    return heard;
}
}

int runSceneCaptureTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("scene-capture-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto uninterrupted = perform (root, {});
        const auto captured = perform (root, [] (Session& s, const Fixture& f)
        {
            const auto scenes = s.snapshot().scenes.size();
            const auto before = s.snapshot();
            juce::String sceneId;
            ok (s.captureAndInsertScene (1, sceneId), "Capture and Insert Scene after the second scene");
            const auto now = s.snapshot();
            check (now.scenes.size() == scenes + 1 && now.scenes[2].id == sceneId, "a scene is inserted after the second");
            const auto& bass = now.tracks[(size_t) f.bass].slots; const auto& keys = now.tracks[(size_t) f.keys].slots; const auto& loop = now.tracks[(size_t) f.loop].slots;
            check (bass[2].hasClip && keys[2].hasClip && loop[2].hasClip && bass[2].clip.notes.size() == 1 && bass[2].clip.notes[0].pitch == 36
                       && keys[2].clip.notes.size() == 1 && keys[2].clip.notes[0].pitch == 64 && loop[2].clip.isAudio,
                   "it holds the playing clips: the first bass clip, the second keys clip and the sine");
            check (bass[2].playing && keys[2].playing && loop[2].playing && ! bass[0].playing && ! keys[1].playing && ! loop[0].playing,
                   "and plays at once, the scenes they came from no longer playing");
            check (bass[0].hasClip && keys[1].hasClip && loop[0].hasClip && bass[0].clip.notes.size() == 1 && bass[0].clip.notes[0].pitch == 36 && keys[1].clip.notes[0].pitch == 64
                       && bass[0].clip.id != bass[2].clip.id && bass[0].clip.notes[0].id != bass[2].clip.notes[0].id && keys[1].clip.id != keys[2].clip.id,
                   "copies of their own stay in the old scenes");
            check (bass[1].hasClip && bass[1].clip.notes[0].pitch == 38 && keys[0].hasClip && keys[0].clip.notes[0].pitch == 60, "the rest of the grid is kept");
            s.undo();
            const auto undone = s.snapshot();
            check (undone.scenes.size() == scenes && undone.tracks[(size_t) f.bass].slots[0].clip.id == before.tracks[(size_t) f.bass].slots[0].clip.id
                       && undone.tracks[(size_t) f.bass].slots[0].playing && undone.tracks[(size_t) f.keys].slots[1].playing && undone.tracks[(size_t) f.loop].slots[0].playing,
                   "one undo removes the captured scene, the clips playing on in their own");
            s.redo();
            check (s.snapshot().scenes[2].id == sceneId && s.snapshot().tracks[(size_t) f.bass].slots[2].playing, "and redo captures again");
        });
        double worst = 0, level = 0;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < after; ++n)
            {
                worst = std::max (worst, (double) std::abs (captured.getSample (c, n) - uninterrupted.getSample (c, n)));
                level = std::max (level, (double) std::abs (uninterrupted.getSample (c, n)));
            }
        check (level > .2 && uninterrupted.getMagnitude (0, after - 4800, 4800) > .2f, "the held notes and the sine sound throughout");
        check (worst < 1e-6, "capturing interrupts nothing: what is heard is the same as without it (" + juce::String (worst, 8) + " at worst)");
        root.deleteRecursively();
        std::cout << "Scene capture passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL scene capture: " << e.what() << std::endl; return 1; }
}
}
