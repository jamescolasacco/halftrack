#include "NativeEffects.h"
#include "Racks.h"
#include "Session.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

// Racks. Generated sources play through Audio Effect, Instrument, MIDI Effect and Drum Racks;
// every render is compared against values computed here from the zone, gain and pan rules
// written out by hand, never from the engine's own helpers. Chains of known devices (an
// Eight-band EQ with only an output gain, Samplers playing generated samples at their root
// note) make the expected output exact. Undo, save/reopen and malformed projects are checked
// through the Session facade, and the rack callback is audited for heap calls.
namespace halftrack::allocationAudit
{
extern thread_local bool enabled;
extern thread_local std::size_t allocations, deallocations;
}
namespace halftrack
{
namespace
{
namespace te = tracktion;
using Kind = Session::EffectKind;
constexpr double rate = 48000, pi = 3.14159265358979323846;
constexpr int beat = 24000; // Frames per beat at the default 120 BPM.
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS racks: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
double gainOf (double decibels) { return std::pow (10.0, decibels / 20.0); }
using Buffer = juce::AudioBuffer<float>;
using Expected = std::function<std::pair<double, double> (int)>;

// A stereo source with different left and right content, peaking below a quarter.
double sourceLeft (int n) { return .15 * std::sin (2 * pi * 220 * n / rate) + .08 * std::sin (2 * pi * 1375 * n / rate + .3); }
double sourceRight (int n) { return .12 * std::sin (2 * pi * 330 * n / rate + 1) - .07 * std::sin (2 * pi * 2750 * n / rate); }
// Decaying percussive samples for instruments, distinct per seed.
std::vector<float> hit (int frames, double frequency, double decay, double phase)
{
    std::vector<float> s;
    for (int n = 0; n < frames; ++n) s.push_back ((float) (.4 * std::exp (-n / decay) * std::sin (2 * pi * frequency * n / rate + phase)));
    return s;
}
void writeWav (const juce::File& f, const std::vector<float>& left, const std::vector<float>& right)
{
    Buffer a (2, (int) left.size());
    for (int n = 0; n < (int) left.size(); ++n) { a.setSample (0, n, left[(size_t) n]); a.setSample (1, n, right[(size_t) n]); }
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (a, 0, a.getNumSamples()), "write generated " + f.getFileName());
}
Buffer read (const juce::File& f)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    check (r != nullptr, "read " + f.getFileName()); Buffer x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
Buffer render (Session& s, const juce::File& file, double tail = 0)
{ ok (s.exportWav (file, rate, 32, tail), "render " + file.getFileNameWithoutExtension()); return read (file); }
double error (const Buffer& a, const Expected& expected, int start, int end)
{
    double worst = 0;
    for (int n = start; n < std::min (end, a.getNumSamples()); ++n)
    {
        const auto [l, r] = expected (n);
        worst = std::max ({ worst, std::abs (a.getSample (0, n) - l), std::abs (a.getSample (1, n) - r) });
    }
    return end > a.getNumSamples() ? 1e9 : worst;
}
double difference (const Buffer& a, const Buffer& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1e9;
    double worst = 0; for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n) worst = std::max (worst, (double) std::abs (a.getSample (c, n) - b.getSample (c, n)));
    return worst;
}
double peak (const Buffer& a, int start, int end)
{ end = std::min (end, a.getNumSamples()); return end <= start ? 0.0 : std::max (a.getMagnitude (0, start, end - start), a.getMagnitude (1, start, end - start)); }

const Session::PluginInfo* findIn (const std::vector<Session::PluginInfo>& devices, const juce::String& id)
{
    for (const auto& d : devices)
    {
        if (d.id == id) return &d;
        for (const auto* list : { &d.chains, &d.returns })
            for (const auto& c : *list) if (auto* found = findIn (c.devices, id)) return found;
    }
    return nullptr;
}
Session::PluginInfo device (Session& s, const juce::String& id)
{
    const auto snapshot = s.snapshot();
    for (const auto& t : snapshot.tracks)
    {
        if (auto* found = findIn (t.plugins, id)) return *found;
        const std::vector<Session::PluginInfo> instrument { t.instrument };
        if (auto* found = findIn (instrument, id)) return *found;
    }
    throw std::runtime_error ("device not found: " + id.toStdString());
}
Session::RackChainInfo chainOf (Session& s, const juce::String& rackId, const juce::String& chainId)
{
    const auto rack = device (s, rackId);
    for (const auto* list : { &rack.chains, &rack.returns }) for (const auto& c : *list) if (c.id == chainId) return c;
    throw std::runtime_error ("chain not found");
}
juce::String addChain (Session& s, const juce::String& rack, const juce::String& name = {})
{ juce::String id; ok (s.addRackChain (rack, name, id), "add chain " + name); return id; }
juce::String addDevice (Session& s, const juce::String& chain, Kind kind)
{ juce::String id; ok (s.addRackDevice (chain, kind, id), juce::String ("add ") + effectName (kind)); return id; }
void editChain (Session& s, const juce::String& rack, const juce::String& chain, const std::function<void (Session::RackChainSettings&)>& change)
{ auto settings = chainOf (s, rack, chain).settings; change (settings); ok (s.setRackChainSettings (chain, settings), "edit chain " + settings.name); }
void setControl (Session& s, const juce::String& id, const char* key, float value)
{
    auto settings = device (s, id).effect;
    const auto index = effectParameterIndex (settings.kind, key);
    check (index >= 0, juce::String ("control ") + key + " exists");
    settings.values[(size_t) index] = value;
    ok (s.setNativeEffectSettings (id, settings), juce::String ("set ") + key);
}
// An EQ whose bands all pass unchanged and whose output stage applies decibels.
juce::String gainStage (Session& s, const juce::String& chain, float decibels)
{ const auto id = addDevice (s, chain, Kind::equalizer); setControl (s, id, "outputDb", decibels); return id; }
// A Sampler that plays its sample at velocity-scaled unity level from its root note.
juce::String sampler (Session& s, const juce::String& chain, const juce::File& file, int root)
{
    const auto id = addDevice (s, chain, Kind::sampler);
    ok (s.setRackDeviceSample (id, file), "load " + file.getFileName());
    auto settings = device (s, id).effect;
    namespace sp = instruments::sampler;
    settings.values[sp::mode] = 1; settings.values[sp::rootNote] = (float) root; settings.values[sp::velocityAmp] = 1;
    ok (s.setNativeEffectSettings (id, settings), "configure one-shot Sampler");
    return id;
}
juce::String midiClip (Session& s, const juce::String& track, double beats)
{
    ok (s.addMidiClip (track, 0, beats), "add MIDI clip");
    for (const auto& t : s.snapshot().tracks) if (t.id == track) return t.clips.back().id;
    throw std::runtime_error ("clip not found");
}
void clearNotes (Session& s, const juce::String& clip)
{
    juce::StringArray ids;
    for (const auto& t : s.snapshot().tracks) for (const auto& c : t.clips) if (c.id == clip) for (const auto& n : c.notes) ids.add (n.id);
    ok (s.removeMidiNotes (clip, ids), "clear notes");
}

