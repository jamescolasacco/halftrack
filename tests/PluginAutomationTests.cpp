#include "Session.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

// Automation of an external plug-in's parameters, with the VST3 gain fixture. A constant 0.5 input
// makes each output sample half the gain, so a lane ramping Gain from 0 to 1 over four beats is
// read directly from the render. The lane is one undo step, survives reopening and keeps its beats
// through a tempo change; switched off, the manual value applies again.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS plug-in automation: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render " + file.getFileName());
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
float at (const juce::AudioBuffer<float>& x, double seconds) { return x.getSample (0, (int) std::lround (seconds * 48000)); }
}

int runPluginAutomationTests (const juce::File& artifacts)
{
    try
    {
        const auto* pluginPath = std::getenv ("HALFTRACK_TEST_PLUGIN");
        check (pluginPath != nullptr, "the VST3 gain fixture is configured");
        const auto root = artifacts.getChildFile ("plugin-automation-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("half.wav");
        {
            juce::AudioBuffer<float> x (2, 4 * 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, .5f);
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a constant 0.5 signal");
        }
        Session s (false);
        ok (s.importAudio (source, 0, 0), "import it");
        juce::OwnedArray<juce::PluginDescription> descriptions;
        for (auto* format : s.pluginFormats().getFormats())
            if (format->getName() == "VST3") format->findAllTypesForFile (descriptions, juce::File (pluginPath).getFullPathName());
        check (! descriptions.isEmpty(), "scan the gain fixture");
        ok (s.addPlugin (0, *descriptions[0]), "insert it");
        const auto id = s.snapshot().tracks[0].plugins.back().id;
        const auto parameters = s.pluginParameters (id);
        // The VST3 wrapper adds a Bypass parameter after the fixture's own Gain.
        check (parameters.size() == 2 && parameters[0].name == "Gain" && std::abs (parameters[0].value - .5f) < 1e-6f && parameters[0].text == "0.50" && parameters[1].name == "Bypass",
               "its parameters are Gain, half way with its own text, and the wrapper's Bypass");
        check (s.pluginParameters ("missing").empty() && s.pluginParameters (s.snapshot().tracks[0].id).empty(), "unknown ids have no parameters");
        const auto steady = render (s, root.getChildFile ("steady.wav"));
        check (std::abs (at (steady, .5) - .25f) < 1e-6f && std::abs (at (steady, 1.5) - .25f) < 1e-6f, "without automation the gain stays at its manual value");

        Session::AutomationLane ramp; ramp.points = { { 0, 0 }, { 4, 1 } };
        check (s.setDeviceAutomation (id, 0, { true, { { 0, 0 }, { 4, 1.5f } } }).failed() && s.setDeviceAutomation (id, 2, ramp).failed()
               && s.setDeviceAutomation ("missing", 0, ramp).failed() && s.snapshot().tracks[0].plugins.back().automation.empty(),
               "values outside 0 to 1, unknown parameters and devices are refused");
        ok (s.setDeviceAutomation (id, 0, ramp), "ramp Gain from 0 to 1 over four beats");
        auto automation = s.snapshot().tracks[0].plugins.back().automation;
        check (automation.size() == 1 && automation[0].parameter == 0 && automation[0].lane.points.size() == 2 && automation[0].lane.points[1].value == 1,
               "the lane is in the snapshot in 0-1 values");
        const auto ramped = render (s, root.getChildFile ("ramped.wav"));
        // 120 BPM: four beats are two seconds; the output is half the gain.
        check (at (ramped, .01) < .01f && std::abs (at (ramped, 1) - .25f) < .01f && std::abs (at (ramped, 1.5) - .375f) < .01f && std::abs (at (ramped, 3) - .5f) < .01f,
               "the render follows the ramp and holds its last value");
        s.undo();
        // The engine leaves a plug-in where its automation last put it; Halftrack returns it to its
        // value set by hand at once, so playback and renders agree whenever the plug-in's state was saved.
        check (std::abs (s.pluginParameters (id)[0].value - .5f) < 1e-6f, "one undo returns Gain to its value set by hand at once");
        check (s.snapshot().tracks[0].plugins.back().automation.empty() && std::abs (at (render (s, root.getChildFile ("undone.wav")), 1) - .25f) < 1e-6f,
               "one undo removes the lane");
        s.redo();
        check (s.snapshot().tracks[0].plugins.back().automation.size() == 1, "redo brings the lane back");

        {
            // Clearing the lane also returns Gain to its value set by hand.
            render (s, root.getChildFile ("ramped-again.wav"));
            ok (s.setDeviceAutomation (id, 0, {}), "clear the lane");
            check (std::abs (s.pluginParameters (id)[0].value - .5f) < 1e-6f && std::abs (at (render (s, root.getChildFile ("cleared.wav")), 1) - .25f) < 1e-6f,
                   "clearing it returns Gain to its value set by hand");
            s.undo();
            check (s.snapshot().tracks[0].plugins.back().automation.size() == 1, "one undo brings the cleared lane back");
        }
        const auto project = root.getChildFile ("Plug-in automation").getChildFile ("Plug-in automation.halftrack");
        ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
        const auto reopenedId = s.snapshot().tracks[0].plugins.back().id;
        automation = s.snapshot().tracks[0].plugins.back().automation;
        check (automation.size() == 1 && automation[0].lane.points.size() == 2 && std::abs (automation[0].lane.points[1].beat - 4) < 1e-9, "the lane reopens");
        const auto reopened = render (s, root.getChildFile ("reopened.wav"));
        bool same = reopened.getNumSamples() == ramped.getNumSamples();
        for (int n = 0; same && n < reopened.getNumSamples(); ++n) same = std::abs (reopened.getSample (0, n) - ramped.getSample (0, n)) < 1e-6f;
        check (same, "and renders as before");

        s.setTempo (60);
        automation = s.snapshot().tracks[0].plugins.back().automation;
        const auto slower = render (s, root.getChildFile ("slower.wav"));
        check (std::abs (automation[0].lane.points[1].beat - 4) < 1e-6 && std::abs (at (slower, 2) - .25f) < .01f,
               "at 60 BPM the lane keeps its beats, so half way is now at two seconds");
        auto off = automation[0].lane; off.enabled = false;
        ok (s.setDeviceAutomation (reopenedId, 0, off), "switch the lane off");
        const auto held = render (s, root.getChildFile ("held.wav"));
        check (std::abs (at (held, .1) - at (held, 3.9)) < 1e-6f && ! s.snapshot().tracks[0].plugins.back().automation[0].lane.enabled,
               "switched off, the parameter holds one value instead of following the lane");
        int position = -1;
        for (size_t p = 0; p < s.snapshot().tracks[0].plugins.size(); ++p) if (s.snapshot().tracks[0].plugins[p].id == reopenedId) position = (int) p;
        ok (s.setPluginParameter (0, position, 0, .25f), "set Gain by hand");
        const auto manual = render (s, root.getChildFile ("manual.wav"));
        check (std::abs (at (manual, .1) - .125f) < 1e-6f && std::abs (at (manual, 3.9) - .125f) < 1e-6f, "and the value set by hand applies");
        std::cout << "Plug-in automation passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL plug-in automation: " << e.what() << std::endl; return 1; }
}
}
