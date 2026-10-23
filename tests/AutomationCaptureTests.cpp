#include "NativeEffects.h"
#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Automation override and capture, after the reference. Moving a control whose lane plays
// automation overrides the lane until Re-Enable Automation: renders follow the moved control, then
// the lane again. With Automation Arm on, recording writes moves into the lane at the playhead:
// within an edit gesture the earlier automation resumes after release; other moves hold until stop.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
constexpr double pi = 3.14159265358979323846;
using Lane = Session::AutomationLane;
using Target = Session::AutomationTarget;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS automation capture: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
void write (const juce::File& f)
{
    juce::AudioBuffer<float> x (2, 4 * 48000);
    for (int n = 0; n < x.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) x.setSample (c, n, (float) (.5 * std::sin (2 * pi * 500 * n / 48000)));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
// The render's level in dB relative to the 0.5-amplitude source, over its second second.
double level (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true);
    return 20 * std::log10 (x.getRMSLevel (0, 48000, 48000) / (.5 / std::sqrt (2.0)));
}
// A lane's value at a beat, linear in the engine's curve units, as playback interpolates it.
double valueAt (const Lane& lane, double beat, const std::function<float (float)>& proportion, const std::function<float (float)>& value)
{
    const auto& p = lane.points;
    if (p.empty()) return std::nan ("");
    if (beat <= p.front().beat) return p.front().value;
    for (size_t i = 1; i < p.size(); ++i)
        if (beat <= p[i].beat)
        {
            const auto t = (beat - p[i - 1].beat) / (p[i].beat - p[i - 1].beat);
            return value ((float) (proportion (p[i - 1].value) + t * (proportion (p[i].value) - proportion (p[i - 1].value))));
        }
    return p.back().value;
}
double volumeAt (const Lane& lane, double beat)
{
    return valueAt (lane, beat, [] (float v) { return Session::automationValueToProportion (Target::volume, v); },
                    [] (float p) { return Session::automationProportionToValue (Target::volume, p); });
}
double beatNow (Session& s) { return s.snapshot().position * 2; } // 120 BPM.
}

int runAutomationCaptureTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("automation-capture-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("tone.wav"), file = root.getChildFile ("render.wav"); write (source);

        // Override and Re-Enable Automation, heard in renders.
        {
            Session s (false);
            ok (s.importAudio (source, 0, 0), "import a tone");
            const auto trackId = s.snapshot().tracks[0].id;
            check (std::abs (level (s, file)) < .05, "the tone renders at its own level");
            ok (s.setTrackAutomation (trackId, Target::volume, { true, { { 0, -12 }, { 16, -12 } } }), "volume automation at -12 dB");
            check (std::abs (level (s, file) + 12) < .1 && ! s.snapshot().automationOverridden, "the render follows the lane");
            check (s.reEnableAutomation().wasOk() && ! s.canRedo(), "Re-Enable Automation with nothing overridden changes nothing");
            ok (s.setTrackGain (0, -3), "move the automated fader");
            check (! s.snapshot().tracks[0].volumeAutomation.enabled && s.snapshot().tracks[0].volumeAutomation.points.size() == 2 && s.snapshot().automationOverridden,
                   "the lane is overridden and keeps its points");
            check (std::abs (level (s, file) + 3) < .1, "the render follows the moved fader");
            ok (s.setTrackGain (0, -6), "move it again");
            check (std::abs (level (s, file) + 6) < .1, "an overridden fader keeps moving freely");
            ok (s.reEnableAutomation(), "Re-Enable Automation");
            check (s.snapshot().tracks[0].volumeAutomation.enabled && ! s.snapshot().automationOverridden && std::abs (level (s, file) + 12) < .1,
                   "the render follows the lane again");
            s.undo(); check (! s.snapshot().tracks[0].volumeAutomation.enabled && std::abs (level (s, file) + 6) < .1, "undo returns to the override");
            s.undo(); s.undo();
            check (s.snapshot().tracks[0].volumeAutomation.enabled && std::abs (level (s, file) + 12) < .1, "undoing both moves removes the override");

            // A device control and the master.
            ok (s.addNativeEffect (0, Session::EffectKind::channelEq), "add a Channel EQ");
            const auto eq = s.snapshot().tracks[0].plugins.back();
            const int output = effectParameterIndex (Session::EffectKind::channelEq, "outputDb");
            ok (s.setTrackAutomation (trackId, Target::volume, {}), "clear the volume lane");
            ok (s.setDeviceAutomation (eq.id, output, { true, { { 0, -12 }, { 16, -12 } } }), "automate the Channel EQ's Output at -12 dB");
            check (std::abs (level (s, file) + 12) < .1, "the render follows the device lane");
            auto settings = eq.effect; settings.values[(size_t) output] = -2;
            ok (s.setNativeEffectSettings (eq.id, settings), "turn the automated Output knob");
            const auto overridden = s.snapshot().tracks[0].plugins.back().automation;
            check (overridden.size() == 1 && ! overridden[0].lane.enabled && s.snapshot().automationOverridden && std::abs (level (s, file) + 2) < .1,
                   "the device lane is overridden and the render follows the knob");
            ok (s.reEnableAutomation(), "Re-Enable Automation for the device");
            check (s.snapshot().tracks[0].plugins.back().automation[0].lane.enabled && std::abs (level (s, file) + 12) < .1, "the device follows its lane again");
            ok (s.setDeviceAutomation (eq.id, output, {}), "clear the device lane");
            const auto baseline = level (s, file); // The knob's -2 dB.
            check (std::abs (baseline + 2) < .1, "a cleared device lane leaves the knob's value");
            ok (s.setMasterVolumeAutomation ({ true, { { 0, -9 }, { 16, -9 } } }), "master automation at -9 dB");
            ok (s.setMasterGain (-4), "move the automated master fader");
            check (! s.snapshot().masterVolumeAutomation.enabled && std::abs (level (s, file) - baseline + 4) < .1, "the master lane is overridden and the render follows the fader");
            ok (s.reEnableAutomation(), "Re-Enable Automation for the master");
            check (std::abs (level (s, file) - baseline + 9) < .1, "the master follows its lane again");
            check (s.snapshot().automationArm == false && s.record().failed(), "without Automation Arm or an armed track there is nothing to record");
        }

        // Capture while recording, through the synthetic device.
        Session live (false, makeSyntheticTestDeviceType());
        const auto trackId = live.snapshot().tracks[0].id;
        ok (live.setTrackAutomation (trackId, Target::volume, { true, { { 0, -20 }, { 64, -20 } } }), "an existing volume lane at -20 dB");
        ok (live.addNativeEffect (0, Session::EffectKind::channelEq), "add a Channel EQ");
        const auto eqId = live.snapshot().tracks[0].plugins.back().id;
        ok (live.addReturnTrack ("Echo"), "add a return, with the send Off");
        const auto echo = live.snapshot().tracks.back().id;
        const int midDb = effectParameterIndex (Session::EffectKind::channelEq, "midDb");
        ok (live.setAutomationArm (true), "turn on Automation Arm");
        check (live.snapshot().automationArm, "the snapshot reports Automation Arm");
        pumpSyntheticTestDevice (live, 8);
        ok (live.record(), "record automation with no armed track");
        pumpSyntheticTestDevice (live, 375); // About four beats.
        // A touch pass: the gesture holds the fader through two moves.
        live.beginEditGesture ("Track gain");
        const auto touchStart = beatNow (live); ok (live.setTrackGain (0, -10), "touch the fader to -10 dB");
        pumpSyntheticTestDevice (live, 188);
        const auto touchMove = beatNow (live); ok (live.setTrackGain (0, -5), "move it to -5 dB");
        pumpSyntheticTestDevice (live, 188);
        const auto touchEnd = beatNow (live); live.endEditGesture();
        // A latch pass on pan, and a device control.
        pumpSyntheticTestDevice (live, 188);
        const auto latchStart = beatNow (live); ok (live.setTrackPan (0, .5f), "move pan without a gesture");
        auto eq = live.snapshot().tracks[0].plugins.back().effect; eq.values[(size_t) midDb] = 6;
        ok (live.setNativeEffectSettings (eqId, eq), "turn the Channel EQ's Mid while recording");
        ok (live.setTrackSendGain (0, echo, -12), "turn the send up while recording");
        pumpSyntheticTestDevice (live, 188);
        const auto stopBeat = beatNow (live); live.stop();
        const auto recorded = live.snapshot();
        const auto& volume = recorded.tracks[0].volumeAutomation;
        check (volume.enabled && std::abs (volumeAt (volume, touchStart - .5) + 20) < .01 && std::abs (volumeAt (volume, touchStart + 1e-4) + 10) < .05
               && volumeAt (volume, (touchStart + touchMove) / 2) > -10 && volumeAt (volume, (touchStart + touchMove) / 2) < -5
               && std::abs (volumeAt (volume, touchMove + .1) + 5) < .01 && std::abs (volumeAt (volume, (touchMove + touchEnd) / 2) + 5) < .01
               && std::abs (volumeAt (volume, touchEnd + .2) + 20) < .01 && std::abs (volumeAt (volume, 60) + 20) < .01,
               "the touch pass records -10 then -5 dB, ramping between them, and the -20 dB lane resumes after release");
        check (volume.points.front().beat == 0 && volume.points.back().beat == 64, "the lane keeps its earlier and later points");
        const auto& pan = recorded.tracks[0].panAutomation;
        check (pan.enabled && pan.points.size() == 2 && std::abs (pan.points[0].beat - latchStart) < 1e-6 && pan.points[0].value == .5f
               && std::abs (pan.points[1].beat - stopBeat) < .05 && pan.points[1].value == .5f, "the latch pass holds its value until recording stops");
        const auto devices = recorded.tracks[0].plugins.back().automation;
        check (devices.size() == 1 && devices[0].parameter == midDb && devices[0].lane.points.size() == 2 && devices[0].lane.points[0].value == 6,
               "turning a device control records its lane");
        const auto& send = recorded.tracks[0].sends.at (0).automation;
        check (send.enabled && send.points.size() == 2 && std::abs (send.points[0].beat - latchStart) < .05 && std::abs (send.points[0].value + 12) < 1e-3
               && std::abs (send.points[1].value + 12) < 1e-3, "turning a send that was Off makes it and records its lane");
        check (! recorded.recording && ! recorded.automationOverridden && recorded.tracks[0].clips.empty(), "nothing is overridden and no take is created");
        // Undo removes the recorded automation, then the touch pass.
        for (int step = 0; step < 4 && (live.snapshot().tracks[0].panAutomation.points.size() || live.snapshot().tracks[0].volumeAutomation.points.size() != 2); ++step) live.undo();
        const auto undone = live.snapshot();
        check (undone.tracks[0].panAutomation.points.empty() && undone.tracks[0].plugins.back().automation.empty() && undone.tracks[0].volumeAutomation.points.size() == 2
               && std::abs (undone.tracks[0].volumeAutomation.points[0].value + 20) < 1e-3 && undone.tracks[0].sends.at (0).automation.points.empty()
               && undone.tracks[0].sends.at (0).gainDb == -100, "undo restores the lanes before recording, and the send to Off");
        ok (live.setAutomationArm (false), "turn off Automation Arm");
        check (! live.snapshot().automationArm, "Automation Arm is off");
        {
            // A group's controls record as a track's.
            juce::String bus; ok (live.groupTracks ({ trackId }, bus), "group the track");
            ok (live.setAutomationArm (true), "Automation Arm on again");
            pumpSyntheticTestDevice (live, 8);
            ok (live.record(), "record the group's automation");
            pumpSyntheticTestDevice (live, 188);
            const auto groupStart = beatNow (live);
            ok (live.setGroupPan (bus, -.5f), "move the group's pan while recording");
            ok (live.setGroupSendGain (bus, echo, -12), "and turn its send up");
            pumpSyntheticTestDevice (live, 188);
            live.stop();
            const auto grouped = live.snapshot();
            const auto& groupPan = grouped.groups.at (0).panAutomation;
            check (groupPan.enabled && groupPan.points.size() == 2 && std::abs (groupPan.points[0].beat - groupStart) < .05 && groupPan.points[0].value == -.5f,
                   "moving the group's pan records its lane");
            const auto& groupSend = grouped.groups.at (0).sends.at (0).automation;
            check (groupSend.points.size() == 2 && std::abs (groupSend.points[0].value + 12) < 1e-3, "and turning its send records the send's lane");
            ok (live.setAutomationArm (false), "Automation Arm off");
        }
        std::cout << "Automation capture passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL automation capture: " << e.what() << std::endl; return 1; }
}
}
