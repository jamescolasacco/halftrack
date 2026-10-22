#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Plug-in sidechains and bus layouts, through the VST3 sidechain probe (see TestSidechainPlugin.cpp),
// whose output carries its main input's left channel on the left and its sidechain's left channel
// on the right. Track A plays a constant 0.5 Sends Only, so it is heard only through the sidechain;
// track B plays 0.25 through the probe. Renders and the synthetic device show which of A's taps
// reaches the sidechain, as each setting, undo, mute, removal, reload and duplicate changes it.
namespace halftrack
{
void useHardClipEdges();
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS sidechain: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
bool near (float a, float b, float tolerance = 1e-3f) { return std::abs (a - b) < tolerance; }
juce::File constant (const juce::File& file, float value)
{
    juce::AudioBuffer<float> x (2, 2 * 48000);
    for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, value);
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a constant " + juce::String (value) + " signal");
    return file;
}
juce::AudioBuffer<float> read (const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    juce::AudioBuffer<float> x (2, reader ? (int) reader->lengthInSamples : 0);
    if (reader) reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
// Left and right one second into the clips, in an offline render.
std::pair<float, float> rendered (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render " + file.getFileName());
    const auto x = read (file);
    check (x.getNumSamples() > 72000, "the render holds the clips");
    return { x.getSample (0, 72000), x.getSample (1, 72000) };
}
// Left and right once live playback has settled.
std::pair<float, float> heard (Session& s)
{
    pumpSyntheticTestDevice (s, 8);
    s.seek (1); ok (s.play(), "play");
    const auto out = captureSyntheticTestDevice (s, 40);
    s.stop(); pumpSyntheticTestDevice (s, 8);
    return { out.getSample (0, out.getNumSamples() - 1), out.getSample (1, out.getNumSamples() - 1) };
}
int firstAbove (const juce::AudioBuffer<float>& x, int channel)
{
    for (int n = 0; n < x.getNumSamples(); ++n) if (std::abs (x.getSample (channel, n)) > .1f) return n;
    return -1;
}
juce::PluginDescription scan (Session& s, const char* variable)
{
    const auto* path = std::getenv (variable);
    check (path != nullptr, juce::String (variable) + " names a VST3 fixture");
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : s.pluginFormats().getFormats())
        if (format->getName() == "VST3") format->findAllTypesForFile (found, juce::File (juce::String::fromUTF8 (path)).getFullPathName());
    check (! found.isEmpty(), juce::String (variable) + " scans");
    s.knownPlugins().addType (*found[0]);
    return *found[0];
}
}

