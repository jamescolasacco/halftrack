#include "Racks.h"
#include "NativeEffects.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace halftrack::racks
{
namespace
{
namespace te = tracktion;
constexpr double pi = 3.14159265358979323846;
const juce::Identifier volumeProperty { "volumeDb" }, panProperty { "pan" }, activeProperty { "active" }, soloProperty { "solo" },
    receiveProperty { "receiveNote" }, playProperty { "playNote" }, chokeProperty { "chokeGroup" },
    macroProperty { "macro" }, parameterProperty { "parameter" }, minimumProperty { "minimum" }, maximumProperty { "maximum" };
const std::array<juce::Identifier, 4> keyProperties { juce::Identifier ("keyLow"), juce::Identifier ("keyHigh"), juce::Identifier ("keyFadeLow"), juce::Identifier ("keyFadeHigh") };
const std::array<juce::Identifier, 4> velocityProperties { juce::Identifier ("velocityLow"), juce::Identifier ("velocityHigh"), juce::Identifier ("velocityFadeLow"), juce::Identifier ("velocityFadeHigh") };
const std::array<juce::Identifier, 4> selectProperties { juce::Identifier ("selectLow"), juce::Identifier ("selectHigh"), juce::Identifier ("selectFadeLow"), juce::Identifier ("selectFadeHigh") };
juce::Identifier sendProperty (int index) { return juce::Identifier ("send" + juce::String (index + 1)); }
const float notMapped = std::numeric_limits<float>::quiet_NaN();

// Strict parsing of a saved property: the whole text must be a finite number in range.
bool parse (const juce::var& value, double low, double high, bool integral, double& output)
{
    const auto text = value.toString().trim().toStdString(); char* end = nullptr;
    output = std::strtod (text.c_str(), &end);
    return ! text.empty() && end == text.c_str() + text.size() && std::isfinite (output)
        && output >= low && output <= high && (! integral || std::floor (output) == output);
}
bool optional (const juce::ValueTree& tree, const juce::Identifier& key, double low, double high, bool integral)
{
    double value = 0;
    return ! tree.hasProperty (key) || parse (tree[key], low, high, integral, value);
}
Session::RackZone readZone (const juce::ValueTree& tree, const std::array<juce::Identifier, 4>& keys, Session::RackZone zone)
{
    zone.low = tree.getProperty (keys[0], zone.low); zone.high = tree.getProperty (keys[1], zone.high);
    zone.fadeLow = tree.getProperty (keys[2], zone.fadeLow); zone.fadeHigh = tree.getProperty (keys[3], zone.fadeHigh);
    return zone;
}
void writeZone (juce::ValueTree& tree, const std::array<juce::Identifier, 4>& keys, const Session::RackZone& zone, juce::UndoManager* undo)
{
    tree.setProperty (keys[0], zone.low, undo); tree.setProperty (keys[1], zone.high, undo);
    tree.setProperty (keys[2], zone.fadeLow, undo); tree.setProperty (keys[3], zone.fadeHigh, undo);
}
bool canonicalId (const juce::var& value)
{
    const auto id = te::EditItemID::fromVar (value);
    return id.isValid() && id.toString() == value.toString();
}
Kind kindOf (const juce::ValueTree& plugin) { return static_cast<Kind> (static_cast<int> (plugin.getProperty ("effectKind", 0))); }
bool nativeDevice (const juce::ValueTree& tree)
{ return tree.hasType (te::IDs::PLUGIN) && tree[te::IDs::type].toString() == NativeEffectPlugin::xmlTypeName; }
}

bool isRack (Kind kind) noexcept { return static_cast<int> (kind) >= static_cast<int> (Kind::instrumentRack) && static_cast<int> (kind) < Session::deviceKindCount; }
int deviceStage (Kind kind) noexcept { return effectIsMidi (kind) ? 0 : effectIsInstrument (kind) ? 1 : 2; }
bool chainAccepts (Kind rack, Kind device) noexcept
{
    if (static_cast<int> (device) < 0 || static_cast<int> (device) >= Session::deviceKindCount) return false;
    switch (rack)
    {
        case Kind::audioEffectRack: return deviceStage (device) == 2;
        case Kind::midiEffectRack: return deviceStage (device) == 0;
        case Kind::instrumentRack: case Kind::drumRack: return true;
        default: return false;
    }
}
bool validZone (const Session::RackZone& z, int minimum, int maximum) noexcept
{ return minimum <= z.low && z.low <= z.fadeLow && z.fadeLow <= z.fadeHigh && z.fadeHigh <= z.high && z.high <= maximum; }
bool inZone (const Session::RackZone& z, int value) noexcept { return value >= z.low && value <= z.high; }
double zoneGain (const Session::RackZone& z, int value) noexcept
{
    if (! inZone (z, value)) return 0;
    if (value < z.fadeLow) return (value - z.low + 1) / static_cast<double> (z.fadeLow - z.low + 1);
    if (value > z.fadeHigh) return (z.high - value + 1) / static_cast<double> (z.high - z.fadeHigh + 1);
    return 1;
}
double selectorOutputGain (const Session::RackZone& z, int selector) noexcept
{
    if (selector < z.low) return z.fadeLow > z.low ? 0.0 : 1.0;
    if (selector > z.high) return z.fadeHigh < z.high ? 0.0 : 1.0;
    return zoneGain (z, selector);
}
std::pair<double, double> panGains (double pan) noexcept
{
    const auto p = (std::clamp (pan, -1.0, 1.0) + 1) * .5;
    return { std::sqrt (2.0) * std::sin ((1 - p) * pi * .5), std::sqrt (2.0) * std::sin (p * pi * .5) };
}
double decibelGain (double decibels) noexcept { return decibels <= -100 ? 0.0 : std::pow (10.0, decibels / 20.0); }

Session::RackChainSettings readChain (const juce::ValueTree& tree)
{
    Session::RackChainSettings s;
    s.name = tree[te::IDs::name].toString();
    s.volumeDb = tree.getProperty (volumeProperty, s.volumeDb); s.pan = tree.getProperty (panProperty, s.pan);
    s.active = tree.getProperty (activeProperty, s.active); s.solo = tree.getProperty (soloProperty, s.solo);
    s.key = readZone (tree, keyProperties, s.key); s.velocity = readZone (tree, velocityProperties, s.velocity);
    s.select = readZone (tree, selectProperties, s.select);
    s.receiveNote = tree.getProperty (receiveProperty, s.receiveNote); s.playNote = tree.getProperty (playProperty, s.playNote);
    s.chokeGroup = tree.getProperty (chokeProperty, s.chokeGroup);
    for (int i = 0; i < Session::maximumRackReturns; ++i) s.sendsDb[(size_t) i] = tree.getProperty (sendProperty (i), s.sendsDb[(size_t) i]);
    return s;
}
void writeChain (juce::ValueTree& tree, const Session::RackChainSettings& s, juce::UndoManager* undo)
{
    tree.setProperty (te::IDs::name, s.name, undo);
    tree.setProperty (volumeProperty, s.volumeDb, undo); tree.setProperty (panProperty, s.pan, undo);
    tree.setProperty (activeProperty, s.active, undo); tree.setProperty (soloProperty, s.solo, undo);
    writeZone (tree, keyProperties, s.key, undo); writeZone (tree, velocityProperties, s.velocity, undo); writeZone (tree, selectProperties, s.select, undo);
    tree.setProperty (receiveProperty, s.receiveNote, undo); tree.setProperty (playProperty, s.playNote, undo);
    tree.setProperty (chokeProperty, s.chokeGroup, undo);
    for (int i = 0; i < Session::maximumRackReturns; ++i) tree.setProperty (sendProperty (i), s.sendsDb[(size_t) i], undo);
}
bool validChain (const Session::RackChainSettings& s, Kind, bool) noexcept
{
    const auto within = [] (float v, float low, float high) { return std::isfinite (v) && v >= low && v <= high; };
    if (s.name.length() > 256 || ! within (s.volumeDb, -100, 6) || ! within (s.pan, -1, 1)) return false;
    if (! validZone (s.key, 0, 127) || ! validZone (s.velocity, 1, 127) || ! validZone (s.select, 0, 127)) return false;
    if (s.receiveNote < -1 || s.receiveNote > 127 || s.playNote < 0 || s.playNote > 127 || s.chokeGroup < 0 || s.chokeGroup > 16) return false;
    for (const auto send : s.sendsDb) if (! within (send, -100, 0)) return false;
    return true;
}
Session::MacroMapping readMapping (const juce::ValueTree& tree)
{
    Session::MacroMapping m;
    m.macro = tree.getProperty (macroProperty, 0); m.targetId = tree[te::IDs::targetID].toString();
    m.parameter = tree.getProperty (parameterProperty, 0);
    m.minimum = tree.getProperty (minimumProperty, 0.0f); m.maximum = tree.getProperty (maximumProperty, 1.0f);
    return m;
}
juce::ValueTree makeMapping (const Session::MacroMapping& m)
{
    juce::ValueTree tree (mappingType);
    tree.setProperty (macroProperty, m.macro, nullptr); tree.setProperty (te::IDs::targetID, m.targetId, nullptr);
    tree.setProperty (parameterProperty, m.parameter, nullptr);
    tree.setProperty (minimumProperty, m.minimum, nullptr); tree.setProperty (maximumProperty, m.maximum, nullptr);
    return tree;
}
std::optional<std::pair<float, float>> targetRange (Kind targetKind, bool chainTarget, bool rackTarget, int parameter) noexcept
{
    if (rackTarget) return parameter == Session::rackChainSelector ? std::optional<std::pair<float, float>> ({ 0.0f, 127.0f }) : std::nullopt;
    if (chainTarget)
    {
        if (parameter == 0) return std::pair { -100.0f, 6.0f };
        if (parameter == 1) return std::pair { -1.0f, 1.0f };
        if (parameter >= 2 && parameter < 2 + Session::maximumRackReturns) return std::pair { -100.0f, 0.0f };
        return std::nullopt;
    }
    const auto fields = effectParameters (targetKind);
    if (parameter < 0 || parameter >= (int) fields.size()) return std::nullopt;
    return std::pair { fields[(size_t) parameter].minimum, fields[(size_t) parameter].maximum };
}
float chainValueToProportion (int parameter, float value) noexcept
{
    if (parameter == 1) return std::clamp ((value + 1) * .5f, 0.0f, 1.0f);
    // Volume and sends follow the track fader taper; sends top out at 0 dB.
    const auto top = te::decibelsToVolumeFaderPosition (parameter == 0 ? 6.0f : 0.0f);
    return std::clamp (te::decibelsToVolumeFaderPosition (value) / top, 0.0f, 1.0f);
}
float chainProportionToValue (int parameter, float proportion) noexcept
{
    proportion = std::clamp (proportion, 0.0f, 1.0f);
    if (parameter == 1) return proportion * 2 - 1;
    const auto top = te::decibelsToVolumeFaderPosition (parameter == 0 ? 6.0f : 0.0f);
    return std::clamp (te::volumeFaderPositionToDB (proportion * top), -100.0f, parameter == 0 ? 6.0f : 0.0f);
}

juce::Result validateRack (const juce::ValueTree& rack)
{
    const auto kind = kindOf (rack);
    if (! isRack (kind)) return juce::Result::ok();
    int depth = 1;
    for (auto parent = rack.getParent(); parent.isValid(); parent = parent.getParent())
        if (nativeDevice (parent) && isRack (kindOf (parent))) ++depth;
    if (depth > maximumDepth) return juce::Result::fail ("The project nests racks more deeply than supported.");
    const auto fail = [] { return juce::Result::fail ("The project contains an invalid rack."); };
    if (! optional (rack, visibleMacrosProperty, 1, Session::rackMacroCount, true)) return fail();
    int chains = 0, returns = 0, mappings = 0;
    std::set<juce::String> ids { rack[te::IDs::id].toString() };
    std::map<juce::String, std::pair<Kind, int>> targets; // id -> (kind, 0 device / 1 chain / 2 return)
    for (const auto& child : rack)
    {
        const bool isChain = child.hasType (chainType), isReturn = child.hasType (returnType);
        if (child.hasType (te::IDs::PLUGIN)) return fail();
        if (child.hasType (mappingType)) { ++mappings; continue; }
        if (! isChain && ! isReturn) continue;
        if (isReturn && kind != Kind::drumRack) return fail();
        if (++(isChain ? chains : returns) > (isChain ? Session::maximumRackChains : Session::maximumRackReturns)) return fail();
        if (! canonicalId (child[te::IDs::id]) || ! ids.insert (child[te::IDs::id].toString()).second) return fail();
        targets[child[te::IDs::id].toString()] = { kind, isChain ? 1 : 2 };
        // Every saved chain property is checked strictly; missing ones take their defaults.
        double value = 0;
        if ((child.hasProperty (volumeProperty) && ! parse (child[volumeProperty], -100, 6, false, value))
            || (child.hasProperty (panProperty) && ! parse (child[panProperty], -1, 1, false, value))
            || ! optional (child, activeProperty, 0, 1, true) || ! optional (child, soloProperty, 0, 1, true)
            || ! optional (child, receiveProperty, -1, 127, true) || ! optional (child, playProperty, 0, 127, true)
            || ! optional (child, chokeProperty, 0, 16, true)) return fail();
        for (const auto* keys : { &keyProperties, &velocityProperties, &selectProperties })
            for (const auto& key : *keys) if (! optional (child, key, 0, 127, true)) return fail();
        for (int i = 0; i < Session::maximumRackReturns; ++i) if (! optional (child, sendProperty (i), -100, 0, false)) return fail();
        if (! validChain (readChain (child), kind, isReturn)) return fail();
        int devices = 0, stage = 0, instruments = 0;
        for (const auto& device : child)
        {
            if (! device.hasType (te::IDs::PLUGIN)) continue;
            if (! nativeDevice (device) || ++devices > Session::maximumChainDevices) return fail();
            double type = 0;
            if (! parse (device.getProperty ("effectKind", 0), 0, Session::deviceKindCount - 1, true, type)) return fail();
            const auto deviceKind = static_cast<Kind> (static_cast<int> (type));
            const int next = deviceStage (deviceKind);
            if (! chainAccepts (isReturn ? Kind::audioEffectRack : kind, deviceKind) || next < stage || (next == 1 && ++instruments > 1)) return fail();
            stage = next;
            if (! canonicalId (device[te::IDs::id]) || ! ids.insert (device[te::IDs::id].toString()).second) return fail();
            targets[device[te::IDs::id].toString()] = { deviceKind, 0 };
        }
    }
    if (mappings > Session::maximumMacroMappings) return fail();
    std::set<std::pair<juce::String, int>> mapped;
    for (const auto& child : rack)
    {
        if (! child.hasType (mappingType)) continue;
        double macro = 0, parameter = 0, minimum = 0, maximum = 0;
        if (! parse (child.getProperty (macroProperty, 0), 0, Session::rackMacroCount - 1, true, macro)
            || ! parse (child.getProperty (parameterProperty, 0), 0, controlCount - 1, true, parameter)) return fail();
        const auto target = child[te::IDs::targetID].toString();
        const bool self = target == rack[te::IDs::id].toString();
        const auto found = targets.find (target);
        if (! self && found == targets.end()) return fail();
        const bool chain = ! self && found->second.second != 0;
        // A return chain has volume and pan but no sends.
        if (chain && found->second.second == 2 && parameter > 1) return fail();
        const auto range = targetRange (self ? kind : found->second.first, chain, self, (int) parameter);
        if (! range || ! parse (child.getProperty (minimumProperty, range->first), range->first, range->second, false, minimum)
            || ! parse (child.getProperty (maximumProperty, range->second), range->first, range->second, false, maximum)
            || ! mapped.insert ({ target, (int) parameter }).second) return fail();
    }
    return juce::Result::ok();
}

Located locate (const Session::Snapshot& snapshot, const juce::String& id)
{
    const std::function<Located (const std::vector<Session::PluginInfo>&, const Session::PluginInfo*, const Session::TrackInfo&)> search
        = [&] (const std::vector<Session::PluginInfo>& devices, const Session::PluginInfo* rack, const Session::TrackInfo& track) -> Located
    {
        for (const auto& d : devices)
        {
            if (d.id == id) return { &d, rack, &track };
            for (const auto* list : { &d.chains, &d.returns })
                for (const auto& c : *list) if (auto found = search (c.devices, &d, track); found.device) return found;
        }
        return {};
    };
    for (const auto& track : snapshot.tracks)
    {
        if (track.instrument.isNativeEffect)
        {
            if (track.instrument.id == id) return { &track.instrument, nullptr, &track };
            for (const auto* list : { &track.instrument.chains, &track.instrument.returns })
                for (const auto& c : *list) if (auto found = search (c.devices, &track.instrument, track); found.device) return found;
        }
        if (auto found = search (track.plugins, nullptr, track); found.device) return found;
    }
    return {};
}
int macroOf (const Session::PluginInfo& rack, const juce::String& targetId, int parameter)
{
    for (const auto& m : rack.mappings) if (m.targetId == targetId && m.parameter == parameter) return m.macro;
    return -1;
}
std::optional<float> macroValue (const Session::PluginInfo& rack, const juce::String& targetId, int parameter)
{
    for (const auto& m : rack.mappings)
    {
        if (m.targetId != targetId || m.parameter != parameter) continue;
        const auto macro = std::clamp (rack.effect.values[(size_t) m.macro] / 127.0f, 0.0f, 1.0f);
        if (targetId == rack.id) return std::round (m.minimum + (m.maximum - m.minimum) * macro);
        for (const auto* list : { &rack.chains, &rack.returns })
            for (const auto& c : *list)
            {
                if (c.id == targetId)
                {
                    const auto low = chainValueToProportion (parameter, m.minimum), high = chainValueToProportion (parameter, m.maximum);
                    return chainProportionToValue (parameter, low + (high - low) * macro);
                }
                for (const auto& d : c.devices)
                    if (d.id == targetId)
                    {
                        const auto low = effectValueToProportion (d.effect.kind, parameter, m.minimum), high = effectValueToProportion (d.effect.kind, parameter, m.maximum);
                        return effectProportionToValue (d.effect.kind, parameter, low + (high - low) * macro);
                    }
            }
    }
    return std::nullopt;
}
juce::String macroName (const Session::PluginInfo& rack, int macro)
{
    const Session::MacroMapping* only = nullptr; int count = 0;
    for (const auto& m : rack.mappings) if (m.macro == macro) { only = &m; ++count; }
    if (count == 1)
    {
        if (only->targetId == rack.id) return "Chain Selector";
        for (const auto* list : { &rack.chains, &rack.returns })
            for (const auto& c : *list)
            {
                if (c.id == only->targetId)
                    return c.settings.name + (only->parameter == 0 ? " Volume" : only->parameter == 1 ? " Pan" : " Send " + juce::String::charToString ((juce::juce_wchar) ('A' + only->parameter - 2)));
                for (const auto& d : c.devices)
                    if (d.id == only->targetId)
                        if (const auto fields = effectParameters (d.effect.kind); only->parameter >= 0 && only->parameter < (int) fields.size())
                            return fields[(size_t) only->parameter].label;
            }
    }
    return "Macro " + juce::String (macro + 1);
}

void RackEngine::Ramp::set (double next, int length, bool immediate) noexcept
{
    if (immediate) { value = target = next; remaining = 0; return; }
    if (next == target) return;
    target = next; remaining = std::max (1, length);
}
double RackEngine::Ramp::tick() noexcept
{
    if (remaining > 0) { value += (target - value) / remaining; if (--remaining == 0) value = target; }
    return value;
}

RackEngine::RackEngine (NativeEffectPlugin& o) : owner (o), kind (o.getKind())
{
    chainMidi.reserve (2560); midiOut.reserve (4096);
}
RackEngine::~RackEngine()
{
    for (auto& plugin : initialised) plugin->baseClassDeinitialise();
    initialised.clear();
    published.store (nullptr); acknowledged.store (nullptr); active = nullptr;
    layouts.clear(); current.clear();
}
void RackEngine::structureChanged()
{
    JUCE_ASSERT_MESSAGE_THREAD
    std::vector<te::Plugin::Ptr> next;
    for (const auto& chain : owner.state)
        if (chain.hasType (chainType) || chain.hasType (returnType))
            for (const auto& device : chain)
                if (nativeDevice (device))
                    if (auto plugin = owner.edit.getPluginCache().getOrCreatePluginFor (device); plugin != nullptr && dynamic_cast<NativeEffectPlugin*> (plugin.get()) != nullptr)
                        next.push_back (plugin);
    current = std::move (next);
    if (info)
        for (auto& plugin : current)
            if (std::find (initialised.begin(), initialised.end(), plugin) == initialised.end())
            {
                // Devices joining a running rack are prepared before the audio thread can see them.
                plugin->baseClassInitialise (*info);
                initialised.push_back (plugin);
            }
    publish();
}
std::unique_ptr<RackEngine::Layout> RackEngine::build()
{
    auto layout = std::make_unique<Layout>();
    layout->slotChain.fill (-1);
    const auto sampleRate = info ? info->sampleRate : 48000.0;
    const auto slotFor = [this] (const juce::String& id)
    {
        for (int i = 0; i < maximumSlots; ++i) if (slotIds[(size_t) i] == id) return i;
        for (int i = 0; i < maximumSlots; ++i)
            if (slotIds[(size_t) i].isEmpty()) { slotIds[(size_t) i] = id; ++slotGenerations[(size_t) i]; return i; }
        return -1;
    };
    const auto add = [&] (const juce::ValueTree& tree, bool isReturn)
    {
        Chain chain; chain.isReturn = isReturn; chain.settings = readChain (tree);
        chain.slot = slotFor (tree[te::IDs::id].toString());
        if (chain.slot < 0) return;
        chain.generation = slotGenerations[(size_t) chain.slot];
        for (const auto& device : tree)
            for (auto& plugin : current)
                if (plugin->state == device)
                {
                    auto* native = dynamic_cast<NativeEffectPlugin*> (plugin.get());
                    chain.devices.push_back ({ plugin.get(), native, native->getKind(), plugin->isEnabled(), -1 });
                    if (plugin->isEnabled()) chain.latency += static_cast<int> (std::lround (native->getLatencySeconds() * sampleRate));
                    layout->holders.push_back (plugin);
                }
        (isReturn ? layout->returns : layout->chains).push_back (std::move (chain));
    };
    for (const auto& child : owner.state)
    {
        if (child.hasType (chainType)) add (child, false);
        else if (child.hasType (returnType)) add (child, true);
    }
    int chainLatency = 0, returnLatency = 0;
    for (const auto& c : layout->chains) chainLatency = std::max (chainLatency, c.latency);
    for (const auto& r : layout->returns) returnLatency = std::max (returnLatency, r.latency);
    for (auto* list : { &layout->chains, &layout->returns })
        for (auto& c : *list)
        {
            c.compensation = (c.isReturn ? returnLatency : chainLatency) - c.latency;
            for (auto& line : c.delay) line.assign ((size_t) c.compensation, 0.0f);
        }
    for (auto& line : layout->dryDelay) line.assign ((size_t) returnLatency, 0.0f);
    layout->latency = chainLatency + returnLatency;
    for (size_t i = 0; i < layout->chains.size(); ++i) layout->slotChain[(size_t) layout->chains[i].slot] = (int) i;
    for (size_t i = 0; i < layout->returns.size(); ++i) layout->slotChain[(size_t) layout->returns[i].slot] = (int) (layout->chains.size() + i);
    for (const auto& c : layout->chains) layout->anySolo = layout->anySolo || c.settings.solo;
    // Mappings resolve to a device's control array, a chain slot or the selector.
    for (const auto& child : owner.state)
    {
        if (! child.hasType (mappingType)) continue;
        const auto m = readMapping (child);
        if (m.macro < 0 || m.macro >= Session::rackMacroCount) continue;
        Mapping resolved; resolved.macro = m.macro; resolved.parameter = m.parameter;
        bool found = false;
        if (m.targetId == owner.itemID.toString() && m.parameter == Session::rackChainSelector)
        {
            resolved.target = Mapping::Target::selector;
            resolved.minimum = m.minimum / 127; resolved.maximum = m.maximum / 127; found = true;
        }
        for (auto* list : { &layout->chains, &layout->returns })
            for (auto& c : *list)
            {
                if (found) break;
                if (slotIds[(size_t) c.slot] == m.targetId && m.parameter >= 0 && m.parameter < 2 + Session::maximumRackReturns)
                {
                    resolved.target = m.parameter == 0 ? Mapping::Target::chainVolume : m.parameter == 1 ? Mapping::Target::chainPan : Mapping::Target::chainSend;
                    resolved.index = c.slot;
                    resolved.minimum = chainValueToProportion (m.parameter, m.minimum); resolved.maximum = chainValueToProportion (m.parameter, m.maximum);
                    found = true; break;
                }
                for (auto& device : c.devices)
                    if (device.plugin->itemID.toString() == m.targetId && m.parameter >= 0 && m.parameter < (int) effectParameters (device.kind).size())
                    {
                        if (device.mapped < 0)
                        {
                            device.mapped = (int) layout->values.size();
                            std::array<float, controlCount> values; values.fill (notMapped);
                            layout->values.push_back (values);
                        }
                        resolved.target = Mapping::Target::device; resolved.index = device.mapped; resolved.kind = device.kind;
                        resolved.minimum = effectValueToProportion (device.kind, m.parameter, m.minimum);
                        resolved.maximum = effectValueToProportion (device.kind, m.parameter, m.maximum);
                        found = true; break;
                    }
            }
        if (found) layout->mappings.push_back (resolved);
    }
    return layout;
}
void RackEngine::publish (bool restartGraph)
{
    auto layout = build();
    const auto sampleRate = info ? info->sampleRate : 48000.0;
    const auto nextLatency = layout->latency / sampleRate;
    double chainTail = 0, returnTail = 0;
    for (const auto* list : { &layout->chains, &layout->returns })
        for (const auto& c : *list)
        {
            double sum = 0;
            for (const auto& d : c.devices) if (d.enabled) sum += d.plugin->getTailLength();
            (c.isReturn ? returnTail : chainTail) = std::max (c.isReturn ? returnTail : chainTail, sum);
        }
    tail = chainTail + returnTail;
    auto* raw = layout.get();
    layouts.push_back (std::move (layout));
    published.store (raw, std::memory_order_release);
    const bool latencyChanged = std::abs (nextLatency - latency) > 1e-12;
    latency = nextLatency;
    // The engine compensates a device's latency when it builds the graph.
    if (latencyChanged && info && restartGraph) owner.edit.restartPlayback();
    retire();
}
void RackEngine::retire()
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto* latest = published.load (std::memory_order_acquire);
    // Without a graph nothing processes the rack; otherwise wait for the audio thread.
    if (info && acknowledged.load (std::memory_order_acquire) != latest) return;
    std::erase_if (layouts, [latest] (const auto& layout) { return layout.get() != latest; });
    for (int i = 0; i < maximumSlots; ++i)
        if (slotIds[(size_t) i].isNotEmpty() && (latest == nullptr || latest->slotChain[(size_t) i] < 0)) slotIds[(size_t) i].clear();
    std::erase_if (initialised, [this] (const auto& plugin)
    {
        if (std::find (current.begin(), current.end(), plugin) != current.end()) return false;
        plugin->baseClassDeinitialise(); return true;
    });
}
void RackEngine::initialise (const te::PluginInitialisationInfo& next)
{
    JUCE_ASSERT_MESSAGE_THREAD
    info = next; rate = next.sampleRate > 0 ? next.sampleRate : 48000.0;
    rampLength = std::max (1, static_cast<int> (std::lround (rate * rampSeconds)));
    capacity = std::max (next.blockSizeSamples, 64);
    input.setSize (2, capacity); scratch.setSize (2, capacity); mix.setSize (2, capacity);
    for (auto& r : returnMix) r.setSize (kind == Kind::drumRack ? 2 : 0, kind == Kind::drumRack ? capacity : 0);
    // No graph processes the rack while it initialises, so its audio state can be reset here.
    slots = {}; heldCount = 0; routedCount = 0; pendingChokeCount = 0; active = nullptr;
    acknowledged.store (nullptr, std::memory_order_release);
    for (auto& plugin : current)
        if (std::find (initialised.begin(), initialised.end(), plugin) == initialised.end())
        { plugin->baseClassInitialise (next); initialised.push_back (plugin); }
    // The graph being built reads this latency right after initialisation.
    publish (false);
}
void RackEngine::deinitialise()
{
    JUCE_ASSERT_MESSAGE_THREAD
    for (auto& plugin : initialised) plugin->baseClassDeinitialise();
    initialised.clear();
    info.reset(); active = nullptr;
    acknowledged.store (nullptr, std::memory_order_release);
    retire();
}
void RackEngine::reset() { for (auto& plugin : current) plugin->reset(); }
void RackEngine::prepareForOfflineRender()
{ for (auto* device : devices()) device->prepareForOfflineRender(); }
void RackEngine::refreshAutomation()
{ for (auto* device : devices()) device->refreshAutomation(); }
std::vector<NativeEffectPlugin*> RackEngine::devices() const
{
    std::vector<NativeEffectPlugin*> result;
    for (const auto& plugin : current) if (auto* native = dynamic_cast<NativeEffectPlugin*> (plugin.get())) result.push_back (native);
    return result;
}
NativeEffectPlugin* RackEngine::findDevice (const juce::String& id) const
{
    for (auto* device : devices())
    {
        if (device->itemID.toString() == id) return device;
        if (auto* nested = device->rackEngine()) if (auto* found = nested->findDevice (id)) return found;
    }
    return nullptr;
}