void zoneTests()
{
    check (racks::validZone ({ 0, 127, 0, 127 }, 0, 127) && racks::validZone ({ 5, 5, 5, 5 }, 0, 127) && ! racks::validZone ({ 10, 9, 10, 9 }, 0, 127)
           && ! racks::validZone ({ 10, 20, 9, 20 }, 0, 127) && ! racks::validZone ({ 10, 20, 15, 12 }, 0, 127) && ! racks::validZone ({ 0, 127, 0, 127 }, 1, 127),
           "a zone needs low <= fade low <= fade high <= high inside its range");
    const Session::RackZone faded { 40, 50, 40, 47 };
    check (racks::zoneGain (faded, 47) == 1 && racks::zoneGain (faded, 48) == .75 && racks::zoneGain (faded, 49) == .5 && racks::zoneGain (faded, 50) == .25
           && racks::zoneGain (faded, 51) == 0 && racks::zoneGain (faded, 39) == 0 && racks::zoneGain (faded, 40) == 1,
           "a top fade of three keys steps 3/4, 1/2, 1/4 towards the zone's edge and filters outside it");
    check (std::abs (racks::zoneGain ({ 1, 127, 41, 127 }, 21) - 21.0 / 41) < 1e-15, "a bottom fade gains (value - low + 1) / (fade low - low + 1)");
    const Session::RackZone select { 0, 63, 0, 31 };
    check (std::abs (racks::selectorOutputGain (select, 47) - 17.0 / 33) < 1e-15 && racks::selectorOutputGain (select, 100) == 0
           && racks::selectorOutputGain ({ 0, 63, 0, 63 }, 100) == 1 && racks::selectorOutputGain ({ 32, 127, 63, 127 }, 10) == 0,
           "outside its zone a chain is silent only on a side that ends in a fade");
    const auto centre = racks::panGains (0), left = racks::panGains (-1), right = racks::panGains (1);
    check (std::abs (centre.first - 1) < 1e-12 && std::abs (centre.second - 1) < 1e-12 && std::abs (left.first - std::sqrt (2.0)) < 1e-12
           && std::abs (left.second) < 1e-12 && std::abs (right.first) < 1e-12, "chain pan follows the track mixer's unity-centre law");
    check (racks::decibelGain (-100) == 0 && std::abs (racks::decibelGain (-6) - gainOf (-6)) < 1e-15, "-100 dB is silence");
    check (racks::chainAccepts (Kind::audioEffectRack, Kind::delay) && ! racks::chainAccepts (Kind::audioEffectRack, Kind::sampler)
           && ! racks::chainAccepts (Kind::audioEffectRack, Kind::chord) && racks::chainAccepts (Kind::midiEffectRack, Kind::chord)
           && ! racks::chainAccepts (Kind::midiEffectRack, Kind::delay) && racks::chainAccepts (Kind::instrumentRack, Kind::drumRack)
           && racks::chainAccepts (Kind::drumRack, Kind::audioEffectRack), "each rack kind accepts the devices of its signal");
    check (effectIsInstrument (Kind::instrumentRack) && effectIsInstrument (Kind::drumRack) && effectIsMidi (Kind::midiEffectRack)
           && ! effectIsInstrument (Kind::audioEffectRack) && ! effectIsMidi (Kind::audioEffectRack) && effectParameters (Kind::drumRack).size() == 17,
           "racks classify as instruments, MIDI effects or audio devices and carry sixteen macros and a selector");
}

