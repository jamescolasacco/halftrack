#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Input configuration through the synthetic device, whose left input plays 440 Hz and right
// 660 Hz. Its stereo pair splits into two mono inputs that two tracks record at once, each take
// one channel of its own signal; the pair joins again, inputs switch off and on, the layout is
// kept for the next Session, and impossible layouts are refused.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS input config: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
// A take's channel count and frequency, from its rising zero crossings after the first tenth.
std::pair<int, double> take (const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr, "read " + file.getFileName());
    juce::AudioBuffer<float> x ((int) reader->numChannels, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    int first = -1, last = -1, crossings = 0;
    for (int n = x.getNumSamples() / 10 + 1; n < x.getNumSamples(); ++n)
        if (x.getSample (0, n - 1) < 0 && x.getSample (0, n) >= 0) { if (first < 0) first = n; last = n; ++crossings; }
    return { (int) reader->numChannels, crossings > 1 ? (crossings - 1) * reader->sampleRate / (last - first) : 0.0 };
}
}

int runInputConfigTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("input-config-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        {
            Session s (false, makeSyntheticTestDeviceType());
            auto groups = s.audioInputGroups();
            check (groups.size() == 1 && groups[0].firstChannel == 0 && groups[0].channels == 2 && groups[0].enabled, "the device's two inputs start as one stereo pair");
            ok (s.saveProject (root.getChildFile ("Inputs").getChildFile ("Inputs.halftrack")), "save a recording destination");
            ok (s.setAudioInputPairStereo (0, false), "split the pair into mono inputs");
            groups = s.audioInputGroups();
            check (groups.size() == 2 && groups[0].firstChannel == 0 && groups[0].channels == 1 && groups[1].firstChannel == 1 && groups[1].channels == 1
                       && groups[0].name != groups[1].name && s.inputNames().contains (groups[0].name) && s.inputNames().contains (groups[1].name),
                   "two mono inputs (" + (groups.size() == 2 ? groups[0].name + ", " + groups[1].name : juce::String()) + ")");

            ok (s.addAudioTrack ("Right"), "add a second audio track");
            ok (s.setTrackInput (0, groups[0].name), "route the left input to track 1"); ok (s.setTrackArmed (0, true), "arm it");
            ok (s.setTrackInput (1, groups[1].name), "route the right input to track 2"); ok (s.setTrackArmed (1, true), "arm it");
            pumpSyntheticTestDevice (s, 8);
            s.seek (0); ok (s.record(), "record both");
            pumpSyntheticTestDevice (s, blocksPerSecond / 2);
            s.stop();
            const auto state = s.snapshot();
            check (state.tracks[0].clips.size() == 1 && state.tracks[1].clips.size() == 1, "each track keeps a take");
            const auto [leftChannels, left] = take (state.tracks[0].clips[0].file);
            const auto [rightChannels, right] = take (state.tracks[1].clips[0].file);
            check (leftChannels == 1 && std::abs (left - 440) < 2 && rightChannels == 1 && std::abs (right - 660) < 2,
                   "each take is one channel of its own input (" + juce::String (left, 1) + " Hz and " + juce::String (right, 1) + " Hz)");

            check (s.setAudioInputPairStereo (1, true).failed(), "a pair starts on an even channel");
            check (s.setAudioInputPairStereo (4, false).failed(), "a channel the device lacks is refused");
            ok (s.setAudioInputEnabled (groups[1].name, false), "switch the right input off");
            check (! s.audioInputGroups()[1].enabled && s.setAudioInputEnabled ("nothing", false).failed(), "it is off; an unknown input is refused");
            ok (s.setAudioInputEnabled (groups[1].name, true), "and back on");
        }
        {
            Session next (false, makeSyntheticTestDeviceType());
            const auto groups = next.audioInputGroups();
            check (groups.size() == 2 && groups[0].channels == 1 && groups[1].enabled, "the next Session opens with the mono inputs");
            ok (next.setAudioInputPairStereo (0, true), "join the pair again");
            check (next.audioInputGroups().size() == 1 && next.audioInputGroups()[0].channels == 2, "one stereo input");
        }
        std::cout << "Input config passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL input config: " << e.what() << std::endl; return 1; }
}
}
