#include "Session.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Parameter identity: a MIDI mapping and automation keep driving the same device control through
// device and track reordering, track duplication, deletion and undo, and reopening. A hosted
// plug-in's configured parameters and MIDI mappings are kept by the plug-in's own parameter IDs:
// a saved mapping whose index no longer names its parameter follows the ID back to it, one whose
// parameter has gone drives nothing, and an unavailable plug-in keeps both until it returns.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS parameter identity: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
bool near (float a, float b) { return std::abs (a - b) < 1e-4f; }
// Learns a controller for a target, the way MIDI Map Mode does.
void learn (Session& s, const juce::String& target, int controller)
{
    ok (s.setMidiLearnTarget (target), "learn " + target);
    ok (s.handleMidiControl (1, controller, 64), "move controller " + juce::String (controller));
    ok (s.setMidiLearnTarget ({}), "leave learning");
}
const Session::PluginInfo* device (const Session::Snapshot& snapshot, const juce::String& id)
{
    for (const auto& t : snapshot.tracks) for (const auto& p : t.plugins) if (p.id == id) return &p;
    return nullptr;
}
bool mapped (const Session& s, const juce::String& target)
{
    const auto mappings = s.snapshot().midiMappings;
    return std::any_of (mappings.begin(), mappings.end(), [&] (const auto& m) { return m.target == target; });
}
}

int runParameterIdentityTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("parameter-identity-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto project = root.getChildFile ("Identity").getChildFile ("Identity.halftrack");
        Session s (false);
        ok (s.addAudioTrack ("Drums"), "add a track");
        const auto trackIndex = [&] (const juce::String& name)
        { const auto tracks = s.snapshot().tracks; for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i].name == name) return i; return -1; };
        const auto trackId = s.snapshot().tracks[(size_t) trackIndex ("Drums")].id;
        ok (s.addNativeEffect (trackIndex ("Drums"), Session::EffectKind::eqThree), "an EQ Three");
        ok (s.addNativeEffect (trackIndex ("Drums"), Session::EffectKind::saturator), "and a Saturator");
        const auto eq = s.snapshot().tracks[(size_t) trackIndex ("Drums")].plugins[0].id;
        const auto eqKey = "device:" + eq + ":1", volumeKey = "mixer:" + trackId + ":volume";
        const auto range = s.midiTargetRange (eqKey);
        check (range.has_value(), "the EQ's second control can be mapped");
        learn (s, eqKey, 20); learn (s, volumeKey, 21);
        ok (s.setDeviceAutomation (eq, 2, { true, { { 0, 0 }, { 4, 1 } } }), "automate the EQ's third control");
        const auto drives = [&] (int controller, int value) { ok (s.handleMidiControl (1, controller, value), "send controller " + juce::String (controller)); };
        // Each reads a snapshot it keeps alive while it looks at its devices.
        const auto eqValue = [&] { const auto snapshot = s.snapshot(); const auto* p = device (snapshot, eq); return p != nullptr ? p->effect.values[1] : std::nanf (""); };
        const auto eqAutomated = [&] { const auto snapshot = s.snapshot(); const auto* p = device (snapshot, eq); return p != nullptr && p->automation.size() == 1 && p->automation[0].parameter == 2; };
        drives (20, 127);
        check (near (eqValue(), range->second) && mapped (s, eqKey) && mapped (s, volumeKey) && eqAutomated(), "the mapping drives the EQ control to its maximum");

        // Reordering devices and tracks.
        ok (s.movePlugin (trackIndex ("Drums"), 0, 1), "move the EQ after the Saturator");
        drives (20, 0);
        check (near (eqValue(), range->first) && eqAutomated(), "after the device moves, the mapping and automation stay on the EQ");
        ok (s.addAudioTrack ("Bass"), "add another track");
        ok (s.moveTrack (trackId, trackIndex ("Bass")), "move the Drums track below it");
        drives (21, 0);
        check (s.snapshot().tracks[(size_t) trackIndex ("Drums")].gainDb < -60 && std::abs (s.snapshot().tracks[(size_t) trackIndex ("Bass")].gainDb) < .01f,
               "after the track moves, its volume mapping drives it alone");
        drives (21, 100);

        // Duplicating: the copy's devices have their own identities and automation; the mapping stays.
        juce::String copyId;
        ok (s.duplicateTrack (trackId, copyId), "duplicate the Drums track");
        const auto copied = [&] { for (const auto& t : s.snapshot().tracks) if (t.id == copyId) return t; return Session::TrackInfo {}; }();
        juce::String copiedEq;
        for (const auto& p : copied.plugins) if (p.isNativeEffect && p.effect.kind == Session::EffectKind::eqThree) copiedEq = p.id;
        check (copiedEq.isNotEmpty() && copiedEq != eq && device (s.snapshot(), copiedEq)->automation.size() == 1 && ! mapped (s, "device:" + copiedEq + ":1"),
               "the duplicate's EQ is its own, with the automation and without the mapping");
        drives (20, 127);
        check (near (eqValue(), range->second) && near (device (s.snapshot(), copiedEq)->effect.values[1], range->first), "the mapping still drives only the original EQ");

        // Deleting the EQ hides its mapping; undo brings both back.
        ok (s.removePlugin (trackIndex ("Drums"), 1), "delete the EQ");
        check (! mapped (s, eqKey) && s.handleMidiControl (1, 20, 10).wasOk(), "its mapping is hidden and its controller does nothing");
        s.undo();
        drives (20, 0);
        check (mapped (s, eqKey) && near (eqValue(), range->first) && eqAutomated(), "undo restores the EQ, its mapping and automation");
        const auto afterUndo = s.snapshot();
        check (trackIndex ("Bass") >= 0 && std::any_of (afterUndo.tracks.begin(), afterUndo.tracks.end(), [&] (const auto& t) { return t.id == copyId; }),
               "and only the deletion: edits made while a controller moves are undo steps of their own");

        // Reopening keeps every identity.
        ok (s.saveProject (project), "save");
        ok (s.loadProject (project), "reopen");
        drives (20, 127);
        check (mapped (s, eqKey) && mapped (s, volumeKey) && near (eqValue(), range->second) && eqAutomated(), "after reopening the mapping and automation are on the EQ");

        // A hosted plug-in.
        const auto* pluginPath = std::getenv ("HALFTRACK_TEST_PLUGIN");
        check (pluginPath != nullptr, "the VST3 gain fixture is configured");
        juce::OwnedArray<juce::PluginDescription> descriptions;
        for (auto* format : s.pluginFormats().getFormats())
            if (format->getName() == "VST3") format->findAllTypesForFile (descriptions, juce::File (pluginPath).getFullPathName());
        check (! descriptions.isEmpty(), "scan the gain fixture");
        s.knownPlugins().addType (*descriptions[0]);
        ok (s.addPlugin (trackIndex ("Bass"), *descriptions[0]), "insert it on Bass");
        const auto plugin = s.snapshot().tracks[(size_t) trackIndex ("Bass")].plugins.back().id;
        const auto parameters = s.pluginParameters (plugin);
        check (parameters.size() == 2 && parameters[0].name == "Gain" && parameters[1].name == "Bypass", "its parameters are Gain and the wrapper's Bypass");
        const auto configured = [&] { const auto snapshot = s.snapshot(); const auto* p = device (snapshot, plugin); return p != nullptr ? p->configured : std::vector<int> { -1 }; };
        check (s.setPluginConfiguration (plugin, { 2 }).failed() && s.setPluginConfiguration (plugin, { 0, 0 }).failed() && s.setPluginConfiguration (eq, { 0 }).failed()
                   && configured().empty(), "unknown, repeated and native parameters cannot be configured");
        ok (s.setPluginConfiguration (plugin, { 0 }), "configure Gain");
        check (configured() == std::vector<int> { 0 }, "Gain shows in the panel");
        s.undo();
        check (configured().empty(), "one undo hides it");
        s.redo();
        const auto gainKey = "device:" + plugin + ":0";
        check (s.midiTargetRange (gainKey) == std::pair { 0.0f, 1.0f }, "Gain can be mapped, from 0 to 1");
        learn (s, gainKey, 30);
        const auto gain = [&] { return s.pluginParameters (plugin)[0].value; };
        const auto bypass = [&] { return s.pluginParameters (plugin)[1].value; };
        const auto bypassBefore = bypass();
        drives (30, 127);
        check (near (gain(), 1) && near (bypass(), bypassBefore), "the controller drives Gain");
        drives (30, 0);
        check (near (gain(), 0), "and back to 0");
        ok (s.setPluginParameterValue (plugin, 0, .25f), "set Gain from its panel control");
        check (near (gain(), .25f) && s.setPluginParameterValue (plugin, 2, .5f).failed(), "a panel control sets its parameter; unknown ones are refused");
        ok (s.saveProject (project), "save the plug-in project");
        const auto saved = project.loadFileAsString();
        const auto gainId = [&]
        {
            const auto xml = juce::parseXML (saved);
            std::function<juce::String (const juce::XmlElement&)> find = [&] (const juce::XmlElement& e) -> juce::String
            {
                if (e.getTagName() == "MAPPING" && e.getStringAttribute ("target") == gainKey) return e.getStringAttribute ("parameterID");
                for (const auto* child : e.getChildIterator()) if (const auto id = find (*child); id.isNotEmpty()) return id;
                return {};
            };
            return xml ? find (*xml) : juce::String();
        }();
        check (gainId.isNotEmpty() && saved.contains ("HALFTRACKCONFIGURE") && saved.contains ("id=\"" + gainId + "\""), "the mapping and configuration keep Gain's own ID (" + gainId + ")");

        // A saved index that no longer names its parameter: the ID leads back to Gain.
        const auto moved = root.getChildFile ("Moved.halftrack");
        check (moved.replaceWithText (saved.replace ("target=\"" + gainKey + "\"", "target=\"device:" + plugin + ":1\"")), "write a project whose mapping index moved");
        ok (s.loadProject (moved), "open it");
        drives (30, 127);
        check (mapped (s, gainKey) && ! mapped (s, "device:" + plugin + ":1") && near (gain(), 1) && near (bypass(), bypassBefore) && configured() == std::vector<int> { 0 },
               "the mapping follows Gain's ID back to Gain and leaves Bypass alone");
        // A parameter that has gone drives nothing.
        const auto gone = root.getChildFile ("Gone.halftrack");
        check (gone.replaceWithText (saved.replace ("parameterID=\"" + gainId + "\"", "parameterID=\"no-such-parameter\"")), "write a project whose mapped parameter has gone");
        ok (s.loadProject (gone), "open it");
        const auto before = gain();
        drives (30, 127);
        check (! mapped (s, gainKey) && near (gain(), before) && near (bypass(), bypassBefore), "its mapping is hidden and drives nothing");
        // An unavailable plug-in keeps its configuration and mapping until it returns.
        s.knownPlugins().clear();
        ok (s.loadProject (project), "open the project with the plug-in unscanned");
        {
            const auto snapshot = s.snapshot();
            check (device (snapshot, plugin) != nullptr && device (snapshot, plugin)->missing && ! mapped (s, gainKey), "the plug-in is a placeholder and its mapping is hidden");
        }
        const auto kept = root.getChildFile ("Kept.halftrack");
        ok (s.saveProject (kept), "save it");
        s.knownPlugins().addType (*descriptions[0]);
        ok (s.loadProject (kept), "reopen once the plug-in is scanned");
        drives (30, 127);
        check (mapped (s, gainKey) && near (gain(), 1) && configured() == std::vector<int> { 0 }, "the configuration and mapping return with it");
        // A damaged configuration is refused.
        const auto damaged = root.getChildFile ("Damaged.halftrack");
        check (damaged.replaceWithText (saved.replace ("<PARAMETER id=\"" + gainId + "\"/>", "<PARAMETER id=\"" + gainId + "\"/><PARAMETER id=\"" + gainId + "\"/>")),
               "write a project configuring Gain twice");
        check (s.loadProject (damaged).failed() && mapped (s, gainKey), "it is refused, keeping the open project");
        std::cout << "Parameter identity passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL parameter identity: " << e.what() << std::endl; return 1; }
}
}