void audioRackTests (const juce::File& root)
{
    const int frames = 48000;
    std::vector<float> left, right;
    for (int n = 0; n < frames; ++n) { left.push_back ((float) sourceLeft (n)); right.push_back ((float) sourceRight (n)); }
    const auto sourceFile = root.getChildFile ("source.wav"); writeWav (sourceFile, left, right);
    const auto x = [&] (int n) { return std::pair<double, double> { left[(size_t) n], right[(size_t) n] }; };
    const auto scaled = [&] (double l, double r) -> Expected { return [&, l, r] (int n) { return std::pair<double, double> { l * left[(size_t) n], r * right[(size_t) n] }; }; };
    const int start = 2000, end = frames - 2000;
    const auto out = root.getChildFile ("audio-rack.wav");
    Session s (false);
    ok (s.importAudio (sourceFile, 0), "import the generated source");
    const auto dry = render (s, out);
    check (error (dry, x, start, end) < 1e-6, "the dry track plays the source");
    ok (s.addNativeEffect (0, Kind::audioEffectRack), "insert an Audio Effect Rack");
    const auto rack = s.snapshot().tracks[0].plugins[0];
    check (rack.isRack && rack.effect.kind == Kind::audioEffectRack && rack.chains.empty() && rack.visibleMacros == 8 && rack.name == "Audio Effect Rack",
           "the rack reports itself with no chains and eight visible macros");
    check (difference (render (s, out), dry) == 0, "an empty Audio Effect Rack passes its input");
    const auto a = addChain (s, rack.id);
    check (chainOf (s, rack.id, a).settings.name == "Chain" && chainOf (s, rack.id, a).settings == Session::RackChainSettings { "Chain" },
           "a new chain starts at unity, centred, active, with full key and velocity zones and a selector zone at 0");
    check (error (render (s, out), x, start, end) < 1e-6, "one empty chain passes the signal unchanged");
    const auto b = addChain (s, rack.id, "Quiet");
    const auto eq = gainStage (s, b, -6);
    const double g = gainOf (-6);
    check (error (render (s, out), scaled (1 + g, 1 + g), start, end) < 1e-6, "parallel chains sum: dry plus the -6 dB chain");
    editChain (s, rack.id, a, [] (auto& c) { c.volumeDb = -12; c.pan = -1; });
    const auto aLeft = gainOf (-12) * std::sqrt (2.0);
    const auto mixed = render (s, out);
    check (error (mixed, scaled (aLeft + g, g), start, end) < 1e-6, "chain volume and hard-left pan shape only that chain");
    editChain (s, rack.id, b, [] (auto& c) { c.active = false; });
    check (error (render (s, out), scaled (aLeft, 0), start, end) < 1e-6, "a deactivated chain is silent");
    editChain (s, rack.id, b, [] (auto& c) { c.active = true; c.solo = true; });
    check (error (render (s, out), scaled (g, g), start, end) < 1e-6, "soloing a chain silences the others");
    editChain (s, rack.id, a, [] (auto& c) { c.solo = true; });
    check (error (render (s, out), scaled (aLeft + g, g), start, end) < 1e-6, "two soloed chains both play");
    s.undo(); s.undo(); s.undo();
    check (difference (render (s, out), mixed) == 0, "undo restores chain activation and solo one step at a time");
    // The selector reaches only chains whose zone holds it; an unreached chain gets no input.
    auto controls = device (s, rack.id).effect;
    controls.values[Session::rackChainSelector] = 15;
    ok (s.setNativeEffectSettings (rack.id, controls), "move the chain selector to 15");
    check (peak (render (s, out), 0, frames) == 0, "chains whose selector zones end at 0 receive nothing at 15");
    editChain (s, rack.id, b, [] (auto& c) { c.select = { 0, 127, 0, 127 }; });
    editChain (s, rack.id, a, [] (auto& c) { c.select = { 0, 20, 0, 10 }; });
    check (error (render (s, out), scaled (aLeft * 6 / 11 + g, g), start, end) < 1e-6, "a selector inside a fade range lowers that chain's output linearly");
    editChain (s, rack.id, a, [] (auto& c) { c.select = { 0, 12, 0, 12 }; });
    check (error (render (s, out), scaled (g, g), start, end) < 1e-6, "past a zone's end the chain stops receiving input");
    // Device order and acceptance inside chains.
    juce::String rejected;
    check (s.addRackDevice (a, Kind::sampler, rejected).failed() && s.addRackDevice (a, Kind::chord, rejected).failed() && rejected.isEmpty(),
           "an audio chain refuses instruments and MIDI effects");
    const auto second = gainStage (s, b, -3);
    check (chainOf (s, rack.id, b).devices.size() == 2 && error (render (s, out), scaled (gainOf (-9), gainOf (-9)), start, end) < 1e-6
           && std::abs (gainOf (-6) * gainOf (-3) - gainOf (-9)) < 1e-12, "devices in a chain run in series");
    ok (s.setRackDeviceEnabled (second, false), "bypass the second device");
    check (error (render (s, out), scaled (g, g), start, end) < 1e-6, "a bypassed chain device passes its input");
    ok (s.moveRackDevice (second, 0), "move the bypassed device first");
    check (chainOf (s, rack.id, b).devices[0].id == second && ! chainOf (s, rack.id, b).devices[0].enabled, "moving a device keeps its identity and state");
    ok (s.removeRackDevice (second), "remove the device");
    check (chainOf (s, rack.id, b).devices.size() == 1, "the device leaves the chain");
    s.undo(); check (chainOf (s, rack.id, b).devices.size() == 2 && chainOf (s, rack.id, b).devices[0].id == second, "undo restores the removed device in place");
    s.redo();
    // Nested racks and duplicated chains.
    const auto nested = addDevice (s, b, Kind::audioEffectRack);
    check (error (render (s, out), scaled (g, g), start, end) < 1e-6, "an empty nested rack passes its input");
    const auto inner = addChain (s, nested);
    gainStage (s, inner, -6);
    check (error (render (s, out), scaled (g * g, g * g), start, end) < 1e-6, "a nested rack's chain processes inside the outer chain");
    juce::String copy;
    ok (s.duplicateRackChain (b, copy), "duplicate the chain");
    const auto duplicated = chainOf (s, rack.id, copy);
    check (duplicated.devices.size() == 2 && duplicated.devices[0].id != eq && duplicated.devices[1].isRack && duplicated.devices[1].chains.size() == 1,
           "a duplicated chain copies its devices with new identities");
    check (error (render (s, out), scaled (2 * g * g, 2 * g * g), start, end) < 1e-6, "the duplicate plays alongside the original");
    ok (s.moveRackChain (copy, 0), "move the duplicate first");
    check (device (s, rack.id).chains[0].id == copy, "chains reorder");
    ok (s.removeRackChain (copy), "delete the duplicate");
    check (device (s, rack.id).chains.size() == 2, "the chain is deleted");
    check (s.removeRackChain ("missing").failed() && s.addRackChain ("missing", {}, copy).failed() && s.setRackChainSettings (a, { "x", 7 }).failed(),
           "missing chains, missing racks and out-of-range volumes are rejected");
    // A latent device: the rack delays its other chains and reports the latency to the engine.
    ok (s.addAudioTrack ("Latency"), "add a latency track"); ok (s.importAudio (sourceFile, 1), "import the source again");
    ok (s.addNativeEffect (1, Kind::audioEffectRack), "insert a second rack");
    const auto second2 = s.snapshot().tracks[1].plugins[0].id;
    addChain (s, second2); const auto limited = addChain (s, second2);
    addDevice (s, limited, Kind::limiter);
    ok (s.setTrackMute (0, true), "mute the first track");
    check (error (render (s, out), scaled (2, 2), start, end) < 1e-6, "a dry chain beside a lookahead limiter is delayed to stay aligned");
    ok (s.removeTrack (1), "remove the latency track"); ok (s.setTrackMute (0, false), "unmute the first track");
    // Group Devices wraps a serial pair into a one-chain rack that sounds the same.
    ok (s.addAudioTrack ("Group"), "add a track to group"); ok (s.importAudio (sourceFile, 1), "import the source to group");
    ok (s.setTrackMute (0, true), "mute the rack track");
    ok (s.addNativeEffect (1, Kind::equalizer), "add a first EQ"); ok (s.addNativeEffect (1, Kind::equalizer), "add a second EQ");
    setControl (s, s.snapshot().tracks[1].plugins[0].id, "outputDb", -4); setControl (s, s.snapshot().tracks[1].plugins[1].id, "outputDb", -2);
    const auto serial = render (s, out);
    juce::String grouped;
    const juce::StringArray pair { s.snapshot().tracks[1].plugins[1].id, s.snapshot().tracks[1].plugins[0].id };
    check (s.groupDevices (1, { pair[0], pair[0] }, grouped).failed() && s.groupDevices (1, { pair[0], "missing" }, grouped).failed() && s.groupDevices (1, {}, grouped).failed(),
           "grouping refuses repeated, missing and empty selections");
    ok (s.groupDevices (1, pair, grouped), "group both EQs");
    const auto groupedInfo = s.snapshot().tracks[1].plugins;
    check (groupedInfo.size() == 1 && groupedInfo[0].id == grouped && groupedInfo[0].effect.kind == Kind::audioEffectRack
           && groupedInfo[0].chains.size() == 1 && groupedInfo[0].chains[0].devices.size() == 2, "grouping puts the devices in one chain of a new rack");
    check (difference (render (s, out), serial) < 1e-6, "the grouped chain sounds like the serial devices");
    check (std::abs (groupedInfo[0].chains[0].devices[0].effect.values[(size_t) effectParameterIndex (Kind::equalizer, "outputDb")] + 4) < 1e-6,
           "grouping keeps the devices in track order whatever order they were chosen in");
    ok (s.setRackMacroName (grouped, 0, "  Drive  "), "name macro 1");
    check (device (s, grouped).macroNames[0] == "Drive" && racks::macroName (device (s, grouped), 0) == "Drive" && racks::macroName (device (s, grouped), 1) == "Macro 2",
           "a named macro shows its name; others keep their automatic names");
    check (s.setRackMacroName (grouped, 16, "x").failed() && s.setRackMacroName (grouped, 0, juce::String::repeatedString ("x", 65)).failed(), "macro names need a macro and at most 64 characters");
    s.undo(); check (device (s, grouped).macroNames[0].isEmpty(), "renaming a macro is one undo step");
    ok (s.ungroupRack (grouped), "ungroup the rack");
    const auto ungrouped = s.snapshot().tracks[1].plugins;
    check (ungrouped.size() == 2 && ! ungrouped[0].isRack && ungrouped[0].effect.kind == Kind::equalizer && difference (render (s, out), serial) < 1e-6,
           "ungrouping puts the chain's devices back on the track and they sound the same");
    s.undo(); check (s.snapshot().tracks[1].plugins.size() == 1 && s.snapshot().tracks[1].plugins[0].id == grouped, "undo restores the rack");
    juce::String extraChain;
    ok (s.addRackChain (grouped, {}, extraChain), "give the rack a second chain");
    check (s.ungroupRack (grouped).failed(), "a rack with two chains cannot be ungrouped");
    s.undo(); s.undo();
    check (s.snapshot().tracks[1].plugins.size() == 2 && ! s.snapshot().tracks[1].plugins[0].isRack, "undo restores the ungrouped devices");
    // The channel Utility groups with built-in devices and works inside a chain.
    ok (s.addUtility (1), "add a Utility after the EQs");
    auto utilitySettings = s.snapshot().tracks[1].plugins[2].utility; utilitySettings.gainDb = -6;
    ok (s.setUtilitySettings (s.snapshot().tracks[1].plugins[2].id, utilitySettings), "turn the Utility down 6 dB");
    const auto withUtility = render (s, out);
    juce::StringArray three; const auto beforeUtility = s.snapshot(); for (const auto& p : beforeUtility.tracks[1].plugins) three.add (p.id);
    ok (s.groupDevices (1, three, grouped), "group the EQs and the Utility");
    const auto utilityChain = device (s, grouped).chains[0];
    check (utilityChain.devices.size() == 3 && utilityChain.devices[2].isUtility && utilityChain.devices[2].utility.gainDb == -6
           && difference (render (s, out), withUtility) < 1e-6, "a Utility inside a chain keeps its settings and sound");
    utilitySettings.gainDb = -12;
    ok (s.setUtilitySettings (utilityChain.devices[2].id, utilitySettings), "edit the Utility inside the chain");
    check (difference (render (s, out), withUtility) > 1e-3 && device (s, grouped).chains[0].devices[2].utility.gainDb == -12, "a chain Utility is edited through the ordinary Utility command");
    juce::String added;
    ok (s.addRackUtility (utilityChain.id, added), "add another Utility to the chain");
    check (device (s, grouped).chains[0].devices.size() == 4 && device (s, grouped).chains[0].devices[3].isUtility, "Utilities can be added to chains");
    check (s.setRackMacroMapping (grouped, { 0, added, 0, -100, 0 }).failed(), "macros map built-in device controls, not the Utility");
    s.undo(); s.undo(); s.undo(); s.undo(); s.undo();
    check (s.snapshot().tracks[1].plugins.size() == 2, "undo removes the Utility again");
}

