#include "NativeEffects.h"
#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Arrangement automation of native device controls. A Sampler loops a constant
// 0.5 source for eight beats at 120 BPM; its automated Volume control must scale
// the render by the automated level while the stored manual value is unchanged.
namespace halftrack
{
namespace
{
using Kind = Session::EffectKind;
using Lane = Session::AutomationLane;
constexpr double rate = 48000;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS device automation: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
juce::AudioBuffer<float> read (const juce::File& f)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    check (r != nullptr, "read render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
double gainDb (double db) { return std::pow (10.0, db / 20.0); }
// Largest relative error of the left channel against 0.5 times an expected gain over [start, end).
double gainError (const juce::AudioBuffer<float>& a, const std::function<double (int)>& gain, int start, int end)
{
    double worst = 0;
    for (int n = start; n < end; ++n) worst = std::max (worst, std::abs (a.getSample (0, n) / (.5 * gain (n)) - 1.0));
    return worst;
}
Session::PluginInfo instrument (Session& s) { const auto snapshot = s.snapshot(); return snapshot.tracks.back().instrument; }
}

int runDeviceAutomationTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("device-automation-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("dc.wav");
        {
            juce::AudioBuffer<float> a (2, 48000); for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (a.getWritePointer (c), .5f, a.getNumSamples());
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (a, 0, a.getNumSamples()), "write a constant source");
        }
        Session s (false);
        ok (s.addMidiTrack ("Automated"), "add MIDI track");
        const auto trackId = s.snapshot().tracks.back().id;
        ok (s.addMidiClip (trackId, 0, 8), "add clip");
        { const auto snapshot = s.snapshot(); ok (s.addMidiNote (snapshot.tracks.back().clips[0].id, 60, 0, 8, 1), "hold C3 for eight beats"); }
        ok (s.setInstrumentSample (trackId, source), "load the constant source into a Sampler");
        auto settings = instrument (s).effect;
        settings.values[instruments::sampler::loop] = 1; settings.values[instruments::sampler::velocityAmp] = 0; settings.values[instruments::sampler::ampAttack] = 0;
        settings.values[instruments::sampler::ampSustain] = 1;
        ok (s.setNativeEffectSettings (instrument (s).id, settings), "loop the source at unity");
        const auto id = instrument (s).id;
        const int volume = (int) instruments::sampler::volume;
        const auto render = root.getChildFile ("render.wav");
        const int beats8 = 8 * 24000, settle = 2400;
        check (s.setDeviceAutomation ("missing", volume, {}).failed() && s.setDeviceAutomation (id, 99, {}).failed(), "unknown devices and controls are rejected");
        check (s.setDeviceAutomation (id, volume, { true, { { 0, -40 } } }).failed() && s.setDeviceAutomation (id, volume, { true, { { 2, 0 }, { 1, 0 } } }).failed()
               && instrument (s).automation.empty(), "out-of-range values and unordered points are rejected without a lane");
        ok (s.setDeviceAutomation (id, volume, { true, { { 0, -6 } } }), "automate the Sampler volume at -6 dB");
        auto info = instrument (s);
        check (info.automation.size() == 1 && info.automation[0].parameter == volume && std::abs (info.automation[0].lane.points[0].value + 6) < 1e-4
               && info.effect.values[instruments::sampler::volume] == 0, "the lane is reported in dB and the manual Volume stays at 0 dB");
        ok (s.exportWav (render, rate, 32, 0), "render the automated level");
        check (gainError (read (render), [] (int) { return gainDb (-6); }, settle, beats8 - settle) < 1e-4, "the render follows the automated -6 dB, not the manual 0 dB");
        ok (s.setDeviceAutomation (id, volume, { false, { { 0, -6 } } }), "switch the lane's Read off");
        ok (s.exportWav (render, rate, 32, 0), "render with Read off");
        check (gainError (read (render), [] (int) { return 1.0; }, settle, beats8 - settle) < 1e-5, "with Read off the manual 0 dB plays");
        // A ramp from 0 dB at beat 0 to -36 dB at beat 8, interpolated along the Volume knob's scale.
        ok (s.setDeviceAutomation (id, volume, { true, { { 0, 0 }, { 8, -36 } } }), "ramp the volume down over eight beats");
        ok (s.exportWav (render, rate, 32, 0), "render the ramp");
        const auto p0 = Session::deviceAutomationValueToProportion (Kind::sampler, volume, 0), p1 = Session::deviceAutomationValueToProportion (Kind::sampler, volume, -36);
        const auto ramp = [&] (int n) { return gainDb (Session::deviceAutomationProportionToValue (Kind::sampler, volume, (float) (p0 + (p1 - p0) * n / (double) beats8))); };
        const auto ramped = read (render);
        check (gainError (ramped, ramp, settle, beats8 / 2) < .02, "the first half of the ramp follows the knob-scale interpolation within one audio block");
        check (ramped.getSample (0, beats8 / 4) > ramped.getSample (0, beats8 / 2) && ramped.getSample (0, beats8 / 2) > ramped.getSample (0, 3 * beats8 / 4),
               "the level falls throughout the ramp");
        s.undo(); check (instrument (s).automation[0].lane.enabled == false, "undo restores the switched-off lane");
        s.redo(); check (instrument (s).automation[0].lane.points.size() == 2, "redo restores the ramp");
        s.setTempo (60);
        check (std::abs (instrument (s).automation[0].lane.points[1].beat - 8) < 1e-6, "automation points keep their beats when the tempo changes");
        s.setTempo (120);
        const auto project = root.getChildFile ("Automated").getChildFile ("Automated.halftrack");
        ok (project.getParentDirectory().createDirectory(), "create project folder");
        ok (s.saveProject (project), "save the automated project"); ok (s.loadProject (project), "reopen the automated project");
        info = instrument (s);
        check (info.automation.size() == 1 && info.automation[0].lane.points.size() == 2 && std::abs (info.automation[0].lane.points[1].value + 36) < 1e-3,
               "the device lane reopens");
        ok (s.exportWav (render, rate, 32, 0), "render the reopened ramp");
        const auto reopened = read (render);
        double difference = 0; for (int n = 0; n < beats8; ++n) difference = std::max (difference, (double) std::abs (reopened.getSample (0, n) - ramped.getSample (0, n)));
        check (difference < 1e-6, "the reopened ramp renders identically");
        ok (s.setDeviceAutomation (info.id, volume, { true, {} }), "clear the lane");
        ok (s.exportWav (render, rate, 32, 0), "render after clearing");
        check (gainError (read (render), [] (int) { return 1.0; }, settle, beats8 - settle) < 1e-5 && instrument (s).automation.empty(), "clearing the lane returns to the manual setting");
        // A saved knob position outside 0..1 is rejected before loading and keeps the open project.
        s.undo(); ok (s.saveProject (project), "save the ramp again");
        auto xml = juce::XmlDocument::parse (project);
        check (xml != nullptr, "parse the saved project");
        int edited = 0;
        std::function<void (juce::XmlElement&)> corrupt = [&] (juce::XmlElement& e)
        {
            if (e.hasTagName ("AUTOMATIONCURVE") && e.getStringAttribute ("paramID") == "volumeDb")
                if (auto* point = e.getFirstChildElement()) { point->setAttribute ("v", 2.0); ++edited; }
            for (auto* child : e.getChildIterator()) corrupt (*child);
        };
        corrupt (*xml);
        const auto broken = project.getSiblingFile ("Broken.halftrack");
        check (edited == 1 && xml->writeTo (broken), "write a project with an out-of-range automation point");
        const auto loaded = s.loadProject (broken);
        check (loaded.failed() && loaded.getErrorMessage().contains ("device automation") && instrument (s).automation.size() == 1,
               "invalid device automation rejects the load and keeps the open project");
        std::cout << "Device automation passed: " << root.getFullPathName() << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL device automation: " << e.what() << std::endl; return 1; }
}
}
