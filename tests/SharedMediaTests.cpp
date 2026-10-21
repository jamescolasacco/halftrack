#include "Session.h"
#include <cmath>
#include <iostream>
#include <map>
#include <string_view>
#include <stdexcept>

// Shared media edits. Two clips play the same source file; editing one of them (reversing, gain,
// fades, warping, consolidating) never rewrites that file or the other clip, and a project saved
// elsewhere and then edited leaves the first project's files and clips as they were. Every audio
// file's bytes are hashed before and after.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS shared media: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
// Every audio file under a folder, by a hash of its bytes.
std::map<juce::String, size_t> hashes (const juce::File& folder)
{
    std::map<juce::String, size_t> result;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac"))
    {
        juce::MemoryBlock bytes; f.loadFileAsData (bytes);
        result[f.getRelativePathFrom (folder)] = std::hash<std::string_view> {} (std::string_view (static_cast<const char*> (bytes.getData()), bytes.getSize()));
    }
    return result;
}
juce::AudioBuffer<float> read (const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr, "read " + file.getFileName());
    juce::AudioBuffer<float> x ((int) reader->numChannels, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
}

int runSharedMediaTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("shared-media-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("Library").getChildFile ("ramp.wav");
        ok (source.getParentDirectory().createDirectory(), "create a library folder");
        juce::AudioBuffer<float> ramp (2, 48000);
        for (int c = 0; c < 2; ++c) for (int n = 0; n < ramp.getNumSamples(); ++n) ramp.setSample (c, n, (float) n / ramp.getNumSamples() * (c == 0 ? .5f : -.5f));
        {
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (ramp, 0, ramp.getNumSamples()), "write a one-second ramp");
        }
        const auto library = hashes (source.getParentDirectory());

        Session s (false);
        const auto first = root.getChildFile ("First").getChildFile ("First.halftrack");
        ok (s.saveProject (first), "save the first project");
        ok (s.importAudio (source, 0, 0), "import the ramp");
        const auto clips = [&] { return s.snapshot().tracks[0].clips; };
        const auto a = clips()[0].id;
        juce::String b;
        ok (s.duplicateAudioClip (a, b), "duplicate the clip");
        const auto clipA = [&] { for (const auto& c : clips()) if (c.id == a) return c; return Session::ClipInfo {}; };
        const auto clipB = [&] { for (const auto& c : clips()) if (c.id == b) return c; return Session::ClipInfo {}; };
        check (clipA().file == clipB().file, "both clips play the same file");
        ok (s.saveProject (first), "save, collecting the media");
        const auto firstAssets = first.getParentDirectory();
        const auto before = hashes (firstAssets);
        check (! before.empty() && hashes (source.getParentDirectory()) == library, "the project holds its own copy, and the library file is untouched");

        // Edits to B.
        ok (s.setAudioClipReversed (b, true), "reverse B");
        auto settings = clipB().audio; settings.gainDb = -6; settings.fadeInSeconds = .1; settings.fadeOutSeconds = .1;
        ok (s.setAudioClipSettings (b, settings), "give B gain and fades");
        juce::String c;
        ok (s.duplicateAudioClip (a, c), "duplicate A again as C");
        ok (s.setAudioClipWarp (c, { Session::WarpMode::stretch, 90 }), "warp C");
        check (clipB().reversed && clipA().file == clipB().file, "B plays reversed from the shared file");
        check (! clipA().reversed && clipA().audio.gainDb == 0 && clipA().warp.mode == Session::WarpMode::off, "A keeps its own settings");
        auto now = hashes (firstAssets);
        bool kept = true; for (const auto& [path, hash] : before) kept = kept && now.count (path) && now[path] == hash;
        check (kept, "no file A uses, or any file there before, was rewritten");
        const auto unchanged = read (clipA().file);
        float worst = 0; for (int c = 0; c < 2; ++c) for (int n = 0; n < ramp.getNumSamples(); ++n) worst = std::max (worst, std::abs (unchanged.getSample (c, n) - ramp.getSample (c, n)));
        check (unchanged.getNumSamples() == ramp.getNumSamples() && worst == 0, "A's file still holds the ramp exactly");
        juce::StringArray made;
        ok (s.consolidateRange ({ s.snapshot().tracks[0].id }, 0, clipB().start + clipB().length, made), "consolidate the track");
        now = hashes (firstAssets);
        kept = true; for (const auto& [path, hash] : before) kept = kept && now.count (path) && now[path] == hash;
        check (kept && now.size() > before.size(), "consolidating renders a new file and leaves the others");
        s.undo();
        ok (s.saveProject (first), "save the first project");
        const auto saved = hashes (firstAssets);

        // A second project saved from it, then edited.
        const auto second = root.getChildFile ("Second").getChildFile ("Second.halftrack");
        ok (s.saveProject (second), "save it as a second project");
        check (clipA().file.isAChildOf (second.getParentDirectory()), "the second project plays its own copies");
        ok (s.setAudioClipReversed (a, true), "reverse A in the second project");
        ok (s.saveProject (second), "save the second project");
        check (hashes (firstAssets) == saved && hashes (source.getParentDirectory()) == library, "the first project's and the library's files are untouched");
        ok (s.loadProject (first), "reopen the first project");
        check (! clipA().reversed && clipA().file.existsAsFile() && read (clipA().file).getNumSamples() == ramp.getNumSamples(), "its A is still forward, from its own file");
        std::cout << "Shared media passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL shared media: " << e.what() << std::endl; return 1; }
}
}
