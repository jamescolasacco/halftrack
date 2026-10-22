#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Output routing through a synthetic device with four outputs: the Output Config's pairs (stereo,
// two mono, switched off), a track playing to Ext. Out (bypassing the master), and the project's
// Master Out, which moves the whole mix and is kept with the project.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeFourOutputSyntheticTestDeviceType();
juce::AudioBuffer<float> captureSyntheticTestDeviceOutputs (Session&, int blocks);
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS output routing: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
void write (const juce::File& f)
{
    juce::AudioBuffer<float> x (1, 48000 * 4);
    for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (0, n, (float) (.5 * std::sin (2 * juce::MathConstants<double>::pi * 440 * n / 48000)));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a four-second tone");
}
// Each output channel's RMS over half a second of playback from 1 s.
std::array<double, 4> levels (Session& s)
{
    s.seek (1); ok (s.play(), "play");
    pumpSyntheticTestDevice (s, blocksPerSecond / 4);
    const auto x = captureSyntheticTestDeviceOutputs (s, blocksPerSecond / 2);
    s.stop(); pumpSyntheticTestDevice (s, 8);
    std::array<double, 4> rms {};
    for (int c = 0; c < std::min (4, x.getNumChannels()); ++c) rms[(size_t) c] = x.getRMSLevel (c, 0, x.getNumSamples());
    return rms;
}
juce::String describe (const std::array<double, 4>& r)
{ return juce::String (r[0], 3) + " " + juce::String (r[1], 3) + " " + juce::String (r[2], 3) + " " + juce::String (r[3], 3); }
}

int runOutputRoutingTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("output-routing-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto tone = root.getChildFile ("tone.wav"); write (tone);
        juce::String thirdOut, fourthOut;
        {
        Session s (false, makeFourOutputSyntheticTestDeviceType());
        pumpSyntheticTestDevice (s, 8);
        auto groups = s.audioOutputGroups();
        check (groups.size() == 2 && groups[0].firstChannel == 0 && groups[0].channels == 2 && groups[1].firstChannel == 2 && groups[1].channels == 2 && groups[1].enabled,
               "the device's four outputs are two stereo pairs");
        const auto mainOut = groups[0].name, otherOut = groups[1].name;
        check (s.masterOutput() == mainOut && s.outputNames() == juce::StringArray { mainOut, otherOut }, "the master plays to the first pair, " + mainOut);
        ok (s.importAudio (tone, 0, 0), "a tone on track 1");
        const auto trackId = s.snapshot().tracks[0].id;
        auto r = levels (s);
        check (r[0] > .1 && r[1] > .1 && r[2] < 1e-6 && r[3] < 1e-6, "it plays through the master on outputs 1 and 2 (" + describe (r) + ")");

        // Ext. Out.
        ok (s.setTrackOutput (trackId, { Session::OutputKind::external, {}, otherOut }), "send the track to Ext. Out " + otherOut);
        check (s.snapshot().tracks[0].output == Session::TrackOutput { Session::OutputKind::external, {}, otherOut }, "its output reads Ext. Out " + otherOut);
        ok (s.setMasterVolumeAutomation ({}), "no master automation");
        r = levels (s);
        check (r[0] < 1e-6 && r[1] < 1e-6 && r[2] > .1 && r[3] > .1, "it now plays on outputs 3 and 4 and not through the master (" + describe (r) + ")");
        s.undo();
        check (s.snapshot().tracks[0].output.kind == Session::OutputKind::master, "one undo sends it back to Master");
        check (s.setTrackOutput (trackId, { Session::OutputKind::external, {}, mainOut }).failed() && s.setTrackOutput (trackId, { Session::OutputKind::external, {}, "missing" }).failed(),
               "the Master Out and missing outputs are refused for Ext. Out");
        s.redo();

        // Master Out.
        check (s.setMasterOutput (otherOut).failed(), "the Master Out cannot move to an output a track plays to directly");
        ok (s.setTrackOutput (trackId, { Session::OutputKind::master }), "send the track to Master again");
        ok (s.setMasterOutput (otherOut), "move the Master Out to " + otherOut);
        r = levels (s);
        check (s.masterOutput() == otherOut && r[0] < 1e-6 && r[2] > .1 && r[3] > .1, "the whole mix plays on outputs 3 and 4 (" + describe (r) + ")");
        check (s.setAudioOutputEnabled (otherOut, false).failed(), "the Master Out cannot be switched off");
        const auto project = root.getChildFile ("Routing").getChildFile ("Routing.halftrack");
        ok (s.saveProject (project), "save");
        ok (s.setMasterOutput (mainOut), "move it back");
        ok (s.loadProject (project), "reopen the saved project");
        check (s.masterOutput() == otherOut, "the Master Out is kept with the project");
        ok (s.setMasterOutput (mainOut), "move it back again");

        // Output Config.
        ok (s.setAudioOutputPairStereo (2, false), "make outputs 3 and 4 two mono outputs");
        groups = s.audioOutputGroups();
        check (groups.size() == 3 && groups[1].channels == 1 && groups[2].channels == 1 && groups[1].firstChannel == 2 && groups[2].firstChannel == 3, "the pair is two mono outputs");
        ok (s.setTrackOutput (trackId, { Session::OutputKind::external, {}, groups[1].name }), "send the track to " + groups[1].name);
        r = levels (s);
        check (r[2] > .1 && r[3] < 1e-6 && r[0] < 1e-6, "a mono output plays on its one channel (" + describe (r) + ")");
        ok (s.setTrackOutput (trackId, { Session::OutputKind::master }), "back to Master");
        ok (s.setAudioOutputEnabled (groups[2].name, false), "switch output 4 off");
        check (! s.outputNames().contains (groups[2].name) && s.setTrackOutput (trackId, { Session::OutputKind::external, {}, groups[2].name }).failed(),
               "an output switched off is not offered");
        check (s.setAudioOutputPairStereo (1, true).failed() && s.setAudioOutputPairStereo (8, true).failed(), "odd and missing channels are refused");
        thirdOut = groups[1].name; fourthOut = groups[2].name;
        }
        {
            Session next (false, makeFourOutputSyntheticTestDeviceType());
            const auto kept = next.audioOutputGroups();
            check (kept.size() == 3 && kept[1].channels == 1 && ! kept[2].enabled, "the Output Config is kept with the device settings");
            ok (next.setAudioOutputEnabled (fourthOut, true), "switch output 4 on");
            ok (next.setAudioOutputPairStereo (2, true), "and pair outputs 3 and 4 again");
            check (next.audioOutputGroups().size() == 2, "they are one stereo output again");
        }
        {
            // The cue, after the reference's (static reading of its code): Cue Out, the cue volume,
            // and the Solo/Cue switch, whose cue hears a track before its fader, pan and activator.
            Session s (false, makeFourOutputSyntheticTestDeviceType());
            pumpSyntheticTestDevice (s, 8);
            const auto groups = s.audioOutputGroups();
            const auto mainOut = groups[0].name, otherOut = groups[1].name;
            auto c = s.snapshot();
            check (c.cueOutput.isEmpty() && c.cueVolumeDb == -6 && ! c.cueModeChosen && ! c.cueAvailable && ! c.cueMode,
                   "at first the cue plays with the master, at -6 dB, in Solo mode");
            check (s.setCueMode (true).failed(), "Cue mode needs a Cue Out apart from the Master Out");
            ok (s.importAudio (tone, 0, 0), "a tone on track 1");
            ok (s.setTrackGain (0, -12), "its fader at -12 dB"); ok (s.setTrackPan (0, -1), "panned hard left");
            const auto plain = levels (s);
            check (plain[0] > .01 && plain[2] < 1e-6 && plain[3] < 1e-6, "it plays on the master's outputs (" + describe (plain) + ")");
            ok (s.setCueOutput (otherOut), "Cue Out to " + otherOut);
            ok (s.setCueVolume (0), "the cue volume at unity");
            ok (s.setCueMode (true), "Cue mode");
            c = s.snapshot();
            check (c.cueOutput == otherOut && c.cueModeChosen && c.cueAvailable && c.cueMode, "Cue mode applies with Cue Out apart from the Master Out");
            ok (s.setTrackSolo (0, true), "cue the track with its switch");
            auto r = levels (s);
            const double full = .5 / std::sqrt (2.0); // The tone's own RMS.
            const auto near = [] (double a, double b) { return std::abs (a - b) < 1e-3 * std::max (1.0, std::abs (b)) + 1e-6; };
            check (s.snapshot().tracks[0].solo && near (r[0], plain[0]) && near (r[1], plain[1]) && near (r[2], full) && near (r[3], full),
                   "it plays on to the master as before, and on Cue Out from before its fader and pan, at unity (" + describe (r) + ")");
            ok (s.setTrackMute (0, true), "switch its activator off");
            r = levels (s);
            check (r[0] < 1e-6 && r[1] < 1e-6 && near (r[2], full) && near (r[3], full), "Cue Out still hears it, the master does not (" + describe (r) + ")");
            ok (s.setTrackMute (0, false), "its activator on");
            ok (s.setCueVolume (-6), "the cue volume at -6 dB");
            r = levels (s);
            const auto half = full * std::pow (10.0, -6 / 20.0);
            check (near (r[2], half) && near (r[3], half) && near (r[0], plain[0]), "the cue volume sets Cue Out's level and not the master's (" + describe (r) + ")");
            s.undo(); r = levels (s);
            check (s.snapshot().cueVolumeDb == 0 && near (r[2], full), "the cue volume is one undo step");

            // The switches keep their state between the modes.
            ok (s.setCueMode (false), "Solo mode");
            r = levels (s);
            check (! s.snapshot().cueMode && s.snapshot().tracks[0].solo && r[2] < 1e-6 && r[3] < 1e-6 && near (r[0], plain[0]),
                   "in Solo mode the switch solos the track, and Cue Out is silent (" + describe (r) + ")");
            ok (s.setCueMode (true), "Cue mode again");
            check (s.snapshot().tracks[0].solo && near (levels (s)[2], full), "and the switch cues it again");
            ok (s.setMasterOutput (otherOut), "move the Master Out onto Cue Out");
            c = s.snapshot();
            check (c.cueModeChosen && ! c.cueAvailable && ! c.cueMode && c.tracks[0].solo, "Cue mode yields to Solo while they share outputs, keeping the choice and the switch");
            ok (s.setMasterOutput (mainOut), "move the Master Out back");
            check (s.snapshot().cueMode, "and Cue mode returns");
            ok (s.setTrackSolo (0, false), "stop cueing the track");
            juce::String group;
            ok (s.groupTracks ({ s.snapshot().tracks[1].id }, group), "group the second track");
            check (s.setGroupSolo (group, true).failed() && ! s.snapshot().groups[0].solo, "groups cannot be cued");
            s.undo();

            // The click and previews play on Cue Out, at the cue volume, in either mode.
            ok (s.setTrackMute (0, true), "silence the track");
            auto metronome = s.snapshot().metronome; metronome.enabled = true; metronome.gain = 1;
            ok (s.setMetronome (metronome), "the metronome on");
            r = levels (s);
            check (r[0] < 1e-6 && r[1] < 1e-6 && r[2] > 1e-3 && r[3] > 1e-3, "the click plays on Cue Out (" + describe (r) + ")");
            ok (s.setCueMode (false), "Solo mode");
            r = levels (s);
            check (r[0] < 1e-6 && r[2] > 1e-3, "and does in Solo mode too (" + describe (r) + ")");
            metronome.enabled = false; ok (s.setMetronome (metronome), "the metronome off");
            ok (s.previewFile (tone), "preview the tone");
            pumpSyntheticTestDevice (s, blocksPerSecond / 4);
            auto x = captureSyntheticTestDeviceOutputs (s, blocksPerSecond / 4);
            s.stopPreview();
            check (x.getRMSLevel (0, 0, x.getNumSamples()) < 1e-6 && near (x.getRMSLevel (2, 0, x.getNumSamples()), full) && near (x.getRMSLevel (3, 0, x.getNumSamples()), full),
                   "a preview plays on Cue Out at the cue volume");

            // Kept with the project.
            const auto project = root.getChildFile ("Cue").getChildFile ("Cue.halftrack");
            ok (s.setCueMode (true), "Cue mode before saving");
            ok (s.saveProject (project), "save");
            ok (s.newProject(), "a new project");
            c = s.snapshot();
            check (c.cueOutput.isEmpty() && c.cueVolumeDb == -6 && ! c.cueModeChosen, "a new project starts with the cue's defaults");
            ok (s.loadProject (project), "reopen the saved one");
            c = s.snapshot();
            check (c.cueOutput == otherOut && c.cueVolumeDb == 0 && c.cueMode, "Cue Out, the cue volume and Cue mode are kept with the project");
        }
        root.deleteRecursively();
        std::cout << "Output routing passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL output routing: " << e.what() << std::endl; return 1; }
}
}