void RackEngine::chokeAt (int sample) noexcept
{ if (pendingChokeCount < (int) pendingChokes.size()) pendingChokes[(size_t) pendingChokeCount++] = sample; }
void RackEngine::adopt() noexcept
{
    auto* next = published.load (std::memory_order_acquire);
    if (next == active) return;
    if (next != nullptr)
        for (auto* list : { &next->chains, &next->returns })
            for (auto& chain : *list)
            {
                auto& state = slots[(size_t) chain.slot];
                if (state.generation != chain.generation)
                {
                    // A new chain in a reused slot starts from its own targets.
                    state = SlotState {}; state.generation = chain.generation;
                    continue;
                }
                if (active == nullptr) continue;
                // A surviving chain keeps its compensation delay content.
                const auto index = active->slotChain[(size_t) chain.slot];
                if (index < 0) continue;
                const auto& old = index < (int) active->chains.size() ? active->chains[(size_t) index] : active->returns[(size_t) index - active->chains.size()];
                if (old.compensation == chain.compensation && chain.compensation > 0)
                {
                    for (size_t c = 0; c < 2; ++c) std::copy_n (old.delay[c].data(), chain.compensation, chain.delay[c].data());
                    chain.delayCursor = old.delayCursor;
                }
            }
    if (next != nullptr && active != nullptr && next->dryDelay[0].size() == active->dryDelay[0].size() && ! next->dryDelay[0].empty())
    {
        for (size_t c = 0; c < 2; ++c) std::copy_n (active->dryDelay[c].data(), active->dryDelay[c].size(), next->dryDelay[c].data());
        next->dryCursor = active->dryCursor;
    }
    active = next;
    acknowledged.store (next, std::memory_order_release);
}
void RackEngine::process (const te::PluginRenderContext& context, const Session::EffectSettings& controls) noexcept
{
    adopt();
    if (active == nullptr || capacity <= 0 || context.bufferNumSamples <= 0) { pendingChokeCount = 0; return; }
    const bool instrumentRack = kind == Kind::instrumentRack || kind == Kind::drumRack;
    auto* destination = context.destBuffer;
    if (active->chains.empty() && active->returns.empty())
    {
        // An empty Instrument or Drum Rack is silent; empty effect racks pass their input.
        if (instrumentRack && destination != nullptr) destination->clear (context.bufferStartSample, context.bufferNumSamples);
        pendingChokeCount = 0; return;
    }
    if (kind == Kind::midiEffectRack)
    {
        midiOut.clear();
        midiOut.isAllNotesOff = context.bufferForMidiMessages != nullptr && context.bufferForMidiMessages->isAllNotesOff;
    }
    for (int offset = 0; offset < context.bufferNumSamples; offset += capacity)
        processChunk (context, offset, std::min (capacity, context.bufferNumSamples - offset), controls);
    if (kind == Kind::midiEffectRack && context.bufferForMidiMessages != nullptr)
    {
        // Moving the merged events back keeps this rack's reserved buffer for the next block.
        midiOut.sortByTimestamp();
        auto& destinationMidi = *context.bufferForMidiMessages;
        destinationMidi.clear(); destinationMidi.isAllNotesOff = midiOut.isAllNotesOff;
        for (auto& m : midiOut) destinationMidi.add (std::move (m));
        midiOut.clear();
    }
    pendingChokeCount = 0;
}
void RackEngine::routeNote (int sample, uint8_t status, uint8_t note, uint8_t velocity, int selector) noexcept
{
    const auto addRouted = [this] (int at, int chain, uint8_t s, uint8_t d1, uint8_t d2)
    { if (routedCount < (int) routed.size()) routed[(size_t) routedCount++] = { at, chain, s, d1, d2, 3 }; };
    const auto channel = static_cast<uint8_t> ((status & 0x0f) + 1);
    if ((status & 0xf0) != 0x90 || velocity == 0)
    {
        // A note-off reaches every chain that received the note-on, with the note it played.
        for (int i = 0; i < heldCount;)
        {
            auto& h = held[(size_t) i];
            if (h.channel != channel || h.note != note) { ++i; continue; }
            const auto index = active->slotChain[(size_t) h.slot];
            if (index >= 0 && index < (int) active->chains.size() && slots[(size_t) h.slot].generation == h.generation)
                addRouted (sample, index, static_cast<uint8_t> (0x80 | (channel - 1)), h.played, (status & 0xf0) == 0x80 ? velocity : 64);
            held[(size_t) i] = held[(size_t) --heldCount];
        }
        return;
    }
    for (size_t c = 0; c < active->chains.size(); ++c)
    {
        auto& chain = active->chains[c];
        const auto& s = chain.settings;
        auto played = note; auto level = velocity;
        if (kind == Kind::drumRack)
        {
            if (s.receiveNote >= 0 && s.receiveNote != note) continue;
            if (s.receiveNote >= 0) played = static_cast<uint8_t> (s.playNote);
            // Striking a pad silences the other pads of its choke group.
            if (s.receiveNote >= 0 && s.chokeGroup > 0)
                for (auto& other : active->chains)
                    if (other.settings.chokeGroup == s.chokeGroup && other.settings.receiveNote >= 0 && other.settings.receiveNote != note
                        && other.chokes < (int) other.chokeSamples.size())
                        other.chokeSamples[(size_t) other.chokes++] = sample;
        }
        else
        {
            if (! inZone (s.key, note) || ! inZone (s.velocity, velocity) || ! inZone (s.select, selector)) continue;
            // Fade ranges lower the velocity; a MIDI rack's selector fades do too.
            auto gain = zoneGain (s.key, note) * zoneGain (s.velocity, velocity);
            if (kind == Kind::midiEffectRack) gain *= zoneGain (s.select, selector);
            level = static_cast<uint8_t> (std::clamp (static_cast<int> (std::lround (velocity * gain)), 1, 127));
        }
        addRouted (sample, (int) c, status, played, level);
        if (heldCount < (int) held.size()) held[(size_t) heldCount++] = { channel, note, played, chain.slot, chain.generation };
    }
}
int RackEngine::route (const te::PluginRenderContext& context, int offset, int count, int selector) noexcept
{
    routedCount = 0;
    auto* midi = context.bufferForMidiMessages;
    if (midi == nullptr) return 0;
    if (midi->isAllNotesOff && offset == 0) heldCount = 0;
    for (auto& m : *midi)
    {
        const auto sample = std::clamp (static_cast<int> (std::llround ((m.getTimeStamp() + context.midiBufferOffset) * rate)), 0, context.bufferNumSamples - 1);
        if (sample < offset || sample >= offset + count) continue;
        const auto* raw = m.getRawData();
        if (m.isNoteOnOrOff()) routeNote (sample - offset, raw[0], raw[1], raw[2], selector);
        // A MIDI Effect Rack's chains change notes only, so other events pass straight through it.
        else if (kind == Kind::midiEffectRack) { if (midiOut.size() < 4096) midiOut.add (std::move (m)); }
        // Instrument chains each receive the other short events; system exclusive data stays out.
        else if (! m.isSysEx() && m.getRawDataSize() <= 3 && routedCount < (int) routed.size())
            routed[(size_t) routedCount++] = { sample - offset, -1, raw[0], m.getRawDataSize() > 1 ? raw[1] : uint8_t (0),
                                               m.getRawDataSize() > 2 ? raw[2] : uint8_t (0), static_cast<uint8_t> (m.getRawDataSize()) };
    }
    return routedCount;
}
void RackEngine::processChunk (const te::PluginRenderContext& context, int offset, int count, const Session::EffectSettings& controls) noexcept
{
    auto& layout = *active;
    const bool takesMidi = kind != Kind::audioEffectRack;
    // Macros drive their targets in knob positions between each mapping's limits.
    volumeOverride.fill (notMapped); panOverride.fill (notMapped);
    for (auto& sends : sendOverride) sends.fill (notMapped);
    auto selector = std::clamp (static_cast<int> (std::lround (controls.values[(size_t) Session::rackChainSelector])), 0, 127);
    for (const auto& m : layout.mappings)
    {
        const auto macro = std::clamp (controls.values[(size_t) m.macro] / 127.0f, 0.0f, 1.0f);
        const auto position = m.minimum + (m.maximum - m.minimum) * macro;
        switch (m.target)
        {
            case Mapping::Target::device: layout.values[(size_t) m.index][(size_t) m.parameter] = effectProportionToValue (m.kind, m.parameter, position); break;
            case Mapping::Target::chainVolume: volumeOverride[(size_t) m.index] = chainProportionToValue (0, position); break;
            case Mapping::Target::chainPan: panOverride[(size_t) m.index] = chainProportionToValue (1, position); break;
            case Mapping::Target::chainSend: sendOverride[(size_t) m.index][(size_t) m.parameter - 2] = chainProportionToValue (m.parameter, position); break;
            case Mapping::Target::selector: selector = std::clamp (static_cast<int> (std::lround (position * 127)), 0, 127); break;
        }
    }
    // Chain gains, input gates and sends ramp to this chunk's targets.
    for (auto* list : { &layout.chains, &layout.returns })
        for (auto& chain : *list)
        {
            auto& state = slots[(size_t) chain.slot];
            const auto& s = chain.settings;
            const bool immediate = ! state.primed;
            state.primed = true;
            const auto volume = std::isnan (volumeOverride[(size_t) chain.slot]) ? s.volumeDb : volumeOverride[(size_t) chain.slot];
            const auto pan = std::isnan (panOverride[(size_t) chain.slot]) ? s.pan : panOverride[(size_t) chain.slot];
            const bool audible = s.active && (chain.isReturn || ! layout.anySolo || s.solo);
            const bool selects = ! chain.isReturn && (kind == Kind::instrumentRack || kind == Kind::audioEffectRack);
            const auto gain = (audible ? decibelGain (volume) : 0.0) * (selects ? selectorOutputGain (s.select, selector) : 1.0);
            const auto [left, right] = panGains (pan);
            state.left.set (gain * left, rampLength, immediate); state.right.set (gain * right, rampLength, immediate);
            state.input.set (kind == Kind::audioEffectRack && ! chain.isReturn && ! inZone (s.select, selector) ? 0.0 : 1.0, rampLength, immediate);
            for (size_t k = 0; k < state.sends.size(); ++k)
            {
                const auto send = std::isnan (sendOverride[(size_t) chain.slot][k]) ? s.sendsDb[k] : sendOverride[(size_t) chain.slot][k];
                state.sends[k].set (kind == Kind::drumRack && ! chain.isReturn && k < layout.returns.size() ? decibelGain (send) : 0.0, rampLength, immediate);
            }
        }
    if (takesMidi) route (context, offset, count, selector);
    // Chokes requested by an enclosing rack reach every chain's instruments.
    for (int i = 0; i < pendingChokeCount; ++i)
        if (const auto at = pendingChokes[(size_t) i] - offset; at >= 0 && at < count)
            for (auto& chain : layout.chains) if (chain.chokes < (int) chain.chokeSamples.size()) chain.chokeSamples[(size_t) chain.chokes++] = at;
    auto* destination = context.destBuffer;
    const int channels = destination != nullptr ? destination->getNumChannels() : 0;
    if (kind == Kind::audioEffectRack)
        for (int c = 0; c < 2; ++c)
        {
            if (channels > 0) input.copyFrom (c, 0, *destination, std::min (c, channels - 1), context.bufferStartSample + offset, count);
            else input.clear (c, 0, count);
        }
    mix.clear (0, count);
    for (size_t k = 0; k < layout.returns.size(); ++k) returnMix[k].clear (0, count);
    for (auto& chain : layout.chains) runChain (chain, context, offset, count, takesMidi);
    if (kind == Kind::midiEffectRack) return;
    // The chains wait for the returns' latency, then the returns join them.
    if (! layout.dryDelay[0].empty())
    {
        const auto length = (int) layout.dryDelay[0].size();
        for (int n = 0; n < count; ++n)
        {
            for (size_t c = 0; c < 2; ++c)
            {
                auto* sample = mix.getWritePointer ((int) c, n);
                const auto delayed = layout.dryDelay[c][(size_t) layout.dryCursor];
                layout.dryDelay[c][(size_t) layout.dryCursor] = *sample; *sample = delayed;
            }
            layout.dryCursor = (layout.dryCursor + 1) % length;
        }
    }
    for (auto& chain : layout.returns) runChain (chain, context, offset, count, false);
    if (destination == nullptr || channels == 0) return;
    for (int c = 0; c < channels; ++c)
        destination->copyFrom (c, context.bufferStartSample + offset, mix, std::min (c, 1), 0, count);
}
void RackEngine::runChain (Chain& chain, const te::PluginRenderContext& context, int offset, int count, bool midiInput) noexcept
{
    auto& layout = *active;
    auto& state = slots[(size_t) chain.slot];
    const auto index = layout.slotChain[(size_t) chain.slot];
    auto* left = scratch.getWritePointer (0);
    auto* right = scratch.getWritePointer (1);
    if (chain.isReturn)
    {
        const auto& source = returnMix[(size_t) index - layout.chains.size()];
        std::copy_n (source.getReadPointer (0), count, left); std::copy_n (source.getReadPointer (1), count, right);
    }
    else if (kind == Kind::audioEffectRack)
    {
        const auto* l = input.getReadPointer (0); const auto* r = input.getReadPointer (1);
        for (int n = 0; n < count; ++n) { const auto g = static_cast<float> (state.input.tick()); left[n] = l[n] * g; right[n] = r[n] * g; }
    }
    else { std::fill_n (left, count, 0.0f); std::fill_n (right, count, 0.0f); }
    chainMidi.clear();
    if (midiInput)
    {
        chainMidi.isAllNotesOff = offset == 0 && context.bufferForMidiMessages != nullptr && context.bufferForMidiMessages->isAllNotesOff;
        for (int i = 0; i < routedCount; ++i)
        {
            const auto& e = routed[(size_t) i];
            const uint8_t bytes[] { e.status, e.data1, e.data2 };
            if ((e.chain == index || e.chain < 0) && chainMidi.size() < 2048)
                chainMidi.addMidiMessage (juce::MidiMessage (bytes, e.size, e.sample / rate), {});
        }
    }
    const auto start = context.editTime.getStart() + te::TimeDuration::fromSeconds (offset / rate);
    const te::PluginRenderContext inner (&scratch, 0, count, &chainMidi, 0.0,
                                         { start, start + te::TimeDuration::fromSeconds (count / rate) },
                                         context.isPlaying, context.isScrubbing, context.isRendering, false);
    for (auto& device : chain.devices)
    {
        if (! device.enabled) continue;
        if (deviceStage (device.kind) == 1)
            for (int c = 0; c < chain.chokes; ++c) device.native->chokeAt (chain.chokeSamples[(size_t) c]);
        device.native->setRackControls (device.mapped >= 0 ? layout.values[(size_t) device.mapped].data() : nullptr);
        device.plugin->applyToBufferWithAutomation (inner);
        device.native->setRackControls (nullptr);
    }
    chain.chokes = 0;
    if (chain.compensation > 0)
        for (int n = 0; n < count; ++n)
        {
            for (size_t c = 0; c < 2; ++c)
            {
                auto* sample = (c == 0 ? left : right) + n;
                const auto delayed = chain.delay[c][(size_t) chain.delayCursor];
                chain.delay[c][(size_t) chain.delayCursor] = *sample; *sample = delayed;
            }
            chain.delayCursor = (chain.delayCursor + 1) % chain.compensation;
        }
    if (kind == Kind::midiEffectRack)
    {
        // A deactivated or solo-silenced chain starts no notes; its releases still pass.
        const bool audible = chain.settings.active && (! layout.anySolo || chain.settings.solo);
        const auto shift = offset / rate - context.midiBufferOffset;
        for (auto& m : chainMidi)
            if ((audible || ! m.isNoteOn()) && midiOut.size() < 4096)
            {
                const auto time = m.getTimeStamp() + shift;
                midiOut.add (std::move (m), time);
            }
        return;
    }
    auto* mixLeft = mix.getWritePointer (0);
    auto* mixRight = mix.getWritePointer (1);
    const auto returns = chain.isReturn ? 0 : layout.returns.size();
    for (int n = 0; n < count; ++n)
    {
        const auto l = left[n] * state.left.tick(), r = right[n] * state.right.tick();
        mixLeft[n] += static_cast<float> (l); mixRight[n] += static_cast<float> (r);
        for (size_t k = 0; k < state.sends.size(); ++k)
        {
            const auto send = state.sends[k].tick();
            if (k < returns)
            {
                returnMix[k].getWritePointer (0)[n] += static_cast<float> (l * send);
                returnMix[k].getWritePointer (1)[n] += static_cast<float> (r * send);
            }
        }
    }
}
}