void instrumentRackTests (const juce::File& root)
{
    const auto a = hit (9600, 330, 2000, 0), b = hit (9600, 523, 1500, 1), c = hit (9600, 196, 2500, 2);
    const auto fa = root.getChildFile ("a.wav"), fb = root.getChildFile ("b.wav"), fc = root.getChildFile ("c.wav");
    writeWav (fa, a, a); writeWav (fb, b, b); writeWav (fc, c, c);
    const auto at = [] (const std::vector<float>& s, int n) { return n >= 0 && n < (int) s.size() ? (double) s[(size_t) n] : 0.0; };
    const auto out = root.getChildFile ("instrument-rack.wav");
    Session s (false);
    ok (s.addMidiTrack ("Keys"), "add a MIDI track");
    const auto track = s.snapshot().tracks.back().id;
    const auto clip = midiClip (s, track, 4);
    ok (s.setTrackInstrument (track, Kind::instrumentRack), "choose an Instrument Rack");
    const auto rack = s.snapshot().tracks.back().instrument;
    check (rack.isRack && rack.effect.kind == Kind::instrumentRack, "the Instrument Rack is the track's instrument");
    ok (s.addMidiNote (clip, 48, 0, .5, 1), "C2 on beat one");
    check (peak (render (s, out), 0, 4 * beat) == 0, "an empty Instrument Rack is silent");
    const auto low = addChain (s, rack.id, "Low"), high = addChain (s, rack.id, "High");
    const auto lowSampler = sampler (s, low, fa, 48), highSampler = sampler (s, high, fb, 48);
    check (chainOf (s, rack.id, low).settings.name == "Low", "a named chain keeps its name when its first device arrives");
    check (error (render (s, out), [&] (int n) { const auto v = at (a, n) + at (b, n); return std::pair { v, v }; }, 0, 2 * beat) < 1e-6,
           "every chain whose zones hold the note plays it");
    editChain (s, rack.id, low, [] (auto& z) { z.key = { 0, 59, 0, 59 }; });
    editChain (s, rack.id, high, [] (auto& z) { z.key = { 60, 127, 60, 127 }; });
    ok (s.addMidiNote (clip, 72, 1, .5, 1), "C4 on beat two");
    ok (s.setNativeEffectSettings (highSampler, [&] { auto v = device (s, highSampler).effect; v.values[instruments::sampler::rootNote] = 72; return v; }()), "tune the high Sampler to C4");
    check (error (render (s, out), [&] (int n) { const auto v = at (a, n) + at (b, n - beat); return std::pair { v, v }; }, 0, 2 * beat + 100) < 1e-6,
           "key zones split the keyboard: C2 reaches only the low chain and C4 only the high one");
    editChain (s, rack.id, high, [] (auto& z) { z.key = { 48, 127, 48, 127 }; });
    ok (s.setNativeEffectSettings (highSampler, [&] { auto v = device (s, highSampler).effect; v.values[instruments::sampler::rootNote] = 48; return v; }()), "tune the high Sampler to C2");
    // C2 now reaches both chains; C4, two octaves above the high Sampler's root, reads every fourth frame.
    check (error (render (s, out), [&] (int n) { const auto v = at (a, n) + at (b, n) + (n >= beat && n - beat < 2400 ? at (b, 4 * (n - beat)) : 0.0); return std::pair { v, v }; }, 0, 2 * beat) < 1e-6,
           "overlapping zones layer both chains");
    clearNotes (s, clip);
    editChain (s, rack.id, high, [] (auto& z) { z.key = { 60, 127, 60, 127 }; });
    // A key fade: C#2 sits two keys inside a three-key top fade, so velocity 127 becomes 64.
    editChain (s, rack.id, low, [] (auto& z) { z.key = { 40, 50, 40, 47 }; });
    ok (s.setNativeEffectSettings (lowSampler, [&] { auto v = device (s, lowSampler).effect; v.values[instruments::sampler::rootNote] = 49; return v; }()), "tune the low Sampler to C#2");
    ok (s.addMidiNote (clip, 49, 0, .5, 1), "C#2 at full velocity");
    check (error (render (s, out), [&] (int n) { const auto v = at (a, n) * 64 / 127; return std::pair { v, v }; }, 0, beat) < 1e-6,
           "a key fade range lowers the note's velocity by its position in the fade");
    // Velocity zones split by velocity, and a velocity fade scales it.
    clearNotes (s, clip);
    editChain (s, rack.id, low, [] (auto& z) { z.key = {}; z.velocity = { 1, 63, 1, 63 }; });
    editChain (s, rack.id, high, [] (auto& z) { z.key = {}; z.velocity = { 64, 127, 64, 127 }; });
    for (const auto& id : { lowSampler, highSampler })
        ok (s.setNativeEffectSettings (id, [&] { auto v = device (s, id).effect; v.values[instruments::sampler::rootNote] = 60; return v; }()), "tune to C3");
    ok (s.addMidiNote (clip, 60, 0, .5, 100.0f / 127), "C3 at velocity 100"); ok (s.addMidiNote (clip, 60, 1, .5, 30.0f / 127), "C3 at velocity 30");
    check (error (render (s, out), [&] (int n) { const auto v = at (b, n) * 100 / 127 + at (a, n - beat) * 30 / 127; return std::pair { v, v }; }, 0, 2 * beat) < 1e-6,
           "velocity zones send loud notes to one chain and soft notes to the other");
    editChain (s, rack.id, low, [] (auto& z) { z.velocity = { 1, 127, 41, 127 }; });
    editChain (s, rack.id, high, [] (auto& z) { z.active = false; });
    clearNotes (s, clip); ok (s.addMidiNote (clip, 60, 0, .5, 21.0f / 127), "C3 at velocity 21");
    check (error (render (s, out), [&] (int n) { const auto v = at (a, n) * 11 / 127; return std::pair { v, v }; }, 0, beat) < 1e-6,
           "a velocity fade turns 21 into round(21 * 21 / 41) = 11");
    // Chain select zones crossfade the chains' outputs; a macro moves the selector.
    editChain (s, rack.id, low, [] (auto& z) { z.velocity = { 1, 127, 1, 127 }; z.select = { 0, 63, 0, 31 }; });
    editChain (s, rack.id, high, [] (auto& z) { z.active = true; z.select = { 32, 127, 63, 127 }; });
    clearNotes (s, clip); ok (s.addMidiNote (clip, 60, 0, .5, 1), "C3 at full velocity");
    ok (s.setRackMacroMapping (rack.id, { 0, rack.id, Session::rackChainSelector, 0, 127 }), "map macro 1 to the chain selector");
    check (device (s, rack.id).mappings.size() == 1 && device (s, rack.id).effect.values[0] == 0, "the first mapping takes the selector's position onto the macro");
    auto macros = device (s, rack.id).effect; macros.values[0] = 47;
    ok (s.setNativeEffectSettings (rack.id, macros), "turn macro 1 to 47");
    const auto crossfade = render (s, out);
    check (error (crossfade, [&] (int n) { const auto v = at (a, n) * 17 / 33 + at (b, n) * 16 / 32; return std::pair { v, v }; }, 0, beat) < 1e-6,
           "at 47 both fades are partly open: 17/33 of the low chain and half of the high one");
    macros.values[0] = 100; ok (s.setNativeEffectSettings (rack.id, macros), "turn macro 1 to 100");
    check (error (render (s, out), [&] (int n) { const auto v = at (b, n); return std::pair { v, v }; }, 0, beat) < 1e-6,
           "at 100 only the high chain is reached");
    s.undo();
    check (difference (render (s, out), crossfade) == 0, "undo restores the macro position");
    // MIDI Effect Rack: parallel MIDI chains merge before the instrument.
    ok (s.addMidiTrack ("Arp"), "add a second MIDI track");
    ok (s.setTrackMute (0 + (int) s.snapshot().tracks.size() - 2, true), "mute the rack track");
    const auto second = s.snapshot().tracks.back().id; const auto secondClip = midiClip (s, second, 4);
    ok (s.setInstrumentSample (second, fc), "load a Sampler on the second track");
    auto plain = s.snapshot().tracks.back().instrument.effect;
    plain.values[instruments::sampler::mode] = 1; plain.values[instruments::sampler::velocityAmp] = 1;
    ok (s.setNativeEffectSettings (s.snapshot().tracks.back().instrument.id, plain), "make it a one-shot");
    ok (s.addNativeEffect ((int) s.snapshot().tracks.size() - 1, Kind::midiEffectRack), "insert a MIDI Effect Rack");
    const auto midiRack = s.snapshot().tracks.back().plugins[0].id;
    check (device (s, midiRack).effect.kind == Kind::midiEffectRack, "the MIDI Effect Rack sits before the instrument");
    ok (s.addMidiNote (secondClip, 60, 0, .5, 1), "C3 into the MIDI rack");
    const auto up = addChain (s, midiRack, "Octave"), through = addChain (s, midiRack, "Through");
    const auto pitch = addDevice (s, up, Kind::pitch); setControl (s, pitch, "semitones", 12);
    juce::String refused; check (s.addRackDevice (up, Kind::delay, refused).failed() && s.addRackUtility (up, refused).failed(), "a MIDI chain refuses audio effects and the Utility");
    const auto both = render (s, out);
    check (error (both, [&] (int n) { const auto v = at (c, n) + at (c, 2 * n) * (n < 4800); return std::pair { v, v }; }, 0, beat) < 1e-6,
           "both MIDI chains' notes reach the Sampler: the original and an octave up");
    editChain (s, midiRack, up, [] (auto& z) { z.key = { 0, 59, 0, 59 }; });
    check (error (render (s, out), [&] (int n) { const auto v = at (c, n); return std::pair { v, v }; }, 0, beat) < 1e-6, "MIDI chains filter by key zone");
    editChain (s, midiRack, up, [] (auto& z) { z.key = {}; z.select = { 0, 20, 0, 10 }; });
    editChain (s, midiRack, through, [] (auto& z) { z.select = { 0, 127, 0, 127 }; });
    auto midiControls = device (s, midiRack).effect; midiControls.values[Session::rackChainSelector] = 15;
    ok (s.setNativeEffectSettings (midiRack, midiControls), "move the MIDI rack's selector into a fade");
    check (error (render (s, out), [&] (int n) { const auto v = at (c, n) + at (c, 2 * n) * (n < 4800) * 69 / 127; return std::pair { v, v }; }, 0, beat) < 1e-6,
           "a MIDI rack's selector fade lowers velocity: 127 * 6/11 rounds to 69");
    editChain (s, midiRack, up, [] (auto& z) { z.active = false; });
    check (error (render (s, out), [&] (int n) { const auto v = at (c, n); return std::pair { v, v }; }, 0, beat) < 1e-6, "a deactivated MIDI chain starts no notes");
    // Instrument chains keep their device order.
    juce::String extra;
    check (s.addRackDevice (low, Kind::chord, extra).wasOk() && chainOf (s, rack.id, low).devices[0].effect.kind == Kind::chord, "MIDI effects go before a chain's instrument");
    ok (s.addRackDevice (low, Kind::equalizer, extra), "add an audio effect after the instrument");
    check (s.moveRackDevice (extra, 0).failed(), "an audio effect cannot move before the instrument");
    ok (s.addRackDevice (low, Kind::polySynth, extra), "add another instrument");
    const auto lowChain = chainOf (s, rack.id, low);
    check (lowChain.devices.size() == 3 && lowChain.devices[1].effect.kind == Kind::polySynth, "a second instrument replaces the chain's instrument in place");
    // Group Instrument wraps the instrument and the audio effects after it.
    juce::String instrumentRack;
    ok (s.addNativeEffect ((int) s.snapshot().tracks.size() - 1, Kind::equalizer), "add an EQ after the Sampler");
    ok (s.removePlugin ((int) s.snapshot().tracks.size() - 1, 0), "remove the MIDI rack");
    const auto before = render (s, out);
    ok (s.groupInstrument (second, instrumentRack), "group the instrument");
    const auto groupedTrack = s.snapshot().tracks.back();
    check (groupedTrack.instrument.id == instrumentRack && groupedTrack.instrument.effect.kind == Kind::instrumentRack
           && groupedTrack.instrument.chains[0].devices.size() == 2 && groupedTrack.plugins.empty(), "the instrument and its EQ become one chain of an Instrument Rack");
    check (difference (render (s, out), before) < 1e-6, "the grouped instrument sounds the same");
    ok (s.ungroupRack (instrumentRack), "ungroup the Instrument Rack");
    const auto restored = s.snapshot().tracks.back();
    check (! restored.instrument.isRack && restored.instrument.effect.kind == Kind::sampler && restored.plugins.size() == 1 && restored.plugins[0].effect.kind == Kind::equalizer
           && difference (render (s, out), before) < 1e-6, "ungrouping returns the instrument to the instrument slot and the EQ after it");
}

