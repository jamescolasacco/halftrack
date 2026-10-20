#include "Session.h"
#include "SubtractiveSynth.h"
#include "UtilityProcessor.h"
#include "UtilityEditor.h"
#include "NativeEffects.h"
#include <tracktion_engine/tracktion_engine.h>
#include <map>
#include <array>
#include <set>
#include <cstdlib>

namespace halftrack
{
namespace te = tracktion;
namespace
{
using juce::File;
using juce::Result;
using juce::String;
const juce::Identifier monitorProperty { "halftrackMonitor" };
const juce::Identifier recordQuantisationProperty { "halftrackRecordQuantisation" };
const juce::Identifier countInProperty { "halftrackCountInBars" };
const juce::Identifier midiTrackProperty { "halftrackMidiTrack" };
const juce::Identifier groupFoldedProperty { "halftrackGroupFolded" };
const juce::Identifier launchModeProperty { "halftrackLaunchMode" };
const juce::Identifier launchQuantisationProperty { "halftrackLaunchQuantisation" };
const juce::Identifier launchLegatoProperty { "halftrackLaunchLegato" };
// Engine grid for each ClipQuantisation; Global is resolved from the project setting.
constexpr std::array<te::LaunchQType, 11> clipLaunchGrids { te::LaunchQType::none, te::LaunchQType::none, te::LaunchQType::eightBars, te::LaunchQType::fourBars,
    te::LaunchQType::twoBars, te::LaunchQType::bar, te::LaunchQType::half, te::LaunchQType::quarter, te::LaunchQType::eighth, te::LaunchQType::sixteenth,
    te::LaunchQType::thirtySecond };
Session::LaunchSettings clipLaunchSettings (const juce::ValueTree& state)
{
    Session::LaunchSettings settings;
    const int mode = state.getProperty (launchModeProperty, 0), grid = state.getProperty (launchQuantisationProperty, 0);
    if (mode >= 0 && mode <= 3) settings.mode = static_cast<Session::LaunchMode> (mode);
    if (grid >= 0 && grid < (int) clipLaunchGrids.size()) settings.quantisation = static_cast<Session::ClipQuantisation> (grid);
    settings.legato = state.getProperty (launchLegatoProperty, false);
    return settings;
}
const juce::Identifier midiNoteIDProperty { "halftrackNoteId" };
const juce::Identifier returnProperty { "halftrackReturn" };
const juce::Identifier returnBusProperty { "halftrackReturnBus" };
const juce::Identifier returnModeProperty { "halftrackReturnSendMode" };
const juce::Identifier sendReturnProperty { "halftrackSendReturnID" };

const juce::Identifier manualVolumeProperty { "halftrackManualVolume" };
const juce::Identifier crossfadeProperty { "halftrackCrossfade" };
const juce::Identifier crossfaderProperty { "halftrackCrossfader" };
const juce::Identifier manualPanProperty { "halftrackManualPan" };

// Audio Effect Racks: chain mixer properties, the rack's macro knobs and each device's macro mappings.
const juce::Identifier rackProperty { "halftrackRack" };
const juce::Identifier chainNameProperty { "halftrackChainName" };
const juce::Identifier chainMuteProperty { "halftrackChainMute" };
const juce::Identifier chainSoloProperty { "halftrackChainSolo" };
const juce::Identifier macrosType { "HALFTRACKMACROS" };
const juce::Identifier macroType { "HALFTRACKMACRO" };
const juce::Identifier macroMapProperty { "halftrackMacroMap" };
// Stored mappings use knob positions (0-1); minimum may exceed maximum for an inverted range.
struct StoredMacroMapping { int macro = 0, parameter = 0; float minimum = 0, maximum = 1; };
// "macro,parameter,minimum,maximum" entries separated by semicolons.
std::vector<StoredMacroMapping> macroMappings (const juce::ValueTree& plugin)
{
    std::vector<StoredMacroMapping> result;
    for (const auto& entry : juce::StringArray::fromTokens (plugin[macroMapProperty].toString(), ";", ""))
    {
        const auto fields = juce::StringArray::fromTokens (entry, ",", "");
        if (fields.size() == 4)
            result.push_back ({ fields[0].getIntValue(), fields[1].getIntValue(), fields[2].getFloatValue(), fields[3].getFloatValue() });
    }
    return result;
}
String macroMapText (const std::vector<StoredMacroMapping>& mappings)
{
    juce::StringArray entries;
    for (const auto& m : mappings)
        entries.add (String (m.macro) + "," + String (m.parameter) + "," + String (m.minimum, 7) + "," + String (m.maximum, 7));
    return entries.joinIntoString (";");
}
const juce::Identifier& manualAutomationProperty (Session::AutomationTarget target)
{ return target == Session::AutomationTarget::volume ? manualVolumeProperty : manualPanProperty; }
te::AutomatableParameter& automationParameter (te::VolumeAndPanPlugin& plugin, Session::AutomationTarget target)
{ return *(target == Session::AutomationTarget::volume ? plugin.volParam : plugin.panParam); }
Session::AutomationLane automationLane (te::VolumeAndPanPlugin& plugin, Session::AutomationTarget target)
{
    auto& curve = automationParameter (plugin, target).getCurve();
    Session::AutomationLane lane; lane.enabled = ! curve.bypass.get();
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const auto value = curve.getPointValue (i);
        lane.points.push_back ({ plugin.edit.tempoSequence.toBeats (curve.getPointTime (i)).inBeats(),
            target == Session::AutomationTarget::volume ? te::volumeFaderPositionToDB (value) : value });
    }
    return lane;
}
// Lanes of a native device's controls that have points or are switched off.
std::vector<Session::PluginInfo::ControlAutomation> deviceAutomation (NativeEffectPlugin& plugin)
{
    std::vector<Session::PluginInfo::ControlAutomation> result;
    for (int i = 0; i < plugin.numControls(); ++i)
    {
        auto& curve = plugin.control (i)->getCurve();
        if (curve.getNumPoints() == 0 && ! curve.bypass.get()) continue;
        Session::PluginInfo::ControlAutomation entry { i, {} };
        entry.lane.enabled = ! curve.bypass.get();
        for (int p = 0; p < curve.getNumPoints(); ++p)
            entry.lane.points.push_back ({ plugin.edit.tempoSequence.toBeats (curve.getPointTime (p)).inBeats(),
                                           effectProportionToValue (plugin.getKind(), i, curve.getPointValue (p)) });
        result.push_back (std::move (entry));
    }
    return result;
}
bool validAutomationLane (Session::AutomationTarget target, const Session::AutomationLane& lane)
{
    if (target != Session::AutomationTarget::volume && target != Session::AutomationTarget::pan) return false;
    if (lane.points.size() > 4096) return false;
    double previous = -1;
    for (const auto& point : lane.points)
    {
        if (! std::isfinite (point.beat) || point.beat < 0 || point.beat > 1000000 || point.beat <= previous
            || (previous >= 0 && point.beat - previous < 0.999e-6)
            || ! std::isfinite (point.value) || point.value < (target == Session::AutomationTarget::volume ? -100.0f : -1.0f)
            || point.value > (target == Session::AutomationTarget::volume ? 6.0f : 1.0f)) return false;
        previous = point.beat;
    }
    return true;
}
bool finiteProperty (const juce::var& property, double lower, double upper)
{
    const auto text = property.toString().trim().toStdString(); char* end = nullptr;
    const double value = std::strtod (text.c_str(), &end);
    return ! text.empty() && end == text.c_str() + text.size() && std::isfinite (value) && value >= lower && value <= upper;
}
bool isMixerState (const juce::ValueTree& state)
{
    return state.hasType (te::IDs::PLUGIN)
        && (state[te::IDs::type].toString() == "volume" || state[te::IDs::type].toString() == "halftrack-mixer");
}
const char* mixerParameterID (const juce::ValueTree& state, Session::AutomationTarget target)
{
    const bool master = state.getParent().hasType (te::IDs::MASTERVOLUME);
    return target == Session::AutomationTarget::volume ? (master ? "master volume" : "volume")
        : (master ? "master pan" : "pan");
}
Result readStoredManualParameters (const juce::ValueTree& state, std::map<String, float>& values)
{
    if (! state.hasProperty (te::IDs::parameters)) return Result::ok();
    const auto* data = state[te::IDs::parameters].getBinaryData();
    if (! data || data->getSize() > 256) return Result::fail ("Invalid stored mixer parameter data.");
    juce::MemoryInputStream stream (*data, false);
    while (! stream.isExhausted())
    {
        const auto nativeKey = stream.readString();
        const String key = nativeKey == mixerParameterID (state, Session::AutomationTarget::volume) ? "volume"
            : nativeKey == mixerParameterID (state, Session::AutomationTarget::pan) ? "pan" : "";
        if (key.isEmpty() || stream.getNumBytesRemaining() < 4 || values.contains (key))
            return Result::fail ("Invalid stored mixer parameter entry.");
        const float value = stream.readFloat();
        if (! std::isfinite (value) || value < (key == "volume" ? 0.0f : -1.0f) || value > 1
            || (key == "volume" && value > 0 && value < std::exp (-106.0f / 20.0f) - 1e-9f))
            return Result::fail ("Invalid stored manual mixer level.");
        values.emplace (key, value);
    }
    return Result::ok();
}
Result validateMixerAutomation (const juce::ValueTree& state)
{
    if (isMixerState (state))
    {
        std::map<String, float> explicitValues;
        if (auto result = readStoredManualParameters (state, explicitValues); result.failed()) return result;
        for (const auto& key : { manualVolumeProperty, manualPanProperty })
            if (state.hasProperty (key) && (! finiteProperty (state[key], key == manualVolumeProperty ? 0 : -1, 1)
                || (key == manualVolumeProperty && static_cast<double> (state[key]) > 0
                    && static_cast<double> (state[key]) < std::exp (-106.0 / 20.0) - 1e-9)))
                return Result::fail ("Invalid stored manual mixer level.");
        std::set<String> targets;
        for (const auto& curve : state)
        {
            if (! curve.hasType (te::IDs::AUTOMATIONCURVE)) continue;
            const auto id = curve[te::IDs::paramID].toString();
            const bool volume = id == mixerParameterID (state, Session::AutomationTarget::volume);
            const bool pan = id == mixerParameterID (state, Session::AutomationTarget::pan);
            if ((! volume && ! pan) || ! targets.insert (id).second || curve.getNumChildren() > 4096
                || ! finiteProperty (curve.getProperty (te::IDs::bypass, 0), 0, 1)
                || static_cast<double> (curve.getProperty (te::IDs::bypass, 0)) != std::floor (static_cast<double> (curve.getProperty (te::IDs::bypass, 0))))
                return Result::fail ("Invalid mixer automation lane.");
            if (curve.getNumChildren() > 0 && ! state.hasProperty (volume ? manualVolumeProperty : manualPanProperty)
                && ! explicitValues.contains (volume ? "volume" : "pan"))
            {
                const auto saved = state.getProperty (volume ? te::IDs::volume : te::IDs::pan, volume ? te::decibelsToVolumeFaderPosition (0) : 0);
                if (! finiteProperty (saved, volume ? 0 : -1, 1)
                    || (volume && static_cast<double> (saved) > 0 && static_cast<double> (saved) < std::exp (-106.0 / 20.0) - 1e-9))
                    return Result::fail ("Invalid legacy mixer automation baseline.");
            }
            double previous = -1;
            for (const auto& point : curve)
            {
                if (! point.hasType (te::IDs::POINT) || ! finiteProperty (point[te::IDs::t], 0, 3000000)
                    || ! finiteProperty (point[te::IDs::v], volume ? 0 : -1, 1)
                    || ! finiteProperty (point.getProperty (te::IDs::c, 0), 0, 0)
                    || (volume && static_cast<double> (point[te::IDs::v]) > 0
                        && static_cast<double> (point[te::IDs::v]) < std::exp (-106.0 / 20.0) - 1e-9)
                    || static_cast<double> (point[te::IDs::t]) <= previous)
                    return Result::fail ("Mixer automation needs ordered finite linear points.");
                previous = static_cast<double> (point[te::IDs::t]);
            }
        }
    }
    for (const auto& child : state) if (auto result = validateMixerAutomation (child); result.failed()) return result;
    return Result::ok();
}
void restoreManualMixerState (juce::ValueTree state)
{
    if (isMixerState (state))
    {
        std::map<String, float> explicitValues;
        if (readStoredManualParameters (state, explicitValues).failed()) return; // Validation precedes staged load.
        bool owned = false;
        for (const auto& pair : { std::pair { manualVolumeProperty, te::IDs::volume }, std::pair { manualPanProperty, te::IDs::pan } })
        {
            const auto curve = state.getChildWithProperty (te::IDs::paramID, mixerParameterID (state,
                pair.second == te::IDs::volume ? Session::AutomationTarget::volume : Session::AutomationTarget::pan));
            if (state.hasProperty (pair.first) || (curve.isValid() && curve.getNumChildren() > 0))
            {
                if (! state.hasProperty (pair.first))
                {
                    const auto found = explicitValues.find (pair.second.toString());
                    state.setProperty (pair.first, found != explicitValues.end() ? juce::var (found->second)
                        : state.getProperty (pair.second, pair.second == te::IDs::volume ? te::decibelsToVolumeFaderPosition (0) : 0), nullptr);
                }
                state.setProperty (pair.second, state[pair.first], nullptr); owned = true;
            }
            else if (const auto found = explicitValues.find (pair.second.toString()); found != explicitValues.end())
            {
                // Retiring a shared parameter blob must also retain a sibling
                // parameter that has no owned curve or manual metadata yet.
                state.setProperty (pair.second, found->second, nullptr);
            }
        }
        // Construct parameters from manual values before the native single-point
        // editing path can confuse those values with envelope edits.
        if (owned) state.removeProperty (te::IDs::parameters, nullptr);
        if (state.getChildWithName (te::IDs::AUTOMATIONCURVE).isValid())
            state.setProperty (te::IDs::remapOnTempoChange, true, nullptr);
    }
    for (auto child : state) restoreManualMixerState (child);
}

bool isReturn (const te::AudioTrack& track) { return track.state.getProperty (returnProperty, false); }
Session::SendMode sendMode (const te::AudioTrack& track)
{ return static_cast<Session::SendMode> (static_cast<int> (track.state.getProperty (returnModeProperty, 1))); }

te::AuxSendPlugin* findSend (te::AudioTrack& source, const String& destination)
{
    for (auto* plugin : source.pluginList.getPlugins())
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin))
            if (send->state.getProperty (sendReturnProperty).toString() == destination) return send;
    return nullptr;
}

// Validate routing before constructing native objects, so invalid projects cannot
// replace the active edit, load hidden media, or change its undo history.
Result validateRouting (const juce::ValueTree& state)
{
    struct Destination { int bus; Session::SendMode mode; };
    std::map<String, Destination> destinations;
    std::set<int> buses;
    std::set<String> returnInputTargets;
    std::vector<juce::ValueTree> tracks, routePlugins;
    auto number = [] (const juce::var& value, double low, double high, bool integral = false)
    {
        const auto text = value.toString().trim().toStdString();
        char* end = nullptr;
        const auto parsed = std::strtod (text.c_str(), &end);
        return ! text.empty() && end == text.c_str() + text.size() && std::isfinite (parsed)
            && parsed >= low && parsed <= high && (! integral || std::floor (parsed) == parsed);
    };
    const auto canonicalID = [] (const juce::var& value)
    {
        const auto id = te::EditItemID::fromVar (value);
        return id.isValid() && id.toString() == value.toString();
    };
    const auto collect = [&] (auto&& self, const juce::ValueTree& node) -> void
    {
        if (node.hasType (te::IDs::TRACK)) tracks.push_back (node);
        if (node.hasType (te::IDs::PLUGIN)
            && (node[te::IDs::type].toString() == te::AuxSendPlugin::xmlTypeName
                || node[te::IDs::type].toString() == te::AuxReturnPlugin::xmlTypeName)) routePlugins.push_back (node);
        for (const auto& child : node) self (self, child);
    };
    collect (collect, state);
    for (const auto& track : tracks)
    {
        if (! static_cast<bool> (track.getProperty (returnProperty, false)))
        {
            if (track.hasProperty (returnBusProperty) || track.hasProperty (returnModeProperty))
                return Result::fail ("Return metadata requires a return track.");
            continue;
        }
        if (! number (track[returnBusProperty], 0, 2147483646, true)
            || ! number (track[returnModeProperty], 0, 1, true)
            || static_cast<bool> (track.getProperty (midiTrackProperty, false)))
            return Result::fail ("The project has invalid return track settings.");
        const auto id = track[te::IDs::id].toString();
        const auto bus = static_cast<int> (track[returnBusProperty]);
        if (! canonicalID (track[te::IDs::id]) || destinations.contains (id) || ! buses.insert (bus).second)
            return Result::fail ("Return tracks require unique identities and buses.");
        destinations.emplace (id, Destination { bus, static_cast<Session::SendMode> (static_cast<int> (track[returnModeProperty])) });
        returnInputTargets.insert (id);
        const auto checkContents = [&] (auto&& self, const juce::ValueTree& node) -> bool
        {
            if (te::Clip::isClipState (node)) return false;
            if (node.hasType (te::IDs::CLIPSLOT))
            {
                if (! canonicalID (node[te::IDs::id])) return false;
                returnInputTargets.insert (node[te::IDs::id].toString());
            }
            for (const auto& child : node) if (! self (self, child)) return false;
            return true;
        };
        if (! checkContents (checkContents, track)) return Result::fail ("Return tracks require empty clip slots with valid identities.");
    }
    std::vector<juce::ValueTree> validated;
    for (const auto& track : tracks)
    {
        const auto destination = destinations.find (track[te::IDs::id].toString());
        const bool returned = destination != destinations.end();
        std::vector<juce::ValueTree> plugins;
        int mixer = -1, mixers = 0, returns = 0;
        bool routed = returned;
        for (const auto& child : track)
            if (child.hasType (te::IDs::PLUGIN))
            {
                const auto type = child[te::IDs::type].toString();
                if (type == "volume" || type == "halftrack-mixer") { mixer = static_cast<int> (plugins.size()); ++mixers; }
                if (type == te::AuxSendPlugin::xmlTypeName) routed = true;
                plugins.push_back (child);
            }
        if (routed && (mixers != 1 || ! canonicalID (track[te::IDs::id])))
            return Result::fail ("Routed tracks require a valid identity and exactly one track mixer.");
        if (routed)
            for (const auto& device : track.getChildWithName (te::IDs::OUTPUTDEVICES))
                if (device[te::IDs::name].toString().startsWithIgnoreCase ("track "))
                    return Result::fail ("Auxiliary-routed tracks cannot route their main output to another track.");
        std::set<String> targets;
        bool pastSourceEffects = false;
        for (int index = 0; index < static_cast<int> (plugins.size()); ++index)
        {
            const auto& plugin = plugins[static_cast<size_t> (index)];
            const auto type = plugin[te::IDs::type].toString();
            if (type == te::AuxReturnPlugin::xmlTypeName)
            {
                if (! returned || ++returns != 1 || index != 0 || ! canonicalID (plugin[te::IDs::id])
                    || ! number (plugin[te::IDs::busNum], 0, 2147483646, true)
                    || static_cast<int> (plugin[te::IDs::busNum]) != destination->second.bus
                    || ! static_cast<bool> (plugin.getProperty (te::IDs::enabled, true)))
                    return Result::fail ("The project has an invalid return input.");
                validated.push_back (plugin);
            }
            else if (type == te::AuxSendPlugin::xmlTypeName)
            {
                const auto target = plugin[sendReturnProperty].toString();
                const auto found = destinations.find (target);
                if (returned || found == destinations.end() || ! targets.insert (target).second
                    || ! canonicalID (plugin[te::IDs::id])
                    || ! number (plugin[te::IDs::busNum], 0, 2147483646, true)
                    || static_cast<int> (plugin[te::IDs::busNum]) != found->second.bus
                    || ! number (plugin[te::IDs::auxSendSliderPos], 0, te::decibelsToVolumeFaderPosition (0.0f))
                    || static_cast<bool> (plugin.getProperty (te::IDs::phase, false))
                    || ((index < mixer) != (found->second.mode == Session::SendMode::pre)))
                    return Result::fail ("The project has an invalid send route or tap position.");
                pastSourceEffects = true;
                validated.push_back (plugin);
            }
            else if (index == mixer) pastSourceEffects = true;
            else if (routed && pastSourceEffects && type != "level")
                return Result::fail ("Source and return effects must precede the mixer and send taps.");
        }
        if (returned && returns != 1) return Result::fail ("The return track is missing its input.");
    }
    if (validated.size() != routePlugins.size()) return Result::fail ("Auxiliary routing is only supported directly on audio tracks.");
    const auto checkInputs = [&] (auto&& self, const juce::ValueTree& node) -> bool
    {
        if (node.hasType (te::IDs::INPUTDEVICEDESTINATION) && returnInputTargets.contains (te::EditItemID::fromVar (node[te::IDs::targetID]).toString())) return false;
        if (node.hasType (te::IDs::INPUTDEVICE) && destinations.contains (te::EditItemID::fromVar (node[te::IDs::sourceTrack]).toString())) return false;
        for (const auto& child : node) if (! self (self, child)) return false;
        return true;
    };
    if (! checkInputs (checkInputs, state)) return Result::fail ("Return tracks cannot receive recording inputs.");
    return Result::ok();
}

// Keep the engine's automation, smoothing and gain range, but normalize its
// sine/cosine law to unity at center. A distinct type preserves older projects'
// serialized linear mixers rather than silently changing their sound.
class TrackMixer final : public te::VolumeAndPanPlugin
{
public:
    static constexpr const char* xmlTypeName = "halftrack-mixer";
    explicit TrackMixer (te::PluginCreationInfo info) : VolumeAndPanPlugin (info)
    {
        setPanLaw (te::PanLaw3dBCenter);
    }
    juce::String getPluginType() override { return xmlTypeName; }
    // The crossfade gain is set on the message thread and ramped across each block.
    std::atomic<float> crossfade { 1.0f };
    void initialise (const te::PluginInitialisationInfo& info) override
    { VolumeAndPanPlugin::initialise (info); appliedCrossfade = crossfade.load (std::memory_order_relaxed); }
    void applyToBuffer (const te::PluginRenderContext& context) override
    {
        VolumeAndPanPlugin::applyToBuffer (context);
        const auto target = crossfade.load (std::memory_order_relaxed);
        if (isEnabled() && context.destBuffer != nullptr)
            for (int channel = 0; channel < juce::jmin (2, context.destBuffer->getNumChannels()); ++channel)
                context.destBuffer->applyGainRamp (channel, context.bufferStartSample, context.bufferNumSamples,
                                                   1.4142135623730951f * appliedCrossfade, 1.4142135623730951f * target);
        appliedCrossfade = target;
    }
private:
    float appliedCrossfade = 1.0f;
};

// Track Delay. A positive delay holds the track's audio back through a preallocated ring.
// A negative delay reports that much latency instead, so the engine's delay compensation
// holds every other path back and the track plays early.
const juce::Identifier trackDelayProperty { "halftrackDelayMs" };
class TrackDelayPlugin final : public te::Plugin
{
public:
    static constexpr const char* xmlTypeName = "halftrack-track-delay";
    static constexpr double maximumMs = 1000;
    explicit TrackDelayPlugin (te::PluginCreationInfo info) : Plugin (info)
    {
        milliseconds.referTo (state, trackDelayProperty, getUndoManager(), 0.0);
        delayMs.store (milliseconds.get(), std::memory_order_relaxed);
    }
    juce::String getName() const override { return "Track Delay"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return getName(); }
    bool takesAudioInput() override { return true; }
    bool takesMidiInput() override { return true; }
    bool producesAudioWhenNoAudioInput() override { return milliseconds.get() > 0; }
    int getNumOutputChannelsGivenInputs (int inputs) override { return juce::jmax (1, juce::jmin (2, inputs)); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    double getLatencySeconds() override { return juce::jmax (0.0, -milliseconds.get()) / 1000.0; }
    void initialise (const te::PluginInitialisationInfo& info) override
    {
        rate = info.sampleRate;
        ring.setSize (2, (int) std::ceil (maximumMs / 1000.0 * rate) + 1);
        ring.clear(); position = 0;
    }
    void deinitialise() override {}
    void reset() override { ring.clear(); position = 0; }
    void applyToBuffer (const te::PluginRenderContext& context) override
    {
        auto* buffer = context.destBuffer;
        const int capacity = ring.getNumSamples();
        if (buffer == nullptr || capacity == 0) return;
        const int delay = juce::jlimit (0, capacity - 1, (int) std::lround (juce::jmax (0.0, delayMs.load (std::memory_order_relaxed)) * rate / 1000.0));
        const int channels = juce::jmin (2, buffer->getNumChannels());
        for (int c = 0; c < channels; ++c)
        {
            auto* io = buffer->getWritePointer (c, context.bufferStartSample);
            auto* line = ring.getWritePointer (c);
            int write = position;
            for (int n = 0; n < context.bufferNumSamples; ++n)
            {
                line[write] = io[n];
                int read = write - delay; if (read < 0) read += capacity;
                io[n] = line[read];
                if (++write == capacity) write = 0;
            }
        }
        position = (position + context.bufferNumSamples) % capacity;
    }
    juce::CachedValue<double> milliseconds;
private:
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id) override
    {
        if (tree == state && id == trackDelayProperty)
        {
            milliseconds.forceUpdateOfCachedValue();
            const auto previous = delayMs.exchange (milliseconds.get(), std::memory_order_relaxed);
            // Reported latency is read when the graph is built.
            if (juce::jmin (0.0, previous) != juce::jmin (0.0, milliseconds.get())) edit.restartPlayback();
        }
        Plugin::valueTreePropertyChanged (tree, id);
    }
    std::atomic<double> delayMs { 0 };
    juce::AudioBuffer<float> ring;
    double rate = 44100;
    int position = 0;
};

template <typename TrackType>
Result useTrackMixer (TrackType& track)
{
    auto* previous = track.getVolumePlugin();
    if (previous == nullptr) return Result::fail ("The track has no mixer.");
    const int index = track.pluginList.indexOf (previous);
    auto mixer = track.edit.getPluginCache().createNewPlugin (TrackMixer::xmlTypeName, {});
    if (mixer == nullptr) return Result::fail ("Could not create the track mixer.");
    previous->deleteFromParent();
    track.pluginList.insertPlugin (mixer, index, nullptr);
    return Result::ok();
}

// Arrangement and launcher clips share source ownership, but not playback placement.
juce::Array<te::Clip*> allTrackClips (te::AudioTrack& track)
{
    auto result = track.getClips();
    for (auto* slot : track.getClipSlotList().getClipSlots())
        if (auto* clip = slot->getClip()) result.add (clip);
    return result;
}

Session::AudioClipSettings audioSettings (const te::WaveAudioClip& clip)
{
    return { clip.getGainDB(), clip.getFadeIn().inSeconds(), clip.getFadeOut().inSeconds(),
        static_cast<Session::FadeCurve> (clip.getFadeInType()), static_cast<Session::FadeCurve> (clip.getFadeOutType()) };
}
bool validAudioSettings (const Session::AudioClipSettings& s, double length)
{
    const auto curve = [] (auto v) { return static_cast<int> (v) >= 1 && static_cast<int> (v) <= 4; };
    return std::isfinite (s.gainDb) && s.gainDb >= -100 && s.gainDb <= 24
        && std::isfinite (s.fadeInSeconds) && std::isfinite (s.fadeOutSeconds)
        && s.fadeInSeconds >= 0 && s.fadeOutSeconds >= 0
        && s.fadeInSeconds + s.fadeOutSeconds <= length + 1e-10
        && curve (s.fadeInCurve) && curve (s.fadeOutCurve);
}
bool canEditAudioRange (const te::WaveAudioClip& clip)
{
    return clip.getClipSlot() == nullptr && ! clip.isLooping() && ! clip.getAutoTempo() && ! clip.getWarpTime()
        && ! clip.getIsReversed() && clip.getSpeedRatio() == 1 && ! clip.isGrouped();
}
bool canReverseAudio (te::WaveAudioClip& clip)
{
    return clip.getClipSlot() == nullptr && ! clip.isLooping() && ! clip.getAutoTempo() && ! clip.getWarpTime()
        && ! clip.getAutoPitch() && clip.getPitchChange() == 0 && clip.getSpeedRatio() == 1 && ! clip.isGrouped()
        && clip.getNumTakes (true) <= 1 && ! clip.isCurrentTakeComp() && clip.getClipEffects() == nullptr
        && ! clip.isUsingARA() && ! clip.getClipTrack()->isFrozen (te::Track::anyFreeze);
}
bool canCopyMidiClip (const te::MidiClip& clip)
{
    return clip.getClipSlot() == nullptr && ! clip.isLooping() && ! clip.isGrouped() && ! clip.hasAnyTakes()
        && clip.getSpeedRatio() == 1 && clip.getOffsetInBeats().inBeats() == 0;
}
void renewMidiNoteIDs (juce::ValueTree state, juce::UndoManager* undo = nullptr)
{
    if (state.hasType (te::IDs::NOTE)) state.setProperty (midiNoteIDProperty,juce::Uuid().toString(),undo);
    for (auto child : state) renewMidiNoteIDs (child,undo);
}
double recordGrid(Session::RecordQuantisation choice)
{
    constexpr double grids[]={0,1,.5,1.0/3,.25,1.0/6,.125};
    return grids[juce::jlimit(0,6,static_cast<int>(choice))];
}
Result validateMasterVolume(const juce::ValueTree& state)
{
    // These obsolete native shapes replace/clear the modern master state during
    // construction. Halftrack has always saved the explicit MASTERVOLUME group.
    if (state.getChildWithName (te::IDs::PLUGIN).isValid()) return Result::fail ("Legacy root master processors are unsupported.");
    int groups=0;
    for(const auto& group:state) if(group.hasType(te::IDs::MASTERVOLUME))
    {
        if(++groups>1) return Result::fail("Invalid duplicate master volume.");
        if (group.hasProperty (te::IDs::left) || group.hasProperty (te::IDs::right))
            return Result::fail ("Legacy split master levels are unsupported.");
        int volumes=0;
        for(const auto& plugin:group) if(plugin.hasType(te::IDs::PLUGIN))
        {
            if(++volumes>1 || plugin.getProperty(te::IDs::type).toString()!="volume") return Result::fail("Invalid master volume processor.");
            const auto text=plugin.getProperty(te::IDs::volume,te::decibelsToVolumeFaderPosition(0)).toString().trim().toStdString();char* end=nullptr;
            const auto value=std::strtod(text.c_str(),&end);
            if(text.empty() || end!=text.c_str()+text.size() || !std::isfinite(value) || value<0 || value>1)
                return Result::fail("Invalid master volume level.");
            if (! finiteProperty (plugin.getProperty (te::IDs::pan, 0), -1, 1))
                return Result::fail ("Invalid master stereo balance.");
        }
    }
    return Result::ok();
}
// Crossfade sides are 0 (none), 1 (A) or 2 (B); the crossfader is a finite -1..1 position.
Result validateCrossfade (const juce::ValueTree& state)
{
    if (state.hasProperty (crossfaderProperty))
    {
        const auto text = state[crossfaderProperty].toString().trim().toStdString(); char* end = nullptr;
        const auto value = std::strtod (text.c_str(), &end);
        if (text.empty() || end != text.c_str() + text.size() || ! std::isfinite (value) || value < -1 || value > 1)
            return Result::fail ("The project has an invalid crossfader position.");
    }
    const auto check = [] (auto&& self, const juce::ValueTree& node) -> Result
    {
        if (node.hasType (te::IDs::TRACK) && node.hasProperty (crossfadeProperty))
        {
            const auto text = node[crossfadeProperty].toString().trim();
            if (text != "0" && text != "1" && text != "2") return Result::fail ("The project has an invalid crossfade assignment.");
        }
        for (const auto& child : node) if (auto r = self (self, child); r.failed()) return r;
        return Result::ok();
    };
    return check (check, state);
}
Result validateRecordQuantisation(const juce::ValueTree& state)
{
    const auto text=state.getProperty(recordQuantisationProperty,0).toString().trim().toStdString();char* end=nullptr;
    const auto value=std::strtod(text.c_str(),&end);
    if(text.empty() || end!=text.c_str()+text.size() || !std::isfinite(value) || value<0 || value>6 || std::floor(value)!=value)
        return Result::fail("Invalid project recording quantization.");
    return Result::ok();
}
Result validateMetronome (const juce::ValueTree& state)
{
    const auto read = [](const juce::var& value, double lo, double hi, bool integral)
    {
        const auto text=value.toString().trim().toStdString(); char* end=nullptr;
        const double number=std::strtod(text.c_str(),&end);
        return !text.empty() && end==text.c_str()+text.size() && std::isfinite(number)
            && number>=lo && number<=hi && (!integral || std::floor(number)==number);
    };
    if(!read(state.getProperty(countInProperty,0),0,2,true)) return Result::fail("Invalid project count-in.");
    int count=0;
    for(const auto& node:state) if(node.hasType(te::IDs::CLICKTRACK))
    {
        if(++count>1 || !read(node.getProperty(te::IDs::level,.6),.2,1,false)
            || !read(node.getProperty(te::IDs::active,0),0,1,true)
            || !read(node.getProperty(te::IDs::emphasiseBars,1),0,1,true)
            || !read(node.getProperty(te::IDs::onlyRecording,0),0,1,true))
            return Result::fail("Invalid project metronome settings.");
    }
    return Result::ok();
}

Result validateProjectTempo (const juce::ValueTree& state)
{
    for (const auto& tempo : state.getChildWithName (te::IDs::TEMPOSEQUENCE))
        if (tempo.hasType (te::IDs::TEMPO))
        {
            const auto text = tempo.getProperty (te::IDs::bpm, 120.0).toString().trim().toStdString();
            char* end = nullptr; const double value = std::strtod (text.c_str(), &end);
            if (text.empty() || end != text.c_str()+text.size() || ! std::isfinite (value)
                || value < te::TempoSetting::minBPM || value > te::TempoSetting::maxBPM)
                return Result::fail ("Project tempo must be a finite number from 20 to 999 BPM.");
        }
    return Result::ok();
}
constexpr std::array<te::FollowAction, 10> nativeFollowActions {
    te::FollowAction::none, te::FollowAction::globalStop, te::FollowAction::globalPlayAgain,
    te::FollowAction::trackPrevious, te::FollowAction::trackNext, te::FollowAction::trackFirst,
    te::FollowAction::trackLast, te::FollowAction::trackRoundRobin, te::FollowAction::trackAny, te::FollowAction::trackOther
};
bool validFollowSettings (const Session::FollowSettings& settings)
{
    const auto action = static_cast<int> (settings.action), second = static_cast<int> (settings.actionB);
    if (action < 0 || action >= (int) nativeFollowActions.size() || second < 0 || second >= (int) nativeFollowActions.size()
        || ! std::isfinite (settings.afterBeats) || ! std::isfinite (settings.chanceA) || settings.chanceA < 0 || settings.chanceA > 1) return false;
    if (settings.action == Session::FollowAction::off)
        return settings.afterBeats == 0 && settings.actionB == Session::FollowAction::off && settings.chanceA == 1;
    return settings.afterBeats >= 1.0 / 960 && settings.afterBeats <= 4096;
}
Session::FollowSettings clipFollowSettings (const juce::ValueTree& state)
{
    const auto actions = state.getChildWithName (te::IDs::FOLLOWACTIONS);
    const int count = actions.getNumChildren();
    if (count < 1 || count > 2) return {};
    const auto parse = [] (const juce::ValueTree& action) -> std::optional<Session::FollowAction>
    {
        if (const auto native = te::followActionFromString (action[te::IDs::type].toString()))
            for (size_t i = 0; i < nativeFollowActions.size(); ++i) if (*native == nativeFollowActions[i]) return static_cast<Session::FollowAction> (i);
        return {};
    };
    const auto first = parse (actions.getChild (0));
    if (! first || *first == Session::FollowAction::off) return {};
    Session::FollowSettings settings { *first, static_cast<double> (state.getProperty (te::IDs::followActionBeats, 0.0)) };
    if (count == 2)
    {
        const auto second = parse (actions.getChild (1)); if (! second) return {};
        // A's chance is its share of the two weights; B, or nothing, takes the rest.
        const double a = actions.getChild (0).getProperty (te::IDs::weight, 1.0), b = actions.getChild (1).getProperty (te::IDs::weight, 1.0);
        settings.actionB = *second; settings.chanceA = a + b > 0 ? a / (a + b) : 1.0;
    }
    return settings;
}
Result validateFollowActions (const juce::ValueTree& state)
{
    if (state.hasType (te::IDs::AUDIOCLIP) || state.hasType (te::IDs::MIDICLIP))
    {
        int containers = 0;
        for (const auto& child : state) if (child.hasType (te::IDs::FOLLOWACTIONS)) ++containers;
        const auto actions = state.getChildWithName (te::IDs::FOLLOWACTIONS);
        const auto text = state.getProperty (te::IDs::followActionBeats, 0.0).toString().trim().toStdString();
        char* end = nullptr; const auto beats = std::strtod (text.c_str(), &end);
        if (containers > 1 || actions.getNumChildren() > 2 || text.empty() || end != text.c_str() + text.size()
            || ! std::isfinite (beats) || beats < 0 || beats > 4096
            || state.getProperty (te::IDs::followActionDurationType, "beats").toString() != "beats")
            return Result::fail ("The project contains unsupported clip follow timing.");
        if (actions.getNumChildren() == 0)
        {
            if (beats != 0) return Result::fail ("Clip follow timing has no action.");
        }
        else
        {
            // One action at unit weight, or actions A and B whose weights are their chances.
            const auto settings = clipFollowSettings (state);
            double total = 0; bool shaped = actions.getNumChildren() <= 2;
            for (const auto& action : actions)
            {
                const auto weight = action.getProperty (te::IDs::weight, 1.0).toString().trim().toStdString();
                char* weightEnd = nullptr; const auto value = std::strtod (weight.c_str(), &weightEnd);
                shaped = shaped && action.hasType (te::IDs::ACTION) && ! weight.empty() && weightEnd == weight.c_str() + weight.size()
                       && std::isfinite (value) && value >= 0 && value <= 1;
                total += value;
            }
            if (! shaped || settings.action == Session::FollowAction::off || ! validFollowSettings (settings) || std::abs (total - 1) > 1e-9)
                return Result::fail ("The project contains unsupported clip follow actions.");
        }
    }
    for (const auto& child : state) if (auto result = validateFollowActions (child); result.failed()) return result;
    return Result::ok();
}

