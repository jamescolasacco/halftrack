#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Plug-in presets, with the controlled VST3 gain plug-in. A preset keeps the plug-in's identity
// and saved state; loading it adds the plug-in with that state in one undo step, so a track playing
// a constant 0.5 renders at the preset's quarter gain.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS plug-in presets: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
float level (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr, "read the render");
    juce::AudioBuffer<float> x (2, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x.getSample (0, 24000);
}
}

int runPluginPresetTests (const juce::File& artifacts)
{
    try
    {
        const auto* pluginPath = std::getenv ("HALFTRACK_TEST_PLUGIN");
        check (pluginPath != nullptr, "the controlled VST3 plug-in is configured");
        const auto root = artifacts.getChildFile ("plugin-presets-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("half.wav");
        {
            juce::AudioBuffer<float> x (2, 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, .5f);
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a constant 0.5 signal");
        }
        Session s (false);
        juce::OwnedArray<juce::PluginDescription> descriptions;
        for (auto* format : s.pluginFormats().getFormats())
            if (format->getName() == "VST3") format->findAllTypesForFile (descriptions, juce::File (juce::String (pluginPath)).getFullPathName());
        check (! descriptions.isEmpty(), "scan the controlled VST3 plug-in");
        ok (s.importAudio (source, 0, 0), "put the signal on track 1");
        const auto out = root.getChildFile ("out.wav");
        ok (s.addPlugin (0, *descriptions[0]), "add the gain plug-in");
        ok (s.setPluginParameter (0, 0, 0, .25f), "turn it to a quarter");
        check (std::abs (level (s, out) - .125f) < 1e-4f, "the track renders at a quarter");
        const auto pluginId = s.snapshot().tracks[0].plugins[0].id;

        const auto file = root.getChildFile ("Presets").getChildFile (descriptions[0]->name).getChildFile (juce::String ("Quarter") + Session::presetExtension);
        ok (s.saveDevicePreset (pluginId, file, "Quarter"), "save it as a preset");
        Session::DevicePreset preset;
        ok (Session::readDevicePreset (file, preset), "read the preset");
        check (preset.plugin && preset.name == "Quarter" && preset.description.name == descriptions[0]->name && preset.state.getSize() > 0,
               "the preset names the plug-in and keeps its state");
        check (s.saveDevicePreset (pluginId, file, "Quarter").failed(), "saving over it needs the overwrite choice");

        ok (s.removePlugin (0, 0), "remove the plug-in");
        check (std::abs (level (s, out) - .5f) < 1e-4f, "the track renders dry");
        juce::String loaded;
        ok (s.loadDevicePreset (0, file, loaded), "load the preset");
        check (loaded.isNotEmpty() && s.snapshot().tracks[0].plugins.size() == 1 && s.snapshot().tracks[0].plugins[0].id == loaded, "it adds the plug-in");
        check (std::abs (level (s, out) - .125f) < 1e-4f, "with its saved quarter gain");
        s.undo();
        check (s.snapshot().tracks[0].plugins.empty() && std::abs (level (s, out) - .5f) < 1e-4f, "one undo removes it");
        const auto bad = root.getChildFile (juce::String ("Bad") + Session::presetExtension);
        check (bad.replaceWithText ("<HALFTRACKPRESET version=\"1\" name=\"Bad\" device=\"plugin\"><SETTINGS/></HALFTRACKPRESET>"), "write a plug-in preset without its plug-in");
        check (Session::readDevicePreset (bad, preset).failed() && s.loadDevicePreset (0, bad, loaded).failed() && s.snapshot().tracks[0].plugins.empty(),
               "it is refused, changing nothing");
        std::cout << "Plug-in presets passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL plug-in presets: " << e.what() << std::endl; return 1; }
}
}
