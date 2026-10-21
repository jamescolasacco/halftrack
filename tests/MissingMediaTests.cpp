#include "Session.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

// Missing media. A saved project's collected files are moved away: it still opens, its audio clip
// and Sampler sample are reported missing and play silence, and saving elsewhere keeps their
// references. Searching a folder relinks readable files by name, skipping decoys, after which the
// project renders as before.
namespace halftrack
{
namespace
{
constexpr double pi = 3.14159265358979323846;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS missing media: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
void write (const juce::File& f, double hz, double seconds)
{
    juce::AudioBuffer<float> x (2, (int) (seconds * 48000));
    for (int n = 0; n < x.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) x.setSample (c, n, (float) (.5 * std::sin (2 * pi * hz * n / 48000)));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render " + file.getFileName());
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
// The `source` attributes of the project's audio clips, resolved against its folder.
juce::Array<juce::File> clipSources (const juce::File& project)
{
    juce::Array<juce::File> sources;
    const auto xml = juce::parseXML (project);
    std::function<void (const juce::XmlElement&)> visit = [&] (const juce::XmlElement& e)
    {
        if (e.getTagName() == "AUDIOCLIP") sources.add (project.getParentDirectory().getChildFile (e.getStringAttribute ("source")));
        for (const auto* child : e.getChildIterator()) visit (*child);
    };
    if (xml) visit (*xml);
    return sources;
}
}

int runMissingMediaTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("missing-media-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto tone = root.getChildFile ("tone.wav"), hit = root.getChildFile ("hit.wav");
        write (tone, 440, 1); write (hit, 220, .5);
        const auto project = root.getChildFile ("A").getChildFile ("A.halftrack");
        juce::AudioBuffer<float> before;
        {
            Session first (false);
            ok (first.importAudio (tone, 0, 0), "an audio clip");
            ok (first.addMidiTrack ("Keys"), "a MIDI track");
            const auto keys = first.snapshot().tracks.back().id;
            ok (first.setInstrumentSample (keys, hit), "a Sampler sample");
            ok (first.addMidiClip (keys, 0, 4), "a MIDI clip");
            ok (first.addMidiNote (first.snapshot().tracks.back().clips[0].id, 60, 0, 1, .8f), "a note for the Sampler");
            ok (first.saveProject (project), "save, collecting both files");
            before = render (first, root.getChildFile ("before.wav"));
            check (before.getMagnitude (0, before.getNumSamples()) > .1f && first.missingMedia().isEmpty(), "the project sounds and nothing is missing");
        }

        // Move the collected files away, then open the project afresh: a running engine may still
        // hold the moved files open.
        const auto assets = project.getSiblingFile ("A Assets"), elsewhere = root.getChildFile ("Elsewhere").getChildFile ("Moved");
        ok (elsewhere.getParentDirectory().createDirectory(), "create a new home for the files");
        check (assets.moveFileTo (elsewhere) && ! assets.exists(), "move the project's files away");
        Session s (false);
        ok (s.loadProject (project), "the project still opens");
        auto clip = s.snapshot().tracks[0].clips[0];
        const auto missing = s.missingMedia();
        check (clip.mediaMissing && missing.size() == 2 && missing.contains (clip.file) && ! s.snapshot().tracks.back().clips[0].mediaMissing,
               "its audio clip and Sampler sample are reported missing");
        check (! s.snapshot().dirty, "opening with missing media is not an edit");
        const auto silent = render (s, root.getChildFile ("silent.wav"));
        check (silent.getMagnitude (0, silent.getNumSamples()) < 1e-6f, "missing media plays silence");
        const auto other = root.getChildFile ("B").getChildFile ("B.halftrack");
        ok (s.saveProject (other), "a project with missing media saves");
        const auto sources = clipSources (other);
        check (sources.size() == 1 && sources[0] == clip.file, "saving keeps the reference to where the file was");
        ok (s.loadProject (project), "reopen the original");

        // Locate: an empty folder and a decoy find nothing; the files' new home finds both.
        int found = -1;
        check (s.locateMissingMedia (root.getChildFile ("nowhere.txt"), found).failed() && found == 0, "a missing folder is refused");
        const auto decoy = root.getChildFile ("Decoy");
        ok (decoy.createDirectory(), "a decoy folder");
        check (decoy.getChildFile (tone.getFileName()).replaceWithText ("not audio"), "a text file with the clip's name");
        ok (s.locateMissingMedia (decoy, found), "search the decoy");
        check (found == 0 && s.missingMedia().size() == 2, "a file that is not audio is not linked");
        ok (s.locateMissingMedia (root.getChildFile ("Elsewhere"), found), "search the files' new home");
        clip = s.snapshot().tracks[0].clips[0];
        check (found == 2 && s.missingMedia().isEmpty() && ! clip.mediaMissing && clip.file.isAChildOf (elsewhere), "both files are relinked from their new home");
        check (s.snapshot().dirty && ! s.canUndo(), "relinking marks the project changed without an undo step");
        const auto after = render (s, root.getChildFile ("after.wav"));
        bool same = after.getNumSamples() == before.getNumSamples();
        for (int c = 0; same && c < 2; ++c) for (int n = 0; same && n < after.getNumSamples(); ++n) same = std::abs (after.getSample (c, n) - before.getSample (c, n)) < 1e-6f;
        check (same, "the relinked project renders as before");
        ok (s.saveProject (project), "save the relinked project");
        check (project.getSiblingFile ("A Assets").getChildFile (tone.getFileName()).existsAsFile(), "saving collects the relinked files again");
        std::cout << "Missing media passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL missing media: " << e.what() << std::endl; return 1; }
}
}
