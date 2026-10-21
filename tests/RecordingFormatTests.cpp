#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Recording file type and bit depth, through the synthetic device. Takes are written as WAV, AIFF
// or FLAC at the chosen depth; depths a type cannot hold, and changes while recording, are refused.
// When the take cannot be written, recording is refused with the reason and nothing is left running.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS recording format: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
}

int runRecordingFormatTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("recording-format-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        const auto project = root.getChildFile ("Takes").getChildFile ("Takes.halftrack");
        ok (s.saveProject (project), "save a recording destination");
        ok (s.setTrackInput (0, s.inputNames()[0]), "route the input"); ok (s.setTrackArmed (0, true), "arm the track");
        pumpSyntheticTestDevice (s, 8);
        check (s.recordingFormat().type == Session::RecordingFileType::wav, "takes record as WAV by default");
        using Type = Session::RecordingFileType;
        for (const auto [type, bits, extension, label] : { std::tuple { Type::wav, 16, "wav", "WAV" }, { Type::wav, 24, "wav", "WAV" }, { Type::wav, 32, "wav", "WAV" },
                                                           { Type::aiff, 24, "aif;aiff", "AIFF" }, { Type::flac, 16, "flac", "FLAC" }, { Type::flac, 24, "flac", "FLAC" } })
        {
            const auto name = juce::String (label) + " at " + juce::String (bits) + " bits";
            ok (s.setRecordingFormat ({ type, bits }), "record " + name);
            check (s.recordingFormat() == Session::RecordingFormat { type, bits }, "the setting reports " + name);
            s.seek (0); ok (s.record(), "record");
            pumpSyntheticTestDevice (s, blocksPerSecond / 2);
            s.stop();
            const auto clips = s.snapshot().tracks[0].clips;
            check (! clips.empty(), "a take is kept");
            const auto file = clips.back().file;
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            check (file.hasFileExtension (extension) && reader != nullptr && (int) reader->bitsPerSample == bits, "the take is " + name);
            juce::AudioBuffer<float> x ((int) reader->numChannels, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
            check (x.getMagnitude (0, x.getNumSamples()) > .1f, "and holds the input");
            s.undo();
        }
        check (s.setRecordingFormat ({ Type::flac, 32 }).failed() && s.setRecordingFormat ({ Type::wav, 12 }).failed(), "depths a type cannot hold are refused");
        s.seek (0); ok (s.record(), "record");
        check (s.setRecordingFormat ({ Type::wav, 16 }).failed(), "the format does not change while recording");
        pumpSyntheticTestDevice (s, 8); s.stop(); s.undo();

       #if ! JUCE_WINDOWS
        // Windows ignores a folder's read-only flag when creating files, so this runs elsewhere.
        const auto assets = project.getSiblingFile ("Takes Assets");
        check (assets.isDirectory() && assets.setReadOnly (true, false), "make the take folder unwritable");
        s.seek (0);
        const auto refused = s.record();
        pumpSyntheticTestDevice (s, 8);
        assets.setReadOnly (false, false);
        check (refused.failed() && refused.getErrorMessage().isNotEmpty() && ! s.snapshot().recording,
               "recording into an unwritable folder is refused with the reason (" + refused.getErrorMessage() + ")");
        // The engine's reason was once taken as a file name, recording into "The directory..." here.
        check (juce::File::getCurrentWorkingDirectory().findChildFiles (juce::File::findFilesAndDirectories, false, "The directory*").isEmpty(),
               "and nothing is written under a path made from the reason");
        s.stop();
        s.seek (0); ok (s.record(), "once writable, recording works again");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4); s.stop();
        check (! s.snapshot().tracks[0].clips.empty(), "and keeps its take");
       #endif
        std::cout << "Recording format passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL recording format: " << e.what() << std::endl; return 1; }
}
}