Result validateAudioClips (const juce::ValueTree& state)
{
    if ((state.hasType(te::IDs::AUDIOCLIP) || state.hasType(te::IDs::MIDICLIP)) && state.hasProperty(te::IDs::mute))
    {
        const auto value=state[te::IDs::mute].toString().trim();
        if (value!="0" && value!="1") return Result::fail("The project contains invalid clip activation.");
    }
    if (state.hasType (te::IDs::NOTE) && state.hasProperty (te::IDs::m))
    {
        const auto value = state[te::IDs::m].toString().trim();
        if (value != "0" && value != "1") return Result::fail ("The project contains invalid MIDI note activation.");
    }
    if (state.hasType (te::IDs::AUDIOCLIP))
    {
        const auto parse = [&state] (const juce::Identifier& key, double fallback, double& result)
        {
            if (! state.hasProperty (key)) { result = fallback; return true; }
            const auto value = state[key].toString().trim().toStdString(); char* end = nullptr;
            result = std::strtod (value.c_str(), &end);
            return ! value.empty() && end == value.c_str()+value.size() && std::isfinite (result);
        };
        double gain = 0, in = 0, out = 0, length = 0, inType = 1, outType = 1, reversed = 0;
        if (! parse (te::IDs::gain, 0, gain) || ! parse (te::IDs::fadeIn, 0, in)
            || ! parse (te::IDs::fadeOut, 0, out) || ! parse (te::IDs::length, 0, length)
            || ! parse (te::IDs::fadeInType, 1, inType) || ! parse (te::IDs::fadeOutType, 1, outType)
            || ! parse (te::IDs::isReversed, 0, reversed) || (reversed != 0 && reversed != 1)
            || gain < -100 || gain > 24 || length < 0 || inType < 1 || inType > 4 || outType < 1 || outType > 4
            || std::floor (inType) != inType || std::floor (outType) != outType
            || ! validAudioSettings ({ static_cast<float> (gain), in, out,
                static_cast<Session::FadeCurve> (static_cast<int> (inType)),
                static_cast<Session::FadeCurve> (static_cast<int> (outType)) }, length))
            return Result::fail ("The project contains invalid audio clip gain, fades or direction.");
    }
    for (const auto& child : state) if (auto r = validateAudioClips (child); r.failed()) return r;
    return Result::ok();
}

bool validLocator (const String& name, double seconds)
{
    return name.trim().isNotEmpty() && name.length() <= 128 && ! name.containsAnyOf ("\r\n\t")
        && std::isfinite (seconds) && seconds >= 0 && seconds <= te::Edit::maximumLength-.001;
}
// Audio Effect Racks: each rack type is Halftrack's own, used by exactly one rack on an audio track,
// and holds chains of audio devices that each end in a chain mixer. Connections are rebuilt on load.
Result validateRacks (const juce::ValueTree& state)
{
    const auto fail = [] { return Result::fail ("The project contains an invalid Audio Effect Rack."); };
    const auto racks = state.getChildWithName (te::IDs::RACKS);
    std::map<String, int> uses;
    for (const auto& rack : racks)
    {
        if (! rack.hasType (te::IDs::RACK)) continue;
        const auto id = te::EditItemID::fromID (rack);
        if (! id.isValid() || static_cast<int> (rack.getProperty (rackProperty, 0)) != 1 || uses.contains (id.toString())) return fail();
        uses[id.toString()] = 0;
        const auto macros = rack.getChildWithName (macrosType);
        if (macros.getNumChildren() != Session::rackMacroCount) return fail();
        for (const auto& macro : macros)
            if (! macro.hasType (macroType) || macro[te::IDs::name].toString().trim().isEmpty() || macro[te::IDs::name].toString().length() > 64
                || ! finiteProperty (macro.getProperty (te::IDs::value, 0), 0, 1)) return fail();
        juce::String last;
        for (const auto& instance : rack)
        {
            if (! instance.hasType (te::IDs::PLUGININSTANCE)) continue;
            const auto plugin = instance.getChild (0);
            const auto type = plugin[te::IDs::type].toString();
            if (instance.getNumChildren() != 1 || ! plugin.hasType (te::IDs::PLUGIN)) return fail();
            if (type == TrackMixer::xmlTypeName)
            {
                if (plugin[chainNameProperty].toString().trim().isEmpty() || plugin[chainNameProperty].toString().length() > 128
                    || ! finiteProperty (plugin.getProperty (chainMuteProperty, 0), 0, 1) || ! finiteProperty (plugin.getProperty (chainSoloProperty, 0), 0, 1)
                    || plugin.getChildWithName (te::IDs::AUTOMATIONCURVE).getNumChildren() > 0) return fail();
            }
            else if (type == NativeEffectPlugin::xmlTypeName)
            {
                const auto kind = static_cast<Session::EffectKind> (static_cast<int> (plugin.getProperty ("effectKind", 0)));
                if (effectIsMidi (kind) || effectIsInstrument (kind)) return fail();
                std::set<int> mapped;
                for (const auto& entry : juce::StringArray::fromTokens (plugin[macroMapProperty].toString(), ";", ""))
                {
                    const auto fields = juce::StringArray::fromTokens (entry, ",", "");
                    if (fields.size() != 4 || ! finiteProperty (fields[0], 0, Session::rackMacroCount - 1) || ! finiteProperty (fields[1], 0, 127)
                        || ! finiteProperty (fields[2], 0, 1) || ! finiteProperty (fields[3], 0, 1)
                        || fields[0].getIntValue() != fields[0].getDoubleValue() || fields[1].getIntValue() != fields[1].getDoubleValue()
                        || fields[1].getIntValue() >= (int) effectParameters (kind).size() || ! mapped.insert (fields[1].getIntValue()).second) return fail();
                    for (const auto& curve : plugin)
                        if (curve.hasType (te::IDs::AUTOMATIONCURVE) && curve.getNumChildren() > 0
                            && curve[te::IDs::paramID].toString() == effectParameters (kind)[(size_t) fields[1].getIntValue()].key) return fail();
                }
            }
            else if (type != UtilityPlugin::xmlTypeName && type != te::ExternalPlugin::xmlTypeName) return fail();
            if (type != NativeEffectPlugin::xmlTypeName && plugin.hasProperty (macroMapProperty)) return fail();
            last = type;
        }
        if (last.isNotEmpty() && last != TrackMixer::xmlTypeName) return fail();
    }
    bool valid = true;
    const auto visit = [&] (auto&& self, const juce::ValueTree& node) -> void
    {
        if (node.hasType (te::IDs::PLUGIN) && node[te::IDs::type].toString() == te::RackInstance::xmlTypeName)
        {
            const auto found = uses.find (node[te::IDs::rackType].toString());
            if (found == uses.end() || ++found->second > 1 || ! node.getParent().hasType (te::IDs::TRACK)) valid = false;
        }
        if (node.hasType (te::IDs::RACKS)) return;
        for (const auto& child : node) self (self, child);
    };
    visit (visit, state);
    if (! valid) return fail();
    for (const auto& use : uses) if (use.second != 1) return fail();
    return Result::ok();
}
// At most one Track Delay per track, within range.
Result validateTrackDelays (const juce::ValueTree& state)
{
    if (state.hasType (te::IDs::PLUGIN) && state[te::IDs::type].toString() == TrackDelayPlugin::xmlTypeName)
    {
        int count = 0;
        for (const auto& sibling : state.getParent())
            if (sibling.hasType (te::IDs::PLUGIN) && sibling[te::IDs::type].toString() == TrackDelayPlugin::xmlTypeName) ++count;
        if (! state.getParent().hasType (te::IDs::TRACK) || count != 1
            || ! finiteProperty (state.getProperty (trackDelayProperty, 0), -Session::maximumTrackDelayMs, Session::maximumTrackDelayMs))
            return Result::fail ("The project contains an invalid track delay.");
    }
    for (const auto& child : state) if (auto result = validateTrackDelays (child); result.failed()) return result;
    return Result::ok();
}
Result validateLocators (const juce::ValueTree& state)
{
    std::map<String,int> clipIds;
    std::vector<juce::ValueTree> markers;
    const auto collect = [&] (auto&& self, const juce::ValueTree& node) -> void
    {
        if (te::Clip::isClipState (node)) ++clipIds[te::EditItemID::fromID (node).toString()];
        if (node.hasType (te::IDs::MARKERCLIP)) markers.push_back (node);
        for (const auto& child : node) self (self, child);
    };
    collect (collect, state);
    if (markers.size() > 4096) return Result::fail ("This project exceeds the supported locator count.");
    for (const auto& node : markers)
    {
        const auto read = [&] (const juce::Identifier& key, double& result)
        {
            const auto value = node[key].toString().trim().toStdString(); char* end = nullptr;
            result = std::strtod (value.c_str(), &end);
            return ! value.empty() && end == value.c_str()+value.size() && std::isfinite (result);
        };
        double start=0,length=0,sync=0;
        const auto id = te::EditItemID::fromID (node);
        if (! node.getParent().hasType (te::IDs::MARKERTRACK) || ! id.isValid() || clipIds[id.toString()] != 1
            || ! read (te::IDs::start,start) || ! read (te::IDs::length,length) || ! read (te::IDs::sync,sync)
            || sync != te::Clip::syncAbsolute || length < 0 || start+length > te::Edit::maximumLength
            || ! validLocator (node[te::IDs::name].toString(),start))
            return Result::fail ("The project contains invalid or ambiguous locator data.");
    }
    return Result::ok();
}

Session::WarpSettings warpSettings (te::WaveAudioClip& clip)
{
    Session::WarpSettings result;
    const auto source = clip.getSourceLength().inSeconds();
    result.bpm = Session::estimateClipTempo (source);
    if (! clip.getAutoTempo()) return result;
    result.mode = clip.getTimeStretchMode() == te::TimeStretcher::signalsmithCheaper ? Session::WarpMode::efficient : Session::WarpMode::stretch;
    if (const auto bpm = clip.getLoopInfo().getBpm (clip.getWaveInfo()); std::isfinite (bpm) && bpm > 0) result.bpm = bpm;
    return result;
}
// Engine controller types for Halftrack lanes, and the lane value of a stored event.
int engineControllerType (int lane)
{ return lane == Session::pitchBendLane ? te::MidiControllerEvent::pitchWheelType : lane == Session::channelPressureLane ? te::MidiControllerEvent::channelPressureType : lane; }
int controllerLane (int type)
{
    if (type >= 0 && type < 120) return type;
    if (type == te::MidiControllerEvent::pitchWheelType) return Session::pitchBendLane;
    if (type == te::MidiControllerEvent::channelPressureType) return Session::channelPressureLane;
    return -1;
}
int laneValue (int lane, int stored) { return lane == Session::pitchBendLane ? stored - 8192 : stored >> 7; }
int storedValue (int lane, int value) { return lane == Session::pitchBendLane ? value + 8192 : value << 7; }
Session::ClipInfo describeClip (te::Clip& clip)
{
    Session::ClipInfo result;
    const auto position = clip.getPosition();
    result.id = clip.itemID.toString(); result.name = clip.getName(); result.active = !clip.isMuted();
    result.start = position.time.getStart().inSeconds(); result.length = position.time.getLength().inSeconds();
    result.sourceOffset = position.offset.inSeconds();
    result.follow = clipFollowSettings (clip.state);
    result.launch = clipLaunchSettings (clip.state);
    if (clip.usesSourceFile()) result.file = clip.getSourceFileReference().getFile();
    if (auto* audio = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        result.isAudio = true; result.audio = audioSettings (*audio);
        result.sourceLength = audio->getSourceLength().inSeconds(); result.canEditAudioRange = canEditAudioRange (*audio);
        result.canEditAudioFades = audio->getClipSlot() == nullptr;
        result.reversed = audio->getIsReversed(); result.canReverse = canReverseAudio (*audio);
        result.warp = warpSettings (*audio); result.canWarp = ! audio->getIsReversed() && ! audio->isUsingARA() && ! audio->isGrouped();
    }
    if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
    {
        result.isMidi = true; result.canEditMidiClip = canCopyMidiClip (*midi);
        const auto beats = clip.edit.tempoSequence.toBeats (position.time);
        result.startBeat = beats.getStart().inBeats(); result.lengthBeats = beats.getLength().inBeats();
        for (auto* note : midi->getSequence().getNotes())
            result.notes.push_back ({ note->state.getProperty (midiNoteIDProperty).toString(), note->getNoteNumber(),
                                     note->getStartBeat().inBeats(), note->getLengthBeats().inBeats(), note->getVelocity() / 127.0f, !note->isMute() });
        std::map<int, Session::ControllerLane> lanes;
        for (auto* event : midi->getSequence().getControllerEvents())
            if (const int lane = controllerLane (event->getType()); lane >= 0)
            {
                auto& entry = lanes[lane]; entry.controller = lane;
                entry.points.push_back ({ event->getBeatPosition().inBeats(), laneValue (lane, event->getControllerValue()) });
            }
        for (auto& [lane, entry] : lanes)
        {
            std::stable_sort (entry.points.begin(), entry.points.end(), [] (const auto& a, const auto& b) { return a.beat < b.beat; });
            result.controllers.push_back (std::move (entry));
        }
    }
    return result;
}

class Behaviour final : public te::EngineBehaviour
{
public:
    explicit Behaviour (bool devices) : useDevices (devices) {}
    bool autoInitialiseDeviceManager() override { return useDevices; }
    bool shouldOpenAudioInputByDefault() override { return false; }
    bool usePhysicalMidiDevices() override { return useDevices; }
    bool shouldOpenMidiInputByDefault() override { return false; }
    bool addSystemAudioIODeviceTypes() override { return useDevices; }
    bool canScanPluginsOutOfProcess() override { return useDevices; }
    // Pre sends and soloed returns still need content from a dry-muted source.
    bool shouldProcessMutedTracks() override { return true; }
    // Device chains are open-ended; the engine's 16-plugin default includes the mixer and meter.
    te::EditLimits getEditLimits() override { te::EditLimits limits; limits.maxPluginsOnTrack = 64; return limits; }
    File getFileForNewAudioRecording (te::Track& track, const String& extension) override
    {
        auto base = te::EditFileOperations (track.edit).getEditFile();
        auto dir = base.getSiblingFile (base.getFileNameWithoutExtension() + " Assets");
        dir.createDirectory();
        return dir.getNonexistentChildFile (File::createLegalFileName (track.getName()) + " Take", extension, true);
    }
private:
    bool useDevices;
};

class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (te::Plugin& p, juce::AudioPluginInstance& processor)
        : DocumentWindow (p.getName(), juce::Colour (0xff262724), closeButton), plugin (p)
    {
        auto* editor = processor.createEditorAndMakeActive();
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (processor);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setUsingNativeTitleBar (true);
        centreWithSize (juce::jmax (getWidth(), 320), juce::jmax (getHeight(), 200));
        setVisible (true);
    }
    PluginWindow (te::Plugin& p, std::unique_ptr<juce::Component> editor)
        : DocumentWindow (p.getName(), juce::Colour (0xff262724), closeButton), plugin (p)
    {
        setContentOwned (editor.release(), true);
        setResizable (false, false); setUsingNativeTitleBar (true);
        centreWithSize (getWidth(), getHeight()); setVisible (true);
    }
    ~PluginWindow() override
    {
        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
            editor->getAudioProcessor()->editorBeingDeleted (editor);
        clearContentComponent();
    }
    void closeButtonPressed() override { plugin.windowState->closeWindowExplicitly(); }
private:
    te::Plugin& plugin;
};

class UIBehaviour final : public te::UIBehaviour
{
public:
    explicit UIBehaviour (Session& s, bool showProgress) : owner (s), showProgressWindow (showProgress) {}
    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& state) override
    {
        if (auto* p = dynamic_cast<te::Plugin::WindowState*> (&state))
            if (dynamic_cast<UtilityPlugin*> (&p->plugin))
                return std::make_unique<PluginWindow> (p->plugin, makeUtilityEditor (owner, p->plugin.itemID.toString()));
        if (auto* p = dynamic_cast<te::Plugin::WindowState*> (&state))
            if (dynamic_cast<NativeEffectPlugin*> (&p->plugin))
                return std::make_unique<PluginWindow> (p->plugin, makeNativeEffectEditor (owner, p->plugin.itemID.toString()));
        if (auto* p = dynamic_cast<te::Plugin::WindowState*> (&state))
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (&p->plugin))
                if (! external->isInitialisingAsync())
                    if (auto* processor = external->getAudioPluginInstance())
                        return std::make_unique<PluginWindow> (p->plugin, *processor);
        return {};
    }
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
    {
        // The engine builds and releases the render graph on the message thread.
        // Only runJob belongs on the worker, as in Tracktion's TaskRunner example.
        struct TaskRunner final : juce::Thread
        {
            explicit TaskRunner (te::ThreadPoolJobWithProgress& t) : Thread (t.getJobName()), job (t) {}
            ~TaskRunner() override
            {
                job.signalJobShouldExit();
                signalThreadShouldExit();
                // Never force-kill a thread while it is inside a plug-in.
                waitForThreadToExit (-1);
            }
            void run() override
            {
                while (! threadShouldExit() && ! job.shouldExit())
                    if (job.runJob() == juce::ThreadPoolJob::jobHasFinished) break;
            }
            te::ThreadPoolJobWithProgress& job;
        } runner (task);

        activeTask = &task;
        cancelled = false;
        double progress = 0;
        std::unique_ptr<juce::AlertWindow> window;
        if (showProgressWindow)
        {
            window = std::make_unique<juce::AlertWindow> (task.getJobName(), "Rendering audio...", juce::MessageBoxIconType::NoIcon);
            window->setEscapeKeyCancels (false);
            window->addProgressBarComponent (progress);
            window->addButton ("Cancel", 0);
            window->getButton (0)->onClick = [this, alert = window.get()]
            {
                cancelTask();
                alert->getButton (0)->setEnabled (false);
                alert->setMessage ("Cancelling export...");
            };
            window->enterModalState();
        }
        if (! runner.startThread()) cancelTask();
        while (runner.isThreadRunning())
        {
            progress = juce::jlimit (0.0, 1.0, static_cast<double> (task.getCurrentTaskProgress()));
            if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (10))
            {
                cancelTask();
                juce::Thread::sleep (2);
            }
            // Closing a native alert is also cancellation; keep the worker alive
            // until the engine observes its exit flag and cleans up its writer.
            if (window && ! window->isCurrentlyModal (false)) cancelTask();
        }
        if (window) { window->exitModalState (0); window->setVisible (false); }
        activeTask = nullptr;
    }
    void cancelTask() { if (activeTask) { cancelled = true; activeTask->signalJobShouldExit(); } }
    bool cancelled = false;
private:
    Session& owner;
    bool showProgressWindow;
    te::ThreadPoolJobWithProgress* activeTask = nullptr;
};

// A selected export renders from time zero before slicing: starting a native
// render at the selection would cold-start instruments and effect history.
// Both passes are bounded streaming work, off the message and audio threads.
class RangeExportTask final : public te::ThreadPoolJobWithProgress
{
public:
    RangeExportTask (File source, File destination, juce::AudioFormat& format,
                     Session::AudioExportSettings options, juce::int64 first, juce::int64 last)
        : ThreadPoolJobWithProgress (options.normalizePeak ? "Normalize selected audio" : "Write selected audio"),
          inputFile (std::move (source)), outputFile (std::move (destination)), outputFormat (format),
          settings (std::move (options)), firstFrame (first), frameCount (last - first), measuring (settings.normalizePeak) {}
    float getCurrentTaskProgress() override { return progress.load (std::memory_order_relaxed); }
    bool canCancel() const override { return true; }
    juce::Result result = Result::fail ("The selected audio export did not finish.");

    JobStatus runJob() override
    {
        if (shouldExit()) return fail ("Export cancelled.");
        if (! reader)
        {
            juce::WavAudioFormat wav;
            auto stream = inputFile.createInputStream();
            if (! stream) return fail ("Could not open the floating-point export intermediate.");
            reader.reset (wav.createReaderFor (stream.release(), true));
            // The pinned native WAV writer defaults to float for 32-bit output.
            // Verify that contract here so no integer-clipped intermediate can
            // silently contaminate normalization or an over-range float export.
            if (! reader || ! reader->usesFloatingPointData || reader->bitsPerSample != 32
                || reader->sampleRate != settings.sampleRate || reader->numChannels == 0
                || reader->numChannels > 64 || reader->lengthInSamples < firstFrame + frameCount)
                return fail ("Could not read the floating-point export intermediate.");
            buffer.setSize (static_cast<int> (reader->numChannels), blockFrames);
            if (settings.mono) mono.setSize (1, blockFrames);
        }
        if (! measuring && ! writer)
        {
            juce::StringPairArray metadata;
            te::AudioFileUtils::addBWAVStartToMetadata (metadata, firstFrame);
            writer.reset (te::AudioFileUtils::createWriterFor (&outputFormat, outputFile,
                settings.sampleRate, settings.mono ? 1u : reader->numChannels, settings.bitDepth, metadata, 0));
            if (! writer) return fail ("Could not create the selected audio file.");
        }
        const int count = static_cast<int> (std::min<juce::int64> (blockFrames, frameCount - position));
        if (! reader->read (&buffer, 0, count, firstFrame + position, true, true))
            return fail ("Could not read the selected audio frames.");
        auto* selected = &buffer;
        if (settings.mono)
        {
            mono.clear();
            const float scale = 1.0f / static_cast<float> (buffer.getNumChannels());
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                mono.addFrom (0, 0, buffer, channel, 0, count, scale);
            selected = &mono;
        }
        for (int channel = 0; channel < selected->getNumChannels(); ++channel)
            for (int n = 0; n < count; ++n)
                if (! std::isfinite (selected->getSample (channel, n)))
                    return fail ("The selected mix contains nonfinite audio.");
        if (measuring) peak = std::max (peak, selected->getMagnitude (0, count));
        else
        {
            selected->applyGain (0, count, gain);
            if (! writer->writeFromAudioSampleBuffer (*selected, 0, count))
                return fail ("Could not write the selected audio frames.");
        }
        position += count;
        const double passProgress = static_cast<double> (position) / static_cast<double> (frameCount);
        progress.store (static_cast<float> (settings.normalizePeak ? (measuring ? 0.5 * passProgress : 0.5 + 0.5 * passProgress) : passProgress), std::memory_order_relaxed);
        if (position != frameCount) return jobNeedsRunningAgain;
        if (measuring)
        {
            // Match the existing export's conservative sample-peak headroom and
            // 40 dB boost cap, measured only after selecting/downmixing frames.
            gain = juce::jlimit (0.0f, 100.0f, juce::Decibels::decibelsToGain (settings.peakCeilingDb)
                / (peak * 1.005f + 2.0f / 32768.0f));
            measuring = false; position = 0;
            return jobNeedsRunningAgain;
        }
        // AIFF/FLAC finalize on destruction and do not implement flush(). Verify
        // the finalized header and frame count before replacing the destination.
        writer.reset(); reader.reset();
        auto finishedStream = outputFile.createInputStream();
        if (! finishedStream) return fail ("Could not reopen the selected audio file.");
        std::unique_ptr<juce::AudioFormatReader> finished (outputFormat.createReaderFor (finishedStream.release(), true));
        if (! finished || finished->lengthInSamples != frameCount || finished->sampleRate != settings.sampleRate)
            return fail ("Could not finish the selected audio file.");
        result = Result::ok();
        return jobHasFinished;
    }
private:
    JobStatus fail (const String& message)
    { writer.reset(); reader.reset(); result = Result::fail (message); return jobHasFinished; }
    static constexpr int blockFrames = 16384;
    File inputFile, outputFile;
    juce::AudioFormat& outputFormat;
    Session::AudioExportSettings settings;
    juce::int64 firstFrame, frameCount, position = 0;
    bool measuring;
    float peak = 0.0001f, gain = 1;
    std::atomic<float> progress { 0 };
    std::unique_ptr<juce::AudioFormatReader> reader;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::AudioBuffer<float> buffer, mono;
};

struct Meter
{
    explicit Meter (te::LevelMeasurer& m) : measurer (&m) { m.addClient (client); }
    ~Meter() { if (measurer != nullptr) measurer->removeClient (client); }
    juce::WeakReference<te::LevelMeasurer> measurer;
    te::LevelMeasurer::Client client;
    float read (int channel) { return juce::Decibels::decibelsToGain (client.getAndClearAudioLevel (channel).dB, -90.0f); }
};

juce::ValueTree findStateWithID (juce::ValueTree tree, const juce::var& id)
{
    if (tree.getProperty (te::IDs::id) == id)
        return tree;
    for (auto child : tree)
        if (auto found = findStateWithID (child, id); found.isValid())
            return found;
    return {};
}
}

#if HALFTRACK_TESTS_ENABLED
// Tests run the private incremental worker directly so cancellation at each
// conversion phase is deterministic and does not depend on disk/thread timing.
juce::Result runRangeExportTaskCancellationTests (const juce::File& directory)
{
    const auto input = directory.getChildFile ("worker-float-input.wav");
    const auto destination = directory.getChildFile ("worker-protected.wav");
    juce::WavAudioFormat wav;
    {
        juce::AudioBuffer<float> source (2, 65536);
        for (int channel = 0; channel < 2; ++channel)
            juce::FloatVectorOperations::fill (source.getWritePointer (channel), channel == 0 ? .25f : -.125f, source.getNumSamples());
        auto stream = std::unique_ptr<juce::OutputStream> (input.createOutputStream());
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000)
            .withNumChannels (2).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (! writer || ! writer->writeFromAudioSampleBuffer (source, 0, source.getNumSamples()))
            return Result::fail ("Could not create range worker fixture.");
    }
    if (! destination.replaceWithText ("preserve existing destination")) return Result::fail ("Could not create protected worker destination.");
    {
        juce::TemporaryFile temporary (destination);
        RangeExportTask missing (directory.getChildFile ("missing-worker-source.wav"), temporary.getFile(), wav, {}, 0, 65536);
        if (missing.runJob() != juce::ThreadPoolJob::jobHasFinished || missing.result.wasOk()
            || temporary.getFile().exists() || destination.loadFileAsString() != "preserve existing destination")
            return Result::fail ("Missing intermediate did not fail without changing the destination.");
    }
    // Cancel during peak measurement, at its boundary, during normalized output,
    // and during non-normalized output. Every task is still incomplete.
    for (const int stage : { 0, 1, 2, 3 })
    {
        juce::TemporaryFile temporary (destination);
        Session::AudioExportSettings settings; settings.bitDepth = 24; settings.normalizePeak = stage != 3;
        RangeExportTask task (input, temporary.getFile(), wav, settings, 0, 65536);
        const int calls = stage == 0 || stage == 3 ? 1 : stage == 1 ? 4 : 5;
        for (int i = 0; i < calls; ++i)
            if (task.runJob() != juce::ThreadPoolJob::jobNeedsRunningAgain)
                return Result::fail ("Range worker unexpectedly finished before cancellation.");
        const auto progress = task.getCurrentTaskProgress();
        if ((stage == 0 && ! (progress > 0 && progress < .5f)) || (stage == 1 && progress != .5f)
            || (stage == 2 && ! (progress > .5f && progress < 1)) || (stage == 3 && ! (progress > 0 && progress < 1)))
            return Result::fail ("Cancellation did not reach the expected range worker phase.");
        task.signalJobShouldExit();
        if (task.runJob() != juce::ThreadPoolJob::jobHasFinished || task.result.wasOk()
            || task.getCurrentTaskProgress() != progress || destination.loadFileAsString() != "preserve existing destination")
            return Result::fail ("Range worker cancellation changed output or continued processing.");
    }
    return Result::ok();
}
#endif

