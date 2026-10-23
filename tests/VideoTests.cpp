#include "Session.h"
#include "VideoMedia.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Movies: a generated two-second movie of 50 frames at 25 fps, each showing its number in stripes, with a 440 Hz tone as its sound. It is read back, imported as a video clip,
// followed through trims, saved, reopened and collected, and exported with the mix. Where movies are
// not supported, importing one is refused with the reason.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS video: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr double frameRate = 25;

// The frame a test movie shows at a time, or -1.
int frameAt (const juce::File& movie, double seconds)
{
    int frame = -1;
    return video::testFrameAt (movie, seconds, frame).wasOk() ? frame : -1;
}
juce::AudioBuffer<float> read (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) throw std::runtime_error ("could not read " + file.getFileName().toStdString());
    juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    return audio;
}
}

int runVideoTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("video-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto movie = root.getChildFile ("Picture.mov");
        if (! video::supported())
        {
            Session s (false);
            const auto refused = s.importVideo (movie);
            check (refused.failed() && refused.getErrorMessage().contains ("macOS"), "where movies are not supported, importing one is refused with the reason");
            root.deleteRecursively();
            std::cout << "Video passed (movies unsupported here)" << std::endl;
            return 0;
        }
        ok (video::writeTestMovie (movie, 50, frameRate, 64, 48, 440, 48000), "write a two-second movie of numbered frames with a 440 Hz tone");
        video::Info info;
        ok (video::probe (movie, info), "read the movie's details");
        check (std::abs (info.seconds - 2) < .01 && std::abs (info.frameRate - frameRate) < .01 && info.width == 64 && info.height == 48 && info.hasSound,
               "two seconds of 64 by 48 pictures at 25 fps, with sound");
        bool frames = true;
        for (const int f : { 0, 7, 9, 31, 49 }) frames = frames && frameAt (movie, (f + .5) / frameRate) == f;
        check (frames, "each frame shows where it was written");
        {
            // A clip's pictures: frame 20 scaled to 24 pixels high, its shape kept, its stripes readable;
            // pictures may come from a frame a tenth of a second either side.
            const auto image = video::frameImage (movie, 20.5 / frameRate, 24);
            int shown = 0;
            for (int bit = 0; bit < 6 && image.isValid(); ++bit)
                if (image.getPixelAt ((int) ((bit + .5) * image.getWidth() / 6), 12).getBrightness() > .5f) shown |= 1 << bit;
            check (image.isValid() && image.getHeight() == 24 && image.getWidth() == 32 && std::abs (shown - 20) <= 2,
                   "a frame's picture is 32 by 24 and shows frame 20 (" + juce::String (shown) + ")");
        }
        const auto sound = root.getChildFile ("sound.wav");
        ok (video::extractSound (movie, sound), "take its sound");
        {
            const auto audio = read (sound);
            int crossings = 0;
            for (int n = 1; n < 48000; ++n) crossings += (audio.getSample (0, n - 1) < 0) != (audio.getSample (0, n) < 0);
            check (std::abs (audio.getNumSamples() - 96000) <= 480 && std::abs (audio.getMagnitude (0, 0, 48000) - .5f) < .01f && std::abs (crossings - 880) <= 2,
                   "the sound is the movie's two seconds of 440 Hz (" + juce::String (crossings) + " crossings in a second)");
        }

        Session s (false);
        ok (s.saveProject (root.getChildFile ("Film").getChildFile ("Film.halftrack")), "save a project");
        const auto tracks = s.snapshot().tracks.size();
        ok (s.importVideo (movie), "import the movie");
        const auto clip = [&] { return s.snapshot().tracks.back().clips[0]; };
        const auto assets = root.getChildFile ("Film").getChildFile ("Film Assets");
        check (s.snapshot().tracks.size() == tracks + 1 && s.snapshot().tracks.back().name == juce::String ((int) s.snapshot().tracks.size()) + "-Picture" && clip().name == "Picture" && std::abs (clip().length - 2) < .01
                   && clip().videoFile.isAChildOf (assets) && clip().videoFile.existsAsFile() && clip().file.isAChildOf (assets) && clip().warp.mode == Session::WarpMode::off,
               "it becomes a track named after it, with an unwarped clip of its sound, both collected into the project");
        s.undo();
        check (s.snapshot().tracks.size() == tracks, "one undo removes the import");
        s.redo();
        const auto id = clip().id;
        const auto shown = s.videoAt (1.0);
        check (shown && shown->movie == clip().videoFile && std::abs (shown->seconds - 1) < 1e-6 && shown->clipId == id && ! s.videoAt (2.5),
               "at one second the Video window shows the movie's first second, and after the clip nothing");
        ok (s.setAudioClipRange (id, .5, 1, .4), "move the clip to half a second, trimmed to start 0.4 s in");
        const auto trimmed = s.videoAt (.6);
        check (trimmed && std::abs (trimmed->seconds - .5) < 1e-6 && frameAt (trimmed->movie, trimmed->seconds) == 12, "its picture follows the trim: frame 12 at 0.6 s");
        check (s.setAudioClipWarp (id, { Session::WarpMode::stretch, 120 }).failed() && s.setAudioClipReversed (id, true).failed() && ! clip().canWarp && ! clip().canReverse,
               "a video clip is not warped or reversed");

        ok (s.saveProject (s.snapshot().projectFile), "save");
        {
            Session reopened (false);
            ok (reopened.loadProject (s.snapshot().projectFile), "reopen");
            const auto again = reopened.snapshot().tracks.back().clips[0];
            const auto frame = reopened.videoAt (.6);
            check (again.videoFile == clip().videoFile && frame && std::abs (frame->seconds - .5) < 1e-6, "the video clip reopens with its movie");
            ok (reopened.saveProject (root.getChildFile ("Copy").getChildFile ("Copy.halftrack")), "save it elsewhere");
            check (reopened.snapshot().tracks.back().clips[0].videoFile.isAChildOf (root.getChildFile ("Copy").getChildFile ("Copy Assets"))
                       && reopened.snapshot().tracks.back().clips[0].videoFile.existsAsFile(), "saving elsewhere collects the movie there too");
        }

        // Export Video: the picture under the export's start, for the exported time, with the mix.
        const auto exported = root.getChildFile ("Export.wav");
        Session::AudioExportSettings settings;
        settings.timeRange = juce::Range<double> (.5, 1.5);
        settings.video = true;
        ok (s.exportAudio (exported, settings), "export half a second to a second and a half with video");
        const auto film = exported.withFileExtension (".mov");
        video::Info filmInfo;
        ok (video::probe (film, filmInfo), "read the exported movie");
        check (std::abs (filmInfo.seconds - 1) < .02 && filmInfo.hasSound && frameAt (film, .5 / frameRate) == 10 && frameAt (film, 20.5 / frameRate) == 30,
               "it holds the second from the clip's 0.4 s: frames 10 to 30, with sound");
        const auto filmSound = root.getChildFile ("film sound.wav");
        ok (video::extractSound (film, filmSound), "take its sound");
        {
            const auto mix = read (exported), heard = read (filmSound);
            double worst = 0;
            for (int n = 0; n < std::min (mix.getNumSamples(), heard.getNumSamples()); ++n) worst = std::max (worst, (double) std::abs (mix.getSample (0, n) - heard.getSample (0, n)));
            check (std::abs (heard.getNumSamples() - mix.getNumSamples()) <= 2 && worst < 1e-4, "its sound is the exported mix (" + juce::String (worst, 6) + " at worst)");
        }
        check (s.exportAudio (exported, settings).failed(), "an existing movie is kept unless replacing it is chosen");
        settings.replaceVideo = true;
        ok (s.exportAudio (exported, settings), "and replaced when it is");
        settings.timeRange = juce::Range<double> (0, .4);
        check (s.exportAudio (root.getChildFile ("Before.wav"), settings).failed(), "Export Video needs a video clip at the export's start");
        check (s.importVideo (sound).failed(), "a file with no picture is not imported as a movie");
        root.deleteRecursively();
        std::cout << "Video passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL video: " << e.what() << std::endl; return 1; }
}
}