void drumRackTests (const juce::File& root)
{
    const auto kick = hit (12000, 60, 3000, 0), snare = hit (6000, 210, 900, 1), closed = hit (2400, 3100, 300, .5), open = hit (24000, 2900, 9000, 1.5);
    const auto fk = root.getChildFile ("kick.wav"), fs = root.getChildFile ("snare.wav"), fcl = root.getChildFile ("closed.wav"), fo = root.getChildFile ("open.wav");
    writeWav (fk, kick, kick); writeWav (fs, snare, snare); writeWav (fcl, closed, closed); writeWav (fo, open, open);
    const auto at = [] (const std::vector<float>& s, int n) { return n >= 0 && n < (int) s.size() ? (double) s[(size_t) n] : 0.0; };
    const auto out = root.getChildFile ("drum-rack.wav");
    Session s (false);
    ok (s.addMidiTrack ("Drums"), "add a drum track");
    const auto track = s.snapshot().tracks.back().id; const auto clip = midiClip (s, track, 4);
    ok (s.setTrackInstrument (track, Kind::drumRack), "choose a Drum Rack");
    const auto rack = s.snapshot().tracks.back().instrument.id;
    juce::String kickChain, snareChain, chain;
    check (s.setDrumRackPadSample (rack, 128, fk, chain).failed() && s.setDrumRackPadSample (rack, 36, root.getChildFile ("none.wav"), chain).failed(),
           "pads outside 0-127 and missing samples are rejected");
    ok (s.setDrumRackPadSample (rack, 36, fk, kickChain), "drop the kick on C1");
    ok (s.setDrumRackPadSample (rack, 38, fs, snareChain), "drop the snare on D1");
    const auto kickInfo = chainOf (s, rack, kickChain);
    check (kickInfo.settings.receiveNote == 36 && kickInfo.settings.playNote == 60 && kickInfo.settings.name == "kick" && kickInfo.devices.size() == 1
           && kickInfo.devices[0].effect.kind == Kind::sampler && kickInfo.devices[0].effect.values[instruments::sampler::mode] == 1 && kickInfo.devices[0].sample == fk,
           "a dropped sample makes a pad chain named after it with a one-shot Sampler playing C3");
    ok (s.addMidiNote (clip, 36, 0, .25, 1), "kick on beat one"); ok (s.addMidiNote (clip, 38, .5, .25, 1), "snare on the off-beat"); ok (s.addMidiNote (clip, 40, 1, .25, 1), "a note without a pad");
    const auto beatOne = render (s, out);
    check (error (beatOne, [&] (int n) { const auto v = at (kick, n) + at (snare, n - beat / 2); return std::pair { v, v }; }, 0, 2 * beat) < 1e-6,
           "each pad's chain receives only its note and plays its sample exactly");
    juce::String same;
    ok (s.setDrumRackPadSample (rack, 36, fs, same), "drop the snare on the kick pad");
    check (same == kickChain && chainOf (s, rack, kickChain).devices[0].sample == fs, "a sample dropped on a filled pad replaces its Sampler's sample");
    s.undo(); check (chainOf (s, rack, kickChain).devices[0].sample == fk, "undo restores the pad's sample");
    editChain (s, rack, kickChain, [] (auto& z) { z.playNote = 72; });
    check (error (render (s, out), [&] (int n) { const auto v = at (kick, 2 * n) * (n < 6000) + at (snare, n - beat / 2); return std::pair { v, v }; }, 0, beat) < 1e-6,
           "the play note transposes what the pad's devices receive");
    editChain (s, rack, kickChain, [] (auto& z) { z.playNote = 60; z.volumeDb = -6; z.pan = 1; });
    check (error (render (s, out), [&] (int n) { const auto k = at (kick, n) * gainOf (-6) * std::sqrt (2.0); const auto sn = at (snare, n - beat / 2); return std::pair { sn, k + sn }; }, 0, beat) < 1e-6,
           "pad volume and pan apply to that pad only");
    editChain (s, rack, kickChain, [] (auto& z) { z.volumeDb = 0; z.pan = 0; z.solo = true; });
    check (error (render (s, out), [&] (int n) { const auto v = at (kick, n); return std::pair { v, v }; }, 0, beat) < 1e-6, "a soloed pad silences the other pads");
    editChain (s, rack, kickChain, [] (auto& z) { z.solo = false; });
    ok (s.swapDrumRackPads (rack, 36, 38), "swap the kick and snare pads");
    check (error (render (s, out), [&] (int n) { const auto v = at (snare, n) + at (kick, n - beat / 2); return std::pair { v, v }; }, 0, beat + beat / 2) < 1e-6,
           "swapping pads exchanges the notes their chains receive");
    s.undo();
    // All Notes: a chain receiving every note plays it unchanged.
    const auto every = addChain (s, rack, "Every");
    editChain (s, rack, every, [] (auto& z) { z.receiveNote = -1; });
    sampler (s, every, fcl, 40);
    // Every note reaches it; E1 at the Sampler's root plays the sample unchanged once the pads have ended.
    check (error (render (s, out), [&] (int n) { const auto v = at (closed, n - beat); return std::pair { v, v }; }, beat - 10, beat + 2400) < 1e-6,
           "an All Notes chain plays the note without a pad at its own pitch");
    ok (s.removeRackChain (every), "remove the All Notes chain");
    // Choke: striking the closed hat fades the open hat over 5 ms.
    clearNotes (s, clip);
    juce::String openChain, closedChain;
    ok (s.setDrumRackPadSample (rack, 46, fo, openChain), "drop the open hat on A#1"); ok (s.setDrumRackPadSample (rack, 42, fcl, closedChain), "drop the closed hat on F#1");
    editChain (s, rack, openChain, [] (auto& z) { z.chokeGroup = 1; }); editChain (s, rack, closedChain, [] (auto& z) { z.chokeGroup = 1; });
    ok (s.addMidiNote (clip, 46, 0, 1, 1), "open hat on beat one"); ok (s.addMidiNote (clip, 42, .2, .25, 1), "closed hat 4800 frames later");
    const auto fade = [] (int n) { return n < 4800 ? 1.0 : std::max (0.0, 1 - (n - 4800 + 1) / 240.0); };
    check (error (render (s, out), [&] (int n) { const auto v = at (open, n) * fade (n) + at (closed, n - 4800); return std::pair { v, v }; }, 0, beat) < 1e-6,
           "a pad in a choke group fades the group's other pad linearly over 240 frames from the strike");
    editChain (s, rack, closedChain, [] (auto& z) { z.chokeGroup = 2; });
    check (error (render (s, out), [&] (int n) { const auto v = at (open, n) + at (closed, n - 4800); return std::pair { v, v }; }, 0, beat) < 1e-6,
           "pads in different choke groups ring together");
    // Sends feed return chains after the chain's volume and pan.
    clearNotes (s, clip); ok (s.addMidiNote (clip, 36, 0, .25, 1), "kick alone");
    juce::String returnId;
    ok (s.addRackReturn (rack, {}, returnId), "add a return chain");
    check (chainOf (s, rack, returnId).settings.name == "Return A" && device (s, rack).returns.size() == 1, "the first return is Return A");
    gainStage (s, returnId, -6);
    check (error (render (s, out), [&] (int n) { const auto v = at (kick, n); return std::pair { v, v }; }, 0, beat) < 1e-6, "sends start off");
    editChain (s, rack, kickChain, [] (auto& z) { z.sendsDb[0] = -12; z.volumeDb = -3; });
    check (error (render (s, out), [&] (int n) { const auto v = at (kick, n) * gainOf (-3) * (1 + gainOf (-12) * gainOf (-6)); return std::pair { v, v }; }, 0, beat) < 1e-6,
           "a send taps the chain after its volume, through the return's devices");
    juce::String secondReturn;
    ok (s.addRackReturn (rack, "Room", secondReturn), "add a second return");
    editChain (s, rack, kickChain, [] (auto& z) { z.sendsDb[1] = -20; });
    ok (s.removeRackReturn (returnId), "delete the first return");
    check (chainOf (s, rack, kickChain).settings.sendsDb[0] == -20 && chainOf (s, rack, kickChain).settings.sendsDb[1] == -100, "later sends move down with their returns");
    check (error (render (s, out), [&] (int n) { const auto v = at (kick, n) * gainOf (-3) * (1 + gainOf (-20)); return std::pair { v, v }; }, 0, beat) < 1e-6,
           "the remaining return still receives its send");
    s.undo();
    juce::String rejected;
    check (s.addRackReturn (s.snapshot().tracks.back().instrument.id + "x", {}, rejected).failed(), "returns need a Drum Rack");
    // Pads clear and the kit reopens with its samples collected.
    ok (s.setDrumRackPadSample (rack, 46, {}, chain), "clear the open hat pad");
    check (chain.isEmpty() && device (s, rack).chains.size() == 3, "clearing a pad removes its chain");
    const auto kit = render (s, out);
    const auto project = root.getChildFile ("Kit").getChildFile ("Kit.halftrack");
    ok (project.getParentDirectory().createDirectory(), "create the kit folder");
    ok (s.saveProject (project), "save the Drum Rack project");
    const auto assets = project.getSiblingFile ("Kit Assets");
    check (chainOf (s, rack, kickChain).devices[0].sample.isAChildOf (assets), "saving collects pad samples into the project's assets");
    const auto moved = root.getChildFile ("Kit moved");
    check (project.getParentDirectory().copyDirectoryTo (moved), "copy the kit elsewhere");
    ok (s.loadProject (moved.getChildFile ("Kit.halftrack")), "reopen the moved kit");
    const auto reopened = s.snapshot().tracks.back().instrument;
    check (reopened.id == rack && reopened.chains.size() == 3 && reopened.returns.size() == 2 && chainOf (s, rack, kickChain).devices[0].sample.isAChildOf (moved),
           "the reopened rack keeps its identities, chains, returns and its own samples");
    check (difference (render (s, out), kit) == 0, "the reopened Drum Rack renders identically");
    juce::String copy;
    ok (s.duplicateTrack (track, copy), "duplicate the drum track");
    const auto duplicate = s.snapshot().tracks.back().instrument;
    check (duplicate.id != rack && duplicate.chains.size() == 3 && duplicate.chains[0].id != reopened.chains[0].id
           && duplicate.chains[0].devices[0].id != reopened.chains[0].devices[0].id, "a duplicated track's rack, chains and devices get new identities");
}

