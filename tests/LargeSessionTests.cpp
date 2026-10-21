#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#if JUCE_MAC
 #include <mach/mach.h>
#elif JUCE_LINUX
 #include <unistd.h>
#elif JUCE_WINDOWS
 #include <windows.h>
 #include <psapi.h>
#endif

// A large session: 24 audio tracks of 16 clips each with an EQ Three, 24 MIDI tracks of 16 clips
// of 32 notes, and a 64-point volume envelope on every track. It is saved, reopened and rendered;
// each step's time and the memory the reopened project takes are reported, and each is held under
// a generous limit so a regression fails the suite. Everything reopens as it was.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS large session: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { if (r.failed()) throw std::runtime_error ((text + ": " + r.getErrorMessage()).toStdString()); }
// This process's resident memory in megabytes, or -1 where unavailable.
double residentMegabytes()
{
   #if JUCE_MAC
    mach_task_basic_info info {}; mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info (mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count) == KERN_SUCCESS) return info.resident_size / 1048576.0;
   #elif JUCE_LINUX
    long pages = 0, resident = 0;
    if (auto* f = std::fopen ("/proc/self/statm", "r")) { if (std::fscanf (f, "%ld %ld", &pages, &resident) != 2) resident = -1; std::fclose (f); return resident * (double) sysconf (_SC_PAGESIZE) / 1048576.0; }
   #elif JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS counters {};
    if (GetProcessMemoryInfo (GetCurrentProcess(), &counters, sizeof (counters))) return counters.WorkingSetSize / 1048576.0;
   #endif
    return -1;
}
template <typename Step> double seconds (Step&& step)
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    step();
    return (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
}
juce::String counts (Session& s)
{
    int clips = 0, notes = 0, points = 0, devices = 0;
    for (const auto& t : s.snapshot().tracks)
    {
        clips += (int) t.clips.size(); devices += (int) t.plugins.size(); points += (int) t.volumeAutomation.points.size();
        for (const auto& c : t.clips) notes += (int) c.notes.size();
    }
    return juce::String ((int) s.snapshot().tracks.size()) + " tracks, " + juce::String (clips) + " clips, " + juce::String (notes) + " notes, "
           + juce::String (points) + " automation points, " + juce::String (devices) + " devices";
}
}

int runLargeSessionTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("large-session-" + juce::Uuid().toString()); check (root.createDirectory().wasOk(), "create fixtures");
        const auto source = root.getChildFile ("tone.wav");
        {
            juce::AudioBuffer<float> x (2, 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (.05 * std::sin (n * .03)));
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a one-second tone");
        }
        const auto project = root.getChildFile ("Large").getChildFile ("Large.halftrack");
        juce::String built;
        {
            Session s (false);
            const auto buildTime = seconds ([&]
            {
                const auto indexOf = [&] (const juce::String& name)
                { const auto tracks = s.snapshot().tracks; for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i].name == name) return i; return -1; };
                for (int t = 0; t < 24; ++t)
                {
                    const auto name = "Large audio " + juce::String (t + 1);
                    ok (s.addAudioTrack (name), "add an audio track");
                    const int index = indexOf (name);
                    for (int c = 0; c < 16; ++c) ok (s.importAudio (source, index, c * 1.5), "import a clip");
                    ok (s.addNativeEffect (index, Session::EffectKind::eqThree), "add an EQ Three");
                }
                for (int t = 0; t < 24; ++t)
                {
                    const auto name = "Large MIDI " + juce::String (t + 1);
                    ok (s.addMidiTrack (name), "add a MIDI track");
                    const auto index = indexOf (name);
                    const auto trackId = s.snapshot().tracks[(size_t) index].id;
                    for (int c = 0; c < 16; ++c)
                    {
                        ok (s.addMidiClip (trackId, c * 4.0, 4), "add a MIDI clip");
                        std::vector<Session::MidiNoteInfo> notes;
                        for (int n = 0; n < 32; ++n) notes.push_back ({ {}, 48 + (n * 7) % 36, n * .125, .1, .5f });
                        juce::StringArray ids;
                        ok (s.addMidiNotes (s.snapshot().tracks[(size_t) index].clips.back().id, notes, ids), "add notes");
                    }
                }
                for (const auto& t : s.snapshot().tracks)
                {
                    if (t.isReturn) continue;
                    Session::AutomationLane lane;
                    for (int p = 0; p < 64; ++p) lane.points.push_back ({ p * 1.0, (float) (-12 + 6 * std::sin (p * .3)) });
                    ok (s.setTrackAutomation (t.id, Session::AutomationTarget::volume, lane), "automate the volume");
                }
            });
            built = counts (s);
            check (s.snapshot().tracks.size() >= 48, "built " + built + " in " + juce::String (buildTime, 2) + " s");
            const auto saveTime = seconds ([&] { ok (s.saveProject (project), "save"); });
            check (saveTime < 20, "saved " + juce::String (project.getSize() / 1024) + " KB in " + juce::String (saveTime, 2) + " s");
        }
        const auto before = residentMegabytes();
        Session reopened (false);
        const auto loadTime = seconds ([&] { ok (reopened.loadProject (project), "reopen"); });
        const auto after = residentMegabytes();
        check (counts (reopened) == built, "reopened " + counts (reopened) + " in " + juce::String (loadTime, 2) + " s");
        check (loadTime < 30, "within the load limit");
        if (before > 0) check (after - before < 2048, "the reopened project takes " + juce::String (after - before, 1) + " MB more memory");
        const auto out = root.getChildFile ("out.wav");
        const auto renderTime = seconds ([&] { ok (reopened.exportWav (out, 48000, 24, 0), "render"); });
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (out));
        check (reader != nullptr && reader->lengthInSamples > 48000 * 20, "rendered " + juce::String (reader != nullptr ? reader->lengthInSamples / 48000.0 : 0.0, 1)
                                                                       + " s of audio in " + juce::String (renderTime, 2) + " s");
        check (renderTime < 120, "within the render limit");
        std::cout << "Large session passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL large session: " << e.what() << std::endl; return 1; }
}
}
