#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Browser preview. Through the synthetic device, a previewed file sounds at the preview volume
// beside the project, which it never changes, and stops when replaced, stopped or finished.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
constexpr double pi = 3.14159265358979323846;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS preview: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
void write (const juce::File& f, double hz, double seconds, double amplitude)
{
    juce::AudioBuffer<float> x (2, (int) (seconds * 48000));
    for (int n = 0; n < x.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) x.setSample (c, n, (float) (amplitude * std::sin (2 * pi * hz * n / 48000)));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
float peak (Session& s, int blocks) { const auto out = captureSyntheticTestDevice (s, blocks); return out.getMagnitude (0, 0, out.getNumSamples()); }
}

int runPreviewTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("preview-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto tone = root.getChildFile ("tone.wav"), other = root.getChildFile ("other.wav"), text = root.getChildFile ("notes.txt");
        write (tone, 440, 1.0, .5); write (other, 660, 4.0, .25);
        check (text.replaceWithText ("not audio"), "write a text file");
        {
            Session offline (false);
            check (offline.previewFile (tone).failed() && offline.snapshot().previewing == juce::File(), "without an audio device nothing previews");
        }
        Session s (false, makeSyntheticTestDeviceType());
        pumpSyntheticTestDevice (s, 8);
        check (peak (s, 20) == 0, "the empty project is silent");
        ok (s.previewFile (tone), "preview a 0.5 amplitude tone");
        const auto during = s.snapshot();
        // A loaded machine can take a moment to start the preview; its level is measured once it sounds.
        for (int wait = 0; wait < 40 && peak (s, 1) < .1f; ++wait) {}
        // Previews play at the cue volume, -6 dB at first as in the reference's default Set.
        check (during.previewing == tone && during.previewVolumeDb == -6 && during.cueVolumeDb == -6 && std::abs (peak (s, 40) - .5f * std::pow (10.0f, -6 / 20.0f)) < .01f,
               "the preview sounds at the cue volume, -6 dB at first");
        check (during.tracks.size() == 2 && during.tracks[0].clips.empty() && ! during.dirty && ! s.canUndo(), "the project is untouched");
        ok (s.setPreviewVolume (0), "the preview volume, which is the cue volume, at 0 dB");
        captureSyntheticTestDevice (s, 10); // The volume glides to its new level.
        check (std::abs (peak (s, 40) - .5f) < .01f && s.snapshot().previewVolumeDb == 0 && s.snapshot().cueVolumeDb == 0, "the preview sounds at the file's level");
        ok (s.setPreviewVolume (-6), "preview volume -6 dB");
        captureSyntheticTestDevice (s, 10);
        check (std::abs (peak (s, 40) - .5f * std::pow (10.0f, -6 / 20.0f)) < .01f && s.snapshot().previewVolumeDb == -6, "the preview volume scales it");
        check (s.setPreviewVolume (7).failed() && s.setPreviewVolume (-71).failed(), "volumes outside -70 to +6 dB are refused");
        captureSyntheticTestDevice (s, 200); // Past the one-second file.
        check (peak (s, 20) == 0 && s.snapshot().previewing == juce::File(), "the preview stops at the end of the file");
        ok (s.previewFile (other), "preview a longer file");
        check (std::abs (peak (s, 40) - .25f * std::pow (10.0f, -6 / 20.0f)) < .01f, "it plays at the same preview volume");
        ok (s.previewFile (tone), "preview another file");
        check (s.snapshot().previewing == tone && std::abs (peak (s, 40) - .25f) < .01f, "the new file replaces the old one");
        s.stopPreview();
        check (peak (s, 20) == 0 && s.snapshot().previewing == juce::File(), "Stop silences the preview");
        check (s.previewFile (text).failed() && s.previewFile (root.getChildFile ("missing.wav")).failed() && s.snapshot().previewing == juce::File(),
               "text and missing files cannot be previewed");
        // Beside a playing project.
        ok (s.importAudio (other, 0, 0), "put the longer file in the project");
        ok (s.setPreviewVolume (0), "preview volume back to 0 dB");
        s.seek (0); ok (s.play(), "play the project");
        const auto alone = peak (s, 40);
        ok (s.previewFile (tone), "preview while the project plays");
        const auto both = peak (s, 40);
        check (std::abs (alone - .25f) < .01f && both > alone + .4f && s.snapshot().playing, "the preview adds to the playing project");
        s.stopPreview(); s.stop();
        std::cout << "Preview passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL preview: " << e.what() << std::endl; return 1; }
}
}
