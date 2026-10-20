#include "NativeEffects.h"
#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Audio Effect Racks. A generated stereo tone plays from an audio clip through a
// Utility at -6 dB. Grouping it must keep the sound; further chains run in parallel
// and sum, and chain volume, pan, mute, solo, macros, undo and reopening are measured
// against gains of the unprocessed source.
namespace halftrack
{
namespace
{
constexpr double rate = 48000, pi = 3.14159265358979323846;
constexpr int frames = 96000, settle = 4096;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS rack: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
double left (int n) { return .25 * std::sin (2 * pi * 220 * n / rate); }
double right (int n) { return .2 * std::cos (2 * pi * 330 * n / rate); }
double db (double decibels) { return std::pow (10.0, decibels / 20.0); }
juce::AudioBuffer<float> read (const juce::File& f)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    check (r != nullptr, "read render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
// Largest error against the source scaled by a gain per channel, after the mixers settle.
double error (const juce::AudioBuffer<float>& x, double gainL, double gainR)
{
    double worst = 0;
    for (int n = settle; n < frames - 64; ++n)
        worst = std::max ({ worst, std::abs (x.getSample (0, n) - gainL * left (n)), std::abs (x.getSample (1, n) - gainR * right (n)) });
    return worst;
}
juce::AudioBuffer<float> render (Session& s, const juce::File& file) { ok (s.exportWav (file, rate, 32, 0), "render " + file.getFileName()); return read (file); }
Session::PluginInfo rack (Session& s) { const auto snapshot = s.snapshot(); for (const auto& p : snapshot.tracks[0].plugins) if (p.isRack) return p; return {}; }
// Saved projects wrap the edit in a versioned HALFTRACK element.
juce::XmlElement* racks (juce::XmlElement& project) { auto* edit = project.getChildByName ("EDIT"); return edit ? edit->getChildByName ("RACKS") : nullptr; }
int rackTypes (const juce::File& project)
{
    auto xml = juce::XmlDocument::parse (project); int count = 0;
    if (xml != nullptr) if (auto* list = racks (*xml)) for (auto* r : list->getChildWithTagNameIterator ("RACK")) { juce::ignoreUnused (r); ++count; }
    return count;
}
}

int runRackTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("rack-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("source.wav");
        {
            juce::AudioBuffer<float> a (2, frames);
            for (int n = 0; n < frames; ++n) { a.setSample (0, n, (float) left (n)); a.setSample (1, n, (float) right (n)); }
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (a, 0, a.getNumSamples()), "write a generated stereo source");
        }
        const auto file = root.getChildFile ("render.wav");
        const auto g = db (-6);
        Session s (false);
        ok (s.importAudio (source, 0, 0), "import the source");
        ok (s.addUtility (0), "add a Utility");
        const auto utility = s.snapshot().tracks[0].plugins.back();
        auto settings = utility.utility; settings.gainDb = -6;
        ok (s.setUtilitySettings (utility.id, settings), "set the Utility to -6 dB");
        check (error (render (s, file), g, g) < 1e-5, "the plain chain plays the source at -6 dB");

        juce::String rackId;
        check (s.groupDevices (0, { "missing" }, rackId).failed() && s.groupDevices (0, { utility.id, utility.id }, rackId).failed(), "unknown or repeated devices are rejected");
        ok (s.groupDevices (0, { utility.id }, rackId), "group the Utility");
        auto info = rack (s);
        check (s.snapshot().tracks[0].plugins.size() == 1 && info.id == rackId && info.chains.size() == 1 && info.chains[0].devices.size() == 1
               && info.chains[0].devices[0].id == utility.id && info.chains[0].devices[0].isUtility && info.chains[0].settings.name == utility.name
               && info.macros.size() == (size_t) Session::rackMacroCount && info.macros[0].name == "Macro 1",
               "the rack holds one chain named after the Utility, plus eight macros");
        check (error (render (s, file), g, g) < 1e-5, "grouping keeps the sound");
        check (s.groupDevices (0, { rackId }, rackId).failed(), "a rack cannot go inside a rack");
        const auto first = info.chains[0].id;