struct Session::Impl
{
    explicit Impl (Session& s, bool devices, std::unique_ptr<juce::AudioIODeviceType> customDeviceType)
        : owner (s), engine (devices ? "Halftrack" : "Halftrack Tests", std::make_unique<UIBehaviour> (s, devices),
                            std::make_unique<Behaviour> (devices)), useDevices (devices)
    {
        engine.getPluginManager().createBuiltInType<TrackMixer>();
        engine.getPluginManager().createBuiltInType<TrackDelayPlugin>();
        engine.getPluginManager().createBuiltInType<SubtractiveSynthPlugin>();
        engine.getPluginManager().createBuiltInType<UtilityPlugin>();
        engine.getPluginManager().createBuiltInType<NativeEffectPlugin>();
        engine.getPluginManager().setUsesSeparateProcessForScanning (devices);
        if (customDeviceType)
        {
            auto& dm = engine.getDeviceManager();
            dm.deviceManager.addAudioDeviceType (std::move (customDeviceType));
            dm.initialise (2, 2);
            useDevices = true;
        }
        scratch = File::getSpecialLocation (File::tempDirectory).getChildFile ("Halftrack-" + juce::Uuid().toString());
        scratch.createDirectory();
        // JUCE treats a nonexistent base path as a directory when making relative paths.
        // Materialise the scratch edit location before Tracktion stores source references.
        scratch.getChildFile ("Untitled.halftrack").create();
    }
    ~Impl()
    {
        gesture.reset();
        meters.clear();
        edit.reset();
        // Unsaved sessions are deliberately retained in the temp directory for recovery.
    }
    te::AudioTrack* track (int index) const
    {
        auto tracks = te::getAudioTracks (*edit);
        return juce::isPositiveAndBelow (index, tracks.size()) ? tracks[index] : nullptr;
    }
    te::AudioTrack* track (const String& id) const
    {
        for (auto* t : te::getAudioTracks (*edit)) if (t->itemID.toString() == id) return t;
        return nullptr;
    }
    te::WaveAudioClip* audioClip (const String& id) const
    {
        te::WaveAudioClip* result = nullptr;
        for (auto* t : te::getAudioTracks (*edit)) for (auto* c : allTrackClips (*t))
            if (c->itemID.toString() == id)
            {
                if (result) return nullptr;
                result = dynamic_cast<te::WaveAudioClip*> (c);
            }
        return result;
    }
    te::MarkerClip* locator (const String& id) const
    {
        for (auto* marker : edit->getMarkerManager().getMarkers())
            if (marker->itemID.toString() == id) return marker;
        return nullptr;
    }
    te::MidiClip* midiClip (const String& id) const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* c : allTrackClips (*t)) if (c->itemID.toString() == id) return dynamic_cast<te::MidiClip*> (c);
        return nullptr;
    }
    te::ClipSlot* slot (int trackIndex, int sceneIndex) const
    {
        if (! juce::isPositiveAndBelow (sceneIndex, edit->getSceneList().getNumScenes())) return nullptr;
        if (auto* t = track (trackIndex); t && ! isReturn (*t)) return t->getClipSlotList().getClipSlots()[sceneIndex];
        return nullptr;
    }
    void cancelLaunches()
    {
        for (auto* t : te::getAudioTracks (*edit))
        {
            for (auto* slot : t->getClipSlotList().getClipSlots())
                if (auto* c = slot->getClip()) if (auto h = c->getLaunchHandle()) { h->stop (std::nullopt); h->setLooping (std::nullopt); }
            t->launcherSilence.stop (std::nullopt);
            // Cancel stale claims without releasing already acquired ownership.
            t->launcherOwnership.fetch_add (2, std::memory_order_acq_rel);
        }
        heldSlots.clear();
    }
    // Next boundary on a launch grid, wrapping into the transport loop like the engine's play head.
    std::optional<te::MonotonicBeat> boundaryOn (te::LaunchQType type) const
    {
        auto& transport = edit->getTransport();
        if (! transport.isPlaying() || type == te::LaunchQType::none) return {};
        if (auto* context = transport.getCurrentPlaybackContext())
            if (auto sync = context->getSyncPoint())
            {
                auto next = te::getNext (type, edit->tempoSequence, sync->beat);
                auto distance = next - sync->beat;
                if (transport.looping)
                {
                    const auto loop = edit->tempoSequence.toBeats (transport.getLoopRange());
                    if (loop.contains (sync->beat) && next >= loop.getEnd())
                    {
                        auto wrapped = te::getNext (type, edit->tempoSequence, loop.getStart());
                        if (! loop.contains (wrapped)) wrapped = loop.getStart();
                        distance = loop.getEnd() - sync->beat + (wrapped - loop.getStart());
                    }
                }
                return te::MonotonicBeat { sync->monotonicBeat.v + distance };
            }
        return {};
    }
    std::optional<te::MonotonicBeat> launchBoundary() const { return boundaryOn (edit->getLaunchQuantisation().type); }
    // The grid a clip launches on: its own setting, or the project grid for Global.
    te::LaunchQType clipGrid (te::Clip* clip) const
    {
        const auto grid = clip ? clipLaunchSettings (clip->state).quantisation : Session::ClipQuantisation::global;
        return grid == Session::ClipQuantisation::global ? edit->getLaunchQuantisation().type : clipLaunchGrids[static_cast<size_t> (grid)];
    }
    std::optional<te::MonotonicBeat> clipBoundary (te::Clip* clip) const { return boundaryOn (clipGrid (clip)); }
    // One grid step at the play head, following the current time signature.
    std::optional<te::BeatDuration> gridStep (te::LaunchQType type) const
    {
        if (type == te::LaunchQType::none) return {};
        auto& transport = edit->getTransport();
        auto beat = edit->tempoSequence.toBeats (transport.getPosition());
        if (auto* context = transport.getCurrentPlaybackContext()) if (auto sync = context->getSyncPoint()) beat = sync->beat;
        const auto first = te::getNext (type, edit->tempoSequence, beat);
        const auto second = te::getNext (type, edit->tempoSequence, first + te::BeatDuration::fromBeats (1.0e-6));
        return second - first;
    }
    void queueTrack (te::AudioTrack& track, te::Clip* selected, std::optional<te::MonotonicBeat> boundary)
    {
        if (selected) track.launcherSilence.stop (boundary);
        else track.launcherSilence.play (boundary);
        // A legato clip starts in phase with the clip already playing on its track.
        te::LaunchHandle* playing = nullptr;
        if (selected != nullptr && clipLaunchSettings (selected->state).legato)
            for (auto* slot : track.getClipSlotList().getClipSlots())
                if (auto* c = slot->getClip(); c != nullptr && c != selected)
                    if (auto h = c->getLaunchHandle(); h && h->getPlayingStatus() == te::LaunchHandle::PlayState::playing) playing = h.get();
        // Any launch on a track ends a held Repeat on it.
        std::erase_if (heldSlots, [&] (const auto& h) { return h.trackId == track.itemID.toString(); });
        for (auto* slot : track.getClipSlotList().getClipSlots())
            if (auto* c = slot->getClip())
                if (auto h = c->getLaunchHandle())
                {
                    h->setLooping (std::nullopt);
                    if (c == selected) { if (playing != nullptr) h->playSynced (*playing, boundary); else h->play (boundary); }
                    else h->stop (boundary);
                }
    }
    // Launch buttons held in Gate or Repeat mode.
    struct HeldSlot { String trackId, clipId; };
    std::vector<HeldSlot> heldSlots;
    // Session recordings into empty slots, from their punch-in to a grid-quantised stop.
    struct SlotRecording { String trackId; te::EditItemID slotId; te::TimePosition start; bool stopping = false; };
    std::vector<SlotRecording> slotRecordings;
    const SlotRecording* slotRecording (te::EditItemID slotId) const
    { for (const auto& r : slotRecordings) if (r.slotId == slotId) return &r; return nullptr; }
    SlotRecording* trackRecording (const String& trackId)
    { for (auto& r : slotRecordings) if (r.trackId == trackId) return &r; return nullptr; }
    // Unlooped edit time of a monotonic beat ahead of the play head, or the play head itself.
    te::TimePosition unloopedTime (std::optional<te::MonotonicBeat> beat) const
    {
        if (auto* context = edit->getTransport().getCurrentPlaybackContext())
        {
            if (auto sync = context->getSyncPoint(); sync && beat)
            {
                const auto target = sync->beat + (beat->v - sync->monotonicBeat.v);
                return sync->unloopedTime + (edit->tempoSequence.toTime (target) - sync->time);
            }
            return context->getUnloopedPosition();
        }
        return edit->getTransport().getPosition();
    }
    // Ends a slot recording at the next launch boundary. The finished take loops over its
    // recorded length and, when asked, plays in phase from that boundary.
    void finishSlotRecording (SlotRecording& recording, bool playTake, bool immediately)
    {
        auto* t = track (recording.trackId); auto* in = t ? input (*t) : nullptr;
        if (in == nullptr) { std::erase_if (slotRecordings, [&] (const auto& r) { return &r == &recording; }); return; }
        const auto boundary = immediately ? std::nullopt : launchBoundary();
        const auto end = std::max (recording.start + te::TimeDuration::fromSeconds (.001), unloopedTime (boundary));
        recording.stopping = true;
        te::InputDeviceInstance::StopRecordingParameters params;
        params.targetsToStop = { recording.slotId };
        params.unloopedTimeToEndRecording = end;
        transaction ("Record Session clip");
        const auto trackId = recording.trackId; const auto slotId = recording.slotId; const auto start = recording.start;
        if (immediately)
        {
            // A pending grid stop completes from its own callback inside this call; otherwise complete here.
            [[maybe_unused]] auto clips = in->stopRecording (params);
            completeSlotRecording (trackId, slotId, start, end, boundary, playTake);
            return;
        }
        in->stopRecording (params, [this, trackId, slotId, start, end, boundary, playTake] (tl::expected<te::Clip::Array, juce::String>)
                           { completeSlotRecording (trackId, slotId, start, end, boundary, playTake); });
    }
    void completeSlotRecording (const String& trackId, te::EditItemID slotId, te::TimePosition start, te::TimePosition end,
                                std::optional<te::MonotonicBeat> boundary, bool playTake)
    {
        // Each take completes once, whichever stop reaches it first.
        if (slotRecording (slotId) == nullptr) return;
        std::erase_if (slotRecordings, [&] (const auto& r) { return r.slotId == slotId; });
        auto* t = track (trackId);
        auto* slot = te::findClipSlotForID (*edit, slotId);
        if (t != nullptr) if (auto* in = input (*t)) [[maybe_unused]] auto removed = in->removeTarget (slotId, nullptr);
        if (t == nullptr || slot == nullptr) { owner.sendChangeMessage(); return; }
        // The take covers whole beats from punch-in to the grid stop; an empty MIDI take is still a clip.
        const auto length = std::max (1.0, std::round ((edit->tempoSequence.toBeats (end) - edit->tempoSequence.toBeats (start)).inBeats() * 960.0) / 960.0);
        const te::BeatRange beats { te::BeatPosition(), te::BeatPosition::fromBeats (length) };
        auto* clip = slot->getClip();
        if (clip == nullptr && static_cast<bool> (t->state.getProperty (midiTrackProperty, false)))
            clip = te::insertMIDIClip (*slot, "MIDI clip", edit->tempoSequence.toTime (beats)).get();
        if (clip == nullptr) { changed(); return; }
        auto* undo = &edit->getUndoManager();
        clip->setPosition ({ { te::TimePosition(), edit->tempoSequence.toTime (beats.getEnd()) }, te::TimeDuration() });
        if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
        {
            midi->setSyncType (te::Clip::syncBarsBeats); midi->setLoopRangeBeats (beats);
            for (auto* note : midi->getSequence().getNotes())
                if (! note->state.hasProperty (midiNoteIDProperty)) note->state.setProperty (midiNoteIDProperty, juce::Uuid().toString(), undo);
        }
        else if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip))
        {
            // The take was recorded at the project tempo, so it follows tempo changes like imported slot audio.
            auto info = wave->getLoopInfo(); info.setBpm (edit->tempoSequence.getTempo (0)->getBpm(), wave->getAudioFile().getInfo());
            wave->setLoopInfo (info); wave->setAutoTempo (true); wave->setAutoPitch (false); wave->setUsesProxy (false);
            wave->setLoopRangeBeats (beats);
        }
        if (playTake) queueTrack (*t, clip, boundary);
        changed();
    }
    te::FolderTrack* group (const String& id) const
    {
        for (auto* track : te::getAllTracks (*edit))
            if (auto* folder = dynamic_cast<te::FolderTrack*> (track); folder && folder->isSubmixFolder() && folder->itemID.toString() == id) return folder;
        return nullptr;
    }
    static SubtractiveSynthPlugin* synth (te::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList.getPlugins())
            if (auto* instrument = dynamic_cast<SubtractiveSynthPlugin*> (plugin)) return instrument;
        return nullptr;
    }
    static NativeEffectPlugin* nativeInstrument (te::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList.getPlugins())
            if (auto* native = dynamic_cast<NativeEffectPlugin*> (plugin); native && effectIsInstrument (native->getKind())) return native;
        return nullptr;
    }
    // The MIDI track's sound source: the built-in synth or a native instrument.
    static te::Plugin* instrument (te::AudioTrack& track)
    {
        if (auto* s = synth (track)) return s;
        return nativeInstrument (track);
    }
    Result validateNote (te::MidiClip& clip, int pitch, double start, double length, float velocity) const
    {
        const auto beats = edit->tempoSequence.toBeats (clip.getPosition().time).getLength().inBeats();
        if (pitch < 0 || pitch > 127 || ! std::isfinite (start) || ! std::isfinite (length)
            || start < 0 || length < 1.0 / 960 || start + length > beats + 0.0000001
            || ! std::isfinite (velocity) || velocity <= 0 || velocity > 1)
            return Result::fail ("Notes need a pitch from 0 to 127, positive velocity, and a duration inside the MIDI clip.");
        return Result::ok();
    }
    te::Clip* clip (int t, int c) const
    {
        if (auto* tr = track (t))
        {
            auto clips = tr->getClips();
            if (juce::isPositiveAndBelow (c, clips.size())) return clips[c];
        }
        return nullptr;
    }
    juce::Array<te::Plugin*> plugins (int index) const
    {
        juce::Array<te::Plugin*> result;
        if (auto* t = track (index))
            for (auto* p : t->pluginList.getPlugins())
                if (auto* native = dynamic_cast<NativeEffectPlugin*> (p); native != nullptr ? ! effectIsInstrument (native->getKind())
                        : dynamic_cast<te::ExternalPlugin*> (p) != nullptr || dynamic_cast<UtilityPlugin*> (p) != nullptr
                          || dynamic_cast<te::RackInstance*> (p) != nullptr) result.add (p);
        return result;
    }
    te::Plugin* plugin (int trackIndex, int pluginIndex) const
    {
        auto list = plugins (trackIndex);
        return juce::isPositiveAndBelow (pluginIndex, list.size()) ? list[pluginIndex] : nullptr;
    }
    // Audio Effect Racks. A rack's plugin list is its chains in order: each chain's devices followed
    // by the chain's mixer, a TrackMixer. The rack's connections are always rebuilt from that order.
    struct RackChain { TrackMixer* mixer = nullptr; juce::Array<te::Plugin*> devices; };
    static std::vector<RackChain> rackChains (const te::RackType& rack)
    {
        std::vector<RackChain> chains; RackChain current;
        for (auto* p : rack.getPlugins())
            if (auto* mixer = dynamic_cast<TrackMixer*> (p)) { current.mixer = mixer; chains.push_back (current); current = {}; }
            else current.devices.add (p);
        return chains;
    }
    te::RackInstance* rack (const String& id) const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
                if (auto* r = dynamic_cast<te::RackInstance*> (p); r && r->type && r->itemID.toString() == id) return r;
        return nullptr;
    }
    struct DeviceLocation { te::AudioTrack* track = nullptr; te::RackInstance* rack = nullptr; te::Plugin* plugin = nullptr; int chain = -1; };
    // Finds a device on a track or inside one of its racks.
    DeviceLocation device (const String& id) const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
            {
                if (p->itemID.toString() == id) return { t, nullptr, p, -1 };
                if (auto* r = dynamic_cast<te::RackInstance*> (p); r && r->type)
                {
                    const auto chains = rackChains (*r->type);
                    for (int c = 0; c < (int) chains.size(); ++c)
                        for (auto* d : chains[(size_t) c].devices) if (d->itemID.toString() == id) return { t, r, d, c };
                }
            }
        return {};
    }
    // A chain is named by its mixer's ID.
    DeviceLocation chain (const String& id) const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
                if (auto* r = dynamic_cast<te::RackInstance*> (p); r && r->type)
                {
                    const auto chains = rackChains (*r->type);
                    for (int c = 0; c < (int) chains.size(); ++c)
                        if (chains[(size_t) c].mixer->itemID.toString() == id) return { t, r, chains[(size_t) c].mixer, c };
                }
        return {};
    }
    // Every device on a track, including those inside its racks.
    static juce::Array<te::Plugin*> allDevices (te::AudioTrack& track)
    {
        juce::Array<te::Plugin*> result;
        for (auto* p : track.pluginList.getPlugins())
        {
            result.add (p);
            if (auto* r = dynamic_cast<te::RackInstance*> (p); r && r->type) result.addArray (r->type->getPlugins());
        }
        return result;
    }
    static int audioPins (te::Plugin& plugin, bool inputs)
    {
        juce::StringArray ins, outs; plugin.getChannelNames (&ins, &outs);
        return juce::jlimit (1, 2, (inputs ? ins : outs).size());
    }
    // Chains run in parallel from the rack input and sum at its output; a rack without chains passes its input.
    static void rewire (te::RackType& rackType)
    {
        auto* undo = rackType.getUndoManager();
        for (int i = rackType.state.getNumChildren(); --i >= 0;)
            if (rackType.state.getChild (i).hasType (te::IDs::CONNECTION)) rackType.state.removeChild (i, undo);
        const auto connect = [&rackType] (te::EditItemID source, int sourcePins, te::EditItemID destination, int destinationPins)
        {
            rackType.addConnection (source, 0, destination, 0);
            for (int pin = 1; pin <= juce::jmax (sourcePins, destinationPins); ++pin)
                rackType.addConnection (source, juce::jmin (pin, sourcePins), destination, juce::jmin (pin, destinationPins));
        };
        const auto chains = rackChains (rackType);
        if (chains.empty()) { connect ({}, 2, {}, 2); return; }
        bool soloed = false;
        for (const auto& c : chains) soloed = soloed || static_cast<bool> (c.mixer->state.getProperty (chainSoloProperty, false));
        for (const auto& c : chains)
        {
            te::EditItemID previous; int pins = 2;
            for (auto* d : c.devices) { connect (previous, pins, d->itemID, audioPins (*d, true)); previous = d->itemID; pins = audioPins (*d, false); }
            connect (previous, pins, c.mixer->itemID, 2);
            const bool audible = ! static_cast<bool> (c.mixer->state.getProperty (chainMuteProperty, false))
                && (! soloed || static_cast<bool> (c.mixer->state.getProperty (chainSoloProperty, false)));
            if (audible) connect (c.mixer->itemID, 2, {}, 2);
        }
    }
    // Appends a chain mixer, which ends a new chain after the rack's last chain.
    te::Plugin* addChainMixer (te::RackType& rackType, const String& name)
    {
        auto mixer = edit->getPluginCache().createNewPlugin (TrackMixer::xmlTypeName, {});
        if (mixer == nullptr || ! rackType.addPlugin (mixer, { .5f, .5f }, false)) return nullptr;
        mixer->state.setProperty (chainNameProperty, name, &edit->getUndoManager());
        return mixer.get();
    }
    // Moves a rack plugin so it becomes the rack's plugin at index (counting only plugins).
    void placeInRack (te::RackType& rackType, te::Plugin& plugin, int index)
    {
        auto instance = plugin.state.getParent();
        juce::Array<int> slots;
        for (int i = 0; i < rackType.state.getNumChildren(); ++i) if (rackType.state.getChild (i).hasType (te::IDs::PLUGININSTANCE)) slots.add (i);
        const int from = rackType.state.indexOf (instance);
        if (! juce::isPositiveAndBelow (index, slots.size()) || from < 0 || slots[index] == from) return;
        rackType.state.moveChild (from, slots[index], &edit->getUndoManager());
    }
    // Writes each control mapped to a macro from the macro's position, in the current transaction.
    void applyMacros (te::RackType& rackType)
    {
        const auto macros = rackType.state.getChildWithName (macrosType);
        for (auto* p : rackType.getPlugins())
            if (auto* native = dynamic_cast<NativeEffectPlugin*> (p))
            {
                auto settings = native->readSettings(); const auto before = settings;
                for (const auto& m : macroMappings (native->state))
                {
                    const float value = macros.getChild (m.macro).getProperty (te::IDs::value, 0.0f);
                    settings.values[(size_t) m.parameter] = effectProportionToValue (native->getKind(), m.parameter, m.minimum + value * (m.maximum - m.minimum));
                }
                if (settings.values != before.values) native->writeSettings (settings);
            }
    }
    // A device the user placed: on a track's device list or inside a chain (never a chain mixer).
    bool userDevice (const DeviceLocation& at) const
    {
        if (at.plugin == nullptr) return false;
        if (at.rack != nullptr) return at.chain >= 0;
        return plugins (te::getAudioTracks (*edit).indexOf (at.track)).contains (at.plugin);
    }
    // Adds a new device at the end of a chain, ahead of the chain's mixer.
    Result addToChain (const String& chainId, const juce::ValueTree& pluginState, const String& name)
    {
        const auto at = chain (chainId);
        if (! at.rack) return Result::fail ("Chain not found.");
        if (auto frozen = unfrozen (at.track); frozen.failed()) return frozen;
        if (exporting) return Result::fail ("Finish or cancel the current export first.");
        if (edit->getTransport().isRecording()) return Result::fail ("Stop recording before adding devices.");
        if (rackChains (*at.rack->type)[(size_t) at.chain].devices.size() >= 64) return Result::fail ("A chain holds at most 64 devices.");
        auto plugin = edit->getPluginCache().createNewPlugin (pluginState);
        if (! plugin) return Result::fail ("The device could not be created.");
        transaction ("Add " + name);
        at.rack->type->addPlugin (plugin, { .5f, .5f }, false);
        placeInRack (*at.rack->type, *plugin, at.rack->type->getPlugins().indexOf (at.plugin));
        rewire (*at.rack->type); changed(); return Result::ok();
    }
    static PluginInfo describeDevice (te::Plugin& p)
    {
        PluginInfo device { p.itemID.toString(), p.getName(), p.isEnabled() };
        if (auto* utility = dynamic_cast<UtilityPlugin*> (&p)) { device.isUtility = true; device.utility = utility->readSettings(); }
        if (auto* effect = dynamic_cast<NativeEffectPlugin*> (&p))
        { device.isNativeEffect = true; device.effect = effect->readSettings(); device.automation = deviceAutomation (*effect); }
        if (auto* r = dynamic_cast<te::RackInstance*> (&p); r && r->type)
        {
            device.isRack = true;
            const auto macros = r->type->state.getChildWithName (macrosType);
            for (int m = 0; m < Session::rackMacroCount; ++m)
                device.macros.push_back ({ macros.getChild (m)[te::IDs::name].toString(), macros.getChild (m).getProperty (te::IDs::value, 0.0f), {} });
            for (const auto& c : rackChains (*r->type))
            {
                RackChainInfo chain { c.mixer->itemID.toString(), { c.mixer->state[chainNameProperty].toString(), c.mixer->getVolumeDb(), c.mixer->getPan(),
                    c.mixer->state.getProperty (chainMuteProperty, false), c.mixer->state.getProperty (chainSoloProperty, false) }, {} };
                for (auto* d : c.devices)
                {
                    chain.devices.push_back (describeDevice (*d));
                    if (auto* native = dynamic_cast<NativeEffectPlugin*> (d))
                        for (const auto& m : macroMappings (native->state))
                            if (juce::isPositiveAndBelow (m.macro, (int) device.macros.size()))
                                device.macros[(size_t) m.macro].mappings.push_back ({ d->itemID.toString(), m.parameter,
                                    effectProportionToValue (native->getKind(), m.parameter, m.minimum), effectProportionToValue (native->getKind(), m.parameter, m.maximum) });
                }
                device.chains.push_back (std::move (chain));
            }
        }
        return device;
    }
    // Removes rack types that no rack on a track uses any more.
    void removeUnusedRacks()
    {
        juce::Array<te::RackType*> used;
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins()) if (auto* r = dynamic_cast<te::RackInstance*> (p); r && r->type) used.add (r->type.get());
        const auto types = edit->getRackList().getTypes();
        for (auto* type : types) if (! used.contains (type)) edit->getRackList().removeRackType (type);
    }
    File editLocation() const { return projectFile == File() ? scratch.getChildFile ("Untitled.halftrack") : projectFile; }
    File assets() const { auto f = editLocation(); return f.getSiblingFile (f.getFileNameWithoutExtension() + " Assets"); }
    void configureMidiPlayback()
    {
        // The beat sequencer reconciles edited notes against shared active-note
        // state. The legacy seconds proxy can strand voices after note removal.
        for (auto* track : te::getAudioTracks (*edit))
            for (auto* clip : allTrackClips (*track))
                if (auto* midi = dynamic_cast<te::MidiClip*> (clip)) midi->setUsesProxy (false);
    }
    void flushStructuralUpdates()
    {
        if (! edit) return;
        configureMidiPlayback();
        // Native sort actions must remain in the edit that triggered them, not
        // appear later as an unrelated Undo step when export pumps UI events.
        edit->getTrackList().dispatchPendingSort();
        edit->visitAllTracksRecursive ([] (te::Track& track)
        {
            if (auto* children = track.getSubTrackList()) children->dispatchPendingSort();
            if (auto* owner = dynamic_cast<te::ClipOwner*> (&track)) owner->dispatchPendingSort();
            if (auto* audio = dynamic_cast<te::AudioTrack*> (&track))
                for (auto* slot : audio->getClipSlotList().getClipSlots()) slot->dispatchPendingSort();
            return true;
        });
    }
    void changed()
    {
        flushStructuralUpdates();
        // Tracktion resets initialization changes on a deferred timer. Keep an
        // independent flag so that timer cannot clear a real user edit.
        explicitChanges = true;
        pristineNewDocument = false;
        refreshCrossfade(); // New, duplicated and reordered tracks pick up their crossfade gains.
        edit->markAsChanged();
        owner.sendChangeMessage();
    }
    // Each track mixer's crossfade gain from its side and the project crossfader.
    void refreshCrossfade()
    {
        const auto position = static_cast<float> (edit->state.getProperty (crossfaderProperty, 0.0f));
        for (auto* t : te::getAudioTracks (*edit))
            if (auto* mixer = dynamic_cast<TrackMixer*> (t->getVolumePlugin()))
            {
                const auto side = static_cast<Session::CrossfadeAssign> (juce::jlimit (0, 2, static_cast<int> (t->state.getProperty (crossfadeProperty, 0))));
                mixer->crossfade.store (Session::crossfadeGain (side, position), std::memory_order_relaxed);
            }
    }
    // Frozen tracks play their rendered audio, so their devices and clips cannot change until unfrozen.
    static Result unfrozen (const te::Track* first, const te::Track* second = nullptr)
    {
        for (const auto* t : { first, second })
            if (t != nullptr && t->isFrozen (te::Track::anyFreeze)) return Result::fail ("Unfreeze \"" + t->getName() + "\" before editing its devices or clips.");
        return Result::ok();
    }
    te::Track* trackOfPlugin (const String& pluginId) const { return device (pluginId).track; }
    te::Track* trackOfClip (const String& clipId) const
    {
        for (auto* t : te::getAudioTracks (*edit)) for (auto* c : t->getClips()) if (c->itemID.toString() == clipId) return t;
        return nullptr;
    }
    // Creates an instrument and puts it where the track's instrument belongs, in one transaction.
    Result replaceInstrument (te::AudioTrack& track, const juce::ValueTree& state, const String& name)
    {
        auto* current = instrument (track);
        auto plugin = edit->getPluginCache().createNewPlugin (state);
        if (! plugin) return Result::fail ("The instrument could not be created.");
        // Keep MIDI effects ahead of the new instrument and audio devices after it.
        int index = 0;
        if (current) index = track.pluginList.indexOf (current);
        else for (auto* p : track.pluginList.getPlugins()) if (auto* native = dynamic_cast<NativeEffectPlugin*> (p); native && effectIsMidi (native->getKind())) ++index;
        transaction (name);
        if (current) { current->windowState->closeWindowExplicitly(); current->deleteFromParent(); }
        track.pluginList.insertPlugin (plugin, index, nullptr);
        changed(); return Result::ok();
    }
    void transaction (const String& name)
    {
        flushStructuralUpdates();
        if (! gesture) edit->getUndoManager().beginNewTransaction (name);
    }
    void refreshRouting()
    {
        // Derived gates are published through native graph updates on the message
        // thread. They never replace user send levels or create undo actions.
        const auto tracks = te::getAudioTracks (*edit);
        bool anySolo = false, anySourceSolo = false;
        for (auto* t : tracks)
        {
            anySolo = anySolo || t->isSolo (false);
            anySourceSolo = anySourceSolo || (! isReturn (*t) && t->isSolo (false));
        }
        for (auto* destination : tracks)
        {
            if (! isReturn (*destination)) continue;
            for (auto* source : tracks)
            {
                if (isReturn (*source)) continue;
                if (auto* send = findSend (*source, destination->itemID.toString()))
                {
                    const bool selected = ! anySolo || source->isSolo (false) || destination->isSolo (false);
                    const bool audible = sendMode (*destination) == SendMode::pre || ! source->isMuted (false);
                    send->state.setProperty (te::IDs::enabled, selected && audible, nullptr);
                }
            }
            // Solo-in-place keeps every return output open, including effect
            // tails left by sources whose feeds are now Off or solo-suppressed.
            destination->state.setProperty (te::IDs::soloIsolate, anySourceSolo, nullptr);
        }
        edit->updateMuteSoloStatuses();
    }
    using ClipActivationStates=std::vector<std::pair<juce::String,bool>>;
    ClipActivationStates clipActivationStates() const
    {
        ClipActivationStates states;
        for(auto* track:te::getAudioTracks(*edit)) for(auto* clip:allTrackClips(*track))
            states.emplace_back(clip->itemID.toString(),!clip->isMuted());
        return states;
    }
    void refreshClipActivation(const ClipActivationStates& before)
    {
        const auto after=clipActivationStates();
        if(before==after) return;
        for(const auto& state:after)
        {
            const auto previous=std::find_if(before.begin(),before.end(),[&](const auto& old){return old.first==state.first;});
            if(previous!=before.end() && previous->second!=state.second)
            {edit->restartPlayback();return;}
        }
    }
    using FollowStates = std::vector<std::pair<juce::String, Session::FollowSettings>>;
    FollowStates followStates() const
    {
        FollowStates states;
        for (auto* track : te::getAudioTracks (*edit))
            for (auto* clip : allTrackClips (*track))
                if (clip->getClipSlot()) states.emplace_back (clip->itemID.toString(), clipFollowSettings (clip->state));
        return states;
    }
    void refreshFollowActions (const FollowStates& before)
    {
        for (const auto& current : followStates())
        {
            const auto previous = std::find_if (before.begin(), before.end(), [&] (const auto& old) { return old.first == current.first; });
            if (previous != before.end() && previous->second != current.second)
            { edit->restartPlayback(); return; }
        }
    }
    Result setMixerAutomation (te::VolumeAndPanPlugin& plugin, AutomationTarget target, const AutomationLane& lane)
    {
        if (exporting || edit->getTransport().isRecording())
            return Result::fail ("Stop recording or finish exporting before editing automation.");
        if (! validAutomationLane (target, lane)) return Result::fail ("Automation requires up to 4096 ordered beat points, volume -100 to +6 dB, or pan -1 to +1.");
        double previousTime = -1;
        for (const auto& point : lane.points)
        {
            const double time = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (point.beat)).inSeconds();
            if (! std::isfinite (time) || time <= previousTime) return Result::fail ("Automation points are too close to represent at the project tempo.");
            previousTime = time;
        }
        auto& parameter = automationParameter (plugin, target); auto& curve = parameter.getCurve();
        const auto nativeValue = [target] (float value) { return target == AutomationTarget::volume ? te::decibelsToVolumeFaderPosition (value) : value; };
        bool same = lane.enabled == ! curve.bypass.get() && lane.points.size() == static_cast<size_t> (curve.getNumPoints());
        for (size_t i = 0; same && i < lane.points.size(); ++i)
            same = std::abs (edit->tempoSequence.toBeats (curve.getPointTime ((int) i)).inBeats() - lane.points[i].beat) < 1e-9
                && std::abs (curve.getPointValue ((int) i) - nativeValue (lane.points[i].value)) < 1e-6f;
        if (same) return Result::ok();
        transaction (plugin.isMasterVolAndPan() ? "Edit master volume automation"
            : target == AutomationTarget::volume ? "Edit volume automation" : "Edit pan automation");
        auto* undo = &edit->getUndoManager();
        const auto& property = manualAutomationProperty (target);
        // This baseline must survive Undo of the first envelope. Native automation
        // asynchronously overwrites its attached display property during playback.
        if (! plugin.state.hasProperty (property)) plugin.state.setProperty (property, parameter.getCurrentExplicitValue(), nullptr);
        plugin.remapOnTempoChange = true;
        // Keep the native curve container attached across Undo, including empty lanes,
        // because AutomatableParameter caches that exact tree for its lifetime.
        if (! curve.state.getParent().isValid()) plugin.state.addChild (curve.state, -1, nullptr);
        curve.clear (undo);
        curve.state.setProperty (te::IDs::bypass, ! lane.enabled, undo);
        for (const auto& point : lane.points)
            curve.addPoint (te::EditPosition (te::BeatPosition::fromBeats (point.beat)), nativeValue (point.value), 0, undo);
        if (lane.points.empty()) parameter.setParameter (static_cast<float> (plugin.state[property]), juce::dontSendNotification);
        // Attaching a curve invalidates sibling parameter streams as well.
        // Native streams omit one-point curves: follow their static values on
        // this message-thread edit boundary, preserving explicit manual values.
        for (auto* current : plugin.getAutomatableParameters())
        {
            current->updateStream();
            current->updateToFollowCurve (edit->getTransport().getPosition());
        }
        edit->dispatchPendingUpdatesSynchronously();
        changed(); return Result::ok();
    }
    void refreshParameterValues()
    {
        configureMidiPlayback();
        const auto refresh = [this] (te::Plugin& plugin)
        {
            if (auto* native = dynamic_cast<NativeEffectPlugin*> (&plugin)) { native->refreshAutomation(); return; }
            auto* mixer = dynamic_cast<te::VolumeAndPanPlugin*> (&plugin);
            for (auto* parameter : plugin.getAutomatableParameters())
            {
                const auto target = mixer && parameter == mixer->volParam.get() ? AutomationTarget::volume : AutomationTarget::pan;
                const auto& property = manualAutomationProperty (target);
                if (mixer && plugin.state.hasProperty (property))
                {
                    if (! parameter->hasAutomationPoints())
                    {
                        plugin.state.setProperty (target == AutomationTarget::volume ? te::IDs::volume : te::IDs::pan, plugin.state[property], nullptr);
                        parameter->updateFromAttachedValue();
                    }
                    parameter->updateStream();
                    parameter->updateToFollowCurve (edit->getTransport().getPosition());
                }
                else parameter->updateFromAttachedValue();
            }
        };
        for (auto* track : te::getAudioTracks (*edit))
            for (auto* plugin : allDevices (*track)) refresh (*plugin);
        for (auto* track : te::getAllTracks (*edit))
            if (auto* folder = dynamic_cast<te::FolderTrack*> (track); folder && folder->isSubmixFolder())
                for (auto* plugin : folder->pluginList.getPlugins()) refresh (*plugin);
        refresh (*edit->getMasterVolumePlugin());
        for (auto* t : te::getAudioTracks (*edit))
            if (auto* device = input (*t))
                device->getInputDevice().setMonitorMode (static_cast<te::InputDevice::MonitorMode> (
                    juce::jlimit (0, 2, static_cast<int> (t->state.getProperty (monitorProperty, 0)))));
        edit->setCountInMode (static_cast<te::Edit::CountIn> (static_cast<int> (edit->state.getProperty(countInProperty,0))));
        refreshRouting();
        refreshCrossfade();
        edit->dispatchPendingUpdatesSynchronously();
    }
    void resetTapTempo() { lastTap=-1; numTapIntervals=nextTapInterval=0; }
    void prepareEdit()
    {
        refreshCrossfade(); // Loaded and new projects apply their saved crossfade gains.
        resetTapTempo();
        edit->editFileRetriever = [this] { return editLocation(); };
        flushStructuralUpdates();
        edit->playInStopEnabled = true;
        edit->getAutomationRecordManager().setReadingAutomation (true);
        edit->getAutomationRecordManager().setWritingAutomation (false);
        edit->setCountInMode (static_cast<te::Edit::CountIn> (static_cast<int> (edit->state.getProperty(countInProperty,0))));
        refreshRouting();
        if (useDevices) edit->getTransport().ensureContextAllocated();
        edit->dispatchPendingUpdatesSynchronously();
        edit->getUndoManager().clearUndoHistory();
        edit->resetChangedStatus();
        explicitChanges = false;
        owner.sendChangeMessage();
    }
    te::InputDeviceInstance* input (te::AudioTrack& t) const
    {
        const bool midi = t.state.getProperty (midiTrackProperty, false);
        for (auto* instance : edit->getAllInputDevices())
            if (instance->getInputDevice().isMidi() == midi && !instance->getInputDevice().isTrackDevice()
                && instance->getTargets().contains (t.itemID)) return instance;
        return nullptr;
    }
    juce::StringArray monitoredMidiTracks() const
    {
        juce::StringArray result;
        for (auto* track : te::getAudioTracks(*edit))
            if (track->state.getProperty(midiTrackProperty,false))
                if (auto* device=input(*track); device && device->isLivePlayEnabled(*track))
                    result.add(track->itemID.toString());
        return result;
    }
    void finishInputChange(const juce::StringArray& previouslyMonitored)
    {
        juce::Array<te::AudioTrack*> stopped;
        for (const auto& id : previouslyMonitored)
            if (auto* target=track(id))
                if (auto* device=input(*target); !device || !device->isLivePlayEnabled(*target))
                    stopped.add(target);
        // Publish activation as well as deactivation before returning to the UI.
        // Otherwise the first key can arrive before the replacement input node
        // has registered its MIDI consumer and is silently dropped.
        for (auto* device : edit->getAllInputDevices()) device->flushRecordingStateUpdates();
        edit->dispatchPendingUpdatesSynchronously();
        // Release only tracks that actually lost monitoring. No processor is
        // reset from the message thread while audio can use it.
        if (stopped.isEmpty()) return;
        for (auto* target : stopped)
            for (int channel=1;channel<=16;++channel)
            {
                target->injectLiveMidiMessage(juce::MidiMessage::controllerEvent(channel,64,0),{});
                target->injectLiveMidiMessage(juce::MidiMessage::allNotesOff(channel),{});
            }
    }
    void refreshInputs()
    {
        if (! useDevices) return;
        edit->getTransport().ensureContextAllocated();
    }
    // Enabling a device rebuilds every context's device list. Tracktion stops the play head
    // while clearing the old graph but leaves the transport playing, and its timer then stops
    // the transport and launched clips. Rebuild the graph and resume where the head stopped.
    void resumePlayback (bool wasPlaying)
    {
        if (! wasPlaying || ! edit->getTransport().isPlaying()) return;
        edit->dispatchPendingUpdatesSynchronously();
        if (auto* context = edit->getCurrentPlaybackContext(); context && ! context->isPlaying() && ! context->isPlayPending())
            context->play();
    }
    Meter& meter (const String& id, te::LevelMeasurer& m) const
    {
        auto& result = meters[id];
        if (! result || result->measurer.get() != &m) result = std::make_unique<Meter> (m);
        return *result;
    }
    Session& owner;
    te::Engine engine;
    bool useDevices;
    uint64_t projectGeneration = 0;
    bool explicitChanges = false, pristineNewDocument = false, exporting = false;
    double lastTap=-1;
    std::array<double,4> tapIntervals {};
    int numTapIntervals=0, nextTapInterval=0;
    File scratch, projectFile;
    juce::ValueTree audioClipboard, midiClipboard;
    double midiClipboardBeats = 0;
    std::unique_ptr<juce::TemporaryFile> audioClipboardMedia;
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<te::Edit::UndoTransactionInhibitor> gesture;
    mutable std::map<String, std::unique_ptr<Meter>> meters;
};

Session::Session (bool enableAudioDevices, std::unique_ptr<juce::AudioIODeviceType> customDeviceType)
    : impl (std::make_unique<Impl> (*this, enableAudioDevices, std::move (customDeviceType))) { newProject(); }
Session::~Session() { closePluginEditors(); }

Session::Snapshot Session::snapshot() const
{
    Snapshot s;
    auto& e = *impl->edit;
    auto& transport = e.getTransport();
    s.projectFile = impl->projectFile;
    s.projectGeneration = impl->projectGeneration;
    s.name = s.projectFile == File() ? "Untitled" : s.projectFile.getFileNameWithoutExtension();
    s.dirty = impl->explicitChanges || (! impl->pristineNewDocument && e.hasChangedSinceSaved());
    s.crossfader = static_cast<float> (e.state.getProperty (crossfaderProperty, 0.0f));
    s.playing = transport.isPlaying(); s.recording = transport.isRecording();
    s.looping = transport.looping;
    s.position = transport.getPosition().inSeconds();
    s.duration = e.getLength().inSeconds();
    s.tempo = e.tempoSequence.getTempo (0)->getBpm();
    s.masterGainDb=e.getMasterVolumePlugin()->getVolumeDb();
    s.masterVolumeAutomation = automationLane (*e.getMasterVolumePlugin(), AutomationTarget::volume);
    s.metronome = {e.clickTrackEnabled.get(),e.clickTrackEmphasiseBars.get(),e.clickTrackRecordingOnly.get(),
        e.getClickTrackVolume(),static_cast<int>(e.state.getProperty(countInProperty,0))};
    s.recordQuantisation=static_cast<RecordQuantisation>(static_cast<int>(e.state.getProperty(recordQuantisationProperty,0)));
    const auto clickRange=e.getClickTrackRange();
    if(s.recording && s.metronome.countInBars>0 && !clickRange.isEmpty())
        s.countInBeatsRemaining=juce::jlimit(0.0,s.metronome.countInBars*4.0,
            (e.tempoSequence.toBeats(clickRange.getEnd()).inBeats()+.5)-e.tempoSequence.toBeats(transport.getPosition()).inBeats());
    s.loopStart = transport.getLoopRange().getStart().inSeconds();
    s.loopEnd = transport.getLoopRange().getEnd().inSeconds();
    s.cpu = impl->engine.getDeviceManager().getCpuUsage();
    for (auto* marker : e.getMarkerManager().getMarkers())
        s.locators.push_back ({ marker->itemID.toString(), marker->getName(), marker->getPosition().getStart().inSeconds() });
    for (auto* scene : e.getSceneList().getScenes())
        s.scenes.push_back ({ scene->state.getProperty ("halftrackSceneID").toString(), scene->name.get() });
    const auto quantisation = e.getLaunchQuantisation().type.get();
    s.launchQuantisation = quantisation == te::LaunchQType::none ? LaunchQuantisation::none
        : quantisation == te::LaunchQType::quarter ? LaunchQuantisation::beat : LaunchQuantisation::bar;
    int index = 0;
    for (auto* t : te::getAudioTracks (e))
    {
        TrackInfo info;
        info.id = t->itemID.toString(); info.name = t->getName(); info.colour = t->getColour();
        info.mute = t->isMuted (false); info.solo = t->isSolo (false);
        info.isMidi = t->state.getProperty (midiTrackProperty, false);
        info.isReturn = isReturn (*t);
        if (info.isReturn) info.sendMode = sendMode (*t);
        else for (auto* destination : te::getAudioTracks (e))
            if (isReturn (*destination))
            {
                const auto id = destination->itemID.toString();
                auto* send = findSend (*t, id);
                info.sends.push_back ({ id, send ? send->getGainDb() : -100.0f });
            }
        if (auto* parent = t->getParentFolderTrack()) info.groupId = parent->itemID.toString();
        if (auto* synth = Impl::synth (*t)) info.synth = synth->getSettings();
        if (auto* native = Impl::nativeInstrument (*t))
        {
            info.instrument = { native->itemID.toString(), native->getName(), native->isEnabled() };
            info.instrument.isNativeEffect = true; info.instrument.effect = native->readSettings(); info.instrument.automation = deviceAutomation (*native);
            if (native->getKind() == EffectKind::sampler) info.instrument.sample = native->sampleFile();
            if (native->getKind() == EffectKind::drumSampler)
                for (int pad = 0; pad < drumPadCount; ++pad) info.instrument.padSamples.push_back (native->sampleFile (pad));
        }
        if (auto* v = t->getVolumePlugin())
        {
            info.gainDb = v->getVolumeDb(); info.pan = v->getPan();
            info.volumeAutomation = automationLane (*v, AutomationTarget::volume);
            info.panAutomation = automationLane (*v, AutomationTarget::pan);
        }
        if (auto* input = impl->input (*t))
        {
            info.inputName = input->getInputDevice().getName();
            info.armed = input->isRecordingEnabled (t->itemID);
            info.monitor = static_cast<MonitorMode> (input->getInputDevice().getMonitorMode());
        }
        if (auto* level = t->getLevelMeterPlugin())
        {
            auto& m = impl->meter (info.id, level->measurer);
            info.peakL = m.read (0); info.peakR = m.read (1);
        }
        for (auto* c : t->getClips()) info.clips.push_back (describeClip (*c));
        info.launcherActive = t->playSlotClips.get() || (t->launcherOwnership.load (std::memory_order_acquire) & 1) != 0;
        info.frozen = t->isFrozen (te::Track::individualFreeze);
        info.crossfade = static_cast<CrossfadeAssign> (juce::jlimit (0, 2, static_cast<int> (t->state.getProperty (crossfadeProperty, 0))));
        if (auto* delay = t->pluginList.findFirstPluginOfType<TrackDelayPlugin>()) info.delayMs = delay->milliseconds.get();
        for (auto* slot : t->getClipSlotList().getClipSlots())
        {
            SlotInfo slotInfo;
            slotInfo.id = slot->itemID.toString();
            slotInfo.recording = impl->slotRecording (slot->itemID) != nullptr;
            if (auto* c = slot->getClip())
            {
                slotInfo.hasClip = true; slotInfo.clip = describeClip (*c);
                if (auto h = c->getLaunchHandle())
                {
                    slotInfo.playing = s.playing && h->getPlayingStatus() == te::LaunchHandle::PlayState::playing;
                    slotInfo.queued = h->getQueuedStatus() == te::LaunchHandle::QueueState::playQueued;
                    slotInfo.stopQueued = h->getQueuedStatus() == te::LaunchHandle::QueueState::stopQueued;
                    if (slotInfo.playing)
                        if (auto played = h->getPlayedRange())
                        {
                            const auto loop = c->getLoopRangeBeats();
                            slotInfo.playPositionBeats = played->getLength().inBeats();
                            if (loop.getLength().inBeats() > 0)
                                slotInfo.playPositionBeats = loop.getStart().inBeats()
                                    + std::fmod (slotInfo.playPositionBeats, loop.getLength().inBeats());
                        }
                }
            }
            info.slots.push_back (std::move (slotInfo));
        }
        for (auto* p : impl->plugins (index++)) info.plugins.push_back (Impl::describeDevice (*p));
        s.tracks.push_back (std::move (info));
    }
    for (auto* track : te::getAllTracks (e))
        if (auto* folder = dynamic_cast<te::FolderTrack*> (track); folder != nullptr && folder->isSubmixFolder())
        {
            GroupInfo group;
            group.id = folder->itemID.toString(); group.name = folder->getName(); group.colour = folder->getColour();
            group.mute = folder->isMuted (false); group.solo = folder->isSolo (false);
            group.folded = folder->state.getProperty (groupFoldedProperty, false);
            if (auto* v = folder->getVolumePlugin()) { group.gainDb = v->getVolumeDb(); group.pan = v->getPan(); }
            if (auto* level = folder->pluginList.findFirstPluginOfType<te::LevelMeterPlugin>())
            {
                auto& m = impl->meter (group.id, level->measurer);
                group.peakL = m.read (0); group.peakR = m.read (1);
            }
            for (auto* member : te::getAudioTracks (e)) if (member->getParentFolderTrack() == folder) group.memberIds.add (member->itemID.toString());
            s.groups.push_back (std::move (group));
        }
    if (auto* output = impl->engine.getDeviceManager().getDefaultWaveOutDevice())
    {
        auto& m = impl->meter ("master", output->levelMeasurer);
        s.masterPeakL = m.read (0); s.masterPeakR = m.read (1);
    }
    return s;
}

