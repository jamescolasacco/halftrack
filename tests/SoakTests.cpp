#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#if JUCE_MAC || JUCE_LINUX
 #include <fcntl.h>
 #include <unistd.h>
#endif
#if JUCE_MAC
 #include <mach/mach.h>
#elif JUCE_LINUX
 #include <unistd.h>
#elif JUCE_WINDOWS
 #include <windows.h>
 #include <psapi.h>
#endif

// A long scripted session through the synthetic device: three minutes of playback with an edit
// every five seconds (gains, devices, clips, notes, mutes, undo and redo) and a recorded take
// every minute. Over the run the play head keeps time with the blocks played, resident memory and
// open file handles stay bounded, and the output stays finite. Each figure is printed.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS soak: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { if (r.failed()) throw std::runtime_error ((text + ": " + r.getErrorMessage()).toStdString()); }
constexpr int blocksPerSecond = 48000 / 256;
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
// Open file descriptors (handles on Windows), or -1 where unavailable.
int openHandles()
{
   #if JUCE_MAC || JUCE_LINUX
    // Each descriptor number that is open, up to the process's limit.
    int open = 0;
    for (int fd = 0, limit = (int) std::min<long> (sysconf (_SC_OPEN_MAX), 65536); fd < limit; ++fd) if (fcntl (fd, F_GETFD) != -1) ++open;
    return open;
   #elif JUCE_WINDOWS
    DWORD handles = 0;
    return GetProcessHandleCount (GetCurrentProcess(), &handles) ? (int) handles : -1;
   #else
    return -1;
   #endif
}
}

int runSoakTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("soak-" + juce::Uuid().toString()); check (root.createDirectory().wasOk(), "create fixtures");
        const auto source = root.getChildFile ("tone.wav");
        {
            // Ten seconds at a time: 0.02 radians a sample repeats exactly every 10 s only roughly,
            // which does not matter for a level check.
            juce::AudioBuffer<float> x (2, 48000 * 10);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (.05 * std::sin (n * .02)));
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (16));
            bool written = writer != nullptr;
            for (int part = 0; written && part < 2; ++part) written = writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples());
            check (written, "write a 20-second tone");
        }
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (root.getChildFile ("Soak").getChildFile ("Soak.halftrack")), "save");
        const auto index = [&] (const juce::String& name)
        { const auto tracks = s.snapshot().tracks; for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i].name == name) return i; throw std::runtime_error (("no track " + name).toStdString()); };
        for (int t = 0; t < 4; ++t) { ok (s.addAudioTrack ("Soak " + juce::String (t + 1)), "add a track"); ok (s.importAudio (source, index ("Soak " + juce::String (t + 1)), 0), "import the tone"); }
        for (int t = 0; t < 2; ++t)
        {
            ok (s.addMidiTrack ("Keys " + juce::String (t + 1)), "add a MIDI track");
            const auto id = s.snapshot().tracks.back().id;
            ok (s.addMidiClip (id, 0, 40), "a MIDI clip");
            std::vector<Session::MidiNoteInfo> notes;
            for (int n = 0; n < 20; ++n) notes.push_back ({ {}, 48 + (n * 5) % 24, n * 2.0, 1.0, .6f });
            juce::StringArray ids; ok (s.addMidiNotes (s.snapshot().tracks.back().clips[0].id, notes, ids), "notes all through it");
        }
        const int audio[] = { index ("Soak 1"), index ("Soak 2"), index ("Soak 3"), index ("Soak 4") }, keys[] = { index ("Keys 1"), index ("Keys 2") };
        const int takeTrack = audio[3];
        ok (s.addNativeEffect (audio[0], Session::EffectKind::eqThree), "an EQ Three");
        ok (s.setTrackInput (takeTrack, s.inputNames()[0]), "an input for takes");
        const auto settled = [&] { juce::MessageManager::getInstance()->runDispatchLoopUntil (150); return s.snapshot().position; };
        pumpSyntheticTestDevice (s, 8);

        const int seconds = juce::SystemStats::getEnvironmentVariable ("HALFTRACK_SOAK_SECONDS", "180").getIntValue(), step = 5;
        double memoryAfterFirstMinute = -1, worstDrift = 0;
        int handlesAfterFirstMinute = -1, takes = 0;
        // Playback loops over 16 s, so the media it reads stays the same size and memory growth
        // is the Session's own, not more of a file being mapped in.
        constexpr double loopSeconds = 16;
        s.setLoop (true, 0, loopSeconds);
        s.seek (0); ok (s.play(), "play");
        for (int elapsed = 0; elapsed < seconds; elapsed += step)
        {
            const auto before = settled();
            float peak = 0;
            for (int part = 0; part < step; ++part)
            {
                const auto heard = captureSyntheticTestDevice (s, blocksPerSecond);
                for (int c = 0; c < heard.getNumChannels(); ++c)
                    for (int n = 0; n < heard.getNumSamples(); ++n)
                        if (! std::isfinite (heard.getSample (c, n))) throw std::runtime_error ("a sample was not finite");
                peak = std::max (peak, heard.getMagnitude (0, heard.getNumSamples()));
            }
            auto travelled = settled() - before;
            if (travelled < 0) travelled += loopSeconds; // Once round the loop.
            worstDrift = std::max (worstDrift, std::abs (travelled - step * blocksPerSecond * 256 / 48000.0));
            if (peak > 8) throw std::runtime_error ("the output exceeded 8");
            // An edit every five seconds, in turn.
            const int track = audio[(elapsed / step) % 4];
            switch ((elapsed / step) % 8)
            {
                case 0: ok (s.setTrackGain (track, -12.0f + (float) (elapsed % 11)), "change a gain"); break;
                case 1: ok (s.addNativeEffect (audio[1], Session::EffectKind::saturator), "add a Saturator"); break;
                case 2: if (! s.snapshot().tracks[(size_t) audio[1]].plugins.empty()) ok (s.removePlugin (audio[1], (int) s.snapshot().tracks[(size_t) audio[1]].plugins.size() - 1), "remove it"); break;
                case 3: ok (s.setTrackMute (keys[0], ! s.snapshot().tracks[(size_t) keys[0]].mute), "mute or unmute a MIDI track"); break;
                case 4:
                {
                    juce::StringArray ids;
                    ok (s.addMidiNotes (s.snapshot().tracks[(size_t) keys[1]].clips[0].id, { { {}, 72, (double) ((elapsed / step) % 32), .5, .7f } }, ids), "add a note");
                    break;
                }
                case 5: s.undo(); break;
                case 6: s.redo(); break;
                case 7: ok (s.setTrackPan (track, (float) std::sin (elapsed)), "pan"); break;
            }
            // A take every minute: stop, record two seconds, undo it, play on. The snapshot is held
            // while its clips are read: a range-for over a member of a temporary's element dangles.
            if (elapsed % 60 == 55)
            {
                const auto files = [&] { const auto now = s.snapshot(); juce::StringArray f; for (const auto& c : now.tracks[(size_t) takeTrack].clips) f.add (c.file.getFileName()); return f; };
                const auto at = s.snapshot().position;
                s.stop(); pumpSyntheticTestDevice (s, 8);
                ok (s.setTrackArmed (takeTrack, true), "arm for a take");
                const auto before = files();
                s.setLoop (false, 0, loopSeconds); // One pass, not a loop recording.
                s.seek (std::min (at, loopSeconds - 3)); ok (s.record(), "record a take");
                pumpSyntheticTestDevice (s, 2 * blocksPerSecond);
                s.stop(); pumpSyntheticTestDevice (s, 8);
                // The take split the tone under it; one undo removes it and restores the tone.
                const bool recorded = files().size() > before.size();
                s.undo();
                if (recorded && files() == before) ++takes;
                s.setLoop (true, 0, loopSeconds);
                ok (s.setTrackArmed (takeTrack, false), "disarm");
                s.seek (std::min (at, loopSeconds - 3)); ok (s.play(), "play on");
            }
            if (elapsed + step == 60) { memoryAfterFirstMinute = residentMegabytes(); handlesAfterFirstMinute = openHandles(); }
            if ((elapsed + step) % 60 == 0) std::cout << "At " << (elapsed + step) << " s: " << juce::String (residentMegabytes(), 1) << " MB resident, " << openHandles() << " open handles" << std::endl;
        }
        s.stop();
        const auto memory = residentMegabytes();
        const int handles = openHandles();
        check (worstDrift < .02, "over " + juce::String (seconds) + " s the play head kept time with the blocks: worst " + juce::String (worstDrift * 1000, 2) + " ms in a " + juce::String (step) + " s stretch");
        check (takes == 3, juce::String (takes) + " takes recorded and deleted");
        if (memoryAfterFirstMinute > 0)
            check (memory - memoryAfterFirstMinute < 64, "resident memory grew " + juce::String (memory - memoryAfterFirstMinute, 1) + " MB after the first minute (" + juce::String (memory, 0) + " MB)");
        if (handlesAfterFirstMinute >= 0)
            check (handles - handlesAfterFirstMinute <= 8, "open handles went from " + juce::String (handlesAfterFirstMinute) + " to " + juce::String (handles));
        std::cout << "Soak passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL soak: " << e.what() << std::endl; return 1; }
}
}