        juce::String second;
        ok (s.addRackChain (rackId, second), "add an empty second chain");
        check (rack (s).chains.size() == 2 && rack (s).chains[1].settings.name == "Chain 2" && rack (s).chains[1].devices.empty(), "the new chain is empty");
        check (error (render (s, file), g + 1, g + 1) < 1e-5, "an empty chain passes the input, so the chains sum");
        ok (s.setRackChain (second, { "Dry", -6, 0, false, false }), "turn the second chain down to -6 dB");
        { const auto c = rack (s).chains[1].settings; check (c.name == "Dry" && std::abs (c.gainDb + 6) < 1e-3 && c.pan == 0 && ! c.mute && ! c.solo, "the chain reports its name and volume"); }
        check (error (render (s, file), 2 * g, 2 * g) < 1e-5, "chain volume scales its path");
        ok (s.setRackChain (second, { "Dry", -6, -1, false, false }), "pan the second chain hard left");
        {
            const auto x = render (s, file); double worst = 0, leftGain = 0;
            for (int n = settle; n < frames - 64; ++n) worst = std::max (worst, (double) std::abs (x.getSample (1, n) - g * right (n)));
            for (int n = settle; n < frames - 64; ++n) leftGain = std::max (leftGain, std::abs (x.getSample (0, n) / .25));
            check (worst < 1e-5 && leftGain > 2 * g * .99, "the panned chain leaves the right channel and adds to the left");
        }
        s.undo();
        check (rack (s).chains[1].settings.pan == 0 && error (render (s, file), 2 * g, 2 * g) < 1e-5, "undo restores the centred chain");
        ok (s.setRackChain (second, { "Dry", -6, 0, true, false }), "mute the second chain");
        check (error (render (s, file), g, g) < 1e-5, "a muted chain is silent");
        ok (s.setRackChain (second, { "Dry", -6, 0, false, true }), "solo the second chain");
        check (error (render (s, file), g, g) < 1e-5 && rack (s).chains[1].settings.solo, "soloing a chain silences the others");
        ok (s.setRackChain (first, { utility.name, 0, 0, false, true }), "solo the first chain too");
        check (error (render (s, file), 2 * g, 2 * g) < 1e-5, "both soloed chains play");
        s.undo(); s.undo();
        check (! rack (s).chains[1].settings.solo && rack (s).chains[1].settings.mute && error (render (s, file), g, g) < 1e-5, "undo returns to the muted chain");
        ok (s.setRackChain (second, { "Dry", 0, 0, false, false }), "reset the second chain");
        check (s.setRackChain (second, { "Dry", 7, 0, false, false }).failed() && s.setRackChain (second, { " ", 0, 0, false, false }).failed()
               && s.setRackChain ("missing", {}).failed(), "chain volume above +6 dB, empty names and unknown chains are rejected");

        // Devices inside the second chain, and a macro driving the EQ output.
        ok (s.addNativeEffectToChain (second, Session::EffectKind::equalizer), "add an EQ to the second chain");
        check (s.addNativeEffectToChain (second, Session::EffectKind::arpeggiator).failed() && s.addNativeEffectToChain (second, Session::EffectKind::sampler).failed(),
               "MIDI effects and instruments stay out of Audio Effect Racks");
        const auto eq = rack (s).chains[1].devices[0];
        check (eq.isNativeEffect && error (render (s, file), g + 1, g + 1) < 1e-5, "a neutral EQ in the chain keeps the sum");
        const int output = 32;
        check (std::string (effectParameters (Session::EffectKind::equalizer)[output].key) == "outputDb", "control 32 is the EQ output");
        ok (s.mapRackMacro (rackId, 0, eq.id, output, -24, 24), "map Macro 1 to the EQ output from -24 to +24 dB");
        info = rack (s);
        check (std::abs (info.macros[0].value - .5f) < 1e-6 && info.macros[0].mappings.size() == 1 && info.macros[0].mappings[0].pluginId == eq.id
               && info.macros[0].mappings[0].minimum == -24 && info.macros[0].mappings[0].maximum == 24, "the first mapping takes the control's 0 dB as the macro position");
        check (s.mapRackMacro (rackId, 1, eq.id, output, -24, 24).failed() && s.mapRackMacro (rackId, 1, utility.id, 0, 0, 1).failed()
               && s.mapRackMacro (rackId, 1, eq.id, output - 1, 0, 99).failed(), "a control follows one macro, Utilities and out-of-range mappings are rejected");
        ok (s.setRackMacro (rackId, 0, .25f), "turn Macro 1 to a quarter");
        check (std::abs (rack (s).chains[1].devices[0].effect.values[output] + 12) < 1e-4, "the macro sets the EQ output to -12 dB");
        check (error (render (s, file), g + db (-12), g + db (-12)) < 1e-5, "the render follows the macro");
        auto edited = rack (s).chains[1].devices[0].effect; edited.values[output] = 3;
        check (s.setNativeEffectSettings (eq.id, edited).failed(), "the mapped control cannot be edited directly");
        check (s.setDeviceAutomation (eq.id, output, { true, { { 0, 0 } } }).failed(), "the mapped control cannot be automated");
        edited = rack (s).chains[1].devices[0].effect; edited.values[output + 1] = .5f;
        ok (s.setNativeEffectSettings (eq.id, edited), "other EQ controls stay editable");
        s.undo();
        ok (s.renameRackMacro (rackId, 0, "Level"), "rename Macro 1");
        check (rack (s).macros[0].name == "Level" && s.setRackMacro (rackId, 0, 1.5f).failed() && s.setRackMacro (rackId, 8, 0).failed(), "the macro is renamed and invalid values are rejected");

