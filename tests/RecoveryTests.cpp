#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Crash recovery. A project with imported audio, notes, a Sampler and mixer edits writes
// a recovery copy without saving; a second Session recovers it as unsaved work and renders
// it identically, and the original project file is never touched.
namespace halftrack
{
namespace
{
constexpr double rate = 48000;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS recovery: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
void write (const juce::File& f)
{
    juce::AudioBuffer<float> x (2, 48000);
    for (int n = 0; n < x.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) x.setSample (c, n, (float) (.3 * std::sin (n * .03 + c)));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32, .5), "render " + file.getFileName());
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1;
    double worst = 0;
    for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n) worst = std::max (worst, (double) std::abs (a.getSample (c, n) - b.getSample (c, n)));
    return worst;
}
}

int runRecoveryTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("recovery-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("source.wav"), sample = root.getChildFile ("sample.wav");
        write (source); write (sample);
        const auto recovery = root.getChildFile ("Recovery").getChildFile ("Recovery.halftrack");
        Session s (false);
        ok (s.importAudio (source, 0, 0), "import audio");
        ok (s.setTrackGain (0, -4), "turn the audio track down");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const auto keys = s.snapshot().tracks.back().id;
        ok (s.addMidiClip (keys, 0, 4), "a MIDI clip");
        ok (s.addMidiNote (s.snapshot().tracks.back().clips[0].id, 60, 0, 1, .8f), "a note");
        ok (s.setInstrumentSample (keys, sample), "play it from a Sampler");
        const auto before = s.snapshot();
        const bool couldUndo = s.canUndo();
        const auto reference = render (s, root.getChildFile ("reference.wav"));
        ok (s.writeRecovery (recovery), "write a recovery copy of the untitled project");
        const auto after = s.snapshot();
        check (recovery.existsAsFile() && after.dirty && after.projectFile == juce::File() && s.canUndo() == couldUndo,
               "writing it leaves the project untitled, unsaved and with its history");

        Session other (false);
        juce::File from { "/unset" };
        ok (other.recoverProject (recovery, from), "recover in another session");
        const auto recovered = other.snapshot();
        check (recovered.dirty && recovered.projectFile == juce::File() && from == juce::File() && ! other.canUndo(),
               "the recovered project is untitled, unsaved and has no origin or history");
        check (recovered.tracks.size() == before.tracks.size() && recovered.tracks[0].clips.size() == 1 && std::abs (recovered.tracks[0].gainDb + 4) < 1e-3
               && recovered.tracks.back().clips[0].notes.size() == 1 && recovered.tracks.back().instrument.sample == before.tracks.back().instrument.sample && before.tracks.back().instrument.sample.existsAsFile(), "tracks, clips, notes, mixer and Sampler come back");
        check (difference (render (other, root.getChildFile ("recovered.wav")), reference) < 1e-6, "the recovered project renders identically");

        // A saved project's recovery copy names it, and the saved file is never touched.
        const auto project = root.getChildFile ("Song").getChildFile ("Song.halftrack");
        ok (project.getParentDirectory().createDirectory(), "create a project folder");
        ok (s.saveProject (project), "save the project");
        const auto saved = project.loadFileAsString();
        ok (s.setTrackGain (0, -10), "change it after saving");
        ok (s.writeRecovery (recovery), "write a recovery copy of the changed project");
        check (project.loadFileAsString() == saved && s.snapshot().projectFile == project && s.snapshot().dirty, "the saved file and its association are unchanged");
        ok (other.recoverProject (recovery, from), "recover the changed project");
        check (from == project && other.snapshot().projectFile == juce::File() && std::abs (other.snapshot().tracks[0].gainDb + 10) < 1e-3,
               "recovery reports the original project and keeps the unsaved change, without reopening the original file");

        // A damaged copy is rejected and the open project stays.
        const auto broken = root.getChildFile ("Broken.halftrack");
        check (broken.replaceWithText ("<HALFTRACK version=\"1\"><EDIT"), "write a truncated recovery copy");
        check (other.recoverProject (broken, from).failed() && std::abs (other.snapshot().tracks[0].gainDb + 10) < 1e-3, "a damaged copy is rejected and the open project stays");
        std::cout << "Recovery passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL recovery: " << e.what() << std::endl; return 1; }
}
}