Result Session::newProject()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    endEditGesture();
    closePluginEditors();
    if (impl->edit) impl->edit->getTransport().stop (false, false);
    impl->meters.clear(); impl->heldSlots.clear();
    impl->projectFile = File{};
    auto state = te::createEmptyEdit (impl->engine);
    auto options = te::Edit::Options { impl->engine, state, te::ProjectItemID::fromProperty (state, te::IDs::projectID) };
    options.numAudioTracks = 0; options.defaultMasterVolumedB = 0;
    options.editFileRetriever = [this] { return impl->editLocation(); };
    impl->edit = std::make_unique<te::Edit> (std::move (options));
    ++impl->projectGeneration;
    impl->edit->getMasterVolumePlugin()->setVolumeDb (0);
    impl->edit->tempoSequence.getTempo (0)->setBpm (120);
    impl->edit->clickTrackGain = .6f; impl->edit->clickTrackEmphasiseBars = true;
    impl->edit->state.setProperty(countInProperty,0,nullptr);
    impl->edit->state.setProperty(recordQuantisationProperty,0,nullptr);
    impl->edit->getTransport().setLoopRange ({ te::TimePosition::fromSeconds (0), te::TimePosition::fromSeconds (8) });
    for (auto* t : te::getAudioTracks (*impl->edit)) impl->edit->deleteTrack (t);
    addAudioTrack ("Audio 1"); addAudioTrack ("Audio 2");
    impl->edit->getSceneList().ensureNumberOfScenes (8);
    for (auto* scene : impl->edit->getSceneList().getScenes())
    {
        scene->state.setProperty ("halftrackSceneID", juce::Uuid().toString(), nullptr);
        scene->name = "Scene " + String (scene->getIndex() + 1);
    }
    impl->prepareEdit();
    impl->pristineNewDocument = true;
    return Result::ok();
}

Result Session::loadProject (const File& file)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto xml = juce::XmlDocument::parse (file);
    if (! xml || ! xml->hasTagName ("HALFTRACK") || xml->getIntAttribute ("version") != 1)
        return Result::fail ("This is not a supported Halftrack project.");
    auto* editXml = xml->getChildByName ("EDIT");
    if (! editXml) return Result::fail ("The project has no session data.");
    auto state = juce::ValueTree::fromXml (*editXml);
    // Reject malformed launcher ownership before native constructors can build
    // hidden clips or assert on a slot containing more than one clip.
    int savedSceneCount = 0;
    for (const auto& child : state.getChildWithName (te::IDs::SCENES))
        if (child.hasType (te::IDs::SCENE)) ++savedSceneCount;
    if (savedSceneCount > 256) return Result::fail ("This project exceeds the supported scene count.");
    const auto validateSlots = [&] (auto&& self, const juce::ValueTree& node) -> Result
    {
        if (node.hasType (te::IDs::CLIPSLOTS))
        {
            int rowCount = 0;
            for (const auto& slot : node)
                if (slot.hasType (te::IDs::CLIPSLOT))
                {
                    ++rowCount;
                    int clipCount = 0;
                    for (const auto& clip : slot)
                        if (te::Clip::isClipState (clip) && ++clipCount > 1)
                            return Result::fail ("A session slot cannot contain more than one clip.");
                }
            if (rowCount > savedSceneCount)
                return Result::fail ("This project has clip slots without matching scenes.");
        }
        for (const auto& child : node)
            if (auto result = self (self, child); result.failed()) return result;
        return Result::ok();
    };
    if (auto result = validateSlots (validateSlots, state); result.failed()) return result;
    if (auto result = validateProjectTempo (state); result.failed()) return result;
    if (auto result = validateMetronome (state); result.failed()) return result;
    if (auto result = validateRecordQuantisation (state); result.failed()) return result;
    if (auto result = validateCrossfade (state); result.failed()) return result;
    if (auto result = validateMasterVolume (state); result.failed()) return result;
    if (auto result = validateMixerAutomation (state); result.failed()) return result;
    if (auto result = validateRouting (state); result.failed()) return result;
    if (auto result = UtilityPlugin::validateState (state); result.failed()) return result;
    if (auto result = NativeEffectPlugin::validateState (state); result.failed()) return result;
    if (auto result = NativeEffectPlugin::resolveSamples (state, file.getParentDirectory()); result.failed()) return result;
    if (auto result = validateAudioClips (state); result.failed()) return result;
    if (auto result = validateFollowActions (state); result.failed()) return result;
    if (auto result = validateLocators (state); result.failed()) return result;
    if (auto result = validateRacks (state); result.failed()) return result;
    if (auto result = validateTrackDelays (state); result.failed()) return result;
    restoreManualMixerState (state);
    auto id = te::ProjectItemID::fromProperty (state, te::IDs::projectID);
    if (! id.isValid()) id = te::ProjectItemID::createNewID (te::ProjectID{});
    auto options = te::Edit::Options { impl->engine, state, id };
    options.numAudioTracks = 0;
    options.editFileRetriever = [file] { return file; };
    auto candidate = te::Edit::createEdit (std::move (options));
    if (! candidate) return Result::fail ("This project could not be loaded.");
    for (auto* track : te::getAudioTracks (*candidate))
        if (auto* mixer = track->getVolumePlugin())
            for (const auto target : { AutomationTarget::volume, AutomationTarget::pan })
                if (! validAutomationLane (target, automationLane (*mixer, target)))
                    return Result::fail ("Mixer automation exceeds the supported beat or value range.");
    for (const auto target : { AutomationTarget::volume, AutomationTarget::pan })
        if (! validAutomationLane (target, automationLane (*candidate->getMasterVolumePlugin(), target)))
            return Result::fail ("Master automation exceeds the supported beat or value range.");
    // Rack connections follow from the chain order, so loading rebuilds them outside history.
    for (auto* type : candidate->getRackList().getTypes()) Impl::rewire (*type);
    candidate->getUndoManager().clearUndoHistory();
    for (auto* t : te::getAudioTracks (*candidate))
        for (auto* p : Impl::allDevices (*t))
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (p))
                if (! external->isInitialisingAsync() && external->getAudioPluginInstance() == nullptr)
                    return Result::fail ("Could not load plug-in " + external->getName()
                        + ". Open Scan / Manage Plug-ins and scan its installed format, then reopen this project. "
                        + external->getLoadError());
    for (auto* t : te::getAudioTracks (*candidate))
        for (auto* c : allTrackClips (*t))
            if (c->usesSourceFile())
            {
                auto& reference = c->getSourceFileReference();
                const auto resolved = reference.getFile();
                if (! resolved.existsAsFile()) return Result::fail ("Missing audio file: " + resolved.getFullPathName());
                // In-memory undo can retain detached clip trees across Save As.
                // Anchor their media before edits; only the saved copy is relative.
                reference.source.setValue (resolved.getFullPathName(), nullptr);
            }
    endEditGesture(); closePluginEditors(); stop(); impl->meters.clear();
    impl->projectFile = file;
    impl->pristineNewDocument = false;
    impl->edit = std::move (candidate);
    ++impl->projectGeneration;
    for (auto* t : te::getAudioTracks (*impl->edit))
    {
        t->getClipSlotList().ensureNumberOfSlots (impl->edit->getSceneList().getNumScenes());
        t->playSlotClips = false;
    }
    for (auto* scene : impl->edit->getSceneList().getScenes())
        if (! scene->state.hasProperty ("halftrackSceneID"))
            scene->state.setProperty ("halftrackSceneID", juce::Uuid().toString(), nullptr);
    impl->prepareEdit();
    impl->refreshInputs();
    for (auto* t : te::getAudioTracks (*impl->edit))
        if (auto* input = impl->input (*t))
        {
            input->setRecordingEnabled (t->itemID, false);
            input->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::off);
            t->state.setProperty (monitorProperty, static_cast<int> (MonitorMode::off), nullptr);
        }
    impl->edit->resetChangedStatus();
    return Result::ok();
}

Result Session::saveProject (const File& destination)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before saving.");
    auto file = destination.withFileExtension (".halftrack");
    auto assets = file.getSiblingFile (file.getFileNameWithoutExtension() + " Assets");
    auto directoryResult = assets.createDirectory();
    if (directoryResult.failed()) return directoryResult;
    impl->edit->flushState();
    auto state = impl->edit->state.createCopy();
    std::map<String, File> copied;
    std::vector<std::pair<te::Clip*, File>> sources;
    for (auto* t : te::getAudioTracks (*impl->edit))
        for (auto* c : allTrackClips (*t))
        {
            if (! c->usesSourceFile()) continue;
            auto original = c->getSourceFileReference().getFile();
            if (! original.existsAsFile()) return Result::fail ("Missing audio file: " + original.getFullPathName());
            File target;
            if (original.isAChildOf (assets)) target = original;
            else if (auto it = copied.find (original.getFullPathName()); it != copied.end()) target = it->second;
            else
            {
                target = assets.getNonexistentChildFile (original.getFileNameWithoutExtension(), original.getFileExtension(), true);
                if (! original.copyFileTo (target)) return Result::fail ("Could not collect audio: " + original.getFileName());
                copied[original.getFullPathName()] = target;
            }
            auto node = findStateWithID (state, c->state.getProperty (te::IDs::id));
            node.setProperty (te::IDs::source, target.getRelativePathFrom (file.getParentDirectory()), nullptr);
            sources.emplace_back (c, target);
        }
    // Sampler and drum pad samples are collected the same way and saved relative to the project.
    struct CollectedSample { NativeEffectPlugin* native; int slot; File target; };
    std::vector<CollectedSample> samples;
    for (auto* t : te::getAudioTracks (*impl->edit))
        if (auto* native = Impl::nativeInstrument (*t))
            for (int slot = 0; slot < sampleSlotCount (native->getKind()); ++slot)
        {
            const auto original = native->sampleFile (slot);
            if (original == File()) continue;
            if (! original.existsAsFile()) return Result::fail ("Missing sample: " + original.getFullPathName());
            File target;
            if (original.isAChildOf (assets)) target = original;
            else if (auto it = copied.find (original.getFullPathName()); it != copied.end()) target = it->second;
            else
            {
                target = assets.getNonexistentChildFile (original.getFileNameWithoutExtension(), original.getFileExtension(), true);
                if (! original.copyFileTo (target)) return Result::fail ("Could not collect sample: " + original.getFileName());
                copied[original.getFullPathName()] = target;
            }
            auto node = findStateWithID (state, native->state.getProperty (te::IDs::id));
            node.setProperty (sampleSlotProperty (native->getKind(), slot), target.getRelativePathFrom (file.getParentDirectory()), nullptr);
            samples.push_back ({ native, slot, target });
        }
    juce::XmlElement document ("HALFTRACK");
    document.setAttribute ("version", 1);
    document.addChildElement (state.createXml().release());
    juce::TemporaryFile temp (file);
    if (! document.writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
        return Result::fail ("Could not write the project file.");
    impl->projectFile = file;
    impl->edit->editFileRetriever = [this] { return impl->editLocation(); };
    for (auto& [clip, source] : sources)
        // Keep running and undo-retained sources independent of the project
        // directory. The written copy above remains relative and portable.
        // Native listeners refresh playback, but relocation adds no undo action.
        clip->getSourceFileReference().source.setValue (source.getFullPathName(), nullptr);
    for (auto& collected : samples) collected.native->relocateSample (collected.slot, collected.target);
    impl->edit->resetChangedStatus();
    impl->explicitChanges = false;
    sendChangeMessage();
    return Result::ok();
}

Result Session::addAudioTrack (String name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->transaction ("Add audio track");
    const auto count = te::getAudioTracks (*impl->edit).size();
    impl->edit->ensureNumberOfAudioTracks (count + 1);
    auto* t = impl->track (count);
    if (! t) return Result::fail ("Could not create an audio track.");
    t->setName (name.isEmpty() ? "Audio " + String (count + 1) : name);
    const juce::uint32 colours[] = { 0xffd0a35e, 0xff86a395, 0xff899bbb, 0xffb98579, 0xffaaa179, 0xffa58aaa };
    t->setColour (juce::Colour (colours[count % 6]));
    if (auto result = useTrackMixer (*t); result.failed()) return result;
    t->getVolumePlugin()->setVolumeDb (0);
    t->getClipSlotList().ensureNumberOfSlots (impl->edit->getSceneList().getNumScenes());
    impl->changed();
    return Result::ok();
}
Result Session::addMidiTrack (String name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->transaction ("Add MIDI track");
    const auto count = te::getAudioTracks (*impl->edit).size();
    impl->edit->ensureNumberOfAudioTracks (count + 1);
    auto* track = impl->track (count);
    if (! track) return Result::fail ("Could not create a MIDI track.");
    track->setName (name.isEmpty() ? "MIDI " + String (count + 1) : name);
    track->setColour (juce::Colour (0xff9d91bc));
    track->state.setProperty (midiTrackProperty, true, &impl->edit->getUndoManager());
    if (auto result = useTrackMixer (*track); result.failed()) return result;
    track->getVolumePlugin()->setVolumeDb (0);
    auto synth = impl->edit->getPluginCache().createNewPlugin (SubtractiveSynthPlugin::xmlTypeName, {});
    if (! synth) return Result::fail ("Could not create the built-in synthesizer.");
    track->pluginList.insertPlugin (synth, 0, nullptr);
    track->getClipSlotList().ensureNumberOfSlots (impl->edit->getSceneList().getNumScenes());
    impl->changed(); return Result::ok();
}
Result Session::addReturnTrack (String name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->transaction ("Add return track");
    auto input = impl->edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
    auto* aux = dynamic_cast<te::AuxReturnPlugin*> (input.get());
    if (! aux) return Result::fail ("Could not create the return input.");
    std::set<int> used;
    for (auto* t : te::getAudioTracks (*impl->edit)) if (isReturn (*t)) used.insert (static_cast<int> (t->state[returnBusProperty]));
    int bus = 0;
    while (used.contains (bus)) ++bus;
    const auto count = te::getAudioTracks (*impl->edit).size();
    impl->edit->ensureNumberOfAudioTracks (count + 1);
    auto* track = impl->track (count);
    if (! track) return Result::fail ("Could not create the return track.");
    auto* undo = &impl->edit->getUndoManager();
    track->state.setProperty (returnProperty, true, undo);
    track->state.setProperty (returnBusProperty, bus, undo);
    track->state.setProperty (returnModeProperty, static_cast<int> (SendMode::post), undo);
    track->setName (name.trim().isEmpty() ? "Return " + String (static_cast<int> (used.size()) + 1) : name.trim());
    track->setColour (juce::Colour (0xffb9a283));
    if (auto result = useTrackMixer (*track); result.failed()) return result;
    track->getVolumePlugin()->setVolumeDb (0);
    aux->busNumber = bus;
    track->pluginList.insertPlugin (input, 0, nullptr);
    track->getClipSlotList().ensureNumberOfSlots (impl->edit->getSceneList().getNumScenes());
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::setTrackSendGain (int index, const String& destinationID, float decibels)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* source = impl->track (index); auto* destination = impl->track (destinationID);
    if (! source || isReturn (*source) || ! destination || ! isReturn (*destination))
        return Result::fail ("Sends must connect an audio or MIDI track to a return track.");
    if (! std::isfinite (decibels) || decibels < -100.0f || decibels > 0)
        return Result::fail ("Send gain must be between Off (-100 dB) and 0 dB.");
    auto* send = findSend (*source, destinationID);
    if (! send && decibels == -100.0f) return Result::ok();
    impl->transaction ("Send level");
    if (! send)
    {
        auto plugin = impl->edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
        send = dynamic_cast<te::AuxSendPlugin*> (plugin.get());
        if (! send) return Result::fail ("Could not create the send.");
        send->state.setProperty (sendReturnProperty, destinationID, &impl->edit->getUndoManager());
        send->busNumber = static_cast<int> (destination->state[returnBusProperty]);
        send->setGainDb (-100.0f);
        const auto mixer = source->pluginList.indexOf (source->getVolumePlugin());
        source->pluginList.insertPlugin (plugin, mixer + (sendMode (*destination) == SendMode::post ? 1 : 0), nullptr);
    }
    send->setGainDb (decibels);
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::setReturnSendMode (const String& destinationID, SendMode mode)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* destination = impl->track (destinationID);
    if (! destination || ! isReturn (*destination)) return Result::fail ("Return track not found.");
    if (mode != SendMode::pre && mode != SendMode::post) return Result::fail ("Unknown send mode.");
    if (mode == sendMode (*destination)) return Result::ok();
    impl->transaction ("Return send mode");
    destination->state.setProperty (returnModeProperty, static_cast<int> (mode), &impl->edit->getUndoManager());
    for (auto* source : te::getAudioTracks (*impl->edit))
        if (auto* send = findSend (*source, destinationID))
        {
            te::Plugin::Ptr keepAlive (send);
            send->deleteFromParent();
            const auto mixer = source->pluginList.indexOf (source->getVolumePlugin());
            source->pluginList.insertPlugin (keepAlive, mixer + (mode == SendMode::post ? 1 : 0), nullptr);
        }
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::importMidiFile (const File& file, double startBeat, std::vector<String>& importedIds)
{
    if (impl->exporting || impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording or finish exporting before importing MIDI.");
    if (! std::isfinite (startBeat) || startBeat < 0 || startBeat >= 1000000) return Result::fail ("Choose a valid MIDI insertion position.");
    if (! file.existsAsFile() || file.getSize() > 16*1024*1024) return Result::fail ("Choose a readable MIDI file no larger than 16 MiB.");
    juce::FileInputStream input (file); juce::MidiFile midi; int type = -1;
    if (! input.openedOk() || ! midi.readFrom (input,false,&type) || (type != 0 && type != 1) || midi.getTimeFormat() <= 0)
        return Result::fail ("Import requires a type 0 or type 1 MIDI file with ticks-per-quarter timing.");
    struct Part { String name; double length; std::unique_ptr<te::MidiList> list; };
    std::vector<Part> parts; int eventCount = 0;
    const auto ppq = (double)midi.getTimeFormat();
    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        auto sequence = *midi.getTrack (trackIndex);
        eventCount += sequence.getNumEvents();
        if (eventCount > 100000) return Result::fail ("This MIDI file exceeds 100,000 events.");
        std::array<std::array<int,128>,16> active;
        for (auto& channel : active) channel.fill (-1);
        std::set<int> channels; bool sysex = false;
        String name; double length = 1.0/960;
        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            auto& message = sequence.getEventPointer (i)->message;
            const auto beat = message.getTimeStamp()/ppq;
            if (! std::isfinite (beat) || beat < 0 || beat > 1000000-startBeat)
                return Result::fail ("A MIDI event is outside the supported timeline.");
            message.setTimeStamp (beat); length = std::max (length,beat);
            if (message.isTrackNameEvent()) name = message.getTextFromTextMetaEvent().trim().substring (0,128);
            if (message.isMetaEvent()) continue;
            if (message.isSysEx()) { sysex = true; continue; }
            const auto channel = message.getChannel();
            if (channel < 1 || channel > 16 || ! (message.isNoteOnOrOff() || message.isController() || message.isProgramChange()
                || message.isPitchWheel() || message.isAftertouch() || message.isChannelPressure()))
                return Result::fail ("The MIDI file contains an unsupported system event.");
            channels.insert (channel);
            if (message.isNoteOnOrOff())
            {
                auto& held = active[(size_t)channel-1][(size_t)message.getNoteNumber()];
                if (message.isNoteOn())
                {
                    if (held >= 0) return Result::fail ("Overlapping notes of the same pitch and channel must be resolved before import.");
                    held = i;
                }
                else
                {
                    if (held < 0 || beat <= sequence.getEventPointer (held)->message.getTimeStamp())
                        return Result::fail ("Every imported note needs one later matching note-off.");
                    held = -1;
                }
            }
        }
        for (const auto& channel : active) for (int held : channel)
            if (held >= 0) return Result::fail ("The MIDI file has a note without a matching note-off.");
        if (channels.empty() && sysex) channels.insert (1);
        if (parts.size()+channels.size() > 64) return Result::fail ("The MIDI file would create more than 64 tracks.");
        if (name.isEmpty()) name = file.getFileNameWithoutExtension().substring (0,96)+" / "+String(trackIndex+1);
        if (impl->edit->tempoSequence.toTime (te::BeatPosition::fromBeats(startBeat+length)).inSeconds() > te::Edit::maximumLength)
            return Result::fail ("The imported MIDI clips would exceed the 48-hour timeline.");
        sequence.updateMatchedPairs();
        for (int channel : channels)
        {
            auto list = std::make_unique<te::MidiList>(); list->setMidiChannel (te::MidiChannel(channel));
            juce::MidiMessageSequence filtered;
            for (int i = 0; i < sequence.getNumEvents(); ++i)
            {
                const auto& message = sequence.getEventPointer(i)->message;
                if (message.isForChannel(channel) || (message.isSysEx() && channel == *channels.begin())) filtered.addEvent(message);
            }
            filtered.updateMatchedPairs(); list->importMidiSequence(filtered,nullptr,{},nullptr);
            std::map<std::pair<double,int>,int> releases;
            for (int i=0;i<filtered.getNumEvents();++i)
                if (auto* event=filtered.getEventPointer(i);event->message.isNoteOn() && event->noteOffObject)
                    releases[{event->message.getTimeStamp(),event->message.getNoteNumber()}]=event->noteOffObject->message.getVelocity();
            for (auto* note : list->getNotes())
            {
                note->setNoteOffVelocity(releases.at({note->getStartBeat().inBeats(),note->getNoteNumber()}),nullptr);
                note->state.setProperty(midiNoteIDProperty,juce::Uuid().toString(),nullptr);
            }
            parts.push_back ({name+" / Ch "+String(channel),length,std::move(list)});
        }
    }
    if (parts.empty()) return Result::fail ("The file contains no supported MIDI performance events.");
    beginEditGesture ("Import MIDI file"); std::vector<String> created;
    for (const auto& part : parts)
    {
        const auto count = te::getAudioTracks(*impl->edit).size();
        auto result = addMidiTrack(part.name);
        if (result.failed()) { endEditGesture(); undo(); return result; }
        auto* track = impl->track(count);
        result = addMidiClip(track->itemID.toString(),startBeat,part.length);
        if (result.failed()) { endEditGesture(); undo(); return result; }
        auto* clip = dynamic_cast<te::MidiClip*>(track->getClips().getLast());
        clip->setName(part.name);clip->getSequence().copyFrom(*part.list,&impl->edit->getUndoManager());
        created.push_back(clip->itemID.toString());
    }
    endEditGesture();impl->changed();importedIds=std::move(created);return Result::ok();
}
Result Session::exportMidiClip (const String& id, const File& destination)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip=impl->midiClip(id);if(!clip)return Result::fail("Select a MIDI clip to export.");
    if(destination.isDirectory())return Result::fail("Choose a MIDI file destination, not a directory.");
    juce::MidiMessageSequence sequence;const int channel=clip->getMidiChannel().getChannelNumber();
    if(channel<1||channel>16)return Result::fail("The clip has an invalid MIDI channel.");
    const auto add=[&](juce::MidiMessage message,double beat)
    {message.setTimeStamp(std::round(beat*9600));sequence.addEvent(message);};
    add(juce::MidiMessage::textMetaEvent(3,clip->getName()),0);
    add(juce::MidiMessage::tempoMetaEvent((int)std::round(60000000.0/snapshot().tempo)),0);
    double end=clip->getLengthInBeats().inBeats();
    for(auto* event:clip->getSequence().getSysexEvents())
    {
        const auto beat=event->getBeatPosition().inBeats();
        if(!std::isfinite(beat)||beat<0||beat>1000000)return Result::fail("The clip contains an invalid SysEx time.");
        add(event->getMessage(),beat);end=std::max(end,beat);
    }
    // At shared ticks, initialize SysEx/controllers before triggering notes.
    for(auto* event:clip->getSequence().getControllerEvents())
    {
        const auto type=event->getType(),value=event->getControllerValue(),data=event->getMetadata();
        const auto beat=event->getBeatPosition().inBeats();
        if(!std::isfinite(beat)||beat<0||beat>1000000||value<0||value>16383
            ||(type==te::MidiControllerEvent::aftertouchType&&(data<0||data>127)))
            return Result::fail("The clip contains invalid MIDI controller data.");
        if(type>=0&&type<128)add(juce::MidiMessage::controllerEvent(channel,type,value>>7),beat);
        else if(type==te::MidiControllerEvent::programChangeType)add(juce::MidiMessage::programChange(channel,value>>7),beat);
        else if(type==te::MidiControllerEvent::pitchWheelType)add(juce::MidiMessage::pitchWheel(channel,value),beat);
        else if(type==te::MidiControllerEvent::aftertouchType)add(juce::MidiMessage::aftertouchChange(channel,data,value>>7),beat);
        else if(type==te::MidiControllerEvent::channelPressureType)add(juce::MidiMessage::channelPressureChange(channel,value>>7),beat);
        else return Result::fail("The clip contains an unsupported MIDI controller event.");
        end=std::max(end,beat);
    }
    for(auto* note:clip->getSequence().getNotes())
    {
        if(note->isMute())continue;
        const auto start=note->getStartBeat().inBeats(),finish=start+note->getLengthBeats().inBeats();
        if(!std::isfinite(start)||!std::isfinite(finish)||start<0||finish<=start||finish>1000000
            ||note->getNoteNumber()<0||note->getNoteNumber()>127||note->getVelocity()<1||note->getVelocity()>127
            ||note->getNoteOffVelocity()<0||note->getNoteOffVelocity()>127)
            return Result::fail("The clip contains an invalid MIDI note range.");
        add(juce::MidiMessage::noteOn(channel,note->getNoteNumber(),(juce::uint8)note->getVelocity()),start);
        add(juce::MidiMessage::noteOff(channel,note->getNoteNumber(),(juce::uint8)note->getNoteOffVelocity()),std::max(finish,start+1.0/9600));
        end=std::max(end,std::max(finish,start+1.0/9600));
    }
    if(!std::isfinite(end)||end<0||end*9600>268435455)return Result::fail("The clip exceeds the supported MIDI export duration at 9600 ticks per quarter.");
    add(juce::MidiMessage::endOfTrack(),end);
    juce::MidiFile output;output.setTicksPerQuarterNote(9600);output.addTrack(sequence);
    auto result=destination.getParentDirectory().createDirectory();if(result.failed())return result;
    juce::TemporaryFile temporary(destination);
    {auto stream=temporary.getFile().createOutputStream();if(!stream||!output.writeTo(*stream,0))return Result::fail("Could not write the MIDI file.");
     stream->flush();if(stream->getStatus().failed())return stream->getStatus();}
    if(!temporary.overwriteTargetFileWithTemporary())return Result::fail("Could not replace the MIDI destination.");
    return Result::ok();
}
Result Session::addMidiClip (const String& trackId, double start, double length)
{
    if (auto frozen = impl->unfrozen (impl->track (trackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackId);
    if (! track || isReturn (*track) || ! static_cast<bool> (track->state.getProperty (midiTrackProperty))) return Result::fail ("Select a MIDI track first.");
    if (! std::isfinite (start) || ! std::isfinite (length) || start < 0 || length <= 0 || start + length > 1000000)
        return Result::fail ("Enter a positive MIDI clip duration and a valid start beat.");
    impl->transaction ("Add MIDI clip");
    const auto time = impl->edit->tempoSequence.toTime (te::BeatRange { te::BeatPosition::fromBeats (start), te::BeatPosition::fromBeats (start + length) });
    auto clip = track->insertMIDIClip ("MIDI clip", time, nullptr);
    if (! clip) return Result::fail ("Could not create the MIDI clip.");
    clip->setSyncType (te::Clip::syncBarsBeats);
    impl->changed(); return Result::ok();
}
Result Session::addScene (String name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing scenes.");
    auto& list = impl->edit->getSceneList();
    const auto count = list.getNumScenes();
    if (count >= 256) return Result::fail ("This project has reached the scene limit.");
    impl->transaction ("Add scene");
    list.ensureNumberOfScenes (count + 1);
    auto* scene = list.getScenes()[count];
    scene->state.setProperty ("halftrackSceneID", juce::Uuid().toString(), &impl->edit->getUndoManager());
    scene->name = name.trim().isEmpty() ? "Scene " + String (count + 1) : name.trim();
    impl->changed(); return Result::ok();
}
Result Session::removeScene (int index)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing scenes.");
    auto& list = impl->edit->getSceneList();
    if (! juce::isPositiveAndBelow (index, list.getNumScenes())) return Result::fail ("Scene not found.");
    impl->transaction ("Remove scene");
    list.deleteScene (*list.getScenes()[index]);
    impl->changed(); return Result::ok();
}
Result Session::duplicateScene (const String& id, String& duplicatedId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing scenes.");
    auto& list = impl->edit->getSceneList();
    int index = -1;
    for (int i = 0; i < list.getNumScenes(); ++i)
        if (list.getScenes()[i]->state.getProperty ("halftrackSceneID").toString() == id)
        {
            if (index >= 0) return Result::fail ("Scene identity is ambiguous.");
            index = i;
        }
    if (index < 0) return Result::fail ("Scene not found.");
    if (list.getNumScenes() >= 256) return Result::fail ("This project has reached the scene limit.");

    struct Copy { te::AudioTrack* track; te::ClipCopy clip; };
    std::vector<Copy> clips;
    std::vector<std::pair<te::AudioTrack*, te::Clip*>> sources;
    for (auto* track : te::getAudioTracks (*impl->edit))
    {
        auto* slot = track->getClipSlotList().getClipSlots()[index];
        if (! slot) return Result::fail ("The scene contains an incomplete slot row.");
        if (auto* clip = slot->getClip())
        {
            if (isReturn (*track) || track->isFrozen (te::Track::anyFreeze)
                || (! dynamic_cast<te::WaveAudioClip*> (clip) && ! dynamic_cast<te::MidiClip*> (clip)))
                return Result::fail ("Duplicate audio/MIDI scenes on unfrozen ordinary tracks.");
            sources.emplace_back (track, clip);
        }
    }
    // Prepare independent state only after validating every populated source.
    for (const auto& source : sources)
    {
        auto copy = te::ClipCopy::fromClip (*source.second).withNewItemID (*impl->edit);
        renewMidiNoteIDs (copy.getState());
        clips.push_back ({ source.first, std::move (copy) });
    }
    auto sceneState = list.getScenes()[index]->state.createCopy();
    const auto newId = juce::Uuid().toString();
    sceneState.setProperty ("halftrackSceneID", newId, nullptr);
    sceneState.setProperty (te::IDs::name, list.getScenes()[index]->name.get() + " copy", nullptr);
    impl->transaction ("Duplicate scene");
    auto* undo = &impl->edit->getUndoManager();
    auto* scene = list.insertScene (index + 1);
    scene->state.copyPropertiesFrom (sceneState, undo);
    for (const auto& copy : clips)
    {
        auto* slot = copy.track->getClipSlotList().getClipSlots()[index + 1];
        if (! te::insertClipCopy (*slot, copy.clip))
        {
            list.deleteScene (*scene);
            impl->changed();
            return Result::fail ("A scene clip could not be copied.");
        }
    }
    duplicatedId = newId;
    impl->changed();
    return Result::ok();
}
Result Session::moveScene (const String& id, int destination)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing scenes.");
    auto& list = impl->edit->getSceneList();
    if (! juce::isPositiveAndBelow (destination, list.getNumScenes())) return Result::fail ("Scene destination is outside the project.");
    int index = -1;
    for (int i = 0; i < list.getNumScenes(); ++i)
        if (list.getScenes()[i]->state.getProperty ("halftrackSceneID").toString() == id)
        {
            if (index >= 0) return Result::fail ("Scene identity is ambiguous.");
            index = i;
        }
    if (index < 0) return Result::fail ("Scene not found.");
    if (index == destination) return Result::ok();
    for (auto* track : te::getAudioTracks (*impl->edit))
        if (track->getClipSlotList().getClipSlots().size() != list.getNumScenes())
            return Result::fail ("The project contains an incomplete scene row.");
    impl->transaction ("Move scene");
    list.moveScene (index, destination);
    impl->changed();
    return Result::ok();
}
Result Session::renameScene (int index, const String& name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto& list = impl->edit->getSceneList();
    if (! juce::isPositiveAndBelow (index, list.getNumScenes())) return Result::fail ("Scene not found.");
    if (name.trim().isEmpty()) return Result::fail ("Enter a scene name.");
    impl->transaction ("Rename scene"); list.getScenes()[index]->name = name.trim();
    impl->changed(); return Result::ok();
}
Result Session::importAudioToSlot (const File& source, int trackIndex, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing slots.");
    auto* slot = impl->slot (trackIndex, sceneIndex);
    if (! slot) return Result::fail ("Slot not found.");
    if (slot->getClip()) return Result::fail ("Clear this slot before importing another clip.");
    if (static_cast<bool> (slot->track.state.getProperty (midiTrackProperty))) return Result::fail ("Import audio onto an audio track.");
    te::AudioFile audio (impl->engine, source);
    if (! audio.isValid() || audio.getLength() <= 0) return Result::fail ("This audio file could not be read.");
    auto assets = impl->assets(); auto result = assets.createDirectory(); if (result.failed()) return result;
    auto file = source.isAChildOf (assets) ? source : assets.getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), true);
    if (file != source && ! source.copyFileTo (file)) return Result::fail ("Could not copy the imported audio.");
    impl->transaction ("Import session clip");
    const te::TimeRange range { te::TimePosition::fromSeconds (0), te::TimePosition::fromSeconds (audio.getLength()) };
    auto clip = te::insertWaveClip (*slot, source.getFileNameWithoutExtension(), file, { range, {} }, te::DeleteExistingClips::no);
    if (! clip) return Result::fail ("Could not create the audio clip.");
    clip->getSourceFileReference().setToFile (file, te::SourceFileReference::PathStyle::alwaysAbsolute, false);
    clip->setAutoTempo (true); clip->setAutoPitch (false); clip->setUsesProxy (false);
    clip->setLoopRange (range);
    clip->setFadeIn (te::TimeDuration::fromSeconds (0)); clip->setFadeOut (te::TimeDuration::fromSeconds (0));
    impl->changed(); return Result::ok();
}
Result Session::addMidiClipToSlot (const String& trackId, int sceneIndex, double length)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing slots.");
    auto* track = impl->track (trackId);
    if (! track || isReturn (*track) || ! static_cast<bool> (track->state.getProperty (midiTrackProperty))) return Result::fail ("Select a MIDI track first.");
    if (! juce::isPositiveAndBelow (sceneIndex, impl->edit->getSceneList().getNumScenes())) return Result::fail ("Scene not found.");
    auto* slot = track->getClipSlotList().getClipSlots()[sceneIndex];
    if (! slot) return Result::fail ("Slot not found.");
    if (slot->getClip()) return Result::fail ("Clear this slot before creating another clip.");
    if (! std::isfinite (length) || length <= 0 || length > 1000000) return Result::fail ("Enter a positive clip length in beats.");
    impl->transaction ("Add session MIDI clip");
    const te::BeatRange beats { te::BeatPosition::fromBeats (0), te::BeatPosition::fromBeats (length) };
    auto clip = te::insertMIDIClip (*slot, "MIDI clip", impl->edit->tempoSequence.toTime (beats));
    if (! clip) return Result::fail ("Could not create the MIDI clip.");
    clip->setSyncType (te::Clip::syncBarsBeats); clip->setLoopRangeBeats (beats);
    impl->changed(); return Result::ok();
}
Result Session::clearSlot (int trackIndex, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing slots.");
    auto* slot = impl->slot (trackIndex, sceneIndex);
    if (! slot) return Result::fail ("Slot not found.");
    if (auto* clip = slot->getClip())
    {
        impl->transaction ("Clear session clip"); clip->removeFromParent(); impl->changed();
    }
    return Result::ok();
}
Result Session::setLaunchQuantisation (LaunchQuantisation quantisation)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    te::LaunchQType type;
    switch (quantisation)
    {
        case LaunchQuantisation::none: type = te::LaunchQType::none; break;
        case LaunchQuantisation::beat: type = te::LaunchQType::quarter; break;
        case LaunchQuantisation::bar: type = te::LaunchQType::bar; break;
        default: return Result::fail ("Unknown launch quantisation.");
    }
    impl->transaction ("Launch quantisation"); impl->edit->getLaunchQuantisation().type = type;
    impl->changed(); return Result::ok();
}
Result Session::setClipLaunchSettings (const String& id, const LaunchSettings& settings)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    te::Clip* clip = nullptr;
    for (auto* track : te::getAudioTracks (*impl->edit))
        for (auto* slot : track->getClipSlotList().getClipSlots()) if (auto* c = slot->getClip(); c && c->itemID.toString() == id) clip = c;
    if (! clip) return Result::fail ("Session clip not found.");
    if (static_cast<int> (settings.mode) < 0 || static_cast<int> (settings.mode) > 3
        || static_cast<int> (settings.quantisation) < 0 || static_cast<int> (settings.quantisation) >= (int) clipLaunchGrids.size())
        return Result::fail ("Unknown launch setting.");
    if (clipLaunchSettings (clip->state) == settings) return Result::ok();
    auto* um = &impl->edit->getUndoManager();
    impl->transaction ("Clip launch settings");
    clip->state.setProperty (launchModeProperty, static_cast<int> (settings.mode), um);
    clip->state.setProperty (launchQuantisationProperty, static_cast<int> (settings.quantisation), um);
    clip->state.setProperty (launchLegatoProperty, settings.legato, um);
    impl->changed(); return Result::ok();
}
Result Session::recordSlot (int trackIndex, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop Arrangement recording before recording into Session slots.");
    auto* slot = impl->slot (trackIndex, sceneIndex);
    if (! slot) return Result::fail ("Slot not found.");
    auto* track = impl->track (trackIndex);
    const auto trackId = track->itemID.toString();
    if (auto* active = impl->trackRecording (trackId))
    {
        if (active->stopping) return Result::ok();
        impl->finishSlotRecording (*active, active->slotId == slot->itemID, false);
        sendChangeMessage(); return Result::ok();
    }
    if (slot->getClip()) return Result::fail ("Session recording needs an empty slot.");
    auto* input = impl->input (*track);
    if (input == nullptr || ! input->isRecordingEnabled (track->itemID)) return Result::fail ("Arm the track to record into a Session slot.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio device in Audio Settings.");
    if (auto r = impl->assets().createDirectory(); r.failed()) return r;
    auto& transport = impl->edit->getTransport();
    transport.ensureContextAllocated();
    auto destination = input->setTarget (slot->itemID, false, nullptr);
    if (! destination) return Result::fail (destination.error());
    (*destination)->recordEnabled = true;
    // Apply the new destination to the graph before the transport starts, so a deferred
    // rebuild cannot replace a running play head.
    input->flushRecordingStateUpdates();
    impl->edit->dispatchPendingUpdatesSynchronously();
    const bool wasPlaying = transport.isPlaying();
    // From a stopped transport the take starts at once; otherwise on the launch grid.
    const auto boundary = wasPlaying ? impl->launchBoundary() : std::nullopt;
    const auto start = wasPlaying ? impl->unloopedTime (boundary) : transport.getPosition();
    if (auto* midi = dynamic_cast<te::MidiInputDevice*> (&input->getInputDevice())) midi->quantisation.setType ("(none)");
    te::InputDeviceInstance::RecordingParameters params;
    params.punchRange = { start, te::Edit::getMaximumEditTimeRange().getEnd() };
    params.targets = { slot->itemID };
    auto [contexts, errors] = te::extract (input->prepareToRecord (params));
    if (! errors.isEmpty()) { [[maybe_unused]] auto removed = input->removeTarget (slot->itemID, nullptr); return Result::fail (errors[0]); }
    // The track's other clips stop where the take begins.
    impl->queueTrack (*track, nullptr, boundary);
    input->startRecording (std::move (contexts));
    if (! transport.isPlaying()) transport.play (false);
    impl->slotRecordings.push_back ({ trackId, slot->itemID, start });
    sendChangeMessage(); return Result::ok();
}
Result Session::pressSlot (int trackIndex, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* slot = impl->slot (trackIndex, sceneIndex);
    if (! slot) return Result::fail ("Slot not found.");
    auto* clip = slot->getClip();
    // While a track records, any of its slot buttons ends the take; on an armed track an empty slot records.
    if (auto* active = impl->trackRecording (impl->track (trackIndex)->itemID.toString()))
    {
        if (active->slotId == slot->itemID || clip == nullptr) return recordSlot (trackIndex, sceneIndex);
        impl->finishSlotRecording (*active, false, false);
    }
    else if (clip == nullptr)
        if (auto* input = impl->input (*impl->track (trackIndex)); input && input->isRecordingEnabled (impl->track (trackIndex)->itemID))
            return recordSlot (trackIndex, sceneIndex);
    if (clip == nullptr) return launchSlot (trackIndex, sceneIndex);
    const auto settings = clipLaunchSettings (clip->state);
    if (settings.mode == LaunchMode::toggle)
        if (auto h = clip->getLaunchHandle(); h && (h->getPlayingStatus() == te::LaunchHandle::PlayState::playing
                                                    || h->getQueuedStatus() == te::LaunchHandle::QueueState::playQueued))
        {
            impl->queueTrack (*impl->track (trackIndex), nullptr, impl->clipBoundary (clip));
            sendChangeMessage(); return Result::ok();
        }
    const auto result = launchSlot (trackIndex, sceneIndex);
    if (result.wasOk() && (settings.mode == LaunchMode::gate || settings.mode == LaunchMode::repeat))
    {
        impl->heldSlots.push_back ({ impl->track (trackIndex)->itemID.toString(), clip->itemID.toString() });
        // Repeat restarts the clip every grid step from its launch for as long as it is held.
        if (settings.mode == LaunchMode::repeat)
            if (auto step = impl->gridStep (impl->clipGrid (clip))) clip->getLaunchHandle()->setLooping (step);
    }
    return result;
}
Result Session::releaseSlot (int trackIndex, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* slot = impl->slot (trackIndex, sceneIndex);
    if (! slot) return Result::fail ("Slot not found.");
    auto* track = impl->track (trackIndex);
    const auto trackId = track->itemID.toString();
    auto* clip = slot->getClip();
    const auto held = std::find_if (impl->heldSlots.begin(), impl->heldSlots.end(), [&] (const auto& h)
        { return h.trackId == trackId && clip != nullptr && h.clipId == clip->itemID.toString(); });
    if (held == impl->heldSlots.end()) return Result::ok();
    // Releasing a Gate or Repeat clip stops it on the clip's own grid.
    impl->queueTrack (*track, nullptr, impl->clipBoundary (clip));
    sendChangeMessage(); return Result::ok();
}
Result Session::launchSlot (int trackIndex, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* slot = impl->slot (trackIndex, sceneIndex);
    if (! slot) return Result::fail ("Slot not found.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio output in Audio Settings.");
    impl->edit->dispatchPendingUpdatesSynchronously();
    impl->edit->getTransport().ensureContextAllocated();
    impl->queueTrack (*impl->track (trackIndex), slot->getClip(), impl->clipBoundary (slot->getClip()));
    if (! impl->edit->getTransport().isPlaying()) impl->edit->getTransport().play (false);
    sendChangeMessage(); return Result::ok();
}
Result Session::launchScene (int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! juce::isPositiveAndBelow (sceneIndex, impl->edit->getSceneList().getNumScenes())) return Result::fail ("Scene not found.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio output in Audio Settings.");
    impl->edit->dispatchPendingUpdatesSynchronously();
    impl->edit->getTransport().ensureContextAllocated();
    const auto boundary = impl->launchBoundary();
    for (auto* track : te::getAudioTracks (*impl->edit))
        if (! isReturn (*track)) impl->queueTrack (*track, track->getClipSlotList().getClipSlots()[sceneIndex]->getClip(), boundary);
    if (! impl->edit->getTransport().isPlaying()) impl->edit->getTransport().play (false);
    sendChangeMessage(); return Result::ok();
}
Result Session::stopLauncherTrack (int trackIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackIndex); if (! track) return Result::fail ("Track not found.");
    if (isReturn (*track)) return Result::fail ("Return tracks do not launch clips.");
    if (auto* active = impl->trackRecording (track->itemID.toString()); active && ! active->stopping) impl->finishSlotRecording (*active, false, false);
    impl->queueTrack (*track, nullptr, impl->launchBoundary());
    sendChangeMessage(); return Result::ok();
}
Result Session::launchGroupSlot (const String& groupId, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* folder = impl->group (groupId);
    if (! folder) return Result::fail ("Group not found.");
    if (! juce::isPositiveAndBelow (sceneIndex, impl->edit->getSceneList().getNumScenes())) return Result::fail ("Scene not found.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio output in Audio Settings.");
    impl->edit->dispatchPendingUpdatesSynchronously();
    impl->edit->getTransport().ensureContextAllocated();
    const auto boundary = impl->launchBoundary();
    for (auto* track : te::getAudioTracks (*impl->edit))
        if (track->getParentFolderTrack() == folder) impl->queueTrack (*track, track->getClipSlotList().getClipSlots()[sceneIndex]->getClip(), boundary);
    if (! impl->edit->getTransport().isPlaying()) impl->edit->getTransport().play (false);
    sendChangeMessage(); return Result::ok();
}
Result Session::stopGroup (const String& groupId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* folder = impl->group (groupId);
    if (! folder) return Result::fail ("Group not found.");
    const auto boundary = impl->launchBoundary();
    for (auto* track : te::getAudioTracks (*impl->edit))
        if (track->getParentFolderTrack() == folder) impl->queueTrack (*track, nullptr, boundary);
    sendChangeMessage(); return Result::ok();
}
Result Session::stopAllLauncherClips()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    for (auto& recording : impl->slotRecordings) if (! recording.stopping) impl->finishSlotRecording (recording, false, false);
    const auto boundary = impl->launchBoundary();
    for (auto* track : te::getAudioTracks (*impl->edit))
        if (! isReturn (*track)) impl->queueTrack (*track, nullptr, boundary);
    sendChangeMessage(); return Result::ok();
}
Result Session::returnToArrangement()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->cancelLaunches();
    for (auto* track : te::getAudioTracks (*impl->edit))
    {
        track->playSlotClips = false;
        track->releaseLauncherOwnership();
    }
    sendChangeMessage(); return Result::ok();
}