        // Chain device order, bypass and removal.
        ok (s.addUtilityToChain (second), "add a Utility after the EQ");
        auto devices = rack (s).chains[1].devices;
        check (devices.size() == 2 && devices[0].id == eq.id && devices[1].isUtility, "chain devices keep their order");
        ok (s.moveDevice (devices[1].id, 0), "move the Utility first");
        check (rack (s).chains[1].devices[0].id == devices[1].id && rack (s).chains[1].devices[1].id == eq.id, "the Utility now comes first");
        check (error (render (s, file), g + db (-12), g + db (-12)) < 1e-5, "the reordered chain plays the same");
        ok (s.setDeviceEnabled (eq.id, false), "turn the EQ off");
        check (! rack (s).chains[1].devices[1].enabled && error (render (s, file), g + 1, g + 1) < 1e-5, "a deactivated chain device is bypassed");
        ok (s.setDeviceEnabled (eq.id, true), "turn the EQ on");
        ok (s.removeDevice (devices[1].id), "remove the chain's Utility");
        check (rack (s).chains[1].devices.size() == 1 && error (render (s, file), g + db (-12), g + db (-12)) < 1e-5, "removing a device leaves the rest of the chain");

        // Reopening.
        const auto project = root.getChildFile ("Rack").getChildFile ("Rack.halftrack");
        ok (project.getParentDirectory().createDirectory(), "create the project folder");
        const auto before = rack (s);
        const auto saved = render (s, file);
        ok (s.saveProject (project), "save the rack");
        check (rackTypes (project) == 1, "the saved project holds one rack");
        const auto mapped = project.getSiblingFile ("Mapped.halftrack");
        check (project.copyFileTo (mapped), "keep a copy with a mapped macro");
        ok (s.loadProject (project), "reopen the rack");
        info = rack (s);
        bool same = info.id == before.id && info.chains.size() == 2 && info.macros[0].name == "Level" && std::abs (info.macros[0].value - .25f) < 1e-6
            && info.macros[0].mappings == before.macros[0].mappings;
        for (size_t c = 0; same && c < info.chains.size(); ++c)
        {
            same = info.chains[c].id == before.chains[c].id && info.chains[c].settings.name == before.chains[c].settings.name
                && std::abs (info.chains[c].settings.gainDb - before.chains[c].settings.gainDb) < 1e-3 && info.chains[c].devices.size() == before.chains[c].devices.size();
            for (size_t d = 0; same && d < info.chains[c].devices.size(); ++d) same = info.chains[c].devices[d].id == before.chains[c].devices[d].id;
        }
        check (same && ! s.canUndo(), "chains, devices, macros and mappings reopen without history");
        {
            const auto x = render (s, file); double worst = 0;
            for (int c = 0; c < 2; ++c) for (int n = 0; n < frames; ++n) worst = std::max (worst, (double) std::abs (x.getSample (c, n) - saved.getSample (c, n)));
            check (worst < 1e-6, "the reopened rack renders identically");
        }
        ok (s.setRackMacro (rackId, 0, .5f), "the reopened macro still drives its control");
        check (std::abs (rack (s).chains[1].devices[0].effect.values[output]) < 1e-4, "Macro 1 at half sets 0 dB");
        s.undo();

        // Duplicates get their own rack.
        juce::String copy;
        ok (s.duplicateTrack (s.snapshot().tracks[0].id, copy), "duplicate the track");
        Session::PluginInfo copied;
        { const auto snapshot = s.snapshot(); for (const auto& t : snapshot.tracks) if (t.id == copy) copied = t.plugins[0]; }
        check (copied.isRack && copied.id != rackId && copied.chains.size() == 2 && copied.chains[0].id != first
               && copied.chains[1].devices[0].id != eq.id && copied.macros[0].mappings.size() == 1 && copied.macros[0].mappings[0].pluginId == copied.chains[1].devices[0].id,
               "the copy has its own rack, chains, devices and mappings");
        ok (s.setRackChain (copied.chains[1].id, { "Copy", 0, 0, true, false }), "mute the copy's second chain");
        check (! rack (s).chains[1].settings.mute, "the original rack is unchanged");
        int copyIndex = -1;
        for (int t = 0; t < (int) s.snapshot().tracks.size(); ++t) if (s.snapshot().tracks[(size_t) t].id == copy) copyIndex = t;
        ok (s.removeTrack (copyIndex), "remove the copy");
        ok (s.saveProject (project), "save without the copy");
        check (rackTypes (project) == 1, "removing a track removes its rack");

