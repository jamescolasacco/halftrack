#include "Session.h"
#include <iostream>
#include <stdexcept>

// Audio input permission, with a stand-in for the system's permission and its prompt (the real one
// is never asked). Refused, a hardware input cannot be armed, monitored or recorded, and the reason
// says what to do; another track's audio needs no permission. Undetermined, arming asks once and
// returns at once; the answer, arriving later, decides the next try. Withdrawn after arming,
// recording is refused rather than taking silence.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS input permission: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
}

int runInputPermissionTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("input-permission-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (root.getChildFile ("Permission").getChildFile ("Permission.halftrack")), "save a recording destination");
        check (s.inputPermission() == Session::InputPermission::granted, "a Session without audio devices needs no permission");
        const auto inputs = s.inputNames (false);
        check (! inputs.isEmpty(), "the synthetic device has inputs");
        ok (s.setTrackInput (0, inputs[0]), "give track 1 a hardware input");
        const auto a = s.snapshot().tracks[0].id, b = s.snapshot().tracks[1].id;
        const auto armed = [&] (int index) { const auto now = s.snapshot(); return now.tracks[(size_t) index].armed; };

        // Refused.
        auto status = Session::InputPermission::denied;
        int asked = 0;
        std::function<void (bool)> answer;
        s.setInputPermissionSource ([&] { return status; }, [&] (std::function<void (bool)> done) { ++asked; answer = std::move (done); });
        check (s.inputPermission() == Session::InputPermission::denied, "the stand-in reports a refused permission");
        const auto refused = s.setTrackArmed (0, true);
        check (refused.failed() && refused.getErrorMessage().contains ("System Settings") && refused.getErrorMessage().contains ("silence") && ! armed (0),
               "arming the hardware input is refused, saying why and what to do (" + refused.getErrorMessage() + ")");
        check (s.setTrackMonitor (0, Session::MonitorMode::on).failed() && s.snapshot().tracks[0].monitor == Session::MonitorMode::off, "so is monitoring it");
        ok (s.setTrackMonitor (0, Session::MonitorMode::off), "turning monitoring off is allowed");
        check (asked == 0, "a refused permission is not asked for again");
        ok (s.setTrackInputTrack (b, a), "track 2 takes track 1's audio");
        ok (s.setTrackArmed (1, true), "another track's audio needs no permission");
        ok (s.setTrackArmed (1, false), "disarm it");

        // Undetermined: asked once, without waiting.
        status = Session::InputPermission::undetermined;
        const auto asking = s.setTrackArmed (0, true);
        check (asking.failed() && asking.getErrorMessage().contains ("asking") && asked == 1 && ! armed (0), "an undetermined permission is asked for and the arm waits for the answer");
        check (s.setTrackArmed (0, true).failed() && asked == 1, "asking again while the prompt is open does not open another");
        status = Session::InputPermission::denied; answer (false);
        check (s.setTrackArmed (0, true).getErrorMessage().contains ("System Settings"), "refused at the prompt, the next try explains it");
        status = Session::InputPermission::undetermined;
        check (s.setTrackArmed (0, true).failed() && asked == 2, "asked again later");
        status = Session::InputPermission::granted; answer (true);
        ok (s.setTrackArmed (0, true), "allowed at the prompt, the track arms");
        check (armed (0), "and shows armed");

        // Withdrawn after arming.
        pumpSyntheticTestDevice (s, 8);
        status = Session::InputPermission::denied;
        const auto clips = s.snapshot().tracks[0].clips.size();
        const auto recording = s.record();
        check (recording.failed() && recording.getErrorMessage().contains ("System Settings") && ! s.snapshot().recording, "recording is refused once permission is withdrawn");
        check (s.recordSlot (0, 0).failed() && s.sessionRecord (0).failed(), "and so is recording into Session slots");
        pumpSyntheticTestDevice (s, 8);
        check (s.snapshot().tracks[0].clips.size() == clips && s.snapshot().tracks[0].slots.front().hasClip == false, "no take of silence is made");
        ok (s.setTrackArmed (0, false), "disarming is allowed");
        status = Session::InputPermission::granted;
        ok (s.setTrackArmed (0, true), "with permission back, the track arms again");
        ok (s.setTrackArmed (0, false), "disarm it");
        std::cout << "Input permission passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL input permission: " << e.what() << std::endl; return 1; }
}
}