Result Session::addMidiNote (const String& clipId, int pitch, double start, double length, float velocity)
{
    juce::StringArray insertedIds;
    return addMidiNotes (clipId, { { {}, pitch, start, length, velocity } }, insertedIds);
}
Result Session::addMidiNotes (const String& clipId, const std::vector<MidiNoteInfo>& notes, juce::StringArray& insertedIds)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    if (notes.empty()) { insertedIds.clear(); return Result::ok(); }
    const auto count = clip->getSequence().getNumNotes();
    if (count >= 100000 || notes.size() > static_cast<size_t> (100000 - count))
        return Result::fail ("The MIDI clip has reached its note limit.");
    for (const auto& note : notes)
        if (auto valid = impl->validateNote (*clip, note.pitch, note.startBeat, note.lengthBeats, note.velocity); valid.failed())
            return valid;
    std::set<String> unavailableIds;
    for (auto* note : clip->getSequence().getNotes())
        unavailableIds.insert (note->state.getProperty (midiNoteIDProperty).toString());
    for (const auto& note : notes) unavailableIds.insert (note.id);
    auto prepared = notes;
    juce::StringArray newIds;
    for (auto& note : prepared)
    {
        do { note.id = juce::Uuid().toString(); } while (! unavailableIds.insert (note.id).second);
        newIds.add (note.id);
    }
    impl->transaction (prepared.size() == 1 ? "Add MIDI note" : "Add MIDI notes");
    auto* undo = &impl->edit->getUndoManager();
    for (const auto& value : prepared)
    {
        auto* note = clip->getSequence().addNote (value.pitch, te::BeatPosition::fromBeats (value.startBeat),
                                                 te::BeatDuration::fromBeats (value.lengthBeats),
                                                 juce::jlimit (1, 127, juce::roundToInt (value.velocity * 127)), 0, undo);
        // Native addNote synchronously creates an event for its new NOTE state.
        jassert (note != nullptr);
        note->state.setProperty (midiNoteIDProperty, value.id, undo);
        note->setMute (!value.active, undo);
    }
    insertedIds = std::move (newIds);
    impl->changed(); return Result::ok();
}
Result Session::updateMidiNote (const String& clipId, const String& noteId, int pitch, double start, double length, float velocity)
{
    return updateMidiNotes (clipId, { { noteId, pitch, start, length, velocity } });
}
Result Session::updateMidiNotes (const String& clipId, const std::vector<MidiNoteInfo>& notes)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    if (notes.empty()) return Result::ok();
    std::map<String, te::MidiNote*> existing;
    for (auto* note : clip->getSequence().getNotes())
    {
        const auto id = note->state.getProperty (midiNoteIDProperty).toString();
        auto [entry, inserted] = existing.emplace (id, note);
        if (! inserted) entry->second = nullptr; // A loaded duplicate ID is ambiguous.
    }
    struct Change { te::MidiNote* note; MidiNoteInfo value; int velocity; };
    std::vector<Change> changes;
    std::set<String> requested;
    for (const auto& value : notes)
    {
        if (value.id.isEmpty() || ! requested.insert (value.id).second)
            return Result::fail ("Each MIDI note needs a unique identifier.");
        const auto found = existing.find (value.id);
        if (found == existing.end() || found->second == nullptr)
            return Result::fail ("MIDI note not found or its identifier is ambiguous.");
        auto valid = impl->validateNote (*clip, value.pitch, value.startBeat, value.lengthBeats, value.velocity);
        if (valid.failed()) return valid;
        auto* note = found->second;
        const int velocity = juce::jlimit (1, 127, juce::roundToInt (value.velocity * 127));
        if (note->getNoteNumber() != value.pitch || note->getStartBeat().inBeats() != value.startBeat
            || note->getLengthBeats().inBeats() != value.lengthBeats || note->getVelocity() != velocity)
            changes.push_back ({ note, value, velocity });
    }
    // Resolve and validate the whole batch before touching native state or undo.
    if (changes.empty()) return Result::ok();
    impl->transaction ("Edit MIDI notes");
    auto* undo = &impl->edit->getUndoManager();
    for (const auto& change : changes)
    {
        change.note->setNoteNumber (change.value.pitch, undo);
        change.note->setStartAndLength (te::BeatPosition::fromBeats (change.value.startBeat),
                                        te::BeatDuration::fromBeats (change.value.lengthBeats), undo);
        change.note->setVelocity (change.velocity, undo);
    }
    impl->changed(); return Result::ok();
}
Result Session::setMidiNotesActive (const String& clipId, const juce::StringArray& ids, bool active)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId);
    if (!clip) return Result::fail ("MIDI clip not found.");
    if (clip->getClipTrack()->isFrozen (te::Track::anyFreeze)) return Result::fail ("Unfreeze the track before editing notes.");
    std::map<String, te::MidiNote*> existing;
    for (auto* note : clip->getSequence().getNotes())
    {
        const auto id = note->state.getProperty (midiNoteIDProperty).toString();
        auto [entry, inserted] = existing.emplace (id, note);
        if (!inserted) entry->second = nullptr;
    }
    std::set<String> requested;
    std::vector<te::MidiNote*> changes;
    for (const auto& id : ids)
    {
        if (id.isEmpty() || !requested.insert (id).second) return Result::fail ("Each selected note needs a unique identifier.");
        const auto found = existing.find (id);
        if (found == existing.end() || !found->second) return Result::fail ("MIDI note not found or its identifier is ambiguous.");
        if (found->second->isMute() == active) changes.push_back (found->second);
    }
    if (changes.empty()) return Result::ok();
    impl->transaction (active ? "Activate MIDI notes" : "Deactivate MIDI notes");
    for (auto* note : changes) note->setMute (!active, &impl->edit->getUndoManager());
    impl->changed();
    return Result::ok();
}
Result Session::splitMidiNotes(const String& clipId,const juce::StringArray& ids,double beat,juce::StringArray& insertedIds)
{
    if(impl->exporting) return Result::fail("Finish or cancel the current export first.");
    auto* clip=impl->midiClip(clipId);if(!clip) return Result::fail("MIDI clip not found.");
    if(!std::isfinite(beat)||beat<0||beat>clip->getLengthInBeats().inBeats()) return Result::fail("Place the split marker inside the clip.");
    if(clip->getClipTrack()->isFrozen(te::Track::anyFreeze)) return Result::fail("Unfreeze the track before splitting notes.");
    std::map<String,te::MidiNote*> existing;
    for(auto* note:clip->getSequence().getNotes())
    {
        const auto id=note->state.getProperty(midiNoteIDProperty).toString();
        auto [entry,inserted]=existing.emplace(id,note);if(!inserted)entry->second=nullptr;
    }
    struct Split {te::MidiNote* note;double start,end;String id;};
    std::vector<Split> splits;std::set<String> requested;
    for(const auto& id:ids)
    {
        if(id.isEmpty()||!requested.insert(id).second) return Result::fail("Each selected note needs a unique identifier.");
        const auto entry=existing.find(id);
        if(entry==existing.end()||!entry->second) return Result::fail("MIDI note not found or its identifier is ambiguous.");
        auto* note=entry->second;const auto start=note->getStartBeat().inBeats(),end=start+note->getLengthBeats().inBeats();
        if(beat<=start||beat>=end)continue;
        if(note->state.getNumChildren()!=0) return Result::fail("Splitting per-note expression is not supported yet.");
        for(const auto part:std::array<std::pair<double,double>,2>{{{start,beat-start},{beat,end-beat}}})
            if(auto valid=impl->validateNote(*clip,note->getNoteNumber(),part.first,part.second,note->getVelocity()/127.0f);valid.failed())return valid;
        splits.push_back({note,start,end,{}});
    }
    if(splits.empty()){insertedIds.clear();return Result::ok();}
    if(splits.size()>static_cast<size_t>(std::max(0,100000-clip->getSequence().getNumNotes())))return Result::fail("The MIDI clip has reached its note limit.");
    std::set<String> unavailable;
    for(const auto& entry:existing)unavailable.insert(entry.first);
    juce::StringArray newIds;
    for(auto& split:splits){do{split.id=juce::Uuid().toString();}while(!unavailable.insert(split.id).second);newIds.add(split.id);}
    impl->transaction("Split MIDI notes");auto* undo=&impl->edit->getUndoManager();
    for(const auto& split:splits)
    {
        auto* right=clip->getSequence().addNote(*split.note,undo);
        right->state.setProperty(midiNoteIDProperty,split.id,undo);
        right->setStartAndLength(te::BeatPosition::fromBeats(beat),te::BeatDuration::fromBeats(split.end-beat),undo);
        split.note->setStartAndLength(te::BeatPosition::fromBeats(split.start),te::BeatDuration::fromBeats(beat-split.start),undo);
    }
    insertedIds=std::move(newIds);impl->changed();return Result::ok();
}
Result Session::transformMidiNotes (const String& clipId, const juce::StringArray& ids, NoteTransform operation, double grid)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    if (! std::isfinite (grid) || grid < 1.0/960 || grid > 16
        || static_cast<int> (operation) < 0 || static_cast<int> (operation) > 7)
        return Result::fail ("Invalid note transformation or grid.");
    if (ids.isEmpty()) return Result::ok();
    std::set<String> requested;
    for (const auto& id : ids) if (id.isEmpty() || ! requested.insert (id).second)
        return Result::fail ("Each selected note needs a unique identifier.");
    std::vector<MidiNoteInfo> notes;
    for (auto* note : clip->getSequence().getNotes())
    {
        const auto id = note->state.getProperty (midiNoteIDProperty).toString();
        if (requested.contains (id)) notes.push_back ({ id, note->getNoteNumber(), note->getStartBeat().inBeats(),
            note->getLengthBeats().inBeats(), note->getVelocity()/127.0f });
    }
    if (notes.size() != requested.size()) return Result::fail ("A selected note is missing or ambiguous.");
    const auto clipLength = clip->getLengthInBeats().inBeats();
    double first = notes.front().startBeat, last = first + notes.front().lengthBeats;
    int low = notes.front().pitch, high = low;
    for (const auto& note : notes)
    {
        first = std::min (first, note.startBeat); last = std::max (last, note.startBeat+note.lengthBeats);
        low = std::min (low, note.pitch); high = std::max (high, note.pitch);
    }
    const auto original = notes;
    for (auto& note : notes)
    {
        switch (operation)
        {
            case NoteTransform::quantizeStarts:
                note.startBeat = std::clamp (std::floor (note.startBeat/grid+.5)*grid, 0.0, std::max (0.0, clipLength-note.lengthBeats));
                break;
            case NoteTransform::quantizeEnds:
            {
                const auto end = std::min (clipLength, std::max (note.startBeat+std::min(grid,1.0/960),
                    std::floor ((note.startBeat+note.lengthBeats)/grid+.5)*grid));
                note.lengthBeats = std::max (1.0/960, end-note.startBeat); break;
            }
            case NoteTransform::reverse: note.startBeat = std::max (0.0, first+last-note.startBeat-note.lengthBeats); break;
            case NoteTransform::invertPitch: note.pitch = low+high-note.pitch; break;
            case NoteTransform::legato:
            {
                auto next = last;
                for (const auto& other : original) if (other.startBeat > note.startBeat+1e-10) next = std::min (next, other.startBeat);
                note.lengthBeats = next-note.startBeat; break;
            }
            case NoteTransform::halfTime:
            case NoteTransform::doubleTime:
            {
                const auto factor = operation == NoteTransform::halfTime ? .5 : 2.0;
                note.startBeat = first+(note.startBeat-first)*factor; note.lengthBeats *= factor; break;
            }
            case NoteTransform::fixedLength: note.lengthBeats = std::min (grid, clipLength-note.startBeat); break;
        }
    }
    // Existing batch validation rejects the complete edit before changing any note.
    return updateMidiNotes (clipId, notes);
}
Result Session::setMidiControllerLane (const String& clipId, int controller, const std::vector<ControllerPoint>& points)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing controllers.");
    auto* clip = impl->midiClip (clipId);
    if (clip == nullptr) return Result::fail ("MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    if (! canCopyMidiClip (*clip)) return Result::fail ("This clip's controllers cannot be edited.");
    if (! ((controller >= 0 && controller < 120) || controller == pitchBendLane || controller == channelPressureLane))
        return Result::fail ("Choose a controller from 0 to 119, pitch bend or channel pressure.");
    const auto length = clip->getLengthInBeats().inBeats();
    const int low = controller == pitchBendLane ? -8192 : 0, high = controller == pitchBendLane ? 8191 : 127;
    double previous = -1;
    if (points.size() > 16384) return Result::fail ("A lane holds at most 16384 points.");
    for (const auto& point : points)
    {
        if (! std::isfinite (point.beat) || point.beat < 0 || point.beat > length + 1e-9 || point.beat <= previous || point.value < low || point.value > high)
            return Result::fail ("Controller points need increasing beats inside the clip and values from " + String (low) + " to " + String (high) + ".");
        previous = point.beat;
    }
    auto& sequence = clip->getSequence();
    const int type = engineControllerType (controller);
    std::vector<ControllerPoint> current;
    for (auto* event : sequence.getControllerEvents())
        if (event->getType() == type) current.push_back ({ event->getBeatPosition().inBeats(), laneValue (controller, event->getControllerValue()) });
    std::stable_sort (current.begin(), current.end(), [] (const auto& a, const auto& b) { return a.beat < b.beat; });
    if (current == points) return Result::ok();
    impl->transaction ("Edit controller lane");
    auto* undo = &impl->edit->getUndoManager();
    const auto events = sequence.getControllerEvents(); // Removing events changes the live list.
    for (auto* event : events) if (event->getType() == type) sequence.removeControllerEvent (*event, undo);
    for (const auto& point : points) sequence.addControllerEvent (te::BeatPosition::fromBeats (point.beat), type, storedValue (controller, point.value), undo);
    impl->changed(); return Result::ok();
}
Result Session::removeMidiNote (const String& clipId, const String& noteId)
{
    return removeMidiNotes (clipId, { noteId });
}
Result Session::removeMidiNotes (const String& clipId, const juce::StringArray& noteIds)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    if (noteIds.isEmpty()) return Result::ok();
    std::map<String, te::MidiNote*> existing;
    for (auto* note : clip->getSequence().getNotes())
    {
        auto [entry, inserted] = existing.emplace (note->state.getProperty (midiNoteIDProperty).toString(), note);
        if (! inserted) entry->second = nullptr;
    }
    std::set<String> requested;
    std::vector<te::MidiNote*> targets;
    for (const auto& id : noteIds)
    {
        if (id.isEmpty() || ! requested.insert (id).second)
            return Result::fail ("Each MIDI note needs a unique identifier.");
        const auto found = existing.find (id);
        if (found == existing.end() || found->second == nullptr)
            return Result::fail ("MIDI note not found or its identifier is ambiguous.");
        targets.push_back (found->second);
    }
    impl->transaction (targets.size() == 1 ? "Remove MIDI note" : "Remove MIDI notes");
    for (auto* note : targets) clip->getSequence().removeNote (*note, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setSynthSettings (const String& trackId, const SynthSettings& settings)
{
    if (auto frozen = impl->unfrozen (impl->track (trackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackId); auto* synth = track ? Impl::synth (*track) : nullptr;
    if (! synth) return Result::fail ("Built-in synthesizer not found.");
    auto valid = [] (float value, float low, float high) { return std::isfinite (value) && value >= low && value <= high; };
    if (! valid (settings.cutoffHz, 20, 20000) || ! valid (settings.resonance, 0, 1)
        || ! valid (settings.attackSeconds, 0, 10) || ! valid (settings.decaySeconds, 0, 10)
        || ! valid (settings.sustain, 0, 1) || ! valid (settings.releaseSeconds, 0, 20)
        || ! valid (settings.level, 0, 1) || ! valid (settings.pulseWidth, 0, 1)
        || settings.waveform < 0 || settings.waveform > 3)
        return Result::fail ("Synthesizer settings are outside their supported range.");
    impl->transaction ("Synthesizer settings"); synth->setSettings (settings); impl->changed(); return Result::ok();
}
Result Session::setTrackInstrument (const String& trackId, std::optional<EffectKind> kind)
{
    if (auto frozen = impl->unfrozen (impl->track (trackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackId);
    if (! track || ! track->state.getProperty (midiTrackProperty, false)) return Result::fail ("Instruments need a MIDI track.");
    if (kind && ! effectIsInstrument (*kind)) return Result::fail ("That device is not an instrument.");
    auto* current = Impl::instrument (*track);
    if (current != nullptr)
    {
        if (! kind && dynamic_cast<SubtractiveSynthPlugin*> (current)) return Result::ok();
        if (auto* native = dynamic_cast<NativeEffectPlugin*> (current); kind && native && native->getKind() == *kind) return Result::ok();
    }
    juce::ValueTree state (te::IDs::PLUGIN);
    if (kind)
    {
        state.setProperty (te::IDs::type, NativeEffectPlugin::xmlTypeName, nullptr);
        state.setProperty ("effectKind", static_cast<int> (*kind), nullptr);
    }
    else state.setProperty (te::IDs::type, SubtractiveSynthPlugin::xmlTypeName, nullptr);
    return impl->replaceInstrument (*track, state, "Change instrument");
}
Result Session::setInstrumentSample (const String& trackId, const File& sample)
{
    if (auto frozen = impl->unfrozen (impl->track (trackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackId);
    if (! track || ! track->state.getProperty (midiTrackProperty, false)) return Result::fail ("Samplers need a MIDI track.");
    if (const auto problem = samplerSampleProblem (sample); problem.isNotEmpty()) return Result::fail (problem);
    if (auto* native = Impl::nativeInstrument (*track); native && native->getKind() == EffectKind::sampler)
    {
        if (native->sampleFile() == sample) return Result::ok();
        impl->transaction ("Load sample");
        native->state.setProperty (samplerSampleProperty, sample.getFullPathName(), &impl->edit->getUndoManager());
        impl->changed(); return Result::ok();
    }
    juce::ValueTree state (te::IDs::PLUGIN);
    state.setProperty (te::IDs::type, NativeEffectPlugin::xmlTypeName, nullptr);
    state.setProperty ("effectKind", static_cast<int> (EffectKind::sampler), nullptr);
    state.setProperty (samplerSampleProperty, sample.getFullPathName(), nullptr);
    return impl->replaceInstrument (*track, state, "Load sample");
}
Result Session::setDrumPadSample (const String& trackId, int pad, const File& sample)
{
    if (auto frozen = impl->unfrozen (impl->track (trackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackId);
    if (! track || ! track->state.getProperty (midiTrackProperty, false)) return Result::fail ("Drum Samplers need a MIDI track.");
    if (pad < 0 || pad >= drumPadCount) return Result::fail ("Drum pads are numbered 1 to 16.");
    if (sample != File()) if (const auto problem = samplerSampleProblem (sample); problem.isNotEmpty()) return Result::fail (problem);
    const auto path = sample == File() ? String() : sample.getFullPathName();
    if (auto* native = Impl::nativeInstrument (*track); native && native->getKind() == EffectKind::drumSampler)
    {
        if (native->sampleFile (pad) == sample) return Result::ok();
        impl->transaction (sample == File() ? "Clear drum pad" : "Load drum pad");
        native->state.setProperty (drumPadSampleProperty (pad), path, &impl->edit->getUndoManager());
        impl->changed(); return Result::ok();
    }
    if (sample == File()) return Result::fail ("This track has no Drum Sampler.");
    juce::ValueTree state (te::IDs::PLUGIN);
    state.setProperty (te::IDs::type, NativeEffectPlugin::xmlTypeName, nullptr);
    state.setProperty ("effectKind", static_cast<int> (EffectKind::drumSampler), nullptr);
    state.setProperty (drumPadSampleProperty (pad), path, nullptr);
    return impl->replaceInstrument (*track, state, "Load drum pad");
}
Result Session::showInstrumentEditor (int trackIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackIndex);
    auto* native = track ? Impl::nativeInstrument (*track) : nullptr;
    if (! native) return Result::fail ("This track uses the built-in synthesizer panel.");
    native->windowState->showWindowExplicitly();
    auto* window = dynamic_cast<PluginWindow*> (native->windowState->pluginWindow.get());
    if (window == nullptr || ! window->isVisible() || window->getContentComponent() == nullptr) return Result::fail ("This instrument has no available editor.");
    return Result::ok();
}
Result Session::groupTracks (const juce::StringArray& ids, String& groupId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before grouping tracks.");
    if (ids.isEmpty()) return Result::fail ("Select at least one track to group.");
    std::set<String> wanted;
    for (const auto& id : ids) if (! wanted.insert (id).second) return Result::fail ("Each track can be grouped once.");
    // Members keep their visual order regardless of the request order.
    juce::Array<te::AudioTrack*> members;
    for (auto* track : te::getAudioTracks (*impl->edit)) if (wanted.contains (track->itemID.toString())) members.add (track);
    if ((size_t) members.size() != wanted.size()) return Result::fail ("Track not found.");
    for (auto* track : members)
    {
        if (isReturn (*track)) return Result::fail ("Return tracks cannot be grouped.");
        if (track->getParentTrack() != nullptr) return Result::fail ("Grouped tracks cannot be grouped again.");
    }
    impl->transaction ("Group tracks");
    auto folder = impl->edit->insertNewFolderTrack (te::TrackInsertPoint (*members.getFirst(), true), nullptr, true);
    if (folder == nullptr) return Result::fail ("Could not create the group.");
    if (auto result = useTrackMixer (*folder); result.failed()) return result;
    int count = 0; for (auto* track : te::getAllTracks (*impl->edit)) if (auto* f = dynamic_cast<te::FolderTrack*> (track); f && f->isSubmixFolder()) ++count;
    folder->setName ("Group " + String (count));
    folder->setColour (members.getFirst()->getColour());
    te::Track* preceding = nullptr;
    for (auto* track : members) { impl->edit->moveTrack (te::Track::Ptr (track), te::TrackInsertPoint (folder.get(), preceding)); preceding = track; }
    groupId = folder->itemID.toString();
    impl->changed(); return Result::ok();
}
Result Session::ungroupTracks (const String& groupId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before ungrouping tracks.");
    auto* folder = impl->group (groupId); if (! folder) return Result::fail ("Group not found.");
    juce::Array<te::Track*> members;
    for (auto* track : te::getAllTracks (*impl->edit)) if (track->getParentTrack() == folder) members.add (track);
    impl->transaction ("Ungroup tracks");
    te::Track* preceding = folder;
    for (auto* track : members)
    { impl->edit->moveTrack (te::Track::Ptr (track), te::TrackInsertPoint (folder->getParentTrack(), preceding)); preceding = track; }
    impl->edit->deleteTrack (folder);
    impl->changed(); return Result::ok();
}
Result Session::renameGroup (const String& groupId, const String& name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* folder = impl->group (groupId); if (! folder) return Result::fail ("Group not found.");
    if (name.trim().isEmpty()) return Result::fail ("Enter a group name.");
    if (folder->getName() == name.trim()) return Result::ok();
    impl->transaction ("Rename group"); folder->setName (name.trim()); impl->changed(); return Result::ok();
}
Result Session::setGroupGain (const String& groupId, float value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* folder = impl->group (groupId); auto* volume = folder ? folder->getVolumePlugin() : nullptr;
    if (! volume) return Result::fail ("Group not found.");
    if (! std::isfinite (value)) return Result::fail ("Invalid gain.");
    value = juce::jlimit (-100.0f, 6.0f, value);
    // As on track faders, the undoable manual level is what undo restores to the parameter.
    if (! volume->state.hasProperty (manualVolumeProperty))
        volume->state.setProperty (manualVolumeProperty, volume->volParam->getCurrentExplicitValue(), nullptr);
    impl->transaction ("Group gain"); volume->setVolumeDb (value);
    volume->state.setProperty (manualVolumeProperty, te::decibelsToVolumeFaderPosition (value), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setGroupPan (const String& groupId, float value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* folder = impl->group (groupId); auto* volume = folder ? folder->getVolumePlugin() : nullptr;
    if (! volume) return Result::fail ("Group not found.");
    if (! std::isfinite (value)) return Result::fail ("Invalid pan.");
    value = juce::jlimit (-1.0f, 1.0f, value);
    if (! volume->state.hasProperty (manualPanProperty))
        volume->state.setProperty (manualPanProperty, volume->panParam->getCurrentExplicitValue(), nullptr);
    impl->transaction ("Group pan"); volume->setPan (value);
    volume->state.setProperty (manualPanProperty, value, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setGroupMute (const String& groupId, bool value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* folder = impl->group (groupId); if (! folder) return Result::fail ("Group not found.");
    impl->transaction ("Group mute");
    folder->state.setProperty (te::IDs::mute, value, &impl->edit->getUndoManager());
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::setGroupSolo (const String& groupId, bool value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* folder = impl->group (groupId); if (! folder) return Result::fail ("Group not found.");
    impl->transaction ("Group solo");
    folder->state.setProperty (te::IDs::solo, value, &impl->edit->getUndoManager());
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::setGroupFolded (const String& groupId, bool folded)
{
    auto* folder = impl->group (groupId); if (! folder) return Result::fail ("Group not found.");
    // Folding is view state: saved with the project, not a musical undo step.
    folder->state.setProperty (groupFoldedProperty, folded, nullptr); impl->changed(); return Result::ok();
}
Result Session::duplicateTrack (const String& id, String& duplicateId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before duplicating tracks.");
    auto* source = impl->track (id);
    if (! source || isReturn (*source) || source->getParentTrack() != nullptr || source->isFrozen (te::Track::anyFreeze))
        return Result::fail ("Duplicate an unfrozen top-level audio or MIDI track.");
    impl->transaction ("Duplicate track");
    auto copy = impl->edit->copyTrack (te::Track::Ptr (source), te::TrackInsertPoint (*source, false));
    auto* track = dynamic_cast<te::AudioTrack*> (copy.get());
    if (! track) return Result::fail ("Could not duplicate the track.");
    auto* undo = &impl->edit->getUndoManager();
    // Native construction restores explicit values through the manual setter,
    // which can rewrite a single-point curve. Restore source points inside this
    // same duplication transaction, after construction has completed.
    if (auto* sourceMixer = source->getVolumePlugin())
        if (auto* copiedMixer = track->getVolumePlugin())
            for (const auto target : { AutomationTarget::volume, AutomationTarget::pan })
            {
                auto& original = automationParameter (*sourceMixer, target).getCurve();
                if (original.getNumPoints() == 0) continue;
                auto& parameter = automationParameter (*copiedMixer, target); auto& copiedCurve = parameter.getCurve();
                copiedCurve.clear (undo);
                for (int i = 0; i < original.getNumPoints(); ++i)
                {
                    const auto point = original.getPoint (i);
                    copiedCurve.addPoint (point.time, point.value, point.curve, undo);
                }
                parameter.updateStream();
                parameter.updateFromAutomationSources (impl->edit->getTransport().getPosition());
            }
    renewMidiNoteIDs (track->state, undo);
    // A copied rack would share its chains with the original, so each copy gets its own rack.
    for (auto* p : track->pluginList.getPlugins())
        if (auto* rack = dynamic_cast<te::RackInstance*> (p); rack && rack->type)
        {
            auto type = impl->edit->getRackList().duplicateRack (*rack->type);
            if (type == nullptr) return Result::fail ("Could not duplicate the track's rack.");
            auto state = rack->state.createCopy();
            state.setProperty (te::IDs::rackType, type->itemID.toString(), nullptr);
            te::EditItemID::remapIDs (state, nullptr, *impl->edit);
            const int at = track->pluginList.indexOf (rack);
            rack->deleteFromParent();
            track->pluginList.insertPlugin (impl->edit->getPluginCache().createNewPlugin (state), at, nullptr);
        }
    track->setName (source->getName() + " copy");
    // Input target assignments live outside this copied track, so the new ID has
    // no input/arm state. Runtime Session ownership must not silence its Arrangement.
    track->state.setProperty (te::IDs::playSlotClips, false, undo);
    track->state.setProperty (monitorProperty, static_cast<int> (MonitorMode::off), undo);
    impl->refreshRouting(); impl->changed(); duplicateId = track->itemID.toString(); return Result::ok();
}
Result Session::moveTrack (const String& id, int destinationIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before reordering tracks.");
    const auto tracks = te::getAudioTracks (*impl->edit);
    auto* source = impl->track (id);
    if (! source || ! juce::isPositiveAndBelow (destinationIndex, tracks.size()))
        return Result::fail ("Track or destination position not found.");
    auto* target = tracks[destinationIndex];
    if (source->getParentTrack() != nullptr || target->getParentTrack() != nullptr)
        return Result::fail ("Track ordering currently supports top-level tracks.");
    const int sourceIndex = tracks.indexOf (source);
    if (sourceIndex == destinationIndex) return Result::ok();
    impl->transaction ("Reorder track");
    impl->edit->moveTrack (te::Track::Ptr (source), te::TrackInsertPoint (*target, destinationIndex < sourceIndex));
    impl->changed(); return Result::ok();
}
Result Session::setTrackFrozen (const String& trackId, bool frozen)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before freezing tracks.");
    auto* track = impl->track (trackId);
    if (track == nullptr || isReturn (*track)) return Result::fail ("Freeze an audio or MIDI track.");
    if (track->isFrozen (te::Track::individualFreeze) == frozen) return Result::ok();
    if (frozen && track->getClips().isEmpty()) return Result::fail ("This track has no Arrangement clips to freeze.");
    closePluginEditors();
    impl->transaction (frozen ? "Freeze track" : "Unfreeze track");
    // The engine renders the freeze, or removes it, as this undoable property changes.
    track->state.setProperty (te::IDs::frozenIndividually, frozen, &impl->edit->getUndoManager());
    impl->edit->dispatchPendingUpdatesSynchronously();
    if (frozen && ! track->isFrozen (te::Track::individualFreeze)) return Result::fail ("The track could not be frozen.");
    impl->changed(); return Result::ok();
}
Result Session::flattenTrack (const String& trackId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before flattening tracks.");
    auto* track = impl->track (trackId);
    if (track == nullptr || ! track->isFrozen (te::Track::individualFreeze)) return Result::fail ("Freeze the track before flattening it.");
    const bool midi = track->state.getProperty (midiTrackProperty, false);
    if (midi)
        for (auto* slot : track->getClipSlotList().getClipSlots())
            if (slot->getClip() != nullptr) return Result::fail ("Clear this MIDI track's Session clips before flattening it.");
    const auto freeze = te::TemporaryFileManager::getFreezeFileForTrack (*track);
    std::unique_ptr<juce::AudioFormatReader> reader (impl->engine.getAudioFileFormatManager().readFormatManager.createReaderFor (freeze));
    if (reader == nullptr || reader->lengthInSamples <= 0) return Result::fail ("The frozen render is unavailable. Unfreeze and freeze the track again.");
    // The render becomes ordinary project audio: a 32-bit float WAV in the Assets folder.
    auto assets = impl->assets(); if (auto r = assets.createDirectory(); r.failed()) return r;
    const auto file = assets.getNonexistentChildFile (File::createLegalFileName (track->getName()) + " Flattened", ".wav", true);
    {
        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        if (! reader->read (&audio, 0, audio.getNumSamples(), 0, true, true)) return Result::fail ("The frozen render could not be read.");
        juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (reader->sampleRate)
                                                        .withNumChannels ((int) reader->numChannels).withBitsPerSample (32));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) { file.deleteFile(); return Result::fail ("Could not write the flattened audio."); }
    }
    const auto seconds = (double) reader->lengthInSamples / reader->sampleRate;
    reader.reset();
    closePluginEditors();
    // The engine cannot re-render a freeze while an undo is in progress, so the track is
    // unfrozen outside history: undoing Flatten restores the unfrozen track and its devices.
    track->state.setProperty (te::IDs::frozenIndividually, false, nullptr);
    impl->transaction ("Flatten track");
    auto* undo = &impl->edit->getUndoManager();
    // Remove the sound sources and devices; the mixer, meter and sends stay.
    for (auto* plugin : track->pluginList.getPlugins())
        if (dynamic_cast<NativeEffectPlugin*> (plugin) || dynamic_cast<te::ExternalPlugin*> (plugin) || dynamic_cast<UtilityPlugin*> (plugin)
            || dynamic_cast<SubtractiveSynthPlugin*> (plugin) || dynamic_cast<te::FreezePointPlugin*> (plugin) || dynamic_cast<te::RackInstance*> (plugin))
            plugin->deleteFromParent();
    impl->removeUnusedRacks();
    for (auto* clip : track->getClips()) clip->removeFromParent();
    if (midi)
    {
        track->state.setProperty (midiTrackProperty, false, undo);
        for (auto* input : impl->edit->getAllInputDevices())
            if (input->getTargets().contains (track->itemID)) [[maybe_unused]] auto removed = input->removeTarget (track->itemID, undo);
    }
    auto clip = te::insertWaveClip (*track, track->getName() + " Flattened", file, { { te::TimePosition(), te::TimePosition::fromSeconds (seconds) }, {} }, te::DeleteExistingClips::no);
    if (! clip) return Result::fail ("Could not create the flattened clip.");
    clip->getSourceFileReference().setToFile (file, te::SourceFileReference::PathStyle::alwaysAbsolute, false);
    clip->setAutoTempo (false); clip->setAutoPitch (false);
    clip->setFadeIn (te::TimeDuration::fromSeconds (0)); clip->setFadeOut (te::TimeDuration::fromSeconds (0));
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::removeTrack (int index)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before removing tracks.");
    closePluginEditors(); impl->meters.clear(); impl->transaction ("Remove track");
    if (isReturn (*t))
        for (auto* source : te::getAudioTracks (*impl->edit))
            if (auto* send = findSend (*source, t->itemID.toString())) send->deleteFromParent();
    impl->edit->deleteTrack (t); impl->removeUnusedRacks(); impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::importAudio (const File& source, int index, double start)
{
    if (auto frozen = impl->unfrozen (impl->track (index)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! source.existsAsFile()) return Result::fail ("The audio file does not exist.");
    te::AudioFile audio (impl->engine, source);
    if (! audio.isValid() || audio.getLength() <= 0) return Result::fail ("This audio file could not be read.");
    if (! std::isfinite (start) || start < 0) return Result::fail ("Clip start must be zero or greater.");
    if (index < 0) { auto r = addAudioTrack (source.getFileNameWithoutExtension()); if (r.failed()) return r; index = te::getAudioTracks (*impl->edit).size() - 1; }
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (isReturn (*t) || static_cast<bool> (t->state.getProperty (midiTrackProperty))) return Result::fail ("Import audio onto an audio track.");
    auto assets = impl->assets(); auto r = assets.createDirectory(); if (r.failed()) return r;
    auto file = source.isAChildOf (assets) ? source : assets.getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), true);
    if (file != source && ! source.copyFileTo (file)) return Result::fail ("Could not copy the imported audio.");
    impl->transaction ("Import audio");
    auto clip = t->insertWaveClip (source.getFileNameWithoutExtension(), file,
        { { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (start + audio.getLength()) }, {} }, false);
    if (! clip) return Result::fail ("Could not create the audio clip.");
    clip->getSourceFileReference().setToFile (file, te::SourceFileReference::PathStyle::alwaysAbsolute, false);
    clip->setAutoTempo (false); clip->setAutoPitch (false);
    clip->setFadeIn (te::TimeDuration::fromSeconds (0)); clip->setFadeOut (te::TimeDuration::fromSeconds (0));
    impl->changed(); return Result::ok();
}
double Session::estimateClipTempo (double seconds)
{
    if (! std::isfinite (seconds) || seconds <= 0) return 120;
    double beats = 4, bpm = beats * 60 / seconds;
    while (bpm < 80) { beats *= 2; bpm *= 2; }
    while (bpm >= 160 && beats > .25) { beats /= 2; bpm /= 2; }
    return juce::jlimit (20.0, 999.0, bpm);
}
Result Session::setAudioClipWarp (const String& id, const WarpSettings& settings)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (! std::isfinite (settings.bpm) || settings.bpm < 20 || settings.bpm > 999) return Result::fail ("Clip tempo must be 20-999 BPM.");
    if (clip->getIsReversed() || clip->isUsingARA() || clip->isGrouped()) return Result::fail ("Unreverse or ungroup this clip before warping it.");
    const bool slot = clip->getClipSlot() != nullptr;
    if (slot && settings.mode == WarpMode::off) return Result::fail ("Session clips always follow the project tempo.");
    const auto previous = warpSettings (*clip);
    if (previous == settings || (settings.mode == WarpMode::off && previous.mode == WarpMode::off)) return Result::ok();
    const auto source = clip->getSourceLength().inSeconds();
    if (source <= 0) return Result::fail ("The clip's audio is not available.");
    const auto tempo = impl->edit->tempoSequence.getBeatsPerSecondAt (clip->getPosition().getStart()) * 60;
    const auto position = clip->getPosition();
    const auto start = position.getStart(); auto length = position.getLength().inSeconds(); auto offset = position.getOffset().inSeconds();
    impl->transaction ("Warp audio clip");
    if (settings.mode == WarpMode::off)
    {
        // Back to source time: the visible source span is unchanged.
        const auto scale = tempo / previous.bpm;
        length *= scale; offset *= scale;
        offset = juce::jlimit (0.0, source, offset); length = juce::jlimit (0.001, source - offset, length);
        clip->setAutoTempo (false); clip->setTimeStretchMode (te::TimeStretcher::disabled);
        clip->setPosition ({ { start, start + te::TimeDuration::fromSeconds (length) }, te::TimeDuration::fromSeconds (offset) });
        impl->changed(); return Result::ok();
    }
    clip->setTimeStretchMode (settings.mode == WarpMode::efficient ? te::TimeStretcher::signalsmithCheaper : te::TimeStretcher::signalsmithDefault);
    clip->setUsesProxy (false);
    const auto scale = previous.mode == WarpMode::off ? settings.bpm / tempo : settings.bpm / previous.bpm;
    clip->getLoopInfo().setNumBeats (settings.bpm * source / 60);
    if (slot)
    {
        if (previous.mode != WarpMode::off && previous.bpm != settings.bpm)
        {
            const auto loop = clip->getLoopRangeBeats();
            clip->setLoopRangeBeats ({ te::BeatPosition::fromBeats (loop.getStart().inBeats() * scale), te::BeatPosition::fromBeats (loop.getEnd().inBeats() * scale) });
        }
    }
    else
    {
        if (previous.mode == WarpMode::off) clip->setAutoTempo (true);
        if (scale != 1)
            clip->setPosition ({ { start, start + te::TimeDuration::fromSeconds (length * scale) }, te::TimeDuration::fromSeconds (offset * scale) });
    }
    impl->changed(); return Result::ok();
}
Result Session::setAudioClipSettings (const String& id, const AudioClipSettings& settings)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (! validAudioSettings (settings, clip->getPosition().getLength().inSeconds()))
        return Result::fail ("Clip gain or fades are outside their supported range. Fades must fit inside the clip.");
    const auto previous = audioSettings (*clip);
    if (clip->getClipSlot() && (previous.fadeInSeconds != settings.fadeInSeconds || previous.fadeOutSeconds != settings.fadeOutSeconds
        || previous.fadeInCurve != settings.fadeInCurve || previous.fadeOutCurve != settings.fadeOutCurve))
        return Result::fail ("Manual fade editing is available for Arrangement clips.");
    if (previous == settings && ! clip->getAutoCrossfade()) return Result::ok();
    impl->transaction ("Audio clip gain and fades");
    clip->setAutoCrossfade (false); clip->setGainDB (settings.gainDb);
    // Clear first so the engine's one-edge setters cannot rescale the other edge.
    clip->setFadeIn ({}); clip->setFadeOut (te::TimeDuration::fromSeconds (settings.fadeOutSeconds));
    clip->setFadeIn (te::TimeDuration::fromSeconds (settings.fadeInSeconds));
    clip->setFadeInType (static_cast<te::AudioFadeCurve::Type> (settings.fadeInCurve));
    clip->setFadeOutType (static_cast<te::AudioFadeCurve::Type> (settings.fadeOutCurve));
    impl->changed(); return Result::ok();
}
Result Session::setAudioClipReversed (const String& id, bool reversed)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before reversing clips.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (! canReverseAudio (*clip)) return Result::fail ("Reverse requires a single-source, unwarped, unlooped Arrangement clip at its original speed and pitch.");
    const te::AudioFile source (impl->engine,clip->getOriginalFile());
    const auto position = clip->getPosition();
    if (! source.isValid() || position.offset.inSeconds() < 0
        || position.offset.inSeconds()+position.getLength().inSeconds() > source.getLength()+1e-9)
        return Result::fail ("The source must be readable and contain the complete clip region.");
    if (clip->getIsReversed() == reversed) return Result::ok();
    impl->transaction ("Reverse audio clip");
    // The native setter mirrors the source offset and loop metadata inside this
    // same undo transaction. Reversed playback is generated off the audio thread.
    clip->setIsReversed (reversed); impl->changed(); return Result::ok();
}
Result Session::setAudioClipRange (const String& id, double start, double length, double offset)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before trimming clips.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (! canEditAudioRange (*clip)) return Result::fail ("Range editing requires an unwarped, unlooped arrangement audio clip.");
    const auto sourceLength = clip->getSourceLength().inSeconds();
    if (! std::isfinite (start) || ! std::isfinite (length) || ! std::isfinite (offset)
        || start < 0 || length < .001 || start + length > te::Edit::maximumLength || offset < 0
        || offset + length > sourceLength + 1e-9 || ! validAudioSettings (audioSettings (*clip), length))
        return Result::fail ("The range must fit the source and existing fades, with at least 1 ms of audio.");
    const auto p = clip->getPosition();
    if (p.getStart().inSeconds() == start && p.getLength().inSeconds() == length && p.offset.inSeconds() == offset) return Result::ok();
    impl->transaction ("Trim audio clip");
    clip->setPosition ({ { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (start+length) }, te::TimeDuration::fromSeconds (offset) });
    impl->changed(); return Result::ok();
}
Result Session::splitAudioClip (const String& id, double at, String& rightId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before splitting clips.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    const auto p = clip->getPosition(); const auto s = audioSettings (*clip);
    const auto left = at-p.getStart().inSeconds(), right = p.getEnd().inSeconds()-at;
    if (! canEditAudioRange (*clip) || ! std::isfinite (at) || left < .002 || right < .002
        || left < s.fadeInSeconds || right < s.fadeOutSeconds)
        return Result::fail ("Split inside an unwarped arrangement clip, outside its fades, leaving at least 2 ms on each side.");
    impl->transaction ("Split audio clip");
    auto* second = clip->getClipTrack()->splitClip (*clip, te::TimePosition::fromSeconds (at));
    if (! second) return Result::fail ("The audio clip could not be split.");
    rightId = second->itemID.toString(); impl->changed(); return Result::ok();
}
// Time edits across the whole arrangement. Every automation curve moves with the material.
namespace
{
std::vector<te::AutomationCurve*> arrangementCurves (te::Edit& edit)
{
    std::vector<te::AutomationCurve*> curves;
    const auto add = [&curves] (te::Plugin& plugin) { for (auto* parameter : plugin.getAutomatableParameters()) if (parameter->getCurve().getNumPoints() > 0) curves.push_back (&parameter->getCurve()); };
    for (auto* track : te::getAllTracks (edit))
        if (dynamic_cast<te::AudioTrack*> (track) != nullptr || dynamic_cast<te::FolderTrack*> (track) != nullptr)
            for (auto* plugin : track->pluginList.getPlugins()) add (*plugin);
    if (auto master = edit.getMasterVolumePlugin()) add (*master);
    return curves;
}
// Removes points in `removed` and moves points at or after `from` by `delta`.
void shiftCurve (te::AutomationCurve& curve, te::Edit& edit, te::TimePosition from, te::TimeDuration delta, std::optional<te::TimeRange> removed, juce::UndoManager* undo)
{
    struct Point { te::TimePosition time; float value, shape; };
    std::vector<Point> points; bool changed = false;
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        auto time = curve.getPointTime (i);
        if (removed && time >= removed->getStart() && time < removed->getEnd()) { changed = true; continue; }
        if (time >= from) { time = time + delta; changed = true; }
        points.push_back ({ time, curve.getPointValue (i), curve.getPointCurve (i) });
    }
    if (! changed) return;
    curve.clear (undo);
    for (const auto& p : points) curve.addPoint (te::EditPosition (edit.tempoSequence.toBeats (p.time)), p.value, p.shape, undo);
}
// A clip boundary edit can split an editable audio clip at `at` leaving 2 ms outside its fades on each side.
bool canSplitAt (const te::WaveAudioClip& clip, double at)
{
    const auto p = clip.getPosition(); const auto s = audioSettings (clip);
    const auto left = at - p.getStart().inSeconds(), right = p.getEnd().inSeconds() - at;
    return canEditAudioRange (clip) && left >= .002 && right >= .002 && left >= s.fadeInSeconds && right >= s.fadeOutSeconds;
}
// A MIDI clip can be split strictly inside it, at least a 960th of a beat from either end.
bool canSplitMidiAt (const te::MidiClip& clip, te::TimePosition at)
{
    const auto& tempo = clip.edit.tempoSequence;
    const auto beat = (tempo.toBeats (at) - clip.getStartBeat()).inBeats();
    return canCopyMidiClip (clip) && beat >= 1.0 / 960 && clip.getLengthInBeats().inBeats() - beat >= 1.0 / 960;
}
// Splits a MIDI clip into two clips that both start at zero offset. Notes crossing the split
// become two notes, and each controller's value at the split starts the right-hand clip.
te::MidiClip* splitMidiClipAt (te::MidiClip& clip, te::TimePosition at, juce::UndoManager* undo)
{
    auto& edit = clip.edit;
    const auto split = edit.tempoSequence.toBeats (at) - clip.getStartBeat();
    const auto end = clip.getPosition().getEnd();
    auto copy = te::ClipCopy::fromClipboardState (clip.state, false).withNewItemID (edit);
    auto state = copy.getState();
    state.setProperty (te::IDs::start, at.inSeconds(), nullptr); state.setProperty (te::IDs::length, (end - at).inSeconds(), nullptr);
    renewMidiNoteIDs (state);
    auto* right = dynamic_cast<te::MidiClip*> (te::insertClipCopy (*clip.getClipTrack(), copy));
    if (right == nullptr) return nullptr;
    auto& left = clip.getSequence();
    const auto leftNotes = left.getNotes();
    for (auto* note : leftNotes)
    {
        if (note->getStartBeat() >= toPosition (split)) left.removeNote (*note, undo);
        else if (note->getEndBeat() > toPosition (split)) note->setStartAndLength (note->getStartBeat(), split - toDuration (note->getStartBeat()), undo);
    }
    std::map<int, int> carried; // Each controller's value at the split.
    const auto leftEvents = left.getControllerEvents();
    for (auto* event : leftEvents)
    {
        if (event->getBeatPosition() < toPosition (split)) carried[event->getType()] = event->getControllerValue();
        else left.removeControllerEvent (*event, undo);
    }
    auto& sequence = right->getSequence();
    const auto rightNotes = sequence.getNotes();
    for (auto* note : rightNotes)
    {
        if (note->getEndBeat() <= toPosition (split)) sequence.removeNote (*note, undo);
        else if (note->getStartBeat() < toPosition (split)) note->setStartAndLength (te::BeatPosition(), note->getEndBeat() - toPosition (split), undo);
        else note->setStartAndLength (note->getStartBeat() - split, note->getLengthBeats(), undo);
    }
    std::set<int> startsAtZero;
    const auto rightEvents = sequence.getControllerEvents();
    for (auto* event : rightEvents)
    {
        if (event->getBeatPosition() < toPosition (split)) { sequence.removeControllerEvent (*event, undo); continue; }
        event->setBeatPosition (event->getBeatPosition() - split, undo);
        if (event->getBeatPosition().inBeats() < 1e-9) startsAtZero.insert (event->getType());
    }
    for (const auto& [type, value] : carried)
        if (! startsAtZero.contains (type)) sequence.addControllerEvent (te::BeatPosition(), type, value, undo);
    clip.setLength (at - clip.getPosition().getStart(), true);
    return right;
}
// Whether every Arrangement clip of the track crossing `at` can be split there.
Result splittableAt (te::AudioTrack& track, te::TimePosition at)
{
    for (auto* clip : track.getClips())
    {
        const auto p = clip->getPosition();
        if (p.getStart() >= at || p.getEnd() <= at) continue;
        if (track.isFrozen (te::Track::anyFreeze)) return Result::fail ("Unfreeze \"" + track.getName() + "\" before editing its clips.");
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip); wave != nullptr && canSplitAt (*wave, at.inSeconds())) continue;
        if (auto* midi = dynamic_cast<te::MidiClip*> (clip); midi != nullptr && canSplitMidiAt (*midi, at)) continue;
        return Result::fail ("\"" + clip->getName() + "\" cannot be split at the selection edge (warped, looped, inside a fade or too close to its end).");
    }
    return Result::ok();
}
void splitTrackAt (te::AudioTrack& track, te::TimePosition at, juce::UndoManager* undo)
{
    const auto clips = track.getClips();
    for (auto* clip : clips)
    {
        const auto p = clip->getPosition();
        if (p.getStart() >= at || p.getEnd() <= at) continue;
        if (auto* midi = dynamic_cast<te::MidiClip*> (clip)) splitMidiClipAt (*midi, at, undo);
        else track.splitClip (*clip, at);
    }
}
}
Result Session::splitMidiClip (const String& id, double at, String& rightId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before splitting clips.");
    auto* clip = impl->midiClip (id); if (! clip) return Result::fail ("MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    if (! std::isfinite (at) || ! canSplitMidiAt (*clip, te::TimePosition::fromSeconds (at)))
        return Result::fail ("Split inside an unlooped Arrangement MIDI clip, away from its ends.");
    impl->transaction ("Split MIDI clip");
    auto* right = splitMidiClipAt (*clip, te::TimePosition::fromSeconds (at), &impl->edit->getUndoManager());
    if (! right) return Result::fail ("The MIDI clip could not be split.");
    rightId = right->itemID.toString(); impl->changed(); return Result::ok();
}
namespace
{
// Tracks for a time selection, or a reason it cannot be edited.
Result rangeTracks (te::Edit& edit, const juce::StringArray& ids, double start, double end, juce::Array<te::AudioTrack*>& tracks)
{
    if (! std::isfinite (start) || ! std::isfinite (end) || start < 0 || end - start < .001 || end > te::Edit::maximumLength)
        return Result::fail ("Select a time range inside the timeline.");
    for (const auto& id : ids)
    {
        te::AudioTrack* found = nullptr;
        for (auto* t : te::getAudioTracks (edit)) if (t->itemID.toString() == id) found = t;
        if (found == nullptr) return Result::fail ("A selected track no longer exists.");
        if (! isReturn (*found)) tracks.addIfNotAlreadyThere (found);
    }
    if (tracks.isEmpty()) return Result::fail ("Select time on an audio or MIDI track.");
    return Result::ok();
}
}
Result Session::splitRange (const juce::StringArray& trackIds, double start, double end)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before splitting clips.");
    juce::Array<te::AudioTrack*> tracks;
    if (auto r = rangeTracks (*impl->edit, trackIds, start, end, tracks); r.failed()) return r;
    const auto from = te::TimePosition::fromSeconds (start), to = te::TimePosition::fromSeconds (end);
    for (auto* t : tracks) for (const auto at : { from, to }) if (auto r = splittableAt (*t, at); r.failed()) return r;
    impl->transaction ("Split");
    for (auto* t : tracks) for (const auto at : { from, to }) splitTrackAt (*t, at, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::deleteRange (const juce::StringArray& trackIds, double start, double end)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before deleting clips.");
    juce::Array<te::AudioTrack*> tracks;
    if (auto r = rangeTracks (*impl->edit, trackIds, start, end, tracks); r.failed()) return r;
    const auto from = te::TimePosition::fromSeconds (start), to = te::TimePosition::fromSeconds (end);
    for (auto* t : tracks)
    {
        for (const auto at : { from, to }) if (auto r = splittableAt (*t, at); r.failed()) return r;
        for (auto* c : t->getClips())
            if (c->getPosition().getStart() < to && c->getPosition().getEnd() > from)
                if (auto r = impl->unfrozen (t); r.failed()) return r;
    }
    endEditGesture(); closePluginEditors();
    impl->transaction ("Delete selection");
    for (auto* t : tracks)
    {
        for (const auto at : { from, to }) splitTrackAt (*t, at, &impl->edit->getUndoManager());
        const auto clips = t->getClips();
        for (auto* c : clips)
            if (c->getPosition().getStart() >= from - te::TimeDuration::fromSeconds (1e-6) && c->getPosition().getEnd() <= to + te::TimeDuration::fromSeconds (1e-6)) c->removeFromParent();
    }
    impl->changed(); return Result::ok();
}
Result Session::duplicateRange (const juce::StringArray& trackIds, double start, double end)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before duplicating clips.");
    juce::Array<te::AudioTrack*> tracks;
    if (auto r = rangeTracks (*impl->edit, trackIds, start, end, tracks); r.failed()) return r;
    const auto length = end - start;
    if (end + length > te::Edit::maximumLength) return Result::fail ("The copy must fit inside the 48-hour timeline.");
    const auto from = te::TimePosition::fromSeconds (start), to = te::TimePosition::fromSeconds (end), after = te::TimePosition::fromSeconds (end + length);
    const auto delta = te::TimeDuration::fromSeconds (length), epsilon = te::TimeDuration::fromSeconds (1e-6);
    for (auto* t : tracks)
    {
        for (const auto at : { from, to, after }) if (auto r = splittableAt (*t, at); r.failed()) return r;
        for (auto* c : t->getClips())
            if (c->getPosition().getStart() < after && c->getPosition().getEnd() > from)
                if (auto r = impl->unfrozen (t); r.failed()) return r;
    }
    endEditGesture(); closePluginEditors();
    impl->transaction ("Duplicate selection");
    auto* undo = &impl->edit->getUndoManager();
    for (auto* t : tracks)
    {
        for (const auto at : { from, to, after }) splitTrackAt (*t, at, undo);
        // The copy replaces whatever was in the time just after the selection.
        std::vector<te::Clip::Ptr> sources, replaced;
        for (auto* c : t->getClips())
        {
            const auto p = c->getPosition();
            if (p.getStart() >= from - epsilon && p.getEnd() <= to + epsilon) sources.emplace_back (c);
            else if (p.getStart() >= to - epsilon && p.getEnd() <= after + epsilon) replaced.emplace_back (c);
        }
        for (auto& c : replaced) c->removeFromParent();
        for (auto& c : sources)
        {
            const auto p = c->getPosition();
            auto copy = te::ClipCopy::fromClipboardState (c->state, false).withNewItemID (*impl->edit);
            auto state = copy.getState();
            state.setProperty (te::IDs::start, (p.getStart() + delta).inSeconds(), nullptr);
            renewMidiNoteIDs (state);
            te::insertClipCopy (*t, copy);
        }
    }
    impl->changed(); return Result::ok();
}
Result Session::insertTime (double start, double length)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before inserting time.");
    if (! std::isfinite (start) || ! std::isfinite (length) || start < 0 || length <= 0) return Result::fail ("Choose a start and a positive length of time to insert.");
    double end = 0;
    for (auto* track : te::getAudioTracks (*impl->edit))
        for (auto* clip : track->getClips())
        {
            const auto p = clip->getPosition(); end = std::max (end, p.getEnd().inSeconds());
            if (p.getStart().inSeconds() < start && p.getEnd().inSeconds() > start)
            {
                if (track->isFrozen (te::Track::anyFreeze)) return Result::fail ("Unfreeze \"" + track->getName() + "\" before inserting time inside its clips.");
                const auto* wave = dynamic_cast<te::WaveAudioClip*> (clip); const auto* midi = dynamic_cast<te::MidiClip*> (clip);
                if ((wave != nullptr && ! canSplitAt (*wave, start)) || (midi != nullptr && ! canCopyMidiClip (*midi)) || (wave == nullptr && midi == nullptr))
                    return Result::fail ("\"" + clip->getName() + "\" cannot be split at the insertion point. Move it or choose another point.");
            }
        }
    if (end + length > te::Edit::maximumLength) return Result::fail ("The arrangement would extend past the 48-hour timeline.");
    endEditGesture(); closePluginEditors();
    impl->transaction ("Insert time");
    auto* undo = &impl->edit->getUndoManager();
    const auto from = te::TimePosition::fromSeconds (start); const auto delta = te::TimeDuration::fromSeconds (length);
    const auto deltaBeats = impl->edit->tempoSequence.toBeats (from + delta) - impl->edit->tempoSequence.toBeats (from);
    for (auto* track : te::getAudioTracks (*impl->edit))
    {
        const auto clips = track->getClips();
        for (int i = clips.size(); --i >= 0;)
        {
            auto* clip = clips[i]; const auto p = clip->getPosition();
            if (p.getStart() >= from) { clip->setStart (p.getStart() + delta, false, true); continue; }
            if (p.getEnd() <= from) continue;
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
            {
                // Notes from the insertion point on move later; the clip grows by the gap.
                const auto split = impl->edit->tempoSequence.toBeats (from) - midi->getStartBeat();
                clip->setLength (p.getLength() + delta, true);
                const auto notes = midi->getSequence().getNotes(); // Moving notes can reorder the live list.
                for (auto* note : notes)
                    if (note->getStartBeat().inBeats() >= split.inBeats() - 1e-9)
                        note->setStartAndLength (note->getStartBeat() + deltaBeats, note->getLengthBeats(), undo);
                const auto events = midi->getSequence().getControllerEvents();
                for (auto* event : events)
                    if (event->getBeatPosition().inBeats() >= split.inBeats() - 1e-9) event->setBeatPosition (event->getBeatPosition() + deltaBeats, undo);
            }
            else if (auto* right = track->splitClip (*clip, from)) right->setStart (from + delta, false, true);
        }
    }
    for (auto* marker : impl->edit->getMarkerManager().getMarkers())
        if (marker->getPosition().getStart() >= from) marker->setStart (marker->getPosition().getStart() + delta, false, true);
    for (auto* curve : arrangementCurves (*impl->edit)) shiftCurve (*curve, *impl->edit, from, delta, std::nullopt, undo);
    impl->refreshParameterValues();
    impl->changed(); return Result::ok();
}
Result Session::deleteTime (double start, double length)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before deleting time.");
    if (! std::isfinite (start) || ! std::isfinite (length) || start < 0 || length <= 0) return Result::fail ("Choose a start and a positive length of time to delete.");
    const auto end = start + length;
    for (auto* track : te::getAudioTracks (*impl->edit))
        for (auto* clip : track->getClips())
        {
            const auto p = clip->getPosition(); const auto cs = p.getStart().inSeconds(), ce = p.getEnd().inSeconds();
            if (ce <= start || cs >= end) continue;
            if (track->isFrozen (te::Track::anyFreeze)) return Result::fail ("Unfreeze \"" + track->getName() + "\" before deleting time from its clips.");
            const auto* wave = dynamic_cast<te::WaveAudioClip*> (clip); const auto* midi = dynamic_cast<te::MidiClip*> (clip);
            const bool ok = midi != nullptr ? canCopyMidiClip (*midi)
                          : wave != nullptr && (cs >= start || canSplitAt (*wave, start)) && (ce <= end || canSplitAt (*wave, end));
            if (! ok) return Result::fail ("\"" + clip->getName() + "\" cannot be cut at the edges of the deleted time.");
        }
    endEditGesture(); closePluginEditors();
    impl->transaction ("Delete time");
    auto* undo = &impl->edit->getUndoManager();
    const auto from = te::TimePosition::fromSeconds (start), to = te::TimePosition::fromSeconds (end);
    const auto delta = te::TimeDuration::fromSeconds (length);
    const auto& tempo = impl->edit->tempoSequence;
    for (auto* track : te::getAudioTracks (*impl->edit))
    {
        const auto clips = track->getClips();
        for (auto* clip : clips)
        {
            const auto p = clip->getPosition();
            if (p.getEnd() <= from) continue;
            if (p.getStart() >= to) { clip->setStart (p.getStart() - delta, false, true); continue; }
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
            {
                // Notes inside the deleted time go, later notes move earlier, and a note crossing the start ends there.
                const auto clipStart = midi->getStartBeat(), cut = tempo.toBeats (from), cutEnd = tempo.toBeats (to);
                const auto newStart = p.getStart() < from ? clipStart : cut;
                const auto notes = midi->getSequence().getNotes(); // Removing and moving notes changes the live list.
                for (auto* note : notes)
                {
                    const auto noteStart = clipStart + toDuration (note->getStartBeat()), noteEnd = noteStart + note->getLengthBeats();
                    if (noteStart >= cut && noteStart < cutEnd) { midi->getSequence().removeNote (*note, undo); continue; }
                    auto absolute = noteStart; auto noteLength = note->getLengthBeats();
                    if (noteStart >= cutEnd) absolute = noteStart - (cutEnd - cut);
                    else if (noteEnd > cut) noteLength = cut - noteStart;
                    note->setStartAndLength (te::BeatPosition::fromBeats ((absolute - newStart).inBeats()), noteLength, undo);
                }
                // Controller values set inside the deleted time take effect at the cut, so later notes keep them.
                std::map<int, te::MidiControllerEvent*> lastInside;
                const auto events = midi->getSequence().getControllerEvents();
                for (auto* event : events)
                {
                    const auto at = clipStart + toDuration (event->getBeatPosition());
                    if (at >= cut && at < cutEnd)
                    {
                        if (auto& last = lastInside[event->getType()]; last == nullptr || last->getBeatPosition() <= event->getBeatPosition())
                        { if (last != nullptr) midi->getSequence().removeControllerEvent (*last, undo); last = event; }
                        else midi->getSequence().removeControllerEvent (*event, undo);
                        continue;
                    }
                    const auto absolute = at >= cutEnd ? at - (cutEnd - cut) : at;
                    event->setBeatPosition (te::BeatPosition::fromBeats ((absolute - newStart).inBeats()), undo);
                }
                for (auto& [type, event] : lastInside)
                {
                    bool replaced = false;
                    for (auto* other : midi->getSequence().getControllerEvents())
                        replaced = replaced || (other != event && other->getType() == type && std::abs ((other->getBeatPosition() - te::BeatPosition::fromBeats ((cut - newStart).inBeats())).inBeats()) < 1e-9);
                    if (replaced) midi->getSequence().removeControllerEvent (*event, undo);
                    else event->setBeatPosition (te::BeatPosition::fromBeats ((cut - newStart).inBeats()), undo);
                }
                const auto remaining = p.getLength() - (std::min (p.getEnd(), to) - std::max (p.getStart(), from));
                if (remaining.inSeconds() < .001) { clip->removeFromParent(); continue; }
                clip->setPosition ({ { p.getStart() < from ? p.getStart() : from, remaining }, te::TimeDuration() });
                continue;
            }
            te::Clip* middle = clip;
            if (p.getStart() < from) middle = track->splitClip (*clip, from);
            if (middle == nullptr) continue;
            te::Clip* after = nullptr;
            if (middle->getPosition().getEnd() > to) after = track->splitClip (*middle, to);
            middle->removeFromParent();
            if (after != nullptr) after->setStart (after->getPosition().getStart() - delta, false, true);
        }
    }
    for (auto* marker : impl->edit->getMarkerManager().getMarkers())
    {
        const auto at = marker->getPosition().getStart();
        if (at >= from && at < to) marker->removeFromParent();
        else if (at >= to) marker->setStart (at - delta, false, true);
    }
    for (auto* curve : arrangementCurves (*impl->edit)) shiftCurve (*curve, *impl->edit, to, te::TimeDuration() - delta, te::TimeRange (from, to), undo);
    impl->refreshParameterValues();
    impl->changed(); return Result::ok();
}
Result Session::duplicateAudioClip (const String& id, String& duplicateId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before duplicating clips.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (clip->getClipSlot()) return Result::fail ("Duplicate an arrangement audio clip; a Session slot holds one clip.");
    const auto end = clip->getPosition().getEnd();
    if (end.inSeconds() + clip->getPosition().getLength().inSeconds() > te::Edit::maximumLength)
        return Result::fail ("The duplicate must fit inside the 48-hour timeline.");
    impl->transaction ("Duplicate audio clip");
    auto second = te::duplicateClip (*clip);
    if (! second) return Result::fail ("The audio clip could not be duplicated.");
    second->setStart (end, false, true); duplicateId = second->itemID.toString(); impl->changed(); return Result::ok();
}
Result Session::copyMidiClip (const String& id, bool cut)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (cut && impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before cutting clips.");
    auto* clip=impl->midiClip(id);
    if (!clip || !canCopyMidiClip(*clip)) return Result::fail("Copy a single-take, unlooped Arrangement MIDI clip with zero source offset.");
    if(cut && clip->getClipTrack()->isFrozen(te::Track::anyFreeze)) return Result::fail("Unfreeze the source track before cutting clips.");
    const auto beats=clip->getLengthInBeats().inBeats();
    if(!std::isfinite(beats)||beats<=0)return Result::fail("The MIDI clip has an invalid beat duration.");
    auto copied=clip->state.createCopy();
    if(cut){impl->transaction("Cut MIDI clip");clip->removeFromParent();impl->changed();}
    impl->midiClipboard=std::move(copied);impl->midiClipboardBeats=beats;
    impl->audioClipboard={};impl->audioClipboardMedia.reset();return Result::ok();
}
bool Session::hasMidiClipClipboard() const {return impl->midiClipboard.isValid();}
Result Session::pasteMidiClip (const String& trackId, double start, String& pastedId)
{
    if (auto frozen = impl->unfrozen (impl->track (trackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before pasting clips.");
    auto* track=impl->track(trackId);
    if(!track||isReturn(*track)||!static_cast<bool>(track->state.getProperty(midiTrackProperty))||track->isFrozen(te::Track::anyFreeze))
        return Result::fail("Paste MIDI onto an unfrozen MIDI track.");
    if(!hasMidiClipClipboard())return Result::fail("Copy a MIDI clip before pasting.");
    if(!std::isfinite(start)||start<0||start>=te::Edit::maximumLength)return Result::fail("Paste position is outside the supported timeline.");
    const auto first=impl->edit->tempoSequence.toBeats(te::TimePosition::fromSeconds(start));
    const auto end=impl->edit->tempoSequence.toTime(first+te::BeatDuration::fromBeats(impl->midiClipboardBeats)).inSeconds();
    if(end>te::Edit::maximumLength||end<=start)return Result::fail("The complete MIDI clip must fit inside the 48-hour timeline.");
    auto copy=te::ClipCopy::fromClipboardState(impl->midiClipboard,false).withNewItemID(*impl->edit);
    auto state=copy.getState();state.setProperty(te::IDs::start,start,nullptr);state.setProperty(te::IDs::length,end-start,nullptr);
    state.setProperty(te::IDs::offset,0,nullptr);state.setProperty(te::IDs::sync,te::Clip::syncBarsBeats,nullptr);renewMidiNoteIDs(state);
    impl->transaction("Paste MIDI clip");
    if(auto* clip=te::insertClipCopy(*track,copy)){pastedId=clip->itemID.toString();impl->changed();return Result::ok();}
    return Result::fail("The MIDI clip could not be pasted.");
}
Result Session::duplicateMidiClip (const String& id, String& duplicateId)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before duplicating clips.");
    auto* clip=impl->midiClip(id);
    if(!clip||!canCopyMidiClip(*clip)||clip->getClipTrack()->isFrozen(te::Track::anyFreeze))
        return Result::fail("Duplicate a single-take, unlooped Arrangement MIDI clip on an unfrozen track.");
    const auto start=clip->getPosition().getEnd();
    const auto end=impl->edit->tempoSequence.toTime(clip->getEndBeat()+clip->getLengthInBeats());
    if(end.inSeconds()>te::Edit::maximumLength)return Result::fail("The duplicate would exceed the 48-hour timeline.");
    auto copy=te::ClipCopy::fromClipboardState(clip->state,false).withNewItemID(*impl->edit);auto state=copy.getState();
    state.setProperty(te::IDs::start,start.inSeconds(),nullptr);state.setProperty(te::IDs::length,(end-start).inSeconds(),nullptr);renewMidiNoteIDs(state);
    impl->transaction("Duplicate MIDI clip");
    if(auto* duplicate=te::insertClipCopy(*clip->getClipTrack(),copy)){duplicateId=duplicate->itemID.toString();impl->changed();return Result::ok();}
    return Result::fail("The MIDI clip could not be duplicated.");
}
Result Session::copyAudioClip (const String& id, bool cut)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (cut && impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before cutting clips.");
    auto* clip = impl->audioClip (id);
    if (! clip || ! canEditAudioRange (*clip))
        return Result::fail ("Copy an unwarped, unlooped arrangement audio clip.");
    if (clip->getNumTakes (true) > 1 || clip->isCurrentTakeComp())
        return Result::fail ("Audio clipboard currently supports a single source, without take comping.");
    // A detached state alone would still depend on the original project's media.
    // Retain an independent file before publishing the clipboard or removing a clip.
    const auto source = clip->getSourceFileReference().getFile();
    if (! source.existsAsFile()) return Result::fail ("The audio clip source is missing.");
    auto media = std::make_unique<juce::TemporaryFile> (source.getFileExtension());
    if (! source.copyFileTo (media->getFile())) return Result::fail ("Could not retain audio for the clipboard.");
    auto copied = clip->state.createCopy();
    // A one-take clip's selected source is already resolved above. Do not retain
    // an alternate source reference back into the old project.
    copied.removeChild (copied.getChildWithName (te::IDs::TAKES), nullptr);
    copied.setProperty (te::IDs::source, media->getFile().getFullPathName(), nullptr);
    if (cut)
    {
        impl->transaction ("Cut audio clip");
        clip->removeFromParent();
        impl->changed();
    }
    impl->midiClipboard = {}; impl->midiClipboardBeats = 0;
    impl->audioClipboard = std::move (copied);
    impl->audioClipboardMedia = std::move (media);
    return Result::ok();
}
bool Session::hasAudioClipClipboard() const
{
    return impl->audioClipboard.isValid() && impl->audioClipboardMedia
        && impl->audioClipboardMedia->getFile().existsAsFile();
}
Result Session::pasteAudioClip (const String& trackId, double start, String& pastedId)
{
    if (auto frozen = impl->unfrozen (impl->track (trackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before pasting clips.");
    auto* track = impl->track (trackId);
    if (! track || isReturn (*track) || static_cast<bool> (track->state.getProperty (midiTrackProperty)))
        return Result::fail ("Paste audio onto an audio track.");
    if (! hasAudioClipClipboard()) return Result::fail ("Copy an audio clip before pasting.");
    const double length = impl->audioClipboard[te::IDs::length];
    if (! std::isfinite (start) || start < 0 || start + length > te::Edit::maximumLength)
        return Result::fail ("Paste position is outside the supported timeline.");
    const auto assets = impl->assets();
    if (auto r = assets.createDirectory(); r.failed()) return r;
    const auto source = impl->audioClipboardMedia->getFile();
    const auto name = File::createLegalFileName (impl->audioClipboard[te::IDs::name].toString());
    const auto target = assets.getNonexistentChildFile (name.isEmpty() ? "Pasted audio" : name, source.getFileExtension(), true);
    if (! source.copyFileTo (target)) return Result::fail ("Could not collect clipboard audio into this project.");
    auto copy = te::ClipCopy::fromClipboardState (impl->audioClipboard, false).withNewItemID (*impl->edit);
    auto state = copy.getState();
    state.setProperty (te::IDs::source, target.getFullPathName(), nullptr);
    state.setProperty (te::IDs::start, start, nullptr);
    impl->transaction ("Paste audio clip");
    if (auto* clip = te::insertClipCopy (*track, copy))
    {
        pastedId = clip->itemID.toString(); impl->changed(); return Result::ok();
    }
    target.deleteFile();
    return Result::fail ("The audio clip could not be pasted.");
}
Result Session::moveMidiClip (const String& id, const String& destinationId, double start)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id), impl->track (destinationId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before moving MIDI clips.");
    auto* clip = impl->midiClip (id); auto* destination = impl->track (destinationId);
    if (! clip || clip->getClipSlot() || clip->isGrouped()) return Result::fail ("Move an ungrouped Arrangement MIDI clip.");
    if (! destination || isReturn (*destination) || ! static_cast<bool> (destination->state.getProperty (midiTrackProperty)))
        return Result::fail ("Move MIDI onto a MIDI track.");
    if (destination->isFrozen (te::Track::anyFreeze) || clip->getClipTrack()->isFrozen (te::Track::anyFreeze))
        return Result::fail ("Unfreeze the source and destination tracks before moving MIDI.");
    if (! std::isfinite (start) || start < 0 || start + clip->getPosition().getLength().inSeconds() > te::Edit::maximumLength)
        return Result::fail ("The complete clip must fit inside the 48-hour timeline.");
    if (clip->getClipTrack() == destination && clip->getPosition().getStart().inSeconds() == start) return Result::ok();
    const te::Clip::Ptr retained (clip);
    impl->transaction ("Move MIDI clip");
    if (clip->getClipTrack() != destination && ! clip->moveTo (*destination)) return Result::fail ("Could not move the MIDI clip to that track.");
    clip->setStart (te::TimePosition::fromSeconds (start), false, true);
    impl->changed(); return Result::ok();
}
Result Session::moveAudioClip (const String& id, const String& destinationId, double start)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id), impl->track (destinationId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before moving audio clips.");
    auto* clip = impl->audioClip (id); auto* destination = impl->track (destinationId);
    if (! clip || clip->getClipSlot() || clip->isGrouped()) return Result::fail ("Move an ungrouped Arrangement audio clip.");
    if (! destination || isReturn (*destination) || static_cast<bool> (destination->state.getProperty (midiTrackProperty)))
        return Result::fail ("Move audio onto an audio track.");
    if (destination->isFrozen (te::Track::anyFreeze) || clip->getClipTrack()->isFrozen (te::Track::anyFreeze))
        return Result::fail ("Unfreeze the source and destination tracks before moving audio.");
    if (! std::isfinite (start) || start < 0 || start + clip->getPosition().getLength().inSeconds() > te::Edit::maximumLength)
        return Result::fail ("The complete clip must fit inside the 48-hour timeline.");
    if (clip->getClipTrack() == destination && clip->getPosition().getStart().inSeconds() == start) return Result::ok();
    const te::Clip::Ptr retained (clip);
    impl->transaction ("Move audio clip");
    if (clip->getClipTrack() != destination && ! clip->moveTo (*destination)) return Result::fail ("Could not move the audio clip to that track.");
    clip->setStart (te::TimePosition::fromSeconds (start), false, true);
    impl->changed(); return Result::ok();
}
Result Session::moveClip (int t, int c, double start)
{
    if (auto frozen = impl->unfrozen (impl->track (t)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->clip (t, c); if (! clip) return Result::fail ("Clip not found.");
    if (! std::isfinite (start) || start < 0 || start + clip->getPosition().getLength().inSeconds() > te::Edit::maximumLength)
        return Result::fail ("The complete clip must fit inside the 48-hour timeline.");
    impl->transaction ("Move clip"); clip->setStart (te::TimePosition::fromSeconds (start), false, true); impl->changed(); return Result::ok();
}
Result Session::removeClip (int t, int c)
{
    if (auto frozen = impl->unfrozen (impl->track (t)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->clip (t, c); if (! clip) return Result::fail ("Clip not found.");
    impl->transaction ("Remove clip"); clip->removeFromParent(); impl->changed(); return Result::ok();
}
Result Session::setClipFollowAction (const String& id, const FollowSettings& settings)
{
    if (impl->exporting || impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording or finish exporting before changing clip follow actions.");
    if (! validFollowSettings (settings)) return Result::fail ("Choose a follow action and a duration from 1/960 to 4096 beats, or Off with zero beats.");
    te::Clip* found = nullptr;
    for (auto* track : te::getAudioTracks (*impl->edit))
        for (auto* clip : allTrackClips (*track))
            if (clip->itemID.toString() == id)
            {
                if (found) return Result::fail ("Clip identity is ambiguous.");
                found = clip;
            }
    if (! found || ! found->getClipSlot() || (! dynamic_cast<te::MidiClip*> (found) && ! dynamic_cast<te::WaveAudioClip*> (found)))
        return Result::fail ("Select an audio or MIDI clip in Session.");
    if (found->getClipTrack()->isFrozen (te::Track::anyFreeze)) return Result::fail ("Unfreeze the track before changing follow actions.");
    if (clipFollowSettings (found->state) == settings) return Result::ok();
    impl->transaction ("Change clip follow action");
    // The native object caches this container. Retain its empty scaffolding
    // through history so a later edit cannot target a detached cached tree.
    found->state.getOrCreateChildWithName (te::IDs::FOLLOWACTIONS, nullptr);
    auto* actions = found->getFollowActions();
    found->followActionDurationType = te::Clip::FollowActionDurationType::beats;
    found->followActionBeats = te::BeatDuration::fromBeats (settings.afterBeats);
    while (! actions->getActions().empty()) actions->removeAction (*actions->getActions().back());
    if (settings.action != FollowAction::off)
    {
        auto& action = actions->addAction();
        action.action = nativeFollowActions[(size_t) settings.action];
        const bool single = settings.actionB == FollowAction::off && settings.chanceA == 1;
        action.weight = single ? 1.0 : settings.chanceA;
        if (! single)
        {
            auto& second = actions->addAction();
            second.action = nativeFollowActions[(size_t) settings.actionB];
            second.weight = 1.0 - settings.chanceA;
        }
    }
    impl->edit->restartPlayback(); impl->edit->dispatchPendingUpdatesSynchronously();
    impl->changed(); return Result::ok();
}
Result Session::setClipActive(const String& id,bool active)
{
    if (impl->exporting) return Result::fail("Finish or cancel the current export first.");
    te::Clip* found=nullptr;
    for (auto* track:te::getAudioTracks(*impl->edit)) for (auto* clip:allTrackClips(*track))
        if (clip->itemID.toString()==id && (dynamic_cast<te::WaveAudioClip*>(clip) || dynamic_cast<te::MidiClip*>(clip)))
        {
            if (found) return Result::fail("Clip identity is ambiguous.");
            found=clip;
        }
    if (!found) return Result::fail("Clip not found.");
    if (found->isMuted()==!active) return Result::ok();
    if(found->getClipTrack()->isFrozen(te::Track::anyFreeze)) return Result::fail("Unfreeze the track before changing clip activation.");
    impl->transaction(active?"Activate clip":"Deactivate clip");found->setMuted(!active);
    // Prepared stretch buffers can retain the previous gain. Rebuild through
    // the native message-thread graph path before the next playback block.
    impl->edit->restartPlayback();impl->edit->dispatchPendingUpdatesSynchronously();
    impl->changed();return Result::ok();
}
Result Session::renameClip(const String& id,const String& name)
{
    if(impl->exporting) return Result::fail("Finish or cancel the current export first.");
    const auto trimmed=name.trim();if(trimmed.isEmpty())return Result::fail("Enter a clip name.");
    te::Clip* found=nullptr;
    for(auto* track:te::getAudioTracks(*impl->edit))for(auto* clip:allTrackClips(*track))
        if(clip->itemID.toString()==id&&(dynamic_cast<te::WaveAudioClip*>(clip)||dynamic_cast<te::MidiClip*>(clip)))
        {if(found)return Result::fail("Clip identity is ambiguous.");found=clip;}
    if(!found)return Result::fail("Clip not found.");
    if(found->getName()==trimmed)return Result::ok();
    impl->transaction("Rename clip");
    // Clip::setName can also rename source media under a native global preference.
    // This command changes only the project-owned display name.
    found->state.setProperty(te::IDs::name,trimmed,&impl->edit->getUndoManager());
    impl->changed();return Result::ok();
}
Result Session::renameTrack (int index, const String& name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (name.trim().isEmpty()) return Result::fail ("Enter a track name.");
    impl->transaction ("Rename track"); t->setName (name.trim()); impl->changed(); return Result::ok();
}
float Session::crossfadeGain (CrossfadeAssign side, float position)
{
    const auto x = juce::jlimit (-1.0f, 1.0f, std::isfinite (position) ? position : 0.0f);
    const auto away = side == CrossfadeAssign::a ? x : side == CrossfadeAssign::b ? -x : 0.0f;
    if (away <= 0) return 1.0f;
    if (away >= 1) return 0.0f;
    return std::cos (away * juce::MathConstants<float>::halfPi);
}
Result Session::setTrackCrossfade (const String& trackId, CrossfadeAssign side)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackId); if (! track) return Result::fail ("Track not found.");
    if (static_cast<int> (side) < 0 || static_cast<int> (side) > 2) return Result::fail ("Unknown crossfader side.");
    if (static_cast<int> (track->state.getProperty (crossfadeProperty, 0)) == static_cast<int> (side)) return Result::ok();
    impl->transaction ("Crossfade assign");
    track->state.setProperty (crossfadeProperty, static_cast<int> (side), &impl->edit->getUndoManager());
    impl->refreshCrossfade(); impl->changed(); return Result::ok();
}
Result Session::setTrackDelay (const String& trackId, double milliseconds)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before changing track delay.");
    auto* track = impl->track (trackId);
    if (! track) return Result::fail ("Track not found.");
    if (auto frozen = impl->unfrozen (track); frozen.failed()) return frozen;
    if (! std::isfinite (milliseconds) || std::abs (milliseconds) > maximumTrackDelayMs) return Result::fail ("Track delay runs from -1000 to +1000 ms.");
    auto* delay = track->pluginList.findFirstPluginOfType<TrackDelayPlugin>();
    if ((delay ? delay->milliseconds.get() : 0.0) == milliseconds) return Result::ok();
    impl->transaction ("Track delay");
    if (delay == nullptr)
    {
        // The delay follows the devices and precedes sends and the mixer, so sends hear it too.
        auto plugin = impl->edit->getPluginCache().createNewPlugin (TrackDelayPlugin::xmlTypeName, {});
        if (plugin == nullptr) return Result::fail ("Could not create the track delay.");
        int index = track->pluginList.size();
        for (int i = 0; i < track->pluginList.size(); ++i)
            if (dynamic_cast<te::VolumeAndPanPlugin*> (track->pluginList[i]) || dynamic_cast<te::AuxSendPlugin*> (track->pluginList[i])) { index = i; break; }
        track->pluginList.insertPlugin (plugin, index, nullptr);
        delay = dynamic_cast<TrackDelayPlugin*> (plugin.get());
    }
    delay->milliseconds = milliseconds;
    impl->changed(); return Result::ok();
}
Result Session::setCrossfader (float position)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! std::isfinite (position)) return Result::fail ("Invalid crossfader position.");
    position = juce::jlimit (-1.0f, 1.0f, position);
    if (static_cast<float> (impl->edit->state.getProperty (crossfaderProperty, 0.0f)) == position) return Result::ok();
    impl->transaction ("Crossfader");
    impl->edit->state.setProperty (crossfaderProperty, position, &impl->edit->getUndoManager());
    impl->refreshCrossfade(); impl->changed(); return Result::ok();
}
Result Session::setMasterGain(float value)
{
    if(impl->exporting) return Result::fail("Finish or cancel the current export first.");
    if(!std::isfinite(value)) return Result::fail("Enter a finite master level.");
    value=juce::jlimit(-100.0f,6.0f,value);
    auto master=impl->edit->getMasterVolumePlugin();
    if (master->volParam->hasAutomationPoints()) return Result::fail ("Clear the master volume automation lane before changing its manual fader.");
    if(master->getSliderPos()==te::decibelsToVolumeFaderPosition(value)) return Result::ok();
    if (! master->state.hasProperty (manualVolumeProperty))
        master->state.setProperty (manualVolumeProperty, master->volParam->getCurrentExplicitValue(), nullptr);
    impl->transaction("Master volume"); master->setVolumeDb(value);
    master->state.setProperty (manualVolumeProperty, te::decibelsToVolumeFaderPosition (value), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setTrackGain (int index, float value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (! std::isfinite (value)) return Result::fail ("Invalid gain.");
    auto* volume = t->getVolumePlugin();
    if (volume->volParam->hasAutomationPoints()) return Result::fail ("Clear the volume automation lane before changing its manual fader.");
    value = juce::jlimit (-100.0f, 6.0f, value);
    if (! volume->state.hasProperty (manualVolumeProperty))
        volume->state.setProperty (manualVolumeProperty, volume->volParam->getCurrentExplicitValue(), nullptr);
    impl->transaction ("Track gain"); volume->setVolumeDb (value);
    volume->state.setProperty (manualVolumeProperty, te::decibelsToVolumeFaderPosition (value), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setTrackPan (int index, float value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (! std::isfinite (value)) return Result::fail ("Invalid pan.");
    auto* volume = t->getVolumePlugin();
    if (volume->panParam->hasAutomationPoints()) return Result::fail ("Clear the pan automation lane before changing its manual fader.");
    value = juce::jlimit (-1.0f, 1.0f, value);
    if (! volume->state.hasProperty (manualPanProperty))
        volume->state.setProperty (manualPanProperty, volume->panParam->getCurrentExplicitValue(), nullptr);
    impl->transaction ("Track pan"); volume->setPan (value);
    volume->state.setProperty (manualPanProperty, value, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
float Session::automationValueToProportion (AutomationTarget target, float value)
{
    if (! std::isfinite (value)) return 0;
    return target == AutomationTarget::volume ? te::decibelsToVolumeFaderPosition (juce::jlimit (-100.0f, 6.0f, value))
        : (juce::jlimit (-1.0f, 1.0f, value) + 1) * .5f;
}
float Session::automationProportionToValue (AutomationTarget target, float proportion)
{
    if (! std::isfinite (proportion)) proportion = 0;
    proportion = juce::jlimit (0.0f, 1.0f, proportion);
    return target == AutomationTarget::volume ? juce::jlimit (-100.0f, 6.0f, te::volumeFaderPositionToDB (proportion))
        : proportion * 2 - 1;
}
Result Session::setTrackAutomation (const String& trackId, AutomationTarget target, const AutomationLane& lane)
{
    if (impl->exporting || impl->edit->getTransport().isRecording())
        return Result::fail ("Stop recording or finish exporting before editing automation.");
    auto* track = impl->track (trackId);
    if (! track || ! track->getVolumePlugin()) return Result::fail ("Automation track not found.");
    if (track->isFrozen (te::Track::anyFreeze)) return Result::fail ("Unfreeze the track before editing automation.");
    return impl->setMixerAutomation (*track->getVolumePlugin(), target, lane);
}
float Session::deviceAutomationValueToProportion (EffectKind kind, int parameter, float value) { return effectValueToProportion (kind, parameter, value); }
float Session::deviceAutomationProportionToValue (EffectKind kind, int parameter, float proportion) { return effectProportionToValue (kind, parameter, proportion); }
Result Session::setDeviceAutomation (const String& pluginId, int parameter, const AutomationLane& lane)
{
    if (auto frozen = impl->unfrozen (impl->trackOfPlugin (pluginId)); frozen.failed()) return frozen;
    if (impl->exporting || impl->edit->getTransport().isRecording())
        return Result::fail ("Stop recording or finish exporting before editing automation.");
    auto* device = dynamic_cast<NativeEffectPlugin*> (impl->device (pluginId).plugin);
    if (device == nullptr) return Result::fail ("Native device not found.");
    auto* control = device->control (parameter);
    if (control == nullptr) return Result::fail ("That device has no such control.");
    for (const auto& m : macroMappings (device->state))
        if (m.parameter == parameter && ! lane.points.empty()) return Result::fail ("This control follows Macro " + String (m.macro + 1) + ". Unmap it before drawing automation.");
    const auto& field = effectParameters (device->getKind())[(size_t) parameter];
    if (lane.points.size() > 4096) return Result::fail ("A lane can contain at most 4096 points.");
    double previousBeat = -1, previousTime = -1;
    for (const auto& point : lane.points)
    {
        const double time = std::isfinite (point.beat) ? impl->edit->tempoSequence.toTime (te::BeatPosition::fromBeats (point.beat)).inSeconds() : -1;
        if (! std::isfinite (point.beat) || point.beat < 0 || point.beat > 1000000 || point.beat <= previousBeat || (previousBeat >= 0 && point.beat - previousBeat < 0.999e-6)
            || ! std::isfinite (point.value) || point.value < field.minimum || point.value > field.maximum || ! std::isfinite (time) || time <= previousTime)
            return Result::fail ("Automation requires up to 4096 ordered beat points within the control's range.");
        previousBeat = point.beat; previousTime = time;
    }
    auto& curve = control->getCurve();
    const auto kind = device->getKind();
    bool same = lane.enabled == ! curve.bypass.get() && lane.points.size() == static_cast<size_t> (curve.getNumPoints());
    for (size_t i = 0; same && i < lane.points.size(); ++i)
        same = std::abs (impl->edit->tempoSequence.toBeats (curve.getPointTime ((int) i)).inBeats() - lane.points[i].beat) < 1e-9
            && std::abs (curve.getPointValue ((int) i) - effectValueToProportion (kind, parameter, lane.points[i].value)) < 1e-6f;
    if (same) return Result::ok();
    impl->transaction ("Edit " + String (field.label) + " automation");
    auto* undo = &impl->edit->getUndoManager();
    device->remapOnTempoChange = true;
    // Keep the curve container attached across Undo; the parameter caches that exact tree.
    if (! curve.state.getParent().isValid()) device->state.addChild (curve.state, -1, nullptr);
    curve.clear (undo);
    curve.state.setProperty (te::IDs::bypass, ! lane.enabled, undo);
    for (const auto& point : lane.points)
        curve.addPoint (te::EditPosition (te::BeatPosition::fromBeats (point.beat)), effectValueToProportion (kind, parameter, point.value), 0, undo);
    device->refreshAutomation();
    impl->edit->dispatchPendingUpdatesSynchronously();
    impl->changed(); return Result::ok();
}
Result Session::setMasterVolumeAutomation (const AutomationLane& lane)
{
    return impl->setMixerAutomation (*impl->edit->getMasterVolumePlugin(), AutomationTarget::volume, lane);
}
Result Session::setTrackMute (int index, bool value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    impl->transaction ("Track mute");
    t->state.setProperty (te::IDs::mute, value, &impl->edit->getUndoManager());
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::setTrackSolo (int index, bool value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    impl->transaction ("Track solo");
    t->state.setProperty (te::IDs::solo, value, &impl->edit->getUndoManager());
    impl->refreshRouting(); impl->changed(); return Result::ok();
}

juce::StringArray Session::inputNames (bool midi) const
{
    juce::StringArray names;
    if (midi)
    {
        for (auto device : impl->engine.getDeviceManager().getMidiInDevices())
            if (!device->isTrackDevice()) names.add (device->getName());
    }
    else for (auto* device : impl->engine.getDeviceManager().getWaveInputDevices()) names.add (device->getName());
    return names;
}
Result Session::setTrackInput (int index, const String& name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (isReturn (*t)) return Result::fail ("Return tracks cannot receive recording inputs.");
    // Enabling a device rebuilds the device lists, which discards active takes, and a recording
    // track must keep the input its Session take stops on. Session takes do not set the transport
    // recording, so they are checked separately.
    if (impl->edit->getTransport().isRecording() || ! impl->slotRecordings.empty()) return Result::fail ("Stop recording before changing inputs.");
    const bool midi = t->state.getProperty (midiTrackProperty, false);
    // Validate the typed name before enabling anything. Re-query instances after
    // enabling because the native device list and playback graph may rebuild.
    if (name.isNotEmpty() && !inputNames (midi).contains (name)) return Result::fail ("That input is not available for this track type.");
    const auto monitored=impl->monitoredMidiTracks();
    const bool playing = impl->edit->getTransport().isPlaying();
    if (midi)
    {
        auto& devices = impl->engine.getDeviceManager();
        bool changed = false;
        for (auto device : devices.getMidiInDevices())
            if (!device->isTrackDevice() && device->getName() == name)
            {
                if (auto* aggregate = dynamic_cast<te::VirtualMidiInputDevice*> (device.get()))
                    if (aggregate->useAllInputs)
                        for (auto source : devices.getMidiInDevices())
                            if (source->getDeviceType() == te::InputDevice::physicalMidiDevice && !source->isEnabled())
                            { source->setEnabled (true); changed = true; }
                if (!device->isEnabled()) { device->setEnabled (true); changed = true; }
                break;
            }
        if (changed) devices.rescanMidiDeviceListSynchronously();
    }
    else for (auto* device : impl->engine.getDeviceManager().getWaveInputDevices())
        if (device->getName() == name)
        { if (!device->isEnabled()) device->setEnabled (true); break; }
    impl->engine.getDeviceManager().dispatchPendingUpdates(); impl->refreshInputs(); impl->resumePlayback (playing);
    te::InputDeviceInstance* selected = nullptr;
    for (auto* d : impl->edit->getAllInputDevices())
        if (d->getInputDevice().isMidi() == midi && !d->getInputDevice().isTrackDevice() && d->getInputDevice().getName() == name) selected = d;
    if (name.isNotEmpty() && selected == nullptr) return Result::fail ("The selected input could not be opened.");
    impl->transaction ("Assign input");
    t->state.setProperty (monitorProperty, static_cast<int> (MonitorMode::off), &impl->edit->getUndoManager());
    for (auto* d : impl->edit->getAllInputDevices())
        if (d->getTargets().contains (t->itemID)) { [[maybe_unused]] auto r = d->removeTarget (t->itemID, &impl->edit->getUndoManager()); }
    if (selected)
    {
        auto result = selected->setTarget (t->itemID, true, &impl->edit->getUndoManager(), 0);
        if (! result) return Result::fail (result.error());
        selected->setRecordingEnabled (t->itemID, false);
        selected->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::off);
    }
    impl->finishInputChange(monitored);impl->changed(); return Result::ok();
}
Result Session::setTrackArmed (int index, bool value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (isReturn (*t)) return Result::fail ("Return tracks cannot receive recording inputs.");
    auto* input = impl->input (*t); if (! input) return Result::fail ("Select an input before arming this track.");
    if (input->isRecordingEnabled (t->itemID) == value) return Result::ok();
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before changing record arms.");
    auto* destination = te::getDestination (*input, *t, 0);
    if (!destination) return Result::fail ("Input destination is unavailable.");
    const auto monitored=impl->monitoredMidiTracks();
    impl->transaction ("Record arm");
    destination->recordEnabled.setValue (value, &impl->edit->getUndoManager());
    impl->finishInputChange(monitored);impl->changed(); return Result::ok();
}
Result Session::setTrackMonitor (int index, MonitorMode mode)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (isReturn (*t)) return Result::fail ("Return tracks cannot receive recording inputs.");
    auto* input = impl->input (*t); if (! input) return Result::fail ("Select an input before enabling monitoring.");
    if (input->getInputDevice().getMonitorMode() == static_cast<te::InputDevice::MonitorMode> (mode)) return Result::ok();
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before changing monitoring.");
    const auto monitored=impl->monitoredMidiTracks();
    impl->transaction ("Input monitoring");
    input->getInputDevice().setMonitorMode (static_cast<te::InputDevice::MonitorMode> (mode));
    t->state.setProperty (monitorProperty, static_cast<int> (mode), &impl->edit->getUndoManager());
    impl->edit->restartPlayback(); impl->finishInputChange(monitored);impl->changed(); return Result::ok();
}
Result Session::play()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio output in Audio Settings.");
    impl->edit->getTransport().play (false); sendChangeMessage(); return Result::ok();
}
void Session::stop()
{
    if (impl->exporting) return;
    const bool recording = impl->edit->getTransport().isRecording();
    // Session recordings end where the transport stops and keep their takes.
    while (! impl->slotRecordings.empty())
    {
        endEditGesture();
        impl->finishSlotRecording (impl->slotRecordings.front(), false, true);
    }
    if (recording) { endEditGesture(); impl->transaction ("Record take"); }
    te::Edit::UndoTransactionInhibitor stopping (*impl->edit);
    impl->cancelLaunches();
    impl->edit->getTransport().stop (false, false);
    if (recording)
    {
        const auto grid=recordGrid(snapshot().recordQuantisation);
        for (auto* track : te::getAudioTracks (*impl->edit)) for (auto* clip : allTrackClips (*track))
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
            {
                const auto range=impl->edit->tempoSequence.toBeats(midi->getPosition().time);
                const auto notes=midi->getSequence().getNotes(); // Timing changes may reorder native notes.
                for (auto* note : notes)
                    if (!note->state.hasProperty (midiNoteIDProperty))
                    {
                        auto* undo=&impl->edit->getUndoManager();
                        if(grid>0)
                        {
                            const auto length=note->getLengthBeats().inBeats();
                            const auto absolute=range.getStart().inBeats()+note->getStartBeat().inBeats();
                            const auto snapped=std::floor(absolute/grid+.5)*grid-range.getStart().inBeats();
                            const auto start=juce::jlimit(0.0,std::max(0.0,range.getLength().inBeats()-length),snapped);
                            note->setStartAndLength(te::BeatPosition::fromBeats(start),note->getLengthBeats(),undo);
                        }
                        note->state.setProperty (midiNoteIDProperty, juce::Uuid().toString(), undo);
                    }
            }
        impl->changed();
    }
    sendChangeMessage();
}
Result Session::record()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::ok();
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio device in Audio Settings.");
    bool armed = false;
    for (auto* t : te::getAudioTracks (*impl->edit)) if (auto* d = impl->input (*t)) armed |= d->isRecordingEnabled (t->itemID);
    if (! armed) return Result::fail ("Assign an input and arm at least one track before recording.");
    const auto quantisation=snapshot().recordQuantisation;
    if(quantisation!=RecordQuantisation::none)
    {
        for(auto* track:te::getAudioTracks(*impl->edit)) if(auto* input=impl->input(*track))
            if(input->getInputDevice().isMidi() && input->isRecordingEnabled(track->itemID))
            {
                if(impl->edit->getTransport().looping) return Result::fail("Disable loop recording before using MIDI recording quantization.");
                for(auto* clip:track->getClips()) if(auto* midi=dynamic_cast<te::MidiClip*>(clip))
                    if(!canCopyMidiClip(*midi)) return Result::fail("Recording quantization requires unlooped, ungrouped, single-take MIDI clips at normal speed without source offsets.");
            }
    }
    auto r = impl->assets().createDirectory(); if (r.failed()) return r;
    for (auto* input : impl->edit->getAllInputDevices()) input->flushRecordingStateUpdates();
    impl->edit->dispatchPendingUpdatesSynchronously();
    impl->edit->setCountInMode (static_cast<te::Edit::CountIn> (snapshot().metronome.countInBars));
    for(auto* input:impl->edit->getAllInputDevices()) if(input->isRecordingActive())
        if(auto* midi=dynamic_cast<te::MidiInputDevice*>(&input->getInputDevice())) midi->quantisation.setType("(none)");
    impl->edit->setClickTrackRange ({});
    impl->edit->getTransport().record (false);
    if (! impl->edit->getTransport().isRecording()) return Result::fail ("Recording could not start. Check the selected input and device settings.");
    impl->changed(); return Result::ok();
}
void Session::seek (double seconds)
{
    if (impl->exporting) return;
    if (std::isfinite (seconds))
    {
        if (impl->edit->getTransport().isRecording()) stop();
        impl->cancelLaunches();
        impl->edit->getTransport().setPosition (te::TimePosition::fromSeconds (juce::jmax (0.0, seconds)));
    }
}
void Session::setTempo (double bpm)
{
    if (impl->exporting) return;
    if (! std::isfinite (bpm)) return;
    impl->resetTapTempo();
    const auto value = juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm);
    auto* tempo = impl->edit->tempoSequence.getTempo (0);
    if (tempo->getBpm() == value) return;
    impl->transaction ("Tempo"); tempo->setBpm (value); impl->changed();
}
Result Session::tapTempo(double seconds)
{
    if (impl->exporting) return Result::fail("Finish or cancel the current export first.");
    if (!std::isfinite(seconds) || seconds<0 || (impl->lastTap>=0 && seconds<impl->lastTap))
        return Result::fail("Tap timestamps must be finite and monotonic.");
    const double interval=seconds-impl->lastTap;
    if (impl->lastTap<0 || interval>60.0/te::TempoSetting::minBPM+1e-9)
    {
        impl->resetTapTempo();impl->lastTap=seconds;return Result::ok();
    }
    // Ignore switch bounce without advancing the anchor or touching history.
    if (interval<60.0/te::TempoSetting::maxBPM-1e-9) return Result::ok();
    impl->lastTap=seconds;
    impl->tapIntervals[(size_t)impl->nextTapInterval]=interval;
    impl->nextTapInterval=(impl->nextTapInterval+1)%4;
    impl->numTapIntervals=std::min(4,impl->numTapIntervals+1);
    double elapsed=0;
    for (int i=0;i<impl->numTapIntervals;++i) elapsed+=impl->tapIntervals[(size_t)i];
    const double bpm=juce::jlimit(te::TempoSetting::minBPM,te::TempoSetting::maxBPM,60.0*impl->numTapIntervals/elapsed);
    auto* tempo=impl->edit->tempoSequence.getTempo(0);
    if (std::abs(tempo->getBpm()-bpm)<1e-9) return Result::ok();
    impl->transaction("Tap tempo");tempo->setBpm(bpm);impl->changed();return Result::ok();
}
Result Session::setMetronome (const MetronomeSettings& settings)
{
    if(impl->exporting) return Result::fail("Finish or cancel the current export first.");
    if(!std::isfinite(settings.gain) || settings.gain<.2f || settings.gain>1 || settings.countInBars<0 || settings.countInBars>2)
        return Result::fail("Metronome level must be 20–100% and count-in zero, one or two bars.");
    const auto previous=snapshot().metronome;
    if(previous==settings) return Result::ok();
    if(impl->edit->getTransport().isRecording() && previous.countInBars!=settings.countInBars)
        return Result::fail("Stop recording before changing the count-in.");
    impl->transaction("Metronome"); auto* undo=&impl->edit->getUndoManager(); auto& edit=*impl->edit;
    edit.clickTrackEnabled.setValue(settings.enabled,undo);
    edit.clickTrackEmphasiseBars.setValue(settings.accent,undo);
    edit.clickTrackRecordingOnly.setValue(settings.recordingOnly,undo);
    edit.clickTrackGain.setValue(settings.gain,undo);
    edit.state.setProperty(countInProperty,settings.countInBars,undo);
    edit.setCountInMode(static_cast<te::Edit::CountIn>(settings.countInBars));
    impl->changed(); return Result::ok();
}
Result Session::setRecordQuantisation(RecordQuantisation value)
{
    const int choice=static_cast<int>(value);
    if(choice<0 || choice>6) return Result::fail("Choose a supported recording grid.");
    if(snapshot().recordQuantisation==value) return Result::ok();
    if(impl->exporting || impl->edit->getTransport().isRecording()) return Result::fail("Stop recording or finish exporting before changing recording quantization.");
    impl->transaction("Recording quantization");
    impl->edit->state.setProperty(recordQuantisationProperty,choice,&impl->edit->getUndoManager());
    impl->changed();return Result::ok();
}
void Session::setLoop (bool enabled, double start, double end)
{
    if (impl->exporting) return;
    if (! std::isfinite (start) || ! std::isfinite (end) || start < 0 || end <= start) return;
    auto& t = impl->edit->getTransport();
    impl->transaction ("Loop range");
    auto* undo = &impl->edit->getUndoManager();
    // Transport defaults to non-undoable navigation. User-edited loop bounds
    // are document edits, so supply the edit's undo manager explicitly.
    t.loopPoint1.setValue (te::TimePosition::fromSeconds (start), undo);
    t.loopPoint2.setValue (te::TimePosition::fromSeconds (end), undo);
    t.looping.setValue (enabled, undo);
    impl->changed();
}
Result Session::addLocator (double seconds, const String& requestedName, String& createdId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto& manager = impl->edit->getMarkerManager();
    if (manager.getMarkers().size() >= 4096) return Result::fail ("The project already contains 4096 locators.");
    const auto name = requestedName.trim().isEmpty() ? "Locator " + String (manager.getNextUniqueID()) : requestedName.trim();
    if (! validLocator (name,seconds)) return Result::fail ("Use a locator name of 1–128 characters and a position inside the 48-hour timeline.");
    impl->transaction ("Add locator"); impl->edit->ensureMarkerTrack();
    auto marker = manager.createMarker (-1,te::TimePosition::fromSeconds (seconds),te::TimeDuration::fromSeconds (.001),te::Clip::syncAbsolute,nullptr);
    if (! marker) return Result::fail ("The locator could not be created.");
    marker->setName (name); createdId = marker->itemID.toString(); impl->changed(); return Result::ok();
}
Result Session::updateLocator (const String& id, const String& requestedName, double seconds)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* marker = impl->locator (id); if (! marker) return Result::fail ("Locator not found.");
    const auto name = requestedName.trim();
    if (! validLocator (name,seconds)) return Result::fail ("Use a locator name of 1–128 characters and a position inside the 48-hour timeline.");
    if (marker->getName() == name && marker->getPosition().getStart().inSeconds() == seconds) return Result::ok();
    impl->transaction ("Edit locator"); marker->setName (name);
    marker->setStart (te::TimePosition::fromSeconds (seconds),false,true); impl->changed(); return Result::ok();
}
Result Session::removeLocator (const String& id)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* marker = impl->locator (id); if (! marker) return Result::fail ("Locator not found.");
    impl->transaction ("Delete locator"); marker->removeFromParent(); impl->changed(); return Result::ok();
}
Result Session::seekLocator (const String& id)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* marker = impl->locator (id); if (! marker) return Result::fail ("Locator not found.");
    seek (marker->getPosition().getStart().inSeconds()); return Result::ok();
}
Result Session::navigateLocator (bool forward)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto& manager = impl->edit->getMarkerManager(); const auto position = impl->edit->getTransport().getPosition();
    if (auto* marker = forward ? manager.getNextMarker (position) : manager.getPrevMarker (position))
        seek (marker->getPosition().getStart().inSeconds());
    return Result::ok();
}
void Session::beginEditGesture (const String& name)
{
    if (impl->exporting) return;
    endEditGesture();
    impl->edit->getUndoManager().beginNewTransaction (name);
    impl->gesture = std::make_unique<te::Edit::UndoTransactionInhibitor> (*impl->edit);
}
void Session::endEditGesture()
{
    if (impl->exporting) return;
    impl->flushStructuralUpdates();
    if (! impl->gesture) return;
    impl->gesture.reset();
    impl->edit->getUndoManager().beginNewTransaction();
}
void Session::undo()
{
    if (impl->exporting || impl->edit->getTransport().isRecording()) return;
    const auto monitored=impl->monitoredMidiTracks();const auto clips=impl->clipActivationStates(); const auto follows=impl->followStates();
    impl->resetTapTempo();
    endEditGesture(); closePluginEditors(); impl->edit->getUndoManager().undo(); impl->refreshClipActivation(clips); impl->refreshFollowActions(follows); impl->refreshParameterValues();
    impl->finishInputChange(monitored);sendChangeMessage();
}
void Session::redo()
{
    if (impl->exporting || impl->edit->getTransport().isRecording()) return;
    const auto monitored=impl->monitoredMidiTracks();const auto clips=impl->clipActivationStates(); const auto follows=impl->followStates();
    impl->resetTapTempo();
    endEditGesture(); closePluginEditors(); impl->edit->getUndoManager().redo(); impl->refreshClipActivation(clips); impl->refreshFollowActions(follows); impl->refreshParameterValues();
    impl->finishInputChange(monitored);sendChangeMessage();
}
bool Session::canUndo() const { return ! impl->exporting && ! impl->edit->getTransport().isRecording() && impl->edit->getUndoManager().canUndo(); }
bool Session::canRedo() const { return ! impl->exporting && ! impl->edit->getTransport().isRecording() && impl->edit->getUndoManager().canRedo(); }

Result Session::exportWav (const File& file, double rate, int bits, double tailSeconds)
{
    return exportAudio (file, ExportFormat::wav, rate, bits, tailSeconds);
}

Result Session::exportStems (const File& folder, const AudioExportSettings& settings, juce::StringArray& written, bool overwrite)
{
    written.clear();
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before exporting.");
    const auto snapshot = this->snapshot();
    const juce::String extension = settings.format == ExportFormat::aiff ? ".aiff" : settings.format == ExportFormat::flac ? ".flac" : ".wav";
    // Stems in display order: a group before its members, returns after the tracks, then Master.
    struct Stem { String name; std::optional<String> trackId, groupId; bool master = false; };
    std::vector<Stem> stems; String open;
    for (const auto& track : snapshot.tracks)
    {
        if (track.groupId.isNotEmpty() && track.groupId != open)
            for (const auto& group : snapshot.groups) if (group.id == track.groupId) stems.push_back ({ group.name, std::nullopt, group.id });
        open = track.groupId;
        if (! track.isReturn) stems.push_back ({ track.name, track.id, std::nullopt });
    }
    for (const auto& track : snapshot.tracks) if (track.isReturn) stems.push_back ({ track.name, track.id, std::nullopt });
    stems.push_back ({ "Master", std::nullopt, std::nullopt, true });
    std::vector<File> files; std::set<String> used;
    for (const auto& stem : stems)
    {
        auto base = File::createLegalFileName (snapshot.name + " " + stem.name), name = base;
        for (int n = 2; ! used.insert (name.toLowerCase()).second; ++n) name = base + " " + String (n);
        files.push_back (folder.getChildFile (name + extension));
        if (! overwrite && files.back().exists()) return Result::fail (files.back().getFileName() + " already exists. Choose another folder or replace the files.");
    }
    if (auto r = folder.createDirectory(); r.failed()) return r;
    // Isolate each stem with solo and return mutes outside history, then restore them exactly.
    std::vector<std::pair<juce::ValueTree, std::pair<juce::var, juce::var>>> saved;
    for (auto* track : te::getAllTracks (*impl->edit))
        if (dynamic_cast<te::AudioTrack*> (track) != nullptr || dynamic_cast<te::FolderTrack*> (track) != nullptr)
            saved.push_back ({ track->state, { track->state[te::IDs::solo], track->state[te::IDs::mute] } });
    const bool wasDirty = snapshot.dirty;
    const auto put = [] (juce::ValueTree& state, const juce::Identifier& id, const juce::var& value)
    { if (value.isVoid()) state.removeProperty (id, nullptr); else state.setProperty (id, value, nullptr); };
    const auto restore = [&]
    {
        for (auto& [state, values] : saved) { put (state, te::IDs::solo, values.first); put (state, te::IDs::mute, values.second); }
        impl->refreshRouting();
        if (! wasDirty) impl->edit->resetChangedStatus();
    };
    for (size_t i = 0; i < stems.size(); ++i)
    {
        const auto& stem = stems[i];
        // Start each stem from the project's own solo and mute state; Master keeps it.
        for (auto& [state, values] : saved) { put (state, te::IDs::solo, values.first); put (state, te::IDs::mute, values.second); }
        if (! stem.master)
        {
            juce::ValueTree target;
            if (stem.groupId) if (auto* folderTrack = impl->group (*stem.groupId)) target = folderTrack->state;
            if (stem.trackId) if (auto* track = impl->track (*stem.trackId)) target = track->state;
            for (auto& [state, values] : saved) state.setProperty (te::IDs::solo, state == target, nullptr);
            // Track and group stems leave out return effects; a return stem keeps its sends.
            for (auto* track : te::getAudioTracks (*impl->edit))
                if (isReturn (*track) && track->state != target) track->state.setProperty (te::IDs::mute, true, nullptr);
        }
        impl->refreshRouting();
        const auto result = exportAudio (files[i], settings);
        if (result.failed()) { restore(); return result; }
        written.add (files[i].getFullPathName());
    }
    restore();
    sendChangeMessage(); return Result::ok();
}
Result Session::exportAudio (const File& file, ExportFormat format, double rate, int bits, double tailSeconds)
{
    return exportAudio (file, { format, rate, bits, tailSeconds });
}

Result Session::exportAudio (const File& file, const AudioExportSettings& settings)
{
    const auto format = settings.format;
    const double rate = settings.sampleRate, tailSeconds = settings.tailSeconds;
    const int bits = settings.bitDepth;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before exporting.");
    if (rate < 8000 || rate > 384000 || ! std::isfinite (rate) || (bits != 16 && bits != 24 && bits != 32)) return Result::fail ("Unsupported export format.");
    auto& formats = impl->engine.getAudioFileFormatManager();
    juce::AudioFormat* audioFormat = nullptr;
    switch (format)
    {
        case ExportFormat::wav: audioFormat = formats.getWavFormat(); break;
        case ExportFormat::aiff: audioFormat = formats.getAiffFormat(); break;
        case ExportFormat::flac: audioFormat = formats.getFlacFormat(); break;
    }
    if (audioFormat == nullptr || ! audioFormat->getPossibleBitDepths().contains (bits))
        return Result::fail ("The selected format does not support this bit depth.");
    if (! std::isfinite (tailSeconds) || tailSeconds < 0 || tailSeconds > 60) return Result::fail ("Export tail must be between 0 and 60 seconds.");
    if (! std::isfinite (settings.peakCeilingDb) || settings.peakCeilingDb < -60 || settings.peakCeilingDb > 0)
        return Result::fail ("Choose a peak ceiling from -60 to 0 dB.");
    static_assert (maximumExportRangeSeconds == te::Edit::maximumLength);
    juce::int64 firstFrame = 0, lastFrame = 0;
    if (settings.timeRange)
    {
        const double first = settings.timeRange->getStart(), last = settings.timeRange->getEnd();
        if (! std::isfinite (first) || ! std::isfinite (last) || first < 0 || first >= last
            || last > maximumExportRangeSeconds)
            return Result::fail ("Choose an export range within the first 48 hours, with its end after its start.");
        if (tailSeconds != 0) return Result::fail ("A selected range requires a zero-second export tail.");
        firstFrame = std::llround (first * rate); lastFrame = std::llround (last * rate);
        if (static_cast<double> (lastFrame) / rate > maximumExportRangeSeconds)
            return Result::fail ("The rounded export end exceeds the supported 48-hour timeline.");
        if (lastFrame <= firstFrame) return Result::fail ("The selected export range must contain at least one output frame.");
    }
    endEditGesture();
    double end = 0;
    for (auto* t : te::getAudioTracks (*impl->edit)) for (auto* c : t->getClips()) end = juce::jmax (end, c->getPosition().time.getEnd().inSeconds());
    if (end <= 0) return Result::fail ("Add audio or MIDI clips before exporting.");
    end = settings.timeRange ? static_cast<double> (lastFrame) / rate : end + tailSeconds;
    auto r = file.getParentDirectory().createDirectory(); if (r.failed()) return r;
    juce::TemporaryFile output (file);
    juce::TemporaryFile prefix (".wav");
    // Native menu callbacks still dispatch during a modal progress window. Hold
    // this guard until the worker and render scopes have completely released the
    // edit; a reentrant New, Undo or Export must never mutate their graph.
    const juce::ScopedValueSetter<bool> exporting (impl->exporting, true);
    impl->edit->dispatchPendingUpdatesSynchronously();
    // Nesting is supported by the engine. This outer scope drains playback
    // before resetting owned control queues and restores it only after export.
    const te::Edit::ScopedRenderStatus renderStatus (*impl->edit, true);
    for (auto* track : te::getAudioTracks (*impl->edit))
    {
        if (auto* synth = Impl::synth (*track)) synth->prepareForOfflineRender();
        for (auto* plugin : Impl::allDevices (*track))
        {
            if (auto* utility = dynamic_cast<UtilityPlugin*> (plugin)) utility->prepareForOfflineRender();
            if (auto* effect = dynamic_cast<NativeEffectPlugin*> (plugin)) effect->prepareForOfflineRender();
        }
    }
    te::Renderer::Parameters p (*impl->edit);
    p.destFile = settings.timeRange ? prefix.getFile() : output.getFile();
    p.audioFormat = settings.timeRange ? formats.getWavFormat() : audioFormat;
    p.bitDepth = settings.timeRange ? 32 : bits; p.sampleRateForAudio = rate; p.blockSizeForAudio = 512;
    p.time = { te::TimePosition::fromSeconds (0), te::TimePosition::fromSeconds (end) };
    p.tracksToDo = te::toBitSet (te::getAllTracks (*impl->edit));
    for (auto* track : te::getAudioTracks (*impl->edit))
        for (auto* clip : track->getClips()) p.allowedClips.add (clip);
    p.usePlugins = true; p.useMasterPlugins = true; p.canRenderInMono = settings.mono && ! settings.timeRange; p.mustRenderInMono = p.canRenderInMono;
    p.shouldNormalise = settings.normalizePeak && ! settings.timeRange; p.normaliseToLevelDb = settings.peakCeilingDb;
    p.checkNodesForAudio = false; p.ditheringEnabled = false;
    auto& ui = static_cast<UIBehaviour&> (impl->engine.getUIBehaviour());
    ui.cancelled = false;
    auto rendered = te::Renderer::renderToFile ("Export Halftrack mix", p);
    if (ui.cancelled) return Result::fail ("Export cancelled.");
    if (! rendered.existsAsFile()) return Result::fail ("The audio export could not be completed.");
    if (settings.timeRange)
    {
        RangeExportTask task (rendered, output.getFile(), *audioFormat, settings, firstFrame, lastFrame);
        ui.runTaskWithProgressBar (task);
        if (ui.cancelled) return Result::fail ("Export cancelled.");
        if (task.result.failed()) return task.result;
    }
    if (! output.overwriteTargetFileWithTemporary()) return Result::fail ("The audio export could not be completed.");
    sendChangeMessage(); return Result::ok();
}

bool Session::isExporting() const { return impl->exporting; }
void Session::cancelExport() { static_cast<UIBehaviour&> (impl->engine.getUIBehaviour()).cancelTask(); }

juce::AudioDeviceManager& Session::audioDeviceManager() { return impl->engine.getDeviceManager().deviceManager; }
juce::KnownPluginList& Session::knownPlugins() { return impl->engine.getPluginManager().knownPluginList; }
juce::AudioPluginFormatManager& Session::pluginFormats() { return impl->engine.getPluginManager().pluginFormatManager; }
File Session::pluginScanDeadMansFile() const
{
    auto folder = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Halftrack");
    folder.createDirectory(); return folder.getChildFile ("plugin-scan-in-progress.txt");
}
Result Session::addNativeEffect (int index, EffectKind kind)
{
    if (auto frozen = impl->unfrozen (impl->track (index)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (index); if (! track) return Result::fail ("Track not found.");
    if (effectParameters (kind).empty()) return Result::fail ("Unknown native effect.");
    if (! track->pluginList.canInsertPlugin()) return Result::fail ("This track's device chain is full.");
    juce::ValueTree state (te::IDs::PLUGIN);
    state.setProperty (te::IDs::type, NativeEffectPlugin::xmlTypeName, nullptr);
    state.setProperty ("effectKind", static_cast<int> (kind), nullptr);
    if (effectIsInstrument (kind)) return Result::fail ("Choose instruments from the track's instrument menu.");
    auto* instrument = Impl::instrument (*track);
    if (effectIsMidi (kind) && instrument == nullptr) return Result::fail ("MIDI effects need a MIDI track.");
    auto plugin = impl->edit->getPluginCache().createNewPlugin (state);
    if (! plugin) return Result::fail ("The native effect could not be created.");
    int insertion = track->pluginList.size();
    for (int i = 0; i < track->pluginList.size(); ++i)
        if (dynamic_cast<te::VolumeAndPanPlugin*> (track->pluginList[i]) || dynamic_cast<te::AuxSendPlugin*> (track->pluginList[i]) || dynamic_cast<TrackDelayPlugin*> (track->pluginList[i])
            || (effectIsMidi (kind) && track->pluginList[i] == instrument))
        { insertion = i; break; }
    impl->transaction ("Add " + String (effectName (kind)));
    track->pluginList.insertPlugin (plugin, insertion, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setNativeEffectSettings (const String& id, const EffectSettings& settings)
{
    if (auto frozen = impl->unfrozen (impl->trackOfPlugin (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! validEffectSettings (settings)) return Result::fail ("Effect settings are outside their supported ranges.");
    if (auto* effect = dynamic_cast<NativeEffectPlugin*> (impl->device (id).plugin))
    {
        const auto previous = effect->readSettings();
        if (previous.kind != settings.kind) return Result::fail ("Effect type cannot change during an edit.");
        if (previous.values == settings.values) return Result::ok();
        for (const auto& m : macroMappings (effect->state))
            if (settings.values[(size_t) m.parameter] != previous.values[(size_t) m.parameter])
                return Result::fail (String (effectParameters (settings.kind)[(size_t) m.parameter].label) + " follows Macro " + String (m.macro + 1) + ".");
        impl->transaction ("Edit " + String (effectName (settings.kind)));
        effect->writeSettings (settings); impl->changed(); return Result::ok();
    }
    return Result::fail ("Native effect not found.");
}
Result Session::addUtility (int index)
{
    if (auto frozen = impl->unfrozen (impl->track (index)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (index); if (! track) return Result::fail ("Track not found.");
    if (! track->pluginList.canInsertPlugin()) return Result::fail ("This track's device chain is full.");
    auto plugin = impl->edit->getPluginCache().createNewPlugin (UtilityPlugin::xmlTypeName, {});
    if (! plugin) return Result::fail ("The channel utility could not be created.");
    int insertion = track->pluginList.size();
    for (int i = 0; i < track->pluginList.size(); ++i)
        if (dynamic_cast<te::VolumeAndPanPlugin*> (track->pluginList[i]) || dynamic_cast<te::AuxSendPlugin*> (track->pluginList[i]) || dynamic_cast<TrackDelayPlugin*> (track->pluginList[i]))
        { insertion = i; break; }
    impl->transaction ("Add channel utility");
    track->pluginList.insertPlugin (plugin, insertion, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setUtilitySettings (const String& id, const UtilitySettings& settings)
{
    if (auto frozen = impl->unfrozen (impl->trackOfPlugin (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! UtilityPlugin::validSettings (settings)) return Result::fail ("Channel utility settings are outside their supported ranges.");
    if (auto* utility = dynamic_cast<UtilityPlugin*> (impl->device (id).plugin))
    {
        if (utility->readSettings() == settings) return Result::ok();
        impl->transaction ("Edit channel utility"); utility->writeSettings (settings);
        impl->changed(); return Result::ok();
    }
    return Result::fail ("Channel utility not found.");
}
Result Session::movePlugin (int t, int p, int destination)
{
    if (auto frozen = impl->unfrozen (impl->track (t)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto list = impl->plugins (t);
    if (! juce::isPositiveAndBelow (p, list.size()) || ! juce::isPositiveAndBelow (destination, list.size()))
        return Result::fail ("Device position not found.");
    if (p == destination) return Result::ok();
    // MIDI effects stay ahead of the instrument; audio devices stay after it.
    const auto midiDevice = [] (te::Plugin* plugin)
    { auto* effect = dynamic_cast<NativeEffectPlugin*> (plugin); return effect != nullptr && effect->takesMidiInput(); };
    if (midiDevice (list[p]) != midiDevice (list[destination]))
        return Result::fail ("MIDI effects stay before the instrument; audio devices stay after it.");
    auto state = list[p]->state; auto parent = state.getParent();
    if (parent != list[destination]->state.getParent()) return Result::fail ("Devices must be on the same track.");
    impl->transaction ("Move device");
    parent.moveChild (parent.indexOf (state), parent.indexOf (list[destination]->state), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::addPlugin (int index, const juce::PluginDescription& description)
{
    if (auto frozen = impl->unfrozen (impl->track (index)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (! t->pluginList.canInsertPlugin()) return Result::fail ("This track's device chain is full.");
    impl->transaction ("Add plug-in");
    // Tracktion resolves its persisted plug-in identity through this registry.
    knownPlugins().addType (description);
    auto plugin = impl->edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, description);
    if (! plugin) return Result::fail ("The plug-in could not be loaded.");
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get());
    if (! external || (! external->getAudioPluginInstance() && ! external->isInitialisingAsync()))
        return Result::fail (external ? external->getLoadError() : "The plug-in could not be instantiated.");
    auto insertIndex = isReturn (*t) ? 1 : 0;
    if (auto* source = Impl::instrument (*t)) insertIndex = t->pluginList.indexOf (source) + 1;
    t->pluginList.insertPlugin (plugin, insertIndex, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setPluginEnabled (int t, int p, bool enabled)
{
    if (auto frozen = impl->unfrozen (impl->track (t)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* plugin = impl->plugin (t, p); if (! plugin) return Result::fail ("Plug-in not found.");
    impl->transaction ("Bypass plug-in"); plugin->setEnabled (enabled); impl->changed(); return Result::ok();
}
Result Session::removePlugin (int t, int p)
{
    if (auto frozen = impl->unfrozen (impl->track (t)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* plugin = impl->plugin (t, p); if (! plugin) return Result::fail ("Plug-in not found.");
    if (auto* rack = dynamic_cast<te::RackInstance*> (plugin); rack && rack->type) for (auto* d : rack->type->getPlugins()) d->windowState->closeWindowExplicitly();
    plugin->windowState->closeWindowExplicitly(); impl->transaction ("Remove plug-in"); plugin->deleteFromParent(); impl->removeUnusedRacks();
    impl->changed(); return Result::ok();
}
// Audio Effect Racks. Chains are identified by their mixer's ID, devices by their own ID.
Result Session::groupDevices (int index, const juce::StringArray& ids, String& rackId)
{
    auto* track = impl->track (index);
    if (! track) return Result::fail ("Track not found.");
    if (auto frozen = impl->unfrozen (track); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before grouping devices.");
    juce::Array<te::Plugin*> devices;
    for (auto* p : impl->plugins (index)) if (ids.contains (p->itemID.toString())) devices.add (p);
    juce::StringArray unique (ids); unique.removeDuplicates (false);
    if (devices.size() != ids.size() || unique.size() != ids.size()) return Result::fail ("Group devices from the selected track's device chain.");
    for (auto* d : devices)
    {
        if (dynamic_cast<te::RackInstance*> (d)) return Result::fail ("Racks cannot contain other racks.");
        if (auto* native = dynamic_cast<NativeEffectPlugin*> (d); native && effectIsMidi (native->getKind()))
            return Result::fail ("MIDI effects cannot go in an Audio Effect Rack.");
        if (! te::RackType::isPluginAllowed (d)) return Result::fail (d->getName() + " cannot go in a rack.");
    }
    if (devices.isEmpty() && ! track->pluginList.canInsertPlugin()) return Result::fail ("This track's device chain is full.");
    int insertion = track->pluginList.size();
    if (! devices.isEmpty()) insertion = track->pluginList.indexOf (devices.getFirst());
    else
        for (int i = 0; i < track->pluginList.size(); ++i)
            if (dynamic_cast<te::VolumeAndPanPlugin*> (track->pluginList[i]) || dynamic_cast<te::AuxSendPlugin*> (track->pluginList[i]) || dynamic_cast<TrackDelayPlugin*> (track->pluginList[i])) { insertion = i; break; }
    impl->transaction (devices.isEmpty() ? "Add Audio Effect Rack" : "Group devices");
    auto* undo = &impl->edit->getUndoManager();
    auto type = impl->edit->getRackList().addNewRack();
    if (type == nullptr) return Result::fail ("Could not create the rack.");
    type->rackName = "Audio Effect Rack";
    type->state.setProperty (rackProperty, 1, undo);
    juce::ValueTree macros (macrosType);
    for (int m = 0; m < rackMacroCount; ++m)
        macros.appendChild (juce::ValueTree (macroType, { { te::IDs::name, "Macro " + String (m + 1) }, { te::IDs::value, 0.0f } }), nullptr);
    type->state.appendChild (macros, undo);
    for (auto* d : devices) { d->windowState->closeWindowExplicitly(); type->addPlugin (d, { .5f, .5f }, false); }
    if (! devices.isEmpty() && impl->addChainMixer (*type, devices.getFirst()->getName()) == nullptr) return Result::fail ("Could not create the chain.");
    auto instance = impl->edit->getPluginCache().createNewPlugin (te::RackInstance::create (*type));
    if (instance == nullptr) return Result::fail ("Could not create the rack.");
    track->pluginList.insertPlugin (instance, insertion, nullptr);
    impl->rewire (*type);
    rackId = instance->itemID.toString();
    impl->changed(); return Result::ok();
}
Result Session::ungroupRack (const String& rackId)
{
    auto* rack = impl->rack (rackId);
    if (! rack) return Result::fail ("Rack not found.");
    auto* track = impl->trackOfPlugin (rackId);
    if (auto frozen = impl->unfrozen (track); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before ungrouping devices.");
    const auto chains = Impl::rackChains (*rack->type);
    if (chains.size() > 1) return Result::fail ("Only a rack with one chain can be ungrouped.");
    auto* audio = dynamic_cast<te::AudioTrack*> (track);
    impl->transaction ("Ungroup rack");
    auto* undo = &impl->edit->getUndoManager();
    int index = audio->pluginList.indexOf (rack);
    if (! chains.empty())
        for (auto* d : chains.front().devices)
        {
            const te::Plugin::Ptr device (d);
            device->windowState->closeWindowExplicitly();
            device->state.removeProperty (macroMapProperty, undo); // The rack's macros go with it.
            audio->pluginList.insertPlugin (device, index++, nullptr);
        }
    rack->windowState->closeWindowExplicitly();
    rack->deleteFromParent(); impl->removeUnusedRacks();
    impl->changed(); return Result::ok();
}
Result Session::addRackChain (const String& rackId, String& chainId)
{
    auto* rack = impl->rack (rackId);
    if (! rack) return Result::fail ("Rack not found.");
    if (auto frozen = impl->unfrozen (impl->trackOfPlugin (rackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before adding chains.");
    const auto count = Impl::rackChains (*rack->type).size();
    if (count >= 64) return Result::fail ("A rack holds at most 64 chains.");
    impl->transaction ("Add chain");
    auto* mixer = impl->addChainMixer (*rack->type, "Chain " + String ((int) count + 1));
    if (mixer == nullptr) return Result::fail ("Could not create the chain.");
    impl->rewire (*rack->type);
    chainId = mixer->itemID.toString();
    impl->changed(); return Result::ok();
}
Result Session::removeRackChain (const String& chainId)
{
    const auto at = impl->chain (chainId);
    if (! at.rack) return Result::fail ("Chain not found.");
    if (auto frozen = impl->unfrozen (at.track); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before removing chains.");
    const auto chains = Impl::rackChains (*at.rack->type);
    impl->transaction ("Delete chain");
    for (auto* d : chains[(size_t) at.chain].devices) { d->windowState->closeWindowExplicitly(); d->deleteFromParent(); }
    at.plugin->deleteFromParent();
    impl->rewire (*at.rack->type);
    impl->changed(); return Result::ok();
}
Result Session::setRackChain (const String& chainId, const ChainSettings& settings)
{
    const auto at = impl->chain (chainId);
    auto* mixer = dynamic_cast<TrackMixer*> (at.plugin);
    if (! at.rack || ! mixer) return Result::fail ("Chain not found.");
    if (auto frozen = impl->unfrozen (at.track); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! std::isfinite (settings.gainDb) || ! std::isfinite (settings.pan) || settings.pan < -1 || settings.pan > 1 || settings.gainDb > 6)
        return Result::fail ("Chain volume runs to +6 dB and pan from -1 to +1.");
    const auto name = settings.name.trim();
    if (name.isEmpty() || name.length() > 128) return Result::fail ("Chains need a name of up to 128 characters.");
    auto& state = mixer->state;
    const bool routing = static_cast<bool> (state.getProperty (chainMuteProperty, false)) != settings.mute
        || static_cast<bool> (state.getProperty (chainSoloProperty, false)) != settings.solo;
    const auto gain = juce::jmax (-100.0f, settings.gainDb);
    const bool level = std::abs (mixer->getVolumeDb() - gain) > 1e-4f || std::abs (mixer->getPan() - settings.pan) > 1e-6f;
    if (! routing && ! level && state[chainNameProperty].toString() == name) return Result::ok();
    auto* undo = &impl->edit->getUndoManager();
    // As on track faders, the undoable manual values are what undo restores to the parameters.
    for (const auto& pair : { std::pair { &manualVolumeProperty, mixer->volParam.get() }, std::pair { &manualPanProperty, mixer->panParam.get() } })
        if (! state.hasProperty (*pair.first)) state.setProperty (*pair.first, pair.second->getCurrentExplicitValue(), nullptr);
    impl->transaction ("Edit chain");
    state.setProperty (chainNameProperty, name, undo);
    state.setProperty (chainMuteProperty, settings.mute, undo);
    state.setProperty (chainSoloProperty, settings.solo, undo);
    if (level)
    {
        mixer->setVolumeDb (gain); mixer->setPan (settings.pan);
        state.setProperty (manualVolumeProperty, te::decibelsToVolumeFaderPosition (gain), undo);
        state.setProperty (manualPanProperty, settings.pan, undo);
    }
    if (routing) impl->rewire (*at.rack->type);
    impl->changed(); return Result::ok();
}
Result Session::addNativeEffectToChain (const String& chainId, EffectKind kind)
{
    if (effectParameters (kind).empty()) return Result::fail ("Unknown native effect.");
    if (effectIsInstrument (kind) || effectIsMidi (kind)) return Result::fail ("Audio Effect Racks hold audio effects.");
    juce::ValueTree state (te::IDs::PLUGIN);
    state.setProperty (te::IDs::type, NativeEffectPlugin::xmlTypeName, nullptr);
    state.setProperty ("effectKind", static_cast<int> (kind), nullptr);
    return impl->addToChain (chainId, state, effectName (kind));
}
Result Session::addUtilityToChain (const String& chainId)
{
    juce::ValueTree state (te::IDs::PLUGIN);
    state.setProperty (te::IDs::type, UtilityPlugin::xmlTypeName, nullptr);
    return impl->addToChain (chainId, state, "Utility");
}
Result Session::setDeviceEnabled (const String& id, bool enabled)
{
    const auto at = impl->device (id);
    if (! impl->userDevice (at)) return Result::fail ("Device not found.");
    if (auto frozen = impl->unfrozen (at.track); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (at.plugin->isEnabled() == enabled) return Result::ok();
    impl->transaction (enabled ? "Activate device" : "Deactivate device"); at.plugin->setEnabled (enabled);
    impl->changed(); return Result::ok();
}
Result Session::removeDevice (const String& id)
{
    const auto at = impl->device (id);
    if (! impl->userDevice (at)) return Result::fail ("Device not found.");
    if (! at.rack) return removePlugin (te::getAudioTracks (*impl->edit).indexOf (at.track), impl->plugins (te::getAudioTracks (*impl->edit).indexOf (at.track)).indexOf (at.plugin));
    if (auto frozen = impl->unfrozen (at.track); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before removing devices.");
    at.plugin->windowState->closeWindowExplicitly();
    impl->transaction ("Remove device"); at.plugin->deleteFromParent(); impl->rewire (*at.rack->type);
    impl->changed(); return Result::ok();
}
Result Session::moveDevice (const String& id, int destination)
{
    const auto at = impl->device (id);
    if (! impl->userDevice (at)) return Result::fail ("Device not found.");
    if (! at.rack)
    {
        const int t = te::getAudioTracks (*impl->edit).indexOf (at.track);
        return movePlugin (t, impl->plugins (t).indexOf (at.plugin), destination);
    }
    if (auto frozen = impl->unfrozen (at.track); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    const auto devices = Impl::rackChains (*at.rack->type)[(size_t) at.chain].devices;
    if (! juce::isPositiveAndBelow (destination, devices.size())) return Result::fail ("Device position not found.");
    if (devices[destination] == at.plugin) return Result::ok();
    impl->transaction ("Move device");
    impl->placeInRack (*at.rack->type, *at.plugin, at.rack->type->getPlugins().indexOf (devices[destination]));
    impl->rewire (*at.rack->type);
    impl->changed(); return Result::ok();
}
Result Session::showDeviceEditor (const String& id)
{
    const auto at = impl->device (id);
    if (! impl->userDevice (at)) return Result::fail ("Device not found.");
    if (! at.rack)
    {
        const int t = te::getAudioTracks (*impl->edit).indexOf (at.track);
        return showPluginEditor (t, impl->plugins (t).indexOf (at.plugin));
    }
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (auto* external = dynamic_cast<te::ExternalPlugin*> (at.plugin); external && external->getAudioPluginInstance() == nullptr)
        return Result::fail (external->isInitialisingAsync() ? "This plug-in is still loading. Try opening its editor again when loading completes."
                                                            : external->getLoadError().isNotEmpty() ? external->getLoadError() : "This plug-in could not be loaded.");
    at.plugin->windowState->showWindowExplicitly(); return Result::ok();
}
Result Session::setRackMacro (const String& rackId, int macro, float value)
{
    auto* rack = impl->rack (rackId);
    if (! rack) return Result::fail ("Rack not found.");
    if (auto frozen = impl->unfrozen (impl->trackOfPlugin (rackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto knob = rack->type->state.getChildWithName (macrosType).getChild (macro);
    if (! juce::isPositiveAndBelow (macro, rackMacroCount) || ! knob.isValid()) return Result::fail ("Macro not found.");
    if (! std::isfinite (value) || value < 0 || value > 1) return Result::fail ("Macros run from 0 to 1.");
    if (static_cast<float> (knob.getProperty (te::IDs::value, 0.0f)) == value) return Result::ok();
    impl->transaction ("Set " + knob[te::IDs::name].toString());
    knob.setProperty (te::IDs::value, value, &impl->edit->getUndoManager());
    impl->applyMacros (*rack->type);
    impl->changed(); return Result::ok();
}
Result Session::renameRackMacro (const String& rackId, int macro, const String& name)
{
    auto* rack = impl->rack (rackId);
    if (! rack) return Result::fail ("Rack not found.");
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto knob = rack->type->state.getChildWithName (macrosType).getChild (macro);
    if (! juce::isPositiveAndBelow (macro, rackMacroCount) || ! knob.isValid()) return Result::fail ("Macro not found.");
    const auto text = name.trim();
    if (text.isEmpty() || text.length() > 64) return Result::fail ("Macros need a name of up to 64 characters.");
    if (knob[te::IDs::name].toString() == text) return Result::ok();
    impl->transaction ("Rename macro"); knob.setProperty (te::IDs::name, text, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::mapRackMacro (const String& rackId, int macro, const String& pluginId, int parameter, float minimum, float maximum)
{
    auto* rack = impl->rack (rackId);
    if (! rack) return Result::fail ("Rack not found.");
    if (auto frozen = impl->unfrozen (impl->trackOfPlugin (rackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto knob = rack->type->state.getChildWithName (macrosType).getChild (macro);
    if (! juce::isPositiveAndBelow (macro, rackMacroCount) || ! knob.isValid()) return Result::fail ("Macro not found.");
    const auto at = impl->device (pluginId);
    auto* native = dynamic_cast<NativeEffectPlugin*> (at.plugin);
    if (at.rack != rack || native == nullptr) return Result::fail ("Macros map controls of native devices inside their own rack.");
    const auto kind = native->getKind();
    if (native->control (parameter) == nullptr) return Result::fail ("That device has no such control.");
    const auto& field = effectParameters (kind)[(size_t) parameter];
    if (! std::isfinite (minimum) || ! std::isfinite (maximum) || minimum < field.minimum || minimum > field.maximum
        || maximum < field.minimum || maximum > field.maximum) return Result::fail ("A mapping range must lie within the control's range.");
    auto mappings = macroMappings (native->state);
    for (const auto& m : mappings)
        if (m.parameter == parameter) return Result::fail (String (field.label) + " already follows Macro " + String (m.macro + 1) + ".");
    if (native->control (parameter)->getCurve().getNumPoints() > 0) return Result::fail ("Clear this control's automation before mapping it.");
    const auto low = effectValueToProportion (kind, parameter, minimum), high = effectValueToProportion (kind, parameter, maximum);
    // A macro's first mapping takes the control's current position, so mapping does not move it.
    bool first = true;
    for (auto* p : rack->type->getPlugins())
        for (const auto& m : macroMappings (p->state)) first = first && m.macro != macro;
    impl->transaction ("Map to " + knob[te::IDs::name].toString());
    auto* undo = &impl->edit->getUndoManager();
    if (first)
    {
        const auto position = effectValueToProportion (kind, parameter, native->readSettings().values[(size_t) parameter]);
        knob.setProperty (te::IDs::value, high == low ? 0.0f : juce::jlimit (0.0f, 1.0f, (position - low) / (high - low)), undo);
    }
    mappings.push_back ({ macro, parameter, low, high });
    native->state.setProperty (macroMapProperty, macroMapText (mappings), undo);
    impl->applyMacros (*rack->type);
    impl->changed(); return Result::ok();
}
Result Session::unmapRackMacro (const String& pluginId, int parameter)
{
    const auto at = impl->device (pluginId);
    auto* native = dynamic_cast<NativeEffectPlugin*> (at.plugin);
    if (native == nullptr || at.rack == nullptr) return Result::fail ("Mapped control not found.");
    if (auto frozen = impl->unfrozen (at.track); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto mappings = macroMappings (native->state);
    const auto before = mappings.size();
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [parameter] (const auto& m) { return m.parameter == parameter; }), mappings.end());
    if (mappings.size() == before) return Result::fail ("That control is not mapped to a macro.");
    impl->transaction ("Remove macro mapping");
    auto* undo = &impl->edit->getUndoManager();
    if (mappings.empty()) native->state.removeProperty (macroMapProperty, undo);
    else native->state.setProperty (macroMapProperty, macroMapText (mappings), undo);
    impl->changed(); return Result::ok();
}
Result Session::setPluginParameter (int t, int p, int parameter, float value)
{
    if (auto frozen = impl->unfrozen (impl->track (t)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* plugin = impl->plugin (t, p);
    if (! plugin || ! std::isfinite (value)) return Result::fail ("Plug-in parameter not found.");
    auto* processor = plugin->getWrappedAudioProcessor();
    if (! processor || ! juce::isPositiveAndBelow (parameter, processor->getParameters().size()))
        return Result::fail ("Plug-in parameter not found.");
    auto* param = processor->getParameters()[parameter];
    param->beginChangeGesture(); param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value)); param->endChangeGesture();
    impl->edit->pluginChanged (*plugin); impl->changed(); return Result::ok();
}
Result Session::showPluginEditor (int t, int p)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* plugin = impl->plugin (t, p); if (! plugin) return Result::fail ("Plug-in not found.");
    if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin))
    {
        if (external->isInitialisingAsync())
            return Result::fail ("This plug-in is still loading. Try opening its editor again when loading completes.");
        if (external->getAudioPluginInstance() == nullptr)
        {
            const auto error = external->getLoadError();
            return Result::fail (error.isNotEmpty() ? error : "This plug-in could not be loaded.");
        }
    }
    plugin->windowState->showWindowExplicitly();
    auto* window = dynamic_cast<PluginWindow*> (plugin->windowState->pluginWindow.get());
    if (window == nullptr || ! window->isVisible() || window->getContentComponent() == nullptr)
        return Result::fail ("This plug-in has no available editor.");
    return Result::ok();
}
void Session::closePluginEditors()
{
    if (impl->exporting) return;
    if (impl && impl->edit)
        for (auto* t : te::getAudioTracks (*impl->edit)) for (auto* p : Impl::allDevices (*t)) p->windowState->closeWindowExplicitly();
}
}