void macroTests (const juce::File& root)
{
    const int frames = 2 * beat;
    std::vector<float> left, right;
    for (int n = 0; n < frames; ++n) { left.push_back ((float) sourceLeft (n)); right.push_back ((float) sourceRight (n)); }
    const auto sourceFile = root.getChildFile ("macro-source.wav"); writeWav (sourceFile, left, right);
    const auto scaled = [&] (double gain) -> Expected { return [&, gain] (int n) { return std::pair<double, double> { gain * left[(size_t) n], gain * right[(size_t) n] }; }; };
    const auto out = root.getChildFile ("macros.wav");
    const int start = 2000, end = frames - 2000;
    Session s (false);
    ok (s.importAudio (sourceFile, 0), "import the macro source");
    ok (s.addNativeEffect (0, Kind::audioEffectRack), "insert a rack");
    const auto rack = s.snapshot().tracks[0].plugins[0].id;
    const auto chain = addChain (s, rack); const auto eq = gainStage (s, chain, 0);
    const auto output = effectParameterIndex (Kind::equalizer, "outputDb");
    ok (s.setRackMacroMapping (rack, { 0, eq, output, -24, 0 }), "map macro 1 to the EQ output from -24 to 0 dB");
    const auto mapped = device (s, rack);
    check (mapped.mappings.size() == 1 && mapped.mappings[0] == Session::MacroMapping { 0, eq, output, -24, 0 } && mapped.effect.values[0] == 127,
           "the mapping is reported and the macro starts where the control was (0 dB is the top of the range)");
    check (error (render (s, out), scaled (1), start, end) < 1e-6, "mapping alone changes nothing");
    const auto setMacro = [&] (int macro, float value)
    { auto v = device (s, rack).effect; v.values[(size_t) macro] = value; ok (s.setNativeEffectSettings (rack, v), "set macro " + juce::String (macro + 1)); };
    setMacro (0, 0);
    check (error (render (s, out), scaled (gainOf (-24)), start, end) < 1e-6 && device (s, eq).effect.values[(size_t) output] == 0,
           "macro at 0 drives the EQ to -24 dB without rewriting its own setting");
    setMacro (0, 63.5f);
    check (error (render (s, out), scaled (gainOf (-12)), start, end) < 1e-6, "halfway between the limits is -12 dB");
    ok (s.setRackMacroMapping (rack, { 0, eq, output, 0, -24 }), "invert the mapping");
    check (device (s, rack).mappings.size() == 1, "remapping the same control replaces its mapping");
    setMacro (0, 127);
    check (error (render (s, out), scaled (gainOf (-24)), start, end) < 1e-6, "an inverted mapping falls as the macro rises");
    // Several targets on one macro; chain volume as a target.
    ok (s.setRackMacroMapping (rack, { 0, chain, 0, -100, 0 }), "map macro 1 to the chain volume as well");
    check (device (s, rack).effect.values[0] == 127, "a macro's later mappings take its current position");
    setMacro (0, 0);
    check (error (render (s, out), scaled (0), start, end) < 1e-6 && peak (render (s, out), 0, frames) == 0, "macro at 0 closes the chain volume");
    setMacro (0, 63.5f);
    const auto halfway = render (s, out);
    // The fader taper puts dB at 20 ln(position) + 6, so halfway between silence and 0 dB
    // (position e^-0.3) is 20 ln(0.5) dB.
    const auto halfVolume = 20 * std::log (.5);
    check (error (halfway, scaled (gainOf (-12) * gainOf (halfVolume)), start, end) < 1e-5, "the chain volume follows the fader taper between its limits");
    ok (s.removeRackMacroMapping (rack, 0, chain, 0), "unmap the chain volume");
    check (std::abs (chainOf (s, rack, chain).settings.volumeDb - halfVolume) < 1e-3
           && difference (render (s, out), halfway) < 1e-6, "unmapping leaves the chain at the volume the macro gave it");
    ok (s.removeRackMacroMapping (rack, 0, eq, output), "unmap the EQ");
    check (std::abs (device (s, eq).effect.values[(size_t) output] + 12) < 1e-4 && device (s, rack).mappings.empty(), "unmapping leaves the EQ at -12 dB");
    s.undo(); s.undo();
    check (device (s, rack).mappings.size() == 2 && device (s, eq).effect.values[(size_t) output] == 0, "undo restores both mappings and the manual value");
    s.redo(); s.redo();
    check (s.setRackMacroMapping (rack, { 16, eq, output, -24, 0 }).failed() && s.setRackMacroMapping (rack, { 0, eq, 999, 0, 1 }).failed()
           && s.setRackMacroMapping (rack, { 0, eq, output, -48, 0 }).failed() && s.setRackMacroMapping (rack, { 0, "missing", 0, 0, 1 }).failed()
           && s.setRackMacroMapping (rack, { 0, chain, 2, -100, 0 }).failed(), "macros 1-16, real controls, in-range limits and existing sends only");
    // Automating a macro drives its targets during playback.
    setControl (s, eq, "outputDb", 0);
    editChain (s, rack, chain, [] (auto& c) { c.volumeDb = 0; });
    ok (s.setRackMacroMapping (rack, { 1, eq, output, -24, 0 }), "map macro 2 to the EQ output");
    Session::AutomationLane lane; lane.points = { { 0, 127 }, { 1, 127 }, { 1.000001, 0 } };
    ok (s.setDeviceAutomation (rack, 1, lane), "automate macro 2 to drop at beat 2");
    check (device (s, rack).automation.size() == 1 && device (s, rack).automation[0].parameter == 1, "macro automation is reported on the rack");
    const auto automated = render (s, out);
    // As for any device control, automation reaches the device at block rate; allow the
    // existing device-automation settling window around the step.
    const int settle = 2400;
    check (error (automated, scaled (1), start, beat - settle) < 1e-6 && error (automated, scaled (gainOf (-24)), beat + settle, end) < 1e-6,
           "the automated macro holds the EQ at 0 dB, then -24 dB after the drop");
    // Save, reopen and reject malformed racks.
    const auto project = root.getChildFile ("Macros.halftrack");
    ok (s.saveProject (project), "save the macro project");
    ok (s.loadProject (project), "reopen the macro project");
    check (device (s, rack).mappings.size() == 1 && device (s, rack).automation.size() == 1 && difference (render (s, out), automated) == 0,
           "mappings and macro automation reopen and play the same");
    const auto text = project.loadFileAsString();
    const auto rejects = [&] (const juce::String& changed, const juce::String& what)
    {
        const auto broken = root.getChildFile ("Broken.halftrack");
        check (changed != text && broken.replaceWithText (changed), "write a project with " + what);
        const auto result = s.loadProject (broken);
        check (result.failed() && device (s, rack).mappings.size() == 1, "a project with " + what + " is rejected and the open project kept (" + result.getErrorMessage() + ")");
    };
    rejects (text.replaceFirstOccurrenceOf ("keyLow=\"0\"", "keyLow=\"100\""), "a key zone whose low is above its fade");
    rejects (text.replaceFirstOccurrenceOf ("volumeDb=\"", "volumeDb=\"1"), "a chain volume above +6 dB");
    rejects (text.replace ("targetID=\"" + eq + "\"", "targetID=\"12345\""), "a mapping to a missing device");
    rejects (text.replaceFirstOccurrenceOf ("macro=\"1\"", "macro=\"16\""), "a mapping to a seventeenth macro");
    rejects (text.replaceFirstOccurrenceOf ("effectKind=\"0\"", "effectKind=\"28\""), "an instrument inside an audio chain");
    rejects (text.replaceFirstOccurrenceOf ("<RACKCHAIN ", "<RACKRETURN ").replaceFirstOccurrenceOf ("</RACKCHAIN>", "</RACKRETURN>"), "a return chain in an Audio Effect Rack");
    rejects (text.replaceFirstOccurrenceOf ("<RACKCHAIN ", "<PLUGIN type=\"halftrackNativeEffect\" effectKind=\"0\" id=\"98765\"/><RACKCHAIN "), "a device outside the rack's chains");
    const auto chainStart = text.indexOf ("<RACKCHAIN ");
    const auto chainEnd = text.indexOf (chainStart, "</RACKCHAIN>") + 12;
    rejects (text.replaceSection (chainEnd, 0, text.substring (chainStart, chainEnd)), "two chains with the same identity");
}

