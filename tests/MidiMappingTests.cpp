#include "NativeEffects.h"
#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// MIDI mappings. Learning maps the next controller to a control; mapped controllers then move
// track volume and pan, the master and device controls along their fader or knob taper between
// each mapping's ends, in one undo step per burst. Mappings reopen with the project, damaged ones
// are rejected, and a virtual MIDI controller drives them through the real input path.
namespace halftrack
{
namespace te = tracktion::engine;
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
using Target = Session::AutomationTarget;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS MIDI mapping: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
bool near (double a, double b, double tolerance = 1e-3) { return std::abs (a - b) <= tolerance; }
// The control value a controller value gives along a fader taper between two ends.
float alongVolume (float low, float high, int value)
{
    const auto a = Session::automationValueToProportion (Target::volume, low), b = Session::automationValueToProportion (Target::volume, high);
    return Session::automationProportionToValue (Target::volume, a + (b - a) * value / 127.0f);
}
const Session::MidiMapping* find (const std::vector<Session::MidiMapping>& mappings, const juce::String& target)
{
    for (const auto& m : mappings) if (m.target == target) return &m;
    return nullptr;
}
void rest() { juce::MessageManager::getInstance()->runDispatchLoopUntil (700); }
}

int runMidiMappingTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("midi-mapping-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false);
        const auto trackId = s.snapshot().tracks[0].id;
        const auto volume = "mixer:" + trackId + ":volume", pan = "mixer:" + trackId + ":pan";
        check (s.midiTargetRange (volume) == std::pair { -100.0f, 6.0f } && s.midiTargetRange (pan) == std::pair { -1.0f, 1.0f }
               && s.midiTargetRange ("master") == std::pair { -100.0f, 6.0f } && ! s.midiTargetRange ("mixer:" + trackId + ":width")
               && ! s.midiTargetRange ("mixer:missing:volume") && ! s.midiTargetRange ("bogus"), "targets report their ranges and unknown ones nothing");
        check (s.setMidiLearnTarget ("bogus").failed() && s.snapshot().midiLearnTarget.isEmpty(), "unknown targets cannot be learned");

        // Learning.
        ok (s.setMidiLearnTarget (volume), "learn the first track's volume");
        ok (s.handleMidiControl (1, 7, 100), "move CC 7 on channel 1");
        auto mappings = s.snapshot().midiMappings;
        check (mappings.size() == 1 && mappings[0] == Session::MidiMapping { volume, 1, 7, -100, 6 } && near (s.snapshot().tracks[0].gainDb, 0)
               && s.snapshot().midiLearnTarget == volume, "the controller maps to the whole volume range and moves nothing while learning");
        ok (s.setMidiLearnTarget (pan), "learn pan");
        ok (s.handleMidiControl (1, 7, 30), "move CC 7 again");
        mappings = s.snapshot().midiMappings;
        check (mappings.size() == 1 && mappings[0].target == pan, "a controller drives one target: it moves to pan");
        ok (s.handleMidiControl (1, 10, 0), "move CC 10");
        mappings = s.snapshot().midiMappings;
        check (mappings.size() == 1 && mappings[0].controller == 10, "a target has one controller: pan moves to CC 10");
        ok (s.setMidiLearnTarget (volume), "learn volume again"); ok (s.handleMidiControl (1, 7, 0), "with CC 7");
        ok (s.setMidiLearnTarget ({}), "end learning");
        check (s.snapshot().midiMappings.size() == 2 && s.snapshot().midiLearnTarget.isEmpty(), "two mappings, learning over");

        // Moving mapped controls.
        ok (s.handleMidiControl (1, 7, 127), "CC 7 at 127"); check (near (s.snapshot().tracks[0].gainDb, 6), "full scale is +6 dB");
        ok (s.handleMidiControl (1, 7, 64), "CC 7 at 64"); check (near (s.snapshot().tracks[0].gainDb, alongVolume (-100, 6, 64)), "the middle follows the fader taper");
        ok (s.handleMidiControl (1, 7, 0), "CC 7 at 0"); check (s.snapshot().tracks[0].gainDb <= -99.9f, "zero is the bottom of the fader");
        ok (s.handleMidiControl (1, 10, 0), "CC 10 at 0"); check (near (s.snapshot().tracks[0].pan, -1), "pan reaches hard left");
        ok (s.handleMidiControl (1, 10, 127), "CC 10 at 127"); check (near (s.snapshot().tracks[0].pan, 1), "and hard right");
        const auto before = s.snapshot().tracks[0].gainDb;
        ok (s.handleMidiControl (2, 7, 127), "CC 7 on channel 2"); ok (s.handleMidiControl (1, 8, 127), "CC 8 on channel 1");
        check (s.snapshot().tracks[0].gainDb == before, "other channels and controllers move nothing");
        check (s.handleMidiControl (0, 7, 1).failed() && s.handleMidiControl (1, 128, 1).failed() && s.handleMidiControl (1, 7, 128).failed(), "malformed messages are refused");

        // Ranges.
        ok (s.setMidiMappingRange (volume, -24, 0), "range -24 to 0 dB");
        ok (s.handleMidiControl (1, 7, 127), "CC 7 at 127"); check (near (s.snapshot().tracks[0].gainDb, 0), "full scale reaches the range's maximum");
        ok (s.handleMidiControl (1, 7, 0), "CC 7 at 0"); check (near (s.snapshot().tracks[0].gainDb, -24), "zero its minimum");
        ok (s.setMidiMappingRange (pan, .5f, -.5f), "an inverted pan range");
        ok (s.handleMidiControl (1, 10, 127), "CC 10 at 127"); check (near (s.snapshot().tracks[0].pan, -.5f), "an inverted range turns the other way");
        check (s.setMidiMappingRange (volume, -200, 0).failed() && s.setMidiMappingRange ("master", -6, 0).failed(), "ranges beyond the control, or without a mapping, are refused");
        rest();

        // One undo step per burst.
        ok (s.handleMidiControl (1, 7, 64), "set a starting level"); rest();
        const auto start = s.snapshot().tracks[0].gainDb;
        for (const int value : { 70, 80, 90, 100, 110 }) ok (s.handleMidiControl (1, 7, value), "a burst of moves");
        rest();
        check (! near (s.snapshot().tracks[0].gainDb, start), "the burst moves the fader");
        s.undo(); check (near (s.snapshot().tracks[0].gainDb, start), "one undo restores the level before the burst");
        s.redo();

        // Device controls and the master.
        ok (s.addNativeEffect (0, Session::EffectKind::channelEq), "add a Channel EQ");
        const auto eq = s.snapshot().tracks[0].plugins.back().id;
        const int mid = effectParameterIndex (Session::EffectKind::channelEq, "midDb"), hp = effectParameterIndex (Session::EffectKind::channelEq, "highPass");
        const auto midTarget = "device:" + eq + ":" + juce::String (mid), hpTarget = "device:" + eq + ":" + juce::String (hp);
        check (s.midiTargetRange (midTarget) == std::pair { -12.0f, 12.0f }, "a device control's range is its own");
        ok (s.setMidiLearnTarget (midTarget), "learn Mid"); ok (s.handleMidiControl (3, 20, 0), "with CC 20 on channel 3");
        ok (s.setMidiLearnTarget (hpTarget), "learn HP 80 Hz"); ok (s.handleMidiControl (3, 21, 0), "with CC 21");
        ok (s.setMidiLearnTarget ("master"), "learn the master"); ok (s.handleMidiControl (3, 22, 0), "with CC 22");
        ok (s.setMidiLearnTarget ({}), "end learning");
        ok (s.handleMidiControl (3, 20, 127), "CC 20 at 127");
        const auto effect = [&] { return s.snapshot().tracks[0].plugins.back().effect; };
        check (near (effect().values[(size_t) mid], 12), "full scale is +12 dB");
        ok (s.handleMidiControl (3, 20, 0), "CC 20 at 0"); check (near (effect().values[(size_t) mid], -12), "zero is -12 dB");
        ok (s.handleMidiControl (3, 21, 127), "CC 21 at 127"); check (effect().values[(size_t) hp] == 1, "a switch turns on at the top");
        ok (s.handleMidiControl (3, 21, 10), "CC 21 at 10"); check (effect().values[(size_t) hp] == 0, "and off near the bottom");
        ok (s.handleMidiControl (3, 22, 127), "CC 22 at 127"); check (near (s.snapshot().masterGainDb, 6), "the master follows its controller");
        rest();

        // Automation: a mapped controller overrides a playing lane like the mouse does.
        ok (s.setTrackAutomation (trackId, Target::volume, { true, { { 0, -10 }, { 8, -10 } } }), "automate the volume");
        ok (s.handleMidiControl (1, 7, 100), "move the mapped controller");
        check (! s.snapshot().tracks[0].volumeAutomation.enabled && s.snapshot().automationOverridden, "the controller overrides the automation");
        rest();

        // Removal and reopening.
        ok (s.removeMidiMapping (hpTarget), "remove the HP mapping");
        ok (s.handleMidiControl (3, 21, 127), "CC 21 at 127"); check (effect().values[(size_t) hp] == 0 && s.snapshot().midiMappings.size() == 4, "removed mappings move nothing");
        check (s.removeMidiMapping (hpTarget).failed(), "a control without a mapping cannot lose one");
        const auto saved = s.snapshot().midiMappings;
        const auto project = root.getChildFile ("Mapped").getChildFile ("Mapped.halftrack");
        ok (project.getParentDirectory().createDirectory(), "create the project folder");
        ok (s.setMidiLearnTarget (pan), "leave learning on");
        ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
        check (s.snapshot().midiMappings == saved && s.snapshot().midiLearnTarget.isEmpty(), "the mappings reopen and learning is off");
        ok (s.handleMidiControl (3, 20, 127), "CC 20 after reopening"); check (near (effect().values[(size_t) mid], 12), "they still drive their controls");
        const auto corrupt = [&] (const juce::String& name, const std::function<void (juce::XmlElement&)>& change)
        {
            auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "parse the project");
            change (*xml->getChildByName ("EDIT")->getChildByName ("HALFTRACKMIDIMAP"));
            const auto damaged = project.getSiblingFile (name + ".halftrack"); check (xml->writeTo (damaged), "write " + name);
            check (s.loadProject (damaged).failed() && s.snapshot().midiMappings == saved, name + " is rejected and the open project stays");
        };
        corrupt ("Channel", [] (juce::XmlElement& m) { m.getFirstChildElement()->setAttribute ("channel", 17); });
        corrupt ("Controller", [] (juce::XmlElement& m) { m.getFirstChildElement()->setAttribute ("controller", "seven"); });
        corrupt ("Target", [] (juce::XmlElement& m) { m.getFirstChildElement()->setAttribute ("target", "bogus"); });
        corrupt ("Shared", [] (juce::XmlElement& m) { m.addChildElement (new juce::XmlElement (*m.getFirstChildElement())); });
        // A deleted device's mappings drop out of the list and do nothing.
        ok (s.removePlugin (0, 0), "delete the Channel EQ");
        check (s.snapshot().tracks[0].plugins.empty() && s.snapshot().midiMappings.size() == 3 && s.handleMidiControl (3, 20, 0).wasOk(), "a deleted device's mappings drop out and are inert");

        // A virtual controller through the real MIDI input path.
        {
            Session live (false, makeSyntheticTestDeviceType());
            auto& devices = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (live.audioDeviceManager()).engine.getDeviceManager();
            const auto name = "Halftrack remote " + juce::Uuid().toString();
            ok (devices.createVirtualMidiDevice (name), "create a virtual controller");
            devices.rescanMidiDeviceListSynchronously();
            std::shared_ptr<te::VirtualMidiInputDevice> midi;
            for (auto device : devices.getMidiInDevices()) if (device->getName() == name) midi = std::dynamic_pointer_cast<te::VirtualMidiInputDevice> (device);
            check (midi != nullptr, "the controller is discovered");
            midi->setMIDIInputSourceDevices ({});
            const auto send = [&] (int value)
            {
                auto message = juce::MidiMessage::controllerEvent (1, 7, value);
                message.setTimeStamp (devices.getCurrentStreamTime() - midi->getAdjustSecs());
                midi->handleIncomingMidiMessage (message, midi->getMPESourceID());
                for (int i = 0; i < 20; ++i) pumpSyntheticTestDevice (live, 4);
            };
            pumpSyntheticTestDevice (live, 8);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1200); // The remote attaches to new inputs within a second.
            const auto liveVolume = "mixer:" + live.snapshot().tracks[0].id + ":volume";
            ok (live.setMidiLearnTarget (liveVolume), "learn the live track's volume");
            send (5);
            check (live.snapshot().midiMappings.size() == 1 && live.snapshot().midiMappings[0].controller == 7, "the controller's first move maps it");
            ok (live.setMidiLearnTarget ({}), "end learning");
            send (127);
            check (near (live.snapshot().tracks[0].gainDb, 6), "its moves drive the fader");
            live.stop();
            devices.deleteVirtualMidiDevice (*midi); devices.rescanMidiDeviceListSynchronously();
        }
        std::cout << "MIDI mapping passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL MIDI mapping: " << e.what() << std::endl; return 1; }
}
}