int runSidechainTests (const juce::File& artifacts)
{
    try
    {
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
        const auto root = artifacts.getChildFile ("sidechain-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto half = constant (root.getChildFile ("half.wav"), .5f), quarter = constant (root.getChildFile ("quarter.wav"), .25f);
        Session s (false, makeSyntheticTestDeviceType());
        const auto probe = scan (s, "HALFTRACK_TEST_SIDECHAIN_PLUGIN");
        const auto latency = scan (s, "HALFTRACK_TEST_LATENCY_PLUGIN");
        ok (s.saveProject (root.getChildFile ("Sidechain").getChildFile ("Sidechain.halftrack")), "save the project");
        ok (s.importAudio (half, 0, .5), "A plays 0.5");
        ok (s.importAudio (quarter, 1, .5), "B plays 0.25");
        const auto a = s.snapshot().tracks[0].id, b = s.snapshot().tracks[1].id;
        ok (s.setTrackOutput (a, { Session::OutputKind::sendsOnly, {} }), "A plays Sends Only");

        // Bus layouts.
        ok (s.addPlugin (1, probe), "insert the probe on B");
        const auto device = [&] { return s.snapshot().tracks[1].plugins.back(); };
        const auto id = device().id;
        check (device().inputBuses == std::vector<int> { 2, 2 } && device().outputBuses == std::vector<int> { 2, 2 }, "the probe reports a stereo main bus, a stereo sidechain and a stereo auxiliary output");
        check (device().canSidechain && device().sidechain == Session::SidechainSettings {}, "it can take a sidechain, which starts off, with no source, at Post FX");
        ok (s.addPlugin (0, latency), "insert the latency fixture on A");
        const auto latencyDevice = s.snapshot().tracks[0].plugins.back();
        check (latencyDevice.inputBuses == std::vector<int> { 2 } && ! latencyDevice.canSidechain, "a stereo effect has no sidechain");
        check (s.setPluginSidechain (latencyDevice.id, { true, b }).failed(), "and refuses one");
        ok (s.removePlugin (0, 0), "remove it again");
        auto [left, right] = rendered (s, root.getChildFile ("none.wav"));
        check (near (left, .25f) && near (right, 0), "without a sidechain, B's main input reaches the probe and its sidechain hears silence");

        // Choosing a source.
        const auto sources = s.sidechainSources (id);
        check (sources.contains (a) && ! sources.contains (b), "A is offered as a source and B, the probe's own track, is not");
        for (const auto& track : s.snapshot().tracks) if (track.isReturn) check (! sources.contains (track.id), "returns are not offered");
        check (s.setPluginSidechain (id, { true, b }).failed() && s.setPluginSidechain (id, { true, "missing" }).failed(), "its own track and unknown tracks are refused");
        ok (s.setPluginSidechain (id, { true, a }), "the probe takes its sidechain from A");
        check (device().sidechain == Session::SidechainSettings { true, a, Session::InputTap::postFX }, "the probe shows A, Post FX");
        std::tie (left, right) = rendered (s, root.getChildFile ("post-fx.wav"));
        check (near (left, .25f) && near (right, .5f), "A reaches the sidechain, and B's main input is unchanged");
        std::tie (left, right) = heard (s);
        check (near (left, .25f) && near (right, .5f), "live too");
        s.undo();
        check (device().sidechain == Session::SidechainSettings {}, "undo removes the sidechain in one step");
        std::tie (left, right) = heard (s);
        check (near (right, 0), "and the sidechain is silent again");
        s.redo();
        check (device().sidechain.sourceTrackId == a, "redo brings it back");

        // Taps, with a -6 dB Utility on A before its -6 dB mixer.
        ok (s.addUtility (0), "add a Utility to A");
        check (s.snapshot().tracks[0].plugins.size() == 1 && s.snapshot().tracks[0].plugins[0].isUtility, "A holds the Utility");
        auto utility = s.snapshot().tracks[0].plugins[0].utility; utility.gainDb = -6;
        ok (s.setUtilitySettings (s.snapshot().tracks[0].plugins[0].id, utility), "the Utility at -6 dB");
        ok (s.setTrackGain (0, -6), "A's mixer at -6 dB");
        const auto g = std::pow (10.0f, -6 / 20.0f);
        std::tie (left, right) = rendered (s, root.getChildFile ("post-fx-gain.wav"));
        check (near (right, .5f * g), "Post FX hears A's devices but not its mixer");
        ok (s.setPluginSidechain (id, { true, a, Session::InputTap::preFX }), "tap A Pre FX");
        std::tie (left, right) = rendered (s, root.getChildFile ("pre-fx.wav"));
        check (near (right, .5f), "Pre FX hears neither");
        ok (s.setPluginSidechain (id, { true, a, Session::InputTap::postMixer }), "tap A Post Mixer");
        std::tie (left, right) = rendered (s, root.getChildFile ("post-mixer.wav"));
        check (near (right, .5f * g * g), "Post Mixer hears both");
        std::tie (left, right) = heard (s);
        check (near (right, .5f * g * g), "live too");
        s.undo();
        check (device().sidechain.tap == Session::InputTap::preFX, "undo returns to Pre FX in one step");
        std::tie (left, right) = heard (s);
        check (near (right, .5f), "and playback follows the undo");
        ok (s.setPluginSidechain (id, { true, a, Session::InputTap::postMixer }), "Post Mixer again");
        ok (s.setTrackMute (0, true), "mute A");
        std::tie (left, right) = rendered (s, root.getChildFile ("muted-post-mixer.wav"));
        check (near (right, 0), "Post Mixer follows A's muting");
        ok (s.setPluginSidechain (id, { true, a, Session::InputTap::postFX }), "Post FX again");
        std::tie (left, right) = rendered (s, root.getChildFile ("muted-post-fx.wav"));
        check (near (right, .5f * g), "Post FX does not");
        ok (s.setTrackMute (0, false), "unmute A");

        // Switching it off keeps the source.
        ok (s.setPluginSidechain (id, { false, a }), "switch the sidechain off");
        check (device().sidechain == Session::SidechainSettings { false, a }, "it keeps A as its source");
        std::tie (left, right) = rendered (s, root.getChildFile ("off.wav"));
        check (near (right, 0), "and hears nothing");
        std::tie (left, right) = heard (s);
        check (near (right, 0), "live too");
        ok (s.setPluginSidechain (id, { true, a }), "switch it on");
        std::tie (left, right) = heard (s);
        check (near (right, .5f * g), "and A is heard again");

        // Loops are refused.
        check (s.inputSources (a).isEmpty() || ! s.inputSources (a).contains (b), "A cannot take B's audio, which A's audio reaches through the sidechain");
        check (s.setTrackInputTrack (a, b).failed(), "so choosing it is refused");
        check (s.setTrackOutput (b, { Session::OutputKind::track, a }).failed(), "and B cannot play into A");
        ok (s.addPlugin (0, probe), "insert a second probe on A");
        // Hosted effects go to the front of an audio track's chain; find each on A by name.
        const auto onA = [&] (const juce::String& name)
        { const auto plugins = s.snapshot().tracks[0].plugins; for (size_t i = 0; i < plugins.size(); ++i) if (plugins[i].name == name) return std::pair { (int) i, plugins[i].id }; return std::pair { -1, juce::String() }; };
        const auto second = onA (probe.name).second;
        check (! s.sidechainSources (second).contains (b) && s.setPluginSidechain (second, { true, b }).failed(), "A's probe cannot take its sidechain from B");
        ok (s.removePlugin (0, onA (probe.name).first), "remove the second probe");

        // Delay compensation: A's 127-sample plug-in delays its audio, and the probe still hears it in step with B.
        ok (s.addPlugin (0, latency), "delay A by 127 samples");
        ok (s.exportWav (root.getChildFile ("latency.wav"), 48000, 32, 0), "render with the delay");
        {
            const auto x = read (root.getChildFile ("latency.wav"));
            const int mainStart = firstAbove (x, 0), sideStart = firstAbove (x, 1);
            check (mainStart > 0 && mainStart == sideStart, "B's audio and A's sidechain start on the same sample (" + juce::String (mainStart) + ", " + juce::String (sideStart) + ")");
        }
        ok (s.removePlugin (0, onA (latency.name).first), "remove the delay");
        {
            check (s.snapshot().tracks[0].plugins.size() == 1 && s.snapshot().tracks[0].plugins[0].isUtility, "A keeps only its Utility");
        }

        // Stems: B's stem, rendered with B soloed, still hears A Post FX.
        {
            juce::StringArray written;
            Session::AudioExportSettings settings; settings.bitDepth = 32;
            const auto folder = root.getChildFile ("stems");
            ok (s.exportStems (folder, settings, written), "export stems");
            juce::File stem;
            for (const auto& path : written) if (juce::File (path).getFileNameWithoutExtension().endsWith (s.snapshot().tracks[1].name)) stem = juce::File (path);
            const auto x = read (stem);
            check (x.getNumSamples() > 72000 && near (x.getSample (1, 72000), .5f * g), "B's stem holds the sidechain");
        }

        // Freezing B renders the sidechain into its freeze.
        ok (s.setTrackFrozen (b, true), "freeze B");
        std::tie (left, right) = rendered (s, root.getChildFile ("frozen.wav"));
        check (s.snapshot().tracks[1].frozen && near (left, .25f) && near (right, .5f * g), "frozen B plays the sidechain it heard");
        s.undo();
        // Duplicating B keeps the copy's sidechain on A.
        juce::String copy;
        ok (s.duplicateTrack (b, copy), "duplicate B");
        for (const auto& track : s.snapshot().tracks)
            if (track.id == copy) check (! track.plugins.empty() && track.plugins.back().sidechain == Session::SidechainSettings { true, a }, "the copy's probe takes A too");
        s.undo();

        // Saving and reopening keeps it; a project naming a missing source is refused.
        ok (s.setPluginSidechain (id, { true, a, Session::InputTap::postMixer }), "Post Mixer before saving");
        const auto project = s.snapshot().projectFile;
        ok (s.saveProject (project), "save");
        ok (s.newProject(), "close it");
        ok (s.loadProject (project), "reopen it");
        const auto reopened = [&] { for (const auto& track : s.snapshot().tracks) for (const auto& p : track.plugins) if (p.canSidechain) return p; return Session::PluginInfo {}; }();
        check (reopened.sidechain.on && reopened.sidechain.tap == Session::InputTap::postMixer && reopened.sidechain.sourceTrackId == s.snapshot().tracks[0].id,
               "the sidechain reopens with its source and tap");
        std::tie (left, right) = rendered (s, root.getChildFile ("reopened.wav"));
        check (near (right, .5f * g * g), "and plays as before");
        {
            auto text = project.loadFileAsString();
            const auto at = text.indexOf ("sidechainSourceID=\"");
            check (at > 0, "the project names the source");
            const auto end = text.indexOf (at + 19, "\"");
            text = text.substring (0, at + 19) + "987654321" + text.substring (end);
            const auto tampered = project.getSiblingFile ("Tampered.halftrack");
            check (tampered.replaceWithText (text), "write a project naming a missing source");
            check (s.loadProject (tampered).failed() && s.snapshot().projectFile == project, "it is refused and the open project stays");
        }

        // Removing the source leaves the sidechain without one.
        ok (s.removeTrack (0), "remove A");
        const auto afterRemoval = [&] { for (const auto& track : s.snapshot().tracks) for (const auto& p : track.plugins) if (p.canSidechain) return p.sidechain; return Session::SidechainSettings {}; };
        check (afterRemoval().on && afterRemoval().sourceTrackId.isEmpty(), "the probe keeps its sidechain on, with no source");
        s.undo();
        check (afterRemoval().sourceTrackId == s.snapshot().tracks[0].id, "undo restores A as its source");
        std::cout << "Sidechain passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL sidechain: " << e.what() << std::endl; return 1; }
}
}