        // Removing a chain and ungrouping.
        check (s.ungroupRack (rackId).failed(), "a rack with two chains cannot be ungrouped");
        ok (s.removeRackChain (second), "delete the second chain");
        check (rack (s).chains.size() == 1 && error (render (s, file), g, g) < 1e-5, "deleting a chain removes its path");
        s.undo(); check (rack (s).chains.size() == 2 && rack (s).chains[1].devices.size() == 1, "undo restores the chain and its EQ");
        ok (s.removeRackChain (second), "delete the second chain again");
        ok (s.ungroupRack (rackId), "ungroup the one-chain rack");
        check (s.snapshot().tracks[0].plugins.size() == 1 && s.snapshot().tracks[0].plugins[0].id == utility.id && error (render (s, file), g, g) < 1e-5,
               "ungrouping returns the Utility to the track");
        ok (s.saveProject (project), "save the ungrouped track");
        check (rackTypes (project) == 0, "ungrouping removes the rack");
        s.undo(); check (rack (s).id == rackId, "undo regroups");

        // An empty rack passes its input; removing it removes its type.
        juce::String empty;
        ok (s.groupDevices (0, {}, empty), "add an empty Audio Effect Rack");
        check (rack (s).id == rackId && s.snapshot().tracks[0].plugins.size() == 2 && s.snapshot().tracks[0].plugins[1].chains.empty(), "the empty rack follows the first");
        check (error (render (s, file), g, g) < 1e-5, "a rack without chains passes its input");
        ok (s.removeDevice (empty), "remove the empty rack");
        ok (s.saveProject (project), "save");
        check (rackTypes (project) == 1, "removing a rack removes its type");

        // Invalid racks reject the load and keep the open project.
        const auto corrupt = [&] (const juce::String& name, const std::function<void (juce::XmlElement&)>& change)
        {
            auto xml = juce::XmlDocument::parse (mapped); check (xml != nullptr, "parse the saved project");
            change (*xml);
            const auto broken = project.getSiblingFile (name + ".halftrack"); check (xml->writeTo (broken), "write " + name);
            const auto result = s.loadProject (broken);
            check (result.failed() && result.getErrorMessage().contains ("Audio Effect Rack") && rack (s).id == rackId, name + " is rejected and the open project stays");
        };
        corrupt ("MacroValue", [] (juce::XmlElement& x) { racks (x)->getChildByName ("RACK")->getChildByName ("HALFTRACKMACROS")->getFirstChildElement()->setAttribute ("value", 2.0); });
        corrupt ("ForeignRack", [] (juce::XmlElement& x) { racks (x)->getChildByName ("RACK")->removeAttribute ("halftrackRack"); });
        corrupt ("BadMapping", [] (juce::XmlElement& x)
        {
            std::function<void (juce::XmlElement&)> visit = [&] (juce::XmlElement& e)
            { if (e.hasAttribute ("halftrackMacroMap")) e.setAttribute ("halftrackMacroMap", "0,999,0,1"); for (auto* c : e.getChildIterator()) visit (*c); };
            visit (x);
        });

        // Guards.
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const auto keys = (int) s.snapshot().tracks.size() - 1;
        ok (s.addNativeEffect (keys, Session::EffectKind::arpeggiator), "add an arpeggiator");
        juce::String unused;
        check (s.groupDevices (keys, { s.snapshot().tracks[(size_t) keys].plugins[0].id }, unused).failed(), "MIDI effects cannot be grouped into an Audio Effect Rack");
        ok (s.setTrackFrozen (s.snapshot().tracks[0].id, true), "freeze the rack's track");
        check (s.setRackMacro (rackId, 0, .1f).failed() && s.addRackChain (rackId, unused).failed() && s.setRackChain (first, { "x", 0, 0, false, false }).failed(),
               "a frozen track's rack is locked");
        std::cout << "Rack passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL rack: " << e.what() << std::endl; return 1; }
}
}