// The rack callback with notes, chokes, sends, macros and a newly published layout allocates nothing.
void allocationTests (const juce::File& root)
{
    struct Quiet final : te::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override { return false; }
        bool usePhysicalMidiDevices() override { return false; }
        bool addSystemAudioIODeviceTypes() override { return false; }
        bool canScanPluginsOutOfProcess() override { return false; }
    };
    // Settings and temporary files stay in this run's folder, apart from other checkouts' runs.
    struct Storage final : te::PropertyStorage
    {
        explicit Storage (juce::File directory) : PropertyStorage ("Halftrack Rack Audit"), folder (std::move (directory)) {}
        juce::File getAppPrefsFolder() override { return folder; }
        juce::File folder;
    };
    const auto engineFolder = root.getChildFile ("Audit engine");
    ok (engineFolder.createDirectory(), "create the audit engine's folder");
    te::Engine engine (std::make_unique<Storage> (engineFolder), nullptr, std::make_unique<Quiet>());
    engine.getPluginManager().createBuiltInType<NativeEffectPlugin>();
    auto edit = te::Edit::createSingleTrackEdit (engine);
    const auto sample = root.getChildFile ("audit.wav");
    const auto data = hit (4800, 440, 800, 0); writeWav (sample, data, data);
    const auto deviceState = [&] (Kind kind)
    {
        juce::ValueTree v (te::IDs::PLUGIN); v.setProperty (te::IDs::type, NativeEffectPlugin::xmlTypeName, nullptr);
        v.setProperty ("effectKind", (int) kind, nullptr); edit->createNewItemID().writeID (v, nullptr); return v;
    };
    auto rackState = deviceState (Kind::drumRack);
    juce::StringArray samplers;
    for (int pad = 0; pad < 8; ++pad)
    {
        juce::ValueTree chain (racks::chainType); edit->createNewItemID().writeID (chain, nullptr);
        Session::RackChainSettings settings; settings.receiveNote = 36 + pad; settings.chokeGroup = pad % 3; settings.sendsDb[0] = -6;
        racks::writeChain (chain, settings, nullptr);
        auto instrument = deviceState (Kind::sampler); instrument.setProperty (samplerSampleProperty, sample.getFullPathName(), nullptr);
        samplers.add (instrument[te::IDs::id].toString());
        chain.appendChild (deviceState (Kind::chord), nullptr); chain.appendChild (instrument, nullptr);
        chain.appendChild (deviceState (pad % 4 == 0 ? Kind::delay : Kind::saturator), nullptr);
        rackState.appendChild (chain, nullptr);
    }
    juce::ValueTree returns (racks::returnType); edit->createNewItemID().writeID (returns, nullptr);
    racks::writeChain (returns, {}, nullptr); returns.appendChild (deviceState (Kind::reverb), nullptr); rackState.appendChild (returns, nullptr);
    rackState.appendChild (racks::makeMapping ({ 0, samplers[0], instruments::sampler::transpose, -12, 12 }), nullptr);
    rackState.appendChild (racks::makeMapping ({ 1, rackState.getChild (0)[te::IDs::id].toString(), 0, -100, 0 }), nullptr);
    auto plugin = edit->getPluginCache().createNewPlugin (rackState);
    auto* rack = dynamic_cast<NativeEffectPlugin*> (plugin.get());
    check (rack != nullptr && rack->rackEngine() != nullptr && rack->rackEngine()->devices().size() == 25, "build a Drum Rack of eight three-device pads and a return");
    plugin->baseClassInitialise ({ te::TimePosition(), rate, 512 });
    Buffer audio (2, 512); te::MidiMessageArray midi; midi.reserve (512);
    bool finite = true; std::size_t heap = 0;
    const auto block = [&] (int index)
    {
        audio.clear(); midi.clear();
        for (int note = 0; note < 24; ++note)
            midi.addMidiMessage (note % 3 == 2 ? juce::MidiMessage::noteOff (1, 36 + (note + index) % 8) : juce::MidiMessage::noteOn (1, 36 + (note + index) % 8, (juce::uint8) (40 + note)),
                                 note * 20 / rate, {});
        midi.addMidiMessage (juce::MidiMessage::controllerEvent (1, 64, index % 2 ? 127 : 0), 300 / rate, {});
        const te::PluginRenderContext context (&audio, 0, 512, &midi, 0, { te::TimePosition::fromSeconds (index * 512 / rate), te::TimePosition::fromSeconds ((index + 1) * 512 / rate) },
                                               true, false, true, false);
        const auto before = allocationAudit::allocations + allocationAudit::deallocations;
        allocationAudit::enabled = true;
        plugin->applyToBufferWithAutomation (context);
        allocationAudit::enabled = false;
        heap += allocationAudit::allocations + allocationAudit::deallocations - before;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < 512; ++n) finite = finite && std::isfinite (audio.getSample (c, n));
    };
    for (int i = 0; i < 4; ++i) block (i); // Let devices adopt their first parameter packets.
    heap = 0;
    for (int i = 4; i < 40; ++i) block (i);
    check (heap == 0 && finite, "36 blocks of note floods, chokes, sends and macro-driven controls make no heap calls");
    // A layout published from the message thread is adopted without heap calls.
    auto settings = racks::readChain (rackState.getChild (1)); settings.volumeDb = -3;
    auto second = rackState.getChild (1); racks::writeChain (second, settings, nullptr);
    heap = 0;
    for (int i = 40; i < 48; ++i) block (i);
    check (heap == 0 && finite, "adopting a new chain layout makes no heap calls");
    plugin->baseClassDeinitialise();
}
}
int runRackTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("racks-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        zoneTests();
        audioRackTests (root);
        instrumentRackTests (root);
        drumRackTests (root);
        macroTests (root);
        allocationTests (root);
        std::cout << "Racks passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL racks: " << e.what() << std::endl; return 1; }
}
}
