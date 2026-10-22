#include "Session.h"
#include "SubtractiveSynth.h"
#include "UtilityProcessor.h"
#include "UtilityEditor.h"
#include "NativeEffects.h"
#include "Racks.h"
#include "NoteTools.h"
#include "Grooves.h"
#include "Tuner.h"
#include "Spectrum.h"
#include "MidiMonitor.h"
#include "WarpStretchers.h"
#include "InputPermission.h"
#include "MackieControl.h"
#include "PluginBinary.h"
#include "TempoDetection.h"
#include "VideoMedia.h"
#include <tracktion_engine/tracktion_engine.h>
#include <map>
#include <array>
#include <bitset>
#include <set>
#include <cstdlib>
#include <filesystem>

namespace halftrack
{
namespace te = tracktion;
namespace
{
using juce::File;
using juce::Result;
using juce::String;
const juce::Identifier monitorProperty { "halftrackMonitor" };
// The clip's last engine warp marker is Halftrack's end marker, not one of the clip's own; markers
// paused while warp is off resume with it.
// Take lanes: a track's lanes of takes, each holding clip states that do not play, and the lane a
// comped main-lane clip was copied from.
const juce::Identifier takeLanesType { "HALFTRACKTAKELANES" }, takeLaneType { "TAKELANE" }, compSourceProperty { "halftrackCompSource" };
const juce::Identifier repitchProperty { "halftrackRepitch" }; // Read by the Re-Pitch engine patch.
const juce::Identifier transposeProperty { "halftrackTranspose" }, detuneProperty { "halftrackDetune" };
const juce::Identifier implicitWarpEndProperty { "halftrackWarpEnd" }, warpMarkersPausedProperty { "halftrackWarpMarkersPaused" };
const juce::Identifier videoProperty { "halftrackVideo" }; // A video clip's movie, absolute or relative to the project.
const juce::Identifier recordQuantisationProperty { "halftrackRecordQuantisation" };
const juce::Identifier countInProperty { "halftrackCountInBars" };
const juce::Identifier inputTapProperty { "halftrackInputTap" }; // Read by the track-inputs engine patch.
const juce::Identifier sceneTempoProperty { "halftrackSceneTempo" }, sceneNumeratorProperty { "halftrackSceneNumerator" },
                       sceneDenominatorProperty { "halftrackSceneDenominator" };
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
const juce::Identifier noteChanceProperty { "halftrackChance" }, noteVelocityRangeProperty { "halftrackVelocityRange" };
const juce::Identifier returnProperty { "halftrackReturn" };
const juce::Identifier linkProperty { "halftrackLink" }; // Linked-track editing: tracks with the same value edit together.
const juce::Identifier hostedInstrumentProperty { "halftrackInstrument" }; // A hosted plug-in that is its track's instrument.
// A hosted plug-in's sidechain tap (InputTap, Post FX by default) and whether it is off; the
// engine's node builder reads both (see cmake/PatchSidechainTaps.cmake).
const juce::Identifier sidechainTapProperty { "halftrackSidechainTap" }, sidechainOffProperty { "halftrackSidechainOff" };
// A MIDI track playing to a MIDI port: the channel (1-16) its MIDI goes out on. The engine's node
// builder applies it (see cmake/PatchMidiPorts.cmake); its presence marks the output as a port.
const juce::Identifier midiOutChannelProperty { "halftrackMidiOutChannel" };
const juce::Identifier returnBusProperty { "halftrackReturnBus" };
const juce::Identifier returnModeProperty { "halftrackReturnSendMode" };
const juce::Identifier sendReturnProperty { "halftrackSendReturnID" };

const juce::Identifier manualVolumeProperty { "halftrackManualVolume" };
const juce::Identifier crossfadeProperty { "halftrackCrossfade" };
const juce::Identifier crossfaderProperty { "halftrackCrossfader" };
const juce::Identifier manualPanProperty { "halftrackManualPan" };
const juce::Identifier automationArmProperty { "halftrackAutomationArm" };
const juce::Identifier punchInProperty { "halftrackPunchIn" }, punchOutProperty { "halftrackPunchOut" };

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
// An external plug-in's own automatable parameters, in order; the engine's dry and wet levels are left out.
std::vector<te::AutomatableParameter*> externalParameters (te::Plugin& plugin)
{
    std::vector<te::AutomatableParameter*> result;
    for (auto* p : plugin.getAutomatableParameters())
        if (p->paramID != "dry level" && p->paramID != "wet level") result.push_back (p);
    return result;
}
// Lanes of an external plug-in's parameters that have points or are switched off, in 0-1 values.
std::vector<Session::PluginInfo::ControlAutomation> externalAutomation (te::Plugin& plugin)
{
    std::vector<Session::PluginInfo::ControlAutomation> result;
    const auto parameters = externalParameters (plugin);
    for (size_t i = 0; i < parameters.size(); ++i)
    {
        auto& curve = parameters[i]->getCurve();
        if (curve.getNumPoints() == 0 && ! curve.bypass.get()) continue;
        Session::PluginInfo::ControlAutomation entry { (int) i, {} };
        entry.lane.enabled = ! curve.bypass.get();
        for (int p = 0; p < curve.getNumPoints(); ++p)
            entry.lane.points.push_back ({ plugin.edit.tempoSequence.toBeats (curve.getPointTime (p)).inBeats(), curve.getPointValue (p) });
        result.push_back (std::move (entry));
    }
    return result;
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
// A hosted plug-in has a sidechain input when it is loaded and its first auxiliary input has channels.
bool hasSidechainInput (te::ExternalPlugin& plugin)
{
    if (plugin.getAudioPluginInstance() == nullptr) return false;
    const auto buses = plugin.getBusses();
    return buses.inputs.size() > 1 && buses.inputs[1].getNumChannels() > 0;
}
// A plug-in's sidechain as its state holds it. Without Halftrack's switch, a sidechain with a
// source is on, as the engine plays it.
Session::SidechainSettings sidechainSettings (const te::Plugin& plugin)
{
    Session::SidechainSettings settings;
    const bool sourced = plugin.getSidechainSourceID().isValid();
    if (sourced) settings.sourceTrackId = plugin.getSidechainSourceID().toString();
    settings.on = ! static_cast<bool> (plugin.state.getProperty (sidechainOffProperty, ! sourced));
    settings.tap = static_cast<Session::InputTap> (juce::jlimit (0, 2, static_cast<int> (plugin.state.getProperty (sidechainTapProperty, 1))));
    return settings;
}
// Connects the plug-in's track (wire sources 0-1) to its main input and the sidechain source
// (sources 2-3) to its first auxiliary input, folding a stereo pair onto a mono bus.
void wireSidechain (te::ExternalPlugin& plugin, juce::UndoManager* undo)
{
    const auto buses = plugin.getBusses();
    const int main = buses.inputs.empty() ? 0 : buses.inputs[0].getNumChannels();
    const int side = buses.inputs.size() > 1 ? buses.inputs[1].getNumChannels() : 0;
    for (int channel = 0; channel < 2; ++channel)
    {
        if (main > 0) plugin.makeConnection (channel, std::min (channel, main - 1), undo);
        if (side > 0) plugin.makeConnection (2 + channel, main + std::min (channel, side - 1), undo);
    }
}
// A native device as the snapshot reports it; a rack also reports its chains, devices and mappings.
Session::PluginInfo describeDevice (NativeEffectPlugin& native)
{
    Session::PluginInfo info { native.itemID.toString(), native.getName(), native.isEnabled() };
    info.isNativeEffect = true; info.effect = native.readSettings(); info.automation = deviceAutomation (native);
    if (native.getKind() == Session::EffectKind::sampler) info.sample = native.sampleFile();
    if (native.getKind() == Session::EffectKind::drumSampler)
        for (int pad = 0; pad < Session::drumPadCount; ++pad) info.padSamples.push_back (native.sampleFile (pad));
    if (auto* engine = native.rackEngine())
    {
        info.isRack = true;
        info.visibleMacros = juce::jlimit (1, Session::rackMacroCount, static_cast<int> (native.state.getProperty (racks::visibleMacrosProperty, 8)));
        for (int m = 0; m < Session::rackMacroCount; ++m) info.macroNames[(size_t) m] = native.state[racks::macroNameProperty (m)].toString();
        const auto devices = engine->plugins();
        for (const auto& child : native.state)
        {
            if (child.hasType (racks::mappingType)) { info.mappings.push_back (racks::readMapping (child)); continue; }
            if (! child.hasType (racks::chainType) && ! child.hasType (racks::returnType)) continue;
            Session::RackChainInfo chain { child[te::IDs::id].toString(), racks::readChain (child), {} };
            for (const auto& device : child)
                for (auto* d : devices)
                {
                    if (d->state != device) continue;
                    if (auto* inner = dynamic_cast<NativeEffectPlugin*> (d)) chain.devices.push_back (describeDevice (*inner));
                    else if (auto* utility = dynamic_cast<UtilityPlugin*> (d))
                    {
                        Session::PluginInfo entry { utility->itemID.toString(), utility->getName(), utility->isEnabled() };
                        entry.isUtility = true; entry.utility = utility->readSettings();
                        chain.devices.push_back (std::move (entry));
                    }
                }
            (child.hasType (racks::returnType) ? info.returns : info.chains).push_back (std::move (chain));
        }
    }
    return info;
}
// Device and chain identities inside racks must be unique across the project: the engine finds
// devices by identity, so a repeated one would play the wrong device.
Result validateRackIdentities (const juce::ValueTree& state)
{
    std::map<String, int> declared;
    const auto collect = [&] (auto&& self, const juce::ValueTree& node) -> void
    {
        if (node.hasProperty (te::IDs::id)) ++declared[node[te::IDs::id].toString()];
        for (const auto& child : node) self (self, child);
    };
    collect (collect, state);
    const auto check = [&] (auto&& self, const juce::ValueTree& node, bool inRack) -> bool
    {
        const bool chain = node.hasType (racks::chainType) || node.hasType (racks::returnType);
        if ((chain || (inRack && node.hasType (te::IDs::PLUGIN))) && declared[node[te::IDs::id].toString()] != 1) return false;
        for (const auto& child : node) if (! self (self, child, inRack || chain)) return false;
        return true;
    };
    if (! check (check, state, false)) return Result::fail ("The project contains a rack device or chain whose identity is not unique.");
    return Result::ok();
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
        // The fader's own level and pan, which the engine would otherwise load out of range and save back.
        if ((state.hasProperty (te::IDs::volume) && ! finiteProperty (state[te::IDs::volume], 0, 1))
            || (state.hasProperty (te::IDs::pan) && ! finiteProperty (state[te::IDs::pan], -1, 1)))
            return Result::fail ("Invalid mixer level or pan.");
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

// The undo step that freezes a track. Redoing it renders the freeze again.
const String freezeStep { "Freeze track" };

// Spans an edit that may freeze the given tracks, as the engine renders each freeze then.
// - Renders leave a Sends Only track's own output out of the mix, so such a track would freeze to
//   silence and be unfrozen. During the render it plays into Master outside history.
// - The engine's file cache can fail to open a freeze file before the render writes it, and then
//   retries only seconds later, so the frozen track would start silent. It reopens the file now.
class FreezeRender
{
public:
    explicit FreezeRender (const juce::Array<te::AudioTrack*>& mayFreeze) : tracks (mayFreeze)
    {
        for (auto* track : tracks)
            if (track->getOutput().outputsToNone())
            {
                auto device = track->state.getChildWithName (te::IDs::OUTPUTDEVICES).getChildWithName (te::IDs::DEVICE);
                outputs.push_back ({ device, device[te::IDs::name] });
                device.setProperty (te::IDs::name, te::DeviceManager::getDefaultAudioOutDeviceName (false), nullptr);
            }
    }
    ~FreezeRender()
    {
        for (auto& [device, name] : outputs) device.setProperty (te::IDs::name, name, nullptr);
        for (auto* track : tracks)
            if (track->isFrozen (te::Track::individualFreeze))
                track->edit.engine.getAudioFileManager().validateFile (te::AudioFile (track->edit.engine, te::TemporaryFileManager::getFreezeFileForTrack (*track)), true);
    }
private:
    juce::Array<te::AudioTrack*> tracks;
    std::vector<std::pair<juce::ValueTree, juce::var>> outputs;
    JUCE_DECLARE_NON_COPYABLE (FreezeRender)
};

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
        // A source track may send and play into another track; the engine drops an output that would feed back.
        if (returned)
            for (const auto& device : track.getChildWithName (te::IDs::OUTPUTDEVICES))
                if (device[te::IDs::name].toString().startsWithIgnoreCase ("track "))
                    return Result::fail ("Return tracks play into Master.");
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
        static_cast<Session::FadeCurve> (clip.getFadeInType()), static_cast<Session::FadeCurve> (clip.getFadeOutType()),
        static_cast<int> (clip.state.getProperty (transposeProperty, 0)), static_cast<float> (static_cast<double> (clip.state.getProperty (detuneProperty, 0.0))) };
}
bool validAudioSettings (const Session::AudioClipSettings& s, double length)
{
    const auto curve = [] (auto v) { return static_cast<int> (v) >= 1 && static_cast<int> (v) <= 4; };
    return std::isfinite (s.gainDb) && s.gainDb >= -100 && s.gainDb <= 24
        && std::isfinite (s.fadeInSeconds) && std::isfinite (s.fadeOutSeconds)
        && s.fadeInSeconds >= 0 && s.fadeOutSeconds >= 0
        && s.fadeInSeconds + s.fadeOutSeconds <= length + 1e-10
        && curve (s.fadeInCurve) && curve (s.fadeOutCurve)
        && s.transposeSemitones >= -48 && s.transposeSemitones <= 48 && std::isfinite (s.detuneCents) && s.detuneCents >= -50 && s.detuneCents <= 50;
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
        && ! clip.isUsingARA() && ! clip.getClipTrack()->isFrozen (te::Track::anyFreeze)
        && clip.state[videoProperty].toString().isEmpty(); // A movie's picture does not play backwards.
}
// Copying, cutting, pasting and duplicating take a MIDI clip whole, its loop and start with it.
bool canCarryMidiClip (const te::MidiClip& clip)
{
    return clip.getClipSlot() == nullptr && ! clip.isGrouped() && ! clip.hasAnyTakes() && clip.getSpeedRatio() == 1;
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
bool validSceneMeter (int numerator, int denominator)
{ return numerator >= 1 && numerator <= 99 && denominator >= 1 && denominator <= 16 && (denominator & (denominator - 1)) == 0; }
// A scene's tempo and meter are optional; a meter has both of its parts.
Result validateSceneSettings (const juce::ValueTree& state)
{
    for (const auto& scene : state.getChildWithName (te::IDs::SCENES))
    {
        if (! scene.hasType (te::IDs::SCENE)) continue;
        if (scene.hasProperty (sceneTempoProperty) && ! finiteProperty (scene[sceneTempoProperty], te::TempoSetting::minBPM, te::TempoSetting::maxBPM))
            return Result::fail ("A scene has an invalid tempo.");
        if (scene.hasProperty (sceneNumeratorProperty) != scene.hasProperty (sceneDenominatorProperty)
            || (scene.hasProperty (sceneNumeratorProperty)
                && (! finiteProperty (scene[sceneNumeratorProperty], 1, 99) || ! finiteProperty (scene[sceneDenominatorProperty], 1, 16)
                    || std::floor ((double) scene[sceneNumeratorProperty]) != (double) scene[sceneNumeratorProperty]
                    || ! validSceneMeter ((int) scene[sceneNumeratorProperty], (int) scene[sceneDenominatorProperty])
                    || (double) (int) scene[sceneDenominatorProperty] != (double) scene[sceneDenominatorProperty])))
            return Result::fail ("A scene has an invalid time signature.");
    }
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
    // Tempo points: the first at beat 0, beats increasing at least a 960th apart, curves from -1 to 1.
    double previous = -1; int tempos = 0;
    for (const auto& tempo : state.getChildWithName (te::IDs::TEMPOSEQUENCE))
        if (tempo.hasType (te::IDs::TEMPO))
        {
            const auto beat = tempo.getProperty (te::IDs::startBeat, 0);
            if (! finiteProperty (beat, 0, 1000000) || ! finiteProperty (tempo.getProperty (te::IDs::curve, 0), -1, 1) || ++tempos > 4096
                || (previous < 0 && static_cast<double> (beat) != 0) || (previous >= 0 && static_cast<double> (beat) - previous < 1.0 / 960 - 1e-12))
                return Result::fail ("The project's tempo map needs a first point at beat 0 and increasing beats.");
            previous = static_cast<double> (beat);
        }
    // One meter at the start: numerator 1-99 over 1, 2, 4, 8 or 16.
    int meters = 0;
    for (const auto& meter : state.getChildWithName (te::IDs::TEMPOSEQUENCE))
        if (meter.hasType (te::IDs::TIMESIG))
        {
            const auto numerator = meter.getProperty (te::IDs::numerator, 4), denominator = meter.getProperty (te::IDs::denominator, 4);
            const int d = denominator;
            if (++meters > 1 || ! finiteProperty (numerator, 1, 99) || static_cast<double> (numerator) != std::floor (static_cast<double> (numerator))
                || ! finiteProperty (denominator, 1, 16) || static_cast<double> (denominator) != d || (d & (d - 1)) != 0
                || ! finiteProperty (meter.getProperty (te::IDs::startBeat, 0), 0, 0))
                return Result::fail ("The project needs one time signature of 1 to 99 over 1, 2, 4, 8 or 16.");
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

// Clip envelope curves name the device they control by its ID and a parameter; the engine
// dereferences that device when connecting the curve, so one naming nothing is refused.
Result validateCurveModifiers (const juce::ValueTree& state)
{
    std::set<String> ids;
    std::function<void (const juce::ValueTree&)> gather = [&] (const juce::ValueTree& v)
    { if (v.hasType (te::IDs::PLUGIN) && v.hasProperty (te::IDs::id)) ids.insert (v[te::IDs::id].toString()); for (const auto& child : v) gather (child); };
    gather (state);
    std::function<bool (const juce::ValueTree&)> valid = [&] (const juce::ValueTree& v)
    {
        if (v.hasType (te::IDs::AUTOMATIONCURVEMODIFIER))
        {
            const auto source = v[te::IDs::source].toString();
            if (! source.containsOnly ("0123456789") || source.isEmpty() || ! ids.contains (source) || v[te::IDs::paramID].toString().isEmpty()) return false;
        }
        for (const auto& child : v) if (! valid (child)) return false;
        return true;
    };
    return valid (state) ? Result::ok() : Result::fail ("The project contains a clip envelope for a device it does not have.");
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
    if (state.hasType (te::IDs::NOTE)
        && ((state.hasProperty (noteChanceProperty) && ! finiteProperty (state[noteChanceProperty], 0, 1))
            || (state.hasProperty (noteVelocityRangeProperty)
                && (! finiteProperty (state[noteVelocityRangeProperty], -127, 127)
                    || std::floor ((double) state[noteVelocityRangeProperty]) != (double) state[noteVelocityRangeProperty]))))
        return Result::fail ("The project contains an invalid MIDI note chance or velocity range.");
    // MPE note expression: points at finite beats inside their ranges.
    if (state.getParent().hasType (te::IDs::NOTE) && te::MidiExpression::isExpression (state.getType()))
    {
        const bool bend = state.hasType (te::IDs::PITCHBEND);
        if (! finiteProperty (state[te::IDs::b], 0, 1000000) || ! finiteProperty (state[te::IDs::v], bend ? -48 : 0, bend ? 48 : 1))
            return Result::fail ("The project contains an invalid MIDI note expression.");
    }
    // Clip envelope curves: values inside their curve's range and finite timing.
    if (state.hasType (te::IDs::AUTOMATIONCURVE) && state.getParent().hasType (te::IDs::AUTOMATIONCURVEMODIFIER))
    {
        const auto type = te::curveModifierTypeFromString (state[te::IDs::type].toString());
        const auto limits = state.getParent()[te::IDs::absoluteLimits].toString();
        const double low = type == te::CurveModifierType::relative ? -0.5 : type == te::CurveModifierType::scale ? 0 : limits.upToFirstOccurrenceOf (" ", false, false).getDoubleValue();
        const double high = type == te::CurveModifierType::relative ? 0.5 : type == te::CurveModifierType::scale ? 1 : limits.fromFirstOccurrenceOf (" ", false, false).getDoubleValue();
        bool valid = type.has_value() && low <= high;
        for (const auto& key : { te::IDs::start, te::IDs::length, te::IDs::loopStart, te::IDs::loopLength })
            valid = valid && (! state.hasProperty (key) || finiteProperty (state[key], 0, 1000000));
        for (const auto& point : state)
            valid = valid && (! point.hasType (te::IDs::POINT) || (finiteProperty (point[te::IDs::t], 0, 1000000) && finiteProperty (point[te::IDs::v], low, high)
                                                                   && (! point.hasProperty (te::IDs::c) || finiteProperty (point[te::IDs::c], -1, 1))));
        if (! valid) return Result::fail ("The project contains an invalid clip envelope.");
    }
    if (state.hasType (te::IDs::AUDIOCLIP))
    {
        // Transposition and the warp modes' settings.
        bool pitch = (! state.hasProperty (transposeProperty) || (finiteProperty (state[transposeProperty], -48, 48)
                                                                  && std::floor ((double) state[transposeProperty]) == (double) state[transposeProperty]))
                  && (! state.hasProperty (detuneProperty) || finiteProperty (state[detuneProperty], -50, 50));
        if (const auto options = state[te::IDs::elastiqueOptions].toString(); options.startsWith ("2/"))
        {
            const auto tokens = juce::StringArray::fromTokens (options, "/", {});
            pitch = pitch && tokens.size() == 9;
            for (int i = 5; pitch && i < 9; ++i) pitch = finiteProperty (tokens[i], 0, 100);
        }
        if (! pitch) return Result::fail ("The project contains an invalid clip transposition or warp setting.");
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
// A link is a short identifier without spaces, on an audio or MIDI track.
// A hosted instrument is a hosted plug-in, one at most on a MIDI track.
Result validateHostedInstruments (const juce::ValueTree& state)
{
    if (state.hasType (te::IDs::TRACK))
    {
        int hosted = 0;
        for (const auto& plugin : state)
            if (plugin.hasType (te::IDs::PLUGIN) && plugin.hasProperty (hostedInstrumentProperty))
                if (++hosted > 1 || plugin[te::IDs::type].toString() != te::ExternalPlugin::xmlTypeName || ! static_cast<bool> (state.getProperty (midiTrackProperty, false)))
                    return Result::fail ("The project contains an invalid plug-in instrument.");
    }
    for (const auto& child : state) if (auto result = validateHostedInstruments (child); result.failed()) return result;
    return Result::ok();
}
// A MIDI port output's channel is 1-16, on a MIDI track.
Result validateMidiPortOutputs (const juce::ValueTree& state)
{
    if (state.hasProperty (midiOutChannelProperty))
    {
        const auto& value = state[midiOutChannelProperty];
        const int channel = static_cast<int> (value);
        if (! state.hasType (te::IDs::TRACK) || ! static_cast<bool> (state.getProperty (midiTrackProperty, false)) || ! (value.isInt() || value.isInt64() || value.toString().containsOnly ("0123456789"))
            || channel < 1 || channel > 16)
            return Result::fail ("The project contains an invalid MIDI port output.");
    }
    for (const auto& child : state) if (auto result = validateMidiPortOutputs (child); result.failed()) return result;
    return Result::ok();
}
// A sidechain names an audio or MIDI track of the project, other than a return, with a known tap.
Result validateSidechains (const juce::ValueTree& edit)
{
    std::set<juce::String> tracks;
    std::function<void (const juce::ValueTree&)> collect = [&] (const juce::ValueTree& v)
    {
        if (v.hasType (te::IDs::TRACK) && ! static_cast<bool> (v.getProperty (returnProperty, false))) tracks.insert (v[te::IDs::id].toString());
        for (const auto& child : v) collect (child);
    };
    collect (edit);
    std::function<Result (const juce::ValueTree&)> check = [&] (const juce::ValueTree& v) -> Result
    {
        if (v.hasType (te::IDs::PLUGIN))
        {
            const auto source = te::EditItemID::fromVar (v[te::IDs::sidechainSourceID]);
            if (source.isValid() && tracks.count (source.toString()) == 0) return Result::fail ("The project contains a sidechain from a missing track.");
            if (v.hasProperty (sidechainTapProperty) && ! juce::isPositiveAndBelow (static_cast<int> (v[sidechainTapProperty]), 3))
                return Result::fail ("The project contains an invalid sidechain tap.");
        }
        for (const auto& child : v) if (auto result = check (child); result.failed()) return result;
        return Result::ok();
    };
    return check (edit);
}
Result validateTrackLinks (const juce::ValueTree& state)
{
    if (state.hasProperty (linkProperty))
    {
        const auto link = state[linkProperty].toString();
        if (! state.hasType (te::IDs::TRACK) || static_cast<bool> (state.getProperty (returnProperty, false)) || link.isEmpty() || link.length() > 64 || link.containsAnyOf (" \t\r\n"))
            return Result::fail ("The project contains an invalid track link.");
    }
    for (const auto& child : state) if (auto result = validateTrackLinks (child); result.failed()) return result;
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

// A warp marker's engine time is the clip's content time at its own tempo: beat * 60 / bpm.
// Halftrack adds a final marker at the source's end that keeps the clip's tempo after the last
// marker; it is not one of the clip's markers.
std::vector<Session::WarpMarker> warpMarkers (te::WaveAudioClip& clip)
{
    std::vector<Session::WarpMarker> result;
    if (! clip.getWarpTime()) return result;
    const auto bpm = std::max (1.0, clip.getLoopInfo().getBpm (clip.getWaveInfo()));
    const auto& markers = clip.getWarpTimeManager().getMarkers();
    for (int i = 0; i < markers.size(); ++i)
    {
        if (i == markers.size() - 1 && static_cast<bool> (clip.state.getProperty (implicitWarpEndProperty, false))) break;
        result.push_back ({ markers[i]->sourceTime.inSeconds(), markers[i]->warpTime.inSeconds() * bpm / 60 });
    }
    return result;
}
Session::WarpSettings warpSettings (te::WaveAudioClip& clip)
{
    Session::WarpSettings result;
    const auto source = clip.getSourceLength().inSeconds();
    result.bpm = Session::estimateClipTempo (source);
    if (! clip.getAutoTempo()) return result;
    using Mode = Session::WarpMode;
    const auto stretcher = clip.getTimeStretchMode();
    const auto values = clip.elastiqueProOptions.get().halftrack;
    result.mode = static_cast<bool> (clip.state.getProperty (repitchProperty, false)) ? Mode::repitch
                : stretcher == te::TimeStretcher::signalsmithCheaper ? Mode::efficient
                : stretcher == te::TimeStretcher::halftrackBeats ? Mode::beats
                : stretcher == te::TimeStretcher::halftrackTones ? Mode::tones
                : stretcher == te::TimeStretcher::halftrackTexture ? Mode::texture
                : stretcher == te::TimeStretcher::halftrackComplexPro ? Mode::complexPro : Mode::stretch;
    if (result.mode == Mode::beats)
    {
        result.transientLoop = static_cast<Session::TransientLoop> (juce::jlimit (0, 2, (int) values[warp::beats::loopMode]));
        result.transientEnvelope = 100 - values[warp::beats::fade];
    }
    if (result.mode == Mode::tones || result.mode == Mode::texture) result.grainSize = values[warp::grains::size];
    if (result.mode == Mode::texture) result.flux = values[warp::grains::flux];
    if (result.mode == Mode::complexPro) result.formants = 100 - values[warp::complexPro::formantShift];
    if (const auto bpm = clip.getLoopInfo().getBpm (clip.getWaveInfo()); std::isfinite (bpm) && bpm > 0) result.bpm = bpm;
    return result;
}
// Clip scales.
const juce::Identifier scaleEnabledProperty { "halftrackScale" };
const juce::Identifier scaleRootProperty { "halftrackScaleRoot" };
const juce::Identifier scaleModeProperty { "halftrackScaleMode" };
Session::ScaleSettings clipScale (const juce::ValueTree& state)
{
    Session::ScaleSettings scale;
    scale.enabled = state.getProperty (scaleEnabledProperty, false);
    scale.root = juce::jlimit (0, 11, static_cast<int> (state.getProperty (scaleRootProperty, 0)));
    scale.mode = juce::jlimit (0, Session::scaleModeCount - 1, static_cast<int> (state.getProperty (scaleModeProperty, 0)));
    return scale;
}
Result validateClipScales (const juce::ValueTree& state)
{
    if (state.hasProperty (scaleEnabledProperty) || state.hasProperty (scaleRootProperty) || state.hasProperty (scaleModeProperty))
    {
        const auto whole = [] (const juce::var& v, double low, double high) { return finiteProperty (v, low, high) && static_cast<double> (v) == std::floor (static_cast<double> (v)); };
        if (! state.hasType (te::IDs::MIDICLIP) || ! whole (state.getProperty (scaleEnabledProperty, 0), 0, 1)
            || ! whole (state.getProperty (scaleRootProperty, 0), 0, 11) || ! whole (state.getProperty (scaleModeProperty, 0), 0, Session::scaleModeCount - 1))
            return Result::fail ("The project contains an invalid clip scale.");
    }
    for (const auto& child : state) if (auto result = validateClipScales (child); result.failed()) return result;
    return Result::ok();
}
// The groove pool: a Halftrack child of the Edit holding the Global Amount and the grooves, and
// each MIDI clip's groove id.
const juce::Identifier groovePoolType { "HALFTRACKGROOVEPOOL" }, grooveType { "HALFTRACKGROOVE" }, grooveEventType { "HALFTRACKGROOVEEVENT" };
const juce::Identifier clipGrooveProperty { "halftrackGroove" };
float grooveAmount (const juce::ValueTree& editState)
{
    const auto pool = editState.getChildWithName (groovePoolType);
    return juce::jlimit (0.0f, Session::maximumGrooveAmount, static_cast<float> (pool.getProperty ("amount", 100.0f)));
}
Session::GrooveInfo readGroove (const juce::ValueTree& v)
{
    Session::GrooveInfo g;
    g.id = v["id"].toString(); g.name = v["name"].toString();
    g.base = static_cast<Session::GrooveBase> (juce::jlimit (0, Session::grooveBaseCount - 1, static_cast<int> (v.getProperty ("base", 3))));
    g.lengthBeats = v.getProperty ("length", 4.0);
    g.quantize = v.getProperty ("quantize", 0.0f); g.timing = v.getProperty ("timing", 100.0f);
    g.random = v.getProperty ("random", 0.0f); g.velocity = v.getProperty ("velocity", 0.0f);
    for (const auto& e : v)
        if (e.hasType (grooveEventType)) g.events.push_back ({ static_cast<double> (e["beat"]), static_cast<float> (e.getProperty ("velocity", 127.0f)) });
    return g;
}
juce::ValueTree grooveTree (const Session::GrooveInfo& g)
{
    juce::ValueTree v (grooveType);
    v.setProperty ("id", g.id, nullptr); v.setProperty ("name", g.name, nullptr); v.setProperty ("base", static_cast<int> (g.base), nullptr);
    v.setProperty ("length", g.lengthBeats, nullptr);
    v.setProperty ("quantize", g.quantize, nullptr); v.setProperty ("timing", g.timing, nullptr);
    v.setProperty ("random", g.random, nullptr); v.setProperty ("velocity", g.velocity, nullptr);
    for (const auto& e : g.events)
    {
        juce::ValueTree event (grooveEventType);
        event.setProperty ("beat", e.beat, nullptr); event.setProperty ("velocity", e.velocity, nullptr);
        v.appendChild (event, nullptr);
    }
    return v;
}
juce::ValueTree grooveState (const juce::ValueTree& editState, const String& id)
{
    if (id.isEmpty()) return {};
    for (const auto& g : editState.getChildWithName (groovePoolType))
        if (g.hasType (grooveType) && g["id"].toString() == id) return g;
    return {};
}
std::optional<grooves::Pattern> clipGroovePattern (const te::MidiClip& clip)
{
    const auto state = grooveState (clip.edit.state, clip.state[clipGrooveProperty].toString());
    if (! state.isValid()) return std::nullopt;
    grooves::Pattern pattern (readGroove (state), grooveAmount (clip.edit.state));
    if (pattern.neutral()) return std::nullopt;
    return pattern;
}
// A note's placement under its clip's groove, from its beat in the clip's own sequence.
grooves::Pattern::Placement placeNote (const grooves::Pattern& pattern, const te::MidiClip& clip, double beat, int pitch, int velocity)
{
    return pattern.place (beat, velocity, grooves::noteSeed (clip.itemID.getRawID(), pitch, beat));
}
// Grooved playback: the clip's notes are moved and accented in a copy of the list being played.
// A looped list repeats the clip's loop from beat 0, so each note is placed by its beat in the clip.
juce::ValueTree groovedSequence (const grooves::Pattern& pattern, const te::MidiClip& clip, const te::MidiList& list, bool looped)
{
    auto copy = list.state.createCopy();
    const auto loopStart = clip.getLoopStartBeats().inBeats(), loopLength = clip.getLoopLengthBeats().inBeats();
    for (auto note : copy)
        if (note.hasType (te::IDs::NOTE))
        {
            const double beat = note[te::IDs::b];
            const auto inClip = looped && loopLength > 0 ? loopStart + beat - std::floor (beat / loopLength) * loopLength : beat;
            const auto placed = placeNote (pattern, clip, inClip, static_cast<int> (note[te::IDs::p]), static_cast<int> (note[te::IDs::v]));
            note.setProperty (te::IDs::b, beat + (placed.beat - inClip), nullptr);
            note.setProperty (te::IDs::v, placed.velocity, nullptr);
        }
    return copy;
}
Result validateGroovePool (const juce::ValueTree& state)
{
    std::set<String> ids;
    int pools = 0;
    for (const auto& pool : state)
    {
        if (! pool.hasType (groovePoolType)) continue;
        if (++pools > 1 || (pool.hasProperty ("amount") && ! finiteProperty (pool["amount"], 0, Session::maximumGrooveAmount)) || pool.getNumChildren() > Session::maximumGrooves)
            return Result::fail ("The project's groove pool is invalid.");
        for (const auto& g : pool)
        {
            if (! g.hasType (grooveType)) return Result::fail ("The project's groove pool is invalid.");
            const auto id = g["id"].toString();
            bool numbers = finiteProperty (g["base"], 0, Session::grooveBaseCount - 1) && static_cast<double> (g["base"]) == std::floor (static_cast<double> (g["base"]))
                && finiteProperty (g["length"], 0, 1024) && finiteProperty (g["quantize"], 0, 100) && finiteProperty (g["timing"], 0, 100)
                && finiteProperty (g["random"], 0, 100) && finiteProperty (g["velocity"], -100, 100);
            for (const auto& e : g)
                numbers = numbers && e.hasType (grooveEventType) && finiteProperty (e["beat"], 0, 1024) && finiteProperty (e["velocity"], 1, 127);
            if (id.isEmpty() || ! ids.insert (id).second || ! numbers || ! grooves::valid (readGroove (g)))
                return Result::fail ("The project contains an invalid groove.");
        }
    }
    const auto clips = [&] (auto&& self, const juce::ValueTree& node) -> Result
    {
        if (node.hasProperty (clipGrooveProperty))
        {
            const auto id = node[clipGrooveProperty].toString();
            if (! node.hasType (te::IDs::MIDICLIP) || (id.isNotEmpty() && ! ids.contains (id)))
                return Result::fail ("A clip uses a groove that is not in the groove pool.");
        }
        for (const auto& child : node) if (auto result = self (self, child); result.failed()) return result;
        return Result::ok();
    };
    return clips (clips, state);
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
// MPE note expression, kept as the engine's PITCHBEND, TIMBRE (slide) and PRESSURE children of a
// note, each point at beats from the note's start.
const juce::Identifier& expressionType (Session::NoteExpression kind)
{ return kind == Session::NoteExpression::pitchBend ? te::IDs::PITCHBEND : kind == Session::NoteExpression::slide ? te::IDs::TIMBRE : te::IDs::PRESSURE; }
std::pair<float, float> expressionRange (Session::NoteExpression kind)
{ return kind == Session::NoteExpression::pitchBend ? std::pair { -48.0f, 48.0f } : std::pair { 0.0f, 1.0f }; }
std::vector<Session::ExpressionPoint> noteExpression (const juce::ValueTree& note, Session::NoteExpression kind)
{
    std::vector<Session::ExpressionPoint> result;
    for (const auto& child : note)
        if (child.hasType (expressionType (kind))) result.push_back ({ static_cast<double> (child[te::IDs::b]), static_cast<float> (static_cast<double> (child[te::IDs::v])) });
    std::stable_sort (result.begin(), result.end(), [] (const auto& a, const auto& b) { return a.beat < b.beat; });
    return result;
}
void writeNoteExpression (juce::ValueTree note, Session::NoteExpression kind, const std::vector<Session::ExpressionPoint>& points, juce::UndoManager* undo)
{
    for (int i = note.getNumChildren(); --i >= 0;) if (note.getChild (i).hasType (expressionType (kind))) note.removeChild (i, undo);
    for (const auto& point : points)
        note.appendChild (juce::ValueTree (expressionType (kind), { { te::IDs::b, point.beat }, { te::IDs::v, point.value } }), undo);
}
bool validExpression (const std::vector<Session::ExpressionPoint>& points, Session::NoteExpression kind, double length)
{
    const auto [low, high] = expressionRange (kind);
    double previous = -1;
    for (const auto& p : points)
    {
        if (! std::isfinite (p.beat) || p.beat < 0 || p.beat > length + 1e-9 || p.beat <= previous || ! std::isfinite (p.value) || p.value < low || p.value > high) return false;
        previous = p.beat;
    }
    return points.size() <= 4096;
}
// A clip plays as MPE while any of its notes has expression.
void updateMpeMode (te::MidiClip& clip)
{
    bool any = false;
    for (auto* note : clip.getSequence().getNotes()) any = any || te::MidiExpression::noteHasExpression (note->state);
    if (clip.getMPEMode() != any) clip.setMPEMode (any);
}
// Clip envelopes sit on the engine's clip curve modifiers, one per control. Each holds an absolute,
// a relative and a scaling curve: automation uses the first, volume modulation scales and other
// modulation offsets, so a control's automation and modulation keep separate curves and timing.
te::CurveModifierType envelopeCurveType (const Session::EnvelopeTarget& target, Session::EnvelopeMode mode)
{
    if (mode == Session::EnvelopeMode::automation) return te::CurveModifierType::absolute;
    return target.pluginId.isEmpty() && target.parameter == 0 ? te::CurveModifierType::scale : te::CurveModifierType::relative;
}
// The control an envelope target names on a track, and the device holding it (none for the mixer).
te::AutomatableParameter* envelopeParameter (te::AudioTrack& track, const Session::EnvelopeTarget& target, te::Plugin** device = nullptr)
{
    if (target.pluginId.isEmpty())
    {
        auto* mixer = track.getVolumePlugin();
        if (mixer == nullptr || (target.parameter != 0 && target.parameter != 1)) return nullptr;
        return &automationParameter (*mixer, target.parameter == 0 ? Session::AutomationTarget::volume : Session::AutomationTarget::pan);
    }
    for (auto* p : track.pluginList.getPlugins())
        if (p->itemID.toString() == target.pluginId)
        {
            if (device != nullptr) *device = p;
            if (auto* native = dynamic_cast<NativeEffectPlugin*> (p)) return native->control (target.parameter);
            if (dynamic_cast<te::ExternalPlugin*> (p) == nullptr) return nullptr;
            const auto parameters = externalParameters (*p);
            return juce::isPositiveAndBelow (target.parameter, (int) parameters.size()) ? parameters[(size_t) target.parameter] : nullptr;
        }
    return nullptr;
}
// The values an envelope takes, in Session's units; both ends are allowed.
std::pair<float, float> envelopeRange (const Session::EnvelopeTarget& target, Session::EnvelopeMode mode, te::Plugin* device)
{
    const bool volume = target.pluginId.isEmpty() && target.parameter == 0;
    if (mode == Session::EnvelopeMode::modulation) return volume ? std::pair { 0.0f, 1.0f } : std::pair { -1.0f, 1.0f };
    if (target.pluginId.isEmpty()) return volume ? std::pair { -100.0f, 6.0f } : std::pair { -1.0f, 1.0f };
    if (auto* native = dynamic_cast<NativeEffectPlugin*> (device))
    { const auto& field = effectParameters (native->getKind())[(size_t) target.parameter]; return { field.minimum, field.maximum }; }
    return { 0.0f, 1.0f };
}
float envelopeToCurve (const Session::EnvelopeTarget& target, Session::EnvelopeMode mode, te::Plugin* device, float value)
{
    const bool volume = target.pluginId.isEmpty() && target.parameter == 0;
    if (mode == Session::EnvelopeMode::modulation) return volume ? value : value * 0.5f;
    if (target.pluginId.isEmpty()) return volume ? te::decibelsToVolumeFaderPosition (value) : value;
    if (auto* native = dynamic_cast<NativeEffectPlugin*> (device)) return effectValueToProportion (native->getKind(), target.parameter, value);
    return value;
}
float curveToEnvelope (const Session::EnvelopeTarget& target, Session::EnvelopeMode mode, te::Plugin* device, float value)
{
    const bool volume = target.pluginId.isEmpty() && target.parameter == 0;
    if (mode == Session::EnvelopeMode::modulation) return volume ? value : value * 2.0f;
    if (target.pluginId.isEmpty()) return volume ? te::volumeFaderPositionToDB (value) : value;
    if (auto* native = dynamic_cast<NativeEffectPlugin*> (device)) return effectProportionToValue (native->getKind(), target.parameter, value);
    return value;
}
// The target a clip curve modifier controls on the clip's track, if it is one Session names.
std::optional<Session::EnvelopeTarget> envelopeTargetOf (te::AudioTrack& track, const te::AutomatableParameterID& id, te::Plugin** device)
{
    if (auto* mixer = track.getVolumePlugin(); mixer != nullptr && mixer->itemID == id.automatableEditItemID)
    {
        if (id.paramID == mixer->volParam->paramID) return Session::EnvelopeTarget { {}, 0 };
        if (id.paramID == mixer->panParam->paramID) return Session::EnvelopeTarget { {}, 1 };
        return std::nullopt;
    }
    for (auto* p : track.pluginList.getPlugins())
        if (p->itemID == id.automatableEditItemID)
        {
            *device = p;
            if (auto* native = dynamic_cast<NativeEffectPlugin*> (p))
            {
                for (int i = 0; i < native->numControls(); ++i) if (native->control (i)->paramID == id.paramID) return Session::EnvelopeTarget { p->itemID.toString(), i };
            }
            else if (dynamic_cast<te::ExternalPlugin*> (p) != nullptr)
            {
                const auto parameters = externalParameters (*p);
                for (size_t i = 0; i < parameters.size(); ++i) if (parameters[i]->paramID == id.paramID) return Session::EnvelopeTarget { p->itemID.toString(), (int) i };
            }
            return std::nullopt;
        }
    return std::nullopt;
}
std::vector<Session::ClipEnvelope> clipEnvelopes (te::Clip& clip)
{
    std::vector<Session::ClipEnvelope> result;
    auto* track = dynamic_cast<te::AudioTrack*> (clip.getTrack());
    auto* list = track != nullptr && clip.state.getChildWithName (te::IDs::AUTOMATIONCURVES).getNumChildren() > 0 ? clip.getAutomationCurveList (false) : nullptr;
    if (list == nullptr) return result;
    for (auto modifier : list->getItems())
    {
        te::Plugin* device = nullptr;
        const auto target = envelopeTargetOf (*track, modifier->getDestID(), &device);
        if (! target) continue;
        for (const auto mode : { Session::EnvelopeMode::modulation, Session::EnvelopeMode::automation })
        {
            const auto type = envelopeCurveType (*target, mode);
            auto& curve = modifier->getCurve (type).curve;
            if (curve.getNumPoints() == 0) continue;
            Session::ClipEnvelope envelope { *target, mode, {} };
            for (int i = 0; i < curve.getNumPoints(); ++i)
                envelope.points.push_back ({ te::toBeats (curve.getPointPosition (i), clip.edit.tempoSequence).inBeats(),
                                             curveToEnvelope (*target, mode, device, curve.getPointValue (i)) });
            auto& timing = modifier->getCurveTiming (type);
            envelope.unlinked = timing.unlinked.get();
            envelope.loopBeats = te::BeatDuration (timing.loopLength.get()).inBeats();
            result.push_back (std::move (envelope));
        }
    }
    return result;
}
Session::ClipInfo describeClip (te::Clip& clip)
{
    Session::ClipInfo result;
    const auto position = clip.getPosition();
    result.id = clip.itemID.toString(); result.name = clip.getName(); result.active = !clip.isMuted();
    result.start = position.time.getStart().inSeconds(); result.length = position.time.getLength().inSeconds();
    result.sourceOffset = position.offset.inSeconds();
    const auto beats = clip.edit.tempoSequence.toBeats (position.time);
    result.startBeat = beats.getStart().inBeats(); result.lengthBeats = beats.getLength().inBeats();
    result.follow = clipFollowSettings (clip.state);
    result.launch = clipLaunchSettings (clip.state);
    result.scale = clipScale (clip.state);
    result.grooveId = clip.state[clipGrooveProperty].toString();
    result.envelopes = clipEnvelopes (clip);
    if (clip.usesSourceFile()) result.file = clip.getSourceFileReference().getFile();
    if (auto* audio = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        result.mediaMissing = ! result.file.existsAsFile();
        result.isAudio = true; result.audio = audioSettings (*audio);
        result.sourceLength = audio->getSourceLength().inSeconds(); result.canEditAudioRange = canEditAudioRange (*audio);
        result.canEditAudioFades = audio->getClipSlot() == nullptr;
        result.reversed = audio->getIsReversed(); result.canReverse = canReverseAudio (*audio);
        result.warp = warpSettings (*audio);
        result.canWarp = ! audio->getIsReversed() && ! audio->isUsingARA() && ! audio->isGrouped() && audio->state[videoProperty].toString().isEmpty();
        result.warpMarkers = warpMarkers (*audio);
        if (const auto movie = audio->state[videoProperty].toString(); movie.isNotEmpty()) result.videoFile = clip.edit.filePathResolver (movie);
    }
    if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
    {
        result.isMidi = true; result.canEditMidiClip = canCarryMidiClip (*midi);
        for (auto* note : midi->getSequence().getNotes())
            result.notes.push_back ({ note->state.getProperty (midiNoteIDProperty).toString(), note->getNoteNumber(),
                                     note->getStartBeat().inBeats(), note->getLengthBeats().inBeats(), note->getVelocity() / 127.0f, !note->isMute(),
                                     static_cast<float> (static_cast<double> (note->state.getProperty (noteChanceProperty, 1.0))),
                                     static_cast<int> (note->state.getProperty (noteVelocityRangeProperty, 0)),
                                     noteExpression (note->state, Session::NoteExpression::pitchBend), noteExpression (note->state, Session::NoteExpression::slide),
                                     noteExpression (note->state, Session::NoteExpression::pressure) });
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
    // Content beats: what plays at the clip's start, and the loop. Looping, the engine's offset
    // counts from the loop's start.
    if (const auto* wave = dynamic_cast<te::WaveAudioClip*> (&clip); result.isMidi || (wave != nullptr && wave->getAutoTempo()))
    {
        result.canLoop = true;
        const auto offset = clip.getOffsetInBeats().inBeats();
        if (clip.isLooping())
        {
            result.loop = { true, clip.getLoopStartBeats().inBeats(), clip.getLoopLengthBeats().inBeats() };
            result.contentStartBeat = result.loop.start + std::fmod (offset, result.loop.length);
        }
        else
        {
            result.contentStartBeat = offset;
            result.loop = { false, offset, clip.edit.tempoSequence.toBeats (position.time).getLength().inBeats() };
        }
    }
    return result;
}
// A take lane clip, from its saved state: its place, source file or notes.
Session::ClipInfo describeLaneClip (const juce::ValueTree& state, te::Edit& edit)
{
    Session::ClipInfo result;
    result.id = state[te::IDs::id].toString(); result.name = state[te::IDs::name].toString();
    result.start = state[te::IDs::start]; result.length = state[te::IDs::length]; result.sourceOffset = state[te::IDs::offset];
    const auto beats = edit.tempoSequence.toBeats (te::TimeRange (te::TimePosition::fromSeconds (result.start), te::TimeDuration::fromSeconds (result.length)));
    result.startBeat = beats.getStart().inBeats(); result.lengthBeats = beats.getLength().inBeats();
    result.active = ! static_cast<bool> (state.getProperty (te::IDs::mute, false));
    if (state.hasType (te::IDs::AUDIOCLIP))
    {
        result.isAudio = true;
        result.file = te::SourceFileReference::findFileFromString (edit, state[te::IDs::source].toString());
        result.mediaMissing = ! result.file.existsAsFile();
    }
    else if (state.hasType (te::IDs::MIDICLIP))
    {
        result.isMidi = true;
        for (auto note : state.getChildWithName (te::IDs::SEQUENCE))
            if (note.hasType (te::IDs::NOTE))
                result.notes.push_back ({ note.getProperty (midiNoteIDProperty).toString(), static_cast<int> (note[te::IDs::p]), static_cast<double> (note[te::IDs::b]),
                                          static_cast<double> (note[te::IDs::l]), static_cast<int> (note[te::IDs::v]) / 127.0f, true });
    }
    return result;
}

class Behaviour final : public te::EngineBehaviour
{
public:
    Behaviour (bool devices, juce::String portPrefix) : useDevices (devices), midiPortPrefix (std::move (portPrefix)) {}
    bool autoInitialiseDeviceManager() override { return useDevices; }
    bool shouldOpenAudioInputByDefault() override { return false; }
    // Without devices, only the MIDI ports a test makes (named with its prefix) are used.
    bool usePhysicalMidiDevices() override { return useDevices || midiPortPrefix.isNotEmpty(); }
    bool shouldUsePhysicalMidiDevice (const juce::MidiDeviceInfo& info, bool) override { return midiPortPrefix.isEmpty() || info.name.startsWith (midiPortPrefix); }
    // As in the reference, a MIDI output plays nothing until its Track or Sync switch is on.
    bool shouldOpenMidiOutputByDefault() override { return false; }
    bool shouldOpenMidiInputByDefault() override { return false; }
    bool addSystemAudioIODeviceTypes() override { return useDevices; }
    bool canScanPluginsOutOfProcess() override { return useDevices; }
    // Pre sends and soloed returns still need content from a dry-muted source.
    bool shouldProcessMutedTracks() override { return true; }
    // A beat is a quarter note at every meter: 6/8 is three beats per bar, and the tempo counts quarter notes.
    bool lengthOfOneBeatDependsOnTimeSignature() override { return false; }
    // Device chains are open-ended; the engine's 16-plugin default includes the mixer and meter.
    te::EditLimits getEditLimits() override { te::EditLimits limits; limits.maxPluginsOnTrack = 64; return limits; }
    // Graph building runs on the message thread, never in the audio callback.
    juce::MidiMessageSequence createPlaybackMidiSequence (const te::MidiList& list, te::MidiClip& clip, te::MidiList::TimeBase base, bool mpe) override
    {
        return playbackSequence (list, clip, base, mpe, &list != &clip.getSequence());
    }
    // Notes with a chance or a velocity range vary between passes of their clip. Each of 64 passes
    // decides from the note's identity and the pass, so the outcome repeats exactly.
    int getNumPlaybackMidiSequenceVariations (te::MidiClip& clip) override
    {
        for (auto note : clip.getSequence().state)
            if (note.hasType (te::IDs::NOTE) && (note.hasProperty (noteChanceProperty) || note.hasProperty (noteVelocityRangeProperty))) return 64;
        return 1;
    }
    juce::MidiMessageSequence createPlaybackMidiSequenceVariation (const te::MidiList& list, te::MidiClip& clip, te::MidiList::TimeBase base,
                                                                   bool mpe, int variation) override
    {
        auto state = list.state.createCopy();
        for (int i = state.getNumChildren(); --i >= 0;)
        {
            auto note = state.getChild (i);
            if (! note.hasType (te::IDs::NOTE)) continue;
            const auto id = note[midiNoteIDProperty].toString() + "@" + note[te::IDs::b].toString() + "/" + note[te::IDs::p].toString();
            if (noteRoll (id, variation, 0) >= static_cast<double> (note.getProperty (noteChanceProperty, 1.0))) { state.removeChild (i, nullptr); continue; }
            if (const int range = note.getProperty (noteVelocityRangeProperty, 0); range != 0)
                note.setProperty (te::IDs::v, juce::jlimit (1, 127, static_cast<int> (note[te::IDs::v]) + juce::roundToInt (noteRoll (id, variation, 1) * range)), nullptr);
        }
        const te::MidiList varied (state, nullptr);
        return playbackSequence (varied, clip, base, mpe, &list != &clip.getSequence());
    }
    // A number in [0, 1) from a note's identity, a pass and a purpose, the same on every platform.
    static double noteRoll (const String& id, int variation, int purpose)
    {
        auto x = static_cast<uint64_t> (id.hashCode64()) ^ (0x9e3779b97f4a7c15ull * static_cast<uint64_t> (variation + 1)) ^ (0xbf58476d1ce4e5b9ull * static_cast<uint64_t> (purpose + 1));
        x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull; x ^= x >> 27; x *= 0x94d049bb133111ebull; x ^= x >> 31;
        return static_cast<double> (x >> 11) * (1.0 / 9007199254740992.0);
    }
    // A clip with a pool groove plays a grooved copy of its notes; the clip's own notes are unchanged.
    static juce::MidiMessageSequence playbackSequence (const te::MidiList& list, te::MidiClip& clip, te::MidiList::TimeBase base, bool mpe, bool looped)
    {
        if (const auto pattern = clipGroovePattern (clip))
        {
            const te::MidiList grooved (groovedSequence (*pattern, clip, list, looped), nullptr);
            return mpe ? mpeSequence (grooved, clip, base) : te::MidiList::createDefaultPlaybackMidiSequence (grooved, clip, base, mpe);
        }
        return mpe ? mpeSequence (list, clip, base) : te::MidiList::createDefaultPlaybackMidiSequence (list, clip, base, mpe);
    }
    // MPE playback: the engine steps between expression points, so ramps get a point every 1/64
    // beat; and each note's channel gets a 48-semitone pitch bend range (RPN 0) as the note starts.
    static juce::MidiMessageSequence mpeSequence (const te::MidiList& list, te::MidiClip& clip, te::MidiList::TimeBase base)
    {
        auto state = list.state.createCopy();
        for (auto note : state)
        {
            if (! note.hasType (te::IDs::NOTE)) continue;
            for (const auto kind : { Session::NoteExpression::pitchBend, Session::NoteExpression::slide, Session::NoteExpression::pressure })
            {
                const auto points = noteExpression (note, kind);
                if (points.size() < 2) continue;
                std::vector<Session::ExpressionPoint> ramped;
                for (size_t i = 0; i + 1 < points.size(); ++i)
                    for (double beat = points[i].beat; beat < points[i + 1].beat - 1e-9; beat += 1.0 / 64)
                        ramped.push_back ({ beat, (float) (points[i].value + (points[i + 1].value - points[i].value) * (beat - points[i].beat) / (points[i + 1].beat - points[i].beat)) });
                ramped.push_back (points.back());
                writeNoteExpression (note, kind, ramped, nullptr);
            }
        }
        const te::MidiList ramped (state, nullptr);
        auto sequence = te::MidiList::createDefaultPlaybackMidiSequence (ramped, clip, base, true);
        juce::MidiMessageSequence ranges;
        for (const auto* event : sequence)
            if (event->message.isNoteOn())
                for (const auto [controller, value] : { std::pair { 101, 0 }, { 100, 0 }, { 6, 48 }, { 38, 0 } })
                    ranges.addEvent (juce::MidiMessage::controllerEvent (event->message.getChannel(), controller, value), event->message.getTimeStamp());
        sequence.addSequence (ranges, 0);
        sequence.updateMatchedPairs();
        return sequence;
    }
    File getFileForNewAudioRecording (te::Track& track, const String& extension) override
    {
        auto base = te::EditFileOperations (track.edit).getEditFile();
        auto dir = base.getSiblingFile (base.getFileNameWithoutExtension() + " Assets");
        dir.createDirectory();
        return dir.getNonexistentChildFile (File::createLegalFileName (track.getName()) + " Take", extension, true);
    }
private:
    bool useDevices;
    juce::String midiPortPrefix;
};

// Test engines keep Tracktion's settings and temporary files in a folder owned by
// this process, so test runs (concurrent ones in other checkouts included) never
// share a device setup, MIDI enablement or wave-device layout. Test Sessions in one
// process share the folder. It is removed at exit unless a storage is still alive.
class TestPropertyStorage final : public te::PropertyStorage
{
public:
    TestPropertyStorage() : PropertyStorage ("Halftrack Tests") { ++live; }
    ~TestPropertyStorage() override { --live; }
    File getAppPrefsFolder() override { auto& folder = processFolder().file; folder.createDirectory(); return folder; }
    File getAppCacheFolder() override { return getAppPrefsFolder(); }
private:
    struct Folder
    {
        File file = File::getSpecialLocation (File::tempDirectory).getChildFile ("Halftrack-test-settings-" + juce::Uuid().toString());
        std::string utf8 = file.getFullPathName().toStdString();
        // Static teardown: use only the standard library, as JUCE's own statics may be gone.
        ~Folder()
        {
            std::error_code ignored;
            if (live == 0) std::filesystem::remove_all (std::u8string (reinterpret_cast<const char8_t*> (utf8.data()), utf8.size()), ignored);
        }
    };
    static Folder& processFolder() { static Folder folder; return folder; }
    static inline std::atomic<int> live { 0 };
};

std::unique_ptr<te::PropertyStorage> settingsStorage (bool devices)
{
    if (devices) return std::make_unique<te::PropertyStorage> ("Halftrack");
    return std::make_unique<TestPropertyStorage>();
}

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

// MIDI mappings: a Halftrack child of the Edit with one MAPPING per controller.
const juce::Identifier midiMapType { "HALFTRACKMIDIMAP" }, midiMappingType { "MAPPING" };
// Key Map Mode's mappings: a key (one lower-case character) and the target it drives.
const juce::Identifier keyMapType { "HALFTRACKKEYMAP" };
Session::KeyMapping readKeyMapping (const juce::ValueTree& v)
{
    const auto key = v["key"].toString();
    return { v["target"].toString(), key.isEmpty() ? (juce::juce_wchar) 0 : key[0], static_cast<float> (v["minimum"]), static_cast<float> (v["maximum"]) };
}
juce::ValueTree keyMappingTree (const Session::KeyMapping& m)
{
    juce::ValueTree v (midiMappingType);
    v.setProperty ("target", m.target, nullptr); v.setProperty ("key", String::charToString (m.key), nullptr);
    v.setProperty ("minimum", m.minimum, nullptr); v.setProperty ("maximum", m.maximum, nullptr);
    return v;
}
// A key Key Map Mode takes: a printable character other than a space, in lower case; 0 otherwise.
juce::juce_wchar mappableKey (juce::juce_wchar key)
{
    const auto lower = juce::CharacterFunctions::toLowerCase (key);
    return lower > 32 && lower < 127 ? lower : 0;
}
bool knownTargetKind (const String& target)
{
    const auto kind = target.upToFirstOccurrenceOf (":", false, false);
    return target == "master" || kind == "mixer" || kind == "device" || kind == "track" || kind == "slot" || kind == "scene" || kind == "transport";
}
// A hosted plug-in's configured parameters, by the plug-in's own IDs, in the order shown.
const juce::Identifier configureType { "HALFTRACKCONFIGURE" }, configuredType { "PARAMETER" };
// A MIDI mapping of a hosted plug-in parameter keeps that parameter's own ID beside its index.
const juce::Identifier parameterIdProperty { "parameterID" };
Session::MidiMapping readMidiMapping (const juce::ValueTree& v)
{
    return { v["target"].toString(), static_cast<int> (v["channel"]), static_cast<int> (v["controller"]),
             static_cast<float> (v["minimum"]), static_cast<float> (v["maximum"]) };
}
juce::ValueTree midiMappingTree (const Session::MidiMapping& m)
{
    juce::ValueTree v (midiMappingType);
    v.setProperty ("target", m.target, nullptr); v.setProperty ("channel", m.channel, nullptr); v.setProperty ("controller", m.controller, nullptr);
    v.setProperty ("minimum", m.minimum, nullptr); v.setProperty ("maximum", m.maximum, nullptr);
    return v;
}
Result validateMidiMappings (const juce::ValueTree& state)
{
    int maps = 0;
    for (const auto& map : state)
    {
        if (! map.hasType (midiMapType)) continue;
        if (++maps > 1 || map.getNumChildren() > 4096) return Result::fail ("The project's MIDI mappings are invalid.");
        std::set<String> targets; std::set<int> controllers;
        for (const auto& m : map)
        {
            const auto whole = [&] (const char* key, double low, double high) { return finiteProperty (m[key], low, high) && static_cast<double> (m[key]) == std::floor (static_cast<double> (m[key])); };
            const auto target = m["target"].toString();
            const auto kind = target.upToFirstOccurrenceOf (":", false, false);
            if (! m.hasType (midiMappingType) || ! knownTargetKind (target) || ! whole ("channel", 1, 16) || ! whole ("controller", 0, 127)
                || ! finiteProperty (m["minimum"], -1.0e6, 1.0e6) || ! finiteProperty (m["maximum"], -1.0e6, 1.0e6)
                || ! targets.insert (target).second || ! controllers.insert (static_cast<int> (m["channel"]) * 128 + static_cast<int> (m["controller"])).second
                || (m.hasProperty (parameterIdProperty) && (kind != "device" || m[parameterIdProperty].toString().isEmpty() || m[parameterIdProperty].toString().length() > 512)))
                return Result::fail ("The project contains an invalid MIDI mapping.");
        }
    }
    return Result::ok();
}
Result validateKeyMappings (const juce::ValueTree& state)
{
    int maps = 0;
    for (const auto& map : state)
    {
        if (! map.hasType (keyMapType)) continue;
        if (++maps > 1 || map.getNumChildren() > 4096) return Result::fail ("The project's key mappings are invalid.");
        std::set<String> targets, keys;
        for (const auto& m : map)
        {
            const auto key = m["key"].toString(), target = m["target"].toString();
            if (! m.hasType (midiMappingType) || ! knownTargetKind (target) || key.length() != 1 || mappableKey (key[0]) != key[0]
                || ! finiteProperty (m["minimum"], -1.0e6, 1.0e6) || ! finiteProperty (m["maximum"], -1.0e6, 1.0e6)
                || ! targets.insert (target).second || ! keys.insert (key).second)
                return Result::fail ("The project contains an invalid key mapping.");
        }
    }
    return Result::ok();
}
Result validateConfigurations (const juce::ValueTree& state)
{
    if (state.hasType (configureType))
    {
        std::set<String> ids;
        if (! state.getParent().hasType (te::IDs::PLUGIN) || state.getNumChildren() > Session::maximumConfiguredParameters)
            return Result::fail ("The project contains an invalid plug-in configuration.");
        for (const auto& parameter : state)
        {
            const auto id = parameter[te::IDs::id].toString();
            if (! parameter.hasType (configuredType) || id.isEmpty() || id.length() > 512 || ! ids.insert (id).second)
                return Result::fail ("The project contains an invalid plug-in configuration.");
        }
    }
    for (const auto& child : state) if (auto result = validateConfigurations (child); result.failed()) return result;
    return Result::ok();
}
// Defined with the range edits below.
namespace
{
bool canSplitMidiAt (const te::MidiClip&, te::TimePosition);
te::MidiClip* splitMidiClipAt (te::MidiClip&, te::TimePosition, juce::UndoManager*);
Result splittableAt (te::AudioTrack&, te::TimePosition);
void splitTrackAt (te::AudioTrack&, te::TimePosition, juce::UndoManager*);
}
// The engine does not watch Halftrack's groove state or plug-in sidechain taps, so playback is
// rebuilt when they change, undo and redo included. Only the Session's current Edit is restarted: a replaced Edit being
// destroyed is never touched.
class GrooveWatcher final : private juce::ValueTree::Listener
{
public:
    explicit GrooveWatcher (const std::unique_ptr<te::Edit>& e) : edit (e) {}
    ~GrooveWatcher() override { watch ({}); }
    void watch (const juce::ValueTree& editState)
    {
        if (state.isValid()) state.removeListener (this);
        state = editState;
        if (state.isValid()) state.addListener (this);
    }
private:
    const std::unique_ptr<te::Edit>& edit;
    juce::ValueTree state;
    static bool inPool (juce::ValueTree v)
    {
        for (; v.isValid(); v = v.getParent()) if (v.hasType (groovePoolType)) return true;
        return false;
    }
    void restart() { if (edit != nullptr && edit->state == state) edit->restartPlayback(); }
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override
    { if (id == clipGrooveProperty || id == sidechainTapProperty || id == sidechainOffProperty || inPool (v)) restart(); }
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override { if (inPool (parent) || child.hasType (groovePoolType)) restart(); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int) override { if (inPool (parent) || child.hasType (groovePoolType)) restart(); }
};

// MIDI remote control: every MIDI input's controller changes reach the message thread, where the
// Session applies its MIDI mappings. The MIDI thread only copies them into a fixed ring.
class MidiRemote final : public te::InputDeviceInstance::Consumer, private juce::AsyncUpdater
{
public:
    explicit MidiRemote (std::function<void (int, int, int)> handler) : handle (std::move (handler)) {}
    ~MidiRemote() override { detach(); cancelPendingUpdate(); }
    // Message thread: consume every MIDI input instance of the Edit, and only those.
    void attach (te::Edit* edit)
    {
        std::vector<te::InputDeviceInstance*> wanted;
        if (edit != nullptr) for (auto* in : edit->getAllInputDevices()) if (in->getInputDevice().isMidi()) wanted.push_back (in);
        const auto isWanted = [&] (te::InputDeviceInstance* in) { return std::find (wanted.begin(), wanted.end(), in) != wanted.end(); };
        for (auto& in : observed) if (in != nullptr && ! isWanted (in.get())) in->removeConsumer (this);
        std::erase_if (observed, [&] (const auto& in) { return in == nullptr || ! isWanted (in.get()); });
        for (auto* in : wanted)
            if (std::none_of (observed.begin(), observed.end(), [in] (const auto& o) { return o.get() == in; })) { in->addConsumer (this); observed.emplace_back (in); }
    }
    void detach() { for (auto& in : observed) if (in != nullptr) in->removeConsumer (this); observed.clear(); }
private:
    struct Message { int channel = 1, controller = 0, value = 0; };
    void handleIncomingMidiMessage (const juce::MidiMessage& m, te::MPESourceID) override
    {
        if (! m.isController()) return;
        {
            const juce::SpinLock::ScopedLockType lock (mutex);
            if (count < ring.size()) ring[(head + count++) % ring.size()] = { m.getChannel(), m.getControllerNumber(), m.getControllerValue() };
        }
        triggerAsyncUpdate();
    }
    void handleAsyncUpdate() override
    {
        std::array<Message, 256> batch; size_t n = 0;
        {
            const juce::SpinLock::ScopedLockType lock (mutex);
            for (; n < count; ++n) batch[n] = ring[(head + n) % ring.size()];
            head = (head + n) % ring.size(); count = 0;
        }
        for (size_t i = 0; i < n; ++i) handle (batch[i].channel, batch[i].controller, batch[i].value);
    }
    std::array<Message, 256> ring;
    size_t head = 0, count = 0;
    juce::SpinLock mutex;
    std::function<void (int, int, int)> handle;
    std::vector<juce::WeakReference<te::InputDeviceInstance>> observed;
};

// Capture MIDI: one per MIDI input instance, timing the notes it receives for the message thread.
class MidiCaptureTap final : public te::InputDeviceInstance::Consumer, private juce::AsyncUpdater
{
public:
    // The engine stamps each message with its arrival in the audio device's stream time.
    using Handler = std::function<void (te::InputDeviceInstance&, const juce::MidiMessage&)>;
    MidiCaptureTap (te::InputDeviceInstance& in, Handler handler) : instance (&in), handle (std::move (handler)) { in.addConsumer (this); }
    ~MidiCaptureTap() override { if (instance != nullptr) instance->removeConsumer (this); cancelPendingUpdate(); }
    te::InputDeviceInstance* get() const { return instance.get(); }
    // Hands on notes still waiting for the message thread, so a capture or a stop includes them.
    void flush() { handleUpdateNowIfNeeded(); }
private:
    void handleIncomingMidiMessage (const juce::MidiMessage& m, te::MPESourceID) override
    {
        if (! m.isNoteOnOrOff()) return;
        {
            const juce::SpinLock::ScopedLockType lock (mutex);
            if (count < ring.size()) ring[(head + count++) % ring.size()] = m;
        }
        triggerAsyncUpdate();
    }
    void handleAsyncUpdate() override
    {
        std::array<juce::MidiMessage, 256> batch; size_t n = 0;
        {
            const juce::SpinLock::ScopedLockType lock (mutex);
            for (; n < count; ++n) batch[n] = ring[(head + n) % ring.size()];
            head = (head + n) % ring.size(); count = 0;
        }
        for (size_t i = 0; i < n; ++i) if (instance != nullptr) handle (*instance, batch[i]);
    }
    std::array<juce::MidiMessage, 256> ring;
    size_t head = 0, count = 0;
    juce::SpinLock mutex;
    juce::WeakReference<te::InputDeviceInstance> instance;
    Handler handle;
};

struct Session::Impl
{
    explicit Impl (Session& s, bool devices, std::unique_ptr<juce::AudioIODeviceType> customDeviceType, const juce::String& midiPortPrefix)
        : owner (s), engine (settingsStorage (devices), std::make_unique<UIBehaviour> (s, devices),
                            std::make_unique<Behaviour> (devices, midiPortPrefix)), useDevices (devices), portPrefix (midiPortPrefix)
    {
        // A Session with audio devices asks the system about input permission; one without needs none.
        if (devices)
        {
            permissionStatus = []
            {
                switch (inputpermission::status())
                {
                    case inputpermission::Status::granted: return InputPermission::granted;
                    case inputpermission::Status::undetermined: return InputPermission::undetermined;
                    case inputpermission::Status::denied: break;
                }
                return InputPermission::denied;
            };
            permissionRequest = [] (std::function<void (bool)> done) { inputpermission::request (std::move (done)); };
        }
        engine.getPluginManager().createBuiltInType<TrackMixer>();
        engine.getPluginManager().createBuiltInType<TrackDelayPlugin>();
        engine.getPluginManager().createBuiltInType<SubtractiveSynthPlugin>();
        engine.getPluginManager().createBuiltInType<UtilityPlugin>();
        engine.getPluginManager().createBuiltInType<NativeEffectPlugin>();
        engine.getPluginManager().setUsesSeparateProcessForScanning (devices);
        // Keep rendering through a CPU overload. The engine's default silences a callback once the load
        // meter passes 0.98, and skips that callback's input and its block of time with it, so a take
        // recorded across a stall lost a block and the transport fell behind. The meter stops at 1, so
        // this limit is never reached: a callback that runs late is the driver's dropout, and every
        // input block the driver delivers reaches the take (tests/RecordingStallTests.cpp).
        engine.getDeviceManager().setCpuLimitBeforeMuting (2.0);
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
        if (useDevices)
        {
            deviceStops.attached = true; engine.getDeviceManager().deviceManager.addAudioCallback (&deviceStops);
            engine.getDeviceManager().deviceManager.addChangeListener (&deviceWatch); deviceChanged();
        }
    }
    ~Impl()
    {
        // Removing the callback reports a device stop; the session is closing, so no take ends here.
        deviceStops.attached = false;
        engine.getDeviceManager().deviceManager.removeAudioCallback (&deviceStops);
        engine.getDeviceManager().deviceManager.removeChangeListener (&deviceWatch);
        gesture.reset();
        meters.clear();
        grooveWatcher.watch ({});
        remoteRest.stopTimer(); remoteWatch.stopTimer(); remote.detach(); captureTaps.clear(); captured.clear();
        surfaceTimer.stopTimer(); closeSurface(); closeClockInputs();
        preview.reset();
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
    te::Clip* anyClip (const String& id) const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* c : allTrackClips (*t)) if (c->itemID.toString() == id) return c;
        return nullptr;
    }
    // Ends a clip's loop, the content beat at its start staying there.
    static void endLoop (te::Clip& clip)
    {
        if (! clip.isLooping()) return;
        const auto start = clip.getLoopStartBeats().inBeats() + std::fmod (clip.getOffsetInBeats().inBeats(), clip.getLoopLengthBeats().inBeats());
        clip.setLoopRangeBeats ({});
        auto position = clip.getPosition();
        position.offset = te::TimeDuration::fromSeconds (start / clip.edit.tempoSequence.getBeatsPerSecondAt (position.getStart()));
        clip.setPosition (position);
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
    // A slot, checked for an edit of its clip: unfrozen, not recording, on an ordinary track.
    Result editableSlot (int trackIndex, int sceneIndex, te::ClipSlot*& found) const
    {
        if (exporting) return Result::fail ("Finish or cancel the current export first.");
        if (edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing slots.");
        found = slot (trackIndex, sceneIndex);
        if (found == nullptr) return Result::fail ("Slot not found.");
        if (slotRecording (found->itemID)) return Result::fail ("End this slot's recording first.");
        return unfrozen (track (trackIndex));
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
        pendingSceneChange.reset(); sceneWatch.stopTimer();
        overdubs.clear();
    }
    // Sets one property on the given notes as one undo step, removing it where it takes its default.
    Result setNotesProperty (const String& clipId, const juce::StringArray& ids, const juce::Identifier& property,
                             const juce::var& value, const juce::var& fallback, const String& action)
    {
        if (exporting) return Result::fail ("Finish or cancel the current export first.");
        auto* clip = midiClip (clipId);
        if (clip == nullptr) return Result::fail ("MIDI clip not found.");
        if (clip->getClipTrack()->isFrozen (te::Track::anyFreeze)) return Result::fail ("Unfreeze the track before editing notes.");
        std::map<String, te::MidiNote*> existing;
        for (auto* note : clip->getSequence().getNotes())
        {
            auto [entry, inserted] = existing.emplace (note->state.getProperty (midiNoteIDProperty).toString(), note);
            if (! inserted) entry->second = nullptr;
        }
        std::set<String> requested;
        std::vector<te::MidiNote*> changes;
        for (const auto& id : ids)
        {
            if (id.isEmpty() || ! requested.insert (id).second) return Result::fail ("Each selected note needs a unique identifier.");
            const auto found = existing.find (id);
            if (found == existing.end() || found->second == nullptr) return Result::fail ("MIDI note not found or its identifier is ambiguous.");
            if (found->second->state.getProperty (property, fallback) != value) changes.push_back (found->second);
        }
        if (changes.empty()) return Result::ok();
        transaction (action);
        auto* undo = &edit->getUndoManager();
        for (auto* note : changes)
            if (value == fallback) note->state.removeProperty (property, undo); else note->state.setProperty (property, value, undo);
        // The engine does not watch Halftrack's note properties, so the clip's sequences are rebuilt here.
        edit->restartPlayback();
        changed(); return Result::ok();
    }
    // Session Record overdubs: the MIDI clips taking the notes played into their armed tracks.
    struct Overdub
    {
        te::EditItemID trackId, clipId;
        struct Held { double position, launched; float velocity; };
        std::map<std::pair<int, int>, Held> held; // By channel and pitch.
    };
    std::vector<Overdub> overdubs;
    // Adds a note to an overdubbing clip where it was played in the clip's loop, at its note-off.
    void overdubNote (Overdub& overdub, const juce::MidiMessage& m)
    {
        auto* clip = dynamic_cast<te::MidiClip*> (te::findClipForID (*edit, overdub.clipId));
        auto handle = clip != nullptr ? clip->getLaunchHandle() : nullptr;
        auto* context = edit->getTransport().getCurrentPlaybackContext();
        if (handle == nullptr || context == nullptr || handle->getPlayingStatus() != te::LaunchHandle::PlayState::playing) return;
        const auto played = handle->getPlayedRange();
        const auto sync = context->getSyncPoint();
        if (! played || ! sync) return;
        // Beats since the clip launched when the note arrived: the beats it has played less the
        // beats from the note's stream time to the play head.
        const auto noteBeat = edit->tempoSequence.toBeats (context->globalStreamTimeToEditTime (m.getTimeStamp())).inBeats();
        const auto launched = played->getLength().inBeats() - (sync->beat.inBeats() - noteBeat);
        const auto loop = clip->getLoopRangeBeats();
        const auto length = loop.getLength().inBeats() > 0 ? loop.getLength().inBeats() : clip->getLengthInBeats().inBeats();
        const auto start = loop.getLength().inBeats() > 0 ? loop.getStart().inBeats() : 0.0;
        if (length <= 0) return;
        const auto key = std::make_pair (m.getChannel(), m.getNoteNumber());
        if (m.isNoteOn())
        {
            overdub.held[key] = { start + std::fmod (std::max (0.0, launched), length), launched, m.getFloatVelocity() };
            return;
        }
        const auto on = overdub.held.find (key);
        if (on == overdub.held.end()) return;
        const auto note = on->second; overdub.held.erase (on);
        const auto duration = juce::jlimit (1.0 / 960, length, launched - note.launched);
        // A pass is one undo step, begun again if another edit came between its notes.
        auto& undo = edit->getUndoManager();
        if (undo.getCurrentTransactionName() != "Overdub") transaction ("Overdub");
        auto* added = clip->getSequence().addNote (m.getNoteNumber(), te::BeatPosition::fromBeats (note.position), te::BeatDuration::fromBeats (duration),
                                                  juce::jlimit (1, 127, juce::roundToInt (note.velocity * 127)), 0, &undo);
        if (added != nullptr) added->state.setProperty (midiNoteIDProperty, juce::Uuid().toString(), &undo);
        changed();
    }
    te::Scene* scene (const String& id) const
    {
        for (auto* s : edit->getSceneList().getScenes()) if (s->state.getProperty ("halftrackSceneID").toString() == id) return s;
        return nullptr;
    }
    // A launched scene's tempo and meter wait for its launch boundary, which the message thread
    // watches through the engine's sync point, and then become the project's as one undo step.
    struct SceneChange { std::optional<te::MonotonicBeat> at; std::optional<double> bpm; std::optional<Session::TimeSignature> meter; };
    std::optional<SceneChange> pendingSceneChange;
    struct SceneWatch final : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { tick(); }
    } sceneWatch;
    void scheduleSceneChange (const te::Scene& scene, std::optional<te::MonotonicBeat> boundary)
    {
        SceneChange change { boundary, {}, {} };
        if (scene.state.hasProperty (sceneTempoProperty)) change.bpm = (double) scene.state[sceneTempoProperty];
        if (scene.state.hasProperty (sceneNumeratorProperty))
            change.meter = Session::TimeSignature { (int) scene.state[sceneNumeratorProperty], (int) scene.state[sceneDenominatorProperty] };
        if (! change.bpm && ! change.meter) return; // A scene without them leaves the earlier launch's change waiting.
        pendingSceneChange = change;
        if (! boundary) { watchSceneChange(); return; }
        sceneWatch.tick = [this] { watchSceneChange(); };
        sceneWatch.startTimer (2);
    }
    void watchSceneChange()
    {
        if (! pendingSceneChange) { sceneWatch.stopTimer(); return; }
        if (pendingSceneChange->at)
        {
            auto* context = edit->getTransport().getCurrentPlaybackContext();
            const auto sync = context != nullptr ? context->getSyncPoint() : std::nullopt;
            if (! sync || sync->monotonicBeat.v < pendingSceneChange->at->v) return;
        }
        const auto change = *pendingSceneChange;
        pendingSceneChange.reset(); sceneWatch.stopTimer();
        // The tempo and meter are not edited while a take records, as elsewhere.
        if (exporting || edit->getTransport().isRecording()) return;
        auto& sequence = edit->tempoSequence;
        auto* tempo = sequence.getNumTempos() == 1 ? sequence.getTempo (0) : nullptr; // Automated tempos are kept.
        auto* meter = sequence.getNumTimeSigs() == 1 ? sequence.getTimeSig (0) : nullptr;
        const bool newTempo = change.bpm && tempo != nullptr && tempo->getBpm() != *change.bpm;
        const bool newMeter = change.meter && meter != nullptr
                              && (meter->numerator.get() != change.meter->numerator || meter->denominator.get() != change.meter->denominator);
        if (! newTempo && ! newMeter) return;
        transaction ("Launch scene");
        if (newTempo) { resetTapTempo(); changeTempo (*tempo, *change.bpm, &edit->getUndoManager()); }
        if (newMeter) { meter->numerator = change.meter->numerator; meter->denominator = change.meter->denominator; }
        changed();
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
        if (performanceStart) performance[track.itemID].push_back ({ unloopedTime (boundary), selected != nullptr ? selected->itemID : te::EditItemID(), {} });
    }
    // Arrangement recording of the Session: each track's clip changes between Record and Stop, as
    // the unlooped times they happen at. An invalid clip id is a stop.
    struct PerformanceChange { te::TimePosition at; te::EditItemID clip; te::TimeDuration offset; };
    std::map<te::EditItemID, std::vector<PerformanceChange>> performance;
    std::optional<te::TimePosition> performanceStart;
    // The unlooped time the engine has played up to: the same point clips' played ranges reach.
    te::TimePosition playedTime() const
    {
        if (auto* context = edit->getTransport().getCurrentPlaybackContext()) if (auto sync = context->getSyncPoint()) return sync->unloopedTime;
        return unloopedTime (std::nullopt);
    }
    // A Session clip playing or about to.
    bool launcherActive() const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* slot : t->getClipSlotList().getClipSlots())
                if (auto* c = slot->getClip()) if (auto h = c->getLaunchHandle())
                    if (h->getPlayingStatus() == te::LaunchHandle::PlayState::playing || h->getQueuedStatus() == te::LaunchHandle::QueueState::playQueued) return true;
        return false;
    }
    // Starts capturing at `start` with the clips already playing, each from the point it has reached.
    void startPerformance (te::TimePosition start)
    {
        performance.clear(); performanceStart = start;
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* slot : t->getClipSlotList().getClipSlots())
                if (auto* c = slot->getClip()) if (auto h = c->getLaunchHandle(); h && h->getPlayingStatus() == te::LaunchHandle::PlayState::playing)
                {
                    double beats = 0;
                    if (auto played = h->getPlayedRange()) beats = played->getLength().inBeats();
                    const auto loop = c->getLoopRangeBeats();
                    if (loop.getLength().inBeats() > 0) beats = loop.getStart().inBeats() + std::fmod (beats, loop.getLength().inBeats());
                    const auto startBeat = edit->tempoSequence.toBeats (start);
                    const auto offset = edit->tempoSequence.toTime (startBeat + te::BeatDuration::fromBeats (beats)) - start;
                    performance[t->itemID].push_back ({ start, c->itemID, offset });
                }
    }
    // Writes what each track played into the Arrangement, replacing what was there.
    void finishPerformance (te::TimePosition end)
    {
        if (! performanceStart) return;
        const auto start = *performanceStart; performanceStart.reset();
        auto* undo = &edit->getUndoManager();
        for (auto& [trackId, changes] : performance)
        {
            auto* track = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, trackId));
            if (track == nullptr) continue;
            std::stable_sort (changes.begin(), changes.end(), [] (const auto& x, const auto& y) { return x.at < y.at; });
            for (size_t i = 0; i < changes.size(); ++i)
            {
                const auto& change = changes[i];
                const auto from = std::max (change.at, start), to = std::min (i + 1 < changes.size() ? changes[i + 1].at : end, end);
                auto* source = change.clip.isValid() ? te::findClipForID (*edit, change.clip) : nullptr;
                if (source == nullptr || to <= from + te::TimeDuration::fromSeconds (1e-4)) continue;
                if (splittableAt (*track, from).wasOk() && splittableAt (*track, to).wasOk())
                {
                    splitTrackAt (*track, from, undo); splitTrackAt (*track, to, undo);
                    const auto existing = track->getClips(); // Removing clips changes the track's list.
                    for (auto* c : existing)
                        if (c->getPosition().getStart() >= from - te::TimeDuration::fromSeconds (1e-6) && c->getPosition().getEnd() <= to + te::TimeDuration::fromSeconds (1e-6)) c->removeFromParent();
                }
                auto state = source->state.createCopy();
                te::EditItemID::remapIDs (state, nullptr, *edit);
                renewMidiNoteIDs (state);
                state.setProperty (te::IDs::start, from.inSeconds(), nullptr);
                state.setProperty (te::IDs::length, (to - from).inSeconds(), nullptr);
                state.setProperty (te::IDs::offset, (change.offset + (from - change.at)).inSeconds(), nullptr);
                track->insertClipWithState (state);
            }
        }
        performance.clear();
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
    // Arrangement recording or any Session take: takes record through their inputs and leave the
    // transport out of recording. History, rendering (which frees the playback context and with
    // it every input recording) and flattening wait for both.
    bool anyRecording() const { return edit->getTransport().isRecording() || ! slotRecordings.empty(); }
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
            if ((clip = te::insertMIDIClip (*slot, "MIDI clip", edit->tempoSequence.toTime (beats)).get()) != nullptr) applyLaunchDefaults (*clip);
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
    static te::ExternalPlugin* hostedInstrument (te::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList.getPlugins())
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin); external && static_cast<bool> (external->state.getProperty (hostedInstrumentProperty, false))) return external;
        return nullptr;
    }
    static te::Plugin* instrument (te::AudioTrack& track)
    {
        if (auto* s = synth (track)) return s;
        if (auto* hosted = hostedInstrument (track)) return hosted;
        return nativeInstrument (track);
    }
    // Where a new device joins the end of a track's chain: before its delay, sends and mixer, or
    // before `before` (the instrument, for a MIDI effect) when that comes first.
    static int chainEnd (te::AudioTrack& track, const te::Plugin* before = nullptr)
    {
        for (int i = 0; i < track.pluginList.size(); ++i)
            if (auto* p = track.pluginList[i]; p == before || dynamic_cast<te::VolumeAndPanPlugin*> (p) || dynamic_cast<te::AuxSendPlugin*> (p) || dynamic_cast<TrackDelayPlugin*> (p))
                return i;
        return track.pluginList.size();
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
                        : dynamic_cast<te::ExternalPlugin*> (p) != nullptr || dynamic_cast<UtilityPlugin*> (p) != nullptr) result.add (p);
        return result;
    }
    te::Plugin* plugin (int trackIndex, int pluginIndex) const
    {
        auto list = plugins (trackIndex);
        return juce::isPositiveAndBelow (pluginIndex, list.size()) ? list[pluginIndex] : nullptr;
    }
    File editLocation() const { return projectFile == File() ? scratch.getChildFile ("Untitled.halftrack") : projectFile; }
    File assets() const { auto f = editLocation(); return f.getSiblingFile (f.getFileNameWithoutExtension() + " Assets"); }
    // Audio takes are written into the assets; a folder that cannot take a file is refused before
    // recording starts, so no take is lost to it.
    Result takeFolderReady() const
    {
        const auto folder = assets();
        if (auto r = folder.createDirectory(); r.failed()) return r;
        if (! folder.hasWriteAccess())
            return Result::fail ("Halftrack cannot write takes into \"" + folder.getFullPathName() + "\". Make it writable, or save the project somewhere writable.");
        return Result::ok();
    }
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
    te::Track* trackOfPlugin (const String& pluginId) const
    {
        for (auto* t : te::getAudioTracks (*edit)) for (auto* p : t->pluginList.getPlugins()) if (p->itemID.toString() == pluginId) return t;
        return nullptr;
    }
    // Every native device on the tracks, including devices inside racks, depth first.
    static void visitDevices (NativeEffectPlugin& device, const std::function<void (NativeEffectPlugin&)>& visit)
    {
        visit (device);
        if (auto* engine = device.rackEngine()) for (auto* inner : engine->devices()) visitDevices (*inner, visit);
    }
    void visitDevices (const std::function<void (te::AudioTrack&, NativeEffectPlugin&)>& visit) const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
                if (auto* native = dynamic_cast<NativeEffectPlugin*> (p)) visitDevices (*native, [&] (NativeEffectPlugin& d) { visit (*t, d); });
    }
    // A native device on a track or inside a rack, and the track that holds it.
    NativeEffectPlugin* nativeDevice (const String& id, te::AudioTrack** owner = nullptr) const
    {
        NativeEffectPlugin* found = nullptr;
        visitDevices ([&] (te::AudioTrack& t, NativeEffectPlugin& d) { if (! found && d.itemID.toString() == id) { found = &d; if (owner) *owner = &t; } });
        return found;
    }
    // A loaded external plug-in on a track, by id.
    te::MidiOutputDevice* midiOutput (const String& name) const
    {
        auto& devices = engine.getDeviceManager();
        for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
            if (auto* device = devices.getMidiOutDevice (i); device != nullptr && device->getName() == name) return device;
        return nullptr;
    }
    // A port's Track switch: it is offered to MIDI tracks' MIDI To while it is open.
    static String midiPortTrackItem (const te::MidiOutputDevice& device) { return "halftrackTrack " + device.getName(); }
    bool midiPortTrack (te::MidiOutputDevice& device) const
    { return device.isEnabled() && static_cast<bool> (engine.getPropertyStorage().getPropertyItem (te::SettingID::midiout, midiPortTrackItem (device), false)); }
    // An instrument arriving on a track that played to a MIDI port: as in the reference, its output
    // returns to Master, in the same undo step.
    void instrumentArrived (te::AudioTrack& track)
    {
        if (! track.state.hasProperty (midiOutChannelProperty)) return;
        track.getOutput().setOutputToDefaultDevice (false);
        track.state.removeProperty (midiOutChannelProperty, &edit->getUndoManager());
    }
    te::ExternalPlugin* externalPlugin (const String& id, te::AudioTrack** owner = nullptr) const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (p); external != nullptr && p->itemID.toString() == id)
                { if (owner) *owner = t; return external; }
        return nullptr;
    }
    NativeEffectPlugin* rack (const String& id, te::AudioTrack** owner = nullptr) const
    {
        auto* device = nativeDevice (id, owner);
        return device != nullptr && device->rackEngine() != nullptr ? device : nullptr;
    }
    // A chain or return chain of a rack, with that rack and the track holding it.
    struct ChainRef { NativeEffectPlugin* rack = nullptr; juce::ValueTree state; te::AudioTrack* track = nullptr; bool isReturn = false; };
    ChainRef chain (const String& id) const
    {
        ChainRef result;
        visitDevices ([&] (te::AudioTrack& t, NativeEffectPlugin& d)
        {
            if (result.rack != nullptr || d.rackEngine() == nullptr) return;
            for (auto child : d.state)
                if ((child.hasType (racks::chainType) || child.hasType (racks::returnType)) && child[te::IDs::id].toString() == id)
                { result = { &d, child, &t, child.hasType (racks::returnType) }; return; }
        });
        return result;
    }
    // A device inside a rack: the rack directly holding it and the chain it is in.
    struct RackDeviceRef { te::Plugin* device = nullptr; NativeEffectPlugin* native = nullptr; NativeEffectPlugin* rack = nullptr; juce::ValueTree chain; te::AudioTrack* track = nullptr; };
    RackDeviceRef rackDevice (const String& id) const
    {
        RackDeviceRef result;
        visitDevices ([&] (te::AudioTrack& t, NativeEffectPlugin& d)
        {
            if (result.device != nullptr || d.rackEngine() == nullptr) return;
            for (auto* inner : d.rackEngine()->plugins())
                if (inner->itemID.toString() == id) { result = { inner, dynamic_cast<NativeEffectPlugin*> (inner), &d, inner->state.getParent(), &t }; return; }
        });
        return result;
    }
    // A Utility on a track or inside a rack.
    UtilityPlugin* utility (const String& id, te::AudioTrack** owner = nullptr) const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
                if (auto* found = dynamic_cast<UtilityPlugin*> (p); found && found->itemID.toString() == id) { if (owner) *owner = t; return found; }
        const auto ref = rackDevice (id);
        if (auto* found = dynamic_cast<UtilityPlugin*> (ref.device)) { if (owner) *owner = ref.track; return found; }
        return nullptr;
    }
    // Removes the rack's macro mappings that drive any of these targets, in the current transaction.
    void removeMappings (NativeEffectPlugin& rackDevice, const juce::StringArray& targets)
    {
        for (int i = rackDevice.state.getNumChildren(); --i >= 0;)
            if (const auto child = rackDevice.state.getChild (i); child.hasType (racks::mappingType) && targets.contains (child[te::IDs::targetID].toString()))
                rackDevice.state.removeChild (i, &edit->getUndoManager());
    }
    // Identities of a chain and the devices directly in it.
    static juce::StringArray chainTargets (const juce::ValueTree& chainState)
    {
        juce::StringArray ids { chainState[te::IDs::id].toString() };
        for (const auto& device : chainState) if (device.hasType (te::IDs::PLUGIN)) ids.add (device[te::IDs::id].toString());
        return ids;
    }
    // Racks nested around a rack device, counting itself.
    static int rackDepth (const juce::ValueTree& rackState)
    {
        int depth = 0;
        for (auto node = rackState; node.isValid(); node = node.getParent())
            if (node.hasType (te::IDs::PLUGIN) && node[te::IDs::type].toString() == NativeEffectPlugin::xmlTypeName
                && effectIsRack (static_cast<EffectKind> (static_cast<int> (node.getProperty ("effectKind", 0))))) ++depth;
        return depth;
    }
    Result editable (const te::Track* owner) const
    {
        if (exporting) return Result::fail ("Finish or cancel the current export first.");
        return unfrozen (owner);
    }
    // Deepest rack nesting inside a device's state, counting the device if it is a rack.
    static int nestedDepth (const juce::ValueTree& device)
    {
        int deepest = 0;
        for (const auto& child : device) for (const auto& inner : child) if (inner.hasType (te::IDs::PLUGIN)) deepest = std::max (deepest, nestedDepth (inner));
        return deepest + (effectIsRack (static_cast<EffectKind> (static_cast<int> (device.getProperty ("effectKind", 0)))) ? 1 : 0);
    }
    // Wraps devices (copies with fresh identities) into one chain of a new rack at the first device's position.
    Result group (te::AudioTrack& track, const std::vector<te::Plugin*>& devices, EffectKind kind, String& rackId)
    {
        int depth = 0;
        for (auto* d : devices) depth = std::max (depth, nestedDepth (d->state));
        if (depth + 1 > racks::maximumDepth) return Result::fail ("Racks can be nested at most four deep.");
        if ((int) devices.size() > maximumChainDevices) return Result::fail ("A chain holds at most 32 devices.");
        juce::ValueTree rackState (te::IDs::PLUGIN), chainState (racks::chainType);
        rackState.setProperty (te::IDs::type, NativeEffectPlugin::xmlTypeName, nullptr);
        rackState.setProperty ("effectKind", static_cast<int> (kind), nullptr);
        edit->createNewItemID().writeID (rackState, nullptr);
        edit->createNewItemID().writeID (chainState, nullptr);
        RackChainSettings settings; settings.name = devices.front()->getName();
        racks::writeChain (chainState, settings, nullptr);
        if (kind == EffectKind::midiEffectRack)
            for (auto* d : devices) if (dynamic_cast<NativeEffectPlugin*> (d) == nullptr) return Result::fail ("A MIDI Effect Rack holds MIDI effects only.");
        for (auto* d : devices)
        {
            auto copy = d->state.createCopy();
            te::EditItemID::remapIDs (copy, nullptr, *edit);
            chainState.appendChild (copy, nullptr);
        }
        rackState.appendChild (chainState, nullptr);
        const int position = track.pluginList.indexOf (devices.front());
        // Creating the inner devices can write to their state, so that belongs to this step too.
        transaction ("Group devices");
        auto plugin = edit->getPluginCache().createNewPlugin (rackState);
        if (plugin == nullptr) return Result::fail ("The rack could not be created.");
        for (auto* d : devices) { d->windowState->closeWindowExplicitly(); d->deleteFromParent(); }
        track.pluginList.insertPlugin (plugin, position, nullptr);
        rackId = plugin->itemID.toString();
        changed(); return Result::ok();
    }
    // Keeps automation of devices inside racks on its beats across a tempo change; the engine
    // remaps only the devices it lists itself.
    // A tempo change keeps rack devices' automation on its beats. Without an undo manager (as external
    // sync follows a clock) neither the tempo nor the moved points are undo steps.
    void changeTempo (te::TempoSetting& tempo, double bpm, juce::UndoManager* undo)
    {
        struct Curve { te::AutomationCurve* curve; std::vector<te::BeatPosition> beats; };
        std::vector<Curve> curves;
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
                if (auto* native = dynamic_cast<NativeEffectPlugin*> (p); native && native->rackEngine())
                    visitDevices (*native, [&] (NativeEffectPlugin& d)
                    {
                        if (&d == native) return;
                        for (int i = 0; i < d.numControls(); ++i)
                        {
                            auto& curve = d.control (i)->getCurve();
                            if (curve.getNumPoints() == 0) continue;
                            Curve entry { &curve, {} };
                            for (int point = 0; point < curve.getNumPoints(); ++point) entry.beats.push_back (edit->tempoSequence.toBeats (curve.getPointTime (point)));
                            curves.push_back (std::move (entry));
                        }
                    });
        if (undo != nullptr) tempo.setBpm (bpm); else tempo.state.setProperty (te::IDs::bpm, bpm, nullptr);
        edit->tempoSequence.updateTempoData();
        for (auto& entry : curves)
            for (int point = (int) entry.beats.size(); --point >= 0;)
                entry.curve->setPointPosition (point, edit->tempoSequence.toTime (entry.beats[(size_t) point]), undo);
        if (! curves.empty()) visitDevices ([] (te::AudioTrack&, NativeEffectPlugin& d) { d.refreshAutomation(); });
    }
    // Preferences, read when the Session starts.
    Preferences prefs;
    bool importingVideo = false; // A movie's sound keeps its own speed: no Auto-Warp.
    // Driver Error Compensation reaches every audio input before a recording starts.
    void applyDriverCompensation()
    {
        for (auto* in : engine.getDeviceManager().getWaveInputDevices())
            if (in->getRecordAdjustmentMs() != prefs.driverErrorCompensationMs) in->setRecordAdjustmentMs (prefs.driverErrorCompensationMs);
    }
    // Audio files' transients and tempo for warping, each read once while the file is unchanged.
    struct SoundAnalysis { std::vector<double> transients; std::optional<tempo::Estimate> tempo; };
    std::map<juce::String, std::shared_ptr<const SoundAnalysis>> analyses;
    std::shared_ptr<const SoundAnalysis> analyse (const juce::File& file)
    {
        const auto key = file.getFullPathName() + "|" + juce::String (file.getSize()) + "|" + juce::String (file.getLastModificationTime().toMilliseconds());
        if (const auto found = analyses.find (key); found != analyses.end()) return found->second;
        auto result = std::make_shared<SoundAnalysis>();
        if (std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (file)); reader != nullptr)
        {
            tempo::OnsetCurve curve (reader->sampleRate);
            if (curve.add (*reader)) { result->transients = curve.transients(); result->tempo = tempo::estimate (curve); }
        }
        if (analyses.size() >= 256) analyses.clear();
        analyses[key] = result;
        return result;
    }
    String prefsProblem;
    File prefsFile() const { return engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("Halftrack Preferences.xml"); }
    static bool validPreferences (const Preferences& p)
    {
        return juce::isPositiveAndBelow ((int) p.defaultWarpMode, 8) && p.defaultWarpMode != WarpMode::off
            && juce::isPositiveAndBelow ((int) p.defaultLaunchMode, 4) && juce::isPositiveAndBelow ((int) p.defaultLaunchQuantisation, 11)
            && juce::isPositiveAndBelow (p.theme, Preferences::themeCount) && p.zoomPercent >= 50 && p.zoomPercent <= 200 && p.zoomPercent % 25 == 0
            && std::isfinite (p.driverErrorCompensationMs) && std::abs (p.driverErrorCompensationMs) <= 200;
    }
    void loadPreferences()
    {
        prefs = {}; prefsProblem.clear();
        const auto file = prefsFile();
        if (! file.existsAsFile()) return;
        auto xml = juce::XmlDocument::parse (file);
        Preferences read;
        const auto number = [&] (const char* key, int fallback, int count)
        {
            const auto text = xml->getStringAttribute (key, juce::String (fallback)).trim();
            return text.containsOnly ("0123456789") && text.isNotEmpty() && text.getIntValue() < count ? text.getIntValue() : -1;
        };
        bool valid = xml != nullptr && xml->hasTagName ("HALFTRACKPREFERENCES") && xml->getIntAttribute ("version", 0) >= 0;
        if (valid)
        {
            // Settings an older file lacks keep their defaults; a newer file's unknown ones are ignored.
            const int arm = number ("exclusiveArm", 1, 2), solo = number ("exclusiveSolo", 1, 2), warp = number ("defaultWarpMode", (int) read.defaultWarpMode, 8);
            const int autoWarp = number ("autoWarpLongSamples", 0, 2), mode = number ("defaultLaunchMode", 0, 4), grid = number ("defaultLaunchQuantisation", 0, 11);
            const int look = number ("theme", 0, Preferences::themeCount), zoom = number ("zoomPercent", 100, 201);
            valid = arm >= 0 && solo >= 0 && warp >= 0 && autoWarp >= 0 && mode >= 0 && grid >= 0 && look >= 0 && zoom >= 0;
            read = { arm == 1, solo == 1, (WarpMode) warp, autoWarp == 1, (LaunchMode) mode, (ClipQuantisation) grid, look, zoom };
            read.driverErrorCompensationMs = xml->getDoubleAttribute ("driverErrorCompensationMs", 0);
            valid = valid && validPreferences (read);
        }
        if (valid) { prefs = read; return; }
        // Set an unreadable file aside rather than lose it, and keep the defaults.
        const auto aside = file.getSiblingFile ("Halftrack Preferences (unreadable).xml");
        aside.deleteFile();
        file.moveFileTo (aside);
        prefsProblem = "The preferences file could not be read, so the defaults are in use. It was kept as " + aside.getFullPathName() + ".";
    }
    Result savePreferences (const Preferences& next)
    {
        juce::XmlElement xml ("HALFTRACKPREFERENCES");
        xml.setAttribute ("version", preferencesVersion);
        xml.setAttribute ("exclusiveArm", next.exclusiveArm ? 1 : 0); xml.setAttribute ("exclusiveSolo", next.exclusiveSolo ? 1 : 0);
        xml.setAttribute ("defaultWarpMode", (int) next.defaultWarpMode); xml.setAttribute ("autoWarpLongSamples", next.autoWarpLongSamples ? 1 : 0);
        xml.setAttribute ("defaultLaunchMode", (int) next.defaultLaunchMode); xml.setAttribute ("defaultLaunchQuantisation", (int) next.defaultLaunchQuantisation);
        xml.setAttribute ("theme", next.theme); xml.setAttribute ("zoomPercent", next.zoomPercent);
        xml.setAttribute ("driverErrorCompensationMs", next.driverErrorCompensationMs);
        const auto file = prefsFile();
        if (auto r = file.getParentDirectory().createDirectory(); r.failed()) return r;
        juce::TemporaryFile temporary (file);
        if (! xml.writeTo (temporary.getFile()) || ! temporary.overwriteTargetFileWithTemporary())
            return Result::fail ("The preferences could not be saved to " + file.getFullPathName() + ".");
        prefs = next;
        return Result::ok();
    }
    // New Session clips launch as the preferences say.
    void applyLaunchDefaults (te::Clip& clip)
    {
        auto* undo = &edit->getUndoManager();
        if (prefs.defaultLaunchMode != LaunchMode::trigger) clip.state.setProperty (launchModeProperty, (int) prefs.defaultLaunchMode, undo);
        if (prefs.defaultLaunchQuantisation != ClipQuantisation::global) clip.state.setProperty (launchQuantisationProperty, (int) prefs.defaultLaunchQuantisation, undo);
    }
    void clearTransposition (te::WaveAudioClip& clip)
    {
        auto* undo = &edit->getUndoManager();
        clip.state.removeProperty (transposeProperty, undo); clip.state.removeProperty (detuneProperty, undo);
        if (clip.getPitchChange() != 0) clip.setPitchChange (0);
    }
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
        instrumentArrived (track);
        changed(); return Result::ok();
    }
    void transaction (const String& name)
    {
        endAudition(); // Edits apply to the main lane, never to an audition.
        // Any other edit ends a controller's gesture, so it is an undo step of its own.
        if (remoteGesture && ! applyingRemote) { remoteGesture = false; owner.endEditGesture(); }
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
        if (exporting || (edit->getTransport().isRecording() && ! internalAutomationWrite))
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
        if (! internalAutomationWrite)
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
    // Automation capture and override. Lanes are named by key ("mixer:<track>:volume|pan", "master",
    // "device:<plugin>:<parameter>") and resolved again at each use, so a pass never holds a device.
    bool internalAutomationWrite = false, captureTransaction = false;
    struct LaneAccess
    {
        std::function<AutomationLane()> read;
        std::function<Result (const AutomationLane&)> write;
        std::function<float (float)> proportion, value; // Lane units to the engine's linear curve units, and back.
    };
    struct CapturePass { std::vector<AutomationPoint> original, recorded; bool touch = false; };
    std::map<String, CapturePass> captures;
    static String mixerKey (const te::AudioTrack& t, AutomationTarget target)
    { return "mixer:" + t.itemID.toString() + (target == AutomationTarget::volume ? ":volume" : ":pan"); }
    static String deviceKey (const String& pluginId, int parameter) { return "device:" + pluginId + ":" + String (parameter); }
    // A hosted plug-in parameter's own ID for its "device:" key, or empty for any other target.
    String externalParameterId (const String& target) const
    {
        const auto parts = juce::StringArray::fromTokens (target, ":", "");
        if (parts.size() != 3 || parts[0] != "device") return {};
        if (auto* external = externalPlugin (parts[1]); external != nullptr && external->getAudioPluginInstance() != nullptr)
        {
            const auto parameters = externalParameters (*external);
            if (const int index = parts[2].getIntValue(); parts[2].containsOnly ("0123456789") && juce::isPositiveAndBelow (index, (int) parameters.size()))
                return parameters[(size_t) index]->paramID;
        }
        return {};
    }
    // A mapping that names a parameter ID drives only that parameter: one a plug-in has moved does
    // not drive whatever now sits at its old index.
    bool mappingCurrent (const juce::ValueTree& m) const
    { return ! m.hasProperty (parameterIdProperty) || externalParameterId (m["target"].toString()) == m[parameterIdProperty].toString(); }
    // After a load: a mapping whose plug-in parameter has moved follows it to its new index.
    void reconcileMidiMappings()
    {
        auto map = edit->state.getChildWithName (midiMapType);
        for (auto m : map)
        {
            if (mappingCurrent (m)) continue;
            const auto parts = juce::StringArray::fromTokens (m["target"].toString(), ":", "");
            auto* external = externalPlugin (parts[1]);
            if (external == nullptr || external->getAudioPluginInstance() == nullptr) continue;
            const auto parameters = externalParameters (*external);
            for (size_t i = 0; i < parameters.size(); ++i)
                if (parameters[i]->paramID == m[parameterIdProperty].toString())
                {
                    const auto moved = deviceKey (parts[1], (int) i);
                    if (std::none_of (map.begin(), map.end(), [&] (const auto& other) { return other["target"].toString() == moved; }))
                        m.setProperty ("target", moved, nullptr);
                    break;
                }
        }
    }
    bool automationArmed() const { return edit->state.getProperty (automationArmProperty, false); }
    bool capturing() const { return automationArmed() && edit->getTransport().isRecording(); }
    LaneAccess mixerLane (te::VolumeAndPanPlugin& plugin, AutomationTarget target)
    {
        return { [&plugin, target] { return automationLane (plugin, target); },
                 [this, &plugin, target] (const AutomationLane& lane) { return setMixerAutomation (plugin, target, lane); },
                 [target] (float v) { return Session::automationValueToProportion (target, v); },
                 [target] (float p) { return Session::automationProportionToValue (target, p); } };
    }
    std::optional<LaneAccess> laneFor (const String& key)
    {
        const auto parts = juce::StringArray::fromTokens (key, ":", "");
        if (parts[0] == "master") return mixerLane (*edit->getMasterVolumePlugin(), AutomationTarget::volume);
        if (parts[0] == "mixer")
            if (auto* t = track (parts[1])) if (auto* mixer = t->getVolumePlugin())
                return mixerLane (*mixer, parts[2] == "pan" ? AutomationTarget::pan : AutomationTarget::volume);
        if (parts[0] == "device")
            if (auto* device = nativeDevice (parts[1]))
            {
                const auto id = parts[1]; const int parameter = parts[2].getIntValue(); const auto kind = device->getKind();
                if (device->control (parameter) == nullptr) return std::nullopt;
                return LaneAccess { [device, parameter] { for (const auto& a : deviceAutomation (*device)) if (a.parameter == parameter) return a.lane; return AutomationLane {}; },
                                    [this, id, parameter] (const AutomationLane& lane) { return owner.setDeviceAutomation (id, parameter, lane); },
                                    [kind, parameter] (float v) { return effectValueToProportion (kind, parameter, v); },
                                    [kind, parameter] (float p) { return effectProportionToValue (kind, parameter, p); } };
            }
            else if (auto* external = externalPlugin (parts[1]))
            {
                const auto id = parts[1]; const int parameter = parts[2].getIntValue();
                if (! juce::isPositiveAndBelow (parameter, (int) externalParameters (*external).size())) return std::nullopt;
                return LaneAccess { [external, parameter] { for (const auto& a : externalAutomation (*external)) if (a.parameter == parameter) return a.lane; return AutomationLane {}; },
                                    [this, id, parameter] (const AutomationLane& lane) { return owner.setDeviceAutomation (id, parameter, lane); },
                                    [] (float v) { return v; }, [] (float p) { return p; } };
            }
        return std::nullopt;
    }
    // Auditioning a take lane: the main lane's clip states, with their places in the track, rest aside
    // while copies of the lane's clips play. The same states return, so undo history stays valid.
    struct Audition { te::EditItemID trackId; String laneId; std::vector<std::pair<int, juce::ValueTree>> main; std::vector<juce::ValueTree> inserted; };
    std::optional<Audition> audition;
    void endAudition()
    {
        if (! audition) return;
        auto stash = std::move (*audition); audition.reset();
        auto* t = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, stash.trackId));
        if (t == nullptr) return;
        for (auto& v : stash.inserted) if (v.getParent().isValid()) v.getParent().removeChild (v, nullptr);
        for (auto& [index, v] : stash.main) t->state.addChild (v, std::min (index, t->state.getNumChildren()), nullptr);
        // The graph is rebuilt now, so the next block plays the main lane however busy the machine is.
        flushStructuralUpdates(); edit->restartPlayback(); edit->dispatchPendingUpdatesSynchronously();
    }
    // Take lanes: the track's lanes, made when asked; a lane by id, with its track.
    juce::ValueTree takeLanes (te::AudioTrack& t, bool create)
    {
        auto lanes = t.state.getChildWithName (takeLanesType);
        if (! lanes.isValid() && create) { lanes = juce::ValueTree (takeLanesType); t.state.appendChild (lanes, &edit->getUndoManager()); }
        return lanes;
    }
    std::pair<te::AudioTrack*, juce::ValueTree> takeLane (const String& id)
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto lane : t->state.getChildWithName (takeLanesType))
                if (lane[te::IDs::id].toString() == id) return { t, lane };
        return { nullptr, {} };
    }
    // Linked-track editing: the other tracks in a track's link; the clips on them that start and
    // end within 1 ms of an Arrangement clip; track ids with their linked tracks added; and the
    // lane at a position on a track.
    std::vector<te::AudioTrack*> linkedTracks (const te::AudioTrack& t) const
    {
        std::vector<te::AudioTrack*> result;
        const auto link = t.state[linkProperty].toString();
        if (link.isNotEmpty())
            for (auto* other : te::getAudioTracks (*edit))
                if (other != &t && other->state[linkProperty].toString() == link) result.push_back (other);
        return result;
    }
    std::vector<te::Clip*> linkedClips (te::Clip& clip) const
    {
        std::vector<te::Clip*> result;
        auto* t = dynamic_cast<te::AudioTrack*> (clip.getClipTrack());
        if (t == nullptr || clip.getClipSlot() != nullptr) return result;
        const auto p = clip.getPosition();
        const auto near = [] (te::TimePosition a, te::TimePosition b) { return std::abs ((a - b).inSeconds()) < .001; };
        for (auto* other : linkedTracks (*t))
            for (auto* c : other->getClips())
                if (near (c->getPosition().getStart(), p.getStart()) && near (c->getPosition().getEnd(), p.getEnd())) result.push_back (c);
        return result;
    }
    juce::StringArray withLinkedTracks (const juce::StringArray& ids) const
    {
        auto result = ids;
        for (const auto& id : ids)
            if (auto* t = track (id)) for (auto* other : linkedTracks (*t)) result.addIfNotAlreadyThere (other->itemID.toString());
        return result;
    }
    juce::ValueTree laneAt (te::AudioTrack& t, int position) { return takeLanes (t, false).getChild (position); }
    // The clips linked to one moving in time by an offset, or why one of them cannot move.
    Result linkedMoves (te::Clip& clip, double offset, std::vector<te::Clip*>& linked) const
    {
        linked = linkedClips (clip);
        for (auto* other : linked)
        {
            if (auto frozen = unfrozen (other->getClipTrack()); frozen.failed()) return frozen;
            const auto p = other->getPosition();
            if (p.getStart().inSeconds() + offset < 0 || p.getEnd().inSeconds() + offset > te::Edit::maximumLength)
                return Result::fail ("\"" + other->getName() + "\" on a linked track would leave the timeline.");
        }
        return Result::ok();
    }
    static void moveLinked (const std::vector<te::Clip*>& linked, double offset)
    {
        for (auto* other : linked) other->setStart (other->getPosition().getStart() + te::TimeDuration::fromSeconds (offset), false, true);
    }
    // Links the given ones left that now have one track end.
    void endSingleLinks (const juce::StringArray& links)
    {
        for (const auto& link : links)
        {
            if (link.isEmpty()) continue;
            std::vector<te::AudioTrack*> members;
            for (auto* t : te::getAudioTracks (*edit)) if (t->state[linkProperty].toString() == link) members.push_back (t);
            if (members.size() == 1) members.front()->state.removeProperty (linkProperty, &edit->getUndoManager());
        }
    }
    // Comping: a lane's material in a range replaces the main lane's there.
    void compLane (te::AudioTrack& t, const juce::ValueTree& lane, double startSeconds, double endSeconds)
    {
        auto* undo = &edit->getUndoManager();
        const auto from = te::TimePosition::fromSeconds (startSeconds), to = te::TimePosition::fromSeconds (endSeconds);
        for (const auto at : { from, to }) splitTrackAt (t, at, undo);
        const auto existing = t.getClips(); // Removing clips changes the track's list.
        for (auto* c : existing)
            if (c->getPosition().getStart() >= from - te::TimeDuration::fromSeconds (1e-6) && c->getPosition().getEnd() <= to + te::TimeDuration::fromSeconds (1e-6)) c->removeFromParent();
        for (auto clip : lane)
        {
            const double start = clip[te::IDs::start], end = start + static_cast<double> (clip[te::IDs::length]);
            const auto a = std::max (start, startSeconds), b = std::min (end, endSeconds);
            if (b <= a + 1e-6) continue;
            auto copy = freshClipCopy (clip);
            copy.setProperty (te::IDs::start, a, nullptr);
            copy.setProperty (te::IDs::length, b - a, nullptr);
            copy.setProperty (te::IDs::offset, static_cast<double> (clip[te::IDs::offset]) + (a - start), nullptr);
            copy.setProperty (compSourceProperty, lane[te::IDs::id].toString(), nullptr);
            t.insertClipWithState (copy);
        }
    }
    static bool laneHasMaterial (const juce::ValueTree& lane, double startSeconds, double endSeconds)
    {
        for (auto clip : lane)
            if (static_cast<double> (clip[te::IDs::start]) < endSeconds && static_cast<double> (clip[te::IDs::start]) + static_cast<double> (clip[te::IDs::length]) > startSeconds) return true;
        return false;
    }
    juce::ValueTree newTakeLane (te::AudioTrack& t)
    {
        auto lanes = takeLanes (t, true);
        juce::ValueTree lane (takeLaneType);
        lane.setProperty (te::IDs::id, juce::Uuid().toString(), nullptr);
        lane.setProperty (te::IDs::name, "Take " + String (lanes.getNumChildren() + 1), nullptr);
        lanes.appendChild (lane, &edit->getUndoManager());
        return lane;
    }
    // A copy of a clip's state with fresh identities.
    juce::ValueTree freshClipCopy (const juce::ValueTree& state)
    {
        auto copy = state.createCopy();
        te::EditItemID::remapIDs (copy, nullptr, *edit);
        renewMidiNoteIDs (copy);
        copy.removeProperty (compSourceProperty, nullptr);
        return copy;
    }
    // Splits the earlier clips under a new one at its edges and removes what lies beneath it. The new
    // clip does not cross its own edges, so only earlier material is split.
    void replaceUnder (te::AudioTrack& t, te::Clip& above, const std::set<String>& earlier)
    {
        const auto range = above.getPosition().time;
        auto* undo = &edit->getUndoManager();
        for (const auto at : { range.getStart(), range.getEnd() })
            if (splittableAt (t, at).wasOk()) splitTrackAt (t, at, undo);
        const auto clips = t.getClips(); // Removing clips changes the track's list.
        for (auto* c : clips)
            if (c != &above && c->getClipSlot() == nullptr && earlier.count (c->itemID.toString()) != 0
                && c->getPosition().getStart() >= range.getStart() - te::TimeDuration::fromSeconds (1e-6) && c->getPosition().getEnd() <= range.getEnd() + te::TimeDuration::fromSeconds (1e-6))
                c->removeFromParent();
    }
    // A recorded pass goes into the first take lane with nothing at or after its start, or a new one.
    void keepTake (te::AudioTrack& t, te::Clip& recorded)
    {
        const auto start = recorded.getPosition().getStart().inSeconds();
        juce::ValueTree target;
        for (auto lane : takeLanes (t, false))
        {
            bool free = true;
            for (auto clip : lane) free = free && static_cast<double> (clip[te::IDs::start]) + static_cast<double> (clip[te::IDs::length]) <= start + 1e-9;
            if (free) { target = lane; break; }
        }
        if (! target.isValid()) target = newTakeLane (t);
        target.appendChild (freshClipCopy (recorded.state), &edit->getUndoManager());
    }
    // An external plug-in parameter's lane, in 0-1 values; one undo step unless written internally.
    Result setExternalAutomation (te::ExternalPlugin& plugin, int parameter, const AutomationLane& lane)
    {
        const auto parameters = externalParameters (plugin);
        if (! juce::isPositiveAndBelow (parameter, (int) parameters.size())) return Result::fail ("That plug-in has no such parameter.");
        if (lane.points.size() > 4096) return Result::fail ("A lane can contain at most 4096 points.");
        double previousBeat = -1, previousTime = -1;
        for (const auto& point : lane.points)
        {
            const double time = std::isfinite (point.beat) ? edit->tempoSequence.toTime (te::BeatPosition::fromBeats (point.beat)).inSeconds() : -1;
            if (! std::isfinite (point.beat) || point.beat < 0 || point.beat > 1000000 || point.beat <= previousBeat || (previousBeat >= 0 && point.beat - previousBeat < 0.999e-6)
                || ! std::isfinite (point.value) || point.value < 0 || point.value > 1 || ! std::isfinite (time) || time <= previousTime)
                return Result::fail ("Automation requires up to 4096 ordered beat points with values from 0 to 1.");
            previousBeat = point.beat; previousTime = time;
        }
        auto* parameterObject = parameters[(size_t) parameter];
        auto& curve = parameterObject->getCurve();
        bool same = lane.enabled == ! curve.bypass.get() && lane.points.size() == static_cast<size_t> (curve.getNumPoints());
        for (size_t i = 0; same && i < lane.points.size(); ++i)
            same = std::abs (edit->tempoSequence.toBeats (curve.getPointTime ((int) i)).inBeats() - lane.points[i].beat) < 1e-9
                && std::abs (curve.getPointValue ((int) i) - lane.points[i].value) < 1e-6f;
        if (same) return Result::ok();
        if (! internalAutomationWrite) transaction ("Edit " + parameterObject->getParameterName() + " automation");
        auto* undo = &edit->getUndoManager();
        plugin.remapOnTempoChange = true;
        curve.clear (undo);
        curve.state.setProperty (te::IDs::bypass, ! lane.enabled, undo);
        for (const auto& point : lane.points) curve.addPoint (te::EditPosition (te::BeatPosition::fromBeats (point.beat)), point.value, 0, undo);
        parameterObject->updateStream();
        edit->dispatchPendingUpdatesSynchronously();
        changed(); return Result::ok();
    }
    void writeLane (const LaneAccess& lane, const AutomationLane& points)
    {
        internalAutomationWrite = true; lane.write (points); internalAutomationWrite = false;
    }
    // Every lane with points that is switched off.
    juce::StringArray overriddenLanes()
    {
        juce::StringArray keys;
        const auto off = [] (const AutomationLane& lane) { return ! lane.enabled && ! lane.points.empty(); };
        if (off (automationLane (*edit->getMasterVolumePlugin(), AutomationTarget::volume))) keys.add ("master");
        for (auto* t : te::getAudioTracks (*edit))
            if (auto* mixer = t->getVolumePlugin())
                for (const auto target : { AutomationTarget::volume, AutomationTarget::pan })
                    if (off (automationLane (*mixer, target))) keys.add (mixerKey (*t, target));
        visitDevices ([&] (te::AudioTrack&, NativeEffectPlugin& d)
        { for (const auto& a : deviceAutomation (d)) if (off (a.lane)) keys.add (deviceKey (d.itemID.toString(), a.parameter)); });
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (p))
                    for (const auto& a : externalAutomation (*external)) if (off (a.lane)) keys.add (deviceKey (external->itemID.toString(), a.parameter));
        return keys;
    }
    // Moving a control whose lane plays automation switches the lane off until Re-Enable Automation.
    void overrideLane (const String& key)
    {
        const auto lane = laneFor (key); if (! lane) return;
        auto current = lane->read();
        if (current.points.empty() || ! current.enabled) return;
        current.enabled = false; writeLane (*lane, current);
    }
    double playheadBeat() const { return std::max (0.0, edit->tempoSequence.toBeats (edit->getTransport().getPosition()).inBeats()); }
    // Punch-In and Punch-Out bound recording by the loop brace.
    bool punchIn() const { return edit->state.getProperty (punchInProperty, false); }
    bool punchOut() const { return edit->state.getProperty (punchOutProperty, false); }
    te::TimeRange loopRange() const { return edit->getTransport().getLoopRange(); }
    std::set<String> clipsBeforeRecording;
    // Trims the clips this recording created to the punch range, removing those wholly outside it.
    void applyPunch()
    {
        if (! punchIn() && ! punchOut()) return;
        const auto range = loopRange();
        auto* undo = &edit->getUndoManager();
        for (auto* track : te::getAudioTracks (*edit))
        {
            const auto clips = track->getClips();
            for (auto* clip : clips)
            {
                if (clipsBeforeRecording.contains (clip->itemID.toString())) continue;
                te::Clip::Ptr current = clip;
                const auto p = current->getPosition();
                const auto start = punchIn() ? std::max (p.getStart(), range.getStart()) : p.getStart();
                const auto end = punchOut() ? std::min (p.getEnd(), range.getEnd()) : p.getEnd();
                if (end <= start + te::TimeDuration::fromSeconds (1.0e-3)) { current->removeFromParent(); continue; }
                if (auto* midi = dynamic_cast<te::MidiClip*> (current.get()))
                {
                    // MIDI clips split at the punch points, keeping zero offsets, and lose their outer parts.
                    if (start > p.getStart() && canSplitMidiAt (*midi, start))
                        if (auto* right = splitMidiClipAt (*midi, start, undo)) { midi->removeFromParent(); current = right; }
                    if (auto* inner = dynamic_cast<te::MidiClip*> (current.get()); inner && end < p.getEnd() && canSplitMidiAt (*inner, end))
                        if (auto* outer = splitMidiClipAt (*inner, end, undo)) outer->removeFromParent();
                    continue;
                }
                // Audio keeps its timing: the source offset moves with the start.
                if (start > p.getStart()) current->setStart (start, true, false);
                if (end < p.getEnd()) current->setEnd (end, true);
            }
        }
    }
    // A capture pass replaces the lane from its first written beat. While it runs the lane holds its
    // last value ahead; when it finishes at `end`, the earlier automation resumes just after.
    void writePass (const CapturePass& pass, const LaneAccess& lane, std::optional<double> end)
    {
        constexpr double gap = 1.0 / 960;
        const auto& o = pass.original; const auto& r = pass.recorded;
        if (r.empty()) return;
        const auto first = r.front().beat, last = end ? std::max (*end, r.back().beat) : r.back().beat;
        // The original lane at a beat: linear in curve units, holding before its first point and after its last.
        const auto originalAt = [&] (double beat)
        {
            if (beat <= o.front().beat) return o.front().value;
            for (size_t i = 1; i < o.size(); ++i)
                if (beat <= o[i].beat)
                {
                    const auto t = (beat - o[i - 1].beat) / (o[i].beat - o[i - 1].beat);
                    const auto a = lane.proportion (o[i - 1].value), b = lane.proportion (o[i].value);
                    return lane.value ((float) (a + t * (b - a)));
                }
            return o.back().value;
        };
        AutomationLane out;
        for (const auto& p : o) if (p.beat < first - gap - 1e-6) out.points.push_back (p);
        if (! o.empty() && first - gap >= 0) out.points.push_back ({ first - gap, originalAt (first - gap) });
        out.points.insert (out.points.end(), r.begin(), r.end());
        if (end)
        {
            if (last - r.back().beat >= gap) out.points.push_back ({ last, r.back().value });
            if (! o.empty()) out.points.push_back ({ last + gap, originalAt (last + gap) });
            for (const auto& p : o) if (p.beat > last + gap + 1e-6) out.points.push_back (p);
        }
        if (out.points.size() > 4096) out.points.erase (out.points.begin(), out.points.end() - 4096);
        writeLane (lane, out);
    }
    void finishPass (const String& key, const CapturePass& pass)
    {
        auto end = playheadBeat();
        if (punchOut()) end = std::min (end, edit->tempoSequence.toBeats (loopRange().getEnd()).inBeats());
        if (const auto lane = laneFor (key)) writePass (pass, *lane, end);
    }
    // Writes `value` into the lane at the playhead. Moves within 1/64 beat replace the last point.
    void capture (const String& key, float value)
    {
        const auto lane = laneFor (key); if (! lane) return;
        const auto beat = playheadBeat();
        auto found = captures.find (key);
        // Punch-In ignores moves before the loop start; Punch-Out ends a pass at the loop end.
        const auto range = edit->tempoSequence.toBeats (loopRange());
        if (punchIn() && beat < range.getStart().inBeats()) return;
        if (punchOut() && beat > range.getEnd().inBeats())
        {
            if (found != captures.end()) { writePass (found->second, *lane, range.getEnd().inBeats()); captures.erase (found); }
            return;
        }
        // A loop wrap ends the pass at the loop end and starts another at the loop start.
        if (found != captures.end() && ! found->second.recorded.empty() && beat < found->second.recorded.back().beat - 1e-9)
        { writePass (found->second, *lane, found->second.recorded.back().beat); captures.erase (found); found = captures.end(); }
        if (found == captures.end())
        {
            found = captures.emplace (key, CapturePass {}).first;
            found->second.original = lane->read().points; found->second.touch = gesture != nullptr;
        }
        // Moves inside an edit gesture join its transaction; others share one per recording stretch.
        if (! gesture && ! captureTransaction) { transaction ("Record automation"); captureTransaction = true; }
        auto& r = found->second.recorded;
        if (! r.empty() && beat - r.back().beat < 1.0 / 64) r.back().value = value;
        else if (r.size() < 4000) r.push_back ({ beat, value });
        else r.back() = { beat, value };
        writePass (found->second, *lane, std::nullopt);
    }
    // Ends touch passes when their gesture ends, or every pass when recording stops.
    void finishCaptures (bool touchOnly)
    {
        for (auto it = captures.begin(); it != captures.end();)
            if (! touchOnly || it->second.touch) { finishPass (it->first, it->second); it = captures.erase (it); }
            else ++it;
        if (! touchOnly) captureTransaction = false;
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
            for (auto* plugin : track->pluginList.getPlugins()) refresh (*plugin);
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
        grooveWatcher.watch (edit->state);
        midiLearnTarget.clear(); keyLearnTarget.clear(); buttonValues.clear(); autoArmed.clear(); autoInput = false;
        remote.attach (edit.get()); attachCapture();
        if (useDevices && ! remoteWatch.isTimerRunning()) { remoteWatch.tick = [this] { remote.attach (edit.get()); attachCapture(); reconcileMidiMappings(); }; remoteWatch.startTimer (1000); }
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
            if (instance->getInputDevice().isMidi() == midi && instance->getTargets().contains (t.itemID)) return instance;
        return nullptr;
    }
    // The track whose output a track device carries.
    te::AudioTrack* sourceOf (const te::InputDevice& device) const
    {
        for (auto* t : te::getAudioTracks (*edit)) if (&t->getWaveInputDevice() == &device) return t;
        return nullptr;
    }
    // Whether a's audio reaches b, through track outputs, track-sourced inputs and plug-in
    // sidechains (whether on or off, so switching one on cannot feed back).
    bool feeds (te::AudioTrack& a, te::AudioTrack& b) const
    {
        std::multimap<te::EditItemID, te::AudioTrack*> sidechains;
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
                if (p->getSidechainSourceID().isValid()) sidechains.emplace (p->getSidechainSourceID(), t);
        std::vector<te::AudioTrack*> pending { &a };
        std::set<te::AudioTrack*> seen;
        while (! pending.empty())
        {
            auto* t = pending.back(); pending.pop_back();
            if (t == &b) return true;
            if (! seen.insert (t).second) continue;
            if (auto* destination = t->getOutput().getDestinationTrack()) pending.push_back (destination);
            for (auto [from, to] = sidechains.equal_range (t->itemID); from != to; ++from) pending.push_back (from->second);
            for (auto* in : edit->getAllInputDevices())
                if (&in->getInputDevice() == &t->getWaveInputDevice())
                    for (auto target : in->getTargets())
                        if (auto* fed = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, target))) pending.push_back (fed);
        }
        return false;
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
        remote.attach (edit.get()); attachCapture(); // Input instances can be replaced by an input change.
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
        applyRecordingFormat();
        if (! useDevices) return;
        edit->getTransport().ensureContextAllocated();
        remote.attach (edit.get()); attachCapture();
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
    // Each audio device stop (an Audio Settings change, a driver restart, a removed device) leads
    // the engine to rebuild its device lists, which discards active takes. The takes end where the
    // device stopped and are kept first, as Stop does.
    struct DeviceStopWatch final : juce::AudioIODeviceCallback, private juce::AsyncUpdater
    {
        explicit DeviceStopWatch (Impl& i) : impl (i) {}
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&) override
        {
            // The device manager adds this callback's outputs to the engine's.
            for (int i = 0; i < numOutputs; ++i) if (outputs[i] != nullptr) juce::FloatVectorOperations::clear (outputs[i], numSamples);
        }
        // Playback that the restart interrupted carries on once the device runs again, as it does
        // after an audio settings change in the reference. Takes are kept and not resumed.
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override
        {
            // A fallback device standing in for a missing one does not resume playback.
            const bool chosen = device == nullptr || impl.chosenDevice.isEmpty() || device->getName() == impl.chosenDevice;
            if (std::exchange (resumePlayback, false) && chosen) impl.deviceRestart.watch();
        }
        void audioDeviceStopped() override
        {
            if (juce::MessageManager::existsAndIsCurrentThread() && attached && impl.edit != nullptr)
                resumePlayback = impl.edit->getTransport().isPlaying() && ! impl.anyRecording() && ! impl.exporting;
            // A stop on the message thread ends the takes before the restart schedules the rebuild.
            if (juce::MessageManager::existsAndIsCurrentThread()) handleAsyncUpdate(); else triggerAsyncUpdate();
        }
        void handleAsyncUpdate() override { if (attached) impl.endTakesForDeviceStop(); }
        Impl& impl;
        bool attached = false; // Message thread only.
        bool resumePlayback = false;
    };
    // The engine rebuilds its playback context after a device restart, and its transport timer then
    // stops a transport whose new play head is not running. For a second after the restart this
    // watches for that and plays again from where playback stopped.
    struct DeviceRestart final : juce::Timer
    {
        explicit DeviceRestart (Impl& i) : impl (i) {}
        void watch() { ticks = 0; startTimer (20); }
        void timerCallback() override { if (++ticks > 50 || impl.continueAfterDeviceRestart()) stopTimer(); }
        Impl& impl;
        int ticks = 0;
    };
    // True once playback runs again, or when there is nothing to resume.
    bool continueAfterDeviceRestart()
    {
        if (edit == nullptr || exporting || anyRecording()) return true;
        auto& transport = edit->getTransport();
        if (transport.isPlaying())
        {
            auto* context = transport.getCurrentPlaybackContext();
            return context != nullptr && context->isPlaying();
        }
        transport.play (false);
        owner.sendChangeMessage();
        return false; // Checked again until the play head runs.
    }
    bool engineOff = false; // Audio Engine Off closed the device.
    // The recording format chosen here, given to inputs that appear later too.
    std::optional<Session::RecordingFormat> chosenRecordingFormat;
    juce::AudioFormat* recordingAudioFormat (Session::RecordingFileType type) const
    {
        auto& formats = engine.getAudioFileFormatManager();
        return type == Session::RecordingFileType::aiff ? formats.getAiffFormat() : type == Session::RecordingFileType::flac ? formats.getFlacFormat() : formats.getWavFormat();
    }
    void applyRecordingFormat()
    {
        if (! chosenRecordingFormat) return;
        const auto name = recordingAudioFormat (chosenRecordingFormat->type)->getFormatName();
        for (auto* device : engine.getDeviceManager().getWaveInputDevices())
        { device->setOutputFormat (name); device->setBitDepth (chosenRecordingFormat->bitDepth); }
    }
    String noDeviceMessage() const
    { return engineOff ? "Turn the audio engine on first (File > Audio Engine On)." : "Choose an audio output in Audio Settings."; }
    void endTakesForDeviceStop()
    {
        if (edit != nullptr && ! exporting && (edit->getTransport().isRecording() || ! slotRecordings.empty())) owner.stop();
    }
    Session& owner;
    te::Engine engine;
    // MIDI mappings: the learn target, the remote inputs, and the gesture that groups a controller's moves.
    String midiLearnTarget;
    MidiRemote remote { [this] (int channel, int controller, int value) { owner.handleMidiControl (channel, controller, value); } };
    // Capture MIDI: the notes played into each armed or In-monitored MIDI track, newest last.
    struct CapturedNote { int channel = 1, pitch = 60; float velocity = 0; double onTime = 0, offTime = -1, onBeat = 0, offBeat = 0; bool timed = false; }; // Times in stream seconds.
    std::map<te::EditItemID, std::vector<CapturedNote>> captured;
    std::vector<std::unique_ptr<MidiCaptureTap>> captureTaps;
    static constexpr size_t maximumCapturedNotes = 4096;
    void attachCapture()
    {
        std::vector<te::InputDeviceInstance*> wanted;
        if (edit != nullptr) for (auto* in : edit->getAllInputDevices()) if (in->getInputDevice().isMidi()) wanted.push_back (in);
        std::erase_if (captureTaps, [&] (const auto& tap) { return tap->get() == nullptr || std::find (wanted.begin(), wanted.end(), tap->get()) == wanted.end(); });
        for (auto* in : wanted)
            if (std::none_of (captureTaps.begin(), captureTaps.end(), [in] (const auto& tap) { return tap->get() == in; }))
                captureTaps.push_back (std::make_unique<MidiCaptureTap> (*in, [this] (te::InputDeviceInstance& i, const juce::MidiMessage& m) { captureNote (i, m); }));
    }
    // The beat a stream time fell on while the transport plays, mapped as the engine maps recorded MIDI.
    std::optional<double> arrivalBeat (double streamTime) const
    {
        auto& transport = edit->getTransport();
        if (! transport.isPlaying()) return std::nullopt;
        auto* context = transport.getCurrentPlaybackContext(); if (context == nullptr) return std::nullopt;
        return edit->tempoSequence.toBeats (context->globalStreamTimeToEditTime (streamTime)).inBeats();
    }
    void captureNote (te::InputDeviceInstance& in, const juce::MidiMessage& m)
    {
        const auto received = m.getTimeStamp();
        const auto beat = arrivalBeat (received);
        for (auto target : in.getTargets())
        {
            auto* t = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, target));
            if (t == nullptr || ! static_cast<bool> (t->state.getProperty (midiTrackProperty))) continue;
            const bool monitored = static_cast<int> (t->state.getProperty (monitorProperty, 0)) == static_cast<int> (MonitorMode::on);
            if (! in.isRecordingEnabled (target) && ! monitored) continue;
            if (in.isRecordingEnabled (target))
                for (auto& overdub : overdubs) if (overdub.trackId == target) overdubNote (overdub, m);
            auto& notes = captured[target];
            if (m.isNoteOn())
            {
                if (notes.size() >= maximumCapturedNotes) notes.erase (notes.begin());
                notes.push_back ({ m.getChannel(), m.getNoteNumber(), m.getFloatVelocity(), received, -1, beat.value_or (0), 0, beat.has_value() });
            }
            else
                for (auto it = notes.rbegin(); it != notes.rend(); ++it)
                    if (it->offTime < 0 && it->channel == m.getChannel() && it->pitch == m.getNoteNumber())
                    { it->offTime = received; it->offBeat = beat.value_or (it->onBeat); break; }
        }
    }
    struct RemoteRest final : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { tick(); }
    } remoteRest;
    bool remoteGesture = false;
    // A control surface's ports, opened by Halftrack itself. Its MIDI thread hands each message to
    // the message thread through a fixed ring.
    struct SurfacePort final : juce::MidiInputCallback, private juce::AsyncUpdater
    {
        std::function<void (const juce::MidiMessage&)> handle;
        ~SurfacePort() override { cancelPendingUpdate(); }
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            if (m.isSysEx()) return;
            { const juce::SpinLock::ScopedLockType lock (mutex); if (count < ring.size()) ring[(head + count++) % ring.size()] = m; }
            triggerAsyncUpdate();
        }
        void handleAsyncUpdate() override
        {
            std::array<juce::MidiMessage, 256> batch; size_t n = 0;
            { const juce::SpinLock::ScopedLockType lock (mutex); for (; n < count; ++n) batch[n] = ring[(head + n) % ring.size()]; head = (head + n) % ring.size(); count = 0; }
            for (size_t i = 0; i < n; ++i) if (handle) handle (batch[i]);
        }
        std::array<juce::MidiMessage, 256> ring;
        size_t head = 0, count = 0;
        juce::SpinLock mutex;
    } surfacePort;
    std::unique_ptr<juce::MidiInput> surfaceIn;   // Destroyed before the port it calls.
    std::unique_ptr<juce::MidiOutput> surfaceOut;
    Session::ControlSurface surface;
    int surfaceBank = 0;
    bool surfaceTouched = false;
    // What the surface was last sent, so only changes go again.
    struct SurfaceShown { std::array<int, 9> fader; std::array<int, 8> ring; std::map<int, bool> lights; juce::String top, bottom; void clear() { fader.fill (-1); ring.fill (-1); lights.clear(); top = bottom = {}; } } shownOnSurface;
    juce::String portPrefix;
    bool portAllowed (const juce::String& name) const { return useDevices ? portPrefix.isEmpty() || name.startsWith (portPrefix) : portPrefix.isNotEmpty() && name.startsWith (portPrefix); }
    // A controller's or surface's moves form one gesture until it rests, unless a fader is touched.
    void remoteMove()
    {
        if (surfaceTouched) return;
        if (! remoteGesture && ! gesture) { owner.beginEditGesture ("MIDI control"); remoteGesture = true; }
        lastRemote = juce::Time::getMillisecondCounter();
        if (! remoteRest.isTimerRunning())
        {
            remoteRest.tick = [this]
            {
                if (! remoteGesture) { remoteRest.stopTimer(); return; }
                if (juce::Time::getMillisecondCounter() - lastRemote < 500) return;
                remoteGesture = false; remoteRest.stopTimer(); owner.endEditGesture();
            };
            remoteRest.startTimer (100);
        }
    }
    RemoteRest surfaceTimer;
    void closeSurface()
    {
        if (surfaceIn != nullptr) surfaceIn->stop();
        surfaceIn.reset(); surfaceOut.reset();
    }
    // The surface's strips show tracks from the bank's first; the ninth is the master.
    te::AudioTrack* surfaceTrack (int strip) const
    {
        const auto tracks = te::getAudioTracks (*edit);
        return juce::isPositiveAndBelow (surfaceBank + strip, tracks.size()) && strip < 8 ? tracks[surfaceBank + strip] : nullptr;
    }
    juce::String surfaceVolumeTarget (int strip) const
    {
        if (strip == 8) return "master";
        auto* t = surfaceTrack (strip);
        return t != nullptr ? "mixer:" + t->itemID.toString() + ":volume" : juce::String();
    }
    void handleSurface (const juce::MidiMessage& message)
    {
        const auto action = mackie::parse (message);
        if (! action) return;
        using mackie::Action; using mackie::Button;
        const auto index = [this] (te::AudioTrack* t) { return t != nullptr ? trackIndexOf (t->itemID.toString()) : -1; };
        const juce::ScopedValueSetter<bool> applying (applyingRemote, true);
        if (action->kind == Action::Kind::fader)
        {
            const auto target = surfaceVolumeTarget (action->strip);
            if (const auto lane = target.isNotEmpty() ? laneFor (target) : std::nullopt)
            { remoteMove(); applyValue (target, lane->value (action->position)); shownOnSurface.fader[(size_t) action->strip] = juce::roundToInt (action->position * 16383.0f); }
        }
        else if (action->kind == Action::Kind::vpot)
        {
            if (auto* t = surfaceTrack (action->strip); t != nullptr && t->getVolumePlugin() != nullptr)
            { remoteMove(); owner.setTrackPan (index (t), juce::jlimit (-1.0f, 1.0f, t->getVolumePlugin()->getPan() + action->ticks * 0.02f)); }
        }
        else if (action->kind == Action::Kind::jog)
        {
            const double beat = 60.0 / edit->tempoSequence.getTempoAt (edit->getTransport().getPosition()).getBpm();
            owner.seek (std::max (0.0, edit->getTransport().getPosition().inSeconds() + action->ticks * beat / 4));
        }
        else if (action->button == Button::faderTouch)
        {
            // A touched fader's moves are one undo step, ending when it is let go.
            if (action->pressed && ! surfaceTouched) { if (remoteGesture) { remoteGesture = false; owner.endEditGesture(); } owner.beginEditGesture ("Control surface fader"); surfaceTouched = true; }
            else if (! action->pressed && surfaceTouched) { surfaceTouched = false; owner.endEditGesture(); }
        }
        else if (action->pressed)
        {
            auto* t = surfaceTrack (action->strip);
            const auto now = owner.snapshot();
            const auto info = [&] () -> const Session::TrackInfo* { const int i = index (t); return i >= 0 && i < (int) now.tracks.size() ? &now.tracks[(size_t) i] : nullptr; }();
            const auto tracks = te::getAudioTracks (*edit).size();
            switch (action->button)
            {
                case Button::recArm: if (info) owner.setTrackArmed (index (t), ! info->armed); break;
                case Button::solo: if (info) owner.setTrackSolo (index (t), ! info->solo); break;
                case Button::mute: if (info) owner.setTrackMute (index (t), ! info->mute); break;
                case Button::vpotPush: if (info) owner.setTrackPan (index (t), 0); break;
                case Button::bankLeft: surfaceBank = std::max (0, surfaceBank - 8); break;
                case Button::bankRight: if (surfaceBank + 8 < tracks) surfaceBank += 8; break;
                case Button::channelLeft: surfaceBank = std::max (0, surfaceBank - 1); break;
                case Button::channelRight: if (surfaceBank + 1 < tracks) ++surfaceBank; break;
                case Button::play: owner.play(); break;
                case Button::stop: if (now.playing) owner.stop(); else owner.seek (0); break;
                case Button::record: if (now.recording) owner.stop(); else owner.record(); break;
                case Button::cycle: owner.setLoop (! now.looping, now.loopStart, now.loopEnd); break;
                case Button::click: { auto m = now.metronome; m.enabled = ! m.enabled; owner.setMetronome (m); break; }
                case Button::rewind: case Button::fastForward:
                {
                    const double bar = 4 * 60.0 / now.tempo;
                    owner.seek (std::max (0.0, now.position + (action->button == Button::rewind ? -bar : bar)));
                    break;
                }
                case Button::select: case Button::faderTouch: case Button::other: break;
            }
        }
        sendSurfaceState();
    }
    // Sends the surface what has changed since it was last sent: faders, rings, lights and names.
    void sendSurfaceState()
    {
        if (surfaceOut == nullptr) return;
        const auto now = owner.snapshot();
        juce::String top, bottom;
        for (int strip = 0; strip < 9; ++strip)
        {
            auto* t = surfaceTrack (strip);
            const int i = t != nullptr ? trackIndexOf (t->itemID.toString()) : -1;
            const auto* info = i >= 0 && i < (int) now.tracks.size() ? &now.tracks[(size_t) i] : nullptr;
            // The fader, unless the hand on it is moving it.
            if (! surfaceTouched)
            {
                const auto target = surfaceVolumeTarget (strip);
                const auto lane = target.isNotEmpty() ? laneFor (target) : std::nullopt;
                const auto value = target.isNotEmpty() ? targetValue (target) : std::nullopt;
                const int position = lane && value ? juce::roundToInt (lane->proportion (*value) * 16383.0f) : 0;
                if (shownOnSurface.fader[(size_t) strip] != position) { surfaceOut->sendMessageNow (mackie::faderPosition (strip, position / 16383.0f)); shownOnSurface.fader[(size_t) strip] = position; }
            }
            if (strip == 8) break;
            const int ring = info != nullptr ? juce::roundToInt (info->pan * 100) : -1000;
            if (shownOnSurface.ring[(size_t) strip] != ring) { surfaceOut->sendMessageNow (mackie::vpotRing (strip, info != nullptr ? info->pan : 0)); shownOnSurface.ring[(size_t) strip] = ring; }
            const auto light = [&] (mackie::Button button, bool on)
            {
                const int note = mackie::noteFor (button, strip);
                if (auto known = shownOnSurface.lights.find (note); known != shownOnSurface.lights.end() && known->second == on) return;
                surfaceOut->sendMessageNow (mackie::buttonLight (button, strip, on)); shownOnSurface.lights[note] = on;
            };
            light (mackie::Button::recArm, info != nullptr && info->armed);
            light (mackie::Button::solo, info != nullptr && info->solo);
            light (mackie::Button::mute, info != nullptr && info->mute);
            top << mackie::stripText (info != nullptr ? info->name : juce::String());
            bottom << mackie::stripText (info != nullptr ? (info->gainDb <= -99.9f ? juce::String ("-inf") : juce::String (info->gainDb, 1)) : juce::String());
        }
        const auto light = [&] (mackie::Button button, bool on)
        {
            const int note = mackie::noteFor (button);
            if (auto known = shownOnSurface.lights.find (note); known != shownOnSurface.lights.end() && known->second == on) return;
            surfaceOut->sendMessageNow (mackie::buttonLight (button, 0, on)); shownOnSurface.lights[note] = on;
        };
        light (mackie::Button::play, now.playing); light (mackie::Button::stop, ! now.playing); light (mackie::Button::record, now.recording);
        light (mackie::Button::cycle, now.looping); light (mackie::Button::click, now.metronome.enabled);
        if (top != shownOnSurface.top) { surfaceOut->sendMessageNow (mackie::display (0, 0, top)); shownOnSurface.top = top; }
        if (bottom != shownOnSurface.bottom) { surfaceOut->sendMessageNow (mackie::display (1, 0, bottom)); shownOnSurface.bottom = bottom; }
    }
    // External Sync, after the reference: MIDI Clock and transport messages from the inputs whose
    // Sync switch is on reach the message thread through a fixed ring. While External Sync is on, the
    // transport follows them: the tempo from the clock's ticks, drawn toward the clock's position
    // while playing, Start from the beginning, Continue, Stop, and Song Position while stopped.
    struct ClockPort final : juce::MidiInputCallback, private juce::AsyncUpdater
    {
        std::function<void (const juce::MidiMessage&)> handle;
        ~ClockPort() override { cancelPendingUpdate(); }
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            if (! (m.isMidiClock() || m.isMidiStart() || m.isMidiContinue() || m.isMidiStop() || m.isSongPositionPointer())) return;
            { const juce::SpinLock::ScopedLockType lock (mutex); if (count < ring.size()) ring[(head + count++) % ring.size()] = m; }
            triggerAsyncUpdate();
        }
        void handleAsyncUpdate() override
        {
            std::array<juce::MidiMessage, 512> batch; size_t n = 0;
            { const juce::SpinLock::ScopedLockType lock (mutex); for (; n < count; ++n) batch[n] = ring[(head + n) % ring.size()]; head = (head + n) % ring.size(); count = 0; }
            for (size_t i = 0; i < n; ++i) if (handle) handle (batch[i]);
        }
        std::array<juce::MidiMessage, 512> ring;
        size_t head = 0, count = 0;
        juce::SpinLock mutex;
    } clockPort;
    std::vector<std::unique_ptr<juce::MidiInput>> clockInputs; // Destroyed before the port they call.
    bool externalSync = false, clockRunning = false;
    bool positionedByClock = false; // Stopped at a Song Position, which the play head keeps through tempo changes.
    std::deque<double> clockTimes; // The last ticks' times, in seconds.
    double clockBpm = 0, clockBeats = 0;
    int ticksSinceFollow = 0;
    static juce::String syncItem (const juce::String& port) { return "halftrackSync " + port; }
    bool syncOn (const juce::String& port) const { return static_cast<bool> (engine.getPropertyStorage().getPropertyItem (te::SettingID::midiin, syncItem (port), false)); }
    void closeClockInputs()
    {
        for (auto& in : clockInputs) in->stop();
        clockInputs.clear();
    }
    void openClockInputs()
    {
        closeClockInputs();
        clockPort.handle = [this] (const juce::MidiMessage& m) { handleClock (m); };
        for (const auto& info : juce::MidiInput::getAvailableDevices())
            if (portAllowed (info.name) && syncOn (info.name))
                if (auto in = juce::MidiInput::openDevice (info.identifier, &clockPort)) { in->start(); clockInputs.push_back (std::move (in)); }
    }
    bool clockReceiving() const { return ! clockTimes.empty() && juce::Time::getMillisecondCounterHiRes() * 0.001 - clockTimes.back() < 0.5; }
    void handleClock (const juce::MidiMessage& m)
    {
        auto& transport = edit->getTransport();
        if (m.isMidiClock())
        {
            const auto at = m.getTimeStamp();
            // A gap of half a second is the clock stopping; its ticks start again afresh.
            if (! clockTimes.empty() && at - clockTimes.back() > 0.5) clockTimes.clear();
            clockTimes.push_back (at);
            while (clockTimes.size() > 25) clockTimes.pop_front();
            if (clockRunning) clockBeats += 1.0 / 24;
            // A tick's length from a line through the last beat's ticks, so one late tick barely moves it.
            if (clockTimes.size() >= 13)
            {
                const auto n = (double) clockTimes.size();
                double meanI = (n - 1) / 2, meanT = 0, spread = 0, covariance = 0;
                for (const auto t : clockTimes) meanT += t / n;
                for (size_t i = 0; i < clockTimes.size(); ++i) { spread += ((double) i - meanI) * ((double) i - meanI); covariance += ((double) i - meanI) * (clockTimes[i] - meanT); }
                if (const auto tick = covariance / spread; tick > 0) clockBpm = 60 / (tick * 24);
            }
            if (externalSync && clockBpm > 0 && ++ticksSinceFollow >= 6) { ticksSinceFollow = 0; followClock(); }
            return;
        }
        if (! externalSync) return;
        if (m.isMidiStart()) { clockBeats = 0; clockRunning = true; positionedByClock = false; owner.seek (0); owner.play(); }
        else if (m.isMidiContinue())
        {
            if (! positionedByClock) clockBeats = edit->tempoSequence.toBeats (transport.getPosition()).inBeats();
            clockRunning = true; positionedByClock = false; owner.continuePlayback();
        }
        else if (m.isMidiStop()) { clockRunning = false; owner.stop(); }
        else if (m.isSongPositionPointer() && ! transport.isPlaying())
        {
            clockBeats = m.getSongPositionPointerMidiBeat() / 4.0;
            positionedByClock = true;
            owner.seek (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (clockBeats)).inSeconds());
        }
    }
    // The clock's tempo, faster while the transport is behind the clock's position and slower while
    // ahead: by its distance in beats beyond 0.02 (the fixed latency of reading either), 10% at most.
    // It is set without undo steps.
    void followClock()
    {
        if (edit->tempoSequence.getNumTempos() > 1) return;
        auto& transport = edit->getTransport();
        auto bpm = clockBpm;
        if (clockRunning && transport.isPlaying())
        {
            const auto heard = transport.getCurrentPlaybackContext() != nullptr ? transport.getCurrentPlaybackContext()->getUnloopedPosition() : transport.getPosition();
            const auto behind = clockBeats - edit->tempoSequence.toBeats (heard).inBeats();
            const auto beyond = std::abs (behind) <= 0.02 ? 0.0 : behind - std::copysign (0.02, behind);
            bpm *= 1 + juce::jlimit (-0.1, 0.1, beyond);
        }
        bpm = juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm);
        auto* tempo = edit->tempoSequence.getTempo (0);
        if (std::abs (tempo->getBpm() - bpm) < 0.005) return;
        changeTempo (*tempo, bpm, nullptr);
        if (positionedByClock && ! transport.isPlaying()) owner.seek (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (clockBeats)).inSeconds());
        owner.sendChangeMessage();
    }
    Result openSurface (const Session::ControlSurface& wanted)
    {
        surfaceTimer.stopTimer(); closeSurface();
        if (surfaceTouched) { surfaceTouched = false; owner.endEditGesture(); }
        surface = {}; surfaceBank = 0; shownOnSurface.clear();
        if (wanted.input.isEmpty()) return Result::ok();
        if (! portAllowed (wanted.input) || (wanted.output.isNotEmpty() && ! portAllowed (wanted.output))) return Result::fail ("That MIDI port is not available.");
        juce::MidiDeviceInfo inputInfo, outputInfo;
        for (const auto& info : juce::MidiInput::getAvailableDevices()) if (info.name == wanted.input) inputInfo = info;
        for (const auto& info : juce::MidiOutput::getAvailableDevices()) if (info.name == wanted.output) outputInfo = info;
        if (inputInfo.identifier.isEmpty()) return Result::fail ("The MIDI input \"" + wanted.input + "\" is not available.");
        if (wanted.output.isNotEmpty() && outputInfo.identifier.isEmpty()) return Result::fail ("The MIDI output \"" + wanted.output + "\" is not available.");
        // The engine's own input for the port stays closed, so the surface plays into no track.
        for (auto device : engine.getDeviceManager().getMidiInDevices())
            if (device->getName() == wanted.input && device->isEnabled()) device->setEnabled (false);
        surfacePort.handle = [this] (const juce::MidiMessage& m) { handleSurface (m); };
        surfaceIn = juce::MidiInput::openDevice (inputInfo.identifier, &surfacePort);
        if (surfaceIn == nullptr) return Result::fail ("The MIDI input \"" + wanted.input + "\" could not be opened.");
        if (wanted.output.isNotEmpty())
        {
            surfaceOut = juce::MidiOutput::openDevice (outputInfo.identifier);
            if (surfaceOut == nullptr) { closeSurface(); return Result::fail ("The MIDI output \"" + wanted.output + "\" could not be opened."); }
        }
        surfaceIn->start();
        surface = wanted;
        sendSurfaceState();
        surfaceTimer.tick = [this] { sendSurfaceState(); };
        surfaceTimer.startTimerHz (20);
        return Result::ok();
    }
    // Input permission (see Session::inputPermission): its source, and whether a prompt is open.
    std::function<InputPermission()> permissionStatus = [] { return InputPermission::granted; };
    std::function<void (std::function<void (bool)>)> permissionRequest = [] (std::function<void (bool)> done) { done (true); };
    bool permissionAsked = false;
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);
    // Why a hardware audio input cannot be used now. An undetermined permission is asked for once
    // at a time; when it is granted, the audio device reopens so its inputs deliver sound.
    Result hardwareInputAllowed (te::InputDeviceInstance& input)
    {
        if (input.getInputDevice().getDeviceType() != te::InputDevice::waveDevice) return Result::ok();
        switch (permissionStatus())
        {
            case InputPermission::granted: return Result::ok();
            case InputPermission::undetermined:
                if (! permissionAsked)
                {
                    permissionAsked = true;
                    permissionRequest ([this, alive = std::weak_ptr<int> (lifetime)] (bool granted)
                    {
                        if (alive.expired()) return;
                        permissionAsked = false;
                        if (granted && useDevices)
                        {
                            auto& devices = engine.getDeviceManager().deviceManager;
                            devices.closeAudioDevice(); devices.restartLastAudioDevice();
                        }
                        owner.sendChangeMessage();
                    });
                }
                return Result::fail ("Halftrack is asking for permission to use audio inputs. Allow it, then try again.");
            case InputPermission::denied: break;
        }
        return Result::fail ("Halftrack has no permission to use audio inputs, so this input would record silence. Allow Halftrack under "
                            "System Settings > Privacy & Security > Microphone, then try again.");
    }
    bool applyingRemote = false; // While a mapped controller's value is applied.
    // Auto-Arm: the track it armed, and whether it also gave that track its input.
    String autoArmed;
    bool autoInput = false;
    void releaseAutoArm()
    {
        if (autoArmed.isEmpty()) return;
        if (auto* t = track (autoArmed))
            if (auto* in = input (*t))
            {
                if (auto* destination = te::getDestination (*in, *t, 0)) destination->recordEnabled.setValue (false, nullptr);
                if (autoInput)
                {
                    [[maybe_unused]] auto removed = in->removeTarget (t->itemID, nullptr);
                    t->state.setProperty (monitorProperty, static_cast<int> (MonitorMode::off), nullptr);
                }
            }
        autoArmed.clear(); autoInput = false;
    }
    // Key Map Mode's learn target, and each button-mapped controller's last value (rising past 63 presses).
    String keyLearnTarget;
    std::map<String, int> buttonValues;
    int trackIndexOf (const String& id) const
    {
        const auto tracks = te::getAudioTracks (*edit);
        for (int i = 0; i < tracks.size(); ++i) if (tracks[i]->itemID.toString() == id) return i;
        return -1;
    }
    int sceneIndexOf (const String& id) const
    {
        for (auto* scene : edit->getSceneList().getScenes()) if (scene->state["halftrackSceneID"].toString() == id) return scene->getIndex();
        return -1;
    }
    // Where a device is, as track and position in the snapshot's device list, or -1s. Read from the
    // engine, as the snapshot asks this of its mappings.
    std::pair<int, int> devicePosition (const String& pluginId) const
    {
        const int count = te::getAudioTracks (*edit).size();
        for (int t = 0; t < count; ++t)
        {
            const auto list = plugins (t);
            for (int p = 0; p < list.size(); ++p) if (list[p]->itemID.toString() == pluginId) return { t, p };
        }
        return { -1, -1 };
    }
    // Whether a button target names something that exists.
    bool buttonExists (const String& target) const
    {
        const auto parts = juce::StringArray::fromTokens (target, ":", "");
        if (parts[0] == "transport") return parts.size() == 2 && juce::StringArray { "play", "stop", "record", "loop", "metronome", "stopClips" }.contains (parts[1]);
        if (parts[0] == "track") { const auto* t = track (parts[1]); return parts.size() == 3 && t != nullptr && ! isReturn (*t) && juce::StringArray { "mute", "solo", "arm", "stop" }.contains (parts[2]); }
        if (parts[0] == "slot") { const auto* t = track (parts[1]); return parts.size() == 3 && t != nullptr && ! isReturn (*t) && sceneIndexOf (parts[2]) >= 0; }
        if (parts[0] == "scene") return parts.size() == 2 && sceneIndexOf (parts[1]) >= 0;
        if (parts[0] == "device") return parts.size() == 3 && parts[2] == "on" && devicePosition (parts[1]).first >= 0;
        return false;
    }
    // Presses a mapped button as a click would.
    Result pressButton (const String& target)
    {
        const auto parts = juce::StringArray::fromTokens (target, ":", "");
        const auto snapshot = owner.snapshot();
        if (parts[0] == "transport")
        {
            if (parts[1] == "play") return snapshot.playing ? Result::ok() : owner.play();
            if (parts[1] == "stop") { owner.stop(); return Result::ok(); }
            if (parts[1] == "record") { if (snapshot.recording) { owner.stop(); return Result::ok(); } return owner.record(); }
            if (parts[1] == "loop") { owner.setLoop (! snapshot.looping, snapshot.loopStart, snapshot.loopEnd); return Result::ok(); }
            if (parts[1] == "metronome") { auto metronome = snapshot.metronome; metronome.enabled = ! metronome.enabled; return owner.setMetronome (metronome); }
            if (parts[1] == "stopClips") return owner.stopAllLauncherClips();
        }
        if (parts[0] == "track")
        {
            const int index = trackIndexOf (parts[1]);
            if (index < 0) return Result::ok();
            const auto& info = snapshot.tracks[(size_t) index];
            if (parts[2] == "mute") return owner.setTrackMute (index, ! info.mute);
            if (parts[2] == "solo") return owner.setTrackSolo (index, ! info.solo);
            if (parts[2] == "arm") return owner.setTrackArmed (index, ! info.armed);
            if (parts[2] == "stop") return owner.stopLauncherTrack (index);
        }
        if (parts[0] == "slot") { const int t = trackIndexOf (parts[1]), scene = sceneIndexOf (parts[2]); return t >= 0 && scene >= 0 ? owner.launchSlot (t, scene) : Result::ok(); }
        if (parts[0] == "scene") { const int scene = sceneIndexOf (parts[1]); return scene >= 0 ? owner.launchScene (scene) : Result::ok(); }
        if (parts[0] == "device")
        {
            const auto [t, p] = devicePosition (parts[1]);
            return t >= 0 ? owner.setPluginEnabled (t, p, ! plugins (t)[p]->isEnabled()) : Result::ok();
        }
        return Result::ok();
    }
    // A mapped control's current value in its own units.
    std::optional<float> targetValue (const String& target) const
    {
        const auto parts = juce::StringArray::fromTokens (target, ":", "");
        if (target == "master") return edit->getMasterVolumePlugin()->getVolumeDb();
        if (parts[0] == "mixer")
            if (auto* t = track (parts[1])) if (auto* mixer = t->getVolumePlugin()) return parts[2] == "pan" ? mixer->getPan() : mixer->getVolumeDb();
        if (parts[0] == "device")
        {
            if (auto* device = nativeDevice (parts[1])) return device->readSettings().values[(size_t) juce::jlimit (0, (int) device->readSettings().values.size() - 1, parts[2].getIntValue())];
            if (auto* external = externalPlugin (parts[1]))
                if (const auto parameters = externalParameters (*external); juce::isPositiveAndBelow (parts[2].getIntValue(), (int) parameters.size()))
                    return parameters[(size_t) parts[2].getIntValue()]->getCurrentNormalisedValue();
        }
        return std::nullopt;
    }
    // Sets a mapped control to a value in its own units.
    Result applyValue (const String& target, float value)
    {
        const auto parts = juce::StringArray::fromTokens (target, ":", "");
        if (target == "master") return owner.setMasterGain (value);
        if (parts[0] == "mixer")
        {
            const int index = trackIndexOf (parts[1]);
            return index < 0 ? Result::ok() : parts[2] == "pan" ? owner.setTrackPan (index, value) : owner.setTrackGain (index, value);
        }
        if (externalPlugin (parts[1]) != nullptr) return owner.setPluginParameterValue (parts[1], parts[2].getIntValue(), value);
        auto* device = nativeDevice (parts[1]);
        if (device == nullptr) return Result::ok();
        auto settings = device->readSettings();
        settings.values[(size_t) parts[2].getIntValue()] = value;
        return owner.setNativeEffectSettings (parts[1], settings);
    }
    // The computer MIDI keyboard's input, made when the Session starts; whether keys play it; its held notes.
    std::shared_ptr<te::VirtualMidiInputDevice> computerKeyboard;
    bool computerKeys = false;
    std::bitset<128> keyboardHeld;
    void ensureComputerKeyboard()
    {
        auto& devices = engine.getDeviceManager();
        const auto find = [&devices]
        {
            for (auto device : devices.getMidiInDevices())
                if (device->getName() == Session::computerKeyboardName)
                    if (auto keyboard = std::dynamic_pointer_cast<te::VirtualMidiInputDevice> (device)) return keyboard;
            return std::shared_ptr<te::VirtualMidiInputDevice>();
        };
        computerKeyboard = find();
        if (computerKeyboard == nullptr && devices.createVirtualMidiDevice (Session::computerKeyboardName).wasOk())
        { devices.rescanMidiDeviceListSynchronously(); computerKeyboard = find(); }
        if (computerKeyboard == nullptr) return;
        computerKeyboard->setMIDIInputSourceDevices ({}); // Only the keys play it.
        computerKeyboard->setEnabled (true);
    }
    // Plays a message from the keys into the Computer Keyboard input and All MIDI Ins.
    void playComputerKeyboard (juce::MidiMessage message)
    {
        auto& devices = engine.getDeviceManager();
        for (auto device : devices.getMidiInDevices())
            if (auto input = std::dynamic_pointer_cast<te::VirtualMidiInputDevice> (device);
                input != nullptr && input->isEnabled() && (input == computerKeyboard || input->useAllInputs))
            {
                message.setTimeStamp (devices.getCurrentStreamTime() - input->getAdjustSecs());
                input->handleIncomingMidiMessage (message, input->getMPESourceID());
            }
    }
    uint32_t lastRemote = 0;
    // Input instances are rebuilt when devices come and go, so the remote reattaches each second.
    RemoteRest remoteWatch;
    // Browser preview: its own Edit, playing beside the project's.
    std::unique_ptr<te::Edit> preview;
    File previewSource;
    float previewVolumeDb = 0;
    bool previewPlaying() const
    {
        if (! preview) return false;
        auto& transport = preview->getTransport();
        return transport.isPlaying() && transport.getPosition() < transport.getLoopRange().getEnd();
    }
    bool useDevices;
    uint64_t projectGeneration = 0;
    bool explicitChanges = false, pristineNewDocument = false, exporting = false;
    double lastTap=-1;
    std::array<double,4> tapIntervals {};
    int numTapIntervals=0, nextTapInterval=0;
    File scratch, projectFile;
    juce::ValueTree audioClipboard, midiClipboard;
    double midiClipboardBeats = 0, midiClipboardOffsetBeats = 0;
    // A time selection's clipboard: each copied track's clips overlapping the range, whole, in
    // track order, with each audio file kept in a temporary copy.
    struct RangeClipboard
    {
        double start = 0, length = 0;
        // An automated control's shape over the range, from its start, and its values at the edges.
        struct Point { double seconds; float value, shape; };
        struct Curve { juce::String key; std::vector<Point> points; float startValue = 0, endValue = 0; };
        struct Track { bool midi = false; std::vector<juce::ValueTree> clips; std::vector<Curve> curves; };
        std::vector<Track> tracks;
        std::map<juce::String, std::unique_ptr<juce::TemporaryFile>> media; // By the audio's own path.
    };
    std::unique_ptr<RangeClipboard> rangeClipboard;
    std::unique_ptr<juce::TemporaryFile> audioClipboardMedia;
    // A copied Session clip, with its audio's own copy for an audio clip.
    juce::ValueTree slotClipboard;
    std::unique_ptr<juce::TemporaryFile> slotClipboardMedia;
    std::unique_ptr<te::Edit> edit;
    GrooveWatcher grooveWatcher { edit };
    std::unique_ptr<te::Edit::UndoTransactionInhibitor> gesture;
    mutable std::map<String, std::unique_ptr<Meter>> meters;
    DeviceRestart deviceRestart { *this };
    // The audio device the user chose. When it disappears the device manager falls back to another,
    // so playback stops instead of moving there; when it returns it is reopened.
    struct DeviceWatch final : juce::ChangeListener
    {
        explicit DeviceWatch (Impl& i) : impl (i) {}
        void changeListenerCallback (juce::ChangeBroadcaster*) override { impl.deviceChanged(); }
        Impl& impl;
    } deviceWatch { *this };
    juce::AudioDeviceManager::AudioDeviceSetup chosenSetup;
    String chosenDevice, deviceNotice;
    bool chosenMissing = false;
    bool chosenAvailable() const
    {
        auto* type = engine.getDeviceManager().deviceManager.getCurrentDeviceTypeObject();
        return type != nullptr && (type->getDeviceNames (false).contains (chosenDevice) || type->getDeviceNames (true).contains (chosenDevice));
    }
    void deviceChanged()
    {
        auto& devices = engine.getDeviceManager().deviceManager;
        auto* current = devices.getCurrentAudioDevice();
        const auto name = current != nullptr ? current->getName() : String();
        if (chosenDevice.isEmpty() || (! chosenMissing && chosenAvailable()))
        {
            // The first device, or one chosen while the previous choice is still there.
            if (name.isNotEmpty()) { chosenDevice = name; chosenSetup = devices.getAudioDeviceSetup(); }
            return;
        }
        if (! chosenAvailable())
        {
            if (chosenMissing) return;
            chosenMissing = true;
            deviceRestart.stopTimer(); deviceStops.resumePlayback = false;
            if (edit != nullptr && edit->getTransport().isPlaying()) owner.stop();
            deviceNotice = "\"" + chosenDevice + "\" was disconnected. Playback stopped"
                         + (name.isNotEmpty() ? "; audio goes to \"" + name + "\" until \"" + chosenDevice + "\" returns." : String ("; it is reopened when it returns."));
            owner.sendChangeMessage();
            return;
        }
        // The chosen device is back.
        chosenMissing = false;
        if (name == chosenDevice) return;
        const auto error = devices.setAudioDeviceSetup (chosenSetup, true);
        deviceNotice = error.isEmpty() ? "\"" + chosenDevice + "\" is connected again." : "\"" + chosenDevice + "\" is back but could not be reopened: " + error;
        owner.sendChangeMessage();
    }
    DeviceStopWatch deviceStops { *this };
};

Session::Session (bool enableAudioDevices, std::unique_ptr<juce::AudioIODeviceType> customDeviceType, juce::String midiPortPrefix)
    : impl (std::make_unique<Impl> (*this, enableAudioDevices, std::move (customDeviceType), midiPortPrefix))
{
    warp::registerStretchers(); impl->loadPreferences();
    if (enableAudioDevices) impl->ensureComputerKeyboard();
    else if (midiPortPrefix.isNotEmpty()) impl->engine.getDeviceManager().rescanMidiDeviceListSynchronously();
    newProject();
    // The control surface chosen before, if its ports are still there.
    if (enableAudioDevices)
    {
        auto& storage = impl->engine.getPropertyStorage();
        const ControlSurface saved { storage.getPropertyItem (te::SettingID::midiin, "halftrackSurfaceInput", {}).toString(), storage.getPropertyItem (te::SettingID::midiin, "halftrackSurfaceOutput", {}).toString() };
        if (saved.input.isNotEmpty()) impl->openSurface (saved);
    }
    // MIDI Clock from the inputs whose Sync switch is on.
    impl->openClockInputs();
}
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
    s.sessionRecording = ! impl->slotRecordings.empty() || ! impl->overdubs.empty();
    s.looping = transport.looping;
    s.position = transport.getPosition().inSeconds();
    s.insertMarker = transport.startPosition.get().inSeconds();
    s.duration = e.getLength().inSeconds();
    s.tempo = e.tempoSequence.getBpmAt (te::TimePosition::fromSeconds (juce::jmax (0.0, s.position)));
    s.tempoMap.clear();
    for (int i = 0; i < e.tempoSequence.getNumTempos(); ++i)
        if (auto* t = e.tempoSequence.getTempo (i)) s.tempoMap.push_back ({ t->startBeatNumber.get().inBeats(), t->getBpm() });
    if (auto* meter = e.tempoSequence.getTimeSig (0)) s.timeSignature = { meter->numerator.get(), meter->denominator.get() };
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
    {
        SceneInfo info { scene->state.getProperty ("halftrackSceneID").toString(), scene->name.get(), {}, {} };
        if (scene->state.hasProperty (sceneTempoProperty)) info.tempo = (double) scene->state[sceneTempoProperty];
        if (scene->state.hasProperty (sceneNumeratorProperty))
            info.timeSignature = TimeSignature { (int) scene->state[sceneNumeratorProperty], (int) scene->state[sceneDenominatorProperty] };
        s.scenes.push_back (std::move (info));
    }
    for (const auto& groove : e.state.getChildWithName (groovePoolType)) s.grooves.push_back (readGroove (groove));
    s.grooveAmount = grooveAmount (e.state);
    s.automationArm = impl->automationArmed();
    s.punchIn = impl->punchIn(); s.punchOut = impl->punchOut();
    s.previewing = impl->previewPlaying() ? impl->previewSource : File();
    s.previewVolumeDb = impl->previewVolumeDb;
    for (const auto& m : e.state.getChildWithName (midiMapType))
        if (auto mapping = readMidiMapping (m); isMappableTarget (mapping.target) && impl->mappingCurrent (m)) s.midiMappings.push_back (mapping);
    s.midiLearnTarget = impl->midiLearnTarget;
    for (const auto& m : e.state.getChildWithName (keyMapType))
        if (auto mapping = readKeyMapping (m); isMappableTarget (mapping.target)) s.keyMappings.push_back (mapping);
    s.keyLearnTarget = impl->keyLearnTarget;
    s.automationOverridden = ! impl->overriddenLanes().isEmpty();
    const auto quantisation = e.getLaunchQuantisation().type.get();
    s.launchQuantisation = quantisation == te::LaunchQType::none ? LaunchQuantisation::none
        : quantisation == te::LaunchQType::quarter ? LaunchQuantisation::beat : LaunchQuantisation::bar;
    int index = 0;
    std::map<String, int> linkSizes;
    for (auto* t : te::getAudioTracks (e)) if (const auto link = t->state[linkProperty].toString(); link.isNotEmpty()) ++linkSizes[link];
    for (auto* t : te::getAudioTracks (e))
    {
        TrackInfo info;
        info.id = t->itemID.toString(); info.name = t->getName(); info.colour = t->getColour();
        if (const auto link = t->state[linkProperty].toString(); link.isNotEmpty() && linkSizes[link] > 1) info.linkId = link;
        info.autoArmed = info.id == impl->autoArmed;
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
        if (auto* native = Impl::nativeInstrument (*t)) info.instrument = describeDevice (*native);
        if (auto* v = t->getVolumePlugin())
        {
            info.gainDb = v->getVolumeDb(); info.pan = v->getPan();
            info.volumeAutomation = automationLane (*v, AutomationTarget::volume);
            info.panAutomation = automationLane (*v, AutomationTarget::pan);
        }
        if (auto* input = impl->input (*t))
        {
            if (! input->getInputDevice().isTrackDevice()) info.inputName = input->getInputDevice().getName();
            else if (auto* source = impl->sourceOf (input->getInputDevice()))
            {
                info.inputTrackId = source->itemID.toString();
                if (auto* destination = te::getDestination (*input, *t, 0))
                    info.inputTap = static_cast<InputTap> (juce::jlimit (0, 2, static_cast<int> (destination->state.getProperty (inputTapProperty, 2))));
            }
            info.armed = input->isRecordingEnabled (t->itemID);
            info.monitor = static_cast<MonitorMode> (input->getInputDevice().getMonitorMode());
        }
        if (auto* level = t->getLevelMeterPlugin())
        {
            auto& m = impl->meter (info.id, level->measurer);
            info.peakL = m.read (0); info.peakR = m.read (1);
        }
        if (impl->audition && impl->audition->trackId == t->itemID)
        {
            // An auditioned track still shows its main lane.
            for (const auto& [index, v] : impl->audition->main) info.clips.push_back (describeLaneClip (v, e));
            info.auditionLane = impl->audition->laneId;
        }
        else for (auto* c : t->getClips()) info.clips.push_back (describeClip (*c));
        for (auto lane : t->state.getChildWithName (takeLanesType))
        {
            TakeLaneInfo laneInfo { lane[te::IDs::id].toString(), lane[te::IDs::name].toString(), {} };
            for (auto clip : lane) laneInfo.clips.push_back (describeLaneClip (clip, e));
            info.takeLanes.push_back (std::move (laneInfo));
        }
        info.launcherActive = t->playSlotClips.get() || (t->launcherOwnership.load (std::memory_order_acquire) & 1) != 0;
        if (const int channel = static_cast<int> (t->state.getProperty (midiOutChannelProperty, 0)); channel > 0) info.output = { OutputKind::midiPort, {}, t->getOutput().getOutputName(), channel };
        else if (auto* destination = t->getOutput().getDestinationTrack()) info.output = { OutputKind::track, destination->itemID.toString() };
        else if (t->getOutput().outputsToNone()) info.output = { OutputKind::sendsOnly, {} };
        info.hasInstrument = info.isMidi && Impl::instrument (*t) != nullptr;
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
                slotInfo.overdubbing = std::any_of (impl->overdubs.begin(), impl->overdubs.end(), [c] (const auto& o) { return o.clipId == c->itemID; });
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
        for (auto* p : impl->plugins (index++))
        {
            PluginInfo device { p->itemID.toString(), p->getName(), p->isEnabled() };
            if (auto* utility = dynamic_cast<UtilityPlugin*> (p)) { device.isUtility = true; device.utility = utility->readSettings(); }
            if (auto* effect = dynamic_cast<NativeEffectPlugin*> (p)) device = describeDevice (*effect);
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (p))
            {
                device.isInstrument = static_cast<bool> (external->state.getProperty (hostedInstrumentProperty, false));
                device.automation = externalAutomation (*external);
                const auto parameters = externalParameters (*external);
                for (const auto& configured : external->state.getChildWithName (configureType))
                    for (size_t i = 0; i < parameters.size(); ++i)
                        if (parameters[i]->paramID == configured[te::IDs::id].toString()) { device.configured.push_back ((int) i); break; }
                if (external->getAudioPluginInstance() != nullptr)
                {
                    const auto buses = external->getBusses();
                    for (const auto& bus : buses.inputs) device.inputBuses.push_back (bus.getNumChannels());
                    for (const auto& bus : buses.outputs) device.outputBuses.push_back (bus.getNumChannels());
                    device.canSidechain = hasSidechainInput (*external);
                }
                device.sidechain = sidechainSettings (*external);
            }
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (p); external && ! external->isInitialisingAsync() && external->getAudioPluginInstance() == nullptr)
            {
                device.missing = true; device.loadError = external->getLoadError();
                // The engine's generic message says nothing useful.
                if (device.loadError.isEmpty() || device.loadError.containsIgnoreCase ("couldn't be loaded")) device.loadError = "It is not installed, or has not been scanned.";
                if (juce::File::isAbsolutePath (external->desc.fileOrIdentifier))
                    if (const auto problem = pluginbinary::architectureProblem (juce::File (external->desc.fileOrIdentifier), device.name); problem.isNotEmpty()) device.loadError = problem;
            }
            info.plugins.push_back (std::move (device));
        }
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
    if (impl->edit) stop(); // Ends Session takes in the replaced project, as loading does.
    impl->meters.clear(); impl->heldSlots.clear();
    impl->projectFile = File{};
    auto state = te::createEmptyEdit (impl->engine);
    auto options = te::Edit::Options { impl->engine, state, te::ProjectItemID::fromProperty (state, te::IDs::projectID) };
    options.numAudioTracks = 0; options.defaultMasterVolumedB = 0;
    // The Impl, not Session::impl: the Edit is destroyed inside ~Impl, when impl is already null.
    options.editFileRetriever = [owner = impl.get()] { return owner->editLocation(); };
    impl->audition.reset(); // Its states belong to the replaced Edit.
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
    if (auto result = validateSceneSettings (state); result.failed()) return result;
    if (auto result = validateRecordQuantisation (state); result.failed()) return result;
    if (auto result = validateCrossfade (state); result.failed()) return result;
    if (auto result = validateMasterVolume (state); result.failed()) return result;
    if (auto result = validateMixerAutomation (state); result.failed()) return result;
    if (auto result = validateRouting (state); result.failed()) return result;
    if (auto result = UtilityPlugin::validateState (state); result.failed()) return result;
    if (auto result = NativeEffectPlugin::validateState (state); result.failed()) return result;
    if (auto result = validateRackIdentities (state); result.failed()) return result;
    if (auto result = NativeEffectPlugin::resolveSamples (state, file.getParentDirectory()); result.failed()) return result;
    if (auto result = validateAudioClips (state); result.failed()) return result;
    if (auto result = validateCurveModifiers (state); result.failed()) return result;
    if (auto result = validateFollowActions (state); result.failed()) return result;
    if (auto result = validateLocators (state); result.failed()) return result;
    if (auto result = validateTrackDelays (state); result.failed()) return result;
    if (auto result = validateTrackLinks (state); result.failed()) return result;
    if (auto result = validateHostedInstruments (state); result.failed()) return result;
    if (auto result = validateSidechains (state); result.failed()) return result;
    if (auto result = validateMidiPortOutputs (state); result.failed()) return result;
    if (auto result = validateClipScales (state); result.failed()) return result;
    if (auto result = validateGroovePool (state); result.failed()) return result;
    if (auto result = validateMidiMappings (state); result.failed()) return result;
    if (auto result = validateConfigurations (state); result.failed()) return result;
    if (auto result = validateKeyMappings (state); result.failed()) return result;
    if (state.hasProperty (automationArmProperty) && ! (finiteProperty (state[automationArmProperty], 0, 1) && (state[automationArmProperty].toString().trim() == "0" || state[automationArmProperty].toString().trim() == "1")))
        return Result::fail ("The project's Automation Arm setting is invalid.");
    for (const auto& property : { punchInProperty, punchOutProperty })
        if (state.hasProperty (property) && state[property].toString().trim() != "0" && state[property].toString().trim() != "1")
            return Result::fail ("The project's punch settings are invalid.");
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
    // Plug-ins that cannot load stay as placeholders; the snapshot marks them missing.
    for (auto* t : te::getAudioTracks (*candidate))
        for (auto* c : allTrackClips (*t))
            if (c->usesSourceFile())
            {
                auto& reference = c->getSourceFileReference();
                const auto resolved = reference.getFile();
                // A missing file keeps its reference; the clip plays silence until it is located.
                // In-memory undo can retain detached clip trees across Save As.
                // Anchor their media before edits; only the saved copy is relative.
                reference.source.setValue (resolved.getFullPathName(), nullptr);
            }
    // Take lane audio is anchored the same way.
    for (auto* t : te::getAudioTracks (*candidate))
        for (auto lane : t->state.getChildWithName (takeLanesType))
            for (auto clip : lane)
                if (clip.hasType (te::IDs::AUDIOCLIP))
                    clip.setProperty (te::IDs::source, te::SourceFileReference::findFileFromString (*candidate, clip[te::IDs::source].toString()).getFullPathName(), nullptr);
    endEditGesture(); closePluginEditors(); stop(); impl->meters.clear();
    impl->projectFile = file;
    impl->pristineNewDocument = false;
    impl->audition.reset(); // Its states belong to the replaced Edit.
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
    impl->reconcileMidiMappings();
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

Result Session::writeRecovery (const File& destination)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Recovery copies wait until recording stops.");
    if (auto r = destination.getParentDirectory().createDirectory(); r.failed()) return r;
    impl->edit->flushState();
    juce::XmlElement document ("HALFTRACK");
    document.setAttribute ("version", 1);
    if (impl->projectFile != File()) document.setAttribute ("halftrackRecoveryOf", impl->projectFile.getFullPathName());
    document.addChildElement (impl->edit->state.createCopy().createXml().release());
    juce::TemporaryFile temp (destination);
    if (! document.writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary()) return Result::fail ("Could not write the recovery copy.");
    return Result::ok();
}
Result Session::saveDefaultSet (const File& destination)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before saving a default set.");
    if (auto r = destination.getParentDirectory().createDirectory(); r.failed()) return r;
    impl->edit->flushState();
    juce::XmlElement document ("HALFTRACK");
    document.setAttribute ("version", 1);
    document.addChildElement (impl->edit->state.createCopy().createXml().release());
    juce::TemporaryFile temp (destination);
    if (! document.writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary()) return Result::fail ("Could not write the default set.");
    return Result::ok();
}
Result Session::newProjectFromDefaultSet (const File& defaultSet)
{
    if (auto r = loadProject (defaultSet); r.failed()) return r;
    // A new project from the template: untitled and unchanged until edited.
    impl->projectFile = File{};
    impl->edit->resetChangedStatus(); impl->explicitChanges = false; impl->pristineNewDocument = true;
    sendChangeMessage(); return Result::ok();
}
namespace
{
const juce::StringArray utilityKeys { "gainDb", "balance", "width", "midSide", "bassFrequency", "channel", "midSideMode", "invertLeft", "invertRight",
                                      "mono", "bassMono", "bassAudition", "mute", "dcFilter" };
bool parseNumber (const juce::String& text, double& value)
{
    const auto raw = text.trim().toStdString(); char* end = nullptr;
    value = std::strtod (raw.c_str(), &end);
    return ! raw.empty() && end == raw.c_str() + raw.size() && std::isfinite (value);
}
}
Result Session::readDevicePreset (const File& file, DevicePreset& preset)
{
    const auto fail = [] { return Result::fail ("This is not a valid Halftrack device preset."); };
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("HALFTRACKPRESET") || xml->getIntAttribute ("version") != 1) return fail();
    auto* values = xml->getChildByName ("SETTINGS");
    if (values == nullptr) return fail();
    DevicePreset result; result.name = xml->getStringAttribute ("name").trim();
    if (result.name.isEmpty() || result.name.length() > 128) return fail();
    const auto device = xml->getStringAttribute ("device");
    if (device == "utility")
    {
        result.utility = true;
        std::array<double, 14> v { 0, 0, 1, 0, 120, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        for (int i = 0; i < utilityKeys.size(); ++i)
            if (values->hasAttribute (utilityKeys[i]) && ! parseNumber (values->getStringAttribute (utilityKeys[i]), v[(size_t) i])) return fail();
        auto& u = result.settings;
        u.gainDb = (float) v[0]; u.balance = (float) v[1]; u.width = (float) v[2]; u.midSide = (float) v[3]; u.bassFrequency = (float) v[4];
        if (v[5] < 0 || v[5] > 3 || v[5] != std::floor (v[5])) return fail();
        u.channel = static_cast<UtilitySettings::Channel> ((int) v[5]);
        for (size_t i = 6; i < 14; ++i) if (v[i] != 0 && v[i] != 1) return fail();
        u.midSideMode = v[6] != 0; u.invertLeft = v[7] != 0; u.invertRight = v[8] != 0; u.mono = v[9] != 0;
        u.bassMono = v[10] != 0; u.bassAudition = v[11] != 0; u.mute = v[12] != 0; u.dcFilter = v[13] != 0;
        if (! UtilityPlugin::validSettings (u)) return fail();
    }
    else if (device == "native")
    {
        double kind = 0;
        if (! parseNumber (xml->getStringAttribute ("kind"), kind) || kind < 0 || kind >= deviceKindCount || kind != std::floor (kind)
            || effectIsRack (static_cast<EffectKind> ((int) kind))) return fail();
        result.effect = defaultEffectSettings (static_cast<EffectKind> ((int) kind));
        const auto fields = effectParameters (result.effect.kind);
        for (size_t i = 0; i < fields.size(); ++i)
            if (values->hasAttribute (fields[i].key))
            {
                double v = 0;
                if (! parseNumber (values->getStringAttribute (fields[i].key), v)) return fail();
                result.effect.values[i] = (float) v;
            }
        if (! validEffectSettings (result.effect)) return fail();
    }
    else if (device == "plugin")
    {
        auto* description = xml->getChildByName ("PLUGIN");
        auto* state = xml->getChildByName ("STATE");
        if (description == nullptr || description->getFirstChildElement() == nullptr || state == nullptr
            || ! result.description.loadFromXml (*description->getFirstChildElement()) || result.description.fileOrIdentifier.isEmpty()
            || ! result.state.fromBase64Encoding (state->getAllSubText().trim())) return fail();
        result.plugin = true;
    }
    else return fail();
    preset = result; return Result::ok();
}
Result Session::saveDevicePreset (const String& pluginId, const File& file, const String& name, bool overwrite)
{
    const auto label = name.trim();
    if (label.isEmpty() || label.length() > 128) return Result::fail ("Presets need a name of up to 128 characters.");
    if (file.exists() && ! overwrite) return Result::fail (file.getFileName() + " already exists.");
    juce::XmlElement document ("HALFTRACKPRESET");
    document.setAttribute ("version", 1); document.setAttribute ("name", label);
    auto* values = document.createNewChildElement ("SETTINGS");
    if (auto* native = impl->nativeDevice (pluginId))
    {
        if (effectIsRack (native->getKind())) return Result::fail ("Rack presets are not supported yet.");
        const auto settings = native->readSettings(); const auto fields = effectParameters (settings.kind);
        document.setAttribute ("device", "native"); document.setAttribute ("kind", static_cast<int> (settings.kind));
        for (size_t i = 0; i < fields.size(); ++i) values->setAttribute (fields[i].key, (double) settings.values[i]);
    }
    else if (auto* utility = impl->utility (pluginId))
    {
        const auto u = utility->readSettings();
        document.setAttribute ("device", "utility");
        const double v[] { u.gainDb, u.balance, u.width, u.midSide, u.bassFrequency, (double) static_cast<int> (u.channel), u.midSideMode ? 1.0 : 0.0,
                           u.invertLeft ? 1.0 : 0.0, u.invertRight ? 1.0 : 0.0, u.mono ? 1.0 : 0.0, u.bassMono ? 1.0 : 0.0, u.bassAudition ? 1.0 : 0.0,
                           u.mute ? 1.0 : 0.0, u.dcFilter ? 1.0 : 0.0 };
        for (int i = 0; i < utilityKeys.size(); ++i) values->setAttribute (utilityKeys[i], v[i]);
    }
    else if (auto* external = impl->externalPlugin (pluginId))
    {
        // A plug-in's preset is its identity and the state it saves, restored when it is loaded.
        auto* instance = external->getAudioPluginInstance();
        if (instance == nullptr || external->isMissing()) return Result::fail ("The plug-in is not loaded, so its state cannot be saved.");
        juce::MemoryBlock state; instance->getStateInformation (state);
        document.setAttribute ("device", "plugin");
        document.createNewChildElement ("PLUGIN")->addChildElement (external->desc.createXml().release());
        document.createNewChildElement ("STATE")->addTextElement (state.toBase64Encoding());
    }
    else return Result::fail ("Presets can be saved from devices and plug-ins.");
    if (auto r = file.getParentDirectory().createDirectory(); r.failed()) return r;
    juce::TemporaryFile temp (file);
    if (! document.writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary()) return Result::fail ("Could not write the preset.");
    return Result::ok();
}
Result Session::loadDevicePreset (int trackIndex, const File& file, String& pluginId)
{
    DevicePreset preset;
    if (auto r = readDevicePreset (file, preset); r.failed()) return r;
    auto* track = impl->track (trackIndex);
    if (track == nullptr) return Result::fail ("Track not found.");
    const auto before = snapshot().tracks[(size_t) trackIndex];
    if (preset.plugin)
    {
        // A scanned plug-in of the same identity is used where it is now, since a plug-in can
        // report a location inside its bundle that its format then does not recognise.
        auto description = preset.description;
        for (const auto& known : knownPlugins().getTypes())
            if (known.pluginFormatName == description.pluginFormatName && known.uniqueId == description.uniqueId && known.name == description.name)
            { description = known; break; }
        // The plug-in, then its saved state, in one undo step.
        beginEditGesture ("Load " + preset.name);
        auto result = addPlugin (trackIndex, description);
        const bool added = result.wasOk();
        if (added)
        {
            const auto after = snapshot().tracks[(size_t) trackIndex];
            pluginId.clear();
            for (const auto& p : after.plugins)
                if (std::none_of (before.plugins.begin(), before.plugins.end(), [&] (const auto& old) { return old.id == p.id; })) pluginId = p.id;
            auto* external = impl->externalPlugin (pluginId);
            auto* instance = external != nullptr ? external->getAudioPluginInstance() : nullptr;
            if (instance == nullptr) result = Result::fail ("The preset's plug-in could not be found.");
            else { instance->setStateInformation (preset.state.getData(), (int) preset.state.getSize()); external->flushPluginStateToValueTree(); }
        }
        endEditGesture();
        if (result.failed()) { if (added && canUndo()) undo(); return result; }
        return Result::ok();
    }
    const bool instrument = ! preset.utility && effectIsInstrument (preset.effect.kind);
    if ((instrument || (! preset.utility && effectIsMidi (preset.effect.kind))) && ! before.isMidi) return Result::fail ("This preset needs a MIDI track.");
    beginEditGesture ("Load " + preset.name);
    auto result = preset.utility ? addUtility (trackIndex) : instrument ? setTrackInstrument (before.id, preset.effect.kind) : addNativeEffect (trackIndex, preset.effect.kind);
    const bool added = result.wasOk();
    if (added)
    {
        const auto after = snapshot().tracks[(size_t) trackIndex];
        pluginId.clear();
        if (instrument) pluginId = after.instrument.id;
        else for (const auto& p : after.plugins)
            if (std::none_of (before.plugins.begin(), before.plugins.end(), [&] (const auto& old) { return old.id == p.id; })) pluginId = p.id;
        if (pluginId.isEmpty()) result = Result::fail ("The preset's device could not be found.");
        else result = preset.utility ? setUtilitySettings (pluginId, preset.settings) : setNativeEffectSettings (pluginId, preset.effect);
    }
    endEditGesture();
    // A device added for a preset whose settings then failed is taken back out.
    if (result.failed()) { if (added && canUndo()) undo(); return result; }
    return Result::ok();
}
Result Session::sliceToNewMidiTrack (const String& clipId, SliceBy by, String& trackId)
{
    if (impl->exporting || impl->anyRecording()) return Result::fail ("Stop recording or finish exporting before slicing.");
    auto* clip = impl->audioClip (clipId);
    if (clip == nullptr || clip->getClipSlot() != nullptr) return Result::fail ("Choose an Arrangement audio clip to slice.");
    if (clip->isLooping()) return Result::fail ("Turn off the clip's loop before slicing it.");
    if (auto frozen = impl->unfrozen (clip->getTrack()); frozen.failed()) return frozen;
    const auto info = describeClip (*clip);
    const auto file = clip->getOriginalFile();
    if (info.mediaMissing || info.reversed || ! file.existsAsFile()) return Result::fail ("Slicing needs the clip's audio file, unreversed.");
    const bool warped = info.warp.mode != WarpMode::off;
    if (by == SliceBy::warpMarkers ? ! warped || info.warpMarkers.size() < 2 : warped && ! info.warpMarkers.empty())
        return Result::fail (by == SliceBy::warpMarkers ? "The clip has no warp markers to slice at." : "Slice a clip with warp markers at its markers.");
    // Clip beats from the clip's start, and the source seconds they play.
    const auto startBeat = beatsAt (info.start), lengthBeats = beatsAt (info.start + info.length) - startBeat;
    const auto offsetBeats = startBeat - beatsAt (std::max (0.0, info.start - info.sourceOffset));
    const auto sourceAt = [&] (double beat)
    {
        if (! warped) return info.sourceOffset + (secondsAt (startBeat + beat) - info.start);
        const auto sourceBeat = offsetBeats + beat;
        if (info.warpMarkers.size() < 2) return sourceBeat * 60 / info.warp.bpm;
        const auto& m = info.warpMarkers;
        size_t i = 1;
        while (i + 1 < m.size() && m[i].beat < sourceBeat) ++i;
        const auto t = (sourceBeat - m[i - 1].beat) / std::max (1e-9, m[i].beat - m[i - 1].beat);
        return m[i - 1].sourceSeconds + t * (m[i].sourceSeconds - m[i - 1].sourceSeconds);
    };
    std::vector<double> beats { 0.0 };
    const auto add = [&] (double beat) { if (beat > beats.back() + 1e-6 && beat < lengthBeats - 1e-6) beats.push_back (beat); };
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return Result::fail ("The clip's audio could not be read.");
    if (by == SliceBy::transients)
    {
        const auto from = (juce::int64) std::floor (std::max (0.0, sourceAt (0)) * reader->sampleRate);
        const auto to = std::min (reader->lengthInSamples, (juce::int64) std::ceil (sourceAt (lengthBeats) * reader->sampleRate));
        if (to - from > (juce::int64) (reader->sampleRate * 600)) return Result::fail ("Slice clips of up to ten minutes at their transients.");
        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) std::max<juce::int64> (1, to - from));
        reader->read (&audio, 0, audio.getNumSamples(), from, true, true);
        const auto beatOfSource = [&] (double seconds)
        {
            if (! warped) return beatsAt (info.start + seconds - info.sourceOffset) - startBeat;
            return seconds * info.warp.bpm / 60 - offsetBeats;
        };
        for (const auto frame : warp::findTransients (audio, reader->sampleRate)) add (beatOfSource ((double) (from + frame) / reader->sampleRate));
    }
    else if (by == SliceBy::warpMarkers)
    {
        for (const auto& marker : info.warpMarkers) add (marker.beat - offsetBeats);
    }
    else
    {
        const auto division = by == SliceBy::sixteenth ? .25 : by == SliceBy::eighth ? .5 : by == SliceBy::quarter ? 1.0 : by == SliceBy::half ? 2.0
                            : snapshot().timeSignature.beatsPerBar();
        for (double beat = division; beat < lengthBeats - 1e-6 && beats.size() <= 128; beat += division) add (beat);
    }
    if (beats.size() > 128) return Result::fail ("That makes more than 128 slices; slice at a coarser division.");
    beats.push_back (lengthBeats);
    // The Sampler starts at the frame below Start and ends at the frame nearest End, so Start sits
    // half a frame into its slice's first frame.
    const auto frames = (double) reader->lengthInSamples;
    const auto frameAt = [&] (double beat) { return juce::jlimit (0.0, frames, (double) std::llround (sourceAt (beat) * reader->sampleRate)); };
    const auto fields = effectParameters (EffectKind::sampler);
    const auto field = [&] (const char* key) { for (size_t i = 0; i < fields.size(); ++i) if (juce::String (fields[i].key) == key) return (int) i; return -1; };
    const int startField = field ("start"), endField = field ("end");
    const auto sourceIndex = te::getAudioTracks (*impl->edit).indexOf (dynamic_cast<te::AudioTrack*> (clip->getTrack()));

    beginEditGesture ("Slice to New MIDI Track");
    auto result = addMidiTrack (clip->getName() + " Slices");
    const auto created = result.wasOk();
    if (created)
    {
        trackId = snapshot().tracks.back().id;
        const auto destination = sourceIndex + 1;
        if (destination < (int) snapshot().tracks.size() - 1) result = moveTrack (trackId, destination);
    }
    if (result.wasOk()) result = setTrackInstrument (trackId, EffectKind::drumRack);
    const auto rackId = [&] { for (const auto& t : snapshot().tracks) if (t.id == trackId) return t.instrument.id; return String(); }();
    for (size_t i = 0; result.wasOk() && i + 1 < beats.size(); ++i)
    {
        String chainId;
        result = setDrumRackPadSample (rackId, 36 + (int) i, file, chainId);
        if (result.failed()) break;
        String samplerId;
        for (const auto& t : snapshot().tracks)
            if (t.id == trackId) for (const auto& chain : t.instrument.chains) if (chain.id == chainId && ! chain.devices.empty()) samplerId = chain.devices.front().id;
        auto* sampler = impl->nativeDevice (samplerId);
        if (sampler == nullptr || startField < 0 || endField < 0) { result = Result::fail ("A slice's Sampler could not be found."); break; }
        auto settings = sampler->readSettings();
        settings.values[(size_t) startField] = (float) juce::jlimit (0.0, 1.0, (frameAt (beats[i]) + 0.5) / frames);
        settings.values[(size_t) endField] = (float) juce::jlimit (0.0, 1.0, frameAt (beats[i + 1]) / frames);
        result = setNativeEffectSettings (samplerId, settings);
    }
    if (result.wasOk()) result = addMidiClip (trackId, startBeat, lengthBeats);
    if (result.wasOk())
    {
        String midiId;
        for (const auto& t : snapshot().tracks) if (t.id == trackId && ! t.clips.empty()) midiId = t.clips.front().id;
        std::vector<MidiNoteInfo> notes;
        for (size_t i = 0; i + 1 < beats.size(); ++i)
        {
            MidiNoteInfo note; note.pitch = 36 + (int) i; note.startBeat = beats[i];
            note.lengthBeats = std::max (1.0 / 64, beats[i + 1] - beats[i]); note.velocity = 1.0f;
            notes.push_back (note);
        }
        juce::StringArray inserted;
        result = addMidiNotes (midiId, notes, inserted);
    }
    endEditGesture();
    if (result.failed()) { if (created && canUndo()) undo(); trackId.clear(); return result; }
    return Result::ok();
}
std::vector<Session::AudioInputGroup> Session::audioInputGroups() const
{
    std::vector<AudioInputGroup> result;
    for (auto* device : impl->engine.getDeviceManager().getWaveInputDevices())
        if (! device->isTrackDevice())
            result.push_back ({ device->getName(), (int) device->deviceDescription.getDeviceChannelRange().first, (int) device->deviceDescription.getNumChannels(), device->isEnabled() });
    std::stable_sort (result.begin(), result.end(), [] (const auto& a, const auto& b) { return a.firstChannel < b.firstChannel; });
    return result;
}
Result Session::setAudioInputPairStereo (int firstChannel, bool stereo)
{
    if (impl->anyRecording()) return Result::fail ("Stop recording before changing the input configuration.");
    if (firstChannel < 0 || firstChannel % 2 != 0) return Result::fail ("Choose the first channel of an input pair.");
    auto& devices = impl->engine.getDeviceManager();
    for (auto* device : devices.getWaveInputDevices())
    {
        if (device->isTrackDevice() || (int) device->deviceDescription.getDeviceChannelRange().first != firstChannel) continue;
        const auto channels = device->deviceDescription.getNumChannels();
        auto* audio = devices.deviceManager.getCurrentAudioDevice();
        if (stereo && channels == 1 && (audio == nullptr || firstChannel + 1 >= audio->getInputChannelNames().size()))
            return Result::fail ("That input has no second channel to pair with.");
        if ((stereo ? 2u : 1u) == channels) return Result::ok();
        devices.setDeviceNumChannels (*device, stereo ? 2 : 1);
        devices.dispatchPendingUpdates();
        impl->refreshInputs(); impl->changed();
        return Result::ok();
    }
    return Result::fail ("There is no input at that channel.");
}
Result Session::setAudioInputEnabled (const String& inputName, bool enabled)
{
    if (impl->anyRecording()) return Result::fail ("Stop recording before changing the input configuration.");
    auto& devices = impl->engine.getDeviceManager();
    for (auto* device : devices.getWaveInputDevices())
        if (! device->isTrackDevice() && device->getName() == inputName)
        {
            if (device->isEnabled() != enabled) { devices.setDeviceEnabled (*device, enabled); devices.dispatchPendingUpdates(); impl->refreshInputs(); impl->changed(); }
            return Result::ok();
        }
    return Result::fail ("That input is not available.");
}
Session::Preferences Session::preferences() const { return impl->prefs; }
juce::File Session::preferencesFile() const { return impl->prefsFile(); }
Session::AudioLatency Session::audioLatency() const
{
    AudioLatency latency;
    latency.compensationMs = impl->prefs.driverErrorCompensationMs;
    if (auto* device = impl->engine.getDeviceManager().deviceManager.getCurrentAudioDevice(); device != nullptr && device->getCurrentSampleRate() > 0)
    {
        latency.inputMs = device->getInputLatencyInSamples() * 1000.0 / device->getCurrentSampleRate();
        latency.outputMs = device->getOutputLatencyInSamples() * 1000.0 / device->getCurrentSampleRate();
    }
    return latency;
}
String Session::preferencesProblem() const { return impl->prefsProblem; }
Result Session::setPreferences (const Preferences& next)
{
    if (! Impl::validPreferences (next)) return Result::fail ("Those preferences are out of range.");
    if (next == impl->prefs && impl->prefsFile().existsAsFile()) return Result::ok();
    if (auto r = impl->savePreferences (next); r.failed()) return r;
    impl->changed(); return Result::ok();
}
Result Session::resetPreferences() { return setPreferences ({}); }
Result Session::setComputerKeyboardEnabled (bool on)
{
    if (on && impl->computerKeyboard == nullptr) impl->ensureComputerKeyboard();
    if (on && impl->computerKeyboard == nullptr) return Result::fail ("The computer MIDI keyboard's input could not be made.");
    if (impl->computerKeys == on) return Result::ok();
    if (! on)
        for (int note = 0; note < 128; ++note)
            if (impl->keyboardHeld[(size_t) note]) impl->playComputerKeyboard (juce::MidiMessage::noteOff (1, note));
    impl->keyboardHeld.reset();
    impl->computerKeys = on;
    sendChangeMessage(); return Result::ok();
}
bool Session::isComputerKeyboardEnabled() const { return impl->computerKeys; }
Result Session::computerKeyboardNote (int note, float velocity, bool on)
{
    if (! impl->computerKeys) return Result::fail ("Turn on the computer MIDI keyboard first.");
    if (! juce::isPositiveAndBelow (note, 128) || ! (velocity >= 0 && velocity <= 1)) return Result::fail ("Choose a note from 0 to 127 and a velocity from 0 to 1.");
    if (impl->keyboardHeld[(size_t) note] == on) return Result::ok();
    impl->keyboardHeld[(size_t) note] = on;
    impl->playComputerKeyboard (on ? juce::MidiMessage::noteOn (1, note, velocity) : juce::MidiMessage::noteOff (1, note));
    return Result::ok();
}
Result Session::setMidiInputMpe (const String& inputName, bool on)
{
    for (auto device : impl->engine.getDeviceManager().getMidiInDevices())
        if (! device->isTrackDevice() && device->getName() == inputName)
        {
            if (device->recordToNoteAutomation != on) { device->recordToNoteAutomation = on; device->saveProps(); }
            impl->changed(); return Result::ok();
        }
    return Result::fail ("That MIDI input is not available.");
}
bool Session::isMidiInputMpe (const String& inputName) const
{
    for (auto device : impl->engine.getDeviceManager().getMidiInDevices())
        if (! device->isTrackDevice() && device->getName() == inputName) return device->recordToNoteAutomation;
    return false;
}
Result Session::recoverProject (const File& recovery, File& recoveredFrom)
{
    auto xml = juce::XmlDocument::parse (recovery);
    if (xml == nullptr || ! xml->hasTagName ("HALFTRACK")) return Result::fail ("The recovery copy could not be read.");
    const auto origin = xml->getStringAttribute ("halftrackRecoveryOf");
    if (auto r = loadProject (recovery); r.failed()) return r;
    // The recovered work is unsaved and has no file, so nothing is overwritten without a Save As.
    impl->projectFile = File{};
    impl->explicitChanges = true; impl->pristineNewDocument = false;
    recoveredFrom = origin.isNotEmpty() && File::isAbsolutePath (origin) ? File (origin) : File{};
    sendChangeMessage(); return Result::ok();
}
Result Session::saveProject (const File& destination)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->endAudition(); // The main lane is what is saved.
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
            if (! original.existsAsFile())
            {
                // Nothing to collect: keep the reference to where the file was.
                findStateWithID (state, c->state.getProperty (te::IDs::id)).setProperty (te::IDs::source, original.getRelativePathFrom (file.getParentDirectory()), nullptr);
                continue;
            }
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
    // Take lane audio is collected the same way.
    std::vector<std::pair<juce::ValueTree, File>> laneSources;
    for (auto* t : te::getAudioTracks (*impl->edit))
        for (auto lane : t->state.getChildWithName (takeLanesType))
            for (auto clip : lane)
                if (clip.hasType (te::IDs::AUDIOCLIP))
                {
                    const auto original = te::SourceFileReference::findFileFromString (*impl->edit, clip[te::IDs::source].toString());
                    auto node = findStateWithID (state, clip[te::IDs::id]);
                    if (! original.existsAsFile()) { node.setProperty (te::IDs::source, original.getRelativePathFrom (file.getParentDirectory()), nullptr); continue; }
                    File target;
                    if (original.isAChildOf (assets)) target = original;
                    else if (auto it = copied.find (original.getFullPathName()); it != copied.end()) target = it->second;
                    else
                    {
                        target = assets.getNonexistentChildFile (original.getFileNameWithoutExtension(), original.getFileExtension(), true);
                        if (! original.copyFileTo (target)) return Result::fail ("Could not collect audio: " + original.getFileName());
                        copied[original.getFullPathName()] = target;
                    }
                    node.setProperty (te::IDs::source, target.getRelativePathFrom (file.getParentDirectory()), nullptr);
                    laneSources.emplace_back (clip, target);
                }
    // Sampler and drum pad samples are collected the same way and saved relative to the project.
    struct CollectedSample { NativeEffectPlugin* native; int slot; File target; };
    std::vector<CollectedSample> samples;
    std::vector<NativeEffectPlugin*> samplers;
    for (auto* t : te::getAudioTracks (*impl->edit))
        if (auto* native = Impl::nativeInstrument (*t)) Impl::visitDevices (*native, [&] (NativeEffectPlugin& d) { samplers.push_back (&d); });
    for (auto* native : samplers)
            for (int slot = 0; slot < sampleSlotCount (native->getKind()); ++slot)
        {
            const auto original = native->sampleFile (slot);
            if (original == File()) continue;
            if (! original.existsAsFile())
            {
                findStateWithID (state, native->state.getProperty (te::IDs::id)).setProperty (sampleSlotProperty (native->getKind(), slot), original.getRelativePathFrom (file.getParentDirectory()), nullptr);
                continue;
            }
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
    // Video clips' movies are collected the same way.
    std::vector<std::pair<te::Clip*, File>> movies;
    for (auto* t : te::getAudioTracks (*impl->edit))
        for (auto* c : t->getClips())
        {
            const auto path = c->state[videoProperty].toString();
            if (path.isEmpty()) continue;
            const auto original = impl->edit->filePathResolver (path);
            auto node = findStateWithID (state, c->state.getProperty (te::IDs::id));
            if (! original.existsAsFile()) { node.setProperty (videoProperty, original.getRelativePathFrom (file.getParentDirectory()), nullptr); continue; }
            File target;
            if (original.isAChildOf (assets)) target = original;
            else if (auto it = copied.find (original.getFullPathName()); it != copied.end()) target = it->second;
            else
            {
                target = assets.getNonexistentChildFile (original.getFileNameWithoutExtension(), original.getFileExtension(), true);
                if (! original.copyFileTo (target)) return Result::fail ("Could not collect the movie: " + original.getFileName());
                copied[original.getFullPathName()] = target;
            }
            node.setProperty (videoProperty, target.getRelativePathFrom (file.getParentDirectory()), nullptr);
            movies.emplace_back (c, target);
        }
    juce::XmlElement document ("HALFTRACK");
    document.setAttribute ("version", 1);
    document.addChildElement (state.createXml().release());
    juce::TemporaryFile temp (file);
    if (! document.writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
        return Result::fail ("Could not write the project file.");
    impl->projectFile = file;
    impl->edit->editFileRetriever = [owner = impl.get()] { return owner->editLocation(); };
    for (auto& [clip, source] : sources)
        // Keep running and undo-retained sources independent of the project
        // directory. The written copy above remains relative and portable.
        // Native listeners refresh playback, but relocation adds no undo action.
        clip->getSourceFileReference().source.setValue (source.getFullPathName(), nullptr);
    for (auto& collected : samples) collected.native->relocateSample (collected.slot, collected.target);
    for (auto& [clip, source] : laneSources) clip.setProperty (te::IDs::source, source.getFullPathName(), nullptr);
    for (auto& [clip, movie] : movies) clip->state.setProperty (videoProperty, movie.getFullPathName(), nullptr);
    impl->edit->resetChangedStatus();
    impl->explicitChanges = false;
    sendChangeMessage();
    return Result::ok();
}

juce::Array<File> Session::missingMedia() const
{
    juce::Array<File> missing;
    for (auto* t : te::getAudioTracks (*impl->edit))
    {
        for (auto lane : t->state.getChildWithName (takeLanesType))
            for (auto clip : lane)
                if (clip.hasType (te::IDs::AUDIOCLIP))
                    if (const auto f = te::SourceFileReference::findFileFromString (*impl->edit, clip[te::IDs::source].toString()); f != File() && ! f.existsAsFile())
                        missing.addIfNotAlreadyThere (f);
        for (auto* c : allTrackClips (*t))
            if (c->usesSourceFile())
                if (const auto f = c->getSourceFileReference().getFile(); f != File() && ! f.existsAsFile()) missing.addIfNotAlreadyThere (f);
        if (auto* native = Impl::nativeInstrument (*t))
            Impl::visitDevices (*native, [&] (NativeEffectPlugin& d)
            {
                for (int slot = 0; slot < sampleSlotCount (d.getKind()); ++slot)
                    if (const auto f = d.sampleFile (slot); f != File() && ! f.existsAsFile()) missing.addIfNotAlreadyThere (f);
            });
    }
    return missing;
}
Result Session::locateMissingMedia (const File& folder, int& found)
{
    found = 0;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before locating media.");
    if (! folder.isDirectory()) return Result::fail ("Choose a folder to search.");
    const auto missing = missingMedia();
    if (missing.isEmpty()) return Result::ok();
    std::map<String, File> candidates; // By lower-case name; the first readable match wins.
    std::set<String> wanted;
    for (const auto& f : missing) wanted.insert (f.getFileName().toLowerCase());
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    int visited = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", File::findFiles))
    {
        if (++visited > 200000) break; // A bounded search of very large folders.
        const auto name = entry.getFile().getFileName().toLowerCase();
        if (wanted.count (name) == 0 || candidates.count (name) != 0) continue;
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (entry.getFile())); reader != nullptr && reader->lengthInSamples > 0)
            candidates[name] = entry.getFile();
    }
    std::set<String> relinked;
    const auto match = [&] (const File& f) -> File
    {
        const auto it = candidates.find (f.getFileName().toLowerCase());
        if (it == candidates.end()) return {};
        relinked.insert (f.getFullPathName());
        return it->second;
    };
    for (auto* t : te::getAudioTracks (*impl->edit))
    {
        for (auto lane : t->state.getChildWithName (takeLanesType))
            for (auto clip : lane)
                if (clip.hasType (te::IDs::AUDIOCLIP))
                    if (const auto f = te::SourceFileReference::findFileFromString (*impl->edit, clip[te::IDs::source].toString()); f != File() && ! f.existsAsFile())
                        if (const auto replacement = match (f); replacement != File()) clip.setProperty (te::IDs::source, replacement.getFullPathName(), nullptr);
        for (auto* c : allTrackClips (*t))
            if (c->usesSourceFile())
                if (const auto f = c->getSourceFileReference().getFile(); f != File() && ! f.existsAsFile())
                    if (const auto replacement = match (f); replacement != File())
                        c->getSourceFileReference().source.setValue (replacement.getFullPathName(), nullptr);
        if (auto* native = Impl::nativeInstrument (*t))
            Impl::visitDevices (*native, [&] (NativeEffectPlugin& d)
            {
                for (int slot = 0; slot < sampleSlotCount (d.getKind()); ++slot)
                    if (const auto f = d.sampleFile (slot); f != File() && ! f.existsAsFile())
                        if (const auto replacement = match (f); replacement != File())
                            d.state.setProperty (sampleSlotProperty (d.getKind(), slot), replacement.getFullPathName(), nullptr); // Reloads the sample.
            });
    }
    found = (int) relinked.size();
    if (found > 0) { impl->edit->restartPlayback(); impl->explicitChanges = true; impl->pristineNewDocument = false; }
    sendChangeMessage(); return Result::ok();
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
    // The tempo at the clip's start, then each later tempo point inside the clip, in clip beats;
    // ramps between points are written as steps at each point.
    {
        const auto origin=clip->getStartBeat().inBeats(),span=clip->getLengthInBeats().inBeats();
        add(juce::MidiMessage::tempoMetaEvent((int)std::round(60000000.0/impl->edit->tempoSequence.getBpmAt(clip->getPosition().getStart()))),0);
        for(const auto& point:snapshot().tempoMap)
            if(point.beat>origin+1e-9&&point.beat<origin+span-1e-9) add(juce::MidiMessage::tempoMetaEvent((int)std::round(60000000.0/point.bpm)),point.beat-origin);
    }
    {const auto meter=snapshot().timeSignature;add(juce::MidiMessage::timeSignatureMetaEvent(meter.numerator,meter.denominator),0);}
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
juce::StringArray Session::outputDestinations (const String& trackId) const
{
    juce::StringArray result;
    auto* source = impl->track (trackId);
    if (source == nullptr || isReturn (*source) || source->getParentFolderTrack() != nullptr) return result;
    for (auto* t : te::getAudioTracks (*impl->edit))
        if (t != source && ! isReturn (*t) && ! static_cast<bool> (t->state.getProperty (midiTrackProperty, false)) && ! impl->feeds (*t, *source))
            result.add (t->itemID.toString());
    return result;
}
juce::StringArray Session::inputSources (const String& trackId) const
{
    juce::StringArray result;
    auto* destination = impl->track (trackId);
    if (destination == nullptr || isReturn (*destination) || static_cast<bool> (destination->state.getProperty (midiTrackProperty, false))) return result;
    for (auto* t : te::getAudioTracks (*impl->edit))
        if (t != destination && ! isReturn (*t) && ! impl->feeds (*destination, *t)) result.add (t->itemID.toString());
    return result;
}
Result Session::setTrackInputTrack (const String& trackId, const String& sourceTrackId, InputTap tap)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (trackId); if (t == nullptr) return Result::fail ("Track not found.");
    if (isReturn (*t)) return Result::fail ("Return tracks cannot receive recording inputs.");
    if (static_cast<bool> (t->state.getProperty (midiTrackProperty, false))) return Result::fail ("MIDI tracks take MIDI, not another track's audio.");
    if (impl->anyRecording()) return Result::fail ("Stop recording before changing inputs.");
    if (! inputSources (trackId).contains (sourceTrackId))
        return Result::fail ("That track cannot feed this one: choose a track with audio output that this track does not play into.");
    auto* source = impl->track (sourceTrackId);
    auto& device = source->getWaveInputDevice();
    if (auto* current = impl->input (*t); current != nullptr && &current->getInputDevice() == &device)
    {
        // The same source: only the tap changes.
        auto* destination = te::getDestination (*current, *t, 0);
        if (destination == nullptr) return Result::fail ("Input destination is unavailable.");
        if (static_cast<int> (destination->state.getProperty (inputTapProperty, 2)) == static_cast<int> (tap)) return Result::ok();
        impl->transaction ("Input tap");
        destination->state.setProperty (inputTapProperty, static_cast<int> (tap), &impl->edit->getUndoManager());
        impl->edit->restartPlayback(); impl->changed(); return Result::ok();
    }
    const auto monitored = impl->monitoredMidiTracks();
    const bool playing = impl->edit->getTransport().isPlaying();
    // The track device and its instance exist while some track takes its audio.
    device.setEnabled (true);
    impl->edit->getTransport().ensureContextAllocated();
    [[maybe_unused]] auto deviceState = impl->edit->getEditInputDevices().getInstanceStateForInputDevice (device);
    impl->resumePlayback (playing);
    te::InputDeviceInstance* selected = nullptr;
    for (auto* d : impl->edit->getAllInputDevices()) if (&d->getInputDevice() == &device) selected = d;
    if (selected == nullptr) return Result::fail ("That track's output could not be opened as an input.");
    impl->transaction ("Assign input");
    auto* undo = &impl->edit->getUndoManager();
    t->state.setProperty (monitorProperty, static_cast<int> (MonitorMode::off), undo);
    for (auto* d : impl->edit->getAllInputDevices())
        if (d->getTargets().contains (t->itemID)) { [[maybe_unused]] auto r = d->removeTarget (t->itemID, undo); }
    auto result = selected->setTarget (t->itemID, true, undo, 0);
    if (! result) return Result::fail (result.error());
    (*result)->state.setProperty (inputTapProperty, static_cast<int> (tap), undo);
    selected->setRecordingEnabled (t->itemID, false);
    device.setMonitorMode (te::InputDevice::MonitorMode::off);
    impl->finishInputChange (monitored); impl->changed(); return Result::ok();
}
Result Session::setTrackOutput (const String& trackId, const TrackOutput& output)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->anyRecording()) return Result::fail ("Stop recording before changing a track's output.");
    auto* t = impl->track (trackId); if (t == nullptr) return Result::fail ("Track not found.");
    TrackOutput current;
    if (const int channel = static_cast<int> (t->state.getProperty (midiOutChannelProperty, 0)); channel > 0) current = { OutputKind::midiPort, {}, t->getOutput().getOutputName(), channel };
    else if (auto* destination = t->getOutput().getDestinationTrack()) current = { OutputKind::track, destination->itemID.toString() };
    else if (t->getOutput().outputsToNone()) current = { OutputKind::sendsOnly, {} };
    if (output.kind == OutputKind::master || output.kind == OutputKind::sendsOnly ? output.kind == current.kind : output == current) return Result::ok();
    if (output.kind != OutputKind::master && isReturn (*t)) return Result::fail ("Return tracks play into Master.");
    if (output.kind != OutputKind::master && t->getParentFolderTrack() != nullptr) return Result::fail ("Tracks in a group play into the group.");
    te::AudioTrack* destination = nullptr;
    te::MidiOutputDevice* port = nullptr;
    if (output.kind == OutputKind::track)
    {
        if (! outputDestinations (trackId).contains (output.trackId))
            return Result::fail ("That track cannot take this track's output: choose another audio track that does not play back into this one.");
        destination = impl->track (output.trackId);
    }
    if (output.kind == OutputKind::midiPort)
    {
        if (! static_cast<bool> (t->state.getProperty (midiTrackProperty, false)) || Impl::instrument (*t) != nullptr)
            return Result::fail ("Only a MIDI track without an instrument plays to a MIDI port.");
        if (output.midiChannel < 1 || output.midiChannel > 16) return Result::fail ("MIDI channels are 1 to 16.");
        port = impl->midiOutput (output.port);
        if (port == nullptr || ! impl->midiPortTrack (*port)) return Result::fail ("Choose a MIDI port whose Track switch is on in the MIDI Ports settings.");
    }
    impl->transaction ("Set track output");
    auto* undo = &impl->edit->getUndoManager();
    switch (output.kind)
    {
        case OutputKind::master: t->getOutput().setOutputToDefaultDevice (false); break;
        case OutputKind::sendsOnly: t->getOutput().setOutputToNone(); break;
        case OutputKind::track: t->getOutput().setOutputToTrack (destination); break;
        // By name, which the engine also resolves, so a project names the port on any machine.
        case OutputKind::midiPort: t->getOutput().setOutputToDeviceID (port->getName()); break;
    }
    if (output.kind == OutputKind::midiPort) t->state.setProperty (midiOutChannelProperty, output.midiChannel, undo);
    else t->state.removeProperty (midiOutChannelProperty, undo);
    impl->edit->restartPlayback(); impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::auditionTakeLane (const String& laneId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->anyRecording()) return Result::fail ("Stop recording before auditioning a take.");
    auto [track, lane] = impl->takeLane (laneId);
    if (track == nullptr) return Result::fail ("Take lane not found.");
    if (impl->audition && impl->audition->laneId == laneId) return Result::ok();
    impl->endAudition();
    Impl::Audition audition { track->itemID, laneId, {}, {} };
    for (auto* c : track->getClips()) audition.main.push_back ({ track->state.indexOf (c->state), c->state });
    std::sort (audition.main.begin(), audition.main.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    for (auto it = audition.main.rbegin(); it != audition.main.rend(); ++it) track->state.removeChild (it->second, nullptr);
    for (auto clip : lane) { auto copy = impl->freshClipCopy (clip); track->state.appendChild (copy, nullptr); audition.inserted.push_back (copy); }
    impl->audition = std::move (audition);
    impl->flushStructuralUpdates(); impl->edit->restartPlayback(); impl->edit->dispatchPendingUpdatesSynchronously();
    sendChangeMessage(); return Result::ok();
}
void Session::stopAuditioning() { impl->endAudition(); sendChangeMessage(); }
Result Session::insertTakeLane (const String& trackId, String& laneId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackId);
    if (track == nullptr || isReturn (*track)) return Result::fail ("Choose an audio or MIDI track for the take lane.");
    impl->transaction ("Insert take lane");
    laneId = impl->newTakeLane (*track)[te::IDs::id].toString();
    for (auto* other : impl->linkedTracks (*track)) impl->newTakeLane (*other);
    impl->changed(); return Result::ok();
}
Result Session::removeTakeLane (const String& laneId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto [track, lane] = impl->takeLane (laneId);
    if (track == nullptr) return Result::fail ("Take lane not found.");
    const int position = lane.getParent().indexOf (lane);
    std::vector<juce::ValueTree> lanes { lane };
    for (auto* other : impl->linkedTracks (*track)) if (auto linked = impl->laneAt (*other, position); linked.isValid()) lanes.push_back (linked);
    impl->transaction ("Delete take lane");
    for (auto& l : lanes) l.getParent().removeChild (l, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::renameTakeLane (const String& laneId, const String& name)
{
    auto [track, lane] = impl->takeLane (laneId);
    if (track == nullptr) return Result::fail ("Take lane not found.");
    if (name.trim().isEmpty()) return Result::fail ("Enter a name for the take lane.");
    std::vector<juce::ValueTree> lanes;
    if (lane[te::IDs::name].toString() != name.trim()) lanes.push_back (lane);
    const int position = lane.getParent().indexOf (lane);
    for (auto* other : impl->linkedTracks (*track))
        if (auto linked = impl->laneAt (*other, position); linked.isValid() && linked[te::IDs::name].toString() != name.trim()) lanes.push_back (linked);
    if (lanes.empty()) return Result::ok();
    impl->transaction ("Rename take lane");
    for (auto& l : lanes) l.setProperty (te::IDs::name, name.trim(), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::compTake (const String& laneId, double startSeconds, double endSeconds)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->anyRecording()) return Result::fail ("Stop recording before comping.");
    auto [track, lane] = impl->takeLane (laneId);
    if (track == nullptr) return Result::fail ("Take lane not found.");
    if (auto frozen = impl->unfrozen (track); frozen.failed()) return frozen;
    if (! std::isfinite (startSeconds) || ! std::isfinite (endSeconds) || endSeconds <= startSeconds || startSeconds < 0)
        return Result::fail ("Select a time range in the take lane.");
    // Linked tracks comp the lane at the same position, where it holds material in the range.
    std::vector<std::pair<te::AudioTrack*, juce::ValueTree>> comps { { track, lane } };
    const int position = lane.getParent().indexOf (lane);
    for (auto* other : impl->linkedTracks (*track))
        if (auto linked = impl->laneAt (*other, position); linked.isValid() && Impl::laneHasMaterial (linked, startSeconds, endSeconds))
        {
            if (auto frozen = impl->unfrozen (other); frozen.failed()) return frozen;
            comps.push_back ({ other, linked });
        }
    for (auto& [t, l] : comps)
        for (const auto seconds : { startSeconds, endSeconds }) if (auto r = splittableAt (*t, te::TimePosition::fromSeconds (seconds)); r.failed()) return r;
    impl->transaction ("Comp take");
    for (auto& [t, l] : comps) impl->compLane (*t, l, startSeconds, endSeconds);
    impl->changed(); return Result::ok();
}
Result Session::cycleTake (const String& trackId, double startSeconds, double endSeconds, bool next)
{
    auto* track = impl->track (trackId);
    if (track == nullptr) return Result::fail ("Track not found.");
    std::vector<String> candidates;
    for (auto lane : impl->takeLanes (*track, false))
    {
        bool has = false;
        for (auto clip : lane)
            has = has || (static_cast<double> (clip[te::IDs::start]) < endSeconds && static_cast<double> (clip[te::IDs::start]) + static_cast<double> (clip[te::IDs::length]) > startSeconds);
        if (has) candidates.push_back (lane[te::IDs::id].toString());
    }
    if (candidates.empty()) return Result::fail ("No take lane holds material in this range.");
    // The lane the main lane's material in the range came from, if any.
    String current;
    for (auto* c : track->getClips())
        if (c->getPosition().getStart().inSeconds() < endSeconds && c->getPosition().getEnd().inSeconds() > startSeconds)
            current = c->state[compSourceProperty].toString();
    auto at = std::find (candidates.begin(), candidates.end(), current);
    size_t index = at == candidates.end() ? (next ? 0 : candidates.size() - 1)
                 : next ? ((size_t) std::distance (candidates.begin(), at) + 1) % candidates.size()
                        : ((size_t) std::distance (candidates.begin(), at) + candidates.size() - 1) % candidates.size();
    return compTake (candidates[index], startSeconds, endSeconds);
}
bool Session::hasCapturedMidi() const
{ return std::any_of (impl->captured.begin(), impl->captured.end(), [] (const auto& entry) { return ! entry.second.empty(); }); }
Result Session::captureMidi (CaptureDestination destination, juce::StringArray& createdClipIds)
{
    createdClipIds.clear();
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->anyRecording()) return Result::fail ("Stop recording before capturing MIDI.");
    for (auto& tap : impl->captureTaps) tap->flush(); // Notes played just before are included.
    if (! hasCapturedMidi()) return Result::fail ("Play some notes into an armed or In-monitored MIDI track first.");
    const auto now = impl->engine.getDeviceManager().getCurrentStreamTime();
    const auto nowBeat = impl->arrivalBeat (now);
    const auto meter = snapshot().timeSignature;
    const double bar = std::max (1.0, meter.beatsPerBar());
    const double bpm = std::clamp (snapshot().tempo, 20.0, 999.0);
    auto captured = std::move (impl->captured); impl->captured.clear();
    beginEditGesture ("Capture MIDI");
    Result result = Result::ok();
    for (auto& [trackId, notes] : captured)
    {
        auto* track = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*impl->edit, trackId));
        if (track == nullptr || notes.empty()) continue;
        // The material since the last pause of more than ten seconds.
        size_t first = 0;
        double latestEnd = notes[0].offTime < 0 ? now : notes[0].offTime;
        for (size_t i = 1; i < notes.size(); ++i)
        {
            if (notes[i].onTime - latestEnd > 10) first = i;
            latestEnd = std::max (latestEnd, notes[i].offTime < 0 ? now : notes[i].offTime);
        }
        std::vector<Impl::CapturedNote> phrase (notes.begin() + (std::ptrdiff_t) first, notes.end());
        for (auto& n : phrase) if (n.offTime < 0) { n.offTime = now; n.offBeat = nowBeat.value_or (n.onBeat); }
        // Notes played with the transport running keep their beats; otherwise they are timed at the tempo.
        const bool timed = std::all_of (phrase.begin(), phrase.end(), [] (const auto& n) { return n.timed; });
        std::vector<MidiNoteInfo> written;
        double startBeat = 0, length = bar;
        if (timed)
        {
            double low = phrase[0].onBeat, high = phrase[0].offBeat;
            for (const auto& n : phrase) { low = std::min (low, n.onBeat); high = std::max (high, n.offBeat); }
            startBeat = std::floor (std::max (0.0, low) / bar) * bar;
            length = std::max (bar, std::ceil ((high - startBeat) / bar - 1e-9) * bar);
            for (const auto& n : phrase)
                written.push_back ({ {}, n.pitch, std::max (0.0, n.onBeat - startBeat), std::max (1.0 / 960, n.offBeat - n.onBeat), n.velocity });
        }
        else
        {
            const auto origin = phrase[0].onTime;
            double high = 0;
            for (const auto& n : phrase)
            {
                const auto on = (n.onTime - origin) / 60.0 * bpm, off = (n.offTime - origin) / 60.0 * bpm;
                written.push_back ({ {}, n.pitch, on, std::max (1.0 / 960, off - on), n.velocity });
                high = std::max (high, off);
            }
            length = std::max (bar, std::ceil (high / bar - 1e-9) * bar);
            startBeat = beatsAt (snapshot().insertMarker);
        }
        const auto id = track->itemID.toString();
        String clipId;
        if (destination == CaptureDestination::session)
        {
            int scene = -1;
            const auto slots = track->getClipSlotList().getClipSlots();
            for (int i = 0; i < slots.size() && scene < 0; ++i) if (slots[i]->getClip() == nullptr) scene = i;
            if (scene < 0) { result = Result::fail ("\"" + track->getName() + "\" has no empty slot to capture into."); continue; }
            if (result = addMidiClipToSlot (id, scene, length); result.failed()) continue;
            clipId = track->getClipSlotList().getClipSlots()[scene]->getClip()->itemID.toString();
        }
        else
        {
            const auto before = snapshot();
            if (result = addMidiClip (id, startBeat, length); result.failed()) continue;
            for (const auto& t : snapshot().tracks)
                if (t.id == id) for (const auto& c : t.clips)
                    if (std::none_of (before.tracks.begin(), before.tracks.end(), [&] (const auto& b) { return std::any_of (b.clips.begin(), b.clips.end(), [&] (const auto& old) { return old.id == c.id; }); }))
                        clipId = c.id;
        }
        juce::StringArray inserted;
        if (clipId.isNotEmpty()) { if (result = addMidiNotes (clipId, written, inserted); result.wasOk()) createdClipIds.add (clipId); }
    }
    endEditGesture();
    if (createdClipIds.isEmpty() && result.wasOk()) return Result::fail ("No MIDI track could take the captured notes.");
    return result;
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
    // Deleting the row would delete a recording slot and its take.
    for (auto* track : te::getAudioTracks (*impl->edit))
        if (auto* slot = track->getClipSlotList().getClipSlots()[index]; slot && impl->slotRecording (slot->itemID))
            return Result::fail ("End this scene's Session recording before deleting it.");
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
Result Session::captureAndInsertScene (int after, String& capturedId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing scenes.");
    auto& list = impl->edit->getSceneList();
    if (list.getNumScenes() >= 256) return Result::fail ("This project has reached the scene limit.");
    const int index = juce::isPositiveAndBelow (after, list.getNumScenes()) ? after + 1 : list.getNumScenes();
    // What plays now: each ordinary track's playing slot clip.
    struct Copy { te::AudioTrack* track; te::ClipSlot* playing; te::ClipCopy clip; };
    std::vector<Copy> clips;
    if (impl->edit->getTransport().isPlaying())
        for (auto* track : te::getAudioTracks (*impl->edit))
        {
            if (isReturn (*track) || track->isFrozen (te::Track::anyFreeze)) continue;
            for (auto* slot : track->getClipSlotList().getClipSlots())
                if (auto* clip = slot->getClip(); clip != nullptr && (dynamic_cast<te::WaveAudioClip*> (clip) || dynamic_cast<te::MidiClip*> (clip)))
                    if (auto handle = clip->getLaunchHandle(); handle && handle->getPlayingStatus() == te::LaunchHandle::PlayState::playing)
                    {
                        auto copy = te::ClipCopy::fromClip (*clip).withNewItemID (*impl->edit);
                        renewMidiNoteIDs (copy.getState());
                        clips.push_back ({ track, slot, std::move (copy) });
                        break;
                    }
        }
    if (clips.empty()) return Result::fail ("No Session clips are playing to capture.");
    impl->transaction ("Capture and insert scene");
    auto* scene = list.insertScene (index);
    if (scene == nullptr) return Result::fail ("The scene could not be inserted.");
    capturedId = juce::Uuid().toString();
    scene->state.setProperty ("halftrackSceneID", capturedId, &impl->edit->getUndoManager());
    for (auto* track : te::getAudioTracks (*impl->edit)) track->getClipSlotList().ensureNumberOfSlots (list.getNumScenes());
    for (auto& copy : clips)
        if (auto* slot = copy.track->getClipSlotList().getClipSlots()[index]; slot == nullptr || ! te::insertClipCopy (*slot, copy.clip))
        {
            list.deleteScene (*scene);
            impl->changed();
            return Result::fail ("A playing clip could not be captured.");
        }
    // The new scene plays at once with no interruption: each playing clip's slot changes places
    // with its copy's, so the clip carries on in the new scene and the copy stays where it was.
    // The engine keeps a slot's playback across graph rebuilds, and its held notes sound on.
    for (auto& copy : clips)
    {
        auto& slots = copy.track->getClipSlotList().state;
        auto* captured = copy.track->getClipSlotList().getClipSlots()[index];
        const int from = slots.indexOf (copy.playing->state), to = slots.indexOf (captured->state);
        slots.moveChild (from, to, &impl->edit->getUndoManager());
        slots.moveChild (from < to ? to - 1 : to + 1, from, &impl->edit->getUndoManager());
    }
    impl->changed(); return Result::ok();
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
Result Session::setSceneTempo (const String& sceneId, std::optional<double> bpm)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* scene = impl->scene (sceneId); if (scene == nullptr) return Result::fail ("Scene not found.");
    if (bpm && (! std::isfinite (*bpm) || *bpm < te::TempoSetting::minBPM || *bpm > te::TempoSetting::maxBPM))
        return Result::fail ("Choose a scene tempo from 20 to 999 BPM.");
    if (scene->state.hasProperty (sceneTempoProperty) ? bpm == (double) scene->state[sceneTempoProperty] : ! bpm) return Result::ok();
    impl->transaction ("Scene tempo");
    auto* undo = &impl->edit->getUndoManager();
    if (bpm) scene->state.setProperty (sceneTempoProperty, *bpm, undo); else scene->state.removeProperty (sceneTempoProperty, undo);
    impl->changed(); return Result::ok();
}
Result Session::setSceneTimeSignature (const String& sceneId, std::optional<TimeSignature> meter)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* scene = impl->scene (sceneId); if (scene == nullptr) return Result::fail ("Scene not found.");
    if (meter && ! validSceneMeter (meter->numerator, meter->denominator))
        return Result::fail ("Choose a scene time signature of 1 to 99 over 1, 2, 4, 8 or 16.");
    const auto current = scene->state.hasProperty (sceneNumeratorProperty)
        ? std::optional<TimeSignature> ({ (int) scene->state[sceneNumeratorProperty], (int) scene->state[sceneDenominatorProperty] }) : std::nullopt;
    if (current == meter) return Result::ok();
    impl->transaction ("Scene time signature");
    auto* undo = &impl->edit->getUndoManager();
    if (meter)
    {
        scene->state.setProperty (sceneNumeratorProperty, meter->numerator, undo);
        scene->state.setProperty (sceneDenominatorProperty, meter->denominator, undo);
    }
    else { scene->state.removeProperty (sceneNumeratorProperty, undo); scene->state.removeProperty (sceneDenominatorProperty, undo); }
    impl->changed(); return Result::ok();
}
// One command's edits as one undo step, unless an edit gesture already groups them.
struct OneUndoStep
{
    OneUndoStep (Session& s, bool needed, bool inGesture, const String& name) : session (s), own (needed && ! inGesture) { if (own) session.beginEditGesture (name); }
    ~OneUndoStep() { if (own) session.endEditGesture(); }
    Session& session;
    const bool own;
};
Result Session::importAudioToSlot (const File& source, int trackIndex, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing slots.");
    auto* slot = impl->slot (trackIndex, sceneIndex);
    if (! slot) return Result::fail ("Slot not found.");
    if (slot->getClip()) return Result::fail ("Clear this slot before importing another clip.");
    // A finished take replaces whatever clip its slot holds.
    if (impl->slotRecording (slot->itemID)) return Result::fail ("End this slot's recording before adding a clip.");
    if (static_cast<bool> (slot->track.state.getProperty (midiTrackProperty))) return Result::fail ("Import audio onto an audio track.");
    te::AudioFile audio (impl->engine, source);
    if (! audio.isValid() || audio.getLength() <= 0) return Result::fail ("This audio file could not be read.");
    auto assets = impl->assets(); auto result = assets.createDirectory(); if (result.failed()) return result;
    auto file = source.isAChildOf (assets) ? source : assets.getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), true);
    if (file != source && ! source.copyFileTo (file)) return Result::fail ("Could not copy the imported audio.");
    const OneUndoStep step (*this, impl->prefs.defaultWarpMode != WarpMode::stretch, impl->gesture != nullptr, "Import session clip");
    impl->transaction ("Import session clip");
    const te::TimeRange range { te::TimePosition::fromSeconds (0), te::TimePosition::fromSeconds (audio.getLength()) };
    auto clip = te::insertWaveClip (*slot, source.getFileNameWithoutExtension(), file, { range, {} }, te::DeleteExistingClips::no);
    if (! clip) return Result::fail ("Could not create the audio clip.");
    clip->getSourceFileReference().setToFile (file, te::SourceFileReference::PathStyle::alwaysAbsolute, false);
    clip->setAutoTempo (true); clip->setAutoPitch (false); clip->setUsesProxy (false);
    clip->setLoopRange (range);
    clip->setFadeIn (te::TimeDuration::fromSeconds (0)); clip->setFadeOut (te::TimeDuration::fromSeconds (0));
    impl->applyLaunchDefaults (*clip);
    // Session clips always follow the tempo, in the preferred warp mode.
    if (impl->prefs.defaultWarpMode != WarpMode::stretch)
    {
        const auto id = clip->itemID.toString();
        if (auto warped = setAudioClipWarp (id, { impl->prefs.defaultWarpMode, warpSettings (*clip).bpm }); warped.failed()) return warped;
    }
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
    // A finished take replaces whatever clip its slot holds.
    if (impl->slotRecording (slot->itemID)) return Result::fail ("End this slot's recording before adding a clip.");
    if (! std::isfinite (length) || length <= 0 || length > 1000000) return Result::fail ("Enter a positive clip length in beats.");
    impl->transaction ("Add session MIDI clip");
    const te::BeatRange beats { te::BeatPosition::fromBeats (0), te::BeatPosition::fromBeats (length) };
    auto clip = te::insertMIDIClip (*slot, "MIDI clip", impl->edit->tempoSequence.toTime (beats));
    if (! clip) return Result::fail ("Could not create the MIDI clip.");
    clip->setSyncType (te::Clip::syncBarsBeats); clip->setLoopRangeBeats (beats);
    impl->applyLaunchDefaults (*clip);
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
static bool slotTakes (const te::ClipSlot& slot, const te::Clip& clip)
{
    return (dynamic_cast<const te::MidiClip*> (&clip) != nullptr) == static_cast<bool> (slot.track.state.getProperty (midiTrackProperty));
}
static Result wrongTrack (const te::Clip& clip)
{
    return Result::fail (dynamic_cast<const te::MidiClip*> (&clip) != nullptr ? "MIDI clips go on MIDI tracks." : "Audio clips go on audio tracks.");
}
Result Session::moveSlotClip (int fromTrack, int fromScene, int toTrack, int toScene, bool copy)
{
    te::ClipSlot* from = nullptr; te::ClipSlot* to = nullptr;
    if (auto r = impl->editableSlot (toTrack, toScene, to); r.failed()) return r;
    if (auto r = copy ? Result::ok() : impl->editableSlot (fromTrack, fromScene, from); r.failed()) return r;
    if (copy && (from = impl->slot (fromTrack, fromScene)) == nullptr) return Result::fail ("Slot not found.");
    auto* clip = from->getClip();
    if (clip == nullptr) return Result::fail ("There is no clip in this slot.");
    if (! slotTakes (*to, *clip)) return wrongTrack (*clip);
    if (from == to) return Result::ok();
    auto* undo = &impl->edit->getUndoManager();
    if (! copy && &from->track == &to->track)
    {
        // Within its track the two slots change places, so a playing clip plays on.
        impl->transaction ("Move session clip");
        if (auto* replaced = to->getClip()) replaced->removeFromParent();
        auto& slots = impl->track (fromTrack)->getClipSlotList().state;
        const int a = slots.indexOf (from->state), b = slots.indexOf (to->state);
        slots.moveChild (a, b, undo); slots.moveChild (a < b ? b - 1 : b + 1, a, undo);
        impl->changed(); return Result::ok();
    }
    auto moved = te::ClipCopy::fromClip (*clip).withNewItemID (*impl->edit);
    renewMidiNoteIDs (moved.getState());
    impl->transaction (copy ? "Copy session clip" : "Move session clip");
    if (auto* replaced = to->getClip()) replaced->removeFromParent();
    if (! te::insertClipCopy (*to, moved)) { undo->undoCurrentTransactionOnly(); impl->changed(); return Result::fail ("The clip could not be placed there."); }
    if (! copy) clip->removeFromParent();
    impl->changed(); return Result::ok();
}
Result Session::duplicateSlotClip (int trackIndex, int sceneIndex, int& duplicateScene)
{
    te::ClipSlot* from = nullptr;
    if (auto r = impl->editableSlot (trackIndex, sceneIndex, from); r.failed()) return r;
    auto* clip = from->getClip();
    if (clip == nullptr) return Result::fail ("There is no clip in this slot.");
    auto& list = impl->edit->getSceneList();
    const int target = sceneIndex + 1;
    auto* below = impl->slot (trackIndex, target);
    const bool insert = below == nullptr || below->getClip() != nullptr || impl->slotRecording (below->itemID);
    if (insert && list.getNumScenes() >= 256) return Result::fail ("This project has reached the scene limit.");
    auto copy = te::ClipCopy::fromClip (*clip).withNewItemID (*impl->edit);
    renewMidiNoteIDs (copy.getState());
    impl->transaction ("Duplicate session clip");
    if (insert)
    {
        auto* scene = list.insertScene (target);
        if (scene == nullptr) { impl->changed(); return Result::fail ("The scene could not be inserted."); }
        scene->state.setProperty ("halftrackSceneID", juce::Uuid().toString(), &impl->edit->getUndoManager());
        for (auto* track : te::getAudioTracks (*impl->edit)) track->getClipSlotList().ensureNumberOfSlots (list.getNumScenes());
        below = impl->slot (trackIndex, target);
    }
    if (below == nullptr || ! te::insertClipCopy (*below, copy))
    {
        impl->edit->getUndoManager().undoCurrentTransactionOnly(); impl->changed();
        return Result::fail ("The clip could not be duplicated.");
    }
    duplicateScene = target;
    impl->changed(); return Result::ok();
}
Result Session::copySlotClip (int trackIndex, int sceneIndex, bool cut)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    te::ClipSlot* slot = nullptr;
    if (cut) { if (auto r = impl->editableSlot (trackIndex, sceneIndex, slot); r.failed()) return r; }
    else slot = impl->slot (trackIndex, sceneIndex);
    auto* clip = slot != nullptr ? slot->getClip() : nullptr;
    if (clip == nullptr) return Result::fail ("There is no clip in this slot.");
    auto copied = clip->state.createCopy();
    std::unique_ptr<juce::TemporaryFile> media;
    if (auto* audio = dynamic_cast<te::WaveAudioClip*> (clip))
    {
        if (audio->getNumTakes (true) > 1 || audio->isCurrentTakeComp()) return Result::fail ("Copy a Session clip with a single take.");
        // A clipboard copy that pastes elsewhere keeps its own audio.
        const auto source = audio->getSourceFileReference().getFile();
        if (! source.existsAsFile()) return Result::fail ("The audio clip source is missing.");
        media = std::make_unique<juce::TemporaryFile> (source.getFileExtension());
        if (! source.copyFileTo (media->getFile())) return Result::fail ("Could not retain audio for the clipboard.");
        copied.removeChild (copied.getChildWithName (te::IDs::TAKES), nullptr);
        copied.setProperty (te::IDs::source, media->getFile().getFullPathName(), nullptr);
    }
    else if (dynamic_cast<te::MidiClip*> (clip) == nullptr) return Result::fail ("Copy a MIDI or audio clip.");
    if (cut) { impl->transaction ("Cut session clip"); clip->removeFromParent(); impl->changed(); }
    impl->midiClipboard = {}; impl->midiClipboardBeats = 0; impl->audioClipboard = {}; impl->audioClipboardMedia.reset(); impl->rangeClipboard.reset();
    impl->slotClipboard = std::move (copied); impl->slotClipboardMedia = std::move (media);
    return Result::ok();
}
bool Session::hasSlotClipboard() const
{
    return impl->slotClipboard.isValid() && (impl->slotClipboardMedia == nullptr || impl->slotClipboardMedia->getFile().existsAsFile());
}
Result Session::pasteSlotClip (int trackIndex, int sceneIndex)
{
    te::ClipSlot* to = nullptr;
    if (auto r = impl->editableSlot (trackIndex, sceneIndex, to); r.failed()) return r;
    if (! hasSlotClipboard()) return Result::fail ("Copy a Session clip before pasting into a slot.");
    const bool midi = impl->slotClipboard.hasType (te::IDs::MIDICLIP);
    if (midi != static_cast<bool> (to->track.state.getProperty (midiTrackProperty)))
        return Result::fail (midi ? "MIDI clips go on MIDI tracks." : "Audio clips go on audio tracks.");
    auto copy = te::ClipCopy::fromClipboardState (impl->slotClipboard, false).withNewItemID (*impl->edit);
    renewMidiNoteIDs (copy.getState());
    File collected;
    if (! midi)
    {
        // The audio is collected into this project, as an Arrangement paste's is.
        const auto assets = impl->assets();
        if (auto r = assets.createDirectory(); r.failed()) return r;
        const auto source = impl->slotClipboardMedia->getFile();
        const auto name = File::createLegalFileName (impl->slotClipboard[te::IDs::name].toString());
        collected = assets.getNonexistentChildFile (name.isEmpty() ? "Pasted audio" : name, source.getFileExtension(), true);
        if (! source.copyFileTo (collected)) return Result::fail ("Could not collect clipboard audio into this project.");
        auto state = copy.getState(); state.setProperty (te::IDs::source, collected.getFullPathName(), nullptr);
    }
    impl->transaction ("Paste session clip");
    if (auto* replaced = to->getClip()) replaced->removeFromParent();
    if (! te::insertClipCopy (*to, copy))
    {
        impl->edit->getUndoManager().undoCurrentTransactionOnly(); impl->changed(); collected.deleteFile();
        return Result::fail ("The clip could not be pasted.");
    }
    impl->changed(); return Result::ok();
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
    impl->applyDriverCompensation();
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
    if (auto allowed = impl->hardwareInputAllowed (*input); allowed.failed()) return allowed;
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail (impl->noDeviceMessage());
    if (auto r = input->getInputDevice().isMidi() ? impl->assets().createDirectory() : impl->takeFolderReady(); r.failed()) return r;
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
Result Session::sessionRecord (int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop Arrangement recording before recording into Session slots.");
    if (! juce::isPositiveAndBelow (sceneIndex, impl->edit->getSceneList().getNumScenes())) return Result::fail ("Scene not found.");
    // Pressed again: new takes end on the launch grid and loop, and overdubbing stops.
    if (! impl->slotRecordings.empty() || ! impl->overdubs.empty())
    {
        std::vector<te::EditItemID> recording;
        for (const auto& r : impl->slotRecordings) if (! r.stopping) recording.push_back (r.slotId);
        for (auto slotId : recording)
            for (auto& r : impl->slotRecordings) if (r.slotId == slotId) { impl->finishSlotRecording (r, true, false); break; }
        for (auto& tap : impl->captureTaps) tap->flush(); // A note released just before is kept.
        impl->overdubs.clear();
        sendChangeMessage(); return Result::ok();
    }
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail (impl->noDeviceMessage());
    auto& transport = impl->edit->getTransport();
    const bool wasPlaying = transport.isPlaying();
    std::vector<std::pair<te::AudioTrack*, te::MidiClip*>> overdubbed;
    bool recorded = false;
    // Every armed input must be usable before any slot starts recording.
    for (auto* t : te::getAudioTracks (*impl->edit))
        if (auto* input = impl->input (*t); input != nullptr && ! isReturn (*t) && input->isRecordingEnabled (t->itemID))
            if (auto allowed = impl->hardwareInputAllowed (*input); allowed.failed()) return allowed;
    for (int i = 0; i < (int) te::getAudioTracks (*impl->edit).size(); ++i)
    {
        auto* t = impl->track (i);
        if (t == nullptr || isReturn (*t)) continue;
        auto* input = impl->input (*t);
        if (input == nullptr || ! input->isRecordingEnabled (t->itemID)) continue;
        auto* slot = impl->slot (i, sceneIndex);
        if (slot == nullptr) continue;
        if (slot->getClip() == nullptr)
        {
            if (auto r = recordSlot (i, sceneIndex); r.failed()) return r;
            recorded = true;
        }
        else if (auto* midi = dynamic_cast<te::MidiClip*> (slot->getClip()); midi != nullptr && static_cast<bool> (t->state.getProperty (midiTrackProperty, false)))
            overdubbed.push_back ({ t, midi });
    }
    if (! recorded && overdubbed.empty()) return Result::fail ("Arm a track to record into this scene.");
    for (auto [t, midi] : overdubbed)
    {
        impl->overdubs.push_back ({ t->itemID, midi->itemID, {} });
        // A clip already playing keeps playing; otherwise it launches with the new takes.
        auto handle = midi->getLaunchHandle();
        if (handle == nullptr || handle->getPlayingStatus() != te::LaunchHandle::PlayState::playing)
            impl->queueTrack (*t, midi, wasPlaying ? impl->clipBoundary (midi) : std::nullopt);
    }
    if (! transport.isPlaying()) { transport.ensureContextAllocated(); transport.play (false); }
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
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail (impl->noDeviceMessage());
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
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail (impl->noDeviceMessage());
    impl->edit->dispatchPendingUpdatesSynchronously();
    impl->edit->getTransport().ensureContextAllocated();
    const auto boundary = impl->launchBoundary();
    for (auto* track : te::getAudioTracks (*impl->edit))
        if (! isReturn (*track)) impl->queueTrack (*track, track->getClipSlotList().getClipSlots()[sceneIndex]->getClip(), boundary);
    impl->scheduleSceneChange (*impl->edit->getSceneList().getScenes()[sceneIndex], boundary);
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
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail (impl->noDeviceMessage());
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
        if (value.chance < 1) note->state.setProperty (noteChanceProperty, juce::jlimit (0.0f, 1.0f, value.chance), undo);
        if (value.velocityRange != 0) note->state.setProperty (noteVelocityRangeProperty, juce::jlimit (-127, 127, value.velocityRange), undo);
        for (const auto kind : { NoteExpression::pitchBend, NoteExpression::slide, NoteExpression::pressure })
            if (const auto& points = kind == NoteExpression::pitchBend ? value.pitchBend : kind == NoteExpression::slide ? value.slide : value.pressure;
                ! points.empty() && validExpression (points, kind, value.lengthBeats))
                writeNoteExpression (note->state, kind, points, undo);
    }
    updateMpeMode (*clip);
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
Result Session::setMidiNotesChance (const String& clipId, const juce::StringArray& ids, float chance)
{
    if (! std::isfinite (chance) || chance < 0 || chance > 1) return Result::fail ("Choose a chance from 0% to 100%.");
    return impl->setNotesProperty (clipId, ids, noteChanceProperty, static_cast<double> (chance), 1.0, "Note chance");
}
Result Session::setMidiNoteExpression (const String& clipId, const String& noteId, NoteExpression kind, const std::vector<ExpressionPoint>& points)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    te::MidiNote* note = nullptr;
    for (auto* n : clip->getSequence().getNotes()) if (n->state[midiNoteIDProperty].toString() == noteId) note = n;
    if (note == nullptr) return Result::fail ("MIDI note not found.");
    if (! validExpression (points, kind, note->getLengthBeats().inBeats()))
        return Result::fail (kind == NoteExpression::pitchBend ? "Pitch bend needs ordered points inside the note, from -48 to 48 semitones."
                                                               : "Slide and pressure need ordered points inside the note, from 0 to 1.");
    if (noteExpression (note->state, kind) == points) return Result::ok();
    impl->transaction (kind == NoteExpression::pitchBend ? "Edit note pitch bend" : kind == NoteExpression::slide ? "Edit note slide" : "Edit note pressure");
    writeNoteExpression (note->state, kind, points, &impl->edit->getUndoManager());
    updateMpeMode (*clip);
    impl->changed(); return Result::ok();
}
Result Session::setMidiNotesVelocityRange (const String& clipId, const juce::StringArray& ids, int range)
{
    if (range < -127 || range > 127) return Result::fail ("Choose a velocity range from -127 to 127.");
    return impl->setNotesProperty (clipId, ids, noteVelocityRangeProperty, range, 0, "Velocity range");
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
Result Session::applyNoteTool (const String& clipId, const juce::StringArray& ids, const NoteToolSettings& settings, juce::StringArray& resultIds)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing notes.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    if (! notetools::valid (settings)) return Result::fail ("The note tool's settings are outside their ranges.");
    std::set<String> requested;
    for (const auto& id : ids) if (id.isEmpty() || ! requested.insert (id).second) return Result::fail ("Each chosen note needs a unique identifier.");
    std::vector<MidiNoteInfo> chosen;
    juce::Array<te::MidiNote*> originals;
    for (auto* note : clip->getSequence().getNotes())
        if (const auto id = note->state.getProperty (midiNoteIDProperty).toString(); requested.contains (id))
        {
            chosen.push_back ({ id, note->getNoteNumber(), note->getStartBeat().inBeats(), note->getLengthBeats().inBeats(), note->getVelocity() / 127.0f, ! note->isMute() });
            originals.add (note);
        }
    if (chosen.size() != requested.size()) return Result::fail ("A chosen note is missing or ambiguous.");
    if (chosen.empty() && ! notetools::generates (settings.tool)) return Result::fail ("Choose the notes to transform.");
    const auto length = clip->getLengthInBeats().inBeats();
    const auto result = notetools::run (settings, chosen, length);
    if (result.size() > 16384 || clip->getSequence().getNumNotes() - (int) chosen.size() + (int) result.size() > 100000)
        return Result::fail ("The result has too many notes.");
    for (const auto& note : result)
        if (auto valid = impl->validateNote (*clip, note.pitch, note.startBeat, note.lengthBeats, note.velocity); valid.failed()) return valid;
    impl->transaction (String (notetools::name (settings.tool)));
    auto* undo = &impl->edit->getUndoManager();
    for (auto* note : originals) clip->getSequence().removeNote (*note, undo);
    resultIds.clear();
    for (const auto& value : result)
    {
        auto* note = clip->getSequence().addNote (value.pitch, te::BeatPosition::fromBeats (value.startBeat), te::BeatDuration::fromBeats (value.lengthBeats),
                                                 juce::jlimit (1, 127, juce::roundToInt (value.velocity * 127)), 0, undo);
        const auto id = juce::Uuid().toString();
        note->state.setProperty (midiNoteIDProperty, id, undo);
        resultIds.add (id);
    }
    impl->changed(); return Result::ok();
}
const char* Session::scaleModeName (int mode) noexcept
{
    constexpr const char* names[] { "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian", "Harmonic Minor", "Melodic Minor",
        "Major Pentatonic", "Minor Pentatonic", "Minor Blues", "Whole Tone", "Half-whole Diminished", "Whole-half Diminished", "Phrygian Dominant" };
    return mode >= 0 && mode < scaleModeCount ? names[mode] : "";
}
uint16_t Session::scaleIntervals (int mode) noexcept
{
    // Bit n is set when the scale holds n semitones above its root, in scaleModeName order.
    constexpr std::array<uint16_t, scaleModeCount> masks { 0xab5, 0x5ad, 0x6ad, 0x5ab, 0xad5, 0x6b5, 0x56b, 0x9ad, 0xaad, 0x295, 0x4a9, 0x4e9, 0x555, 0x6db, 0xb6d, 0x5b3 };
    return mode >= 0 && mode < scaleModeCount ? masks[(size_t) mode] : 0;
}
bool Session::inScale (const ScaleSettings& scale, int pitch) noexcept
{
    if (! scale.enabled) return true;
    return (scaleIntervals (scale.mode) >> (((pitch - scale.root) % 12 + 12) % 12)) & 1u;
}
int Session::fitToScale (const ScaleSettings& scale, int pitch) noexcept
{
    for (int distance = 0; distance < 12; ++distance)
    {
        if (pitch - distance >= 0 && inScale (scale, pitch - distance)) return pitch - distance;
        if (pitch + distance <= 127 && inScale (scale, pitch + distance)) return pitch + distance;
    }
    return pitch;
}
std::optional<Session::TunerReading> Session::tunerReading (const String& pluginId) const
{
    auto* device = impl->nativeDevice (pluginId);
    if (device == nullptr || device->getKind() != EffectKind::tuner) return std::nullopt;
    std::vector<float> frames; double rate = 0;
    if (! device->latestInput (frames, tuner::analysisFrames, rate)) return std::nullopt;
    return tuner::detect (frames.data(), (int) frames.size(), rate, device->readSettings().values[0]);
}
std::optional<Session::SpectrumReading> Session::spectrum (const String& pluginId) const
{
    auto* device = impl->nativeDevice (pluginId);
    if (device == nullptr || device->getKind() != EffectKind::spectrum) return std::nullopt;
    const auto settings = device->readSettings();
    const int block = spectrum::blockSizes[(size_t) settings.values[0]];
    std::vector<float> frames; double rate = 0;
    if (! device->latestInput (frames, block, rate, (int) settings.values[1])) return std::nullopt;
    return SpectrumReading { rate / block, spectrum::analyse (frames.data(), block) };
}
std::optional<Session::MidiMonitorReading> Session::midiMonitor (const String& pluginId) const
{
    auto* device = impl->nativeDevice (pluginId);
    if (device == nullptr || device->getKind() != EffectKind::midiMonitor) return std::nullopt;
    MidiMonitorReading reading; std::array<uint8_t, 16 * 128> held {};
    for (int attempt = 0; attempt < 4 && ! device->monitorReading (reading.messages, reading.total, held); ++attempt) {}
    std::vector<int> pitches;
    for (int channel = 0; channel < 16; ++channel)
        for (int pitch = 0; pitch < 128; ++pitch)
            if (const int velocity = held[(size_t) (channel * 128 + pitch)]; velocity > 0) { reading.held.push_back ({ channel, pitch, velocity }); pitches.push_back (pitch); }
    reading.chord = midimonitor::chordName (pitches);
    return reading;
}
Result Session::addGroove (const GrooveInfo& groove, String& grooveId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto copy = groove; copy.name = copy.name.trim();
    if (! grooves::valid (copy)) return Result::fail ("The groove's settings are out of range.");
    auto pool = impl->edit->state.getChildWithName (groovePoolType);
    if (pool.getNumChildren() >= maximumGrooves) return Result::fail ("The groove pool is full.");
    // Names stay distinguishable in the clip chooser.
    const auto taken = [&] (const String& name) { for (const auto& g : pool) if (g["name"].toString() == name) return true; return false; };
    for (int n = 2; taken (copy.name); ++n) copy.name = groove.name.trim() + " " + String (n);
    copy.id = juce::Uuid().toString();
    impl->transaction ("Add groove");
    auto* undo = &impl->edit->getUndoManager();
    impl->edit->state.getOrCreateChildWithName (groovePoolType, undo).appendChild (grooveTree (copy), undo);
    grooveId = copy.id;
    impl->changed(); return Result::ok();
}
Result Session::extractGroove (const String& clipId, String& grooveId)
{
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("Grooves are extracted from MIDI clips.");
    // A looping clip's groove is its loop, measured from the loop start.
    auto notes = describeClip (*clip).notes;
    const bool looping = clip->isLooping();
    if (looping) for (auto& n : notes) n.startBeat -= clip->getLoopStartBeats().inBeats();
    const auto length = looping ? clip->getLoopLengthBeats().inBeats() : clip->getLengthInBeats().inBeats();
    const auto groove = grooves::extract (clip->getName(), notes, length);
    if (groove.events.empty()) return Result::fail ("The clip has no notes to extract a groove from.");
    return addGroove (groove, grooveId);
}
Result Session::setGroove (const String& grooveId, const GrooveInfo& groove)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto state = grooveState (impl->edit->state, grooveId); if (! state.isValid()) return Result::fail ("Groove not found.");
    auto copy = groove; copy.id = grooveId; copy.name = copy.name.trim();
    if (! grooves::valid (copy)) return Result::fail ("The groove's settings are out of range.");
    if (readGroove (state) == copy) return Result::ok();
    impl->transaction ("Groove");
    auto* undo = &impl->edit->getUndoManager();
    auto pool = state.getParent(); const auto index = pool.indexOf (state);
    pool.removeChild (index, undo); pool.addChild (grooveTree (copy), index, undo);
    impl->changed(); return Result::ok();
}
Result Session::removeGroove (const String& grooveId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto state = grooveState (impl->edit->state, grooveId); if (! state.isValid()) return Result::fail ("Groove not found.");
    impl->transaction ("Remove groove");
    auto* undo = &impl->edit->getUndoManager();
    for (auto* t : te::getAudioTracks (*impl->edit))
        for (auto* c : allTrackClips (*t))
            if (c->state[clipGrooveProperty].toString() == grooveId) c->state.removeProperty (clipGrooveProperty, undo);
    state.getParent().removeChild (state, undo);
    impl->changed(); return Result::ok();
}
Result Session::setGrooveAmount (float percent)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! std::isfinite (percent) || percent < 0 || percent > maximumGrooveAmount) return Result::fail ("Choose a global groove amount from 0 to 130%.");
    if (grooveAmount (impl->edit->state) == percent) return Result::ok();
    impl->transaction ("Groove amount");
    auto* undo = &impl->edit->getUndoManager();
    impl->edit->state.getOrCreateChildWithName (groovePoolType, undo).setProperty ("amount", percent, undo);
    impl->changed(); return Result::ok();
}
Result Session::setClipGroove (const String& clipId, const String& grooveId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId);
    if (! clip) return Result::fail (impl->audioClip (clipId) != nullptr ? "Grooves apply to MIDI clips." : "MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    if (grooveId.isNotEmpty() && ! grooveState (impl->edit->state, grooveId).isValid()) return Result::fail ("Groove not found.");
    if (clip->state[clipGrooveProperty].toString() == grooveId) return Result::ok();
    impl->transaction ("Clip groove");
    auto* undo = &impl->edit->getUndoManager();
    if (grooveId.isEmpty()) clip->state.removeProperty (clipGrooveProperty, undo);
    else clip->state.setProperty (clipGrooveProperty, grooveId, undo);
    impl->changed(); return Result::ok();
}
Result Session::commitClipGroove (const String& clipId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before committing a groove.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    if (clip->state[clipGrooveProperty].toString().isEmpty()) return Result::fail ("The clip has no groove to commit.");
    impl->transaction ("Commit groove");
    auto* undo = &impl->edit->getUndoManager();
    if (const auto pattern = clipGroovePattern (*clip))
    {
        // Each note lands where grooved playback put it; one moved before the clip start starts
        // at the clip start and keeps its end, as playback does.
        struct Moved { te::MidiNote* note; double start, length; int velocity; };
        std::vector<Moved> moved;
        for (auto* note : clip->getSequence().getNotes())
        {
            const auto placed = placeNote (*pattern, *clip, note->getStartBeat().inBeats(), note->getNoteNumber(), note->getVelocity());
            auto length = note->getLengthBeats().inBeats(), start = placed.beat;
            if (start < 0) { length = std::max (1.0 / 960, length + start); start = 0; }
            moved.push_back ({ note, start, length, placed.velocity });
        }
        for (const auto& m : moved)
        {
            m.note->setStartAndLength (te::BeatPosition::fromBeats (m.start), te::BeatDuration::fromBeats (m.length), undo);
            m.note->setVelocity (m.velocity, undo);
        }
    }
    clip->state.removeProperty (clipGrooveProperty, undo);
    impl->changed(); return Result::ok();
}
Result Session::setClipScale (const String& clipId, const ScaleSettings& scale)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (scale.root < 0 || scale.root > 11 || scale.mode < 0 || scale.mode >= scaleModeCount) return Result::fail ("Choose a root from C to B and a listed scale.");
    if (clipScale (clip->state) == scale) return Result::ok();
    impl->transaction ("Clip scale");
    auto* undo = &impl->edit->getUndoManager();
    clip->state.setProperty (scaleEnabledProperty, scale.enabled ? 1 : 0, undo);
    clip->state.setProperty (scaleRootProperty, scale.root, undo);
    clip->state.setProperty (scaleModeProperty, scale.mode, undo);
    impl->changed(); return Result::ok();
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
Result Session::removeTrackInstrument (const String& trackId)
{
    if (auto frozen = impl->unfrozen (impl->track (trackId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackId);
    if (! track || ! track->state.getProperty (midiTrackProperty, false)) return Result::fail ("Instruments need a MIDI track.");
    auto* current = Impl::instrument (*track);
    if (current == nullptr) return Result::ok();
    closePluginEditors();
    impl->transaction ("Remove instrument");
    current->deleteFromParent();
    impl->changed(); return Result::ok();
}
juce::StringArray Session::midiInputPorts() const
{
    juce::StringArray names;
    for (const auto& info : juce::MidiInput::getAvailableDevices()) if (impl->portAllowed (info.name)) names.add (info.name);
    return names;
}
Result Session::setMidiInputSync (const String& port, bool on)
{
    if (! midiInputPorts().contains (port)) return Result::fail ("The MIDI input \"" + port + "\" is not available.");
    if (impl->syncOn (port) == on) return Result::ok();
    impl->engine.getPropertyStorage().setPropertyItem (te::SettingID::midiin, Impl::syncItem (port), on);
    impl->openClockInputs();
    sendChangeMessage(); return Result::ok();
}
bool Session::isMidiInputSync (const String& port) const { return impl->syncOn (port); }
Result Session::setExternalSync (bool on)
{
    if (on == impl->externalSync) return Result::ok();
    if (on && impl->edit->tempoSequence.getNumTempos() > 1) return Result::fail ("External Sync sets one tempo; remove the tempo map's changes first.");
    impl->externalSync = on; impl->clockRunning = false; impl->ticksSinceFollow = 0;
    sendChangeMessage(); return Result::ok();
}
bool Session::isExternalSync() const { return impl->externalSync; }
bool Session::hasSyncInputs() const { return ! impl->clockInputs.empty(); }
Session::ExternalClock Session::externalClock() const { return { impl->clockReceiving(), impl->clockReceiving() ? impl->clockBpm : 0.0 }; }
Result Session::setControlSurface (const ControlSurface& wanted)
{
    if (wanted == impl->surface) return Result::ok();
    auto result = impl->openSurface (wanted);
    auto& storage = impl->engine.getPropertyStorage();
    storage.setPropertyItem (te::SettingID::midiin, "halftrackSurfaceInput", impl->surface.input);
    storage.setPropertyItem (te::SettingID::midiin, "halftrackSurfaceOutput", impl->surface.output);
    sendChangeMessage(); return result;
}
Session::ControlSurface Session::controlSurface() const { return impl->surface; }
int Session::controlSurfaceBank() const { return impl->surfaceBank; }
void Session::updateControlSurface() { impl->sendSurfaceState(); }
std::vector<Session::MidiOutputPortInfo> Session::midiOutputPorts() const
{
    std::vector<MidiOutputPortInfo> ports;
    auto& devices = impl->engine.getDeviceManager();
    for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
        if (auto* device = devices.getMidiOutDevice (i))
            ports.push_back ({ device->getName(), impl->midiPortTrack (*device), device->isEnabled() && device->isSendingClock() });
    return ports;
}
Result Session::setMidiOutputPort (const String& name, bool track, bool sync)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->anyRecording()) return Result::fail ("Stop recording before changing MIDI ports.");
    auto* device = impl->midiOutput (name);
    if (device == nullptr) return Result::fail ("That MIDI port is not available.");
    impl->engine.getPropertyStorage().setPropertyItem (te::SettingID::midiout, Impl::midiPortTrackItem (*device), track);
    device->setSendingClock (sync);
    // A port opens while either switch is on, and the engine reopens its devices synchronously.
    if (device->isEnabled() != (track || sync))
    {
        device->setEnabled (track || sync);
        impl->engine.getDeviceManager().rescanMidiDeviceListSynchronously();
    }
    impl->edit->restartPlayback(); sendChangeMessage(); return Result::ok();
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
Result Session::linkTracks (const juce::StringArray& ids, String& linkId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    std::vector<te::AudioTrack*> tracks;
    for (const auto& id : ids)
    {
        auto* t = impl->track (id);
        if (t == nullptr) return Result::fail ("A selected track no longer exists.");
        if (isReturn (*t)) return Result::fail ("Return tracks cannot be linked.");
        if (std::find (tracks.begin(), tracks.end(), t) == tracks.end()) tracks.push_back (t);
    }
    if (tracks.size() < 2) return Result::fail ("Select two or more audio or MIDI tracks to link.");
    // Already exactly these tracks' link: nothing changes.
    const auto current = tracks.front()->state[linkProperty].toString();
    if (current.isNotEmpty() && std::all_of (tracks.begin(), tracks.end(), [&] (auto* t) { return t->state[linkProperty].toString() == current; })
        && impl->linkedTracks (*tracks.front()).size() + 1 == tracks.size())
    { linkId = current; return Result::ok(); }
    impl->transaction ("Link tracks");
    linkId = juce::Uuid().toString();
    juce::StringArray left;
    for (auto* t : tracks) { left.addIfNotAlreadyThere (t->state[linkProperty].toString()); t->state.setProperty (linkProperty, linkId, &impl->edit->getUndoManager()); }
    impl->endSingleLinks (left);
    impl->changed(); return Result::ok();
}
Result Session::unlinkTracks (const juce::StringArray& ids)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    std::vector<te::AudioTrack*> tracks;
    for (const auto& id : ids)
    {
        auto* t = impl->track (id);
        if (t == nullptr) return Result::fail ("A selected track no longer exists.");
        if (t->state.hasProperty (linkProperty)) tracks.push_back (t);
    }
    if (tracks.empty()) return Result::ok();
    impl->transaction ("Unlink tracks");
    juce::StringArray left;
    for (auto* t : tracks) { left.addIfNotAlreadyThere (t->state[linkProperty].toString()); t->state.removeProperty (linkProperty, &impl->edit->getUndoManager()); }
    impl->endSingleLinks (left);
    impl->changed(); return Result::ok();
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
    track->state.removeProperty (linkProperty, undo); // A duplicate starts unlinked.
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
    if (impl->anyRecording()) return Result::fail ("Stop recording before freezing tracks.");
    auto* track = impl->track (trackId);
    if (track == nullptr || isReturn (*track)) return Result::fail ("Freeze an audio or MIDI track.");
    if (track->isFrozen (te::Track::individualFreeze) == frozen) return Result::ok();
    if (frozen && track->getClips().isEmpty()) return Result::fail ("This track has no Arrangement clips to freeze.");
    closePluginEditors();
    impl->transaction (frozen ? freezeStep : "Unfreeze track");
    {
        // The engine renders the freeze, or removes it, as this undoable property changes. Its
        // render pumps events, so hold off the engine's idle timer, which would otherwise split
        // the engine's own edits after a long render into a separate undo step.
        const te::Edit::UndoTransactionInhibitor inhibitor (*impl->edit);
        const FreezeRender render (frozen ? juce::Array<te::AudioTrack*> { track } : juce::Array<te::AudioTrack*>());
        track->state.setProperty (te::IDs::frozenIndividually, frozen, &impl->edit->getUndoManager());
        impl->edit->dispatchPendingUpdatesSynchronously();
    }
    if (frozen && ! track->isFrozen (te::Track::individualFreeze)) return Result::fail ("The track could not be frozen.");
    impl->changed(); return Result::ok();
}
Result Session::flattenTrack (const String& trackId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->anyRecording()) return Result::fail ("Stop recording before flattening tracks.");
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
    // The transaction starts first so the engine's own unfreeze edits (removing its freeze
    // point) belong to it, rather than to a separate step its idle timer may have opened.
    impl->transaction ("Flatten track");
    track->state.setProperty (te::IDs::frozenIndividually, false, nullptr);
    auto* undo = &impl->edit->getUndoManager();
    // Remove the sound sources and devices; the mixer, meter and sends stay.
    for (auto* plugin : track->pluginList.getPlugins())
        if (dynamic_cast<NativeEffectPlugin*> (plugin) || dynamic_cast<te::ExternalPlugin*> (plugin) || dynamic_cast<UtilityPlugin*> (plugin)
            || dynamic_cast<SubtractiveSynthPlugin*> (plugin) || dynamic_cast<te::FreezePointPlugin*> (plugin))
            plugin->deleteFromParent();
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
    // Other tracks can go during a Session take; this one would take the recording slot with it.
    if (impl->trackRecording (t->itemID.toString())) return Result::fail ("End this track's Session recording before removing it.");
    closePluginEditors(); impl->meters.clear(); impl->transaction ("Remove track");
    if (isReturn (*t))
        for (auto* source : te::getAudioTracks (*impl->edit))
            if (auto* send = findSend (*source, t->itemID.toString())) send->deleteFromParent();
    // Plug-ins taking their sidechain from it keep their sidechain, without a source.
    for (auto* p : impl->edit->getPluginCache().getPlugins())
        if (p->getSidechainSourceID() == t->itemID) p->state.removeProperty (te::IDs::sidechainSourceID, &impl->edit->getUndoManager());
    impl->edit->deleteTrack (t); impl->refreshRouting(); impl->changed(); return Result::ok();
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
    const OneUndoStep step (*this, impl->prefs.autoWarpLongSamples && audio.getLength() > 30, impl->gesture != nullptr, "Import audio");
    impl->transaction ("Import audio");
    auto clip = t->insertWaveClip (source.getFileNameWithoutExtension(), file,
        { { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (start + audio.getLength()) }, {} }, false);
    if (! clip) return Result::fail ("Could not create the audio clip.");
    clip->getSourceFileReference().setToFile (file, te::SourceFileReference::PathStyle::alwaysAbsolute, false);
    clip->setAutoTempo (false); clip->setAutoPitch (false);
    clip->setFadeIn (te::TimeDuration::fromSeconds (0)); clip->setFadeOut (te::TimeDuration::fromSeconds (0));
    // Auto-Warp Long Samples: a long sample is taken for a song. Its tempo and first beat come from
    // its content: the clip starts on the first beat, with a warp marker there. A sound with no
    // steady beat to find takes the tempo of whole bars.
    if (impl->prefs.autoWarpLongSamples && audio.getLength() > 30 && ! impl->importingVideo)
    {
        const auto found = impl->analyse (file)->tempo;
        const auto first = found && found->firstBeatSeconds < audio.getLength() - 1 ? found->firstBeatSeconds : 0.0;
        if (first > 0)
            clip->setPosition ({ { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (start + audio.getLength() - first) }, te::TimeDuration::fromSeconds (first) });
        const auto bpm = found ? found->bpm : estimateClipTempo (audio.getLength());
        if (auto warped = setAudioClipWarp (clip->itemID.toString(), { impl->prefs.defaultWarpMode, bpm }); warped.failed()) return warped;
        if (first > 0 && impl->prefs.defaultWarpMode != WarpMode::repitch)
            if (auto marked = setAudioClipWarpMarkers (clip->itemID.toString(), { { 0, 0 }, { first, first * bpm / 60 } }); marked.failed()) return marked;
    }
    impl->changed(); return Result::ok();
}
Result Session::setClipLoop (const String& id, const ClipLoop& loop, double startBeat)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing clip loops.");
    auto* clip = impl->anyClip (id); if (! clip) return Result::fail ("Clip not found.");
    const auto before = describeClip (*clip);
    if (! before.canLoop) return Result::fail ("Loops are set on MIDI clips and warped audio clips.");
    auto* wave = dynamic_cast<te::WaveAudioClip*> (clip);
    const double sourceBeats = wave != nullptr ? wave->getLoopInfo().getNumBeats() : 1000000;
    const auto within = [] (double v) { return std::isfinite (v) && v >= 0 && v <= 1000000; };
    if (! within (startBeat) || ! within (loop.start) || ! within (loop.length)) return Result::fail ("Clip loop and start beats must be zero or more.");
    if (loop.on)
    {
        if (loop.length < 1.0 / 64) return Result::fail ("A loop lasts at least a 64th of a beat.");
        if (startBeat < loop.start - 1e-9 || startBeat >= loop.start + loop.length - 1e-9) return Result::fail ("The clip's start must lie in its loop.");
        if (loop.start + loop.length > sourceBeats + 1e-6) return Result::fail ("A warped clip's loop must end within its audio.");
    }
    else if (startBeat >= sourceBeats - 1e-6) return Result::fail ("The start must lie within the clip's audio.");
    if (before.loop.on == loop.on && (! loop.on || before.loop == loop) && std::abs (before.contentStartBeat - startBeat) < 1e-9) return Result::ok();
    impl->transaction ("Set clip loop");
    const auto bps = impl->edit->tempoSequence.getBeatsPerSecondAt (clip->getPosition().getStart());
    if (loop.on) clip->setLoopRangeBeats ({ te::BeatPosition::fromBeats (loop.start), te::BeatPosition::fromBeats (loop.start + loop.length) });
    else Impl::endLoop (*clip);
    auto position = clip->getPosition();
    position.offset = te::TimeDuration::fromSeconds ((loop.on ? startBeat - loop.start : startBeat) / bps);
    // A Session clip unlooped plays once, from its start to where its loop ended.
    if (! loop.on && before.loop.on && clip->getClipSlot() != nullptr && before.loop.start + before.loop.length > startBeat + 1e-9)
        position.time = position.time.withLength (te::TimeDuration::fromSeconds ((before.loop.start + before.loop.length - startBeat) / bps));
    // Unlooped, warped audio ends with its source.
    if (! loop.on && wave != nullptr)
    {
        const auto startBeatOfClip = impl->edit->tempoSequence.toBeats (position.getStart()).inBeats();
        const auto end = impl->edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeatOfClip + sourceBeats - startBeat));
        if (position.getEnd() > end) position.time = position.time.withEnd (end);
    }
    clip->setPosition (position);
    impl->changed(); return Result::ok();
}
Result Session::setClipBeats (const String& id, double startBeat, double lengthBeats, double contentStart)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before trimming clips.");
    auto* clip = impl->anyClip (id); if (! clip) return Result::fail ("Clip not found.");
    const auto before = describeClip (*clip);
    if (! before.canLoop) return Result::fail ("Use the audio clip's own range for unwarped audio.");
    // A Session clip sits at its slot's start; only its length and content start change.
    if (clip->getClipSlot() != nullptr && std::abs (startBeat) > 1e-9) return Result::fail ("A Session clip starts at its slot; set its length and start only.");
    if (clip->isGrouped()) return Result::fail ("Ungroup this clip before trimming it.");
    auto* wave = dynamic_cast<te::WaveAudioClip*> (clip);
    const double sourceBeats = wave != nullptr ? wave->getLoopInfo().getNumBeats() : 1000000;
    if (! std::isfinite (startBeat) || ! std::isfinite (lengthBeats) || ! std::isfinite (contentStart) || startBeat < 0 || contentStart < 0
        || lengthBeats < 1.0 / 64 || startBeat + lengthBeats > 1000000)
        return Result::fail ("A clip's start and content start must be zero or more, and it must last at least a 64th of a beat.");
    if (before.loop.on && (contentStart < before.loop.start - 1e-9 || contentStart >= before.loop.start + before.loop.length - 1e-9))
        return Result::fail ("A looped clip's start must lie in its loop.");
    if (! before.loop.on && wave != nullptr && contentStart + lengthBeats > sourceBeats + 1e-6)
        return Result::fail ("An unlooped warped clip cannot run past the end of its audio.");
    auto& tempo = impl->edit->tempoSequence;
    const auto start = tempo.toTime (te::BeatPosition::fromBeats (startBeat)), end = tempo.toTime (te::BeatPosition::fromBeats (startBeat + lengthBeats));
    const auto offset = te::TimeDuration::fromSeconds ((before.loop.on ? contentStart - before.loop.start : contentStart) / tempo.getBeatsPerSecondAt (start));
    const auto now = clip->getPosition();
    if (std::abs ((now.getStart() - start).inSeconds()) < 1e-9 && std::abs ((now.getEnd() - end).inSeconds()) < 1e-9 && std::abs ((now.offset - offset).inSeconds()) < 1e-9) return Result::ok();
    impl->transaction ("Trim clip");
    clip->setPosition ({ { start, end }, offset });
    impl->changed(); return Result::ok();
}
Result Session::importVideo (const File& movie, int index, double start)
{
    if (! video::supported()) return Result::fail ("Movies are supported on macOS only.");
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before importing a movie.");
    video::Info info;
    if (auto r = video::probe (movie, info); r.failed()) return r;
    auto assets = impl->assets();
    if (auto r = assets.createDirectory(); r.failed()) return r;
    // The movie and its sound join the project's media.
    const auto kept = movie.isAChildOf (assets) ? movie : assets.getNonexistentChildFile (movie.getFileNameWithoutExtension(), movie.getFileExtension(), true);
    if (kept != movie && ! movie.copyFileTo (kept)) return Result::fail ("Could not collect the movie into the project.");
    const auto sound = assets.getNonexistentChildFile (movie.getFileNameWithoutExtension() + " Sound", ".wav", true);
    const auto discard = [&] { sound.deleteFile(); if (kept != movie) kept.deleteFile(); };
    if (auto r = video::extractSound (kept, sound); r.failed()) { discard(); return r; }
    const OneUndoStep step (*this, true, impl->gesture != nullptr, "Import movie");
    if (index < 0)
    {
        if (auto r = addAudioTrack (movie.getFileNameWithoutExtension()); r.failed()) { discard(); return r; }
        index = te::getAudioTracks (*impl->edit).size() - 1;
    }
    impl->importingVideo = true;
    const auto imported = importAudio (sound, index, start);
    impl->importingVideo = false;
    if (imported.failed()) { discard(); return imported; }
    auto* track = impl->track (index);
    for (auto* c : track != nullptr ? track->getClips() : juce::Array<te::Clip*> {})
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (c); wave != nullptr && wave->getSourceFileReference().getFile() == sound && wave->state[videoProperty].toString().isEmpty())
        {
            wave->state.setProperty (videoProperty, kept.getFullPathName(), &impl->edit->getUndoManager());
            wave->setName (movie.getFileNameWithoutExtension());
        }
    impl->changed(); return Result::ok();
}
std::optional<Session::VideoFrame> Session::videoAt (double seconds) const
{
    for (auto* track : te::getAudioTracks (*impl->edit))
        for (auto* c : track->getClips())
            if (auto* wave = dynamic_cast<te::WaveAudioClip*> (c); wave != nullptr && ! wave->isMuted())
            {
                const auto movie = wave->state[videoProperty].toString();
                const auto p = wave->getPosition();
                if (movie.isNotEmpty() && seconds >= p.getStart().inSeconds() && seconds < p.getEnd().inSeconds())
                    return VideoFrame { impl->edit->filePathResolver (movie), p.offset.inSeconds() + seconds - p.getStart().inSeconds(), wave->itemID.toString() };
            }
    return std::nullopt;
}
namespace
{
// The beat a point of a source plays on, through warp markers and then the clip's tempo.
double beatOfSource (const std::vector<Session::WarpMarker>& markers, double bpm, double seconds)
{
    auto map = markers;
    if (map.empty() || map.front().sourceSeconds > 0 || map.front().beat > 0) map.insert (map.begin(), { 0, 0 });
    for (size_t i = 1; i < map.size(); ++i)
        if (seconds <= map[i].sourceSeconds)
        {
            const auto span = map[i].sourceSeconds - map[i - 1].sourceSeconds;
            return map[i - 1].beat + (span > 0 ? (seconds - map[i - 1].sourceSeconds) / span : 0) * (map[i].beat - map[i - 1].beat);
        }
    return map.back().beat + (seconds - map.back().sourceSeconds) * bpm / 60;
}
}
Result Session::warpFromHere (const String& id, double seconds)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before warping clips.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    const auto before = describeClip (*clip);
    if (before.warp.mode == WarpMode::off || before.warp.mode == WarpMode::repitch) return Result::fail ("Warp From Here needs a clip warped in a stretching mode.");
    if (! std::isfinite (seconds) || seconds < 0 || seconds >= before.sourceLength - 1) return Result::fail ("Warp From Here needs a point at least a second before the audio ends.");
    // The tempo from the point on, and the point moved onto the transient there.
    std::unique_ptr<juce::AudioFormatReader> reader (impl->engine.getAudioFileFormatManager().readFormatManager.createReaderFor (before.file));
    if (reader == nullptr) return Result::fail ("The clip's audio could not be read.");
    tempo::OnsetCurve curve (reader->sampleRate);
    if (! curve.add (*reader, 600, seconds)) return Result::fail ("The clip's audio could not be read.");
    const auto found = tempo::estimate (curve);
    if (! found) return Result::fail ("No steady beat was found from there.");
    auto point = seconds;
    double nearest = 0.03;
    for (const auto t : impl->analyse (before.file)->transients)
        if (std::abs (t - seconds) < nearest) { nearest = std::abs (t - seconds); point = t; }
    // Markers before the point stay, and the point keeps the beat it plays on now.
    std::vector<WarpMarker> markers;
    for (const auto& m : before.warpMarkers) if (m.sourceSeconds < point - 1e-3) markers.push_back (m);
    if (markers.empty() || markers.front().sourceSeconds > 0 || markers.front().beat > 0) markers.insert (markers.begin(), { 0, 0 });
    const WarpMarker pinned { point, beatOfSource (before.warpMarkers, before.warp.bpm, point) };
    if (pinned.beat <= markers.back().beat + 1e-6) return Result::fail ("Warp From Here needs the point after the clip's earlier markers.");
    markers.push_back (pinned);
    const OneUndoStep step (*this, true, impl->gesture != nullptr, "Warp From Here");
    auto settings = before.warp; settings.bpm = found->bpm;
    if (auto r = setAudioClipWarp (id, settings); r.failed()) return r;
    if (auto r = setAudioClipWarpMarkers (id, markers); r.failed()) return r;
    // The clip keeps its place and length, ending with its audio if that now comes sooner.
    const auto now = describeClip (*clip);
    const auto audioEnd = beatOfSource (now.warpMarkers, now.warp.bpm, now.sourceLength);
    const auto length = std::min (before.lengthBeats, audioEnd - before.contentStartBeat);
    if (! before.loop.on && length > 0)
        if (auto* wave = impl->audioClip (id))
        {
            const auto& tempoMap = impl->edit->tempoSequence;
            const auto start = tempoMap.toTime (te::BeatPosition::fromBeats (before.startBeat)), end = tempoMap.toTime (te::BeatPosition::fromBeats (before.startBeat + length));
            wave->setPosition ({ { start, end }, te::TimeDuration::fromSeconds (before.contentStartBeat / tempoMap.getBeatsPerSecondAt (start)) });
        }
    impl->changed(); return Result::ok();
}
Result Session::warpAsLoop (const String& id, int bars)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    const auto before = describeClip (*clip);
    if (before.warp.mode == WarpMode::off) return Result::fail ("Warp as Loop needs a warped clip.");
    if (bars < 1 || bars > 256) return Result::fail ("Choose 1 to 256 bars.");
    // The source the clip plays, from its start for its length, in seconds of the source.
    const auto from = before.warpMarkers.empty() ? before.contentStartBeat * 60 / before.warp.bpm : 0.0;
    const auto to = before.warpMarkers.empty() ? (before.contentStartBeat + before.lengthBeats) * 60 / before.warp.bpm : before.sourceLength;
    const auto seconds = std::min (before.sourceLength, to) - from;
    if (! (seconds > 0)) return Result::fail ("The clip plays no audio.");
    const auto bpm = bars * snapshot().timeSignature.beatsPerBar() * 60 / seconds;
    if (bpm < 20 || bpm > 999) return Result::fail ("As " + String (bars) + " bars the clip would play at " + String (bpm, 2) + " BPM, outside 20-999.");
    const OneUndoStep step (*this, true, impl->gesture != nullptr, "Warp as loop");
    if (! before.warpMarkers.empty()) if (auto r = setAudioClipWarpMarkers (id, {}); r.failed()) return r;
    auto settings = before.warp; settings.bpm = bpm;
    if (auto r = setAudioClipWarp (id, settings); r.failed()) return r;
    impl->changed(); return Result::ok();
}
std::vector<double> Session::audioClipTransients (const String& id)
{
    auto* clip = impl->audioClip (id);
    if (clip == nullptr) return {};
    return impl->analyse (clip->getSourceFileReference().getFile())->transients;
}
double Session::estimateClipTempo (double seconds)
{
    if (! std::isfinite (seconds) || seconds <= 0) return 120;
    double beats = 4, bpm = beats * 60 / seconds;
    while (bpm < 80) { beats *= 2; bpm *= 2; }
    while (bpm >= 160 && beats > .25) { beats /= 2; bpm /= 2; }
    return juce::jlimit (20.0, 999.0, bpm);
}
Result Session::setAudioClipWarpMarkers (const String& id, const std::vector<WarpMarker>& markers)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing warp markers.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    const auto settings = warpSettings (*clip);
    if (settings.mode == WarpMode::off) return Result::fail ("Turn Warp on before adding warp markers.");
    if (settings.mode == WarpMode::repitch) return Result::fail ("Warp markers need a stretching warp mode; Re-Pitch does not use them.");
    const auto source = clip->getSourceLength().inSeconds();
    if (source <= 0) return Result::fail ("The clip's audio is not available.");
    if (markers.size() > 1024) return Result::fail ("A clip can hold at most 1024 warp markers.");
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto& m = markers[i];
        if (! std::isfinite (m.sourceSeconds) || ! std::isfinite (m.beat) || m.sourceSeconds < 0 || m.sourceSeconds > source || m.beat < 0 || m.beat > 1000000
            || (i > 0 && (m.sourceSeconds <= markers[i - 1].sourceSeconds + 1e-6 || m.beat <= markers[i - 1].beat + 1e-6)))
            return Result::fail ("Warp markers must increase in source time and beat and lie within the source.");
    }
    if (markers == warpMarkers (*clip)) return Result::ok();
    impl->transaction ("Edit warp markers");
    auto* undo = &impl->edit->getUndoManager();
    // The start alone is no markers at all.
    if (markers.empty() || (markers.size() == 1 && markers[0].sourceSeconds == 0 && markers[0].beat == 0))
    {
        if (clip->getWarpTime()) { clip->getWarpTimeManager().removeAllMarkers(); clip->setWarpTime (false); }
        clip->state.removeProperty (warpMarkersPausedProperty, undo);
        clip->state.removeProperty (implicitWarpEndProperty, undo);
        impl->changed(); return Result::ok();
    }
    clip->setWarpTime (true);
    auto& manager = clip->getWarpTimeManager();
    // The engine keeps a start marker at (0, 0); the clip's markers follow it, and Halftrack's end
    // marker keeps the clip's tempo after the last of them.
    auto tree = clip->state.getChildWithName (te::IDs::WARPTIME).getChildWithName (te::IDs::WARPMARKERS);
    if (! tree.isValid()) return Result::fail ("The clip's warp markers are unavailable.");
    const auto secondsPerBeat = 60.0 / settings.bpm;
    std::vector<std::pair<double, double>> points { { 0.0, 0.0 } };
    for (const auto& m : markers) if (m.sourceSeconds > 1e-9 || m.beat > 1e-9) points.push_back ({ m.sourceSeconds, m.beat * secondsPerBeat });
    if (points.size() > 1 && (points[1].first <= 1e-6 || points[1].second <= 1e-6)) return Result::fail ("Warp markers after the start must have a later source time and beat.");
    auto end = points.back().second;
    const bool implicitEnd = points.back().first < source - 1e-6;
    if (implicitEnd) { end += source - points.back().first; points.push_back ({ source, end }); }
    tree.removeAllChildren (undo);
    for (const auto& [sourceTime, warpTime] : points)
        tree.appendChild (juce::ValueTree (te::IDs::WARPMARKER, { { te::IDs::sourceTime, sourceTime }, { te::IDs::warpTime, warpTime } }), undo);
    if (implicitEnd) clip->state.setProperty (implicitWarpEndProperty, true, undo); else clip->state.removeProperty (implicitWarpEndProperty, undo);
    manager.setWarpEndMarkerTime (te::TimePosition::fromSeconds (end));
    impl->edit->restartPlayback();
    impl->changed(); return Result::ok();
}
// A mode's settings, with those of other modes at their defaults, as warpSettings reads them back.
Session::WarpSettings normalisedWarp (Session::WarpSettings s)
{
    using Mode = Session::WarpMode;
    const Session::WarpSettings defaults;
    if (s.mode != Mode::beats) { s.transientLoop = defaults.transientLoop; s.transientEnvelope = defaults.transientEnvelope; }
    if (s.mode != Mode::tones && s.mode != Mode::texture) s.grainSize = defaults.grainSize;
    if (s.mode != Mode::texture) s.flux = defaults.flux;
    if (s.mode != Mode::complexPro) s.formants = defaults.formants;
    return s;
}
Result Session::setAudioClipWarp (const String& id, const WarpSettings& requested)
{
    const auto settings = normalisedWarp (requested);
    const auto within = [] (float v, float low, float high) { return std::isfinite (v) && v >= low && v <= high; };
    if (! within (settings.transientEnvelope, 0, 100) || ! within (settings.grainSize, 0, 100) || ! within (settings.flux, 0, 100) || ! within (settings.formants, 0, 100)
        || ! juce::isPositiveAndBelow ((int) settings.transientLoop, 3) || ! juce::isPositiveAndBelow ((int) settings.mode, 8) || (settings.grainSize > 0 && settings.grainSize < 1))
        return Result::fail ("Warp settings are out of range: envelope, grain size, flux and formants run 0-100, grain size from 1.");
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (! std::isfinite (settings.bpm) || settings.bpm < 20 || settings.bpm > 999) return Result::fail ("Clip tempo must be 20-999 BPM.");
    if (clip->getIsReversed() || clip->isUsingARA() || clip->isGrouped()) return Result::fail ("Unreverse or ungroup this clip before warping it.");
    if (settings.mode != WarpMode::off && clip->state[videoProperty].toString().isNotEmpty()) return Result::fail ("A video clip plays at its own speed, so it is not warped.");
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
        // Unwarped audio does not loop here: the loop ends, the clip's start staying where it is.
        if (clip->isLooping()) { Impl::endLoop (*clip); offset = clip->getPosition().getOffset().inSeconds(); }
        // Back to source time: the visible source span is unchanged.
        const auto scale = tempo / previous.bpm;
        length *= scale; offset *= scale;
        offset = juce::jlimit (0.0, source, offset); length = juce::jlimit (0.001, source - offset, length);
        clip->setAutoTempo (false); clip->setTimeStretchMode (te::TimeStretcher::disabled);
        clip->state.removeProperty (repitchProperty, &impl->edit->getUndoManager());
        impl->clearTransposition (*clip);
        // Markers rest while warp is off.
        if (clip->getWarpTime()) { clip->state.setProperty (warpMarkersPausedProperty, true, &impl->edit->getUndoManager()); clip->setWarpTime (false); }
        clip->setPosition ({ { start, start + te::TimeDuration::fromSeconds (length) }, te::TimeDuration::fromSeconds (offset) });
        impl->changed(); return Result::ok();
    }
    // Re-Pitch resamples with no time stretcher, which the engine's warp markers need, so they rest
    // as with Warp off and return with a stretching mode.
    const bool repitch = settings.mode == WarpMode::repitch;
    // Set first: the stretch mode's change rebuilds the graph, which reads it.
    if (repitch) clip->state.setProperty (repitchProperty, true, &impl->edit->getUndoManager());
    else clip->state.removeProperty (repitchProperty, &impl->edit->getUndoManager());
    // Transposing keeps the speed, which only the stretching modes can.
    if (repitch) impl->clearTransposition (*clip);
    // The mode's settings first, read when the graph rebuilds for the mode.
    auto options = clip->elastiqueProOptions.get();
    options.halftrack = {};
    if (settings.mode == WarpMode::beats)
    {
        options.halftrack[warp::beats::loopMode] = (float) settings.transientLoop;
        options.halftrack[warp::beats::fade] = 100 - settings.transientEnvelope;
    }
    if (settings.mode == WarpMode::tones || settings.mode == WarpMode::texture) options.halftrack[warp::grains::size] = settings.grainSize;
    if (settings.mode == WarpMode::texture) options.halftrack[warp::grains::flux] = settings.flux;
    if (settings.mode == WarpMode::complexPro) options.halftrack[warp::complexPro::formantShift] = 100 - settings.formants;
    if (options != clip->elastiqueProOptions.get()) clip->elastiqueProOptions = options;
    clip->setTimeStretchMode (repitch ? te::TimeStretcher::disabled
                              : settings.mode == WarpMode::efficient ? te::TimeStretcher::signalsmithCheaper
                              : settings.mode == WarpMode::beats ? te::TimeStretcher::halftrackBeats
                              : settings.mode == WarpMode::tones ? te::TimeStretcher::halftrackTones
                              : settings.mode == WarpMode::texture ? te::TimeStretcher::halftrackTexture
                              : settings.mode == WarpMode::complexPro ? te::TimeStretcher::halftrackComplexPro : te::TimeStretcher::signalsmithDefault);
    clip->setUsesProxy (false);
    if (repitch && clip->getWarpTime()) { clip->state.setProperty (warpMarkersPausedProperty, true, &impl->edit->getUndoManager()); clip->setWarpTime (false); }
    if (! repitch && static_cast<bool> (clip->state.getProperty (warpMarkersPausedProperty, false)))
    { clip->state.removeProperty (warpMarkersPausedProperty, &impl->edit->getUndoManager()); clip->setWarpTime (true); }
    const auto scale = previous.mode == WarpMode::off ? settings.bpm / tempo : settings.bpm / previous.bpm;
    clip->getLoopInfo().setNumBeats (settings.bpm * source / 60);
    // A loop covers the same audio at the new tempo.
    if (clip->isLooping() && previous.mode != WarpMode::off && previous.bpm != settings.bpm)
    {
        const auto loop = clip->getLoopRangeBeats();
        clip->setLoopRangeBeats ({ te::BeatPosition::fromBeats (loop.getStart().inBeats() * scale), te::BeatPosition::fromBeats (loop.getEnd().inBeats() * scale) });
    }
    if (! slot)
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
    impl->endAudition(); // No pointer may refer to an audition's copies.
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (! validAudioSettings (settings, clip->getPosition().getLength().inSeconds()))
        return Result::fail ("Clip gain, fades or transposition are outside their supported range. Fades must fit inside the clip.");
    const auto previous = audioSettings (*clip);
    const auto pitched = settings.transposeSemitones != 0 || settings.detuneCents != 0;
    if (pitched && (previous.transposeSemitones != settings.transposeSemitones || previous.detuneCents != settings.detuneCents))
        if (const auto mode = warpSettings (*clip).mode; mode == WarpMode::off || mode == WarpMode::repitch)
            return Result::fail ("Warp the clip in a stretching mode to transpose it.");
    if (clip->getClipSlot() && (previous.fadeInSeconds != settings.fadeInSeconds || previous.fadeOutSeconds != settings.fadeOutSeconds
        || previous.fadeInCurve != settings.fadeInCurve || previous.fadeOutCurve != settings.fadeOutCurve))
        return Result::fail ("Manual fade editing is available for Arrangement clips.");
    // Linked audio clips take the fade changes: a fade-in on those starting with this clip, a
    // fade-out on those whose fade-out starts where this one's did. Their gain and pitch stay.
    const bool fadeIn = previous.fadeInSeconds != settings.fadeInSeconds || previous.fadeInCurve != settings.fadeInCurve;
    const bool fadeOut = previous.fadeOutSeconds != settings.fadeOutSeconds || previous.fadeOutCurve != settings.fadeOutCurve;
    std::vector<std::pair<te::WaveAudioClip*, AudioClipSettings>> linked;
    if (fadeIn || fadeOut)
        for (auto* other : impl->linkedClips (*clip))
            if (auto* wave = dynamic_cast<te::WaveAudioClip*> (other))
            {
                const auto own = audioSettings (*wave);
                auto next = own;
                if (fadeIn) { next.fadeInSeconds = settings.fadeInSeconds; next.fadeInCurve = settings.fadeInCurve; }
                if (fadeOut && std::abs (own.fadeOutSeconds - previous.fadeOutSeconds) < .001) { next.fadeOutSeconds = settings.fadeOutSeconds; next.fadeOutCurve = settings.fadeOutCurve; }
                if (next == own) continue;
                if (auto frozen = impl->unfrozen (wave->getClipTrack()); frozen.failed()) return frozen;
                if (! validAudioSettings (next, wave->getPosition().getLength().inSeconds()))
                    return Result::fail ("The fades do not fit \"" + wave->getName() + "\" on a linked track.");
                linked.push_back ({ wave, next });
            }
    if (previous == settings && ! clip->getAutoCrossfade() && linked.empty()) return Result::ok();
    impl->transaction ("Audio clip gain and fades");
    const auto apply = [this] (te::WaveAudioClip& target, const AudioClipSettings& before, const AudioClipSettings& after)
    {
        target.setAutoCrossfade (false); target.setGainDB (after.gainDb);
        // Clear first so the engine's one-edge setters cannot rescale the other edge.
        target.setFadeIn ({}); target.setFadeOut (te::TimeDuration::fromSeconds (after.fadeOutSeconds));
        target.setFadeIn (te::TimeDuration::fromSeconds (after.fadeInSeconds));
        target.setFadeInType (static_cast<te::AudioFadeCurve::Type> (after.fadeInCurve));
        target.setFadeOutType (static_cast<te::AudioFadeCurve::Type> (after.fadeOutCurve));
        if (before.transposeSemitones != after.transposeSemitones || before.detuneCents != after.detuneCents)
        {
            auto* undo = &impl->edit->getUndoManager();
            if (after.transposeSemitones != 0) target.state.setProperty (transposeProperty, after.transposeSemitones, undo); else target.state.removeProperty (transposeProperty, undo);
            if (after.detuneCents != 0) target.state.setProperty (detuneProperty, after.detuneCents, undo); else target.state.removeProperty (detuneProperty, undo);
            target.setPitchChange ((float) after.transposeSemitones + after.detuneCents / 100.0f);
        }
    };
    apply (*clip, previous, settings);
    for (auto& [wave, next] : linked) apply (*wave, audioSettings (*wave), next);
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
    impl->endAudition();
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (! canEditAudioRange (*clip)) return Result::fail ("Range editing requires an unwarped, unlooped arrangement audio clip.");
    const auto sourceLength = clip->getSourceLength().inSeconds();
    if (! std::isfinite (start) || ! std::isfinite (length) || ! std::isfinite (offset)
        || start < 0 || length < .001 || start + length > te::Edit::maximumLength || offset < 0
        || offset + length > sourceLength + 1e-9 || ! validAudioSettings (audioSettings (*clip), length))
        return Result::fail ("The range must fit the source and existing fades, with at least 1 ms of audio.");
    const auto p = clip->getPosition();
    if (p.getStart().inSeconds() == start && p.getLength().inSeconds() == length && p.offset.inSeconds() == offset) return Result::ok();
    // Linked audio clips move each edge and their source offset by the same amounts.
    const auto startShift = start - p.getStart().inSeconds(), endShift = start + length - p.getEnd().inSeconds(), offsetShift = offset - p.offset.inSeconds();
    std::vector<std::pair<te::WaveAudioClip*, te::ClipPosition>> linked;
    for (auto* other : impl->linkedClips (*clip))
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (other))
        {
            const auto q = wave->getPosition();
            const auto from = q.getStart().inSeconds() + startShift, span = q.getLength().inSeconds() + endShift - startShift, skip = q.offset.inSeconds() + offsetShift;
            if (auto frozen = impl->unfrozen (wave->getClipTrack()); frozen.failed()) return frozen;
            if (! canEditAudioRange (*wave) || from < 0 || span < .001 || from + span > te::Edit::maximumLength || skip < 0
                || skip + span > wave->getSourceLength().inSeconds() + 1e-9 || ! validAudioSettings (audioSettings (*wave), span))
                return Result::fail ("\"" + wave->getName() + "\" on a linked track cannot be trimmed there.");
            linked.push_back ({ wave, { { te::TimePosition::fromSeconds (from), te::TimePosition::fromSeconds (from + span) }, te::TimeDuration::fromSeconds (skip) } });
        }
    impl->transaction ("Trim audio clip");
    clip->setPosition ({ { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (start+length) }, te::TimeDuration::fromSeconds (offset) });
    for (auto& [wave, position] : linked) wave->setPosition (position);
    impl->changed(); return Result::ok();
}
Result Session::splitAudioClip (const String& id, double at, String& rightId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before splitting clips.");
    impl->endAudition();
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    const auto p = clip->getPosition(); const auto s = audioSettings (*clip);
    const auto left = at-p.getStart().inSeconds(), right = p.getEnd().inSeconds()-at;
    if (! canEditAudioRange (*clip) || ! std::isfinite (at) || left < .002 || right < .002
        || left < s.fadeInSeconds || right < s.fadeOutSeconds)
        return Result::fail ("Split inside an unwarped arrangement clip, outside its fades, leaving at least 2 ms on each side.");
    const auto when = te::TimePosition::fromSeconds (at);
    const auto linked = impl->linkedTracks (*dynamic_cast<te::AudioTrack*> (clip->getClipTrack()));
    for (auto* other : linked) if (auto r = splittableAt (*other, when); r.failed()) return r;
    impl->transaction ("Split audio clip");
    auto* second = clip->getClipTrack()->splitClip (*clip, when);
    if (! second) return Result::fail ("The audio clip could not be split.");
    for (auto* other : linked) splitTrackAt (*other, when, &impl->edit->getUndoManager());
    rightId = second->itemID.toString(); impl->changed(); return Result::ok();
}
// Time edits across the whole arrangement. Every automation curve moves with the material.
namespace
{
std::vector<te::AutomationCurve*> arrangementCurves (te::Edit& edit)
{
    std::vector<te::AutomationCurve*> curves;
    // Devices inside racks carry their own curves.
    const std::function<void (te::Plugin&)> add = [&curves, &add] (te::Plugin& plugin)
    {
        for (auto* parameter : plugin.getAutomatableParameters()) if (parameter->getCurve().getNumPoints() > 0) curves.push_back (&parameter->getCurve());
        if (auto* native = dynamic_cast<NativeEffectPlugin*> (&plugin); native != nullptr && native->rackEngine() != nullptr)
            for (auto* inner : native->rackEngine()->devices()) add (*inner);
    };
    for (auto* track : te::getAllTracks (edit))
        if (dynamic_cast<te::AudioTrack*> (track) != nullptr || dynamic_cast<te::FolderTrack*> (track) != nullptr)
            for (auto* plugin : track->pluginList.getPlugins()) add (*plugin);
    if (auto master = edit.getMasterVolumePlugin()) add (*master);
    return curves;
}
// The curves of one track's devices and mixer that hold points, including devices inside racks.
std::vector<te::AutomationCurve*> trackCurves (te::AudioTrack& track)
{
    std::vector<te::AutomationCurve*> curves;
    const std::function<void (te::Plugin&)> add = [&curves, &add] (te::Plugin& plugin)
    {
        for (auto* parameter : plugin.getAutomatableParameters()) if (parameter->getCurve().getNumPoints() > 0) curves.push_back (&parameter->getCurve());
        if (auto* native = dynamic_cast<NativeEffectPlugin*> (&plugin); native != nullptr && native->rackEngine() != nullptr)
            for (auto* inner : native->rackEngine()->devices()) add (*inner);
    };
    for (auto* plugin : track.pluginList.getPlugins()) add (*plugin);
    return curves;
}
// A track's automatable parameters by a key naming the same control on another track: each device's
// type and name and its place among such devices, through racks, then the parameter.
std::vector<std::pair<juce::String, te::AutomatableParameter*>> keyedParameters (te::AudioTrack& track)
{
    std::vector<std::pair<juce::String, te::AutomatableParameter*>> result;
    const std::function<void (const std::vector<te::Plugin*>&, const juce::String&)> walk = [&result, &walk] (const std::vector<te::Plugin*>& plugins, const juce::String& prefix)
    {
        std::map<juce::String, int> seen;
        for (auto* plugin : plugins)
        {
            const auto kind = plugin->getPluginType() + ":" + plugin->getName();
            const auto key = prefix + kind + "#" + juce::String (seen[kind]++) + "/";
            for (auto* parameter : plugin->getAutomatableParameters()) result.push_back ({ key + parameter->paramID, parameter });
            if (auto* native = dynamic_cast<NativeEffectPlugin*> (plugin); native != nullptr && native->rackEngine() != nullptr)
            {
                std::vector<te::Plugin*> inner;
                for (auto* device : native->rackEngine()->devices()) inner.push_back (device);
                walk (inner, key);
            }
        }
    };
    std::vector<te::Plugin*> plugins;
    for (auto* plugin : track.pluginList.getPlugins()) plugins.push_back (plugin);
    walk (plugins, {});
    return result;
}
struct CurvePoint { te::TimePosition time; float value, shape; };
std::vector<CurvePoint> curvePoints (const te::AutomationCurve& curve)
{
    std::vector<CurvePoint> points;
    for (int i = 0; i < curve.getNumPoints(); ++i) points.push_back ({ curve.getPointTime (i), curve.getPointValue (i), curve.getPointCurve (i) });
    return points;
}
void rebuildCurve (te::AutomationCurve& curve, te::Edit& edit, std::vector<CurvePoint> points, juce::UndoManager* undo)
{
    std::stable_sort (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });
    curve.clear (undo);
    for (const auto& p : points) curve.addPoint (te::EditPosition (edit.tempoSequence.toBeats (p.time)), p.value, p.shape, undo);
}
// A hair before an edge, so a curve can change value there while its points stay strictly ordered.
const auto edgeGap = te::TimeDuration::fromSeconds (.001);
// Delete Selection clears a curve inside [from, to), keeping its values at both edges.
void clearCurveRange (te::AutomationCurve& curve, te::Edit& edit, te::TimePosition from, te::TimePosition to, juce::UndoManager* undo)
{
    const auto before = curve.getValueAt (from, 0.0f), after = curve.getValueAt (to, 0.0f);
    std::vector<CurvePoint> points;
    for (const auto& p : curvePoints (curve)) if (p.time < from || p.time > to) points.push_back (p);
    points.push_back ({ from, before, 0 }); points.push_back ({ to, after, 0 });
    rebuildCurve (curve, edit, points, undo);
}
// Duplicate Selection copies a curve's [from, to) shape to [to, to + length), restoring the original after it.
void duplicateCurveRange (te::AutomationCurve& curve, te::Edit& edit, te::TimePosition from, te::TimePosition to, juce::UndoManager* undo)
{
    const auto length = to - from;
    const auto after = to + length;
    const auto start = curve.getValueAt (from, 0.0f), end = curve.getValueAt (to - edgeGap, 0.0f), resume = curve.getValueAt (after, 0.0f);
    const auto original = curvePoints (curve);
    std::vector<CurvePoint> points;
    for (const auto& p : original) if (p.time < to - edgeGap || p.time > after) points.push_back (p);
    points.push_back ({ to - edgeGap, end, 0 }); points.push_back ({ to, start, 0 });
    for (const auto& p : original) if (p.time > from && p.time < to - edgeGap) points.push_back ({ p.time + length, p.value, p.shape });
    points.push_back ({ after - edgeGap, end, 0 }); points.push_back ({ after, resume, 0 });
    rebuildCurve (curve, edit, points, undo);
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
    impl->endAudition();
    auto* clip = impl->midiClip (id); if (! clip) return Result::fail ("MIDI clip not found.");
    if (auto frozen = impl->unfrozen (clip->getClipTrack()); frozen.failed()) return frozen;
    if (! std::isfinite (at) || ! canSplitMidiAt (*clip, te::TimePosition::fromSeconds (at)))
        return Result::fail ("Split inside an unlooped Arrangement MIDI clip, away from its ends.");
    const auto when = te::TimePosition::fromSeconds (at);
    const auto linked = impl->linkedTracks (*dynamic_cast<te::AudioTrack*> (clip->getClipTrack()));
    for (auto* other : linked) if (auto r = splittableAt (*other, when); r.failed()) return r;
    impl->transaction ("Split MIDI clip");
    auto* right = splitMidiClipAt (*clip, when, &impl->edit->getUndoManager());
    if (! right) return Result::fail ("The MIDI clip could not be split.");
    for (auto* other : linked) splitTrackAt (*other, when, &impl->edit->getUndoManager());
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
    if (auto r = rangeTracks (*impl->edit, impl->withLinkedTracks (trackIds), start, end, tracks); r.failed()) return r;
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
    if (auto r = rangeTracks (*impl->edit, impl->withLinkedTracks (trackIds), start, end, tracks); r.failed()) return r;
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
    for (auto* t : tracks) for (auto* curve : trackCurves (*t)) clearCurveRange (*curve, *impl->edit, from, to, &impl->edit->getUndoManager());
    for (auto* t : tracks)
    {
        for (const auto at : { from, to }) splitTrackAt (*t, at, &impl->edit->getUndoManager());
        const auto clips = t->getClips();
        for (auto* c : clips)
            if (c->getPosition().getStart() >= from - te::TimeDuration::fromSeconds (1e-6) && c->getPosition().getEnd() <= to + te::TimeDuration::fromSeconds (1e-6)) c->removeFromParent();
    }
    impl->refreshParameterValues();
    impl->changed(); return Result::ok();
}
Result Session::duplicateRange (const juce::StringArray& trackIds, double start, double end)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before duplicating clips.");
    juce::Array<te::AudioTrack*> tracks;
    if (auto r = rangeTracks (*impl->edit, impl->withLinkedTracks (trackIds), start, end, tracks); r.failed()) return r;
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
    for (auto* t : tracks) for (auto* curve : trackCurves (*t)) duplicateCurveRange (*curve, *impl->edit, from, to, undo);
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
    impl->refreshParameterValues();
    impl->changed(); return Result::ok();
}
Result Session::copyRange (const juce::StringArray& trackIds, double start, double end, bool cut)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (cut && impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before cutting clips.");
    juce::Array<te::AudioTrack*> tracks;
    if (auto r = rangeTracks (*impl->edit, impl->withLinkedTracks (trackIds), start, end, tracks); r.failed()) return r;
    // In the order the tracks are shown, as they paste.
    const auto order = te::getAudioTracks (*impl->edit);
    std::sort (tracks.begin(), tracks.end(), [&] (auto* a, auto* b) { return order.indexOf (a) < order.indexOf (b); });
    const auto from = te::TimePosition::fromSeconds (start), to = te::TimePosition::fromSeconds (end);
    for (auto* t : tracks) for (const auto at : { from, to }) if (auto r = splittableAt (*t, at); r.failed()) return r;
    auto clipboard = std::make_unique<Impl::RangeClipboard>();
    clipboard->start = start; clipboard->length = end - start;
    for (auto* t : tracks)
    {
        Impl::RangeClipboard::Track copied;
        copied.midi = static_cast<bool> (t->state.getProperty (midiTrackProperty));
        for (auto* c : t->getClips())
        {
            if (! (c->getPosition().getStart() < to && c->getPosition().getEnd() > from)) continue;
            auto state = c->state.createCopy();
            if (auto* wave = dynamic_cast<te::WaveAudioClip*> (c))
            {
                if (wave->getNumTakes (true) > 1 || wave->isCurrentTakeComp()) return Result::fail ("\"" + c->getName() + "\" has comped takes; consolidate it before copying.");
                const auto source = wave->getSourceFileReference().getFile();
                if (! source.existsAsFile()) return Result::fail ("The audio of \"" + c->getName() + "\" is missing.");
                auto& kept = clipboard->media[source.getFullPathName()];
                if (kept == nullptr)
                {
                    kept = std::make_unique<juce::TemporaryFile> (source.getFileExtension());
                    if (! source.copyFileTo (kept->getFile())) return Result::fail ("Could not keep the selection's audio for the clipboard.");
                }
                state.removeChild (state.getChildWithName (te::IDs::TAKES), nullptr);
                state.setProperty (te::IDs::source, kept->getFile().getFullPathName(), nullptr);
            }
            copied.clips.push_back (state);
        }
        for (const auto& [key, parameter] : keyedParameters (*t))
        {
            const auto& curve = parameter->getCurve();
            if (curve.getNumPoints() == 0) continue;
            Impl::RangeClipboard::Curve shape { key, {}, curve.getValueAt (from, 0.0f), curve.getValueAt (to - edgeGap, 0.0f) };
            for (const auto& p : curvePoints (curve)) if (p.time > from && p.time < to - edgeGap) shape.points.push_back ({ (p.time - from).inSeconds(), p.value, p.shape });
            copied.curves.push_back (std::move (shape));
        }
        clipboard->tracks.push_back (std::move (copied));
    }
    if (cut) if (auto r = deleteRange (trackIds, start, end); r.failed()) return r;
    impl->midiClipboard = {}; impl->midiClipboardBeats = 0; impl->audioClipboard = {}; impl->audioClipboardMedia.reset(); impl->slotClipboard = {}; impl->slotClipboardMedia.reset();
    impl->rangeClipboard = std::move (clipboard);
    impl->changed(); return Result::ok();
}
bool Session::hasRangeClipboard() const { return impl->rangeClipboard != nullptr; }
double Session::rangeClipboardLength() const { return impl->rangeClipboard != nullptr ? impl->rangeClipboard->length : 0; }
int Session::rangeClipboardTracks() const { return impl->rangeClipboard != nullptr ? (int) impl->rangeClipboard->tracks.size() : 0; }
Result Session::pasteRange (const String& firstTrackId, double start)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before pasting clips.");
    if (impl->rangeClipboard == nullptr) return Result::fail ("Copy a time selection before pasting it.");
    const auto& clipboard = *impl->rangeClipboard;
    if (! std::isfinite (start) || start < 0 || start + clipboard.length > te::Edit::maximumLength) return Result::fail ("Paste position is outside the supported timeline.");
    // One destination for each copied track: the first track onward, skipping returns.
    std::vector<te::AudioTrack*> destinations;
    bool found = false;
    for (auto* t : te::getAudioTracks (*impl->edit))
    {
        found = found || t->itemID.toString() == firstTrackId;
        if (found && ! isReturn (*t) && destinations.size() < clipboard.tracks.size()) destinations.push_back (t);
    }
    if (! found) return Result::fail ("Select a track to paste onto.");
    if (destinations.size() < clipboard.tracks.size()) return Result::fail ("The selection was copied from " + String ((int) clipboard.tracks.size()) + " tracks; there are not enough tracks from here to paste it.");
    for (size_t i = 0; i < destinations.size(); ++i)
        if (static_cast<bool> (destinations[i]->state.getProperty (midiTrackProperty)) != clipboard.tracks[i].midi)
            return Result::fail ("Paste onto tracks of the same kinds as were copied, in order: audio onto audio and MIDI onto MIDI.");
    const auto from = te::TimePosition::fromSeconds (start), to = te::TimePosition::fromSeconds (start + clipboard.length);
    for (auto* t : destinations)
    {
        if (auto r = impl->unfrozen (t); r.failed()) return r;
        for (const auto at : { from, to }) if (auto r = splittableAt (*t, at); r.failed()) return r;
    }
    // The copied audio joins the project's own media.
    const auto assets = impl->assets();
    if (! clipboard.media.empty()) if (auto r = assets.createDirectory(); r.failed()) return r;
    std::map<juce::String, juce::File> collected;
    for (const auto& [original, kept] : clipboard.media)
    {
        const auto target = assets.getNonexistentChildFile (juce::File (original).getFileNameWithoutExtension(), kept->getFile().getFileExtension(), true);
        if (! kept->getFile().copyFileTo (target)) { for (auto& [k, f] : collected) f.deleteFile(); return Result::fail ("Could not collect the clipboard's audio into this project."); }
        collected[kept->getFile().getFullPathName()] = target;
    }
    endEditGesture(); closePluginEditors();
    impl->transaction ("Paste selection");
    auto* undo = &impl->edit->getUndoManager();
    const auto shift = te::TimeDuration::fromSeconds (start - clipboard.start), epsilon = te::TimeDuration::fromSeconds (1e-6);
    // Splits a clip there, keeping the part on the chosen side; a clip wholly on the other side goes.
    const auto keep = [&] (te::AudioTrack& track, te::Clip* clip, te::TimePosition at, bool right) -> te::Clip*
    {
        const auto p = clip->getPosition();
        if (p.getStart() >= at - epsilon || p.getEnd() <= at + epsilon)
        {
            if (right ? p.getEnd() > at + epsilon : p.getStart() < at - epsilon) return clip;
            clip->removeFromParent(); return nullptr;
        }
        te::Clip* after = nullptr;
        if (auto* midi = dynamic_cast<te::MidiClip*> (clip)) after = splitMidiClipAt (*midi, at, undo);
        else after = track.splitClip (*clip, at);
        if (after == nullptr) return clip;
        if (right) { clip->removeFromParent(); return after; }
        after->removeFromParent(); return clip;
    };
    for (size_t i = 0; i < destinations.size(); ++i)
    {
        auto& t = *destinations[i];
        // Each copied control's shape replaces the span on the same control here, held up to it and
        // resuming after it.
        if (! clipboard.tracks[i].curves.empty())
        {
            const auto parameters = keyedParameters (t);
            for (const auto& shape : clipboard.tracks[i].curves)
            {
                const auto found = std::find_if (parameters.begin(), parameters.end(), [&] (const auto& p) { return p.first == shape.key; });
                if (found == parameters.end()) continue;
                auto* parameter = found->second;
                auto& curve = parameter->getCurve();
                const auto valueAt = [&] (te::TimePosition at) { return curve.getNumPoints() > 0 ? curve.getValueAt (at, 0.0f) : parameter->getCurrentValue(); };
                const auto before = valueAt (from - edgeGap), resume = valueAt (to);
                std::vector<CurvePoint> points;
                for (const auto& p : curvePoints (curve)) if (p.time < from - edgeGap || p.time > to) points.push_back (p);
                if (from.inSeconds() >= edgeGap.inSeconds()) points.push_back ({ from - edgeGap, before, 0 });
                points.push_back ({ from, shape.startValue, 0 });
                for (const auto& p : shape.points) points.push_back ({ from + te::TimeDuration::fromSeconds (p.seconds), p.value, p.shape });
                points.push_back ({ to - edgeGap, shape.endValue, 0 }); points.push_back ({ to, resume, 0 });
                rebuildCurve (curve, *impl->edit, points, undo);
            }
        }
        for (const auto at : { from, to }) splitTrackAt (t, at, undo);
        const auto existing = t.getClips();
        for (auto* c : existing)
            if (c->getPosition().getStart() >= from - epsilon && c->getPosition().getEnd() <= to + epsilon) c->removeFromParent();
        for (const auto& state : clipboard.tracks[i].clips)
        {
            auto copy = te::ClipCopy::fromClipboardState (state, false).withNewItemID (*impl->edit);
            auto pasted = copy.getState();
            pasted.setProperty (te::IDs::start, (te::TimePosition::fromSeconds (static_cast<double> (state[te::IDs::start])) + shift).inSeconds(), nullptr);
            if (const auto media = collected.find (state[te::IDs::source].toString()); media != collected.end())
                pasted.setProperty (te::IDs::source, media->second.getFullPathName(), nullptr);
            renewMidiNoteIDs (pasted);
            auto* clip = te::insertClipCopy (t, copy);
            if (clip == nullptr) continue;
            if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip)) wave->getSourceFileReference().setToFile (juce::File (pasted[te::IDs::source].toString()), te::SourceFileReference::PathStyle::alwaysAbsolute, false);
            // Only what lies in the range is pasted.
            if (clip = keep (t, clip, from, true); clip != nullptr) keep (t, clip, to, false);
        }
    }
    impl->refreshParameterValues();
    impl->changed(); return Result::ok();
}
Result Session::consolidateRange (const juce::StringArray& trackIds, double start, double end, juce::StringArray& newClipIds)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before consolidating clips.");
    juce::Array<te::AudioTrack*> tracks;
    if (auto r = rangeTracks (*impl->edit, impl->withLinkedTracks (trackIds), start, end, tracks); r.failed()) return r;
    const auto from = te::TimePosition::fromSeconds (start), to = te::TimePosition::fromSeconds (end);
    const te::TimeRange range (from, to);
    juce::Array<te::AudioTrack*> work;
    for (auto* t : tracks)
    {
        bool any = false;
        for (auto* c : t->getClips()) any = any || c->getPosition().time.overlaps (range);
        if (! any) continue;
        if (auto r = impl->unfrozen (t); r.failed()) return r;
        for (const auto at : { from, to }) if (auto r = splittableAt (*t, at); r.failed()) return r;
        work.add (t);
    }
    if (work.isEmpty()) return Result::fail ("There are no clips in the selection to consolidate.");
    endEditGesture(); closePluginEditors();
    // Audio tracks render first, before anything changes, so a failed render leaves the project as it was.
    std::map<te::AudioTrack*, File> renders;
    {
        const juce::ScopedValueSetter<bool> exporting (impl->exporting, true);
        std::vector<std::pair<juce::ValueTree, std::pair<juce::var, juce::var>>> saved;
        for (auto* track : te::getAllTracks (*impl->edit))
            if (dynamic_cast<te::AudioTrack*> (track) != nullptr || dynamic_cast<te::FolderTrack*> (track) != nullptr)
                saved.push_back ({ track->state, { track->state[te::IDs::solo], track->state[te::IDs::mute] } });
        const bool wasDirty = impl->edit->hasChangedSinceSaved();
        const auto put = [] (juce::ValueTree& state, const juce::Identifier& id, const juce::var& value)
        { if (value.isVoid()) state.removeProperty (id, nullptr); else state.setProperty (id, value, nullptr); };
        const auto restore = [&]
        {
            for (auto& [state, values] : saved) { put (state, te::IDs::solo, values.first); put (state, te::IDs::mute, values.second); }
            impl->refreshRouting();
            if (! wasDirty) impl->edit->resetChangedStatus();
        };
        auto assets = impl->assets();
        if (auto r = assets.createDirectory(); r.failed()) return r;
        const auto rate = impl->engine.getDeviceManager().getSampleRate() > 0 ? impl->engine.getDeviceManager().getSampleRate() : 48000.0;
        for (auto* t : work)
        {
            if (static_cast<bool> (t->state.getProperty (midiTrackProperty, false))) continue;
            // Only this track's clips, heard alone and unmuted, without its devices or mixer.
            for (auto& [state, values] : saved) { state.setProperty (te::IDs::solo, state == t->state, nullptr); if (state == t->state) state.setProperty (te::IDs::mute, false, nullptr); }
            impl->refreshRouting();
            impl->edit->dispatchPendingUpdatesSynchronously();
            const te::Edit::ScopedRenderStatus renderStatus (*impl->edit, true);
            const auto target = assets.getNonexistentChildFile (File::createLegalFileName (t->getName() + " Consolidated"), ".wav", true);
            te::Renderer::Parameters p (*impl->edit);
            p.destFile = target; p.audioFormat = impl->engine.getAudioFileFormatManager().getWavFormat();
            p.bitDepth = 32; p.sampleRateForAudio = rate; p.blockSizeForAudio = 512;
            p.time = range; p.tracksToDo = te::toBitSet (juce::Array<te::Track*> { t });
            for (auto* c : t->getClips()) if (c->getPosition().time.overlaps (range)) p.allowedClips.add (c);
            p.usePlugins = false; p.useMasterPlugins = false; p.canRenderInMono = false; p.checkNodesForAudio = false; p.ditheringEnabled = false;
            const auto rendered = te::Renderer::renderToFile ("Consolidate " + t->getName(), p);
            if (! rendered.existsAsFile()) { restore(); return Result::fail ("\"" + t->getName() + "\" could not be consolidated."); }
            renders[t] = rendered;
        }
        restore();
    }
    impl->transaction ("Consolidate");
    auto* undo = &impl->edit->getUndoManager();
    newClipIds.clear();
    for (auto* t : work)
    {
        for (const auto at : { from, to }) splitTrackAt (*t, at, undo);
        std::vector<te::Clip::Ptr> inside;
        for (auto* c : t->getClips())
            if (c->getPosition().getStart() >= from - te::TimeDuration::fromSeconds (1e-6) && c->getPosition().getEnd() <= to + te::TimeDuration::fromSeconds (1e-6)) inside.emplace_back (c);
        if (auto found = renders.find (t); found != renders.end())
        {
            for (auto& c : inside) c->removeFromParent();
            auto clip = te::insertWaveClip (*t, t->getName() + " Consolidated", found->second, { range, {} }, te::DeleteExistingClips::no);
            if (! clip) return Result::fail ("The consolidated clip could not be created.");
            clip->setAutoTempo (false); clip->setAutoPitch (false);
            clip->setFadeIn (te::TimeDuration()); clip->setFadeOut (te::TimeDuration());
            newClipIds.add (clip->itemID.toString());
            continue;
        }
        // MIDI: one clip over the selection holding every note and controller point, in time order.
        auto joined = t->insertMIDIClip ("MIDI clip", range, nullptr);
        if (joined == nullptr) return Result::fail ("The consolidated MIDI clip could not be created.");
        joined->setSyncType (te::Clip::syncBarsBeats);
        auto& sequence = joined->getSequence();
        const auto origin = impl->edit->tempoSequence.toBeats (from);
        for (auto& c : inside)
            if (auto* midi = dynamic_cast<te::MidiClip*> (c.get()))
            {
                const auto shift = midi->getStartBeat() - origin;
                for (auto* note : midi->getSequence().getNotes())
                {
                    auto* added = sequence.addNote (note->getNoteNumber(), note->getStartBeat() + shift, note->getLengthBeats(), note->getVelocity(), note->getColour(), undo);
                    if (added != nullptr) { added->setMute (note->isMute(), undo); added->state.setProperty (midiNoteIDProperty, juce::Uuid().toString(), undo); }
                }
                for (auto* event : midi->getSequence().getControllerEvents())
                    sequence.addControllerEvent (event->getBeatPosition() + shift, event->getType(), event->getControllerValue(), event->getMetadata(), undo);
                c->removeFromParent();
            }
        newClipIds.add (joined->itemID.toString());
    }
    impl->changed(); return Result::ok();
}
Result Session::insertTime (double start, double length)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->tempoSequence.getNumTempos() > 1) return Result::fail ("Insert Silence needs a constant tempo; clear the Song Tempo lane first.");
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
    if (impl->edit->tempoSequence.getNumTempos() > 1) return Result::fail ("Delete Time needs a constant tempo; clear the Song Tempo lane first.");
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
    if (!clip || !canCarryMidiClip(*clip)) return Result::fail("Copy a single-take, ungrouped Arrangement MIDI clip.");
    if(cut && clip->getClipTrack()->isFrozen(te::Track::anyFreeze)) return Result::fail("Unfreeze the source track before cutting clips.");
    const auto beats=clip->getLengthInBeats().inBeats();
    if(!std::isfinite(beats)||beats<=0)return Result::fail("The MIDI clip has an invalid beat duration.");
    auto copied=clip->state.createCopy();
    // Read before a cut removes, and so deletes, the clip.
    const auto offsetBeats=clip->getOffsetInBeats().inBeats();
    if(cut){impl->transaction("Cut MIDI clip");clip->removeFromParent();impl->changed();}
    impl->midiClipboard=std::move(copied);impl->midiClipboardBeats=beats;impl->midiClipboardOffsetBeats=offsetBeats;
    impl->rangeClipboard.reset(); impl->slotClipboard = {}; impl->slotClipboardMedia.reset();
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
    // The content start keeps its beat at the paste's tempo.
    const auto offset=impl->midiClipboardOffsetBeats/impl->edit->tempoSequence.getBeatsPerSecondAt(te::TimePosition::fromSeconds(start));
    state.setProperty(te::IDs::offset,offset,nullptr);state.setProperty(te::IDs::sync,te::Clip::syncBarsBeats,nullptr);renewMidiNoteIDs(state);
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
    if(!clip||!canCarryMidiClip(*clip)||clip->getClipTrack()->isFrozen(te::Track::anyFreeze))
        return Result::fail("Duplicate a single-take, ungrouped Arrangement MIDI clip on an unfrozen track.");
    const auto start=clip->getPosition().getEnd();
    const auto end=impl->edit->tempoSequence.toTime(clip->getEndBeat()+clip->getLengthInBeats());
    if(end.inSeconds()>te::Edit::maximumLength)return Result::fail("The duplicate would exceed the 48-hour timeline.");
    auto copy=te::ClipCopy::fromClipboardState(clip->state,false).withNewItemID(*impl->edit);auto state=copy.getState();
    state.setProperty(te::IDs::start,start.inSeconds(),nullptr);state.setProperty(te::IDs::length,(end-start).inSeconds(),nullptr);renewMidiNoteIDs(state);
    state.setProperty(te::IDs::offset,clip->getOffsetInBeats().inBeats()/impl->edit->tempoSequence.getBeatsPerSecondAt(start),nullptr);
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
    impl->midiClipboard = {}; impl->midiClipboardBeats = 0; impl->rangeClipboard.reset(); impl->slotClipboard = {}; impl->slotClipboardMedia.reset();
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
    impl->endAudition();
    auto* clip = impl->midiClip (id); auto* destination = impl->track (destinationId);
    if (! clip || clip->getClipSlot() || clip->isGrouped()) return Result::fail ("Move an ungrouped Arrangement MIDI clip.");
    if (! destination || isReturn (*destination) || ! static_cast<bool> (destination->state.getProperty (midiTrackProperty)))
        return Result::fail ("Move MIDI onto a MIDI track.");
    if (destination->isFrozen (te::Track::anyFreeze) || clip->getClipTrack()->isFrozen (te::Track::anyFreeze))
        return Result::fail ("Unfreeze the source and destination tracks before moving MIDI.");
    if (! std::isfinite (start) || start < 0 || start + clip->getPosition().getLength().inSeconds() > te::Edit::maximumLength)
        return Result::fail ("The complete clip must fit inside the 48-hour timeline.");
    if (clip->getClipTrack() == destination && clip->getPosition().getStart().inSeconds() == start) return Result::ok();
    // Moving in time on a linked track moves the linked clips too.
    const auto offset = start - clip->getPosition().getStart().inSeconds();
    std::vector<te::Clip*> linked;
    if (clip->getClipTrack() == destination) if (auto r = impl->linkedMoves (*clip, offset, linked); r.failed()) return r;
    const te::Clip::Ptr retained (clip);
    impl->transaction ("Move MIDI clip");
    if (clip->getClipTrack() != destination && ! clip->moveTo (*destination)) return Result::fail ("Could not move the MIDI clip to that track.");
    clip->setStart (te::TimePosition::fromSeconds (start), false, true);
    Impl::moveLinked (linked, offset);
    impl->changed(); return Result::ok();
}
Result Session::moveAudioClip (const String& id, const String& destinationId, double start)
{
    if (auto frozen = impl->unfrozen (impl->trackOfClip (id), impl->track (destinationId)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before moving audio clips.");
    impl->endAudition();
    auto* clip = impl->audioClip (id); auto* destination = impl->track (destinationId);
    if (! clip || clip->getClipSlot() || clip->isGrouped()) return Result::fail ("Move an ungrouped Arrangement audio clip.");
    if (! destination || isReturn (*destination) || static_cast<bool> (destination->state.getProperty (midiTrackProperty)))
        return Result::fail ("Move audio onto an audio track.");
    if (destination->isFrozen (te::Track::anyFreeze) || clip->getClipTrack()->isFrozen (te::Track::anyFreeze))
        return Result::fail ("Unfreeze the source and destination tracks before moving audio.");
    if (! std::isfinite (start) || start < 0 || start + clip->getPosition().getLength().inSeconds() > te::Edit::maximumLength)
        return Result::fail ("The complete clip must fit inside the 48-hour timeline.");
    if (clip->getClipTrack() == destination && clip->getPosition().getStart().inSeconds() == start) return Result::ok();
    // Moving in time on a linked track moves the linked clips too.
    const auto offset = start - clip->getPosition().getStart().inSeconds();
    std::vector<te::Clip*> linked;
    if (clip->getClipTrack() == destination) if (auto r = impl->linkedMoves (*clip, offset, linked); r.failed()) return r;
    const te::Clip::Ptr retained (clip);
    impl->transaction ("Move audio clip");
    if (clip->getClipTrack() != destination && ! clip->moveTo (*destination)) return Result::fail ("Could not move the audio clip to that track.");
    clip->setStart (te::TimePosition::fromSeconds (start), false, true);
    Impl::moveLinked (linked, offset);
    impl->changed(); return Result::ok();
}
Result Session::moveClip (int t, int c, double start)
{
    if (auto frozen = impl->unfrozen (impl->track (t)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->endAudition();
    auto* clip = impl->clip (t, c); if (! clip) return Result::fail ("Clip not found.");
    if (! std::isfinite (start) || start < 0 || start + clip->getPosition().getLength().inSeconds() > te::Edit::maximumLength)
        return Result::fail ("The complete clip must fit inside the 48-hour timeline.");
    const auto offset = start - clip->getPosition().getStart().inSeconds();
    std::vector<te::Clip*> linked;
    if (auto r = impl->linkedMoves (*clip, offset, linked); r.failed()) return r;
    impl->transaction ("Move clip"); clip->setStart (te::TimePosition::fromSeconds (start), false, true); Impl::moveLinked (linked, offset);
    impl->changed(); return Result::ok();
}
Result Session::removeClip (int t, int c)
{
    if (auto frozen = impl->unfrozen (impl->track (t)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->endAudition();
    auto* clip = impl->clip (t, c); if (! clip) return Result::fail ("Clip not found.");
    const auto linked = impl->linkedClips (*clip);
    for (auto* other : linked) if (auto frozen = impl->unfrozen (other->getClipTrack()); frozen.failed()) return frozen;
    impl->transaction ("Remove clip"); clip->removeFromParent();
    for (auto* other : linked) other->removeFromParent();
    impl->changed(); return Result::ok();
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
    impl->endAudition();
    te::Clip* found=nullptr;
    for (auto* track:te::getAudioTracks(*impl->edit)) for (auto* clip:allTrackClips(*track))
        if (clip->itemID.toString()==id && (dynamic_cast<te::WaveAudioClip*>(clip) || dynamic_cast<te::MidiClip*>(clip)))
        {
            if (found) return Result::fail("Clip identity is ambiguous.");
            found=clip;
        }
    if (!found) return Result::fail("Clip not found.");
    std::vector<te::Clip*> changing;
    if (found->isMuted()!=!active) changing.push_back(found);
    for (auto* other:impl->linkedClips(*found)) if (other->isMuted()!=!active) changing.push_back(other);
    if (changing.empty()) return Result::ok();
    for (auto* clip:changing) if(clip->getClipTrack()->isFrozen(te::Track::anyFreeze)) return Result::fail("Unfreeze the track before changing clip activation.");
    impl->transaction(active?"Activate clip":"Deactivate clip");for (auto* clip:changing) clip->setMuted(!active);
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
    if (impl->capturing()) { impl->capture ("master", value); impl->changed(); return Result::ok(); }
    const bool automated = master->volParam->hasAutomationPoints() && ! master->volParam->getCurve().bypass.get();
    if(! automated && master->getSliderPos()==te::decibelsToVolumeFaderPosition(value)) return Result::ok();
    if (! master->state.hasProperty (manualVolumeProperty))
        master->state.setProperty (manualVolumeProperty, master->volParam->getCurrentExplicitValue(), nullptr);
    impl->transaction("Master volume"); impl->overrideLane ("master"); master->setVolumeDb(value);
    master->state.setProperty (manualVolumeProperty, te::decibelsToVolumeFaderPosition (value), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setTrackGain (int index, float value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (! std::isfinite (value)) return Result::fail ("Invalid gain.");
    auto* volume = t->getVolumePlugin();
    value = juce::jlimit (-100.0f, 6.0f, value);
    const auto key = Impl::mixerKey (*t, AutomationTarget::volume);
    if (impl->capturing()) { impl->capture (key, value); impl->changed(); return Result::ok(); }
    if (! volume->state.hasProperty (manualVolumeProperty))
        volume->state.setProperty (manualVolumeProperty, volume->volParam->getCurrentExplicitValue(), nullptr);
    impl->transaction ("Track gain"); impl->overrideLane (key); volume->setVolumeDb (value);
    volume->state.setProperty (manualVolumeProperty, te::decibelsToVolumeFaderPosition (value), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setTrackPan (int index, float value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (! std::isfinite (value)) return Result::fail ("Invalid pan.");
    auto* volume = t->getVolumePlugin();
    value = juce::jlimit (-1.0f, 1.0f, value);
    const auto key = Impl::mixerKey (*t, AutomationTarget::pan);
    if (impl->capturing()) { impl->capture (key, value); impl->changed(); return Result::ok(); }
    if (! volume->state.hasProperty (manualPanProperty))
        volume->state.setProperty (manualPanProperty, volume->panParam->getCurrentExplicitValue(), nullptr);
    impl->transaction ("Track pan"); impl->overrideLane (key); volume->setPan (value);
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
Result Session::setClipEnvelope (const String& clipId, const ClipEnvelope& envelope)
{
    if (impl->exporting || impl->anyRecording()) return Result::fail ("Stop recording or finish exporting before editing clip envelopes.");
    te::Clip* clip = nullptr;
    for (auto* t : te::getAudioTracks (*impl->edit)) for (auto* c : allTrackClips (*t)) if (c->itemID.toString() == clipId) clip = c;
    auto* track = clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr;
    if (track == nullptr || (dynamic_cast<te::WaveAudioClip*> (clip) == nullptr && dynamic_cast<te::MidiClip*> (clip) == nullptr)) return Result::fail ("Clip not found.");
    if (auto frozen = impl->unfrozen (track); frozen.failed()) return frozen;
    if (envelope.mode == EnvelopeMode::automation && clip->getClipSlot() == nullptr)
        return Result::fail ("Arrangement clips modulate controls; automation envelopes belong to Session clips.");
    te::Plugin* device = nullptr;
    auto* parameter = envelopeParameter (*track, envelope.target, &device);
    if (parameter == nullptr) return Result::fail ("That control is not on the clip's track.");
    const auto [low, high] = envelopeRange (envelope.target, envelope.mode, device);
    if (envelope.points.size() > 4096) return Result::fail ("An envelope can contain at most 4096 points.");
    double previous = -1;
    for (const auto& point : envelope.points)
    {
        if (! std::isfinite (point.beat) || point.beat < 0 || point.beat > 1000000 || point.beat <= previous || (previous >= 0 && point.beat - previous < 0.999e-6)
            || ! std::isfinite (point.value) || point.value < low || point.value > high)
            return Result::fail ("An envelope needs ordered beat points from " + String (low) + " to " + String (high) + ".");
        previous = point.beat;
    }
    if (! std::isfinite (envelope.loopBeats) || envelope.loopBeats < 1 || envelope.loopBeats > 1024) return Result::fail ("An envelope loop is 1 to 1024 beats.");
    const te::AutomatableParameterID id { parameter->automatableEditElement.itemID, parameter->paramID };
    const auto existing = [&] { for (const auto& e : clipEnvelopes (*clip)) if (e.target == envelope.target && e.mode == envelope.mode) return std::optional (e); return std::optional<ClipEnvelope>(); }();
    if (existing ? *existing == envelope : envelope.points.empty()) return Result::ok();
    impl->transaction ("Edit clip envelope");
    auto* undo = &impl->edit->getUndoManager();
    auto* list = clip->getAutomationCurveList (true);
    te::AutomationCurveModifier::Ptr modifier;
    for (auto item : list->getItems()) if (item->getDestID() == id) modifier = item;
    if (modifier == nullptr) modifier = list->addCurve (*parameter);
    const auto type = envelopeCurveType (envelope.target, envelope.mode);
    auto& curve = modifier->getCurve (type).curve;
    curve.clear (undo);
    for (const auto& point : envelope.points)
        curve.addPoint (te::BeatPosition::fromBeats (point.beat), envelopeToCurve (envelope.target, envelope.mode, device, point.value), 0, undo);
    auto& timing = modifier->getCurveTiming (type);
    timing.unlinked = envelope.unlinked;
    timing.start = te::BeatPosition(); timing.loopStart = te::BeatPosition();
    timing.looping = true; timing.loopLength = te::BeatDuration::fromBeats (envelope.loopBeats);
    if (! te::hasAnyPoints (*modifier)) modifier->remove();
    // The mixer keeps its manual level as with track automation, since the engine writes the
    // modulated level into the fader's stored value while the clip plays.
    if (auto* mixer = dynamic_cast<te::VolumeAndPanPlugin*> (&parameter->automatableEditElement))
    {
        const auto& property = manualAutomationProperty (envelope.target.parameter == 0 ? AutomationTarget::volume : AutomationTarget::pan);
        if (! mixer->state.hasProperty (property)) mixer->state.setProperty (property, parameter->getCurrentExplicitValue(), nullptr);
    }
    parameter->updateStream();
    if (auto* native = dynamic_cast<NativeEffectPlugin*> (device)) native->refreshAutomation();
    impl->edit->dispatchPendingUpdatesSynchronously();
    impl->changed(); return Result::ok();
}
Result Session::setDeviceAutomation (const String& pluginId, int parameter, const AutomationLane& lane)
{
    te::AudioTrack* owner = nullptr;
    auto* device = impl->nativeDevice (pluginId, &owner);
    if (auto frozen = impl->unfrozen (owner); frozen.failed()) return frozen;
    if (impl->exporting || (impl->edit->getTransport().isRecording() && ! impl->internalAutomationWrite))
        return Result::fail ("Stop recording or finish exporting before editing automation.");
    if (device == nullptr)
        if (auto* external = impl->externalPlugin (pluginId, &owner))
        {
            if (auto frozen = impl->unfrozen (owner); frozen.failed()) return frozen;
            return impl->setExternalAutomation (*external, parameter, lane);
        }
    if (device == nullptr) return Result::fail ("Device not found.");
    auto* control = device->control (parameter);
    if (control == nullptr) return Result::fail ("That device has no such control.");
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
    if (! impl->internalAutomationWrite) impl->transaction ("Edit " + String (field.label) + " automation");
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
std::vector<Session::PluginParameterInfo> Session::pluginParameters (const String& pluginId) const
{
    std::vector<PluginParameterInfo> result;
    if (auto* external = impl->externalPlugin (pluginId); external != nullptr && external->getAudioPluginInstance() != nullptr)
        for (auto* p : externalParameters (*external))
            result.push_back ({ p->getParameterName(), p->getCurrentValueAsString(), p->getCurrentNormalisedValue() });
    return result;
}
Result Session::setPluginConfiguration (const String& pluginId, const std::vector<int>& chosen)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    te::AudioTrack* owner = nullptr;
    auto* external = impl->externalPlugin (pluginId, &owner);
    if (external == nullptr || external->getAudioPluginInstance() == nullptr) return Result::fail ("Configure a loaded plug-in.");
    if (auto frozen = impl->unfrozen (owner); frozen.failed()) return frozen;
    const auto parameters = externalParameters (*external);
    if ((int) chosen.size() > maximumConfiguredParameters) return Result::fail ("A plug-in shows at most 128 configured parameters.");
    juce::StringArray ids;
    for (const auto index : chosen)
    {
        if (! juce::isPositiveAndBelow (index, (int) parameters.size())) return Result::fail ("That plug-in has no such parameter.");
        if (ids.contains (parameters[(size_t) index]->paramID)) return Result::fail ("Each parameter can be configured once.");
        ids.add (parameters[(size_t) index]->paramID);
    }
    juce::StringArray current;
    for (const auto& configured : external->state.getChildWithName (configureType)) current.add (configured[te::IDs::id].toString());
    if (current == ids) return Result::ok();
    impl->transaction ("Configure plug-in");
    auto* undo = &impl->edit->getUndoManager();
    if (auto previous = external->state.getChildWithName (configureType); previous.isValid()) external->state.removeChild (previous, undo);
    if (! ids.isEmpty())
    {
        juce::ValueTree configure (configureType);
        for (const auto& id : ids) { juce::ValueTree parameter (configuredType); parameter.setProperty (te::IDs::id, id, nullptr); configure.appendChild (parameter, nullptr); }
        external->state.appendChild (configure, undo);
    }
    impl->changed(); return Result::ok();
}
juce::StringArray Session::sidechainSources (const String& pluginId) const
{
    juce::StringArray result;
    te::AudioTrack* owner = nullptr;
    auto* external = impl->externalPlugin (pluginId, &owner);
    if (external == nullptr || owner == nullptr) return result;
    for (auto* t : te::getAudioTracks (*impl->edit))
        if (t != owner && ! isReturn (*t) && ! impl->feeds (*owner, *t)) result.add (t->itemID.toString());
    return result;
}
Result Session::setPluginSidechain (const String& pluginId, const SidechainSettings& settings)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    te::AudioTrack* owner = nullptr;
    auto* external = impl->externalPlugin (pluginId, &owner);
    if (external == nullptr) return Result::fail ("Plug-in not found.");
    if (! hasSidechainInput (*external)) return Result::fail ("This plug-in has no sidechain input.");
    if (auto frozen = impl->unfrozen (owner); frozen.failed()) return frozen;
    if (! juce::isPositiveAndBelow (static_cast<int> (settings.tap), 3)) return Result::fail ("Unknown sidechain tap.");
    auto* source = settings.sourceTrackId.isEmpty() ? nullptr : impl->track (settings.sourceTrackId);
    if (settings.sourceTrackId.isNotEmpty() && (source == nullptr || ! sidechainSources (pluginId).contains (settings.sourceTrackId)))
        return Result::fail ("That track cannot feed this sidechain: choose a track this one does not play into.");
    if (sidechainSettings (*external) == settings) return Result::ok();
    impl->transaction ("Sidechain");
    auto* undo = &impl->edit->getUndoManager();
    if (source != nullptr && external->getNumWires() == 0) wireSidechain (*external, undo);
    if (source != nullptr) external->state.setProperty (te::IDs::sidechainSourceID, source->itemID.toVar(), undo);
    else external->state.removeProperty (te::IDs::sidechainSourceID, undo);
    external->state.setProperty (sidechainOffProperty, ! settings.on, undo);
    external->state.setProperty (sidechainTapProperty, static_cast<int> (settings.tap), undo);
    impl->changed(); return Result::ok();
}
Result Session::setPluginParameterValue (const String& pluginId, int parameter, float value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    te::AudioTrack* owner = nullptr;
    auto* external = impl->externalPlugin (pluginId, &owner);
    if (external == nullptr || external->getAudioPluginInstance() == nullptr || ! std::isfinite (value)) return Result::fail ("Plug-in parameter not found.");
    if (auto frozen = impl->unfrozen (owner); frozen.failed()) return frozen;
    const auto parameters = externalParameters (*external);
    if (! juce::isPositiveAndBelow (parameter, (int) parameters.size())) return Result::fail ("Plug-in parameter not found.");
    value = juce::jlimit (0.0f, 1.0f, value);
    auto* target = parameters[(size_t) parameter];
    if (target->getCurrentNormalisedValue() == value) return Result::ok();
    const auto key = Impl::deviceKey (pluginId, parameter);
    if (impl->capturing()) impl->capture (key, value); else impl->overrideLane (key);
    target->setNormalisedParameter (value, juce::sendNotificationSync);
    impl->changed(); return Result::ok();
}
std::optional<std::pair<float, float>> Session::midiTargetRange (const String& target) const
{
    const auto parts = juce::StringArray::fromTokens (target, ":", "");
    if (target == "master") return std::pair { -100.0f, 6.0f };
    if (parts.size() == 3 && parts[0] == "mixer" && impl->track (parts[1]) != nullptr)
    {
        if (parts[2] == "volume") return std::pair { -100.0f, 6.0f };
        if (parts[2] == "pan") return std::pair { -1.0f, 1.0f };
    }
    if (parts.size() == 3 && parts[0] == "device" && parts[2].containsOnly ("0123456789"))
        if (auto* device = impl->nativeDevice (parts[1]))
        {
            const auto fields = effectParameters (device->getKind());
            const int index = parts[2].getIntValue();
            if (juce::isPositiveAndBelow (index, (int) fields.size())) return std::pair { fields[(size_t) index].minimum, fields[(size_t) index].maximum };
        }
        else if (impl->externalParameterId (target).isNotEmpty()) return std::pair { 0.0f, 1.0f };
    return std::nullopt;
}
Result Session::setMidiLearnTarget (const String& target)
{
    if (target.isNotEmpty() && ! isMappableTarget (target)) return Result::fail ("That control cannot be mapped.");
    if (impl->midiLearnTarget == target) return Result::ok();
    impl->midiLearnTarget = target;
    sendChangeMessage(); return Result::ok();
}
Result Session::removeMidiMapping (const String& target)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto map = impl->edit->state.getChildWithName (midiMapType);
    for (const auto& m : map)
        if (m["target"].toString() == target)
        {
            impl->transaction ("Remove MIDI mapping");
            map.removeChild (m, &impl->edit->getUndoManager());
            impl->changed(); return Result::ok();
        }
    return Result::fail ("That control has no MIDI mapping.");
}
Result Session::setMidiMappingRange (const String& target, float minimum, float maximum)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    const auto range = midiTargetRange (target);
    if (! range || ! std::isfinite (minimum) || ! std::isfinite (maximum) || minimum < range->first || minimum > range->second || maximum < range->first || maximum > range->second)
        return Result::fail ("Choose a minimum and maximum within the control's range.");
    auto map = impl->edit->state.getChildWithName (midiMapType);
    for (auto m : map)
        if (m["target"].toString() == target)
        {
            if (readMidiMapping (m).minimum == minimum && readMidiMapping (m).maximum == maximum) return Result::ok();
            impl->transaction ("MIDI mapping range");
            m.setProperty ("minimum", minimum, &impl->edit->getUndoManager()); m.setProperty ("maximum", maximum, &impl->edit->getUndoManager());
            impl->changed(); return Result::ok();
        }
    return Result::fail ("That control has no MIDI mapping.");
}
Result Session::handleMidiControl (int channel, int controller, int value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (channel < 1 || channel > 16 || controller < 0 || controller > 127 || value < 0 || value > 127) return Result::fail ("Invalid MIDI controller message.");
    auto* undo = &impl->edit->getUndoManager();
    auto map = impl->edit->state.getChildWithName (midiMapType);
    if (impl->midiLearnTarget.isNotEmpty())
    {
        if (! isMappableTarget (impl->midiLearnTarget)) { impl->midiLearnTarget.clear(); sendChangeMessage(); return Result::fail ("The control being mapped no longer exists."); }
        const auto range = midiTargetRange (impl->midiLearnTarget).value_or (std::pair { 0.0f, 1.0f });
        const MidiMapping mapping { impl->midiLearnTarget, channel, controller, range.first, range.second };
        for (const auto& m : map)
            if (readMidiMapping (m) == mapping) return Result::ok();
        impl->transaction ("MIDI mapping");
        map = impl->edit->state.getOrCreateChildWithName (midiMapType, undo);
        // A controller drives one target, and a target has one controller.
        for (int i = map.getNumChildren(); --i >= 0;)
        {
            const auto existing = readMidiMapping (map.getChild (i));
            if (existing.target == mapping.target || (existing.channel == channel && existing.controller == controller)) map.removeChild (i, undo);
        }
        auto added = midiMappingTree (mapping);
        if (const auto id = impl->externalParameterId (mapping.target); id.isNotEmpty()) added.setProperty (parameterIdProperty, id, nullptr);
        map.appendChild (added, undo);
        impl->changed(); return Result::ok();
    }
    std::optional<MidiMapping> mapping;
    for (const auto& m : map)
        if (const auto candidate = readMidiMapping (m); candidate.channel == channel && candidate.controller == controller && impl->mappingCurrent (m)) mapping = candidate;
    if (! mapping) return Result::ok();
    if (isButtonTarget (mapping->target))
    {
        // The button is pressed as the controller rises above 63.
        auto& last = impl->buttonValues.try_emplace (mapping->target, -1).first->second;
        const bool pressed = value > 63 && last <= 63;
        last = value;
        return pressed && impl->buttonExists (mapping->target) ? impl->pressButton (mapping->target) : Result::ok();
    }
    const auto lane = impl->laneFor (mapping->target);
    if (! lane) return Result::ok();
    // The controller moves the fader or knob position between the mapping's ends.
    const auto from = lane->proportion (mapping->minimum), to = lane->proportion (mapping->maximum);
    const auto target = lane->value (from + (to - from) * (float) value / 127.0f);
    // A controller's moves form one gesture until it rests.
    if (! impl->remoteGesture && ! impl->gesture) { beginEditGesture ("MIDI control"); impl->remoteGesture = true; }
    impl->lastRemote = juce::Time::getMillisecondCounter();
    if (! impl->remoteRest.isTimerRunning())
    {
        impl->remoteRest.tick = [this]
        {
            if (! impl->remoteGesture) { impl->remoteRest.stopTimer(); return; }
            if (juce::Time::getMillisecondCounter() - impl->lastRemote < 500) return;
            impl->remoteGesture = false; impl->remoteRest.stopTimer(); endEditGesture();
        };
        impl->remoteRest.startTimer (100);
    }
    const juce::ScopedValueSetter<bool> applying (impl->applyingRemote, true);
    return impl->applyValue (mapping->target, target);
}
bool Session::isButtonTarget (const String& target)
{
    const auto kind = target.upToFirstOccurrenceOf (":", false, false);
    return kind == "track" || kind == "slot" || kind == "scene" || kind == "transport" || (kind == "device" && target.endsWith (":on"));
}
bool Session::isMappableTarget (const String& target) const
{ return isButtonTarget (target) ? impl->buttonExists (target) : midiTargetRange (target).has_value(); }
Result Session::setKeyLearnTarget (const String& target)
{
    if (target.isNotEmpty() && ! isMappableTarget (target)) return Result::fail ("That control cannot be mapped.");
    if (impl->keyLearnTarget == target) return Result::ok();
    impl->keyLearnTarget = target;
    sendChangeMessage(); return Result::ok();
}
Result Session::removeKeyMapping (const String& target)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto map = impl->edit->state.getChildWithName (keyMapType);
    for (const auto& m : map)
        if (m["target"].toString() == target)
        {
            impl->transaction ("Remove key mapping");
            map.removeChild (m, &impl->edit->getUndoManager());
            impl->changed(); return Result::ok();
        }
    return Result::fail ("That control has no key mapping.");
}
Result Session::handleMappedKey (juce::juce_wchar typed, bool& used)
{
    used = false;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    const auto key = mappableKey (typed);
    if (key == 0) return Result::ok();
    auto* undo = &impl->edit->getUndoManager();
    auto map = impl->edit->state.getChildWithName (keyMapType);
    if (impl->keyLearnTarget.isNotEmpty())
    {
        used = true;
        if (! isMappableTarget (impl->keyLearnTarget)) { impl->keyLearnTarget.clear(); sendChangeMessage(); return Result::fail ("The control being mapped no longer exists."); }
        const auto range = midiTargetRange (impl->keyLearnTarget).value_or (std::pair { 0.0f, 1.0f });
        const KeyMapping mapping { impl->keyLearnTarget, key, range.first, range.second };
        for (const auto& m : map)
            if (readKeyMapping (m) == mapping) return Result::ok();
        impl->transaction ("Key mapping");
        map = impl->edit->state.getOrCreateChildWithName (keyMapType, undo);
        // A key drives one target, and a target has one key.
        for (int i = map.getNumChildren(); --i >= 0;)
            if (const auto existing = readKeyMapping (map.getChild (i)); existing.target == mapping.target || existing.key == key) map.removeChild (i, undo);
        map.appendChild (keyMappingTree (mapping), undo);
        impl->changed(); return Result::ok();
    }
    std::optional<KeyMapping> mapping;
    for (const auto& m : map) if (const auto candidate = readKeyMapping (m); candidate.key == key) mapping = candidate;
    if (! mapping || ! isMappableTarget (mapping->target)) return Result::ok();
    used = true;
    if (isButtonTarget (mapping->target)) return impl->pressButton (mapping->target);
    // A key switches a control between its mapping's ends: to whichever it is further from.
    const auto value = impl->targetValue (mapping->target);
    if (! value) return Result::ok();
    const bool nearerMaximum = std::abs (*value - mapping->maximum) < std::abs (*value - mapping->minimum);
    return impl->applyValue (mapping->target, nearerMaximum ? mapping->minimum : mapping->maximum);
}
Result Session::previewFile (const File& file)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! impl->useDevices || audioDeviceManager().getCurrentAudioDevice() == nullptr) return Result::fail ("Choose an audio device in Audio Settings to preview files.");
    if (! te::AudioFile (impl->engine, file).isValid()) return Result::fail ("This file cannot be previewed: " + file.getFileName());
    stopPreview();
    impl->preview = te::Edit::createEditForPreviewingFile (impl->engine, file, nullptr, false, false, nullptr, {});
    if (! impl->preview) return Result::fail ("This file cannot be previewed: " + file.getFileName());
    impl->preview->getMasterVolumePlugin()->setVolumeDb (impl->previewVolumeDb);
    auto& transport = impl->preview->getTransport();
    transport.ensureContextAllocated();
    transport.setPosition (te::TimePosition());
    transport.play (false);
    impl->previewSource = file;
    sendChangeMessage(); return Result::ok();
}
void Session::stopPreview()
{
    if (! impl->preview) return;
    impl->preview->getTransport().stop (false, true);
    impl->preview.reset(); impl->previewSource = File();
    sendChangeMessage();
}
Result Session::setPreviewVolume (float decibels)
{
    if (! std::isfinite (decibels) || decibels < -70 || decibels > 6) return Result::fail ("Choose a preview volume from -70 to +6 dB.");
    impl->previewVolumeDb = decibels;
    if (impl->preview) impl->preview->getMasterVolumePlugin()->setVolumeDb (decibels);
    sendChangeMessage(); return Result::ok();
}
Result Session::setPunchIn (bool on)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before changing the punch switches.");
    if (impl->punchIn() == on) return Result::ok();
    impl->edit->state.setProperty (punchInProperty, on, nullptr); // A transport setting, outside the undo history.
    impl->changed(); return Result::ok();
}
Result Session::setPunchOut (bool on)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before changing the punch switches.");
    if (impl->punchOut() == on) return Result::ok();
    impl->edit->state.setProperty (punchOutProperty, on, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setAutomationArm (bool on)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->automationArmed() == on) return Result::ok();
    if (! on) impl->finishCaptures (false);
    // A transport setting, saved with the project but outside the undo history.
    impl->edit->state.setProperty (automationArmProperty, on, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::reEnableAutomation()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    const auto keys = impl->overriddenLanes();
    if (keys.isEmpty()) return Result::ok();
    impl->transaction ("Re-enable automation");
    for (const auto& key : keys)
        if (const auto lane = impl->laneFor (key)) { auto points = lane->read(); points.enabled = true; impl->writeLane (*lane, points); }
    impl->refreshParameterValues();
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
    if (impl->anyRecording()) return Result::fail ("Stop recording before changing inputs.");
    const bool midi = t->state.getProperty (midiTrackProperty, false);
    // Validate the typed name before enabling anything. Re-query instances after
    // enabling because the native device list and playback graph may rebuild.
    if (name.isNotEmpty() && !inputNames (midi).contains (name)) return Result::fail ("That input is not available for this track type.");
    if (impl->autoArmed == t->itemID.toString()) { impl->autoArmed.clear(); impl->autoInput = false; } // Chosen by hand now.
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
    if (! impl->input (*t)) return Result::fail ("Select an input before arming this track.");
    // By hand, the auto-armed track keeps what it has; arming another releases it.
    if (impl->autoArmed == t->itemID.toString()) { impl->autoArmed.clear(); impl->autoInput = false; }
    else if (value && impl->autoArmed.isNotEmpty() && ! impl->anyRecording()) impl->releaseAutoArm();
    // Linked tracks with an input arm and disarm with this one.
    std::vector<te::AudioTrack*> tracks { t }, changing;
    for (auto* other : impl->linkedTracks (*t)) tracks.push_back (other);
    for (auto* track : tracks)
    {
        auto* input = impl->input (*track);
        if (input == nullptr || input->isRecordingEnabled (track->itemID) == value) continue;
        if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before changing record arms.");
        // A take records through its slot's own destination, so disarming would leave it recording on
        // a track that shows it is not armed.
        if (impl->trackRecording (track->itemID.toString()))
        { if (track == t) return Result::fail ("End this track's Session recording before disarming it."); continue; }
        if (! te::getDestination (*input, *track, 0)) { if (track == t) return Result::fail ("Input destination is unavailable."); continue; }
        if (value) if (auto allowed = impl->hardwareInputAllowed (*input); allowed.failed()) return allowed;
        changing.push_back (track);
    }
    if (changing.empty()) return Result::ok();
    const auto monitored=impl->monitoredMidiTracks();
    impl->transaction ("Record arm");
    for (auto* track : changing) te::getDestination (*impl->input (*track), *track, 0)->recordEnabled.setValue (value, &impl->edit->getUndoManager());
    impl->finishInputChange(monitored);impl->changed(); return Result::ok();
}
juce::String Session::takeDeviceNotice() { return std::exchange (impl->deviceNotice, {}); }
Result Session::autoArmTrack (const String& id)
{
    if (impl->exporting || impl->anyRecording() || id == impl->autoArmed) return Result::ok();
    const auto monitored = impl->monitoredMidiTracks();
    const auto finish = [&] { impl->finishInputChange (monitored); sendChangeMessage(); return Result::ok(); };
    impl->releaseAutoArm();
    auto* t = impl->track (id);
    if (t == nullptr || isReturn (*t) || ! static_cast<bool> (t->state.getProperty (midiTrackProperty, false))
        || t->isFrozen (te::Track::anyFreeze) || ! preferences().exclusiveArm) return finish();
    // A track armed by hand keeps Auto-Arm off.
    for (auto* other : te::getAudioTracks (*impl->edit))
        if (auto* in = impl->input (*other); in != nullptr && in->isRecordingEnabled (other->itemID)) return finish();
    auto* in = impl->input (*t);
    bool gave = false;
    if (in == nullptr)
    {
        // All MIDI Ins, enabled as choosing it would, unless another track has it. A Session whose
        // MIDI devices have not been scanned yet scans them first.
        auto& devices = impl->engine.getDeviceManager();
        const auto devicesNow = devices.getMidiInDevices();
        if (std::none_of (devicesNow.begin(), devicesNow.end(), [] (const auto& d) { auto* v = dynamic_cast<te::VirtualMidiInputDevice*> (d.get()); return v != nullptr && v->useAllInputs; }))
            devices.rescanMidiDeviceListSynchronously();
        for (auto device : devices.getMidiInDevices())
            if (auto* all = dynamic_cast<te::VirtualMidiInputDevice*> (device.get()); all != nullptr && all->useAllInputs && ! all->isEnabled())
            { all->setEnabled (true); devices.rescanMidiDeviceListSynchronously(); devices.dispatchPendingUpdates(); impl->refreshInputs(); break; }
        for (auto* candidate : impl->edit->getAllInputDevices())
            if (auto* all = dynamic_cast<te::VirtualMidiInputDevice*> (&candidate->getInputDevice()); all != nullptr && all->useAllInputs) in = candidate;
        if (in == nullptr || ! in->getTargets().isEmpty()) return finish();
        if (! in->setTarget (t->itemID, true, nullptr, 0)) return finish();
        in->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::automatic);
        t->state.setProperty (monitorProperty, static_cast<int> (MonitorMode::automatic), nullptr);
        gave = true;
    }
    auto* destination = te::getDestination (*in, *t, 0);
    if (destination == nullptr) { if (gave) { [[maybe_unused]] auto removed = in->removeTarget (t->itemID, nullptr); } return finish(); }
    destination->recordEnabled.setValue (true, nullptr);
    impl->autoArmed = id; impl->autoInput = gave;
    return finish();
}
juce::String Session::autoArmedTrack() const { return impl->autoArmed; }
Session::InputPermission Session::inputPermission() const { return impl->permissionStatus(); }
void Session::setInputPermissionSource (std::function<InputPermission()> status, std::function<void (std::function<void (bool)>)> request)
{
    impl->permissionStatus = std::move (status); impl->permissionRequest = std::move (request); impl->permissionAsked = false;
}
Result Session::setTrackMonitor (int index, MonitorMode mode)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (isReturn (*t)) return Result::fail ("Return tracks cannot receive recording inputs.");
    auto* input = impl->input (*t); if (! input) return Result::fail ("Select an input before enabling monitoring.");
    if (input->getInputDevice().getMonitorMode() == static_cast<te::InputDevice::MonitorMode> (mode)) return Result::ok();
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before changing monitoring.");
    if (mode != MonitorMode::off) if (auto allowed = impl->hardwareInputAllowed (*input); allowed.failed()) return allowed;
    const auto monitored=impl->monitoredMidiTracks();
    impl->transaction ("Input monitoring");
    input->getInputDevice().setMonitorMode (static_cast<te::InputDevice::MonitorMode> (mode));
    t->state.setProperty (monitorProperty, static_cast<int> (mode), &impl->edit->getUndoManager());
    impl->edit->restartPlayback(); impl->finishInputChange(monitored);impl->changed(); return Result::ok();
}
Result Session::play()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail (impl->noDeviceMessage());
    auto& transport = impl->edit->getTransport();
    if (! transport.isPlaying()) transport.setPosition (transport.startPosition);
    transport.play (false); sendChangeMessage(); return Result::ok();
}
Result Session::continuePlayback()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail (impl->noDeviceMessage());
    impl->edit->getTransport().play (false); sendChangeMessage(); return Result::ok();
}
void Session::stop()
{
    if (impl->exporting) return;
    impl->deviceRestart.stopTimer(); // A stop after a device restart is the user's, not the engine's.
    const bool recording = impl->edit->getTransport().isRecording();
    // Session recordings end where the transport stops and keep their takes.
    while (! impl->slotRecordings.empty())
    {
        endEditGesture();
        impl->finishSlotRecording (impl->slotRecordings.front(), false, true);
    }
    const auto performanceEnd = impl->playedTime();
    if (recording) { impl->finishCaptures (false); endEditGesture(); impl->transaction ("Record take"); }
    // The performance joins the take's undo step.
    if (recording) impl->finishPerformance (performanceEnd);
    else impl->performanceStart.reset();
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
        impl->applyPunch();
        // Each pass recorded on an armed track is kept in a take lane too, and replaces the main lane's
        // earlier material under it.
        for (auto* track : te::getAudioTracks (*impl->edit))
            if (auto* input = impl->input (*track); input != nullptr && input->isRecordingEnabled (track->itemID))
            {
                std::vector<te::Clip*> recorded;
                for (auto* clip : track->getClips())
                    if (! impl->clipsBeforeRecording.count (clip->itemID.toString()) && clip->getClipSlot() == nullptr) recorded.push_back (clip);
                for (auto* clip : recorded) impl->keepTake (*track, *clip);
                for (auto* clip : recorded) impl->replaceUnder (*track, *clip, impl->clipsBeforeRecording);
            }
        impl->changed();
    }
    sendChangeMessage();
}
Result Session::record()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->endAudition();
    if (impl->edit->getTransport().isRecording()) return Result::ok();
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail (impl->noDeviceMessage());
    impl->applyDriverCompensation();
    bool armed = false;
    for (auto* t : te::getAudioTracks (*impl->edit))
        if (auto* d = impl->input (*t); d != nullptr && d->isRecordingEnabled (t->itemID))
        {
            // Permission can be withdrawn after a track is armed; a take would hold silence.
            if (auto allowed = impl->hardwareInputAllowed (*d); allowed.failed()) return allowed;
            armed = true;
        }
    // Recording with Session clips playing captures the performance into the Arrangement.
    const bool performing = impl->launcherActive();
    if (! armed && ! impl->automationArmed() && ! performing)
        return Result::fail ("Assign an input and arm at least one track, turn on Automation Arm, or launch Session clips before recording.");
    impl->captures.clear(); impl->captureTransaction = false;
    impl->clipsBeforeRecording.clear();
    for (auto* t : te::getAudioTracks (*impl->edit)) for (auto* c : t->getClips()) impl->clipsBeforeRecording.insert (c->itemID.toString());
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
    bool audioArmed = false;
    for (auto* t : te::getAudioTracks (*impl->edit))
        if (auto* d = impl->input (*t); d != nullptr && ! d->getInputDevice().isMidi() && d->isRecordingEnabled (t->itemID)) audioArmed = true;
    auto r = audioArmed ? impl->takeFolderReady() : impl->assets().createDirectory(); if (r.failed()) return r;
    for (auto* input : impl->edit->getAllInputDevices()) input->flushRecordingStateUpdates();
    impl->edit->dispatchPendingUpdatesSynchronously();
    impl->edit->setCountInMode (static_cast<te::Edit::CountIn> (snapshot().metronome.countInBars));
    for(auto* input:impl->edit->getAllInputDevices()) if(input->isRecordingActive())
        if(auto* midi=dynamic_cast<te::MidiInputDevice*>(&input->getInputDevice())) midi->quantisation.setType("(none)");
    impl->edit->setClickTrackRange ({});
    // Recording starts from the insert marker unless the transport is already playing.
    const bool alreadyPlaying = impl->edit->getTransport().isPlaying();
    if (! alreadyPlaying) impl->edit->getTransport().setPosition (impl->edit->getTransport().startPosition);
    // With Automation Arm on, or Session clips playing, recording runs with no track armed.
    impl->edit->getTransport().record (false, ! armed);
    if (! impl->edit->getTransport().isRecording()) return Result::fail ("Recording could not start. Check the selected input and device settings.");
    impl->startPerformance (alreadyPlaying ? impl->playedTime() : impl->edit->getTransport().startPosition.get());
    impl->changed(); return Result::ok();
}
void Session::seek (double seconds)
{
    if (impl->exporting) return;
    if (std::isfinite (seconds))
    {
        // Takes are timed by the play head, so any recording ends where it is first.
        if (impl->anyRecording()) stop();
        impl->cancelLaunches();
        auto& transport = impl->edit->getTransport();
        const auto target = te::TimePosition::fromSeconds (juce::jmax (0.0, seconds));
        transport.startPosition = target;
        // The cached position follows the play head by a timer, so it can read the target while
        // the play head is elsewhere; locate even then.
        const bool unchanged = transport.getPosition() == target;
        transport.setPosition (target);
        if (unchanged) transport.state.sendPropertyChangeMessage (te::IDs::position);
    }
}
void Session::setInsertMarker (double seconds)
{
    if (impl->exporting || ! std::isfinite (seconds)) return;
    auto& transport = impl->edit->getTransport();
    if (! transport.isPlaying()) { seek (seconds); return; }
    transport.startPosition = te::TimePosition::fromSeconds (juce::jmax (0.0, seconds));
    sendChangeMessage();
}
void Session::returnToStart() { setInsertMarker (0); }
Result Session::setTimeSignature (int numerator, int denominator)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before changing the time signature.");
    if (numerator < 1 || numerator > 99 || denominator < 1 || denominator > 16 || (denominator & (denominator - 1)) != 0)
        return Result::fail ("Choose a time signature of 1 to 99 over 1, 2, 4, 8 or 16.");
    auto* meter = impl->edit->tempoSequence.getTimeSig (0);
    if (meter == nullptr) return Result::fail ("The project has no time signature.");
    if (meter->numerator.get() == numerator && meter->denominator.get() == denominator) return Result::ok();
    impl->transaction ("Time signature");
    meter->numerator = numerator; meter->denominator = denominator;
    impl->changed(); return Result::ok();
}
Result Session::setTempoMap (const std::vector<TempoPoint>& points)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before changing the tempo.");
    if (points.empty() || points.size() > 4096 || points.front().beat != 0)
        return Result::fail ("A tempo map starts at beat 0 and holds at most 4096 points.");
    double previous = -1;
    for (const auto& p : points)
    {
        if (! std::isfinite (p.beat) || ! std::isfinite (p.bpm) || p.beat > 1000000 || p.bpm < te::TempoSetting::minBPM || p.bpm > te::TempoSetting::maxBPM
            || (previous >= 0 && p.beat - previous < 1.0 / 960 - 1e-9))
            return Result::fail ("Tempo points need increasing beats and tempos from 20 to 999 BPM.");
        previous = p.beat;
    }
    if (snapshot().tempoMap == points) return Result::ok();
    impl->resetTapTempo();
    impl->transaction ("Tempo");
    auto* undo = &impl->edit->getUndoManager();
    auto& sequence = impl->edit->tempoSequence;
    // Clips, automation and the loop keep their beats while the map is rebuilt.
    te::EditTimecodeRemapperSnapshot remap; remap.savePreChangeState (*impl->edit);
    for (int i = sequence.getNumTempos(); --i > 0;) sequence.removeTempo (i, false);
    auto* first = sequence.getTempo (0);
    first->bpm.setValue (points.front().bpm, undo); first->curve.setValue (0.0f, undo);
    // Curve 0 ramps linearly to the next point; the engine applies ramps in quarter-beat steps.
    for (size_t i = 1; i < points.size(); ++i) sequence.insertTempo (te::BeatPosition::fromBeats (points[i].beat), points[i].bpm, 0.0f);
    remap.remapEdit (*impl->edit);
    impl->refreshParameterValues();
    impl->changed(); return Result::ok();
}
double Session::beatsAt (double seconds) const { return impl->edit->tempoSequence.toBeats (te::TimePosition::fromSeconds (seconds)).inBeats(); }
double Session::secondsAt (double beats) const { return impl->edit->tempoSequence.toTime (te::BeatPosition::fromBeats (beats)).inSeconds(); }
void Session::setTempo (double bpm)
{
    if (impl->exporting) return;
    if (impl->externalSync) return; // The clock sets the tempo.
    if (impl->edit->tempoSequence.getNumTempos() > 1) return;
    if (! std::isfinite (bpm)) return;
    impl->resetTapTempo();
    const auto value = juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm);
    auto* tempo = impl->edit->tempoSequence.getTempo (0);
    if (tempo->getBpm() == value) return;
    impl->transaction ("Tempo"); impl->changeTempo (*tempo, value, &impl->edit->getUndoManager()); impl->changed();
}
Result Session::tapTempo(double seconds)
{
    if (impl->exporting) return Result::fail("Finish or cancel the current export first.");
    if (impl->edit->tempoSequence.getNumTempos() > 1) return Result::fail("The tempo is automated. Edit its points in the Song Tempo lane.");
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
    impl->transaction("Tap tempo");impl->changeTempo(*tempo,bpm,&impl->edit->getUndoManager());impl->changed();return Result::ok();
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
    impl->remoteGesture = false; // A controller's resting no longer ends the gesture begun here.
    endEditGesture();
    impl->edit->getUndoManager().beginNewTransaction (name);
    impl->gesture = std::make_unique<te::Edit::UndoTransactionInhibitor> (*impl->edit);
}
void Session::endEditGesture()
{
    if (impl->exporting) return;
    if (impl->gesture) { impl->finishCaptures (true); impl->captureTransaction = false; }
    impl->flushStructuralUpdates();
    if (! impl->gesture) return;
    impl->gesture.reset();
    impl->edit->getUndoManager().beginNewTransaction();
}
void Session::undo()
{
    if (impl->exporting || impl->anyRecording()) return;
    impl->endAudition();
    const auto monitored=impl->monitoredMidiTracks();const auto clips=impl->clipActivationStates(); const auto follows=impl->followStates();
    impl->resetTapTempo();
    endEditGesture(); closePluginEditors(); impl->edit->getUndoManager().undo(); impl->refreshClipActivation(clips); impl->refreshFollowActions(follows); impl->refreshParameterValues();
    impl->finishInputChange(monitored);sendChangeMessage();
}
void Session::redo()
{
    if (impl->exporting || impl->anyRecording()) return;
    impl->endAudition();
    const auto monitored=impl->monitoredMidiTracks();const auto clips=impl->clipActivationStates(); const auto follows=impl->followStates();
    impl->resetTapTempo();
    endEditGesture(); closePluginEditors();
    {
        auto& history = impl->edit->getUndoManager();
        juce::Array<te::AudioTrack*> mayFreeze;
        if (history.getRedoDescription() == freezeStep)
            for (auto* track : te::getAudioTracks (*impl->edit))
                if (! track->isFrozen (te::Track::individualFreeze)) mayFreeze.add (track);
        const FreezeRender render (mayFreeze);
        history.redo();
    }
    impl->refreshClipActivation(clips); impl->refreshFollowActions(follows); impl->refreshParameterValues();
    impl->finishInputChange(monitored);sendChangeMessage();
}
bool Session::canUndo() const { return ! impl->exporting && ! impl->anyRecording() && impl->edit->getUndoManager().canUndo(); }
bool Session::canRedo() const { return ! impl->exporting && ! impl->anyRecording() && impl->edit->getUndoManager().canRedo(); }

Result Session::exportWav (const File& file, double rate, int bits, double tailSeconds)
{
    return exportAudio (file, ExportFormat::wav, rate, bits, tailSeconds);
}

Result Session::exportStems (const File& folder, const AudioExportSettings& settings, juce::StringArray& written, bool overwrite)
{
    written.clear();
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->anyRecording()) return Result::fail ("Stop recording before exporting.");
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
    impl->endAudition(); // Exports render the main lane.
    const auto format = settings.format;
    const double rate = settings.sampleRate, tailSeconds = settings.tailSeconds;
    const int bits = settings.bitDepth;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->anyRecording()) return Result::fail ("Stop recording before exporting.");
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
        for (auto* plugin : track->pluginList.getPlugins())
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
    if (settings.video)
    {
        // The picture of the video clip under the export's start, for the exported time, with the
        // exported sound, written before the audio replaces any earlier export.
        const auto first = settings.timeRange ? static_cast<double> (firstFrame) / rate : 0.0;
        const auto shown = videoAt (first);
        if (! shown) return Result::fail ("Export Video needs a video clip playing at the start of the export.");
        const auto movie = file.withFileExtension (".mov");
        if (movie.exists() && ! settings.replaceVideo) return Result::fail ("A movie named " + movie.getFileName() + " is already there.");
        juce::TemporaryFile written (movie);
        if (auto r = video::writeWithSound (shown->movie, shown->seconds, end - first, output.getFile(), written.getFile()); r.failed()) return r;
        if (! written.overwriteTargetFileWithTemporary()) return Result::fail ("The movie could not be written.");
    }
    if (! output.overwriteTargetFileWithTemporary()) return Result::fail ("The audio export could not be completed.");
    sendChangeMessage(); return Result::ok();
}

bool Session::isExporting() const { return impl->exporting; }
void Session::cancelExport() { static_cast<UIBehaviour&> (impl->engine.getUIBehaviour()).cancelTask(); }

juce::AudioDeviceManager& Session::audioDeviceManager() { return impl->engine.getDeviceManager().deviceManager; }
Session::RecordingFormat Session::recordingFormat() const
{
    if (impl->chosenRecordingFormat) return *impl->chosenRecordingFormat;
    RecordingFormat format;
    auto& formats = impl->engine.getAudioFileFormatManager();
    for (auto* device : impl->engine.getDeviceManager().getWaveInputDevices())
    {
        const auto name = device->getOutputFormat();
        format.type = name == formats.getAiffFormat()->getFormatName() ? RecordingFileType::aiff
                    : name == formats.getFlacFormat()->getFormatName() ? RecordingFileType::flac : RecordingFileType::wav;
        if (device->getBitDepth() > 0) format.bitDepth = device->getBitDepth();
        break;
    }
    return format;
}
Result Session::setRecordingFormat (const RecordingFormat& format)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->anyRecording()) return Result::fail ("Stop recording before changing the recording format.");
    auto* audioFormat = impl->recordingAudioFormat (format.type);
    if (audioFormat == nullptr || ! audioFormat->getPossibleBitDepths().contains (format.bitDepth))
    {
        juce::StringArray depths; if (audioFormat != nullptr) for (auto d : audioFormat->getPossibleBitDepths()) depths.add (String (d));
        return Result::fail ("This file type records at " + depths.joinIntoString (", ") + " bits.");
    }
    impl->chosenRecordingFormat = format;
    impl->applyRecordingFormat();
    sendChangeMessage(); return Result::ok();
}
Result Session::setAudioEngineEnabled (bool on)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto& devices = audioDeviceManager();
    if (on == isAudioEngineEnabled()) return Result::ok();
    if (! on)
    {
        // Takes end where the transport stops and are kept, as Stop keeps them.
        endEditGesture(); stop();
        devices.closeAudioDevice();
        impl->engineOff = true;
        sendChangeMessage(); return Result::ok();
    }
    devices.restartLastAudioDevice();
    if (devices.getCurrentAudioDevice() == nullptr) return Result::fail ("The audio device could not be reopened. Choose one in Audio Settings.");
    impl->engineOff = false;
    sendChangeMessage(); return Result::ok();
}
bool Session::isAudioEngineEnabled() const
{
    // Choosing a device in Audio Settings turns the engine back on.
    return ! impl->engineOff || impl->engine.getDeviceManager().deviceManager.getCurrentAudioDevice() != nullptr;
}
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
    if (! juce::isPositiveAndBelow (static_cast<int> (kind), deviceKindCount)) return Result::fail ("Unknown native effect.");
    if (! track->pluginList.canInsertPlugin()) return Result::fail ("This track's device chain is full.");
    juce::ValueTree state (te::IDs::PLUGIN);
    state.setProperty (te::IDs::type, NativeEffectPlugin::xmlTypeName, nullptr);
    state.setProperty ("effectKind", static_cast<int> (kind), nullptr);
    if (effectIsInstrument (kind)) return Result::fail ("Choose instruments from the track's instrument menu.");
    auto* instrument = Impl::instrument (*track);
    if (effectIsMidi (kind) && instrument == nullptr) return Result::fail ("MIDI effects need a MIDI track.");
    auto plugin = impl->edit->getPluginCache().createNewPlugin (state);
    if (! plugin) return Result::fail ("The native effect could not be created.");
    const int insertion = Impl::chainEnd (*track, effectIsMidi (kind) ? instrument : nullptr);
    impl->transaction ("Add " + String (effectName (kind)));
    track->pluginList.insertPlugin (plugin, insertion, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setNativeEffectSettings (const String& id, const EffectSettings& settings)
{
    te::AudioTrack* owner = nullptr;
    auto* effect = impl->nativeDevice (id, &owner);
    if (auto frozen = impl->unfrozen (owner); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! validEffectSettings (settings)) return Result::fail ("Effect settings are outside their supported ranges.");
    if (effect == nullptr) return Result::fail ("Native effect not found.");
    const auto previous = effect->readSettings();
    if (previous.kind != settings.kind) return Result::fail ("Effect type cannot change during an edit.");
    if (previous.values == settings.values) return Result::ok();
    const auto controls = effectParameters (settings.kind).size();
    if (impl->capturing())
    {
        for (size_t i = 0; i < controls; ++i)
            if (previous.values[i] != settings.values[i]) impl->capture (Impl::deviceKey (id, (int) i), settings.values[i]);
        effect->writeSettings (settings); impl->changed(); return Result::ok();
    }
    impl->transaction ("Edit " + String (effectName (settings.kind)));
    for (size_t i = 0; i < controls; ++i)
        if (previous.values[i] != settings.values[i]) impl->overrideLane (Impl::deviceKey (id, (int) i));
    effect->writeSettings (settings); impl->changed(); return Result::ok();
}
Result Session::addUtility (int index)
{
    if (auto frozen = impl->unfrozen (impl->track (index)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (index); if (! track) return Result::fail ("Track not found.");
    if (! track->pluginList.canInsertPlugin()) return Result::fail ("This track's device chain is full.");
    auto plugin = impl->edit->getPluginCache().createNewPlugin (UtilityPlugin::xmlTypeName, {});
    if (! plugin) return Result::fail ("The channel utility could not be created.");
    const int insertion = Impl::chainEnd (*track);
    impl->transaction ("Add channel utility");
    track->pluginList.insertPlugin (plugin, insertion, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setUtilitySettings (const String& id, const UtilitySettings& settings)
{
    te::AudioTrack* owner = nullptr;
    auto* utility = impl->utility (id, &owner);
    if (auto frozen = impl->unfrozen (owner); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! UtilityPlugin::validSettings (settings)) return Result::fail ("Channel utility settings are outside their supported ranges.");
    if (utility == nullptr) return Result::fail ("Channel utility not found.");
    if (utility->readSettings() == settings) return Result::ok();
    impl->transaction ("Edit channel utility"); utility->writeSettings (settings);
    impl->changed(); return Result::ok();
}
namespace
{
// Children of a rack's state of one type, in order.
std::vector<juce::ValueTree> rackChildren (const juce::ValueTree& rack, const juce::Identifier& type)
{
    std::vector<juce::ValueTree> result;
    for (const auto& child : rack) if (child.hasType (type)) result.push_back (child);
    return result;
}
std::vector<juce::ValueTree> chainDevices (const juce::ValueTree& chain)
{
    std::vector<juce::ValueTree> result;
    for (const auto& child : chain) if (child.hasType (te::IDs::PLUGIN)) result.push_back (child);
    return result;
}
Session::EffectKind deviceKind (const juce::ValueTree& device) { return static_cast<Session::EffectKind> (static_cast<int> (device.getProperty ("effectKind", 0))); }
// Where a new rack child of this type goes: after the last child of the same type, or at the front.
int rackInsertIndex (const juce::ValueTree& rack, const juce::Identifier& type)
{
    int index = -1;
    for (int i = 0; i < rack.getNumChildren(); ++i) if (rack.getChild (i).hasType (type)) index = i;
    if (index >= 0) return index + 1;
    if (type == racks::returnType) return rackInsertIndex (rack, racks::chainType);
    return 0;
}
juce::ValueTree newDeviceState (te::Edit& edit, Session::EffectKind kind)
{
    juce::ValueTree state (te::IDs::PLUGIN);
    state.setProperty (te::IDs::type, NativeEffectPlugin::xmlTypeName, nullptr);
    state.setProperty ("effectKind", static_cast<int> (kind), nullptr);
    edit.createNewItemID().writeID (state, nullptr);
    return state;
}
}
Result Session::addRackChain (const String& rackId, const String& name, String& chainId, int index)
{
    te::AudioTrack* track = nullptr;
    auto* rack = impl->rack (rackId, &track);
    if (rack == nullptr) return Result::fail ("Rack not found.");
    if (auto r = impl->editable (track); r.failed()) return r;
    const auto chains = rackChildren (rack->state, racks::chainType);
    if ((int) chains.size() >= maximumRackChains) return Result::fail ("A rack holds at most 128 chains.");
    if (name.length() > 256) return Result::fail ("Chain names are at most 256 characters.");
    RackChainSettings settings; settings.name = name.trim().isEmpty() ? String ("Chain") : name.trim();
    if (rack->getKind() == EffectKind::drumRack)
    {
        // A new pad chain takes the first pad without a chain from C1 upward.
        std::set<int> used; for (const auto& c : chains) used.insert (racks::readChain (c).receiveNote);
        int note = 36; for (int i = 0; i < 128 && used.contains (note); ++i) note = (note + 1) % 128;
        settings.receiveNote = note;
    }
    juce::ValueTree chain (racks::chainType);
    impl->edit->createNewItemID().writeID (chain, nullptr);
    racks::writeChain (chain, settings, nullptr);
    const int child = index >= 0 && index < (int) chains.size() ? rack->state.indexOf (chains[(size_t) index]) : rackInsertIndex (rack->state, racks::chainType);
    impl->transaction ("Add chain");
    rack->state.addChild (chain, child, &impl->edit->getUndoManager());
    chainId = chain[te::IDs::id].toString();
    impl->changed(); return Result::ok();
}
Result Session::removeRackChain (const String& chainId)
{
    auto ref = impl->chain (chainId);
    if (ref.rack == nullptr) return Result::fail ("Chain not found.");
    if (ref.isReturn) return removeRackReturn (chainId);
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    closePluginEditors();
    impl->transaction ("Delete chain");
    impl->removeMappings (*ref.rack, Impl::chainTargets (ref.state));
    ref.rack->state.removeChild (ref.state, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::moveRackChain (const String& chainId, int destination)
{
    auto ref = impl->chain (chainId);
    if (ref.rack == nullptr) return Result::fail ("Chain not found.");
    if (ref.isReturn) return Result::fail ("Return chains keep their order.");
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    const auto chains = rackChildren (ref.rack->state, racks::chainType);
    if (! juce::isPositiveAndBelow (destination, (int) chains.size())) return Result::fail ("Chain position not found.");
    if (chains[(size_t) destination] == ref.state) return Result::ok();
    impl->transaction ("Move chain");
    ref.rack->state.moveChild (ref.rack->state.indexOf (ref.state), ref.rack->state.indexOf (chains[(size_t) destination]), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::duplicateRackChain (const String& chainId, String& duplicateId)
{
    auto ref = impl->chain (chainId);
    if (ref.rack == nullptr || ref.isReturn) return Result::fail ("Chain not found.");
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    if ((int) rackChildren (ref.rack->state, racks::chainType).size() >= maximumRackChains) return Result::fail ("A rack holds at most 128 chains.");
    auto copy = ref.state.createCopy();
    te::EditItemID::remapIDs (copy, nullptr, *impl->edit);
    impl->transaction ("Duplicate chain");
    ref.rack->state.addChild (copy, ref.rack->state.indexOf (ref.state) + 1, &impl->edit->getUndoManager());
    duplicateId = copy[te::IDs::id].toString();
    impl->changed(); return Result::ok();
}
Result Session::setRackChainSettings (const String& chainId, const RackChainSettings& settings)
{
    auto ref = impl->chain (chainId);
    if (ref.rack == nullptr) return Result::fail ("Chain not found.");
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    if (! racks::validChain (settings, ref.rack->getKind(), ref.isReturn))
        return Result::fail ("Chain settings are outside their supported ranges.");
    if (racks::readChain (ref.state) == settings) return Result::ok();
    impl->transaction ("Edit chain");
    racks::writeChain (ref.state, settings, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::addRackDevice (const String& chainId, EffectKind kind, String& deviceId, int index)
{
    auto ref = impl->chain (chainId);
    if (ref.rack == nullptr) return Result::fail ("Chain not found.");
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    if (! juce::isPositiveAndBelow ((int) kind, deviceKindCount)) return Result::fail ("Unknown device.");
    if (! racks::chainAccepts (ref.isReturn ? EffectKind::audioEffectRack : ref.rack->getKind(), kind))
        return Result::fail ("That device does not belong in this chain.");
    if (effectIsRack (kind) && Impl::rackDepth (ref.rack->state) + 1 > racks::maximumDepth) return Result::fail ("Racks can be nested at most four deep.");
    const auto devices = chainDevices (ref.state);
    const int stage = racks::deviceStage (kind);
    int low = 0, high = 0; juce::ValueTree replaced;
    for (const auto& d : devices)
    {
        const auto existing = racks::deviceStage (deviceKind (d));
        if (existing < stage) ++low;
        if (existing <= stage) ++high;
        if (existing == 1 && stage == 1) replaced = d;
    }
    if (! replaced.isValid() && (int) devices.size() >= maximumChainDevices) return Result::fail ("A chain holds at most 32 devices.");
    auto state = newDeviceState (*impl->edit, kind);
    auto* undo = &impl->edit->getUndoManager();
    impl->transaction ("Add " + String (effectName (kind)));
    if (replaced.isValid())
    {
        // A chain has one instrument: a new one takes its place.
        closePluginEditors();
        impl->removeMappings (*ref.rack, { replaced[te::IDs::id].toString() });
        const int child = ref.state.indexOf (replaced);
        ref.state.removeChild (replaced, undo);
        ref.state.addChild (state, child, undo);
    }
    else
    {
        const int position = index < 0 ? high : juce::jlimit (low, high, index);
        ref.state.addChild (state, position < (int) devices.size() ? ref.state.indexOf (devices[(size_t) position]) : -1, undo);
    }
    // A new chain is named after its first device.
    if (devices.empty() && ref.state[te::IDs::name].toString() == "Chain") ref.state.setProperty (te::IDs::name, effectName (kind), undo);
    deviceId = state[te::IDs::id].toString();
    impl->changed(); return Result::ok();
}
Result Session::addRackUtility (const String& chainId, String& deviceId, int index)
{
    auto ref = impl->chain (chainId);
    if (ref.rack == nullptr) return Result::fail ("Chain not found.");
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    if (! ref.isReturn && ref.rack->getKind() == EffectKind::midiEffectRack) return Result::fail ("A MIDI chain holds MIDI effects only.");
    const auto devices = chainDevices (ref.state);
    if ((int) devices.size() >= maximumChainDevices) return Result::fail ("A chain holds at most 32 devices.");
    // The Utility goes among the chain's audio devices.
    int low = 0;
    for (const auto& d : devices) if (racks::deviceStage (deviceKind (d)) < 2 && d[te::IDs::type].toString() == NativeEffectPlugin::xmlTypeName) ++low;
    const int position = index < 0 ? (int) devices.size() : juce::jlimit (low, (int) devices.size(), index);
    juce::ValueTree state (te::IDs::PLUGIN);
    state.setProperty (te::IDs::type, UtilityPlugin::xmlTypeName, nullptr);
    impl->edit->createNewItemID().writeID (state, nullptr);
    impl->transaction ("Add channel utility");
    ref.state.addChild (state, position < (int) devices.size() ? ref.state.indexOf (devices[(size_t) position]) : -1, &impl->edit->getUndoManager());
    if (devices.empty() && ref.state[te::IDs::name].toString() == "Chain") ref.state.setProperty (te::IDs::name, "Utility", &impl->edit->getUndoManager());
    deviceId = state[te::IDs::id].toString();
    impl->changed(); return Result::ok();
}
Result Session::showDeviceEditor (const String& deviceId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    te::Plugin* plugin = impl->nativeDevice (deviceId);
    if (plugin == nullptr) plugin = impl->utility (deviceId);
    if (plugin == nullptr) return Result::fail ("Device not found.");
    plugin->windowState->showWindowExplicitly();
    auto* window = dynamic_cast<PluginWindow*> (plugin->windowState->pluginWindow.get());
    if (window == nullptr || ! window->isVisible() || window->getContentComponent() == nullptr) return Result::fail ("This device has no available editor.");
    return Result::ok();
}
Result Session::removeRackDevice (const String& deviceId)
{
    auto ref = impl->rackDevice (deviceId);
    if (ref.device == nullptr) return Result::fail ("Device not found in a rack.");
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    closePluginEditors();
    impl->transaction ("Delete device");
    juce::StringArray targets { deviceId };
    impl->removeMappings (*ref.rack, targets);
    ref.chain.removeChild (ref.device->state, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::moveRackDevice (const String& deviceId, int destination)
{
    auto ref = impl->rackDevice (deviceId);
    if (ref.device == nullptr) return Result::fail ("Device not found in a rack.");
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    auto devices = chainDevices (ref.chain);
    if (! juce::isPositiveAndBelow (destination, (int) devices.size())) return Result::fail ("Device position not found.");
    const auto from = (int) (std::find (devices.begin(), devices.end(), ref.device->state) - devices.begin());
    if (from == destination) return Result::ok();
    auto order = devices; const auto moved = order[(size_t) from];
    order.erase (order.begin() + from); order.insert (order.begin() + destination, moved);
    for (size_t i = 1; i < order.size(); ++i)
        if (racks::deviceStage (deviceKind (order[i])) < racks::deviceStage (deviceKind (order[i - 1])))
            return Result::fail ("MIDI effects stay before the instrument; audio effects stay after it.");
    impl->transaction ("Move device");
    ref.chain.moveChild (ref.chain.indexOf (moved), ref.chain.indexOf (devices[(size_t) destination]), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setRackDeviceEnabled (const String& deviceId, bool enabled)
{
    auto ref = impl->rackDevice (deviceId);
    if (ref.device == nullptr) return Result::fail ("Device not found in a rack.");
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    if (ref.device->isEnabled() == enabled) return Result::ok();
    impl->transaction (enabled ? "Activate device" : "Deactivate device");
    ref.device->setEnabled (enabled);
    impl->changed(); return Result::ok();
}
Result Session::setRackDeviceSample (const String& deviceId, const File& sample)
{
    auto ref = impl->rackDevice (deviceId);
    if (ref.native == nullptr || ref.native->getKind() != EffectKind::sampler) return Result::fail ("Sampler not found in a rack.");
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    if (const auto problem = samplerSampleProblem (sample); problem.isNotEmpty()) return Result::fail (problem);
    if (ref.native->sampleFile() == sample) return Result::ok();
    impl->transaction ("Load sample");
    ref.device->state.setProperty (samplerSampleProperty, sample.getFullPathName(), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setRackMacroMapping (const String& rackId, const MacroMapping& mapping)
{
    te::AudioTrack* track = nullptr;
    auto* rack = impl->rack (rackId, &track);
    if (rack == nullptr) return Result::fail ("Rack not found.");
    if (auto r = impl->editable (track); r.failed()) return r;
    if (! juce::isPositiveAndBelow (mapping.macro, rackMacroCount)) return Result::fail ("Racks have macros 1 to 16.");
    const bool self = mapping.targetId == rackId;
    juce::ValueTree chainState; NativeEffectPlugin* device = nullptr;
    if (! self)
    {
        for (const auto& child : rack->state)
            if ((child.hasType (racks::chainType) || child.hasType (racks::returnType)) && child[te::IDs::id].toString() == mapping.targetId) chainState = child;
        if (! chainState.isValid())
            for (auto* d : rack->rackEngine()->devices()) if (d->itemID.toString() == mapping.targetId) device = d;
    }
    if (! self && ! chainState.isValid() && device == nullptr)
        return Result::fail ("Map a device in this rack, one of its chains or its chain selector.");
    if (chainState.isValid() && mapping.parameter >= 2
        && (chainState.hasType (racks::returnType) || mapping.parameter - 2 >= (int) rackChildren (rack->state, racks::returnType).size()))
        return Result::fail ("That chain has no such send.");
    const auto range = racks::targetRange (device != nullptr ? device->getKind() : rack->getKind(), chainState.isValid(), self, mapping.parameter);
    if (! range) return Result::fail ("That target has no such control.");
    const auto inside = [&] (float v) { return std::isfinite (v) && v >= range->first && v <= range->second; };
    if (! inside (mapping.minimum) || ! inside (mapping.maximum)) return Result::fail ("A mapping's range must lie within the control's range.");
    juce::ValueTree existing; int count = 0; bool macroInUse = false;
    for (const auto& child : rack->state)
    {
        if (! child.hasType (racks::mappingType)) continue;
        ++count; const auto m = racks::readMapping (child);
        if (m.targetId == mapping.targetId && m.parameter == mapping.parameter) existing = child;
        else if (m.macro == mapping.macro) macroInUse = true;
    }
    if (existing.isValid() && racks::readMapping (existing) == mapping) return Result::ok();
    if (! existing.isValid() && count >= maximumMacroMappings) return Result::fail ("A rack holds at most 256 macro mappings.");
    const auto proportion = [&] (float v)
    {
        return device != nullptr ? effectValueToProportion (device->getKind(), mapping.parameter, v)
             : self ? v / 127.0f : racks::chainValueToProportion (mapping.parameter, v);
    };
    float current = 0;
    if (device != nullptr) current = device->readSettings().values[(size_t) mapping.parameter];
    else if (self) current = rack->readSettings().values[(size_t) rackChainSelector];
    else
    {
        const auto chainSettings = racks::readChain (chainState);
        current = mapping.parameter == 0 ? chainSettings.volumeDb : mapping.parameter == 1 ? chainSettings.pan : chainSettings.sendsDb[(size_t) mapping.parameter - 2];
    }
    auto* undo = &impl->edit->getUndoManager();
    impl->transaction ("Map macro");
    if (existing.isValid()) rack->state.removeChild (existing, undo);
    rack->state.addChild (racks::makeMapping (mapping), rackInsertIndex (rack->state, racks::mappingType), undo);
    // Changing an existing mapping's range keeps its macro where it is.
    const bool sameMacro = existing.isValid() && racks::readMapping (existing).macro == mapping.macro;
    if (! macroInUse && ! sameMacro)
    {
        // A macro's first mapping starts the macro at the control's current value, so nothing jumps.
        const auto low = proportion (mapping.minimum), high = proportion (mapping.maximum);
        const auto position = high != low ? juce::jlimit (0.0f, 1.0f, (proportion (current) - low) / (high - low)) : 0.0f;
        auto settings = rack->readSettings(); settings.values[(size_t) mapping.macro] = position * 127.0f;
        rack->writeSettings (settings);
    }
    impl->changed(); return Result::ok();
}
Result Session::removeRackMacroMapping (const String& rackId, int macro, const String& targetId, int parameter)
{
    te::AudioTrack* track = nullptr;
    auto* rack = impl->rack (rackId, &track);
    if (rack == nullptr) return Result::fail ("Rack not found.");
    if (auto r = impl->editable (track); r.failed()) return r;
    juce::ValueTree found;
    for (const auto& child : rack->state)
        if (child.hasType (racks::mappingType))
            if (const auto m = racks::readMapping (child); m.macro == macro && m.targetId == targetId && m.parameter == parameter) found = child;
    if (! found.isValid()) return Result::fail ("Mapping not found.");
    const auto m = racks::readMapping (found);
    const auto macroValue = juce::jlimit (0.0f, 1.0f, rack->readSettings().values[(size_t) macro] / 127.0f);
    auto* undo = &impl->edit->getUndoManager();
    impl->transaction ("Unmap macro");
    rack->state.removeChild (found, undo);
    // The control keeps the value the macro gave it.
    if (targetId == rackId)
    {
        auto settings = rack->readSettings();
        settings.values[(size_t) rackChainSelector] = std::round ((m.minimum + (m.maximum - m.minimum) * macroValue));
        rack->writeSettings (settings);
    }
    else if (auto* device = rack->rackEngine()->findDevice (targetId))
    {
        const auto low = effectValueToProportion (device->getKind(), parameter, m.minimum), high = effectValueToProportion (device->getKind(), parameter, m.maximum);
        auto settings = device->readSettings();
        settings.values[(size_t) parameter] = effectProportionToValue (device->getKind(), parameter, low + (high - low) * macroValue);
        device->writeSettings (settings);
    }
    else
        for (auto child : rack->state)
            if ((child.hasType (racks::chainType) || child.hasType (racks::returnType)) && child[te::IDs::id].toString() == targetId)
            {
                const auto low = racks::chainValueToProportion (parameter, m.minimum), high = racks::chainValueToProportion (parameter, m.maximum);
                const auto value = racks::chainProportionToValue (parameter, low + (high - low) * macroValue);
                auto settings = racks::readChain (child);
                if (parameter == 0) settings.volumeDb = value; else if (parameter == 1) settings.pan = value; else settings.sendsDb[(size_t) parameter - 2] = value;
                racks::writeChain (child, settings, undo);
            }
    impl->changed(); return Result::ok();
}
Result Session::setRackVisibleMacros (const String& rackId, int count)
{
    auto* rack = impl->rack (rackId);
    if (rack == nullptr) return Result::fail ("Rack not found.");
    if (count < 1 || count > rackMacroCount) return Result::fail ("Racks show 1 to 16 macros.");
    if ((int) rack->state.getProperty (racks::visibleMacrosProperty, 8) == count) return Result::ok();
    // Shown macros are view state: saved with the project, not an undo step.
    rack->state.setProperty (racks::visibleMacrosProperty, count, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setRackMacroName (const String& rackId, int macro, const String& name)
{
    te::AudioTrack* track = nullptr;
    auto* rack = impl->rack (rackId, &track);
    if (rack == nullptr) return Result::fail ("Rack not found.");
    if (auto r = impl->editable (track); r.failed()) return r;
    if (! juce::isPositiveAndBelow (macro, rackMacroCount)) return Result::fail ("Racks have macros 1 to 16.");
    const auto trimmed = name.trim();
    if (trimmed.length() > 64 || trimmed.containsAnyOf ("\r\n")) return Result::fail ("Macro names are one line of at most 64 characters.");
    if (rack->state[racks::macroNameProperty (macro)].toString() == trimmed) return Result::ok();
    impl->transaction ("Rename macro");
    if (trimmed.isEmpty()) rack->state.removeProperty (racks::macroNameProperty (macro), &impl->edit->getUndoManager());
    else rack->state.setProperty (racks::macroNameProperty (macro), trimmed, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setDrumRackPadSample (const String& rackId, int note, const File& sample, String& chainId)
{
    te::AudioTrack* track = nullptr;
    auto* rack = impl->rack (rackId, &track);
    if (rack == nullptr || rack->getKind() != EffectKind::drumRack) return Result::fail ("Drum Rack not found.");
    if (auto r = impl->editable (track); r.failed()) return r;
    if (note < 0 || note > 127) return Result::fail ("Drum pads play notes 0 to 127.");
    std::vector<juce::ValueTree> pad;
    for (const auto& chain : rackChildren (rack->state, racks::chainType)) if (racks::readChain (chain).receiveNote == note) pad.push_back (chain);
    auto* undo = &impl->edit->getUndoManager();
    if (sample == File())
    {
        chainId.clear();
        if (pad.empty()) return Result::ok();
        closePluginEditors();
        impl->transaction ("Clear pad");
        for (const auto& chain : pad) { impl->removeMappings (*rack, Impl::chainTargets (chain)); rack->state.removeChild (chain, undo); }
        impl->changed(); return Result::ok();
    }
    if (const auto problem = samplerSampleProblem (sample); problem.isNotEmpty()) return Result::fail (problem);
    // A Sampler already on the pad takes the new sample.
    for (const auto& chain : pad)
        for (auto device : chainDevices (chain))
            if (deviceKind (device) == EffectKind::sampler)
            {
                chainId = chain[te::IDs::id].toString();
                if (device[samplerSampleProperty].toString() == sample.getFullPathName()) return Result::ok();
                impl->transaction ("Load pad sample");
                device.setProperty (samplerSampleProperty, sample.getFullPathName(), undo);
                impl->changed(); return Result::ok();
            }
    if ((int) rackChildren (rack->state, racks::chainType).size() >= maximumRackChains) return Result::fail ("A rack holds at most 128 chains.");
    juce::ValueTree chain (racks::chainType);
    impl->edit->createNewItemID().writeID (chain, nullptr);
    RackChainSettings settings; settings.name = sample.getFileNameWithoutExtension(); settings.receiveNote = note; settings.playNote = 60;
    racks::writeChain (chain, settings, nullptr);
    // Pads play their samples as one-shots at the sample's own pitch.
    auto sampler = newDeviceState (*impl->edit, EffectKind::sampler);
    sampler.setProperty (samplerSampleProperty, sample.getFullPathName(), nullptr);
    sampler.setProperty ("mode", 1, nullptr);
    chain.appendChild (sampler, nullptr);
    impl->transaction ("Load pad sample");
    rack->state.addChild (chain, rackInsertIndex (rack->state, racks::chainType), undo);
    chainId = chain[te::IDs::id].toString();
    impl->changed(); return Result::ok();
}
Result Session::swapDrumRackPads (const String& rackId, int note, int other)
{
    te::AudioTrack* track = nullptr;
    auto* rack = impl->rack (rackId, &track);
    if (rack == nullptr || rack->getKind() != EffectKind::drumRack) return Result::fail ("Drum Rack not found.");
    if (auto r = impl->editable (track); r.failed()) return r;
    if (note < 0 || note > 127 || other < 0 || other > 127) return Result::fail ("Drum pads play notes 0 to 127.");
    std::vector<juce::ValueTree> moving;
    for (const auto& chain : rackChildren (rack->state, racks::chainType))
        if (const auto r = racks::readChain (chain).receiveNote; note != other && (r == note || r == other)) moving.push_back (chain);
    if (moving.empty()) return Result::ok();
    impl->transaction ("Move pad");
    for (auto chain : moving)
    {
        auto settings = racks::readChain (chain);
        settings.receiveNote = settings.receiveNote == note ? other : note;
        racks::writeChain (chain, settings, &impl->edit->getUndoManager());
    }
    impl->changed(); return Result::ok();
}
Result Session::addRackReturn (const String& rackId, const String& name, String& returnId)
{
    te::AudioTrack* track = nullptr;
    auto* rack = impl->rack (rackId, &track);
    if (rack == nullptr || rack->getKind() != EffectKind::drumRack) return Result::fail ("Return chains belong to Drum Racks.");
    if (auto r = impl->editable (track); r.failed()) return r;
    const auto returns = rackChildren (rack->state, racks::returnType);
    if ((int) returns.size() >= maximumRackReturns) return Result::fail ("A Drum Rack holds at most six return chains.");
    if (name.length() > 256) return Result::fail ("Chain names are at most 256 characters.");
    juce::ValueTree chain (racks::returnType);
    impl->edit->createNewItemID().writeID (chain, nullptr);
    RackChainSettings settings;
    settings.name = name.trim().isEmpty() ? "Return " + String::charToString ((juce::juce_wchar) ('A' + returns.size())) : name.trim();
    racks::writeChain (chain, settings, nullptr);
    impl->transaction ("Add return chain");
    rack->state.addChild (chain, rackInsertIndex (rack->state, racks::returnType), &impl->edit->getUndoManager());
    returnId = chain[te::IDs::id].toString();
    impl->changed(); return Result::ok();
}
Result Session::removeRackReturn (const String& returnId)
{
    auto ref = impl->chain (returnId);
    if (ref.rack == nullptr || ! ref.isReturn) return Result::fail ("Return chain not found.");
    if (auto r = impl->editable (ref.track); r.failed()) return r;
    const auto returns = rackChildren (ref.rack->state, racks::returnType);
    const auto index = (int) (std::find (returns.begin(), returns.end(), ref.state) - returns.begin());
    auto* undo = &impl->edit->getUndoManager();
    closePluginEditors();
    impl->transaction ("Delete return chain");
    impl->removeMappings (*ref.rack, Impl::chainTargets (ref.state));
    // Later returns move down one place, and so do the chains' sends to them.
    for (auto chain : rackChildren (ref.rack->state, racks::chainType))
    {
        auto settings = racks::readChain (chain);
        for (int k = index; k < maximumRackReturns; ++k) settings.sendsDb[(size_t) k] = k + 1 < maximumRackReturns ? settings.sendsDb[(size_t) k + 1] : -100.0f;
        racks::writeChain (chain, settings, undo);
    }
    for (int i = ref.rack->state.getNumChildren(); --i >= 0;)
    {
        auto child = ref.rack->state.getChild (i);
        if (! child.hasType (racks::mappingType)) continue;
        const auto m = racks::readMapping (child);
        bool isChain = false;
        for (const auto& chain : rackChildren (ref.rack->state, racks::chainType)) isChain = isChain || chain[te::IDs::id].toString() == m.targetId;
        if (! isChain || m.parameter < 2 + index) continue;
        if (m.parameter == 2 + index) ref.rack->state.removeChild (i, undo);
        else child.setProperty ("parameter", m.parameter - 1, undo);
    }
    ref.rack->state.removeChild (ref.state, undo);
    impl->changed(); return Result::ok();
}
Result Session::groupDevices (int trackIndex, const juce::StringArray& ids, String& rackId)
{
    auto* track = impl->track (trackIndex);
    if (track == nullptr) return Result::fail ("Track not found.");
    if (auto r = impl->editable (track); r.failed()) return r;
    std::set<String> wanted (ids.begin(), ids.end());
    if (ids.isEmpty() || (int) wanted.size() != ids.size()) return Result::fail ("Choose devices to group, each once.");
    std::vector<te::Plugin*> devices; bool anyMidi = false;
    for (auto* p : impl->plugins (trackIndex))
    {
        if (! wanted.contains (p->itemID.toString())) continue;
        auto* native = dynamic_cast<NativeEffectPlugin*> (p);
        // Built-in devices and the Utility can go into racks; hosted plug-ins cannot yet.
        const bool midi = native != nullptr && effectIsMidi (native->getKind());
        if (native == nullptr && dynamic_cast<UtilityPlugin*> (p) == nullptr) return Result::fail ("Only built-in devices and the Utility can be grouped into a rack.");
        if (! devices.empty() && midi != anyMidi) return Result::fail ("Group MIDI effects and audio effects separately.");
        anyMidi = midi; devices.push_back (p);
    }
    if (devices.size() != wanted.size()) return Result::fail ("Those devices are not all on this track.");
    return impl->group (*track, devices, anyMidi ? EffectKind::midiEffectRack : EffectKind::audioEffectRack, rackId);
}
Result Session::ungroupRack (const String& rackId)
{
    te::AudioTrack* track = nullptr;
    auto* rack = impl->rack (rackId, &track);
    if (rack == nullptr || track == nullptr || rack->state.getParent() != track->state) return Result::fail ("Ungroup a rack on a track.");
    if (auto r = impl->editable (track); r.failed()) return r;
    if (rack->getKind() == EffectKind::drumRack) return Result::fail ("A Drum Rack's pads stay in the rack.");
    const auto chains = rackChildren (rack->state, racks::chainType);
    if (chains.size() > 1) return Result::fail ("Only a rack with one chain can be ungrouped.");
    std::vector<juce::ValueTree> copies;
    if (! chains.empty())
        for (const auto& device : chainDevices (chains.front()))
        {
            auto copy = device.createCopy();
            te::EditItemID::remapIDs (copy, nullptr, *impl->edit);
            copies.push_back (copy);
        }
    const bool instrument = effectIsInstrument (rack->getKind());
    if (instrument && std::none_of (copies.begin(), copies.end(), [] (const auto& d) { return racks::deviceStage (deviceKind (d)) == 1; }))
        return Result::fail ("The rack's chain has no instrument to put on the track.");
    if (track->pluginList.size() - 1 + (int) copies.size() > 64) return Result::fail ("This track's device chain is full.");
    impl->transaction ("Ungroup rack");
    std::vector<te::Plugin::Ptr> plugins;
    for (const auto& copy : copies)
        if (auto plugin = impl->edit->getPluginCache().createNewPlugin (copy)) plugins.push_back (plugin);
        else return Result::fail ("A device could not be restored.");
    closePluginEditors();
    // The devices take the rack's place in order; chain volume, pan and macro mappings go with the rack.
    int position = track->pluginList.indexOf (rack);
    rack->deleteFromParent();
    for (auto& plugin : plugins) track->pluginList.insertPlugin (plugin, position++, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::groupInstrument (const String& trackId, String& rackId)
{
    auto* track = impl->track (trackId);
    if (track == nullptr) return Result::fail ("Track not found.");
    if (auto r = impl->editable (track); r.failed()) return r;
    auto* instrument = Impl::nativeInstrument (*track);
    if (instrument == nullptr) return Result::fail ("Group a built-in instrument; the Halftrack Synth cannot go into a rack.");
    // The instrument and the built-in audio devices after it become one chain.
    std::vector<te::Plugin*> devices { instrument };
    for (int i = track->pluginList.indexOf (instrument) + 1; i < track->pluginList.size(); ++i)
    {
        auto* plugin = track->pluginList[i];
        auto* native = dynamic_cast<NativeEffectPlugin*> (plugin);
        if (native != nullptr ? (effectIsMidi (native->getKind()) || effectIsInstrument (native->getKind())) : dynamic_cast<UtilityPlugin*> (plugin) == nullptr) break;
        devices.push_back (plugin);
    }
    return impl->group (*track, devices, EffectKind::instrumentRack, rackId);
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
Session::PluginRole Session::pluginRole (const juce::PluginDescription& description)
{
    // A scan may not know a plug-in's channels (VST3 descriptions report none), so a MIDI effect is
    // one whose category says so, as Audio Units' MIDI processors do.
    if (description.isInstrument) return PluginRole::instrument;
    if (description.category.containsIgnoreCase ("MIDI")) return PluginRole::midiEffect;
    return PluginRole::audioEffect;
}
juce::String Session::pluginSummary (const juce::PluginDescription& description)
{
    const auto role = pluginRole (description);
    const auto dot = " " + juce::String::charToString ((juce::juce_wchar) 0x00b7) + " "; // A middle dot.
    auto summary = juce::String (role == PluginRole::instrument ? "Instrument" : role == PluginRole::midiEffect ? "MIDI Effect" : "Audio Effect")
                 + dot + description.pluginFormatName;
    // Channel counts only when the scan knew them.
    if (description.numInputChannels > 0 || description.numOutputChannels > 0)
        summary << dot << description.numInputChannels << " in, " << description.numOutputChannels << " out";
    return summary;
}
Result Session::addPlugin (int index, const juce::PluginDescription& description)
{
    if (auto frozen = impl->unfrozen (impl->track (index)); frozen.failed()) return frozen;
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (! t->pluginList.canInsertPlugin()) return Result::fail ("This track's device chain is full.");
    const auto role = pluginRole (description);
    if (role == PluginRole::midiEffect && ! static_cast<bool> (t->state.getProperty (midiTrackProperty, false))) return Result::fail ("MIDI effect plug-ins go on MIDI tracks.");
    // A plug-in built only for another architecture is explained without loading it: loading one
    // can stop on a system error dialog (Windows) or start translation (Rosetta).
    if (juce::File::isAbsolutePath (description.fileOrIdentifier))
        if (const auto problem = pluginbinary::architectureProblem (juce::File (description.fileOrIdentifier), description.name); problem.isNotEmpty())
            return Result::fail (problem);
    impl->transaction ("Add plug-in");
    // Tracktion resolves its persisted plug-in identity through this registry.
    knownPlugins().addType (description);
    auto plugin = impl->edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, description);
    if (! plugin) return Result::fail ("The plug-in could not be loaded.");
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get());
    if (! external || (! external->getAudioPluginInstance() && ! external->isInitialisingAsync()))
    {
        // A plug-in built for another architecture says so, rather than only failing to load.
        if (juce::File::isAbsolutePath (description.fileOrIdentifier))
            if (const auto problem = pluginbinary::architectureProblem (juce::File (description.fileOrIdentifier), description.name); problem.isNotEmpty())
                return Result::fail (problem);
        return Result::fail (external ? external->getLoadError() : "The plug-in could not be instantiated.");
    }
    if (description.isInstrument)
    {
        // As in the reference, an instrument takes the place of the MIDI track's instrument.
        if (! static_cast<bool> (t->state.getProperty (midiTrackProperty, false))) return Result::fail ("Instrument plug-ins go on MIDI tracks.");
        auto* current = Impl::instrument (*t);
        int index = 0;
        if (current) index = t->pluginList.indexOf (current);
        else for (auto* p : t->pluginList.getPlugins()) if (auto* native = dynamic_cast<NativeEffectPlugin*> (p); native && effectIsMidi (native->getKind())) ++index;
        external->state.setProperty (hostedInstrumentProperty, true, nullptr);
        if (current) { current->windowState->closeWindowExplicitly(); current->deleteFromParent(); }
        t->pluginList.insertPlugin (plugin, index, nullptr);
        impl->instrumentArrived (*t);
        impl->changed(); return Result::ok();
    }
    // As in the reference with no device selected, an effect goes at the end of the chain, like a
    // native effect, and a MIDI effect after the other MIDI effects, ahead of the instrument.
    auto insertIndex = Impl::chainEnd (*t);
    if (role == PluginRole::midiEffect) { auto* source = Impl::instrument (*t); insertIndex = source ? t->pluginList.indexOf (source) : 0; }
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
    plugin->windowState->closeWindowExplicitly(); impl->transaction ("Remove plug-in"); plugin->deleteFromParent(); impl->changed(); return Result::ok();
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
        for (auto* t : te::getAudioTracks (*impl->edit)) for (auto* p : t->pluginList.getPlugins()) p->windowState->closeWindowExplicitly();
}
}
