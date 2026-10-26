#include "RemoteMapping.h"
#include "NativeEffects.h"
#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// MIDI mappings. Learning maps the next controller or note to a control; mapped controllers then
// move track volume and pan, the master and device controls along their fader or knob taper between
// each mapping's ends, in one undo step per burst. Mapped notes press and release clip slots with
// their note-on and note-off, toggle switches and switch controls between their ends. Mappings
// reopen with the project, damaged ones are rejected, and a virtual MIDI controller, its Remote switch on,
// drives them through the real input path, where mapped notes stop acting as track input.
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
// A controller rests after half a second; allow for a loaded machine's timers.
void rest() { juce::MessageManager::getInstance()->runDispatchLoopUntil (1500); }
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
        corrupt ("Note flag", [] (juce::XmlElement& m) { m.getFirstChildElement()->setAttribute ("note", 2); });
        // A deleted device's mappings drop out of the list and do nothing.
        ok (s.removePlugin (0, 0), "delete the Channel EQ");
        check (s.snapshot().tracks[0].plugins.empty() && s.snapshot().midiMappings.size() == 3 && s.handleMidiControl (3, 20, 0).wasOk(), "a deleted device's mappings drop out and are inert");

        // Notes.
        {
            Session n (false, makeSyntheticTestDeviceType());
            const auto first = n.snapshot().tracks[0].id;
            const auto mute = "track:" + first + ":mute", firstVolume = "mixer:" + first + ":volume";
            ok (n.setMidiLearnTarget (mute), "learn the activator");
            ok (n.handleMidiNote (1, 60, 0), "a note-off while learning");
            check (n.snapshot().midiMappings.empty(), "maps nothing: a note maps as it goes down");
            ok (n.handleMidiNote (1, 60, 90), "play C3");
            check (n.snapshot().midiMappings.size() == 1 && n.snapshot().midiMappings[0] == Session::MidiMapping { mute, 1, 60, 0, 1, true }
                       && ! n.snapshot().tracks[0].mute, "C3 maps to the activator as a note and switches nothing while learning");
            n.undo(); check (n.snapshot().midiMappings.empty(), "learning a note is one undo step"); n.redo();
            ok (n.setMidiLearnTarget (firstVolume), "learn the volume");
            ok (n.handleMidiControl (1, 60, 10), "with controller 60 on the same channel");
            ok (n.setMidiLearnTarget ({}), "end learning");
            check (n.snapshot().midiMappings.size() == 2, "a note and a controller with the same number are different mappings");
            check (n.handleMidiNote (1, 60, 100).wasOk() && n.snapshot().tracks[0].mute, "the note-on switches the track off");
            check (n.handleMidiNote (1, 60, 0).wasOk() && n.snapshot().tracks[0].mute, "the note-off leaves it");
            check (n.handleMidiNote (1, 60, 100).wasOk() && ! n.snapshot().tracks[0].mute, "the next note-on switches it on again");
            ok (n.handleMidiNote (2, 60, 100), "C3 on channel 2"); ok (n.handleMidiNote (1, 61, 100), "C#3 on channel 1");
            check (! n.snapshot().tracks[0].mute, "other channels and notes switch nothing");
            check (n.handleMidiNote (0, 60, 1).failed() && n.handleMidiNote (1, 128, 1).failed() && n.handleMidiNote (1, 60, 128).failed(), "malformed notes are refused");
            ok (n.setMidiLearnTarget (firstVolume), "learn the volume again");
            ok (n.handleMidiNote (1, 62, 100), "with D3");
            ok (n.setMidiLearnTarget ({}), "end learning");
            const auto noteMappings = n.snapshot().midiMappings;
            const auto volumeMapping = find (noteMappings, firstVolume);
            check (volumeMapping != nullptr && volumeMapping->note && volumeMapping->controller == 62 && noteMappings.size() == 2,
                   "a target has one mapping: the note replaces the controller");
            ok (n.setMidiMappingRange (firstVolume, -12, 0), "range -12 to 0 dB");
            check (n.handleMidiNote (1, 62, 30).wasOk() && near (n.snapshot().tracks[0].gainDb, -12), "from at or above the range's midpoint a note sends the control to Min");
            check (n.handleMidiNote (1, 62, 0).wasOk() && near (n.snapshot().tracks[0].gainDb, -12), "its note-off leaves it there");
            check (n.handleMidiNote (1, 62, 127).wasOk() && near (n.snapshot().tracks[0].gainDb, 0), "and from below it, to Max, whatever the velocity");
            // A choice steps to the next and wraps; a two-state switch toggles.
            ok (n.addNativeEffect (0, Session::EffectKind::equalizer), "add an Eight-band EQ");
            const auto eight = n.snapshot().tracks[0].plugins.back().id;
            const int bandType = effectParameterIndex (Session::EffectKind::equalizer, "band1Type");
            const auto typeTarget = "device:" + eight + ":" + juce::String (bandType);
            const auto bandValue = [&] { return n.snapshot().tracks[0].plugins.back().effect.values[(size_t) bandType]; };
            ok (n.setMidiLearnTarget (typeTarget), "learn band 1's mode"); ok (n.handleMidiNote (1, 64, 100), "with E3"); ok (n.setMidiLearnTarget ({}), "end learning");
            check (bandValue() == 1 && n.handleMidiNote (1, 64, 100).wasOk() && bandValue() == 2, "a note steps a choice to the next, from Bell to Low shelf");
            ok (n.setMidiMappingRange (typeTarget, 1, 3), "a range of the second to fourth modes");
            std::vector<float> steps;
            for (int i = 0; i < 3; ++i) { ok (n.handleMidiNote (1, 64, 100), "step"); steps.push_back (bandValue()); }
            check (steps == std::vector<float> { 3, 1, 2 }, "the steps run to Max and wrap back to Min");
            ok (n.addNativeEffect (0, Session::EffectKind::channelEq), "add a Channel EQ");
            const auto channel = n.snapshot().tracks[0].plugins.back().id;
            const int highPass = effectParameterIndex (Session::EffectKind::channelEq, "highPass");
            const auto hpTarget = "device:" + channel + ":" + juce::String (highPass);
            const auto hpValue = [&] { return n.snapshot().tracks[0].plugins.back().effect.values[(size_t) highPass]; };
            ok (n.setMidiLearnTarget (hpTarget), "learn HP 80 Hz"); ok (n.handleMidiNote (1, 65, 100), "with F3"); ok (n.setMidiLearnTarget ({}), "end learning");
            check (n.handleMidiNote (1, 65, 100).wasOk() && hpValue() == 1 && n.handleMidiNote (1, 65, 100).wasOk() && hpValue() == 0, "a note toggles a two-state switch");

            // Clip slots follow the clip's Launch Mode: Gate plays while the note or controller is held.
            ok (n.addMidiTrack ("Keys"), "add a MIDI track");
            const int keys = (int) n.snapshot().tracks.size() - 1;
            const auto keysId = n.snapshot().tracks[(size_t) keys].id;
            ok (n.addMidiClipToSlot (keysId, 0, 4), "a Session clip in the first scene");
            ok (n.addMidiClipToSlot (keysId, 1, 4), "and in the second");
            for (int scene : { 0, 1 })
                ok (n.setClipLaunchSettings (n.snapshot().tracks[(size_t) keys].slots[(size_t) scene].clip.id, { Session::LaunchMode::gate, Session::ClipQuantisation::none, false }),
                    "Gate, unquantized");
            const auto scenes = n.snapshot().scenes;
            ok (n.setMidiLearnTarget ("slot:" + keysId + ":" + scenes[0].id), "learn the first slot"); ok (n.handleMidiNote (1, 48, 100), "with C2");
            ok (n.setMidiLearnTarget ("slot:" + keysId + ":" + scenes[1].id), "learn the second"); ok (n.handleMidiControl (1, 41, 127), "with controller 41");
            ok (n.setMidiLearnTarget ({}), "end learning");
            const auto playing = [&] (int scene) { return n.snapshot().tracks[(size_t) keys].slots[(size_t) scene].playing; };
            const auto settle = [&] (int scene, bool want) { for (int i = 0; i < 2000 && playing (scene) != want; ++i) pumpSyntheticTestDevice (n, 1); return playing (scene) == want; };
            check (n.handleMidiNote (1, 48, 100).wasOk() && settle (0, true), "C2's note-on launches the Gate clip");
            check (n.handleMidiNote (1, 48, 0).wasOk() && settle (0, false), "and its note-off stops it");
            check (n.handleMidiControl (1, 41, 100).wasOk() && settle (1, true), "controller 41 rising above 63 launches the other");
            check (n.handleMidiControl (1, 41, 63).wasOk() && settle (1, false), "and falling to 63 stops it, as a note-off would");
            n.stop();

            // Reopening.
            const auto project = root.getChildFile ("Notes").getChildFile ("Notes.halftrack");
            ok (project.getParentDirectory().createDirectory(), "create the notes project folder");
            const auto notes = n.snapshot().midiMappings;
            ok (n.saveProject (project), "save the note mappings");
            Session reopened (false);
            ok (reopened.loadProject (project), "reopen them");
            check (reopened.snapshot().midiMappings == notes, "note mappings reopen as notes");
            const auto damaged = project.getSiblingFile ("Clash.halftrack");
            auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "parse the notes project");
            auto* map = xml->getChildByName ("EDIT")->getChildByName ("HALFTRACKMIDIMAP");
            auto* clash = new juce::XmlElement (*map->getFirstChildElement());
            clash->setAttribute ("target", "master"); map->addChildElement (clash);
            check (xml->writeTo (damaged) && reopened.loadProject (damaged).failed() && reopened.snapshot().midiMappings == notes,
                   "two mappings of one note are refused, keeping the open project");
        }

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
            check (! live.isMidiInputRemote (name), "a new input's Remote switch is off, as the reference's is");
            ok (live.setMidiLearnTarget (liveVolume), "learn the live track's volume");
            send (4);
            check (live.snapshot().midiMappings.empty(), "with Remote off, the controller maps nothing");
            ok (live.setMidiInputRemote (name, true), "switch its Remote on");
            check (live.isMidiInputRemote (name) && live.setMidiInputRemote ("No such input", true).failed(), "it is on, and an unknown input has none");
            send (5);
            check (live.snapshot().midiMappings.size() == 1 && live.snapshot().midiMappings[0].controller == 7, "the controller's first move with Remote on maps it");
            ok (live.setMidiLearnTarget ({}), "end learning");
            send (127);
            check (near (live.snapshot().tracks[0].gainDb, 6), "its moves drive the fader");

            // Mapped notes stop acting as track input; others still play the armed track.
            ok (live.addMidiTrack ("Keys"), "add a MIDI track");
            const int keys = (int) live.snapshot().tracks.size() - 1;
            ok (live.setTrackInput (keys, name), "route the controller to it");
            ok (live.setTrackArmed (keys, true), "arm it");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1200);
            const auto note = [&] (int pitch, int velocity)
            {
                auto message = velocity > 0 ? juce::MidiMessage::noteOn (1, pitch, (juce::uint8) velocity) : juce::MidiMessage::noteOff (1, pitch);
                message.setTimeStamp (devices.getCurrentStreamTime() + 256.0 / 48000 - midi->getAdjustSecs());
                midi->handleIncomingMidiMessage (message, midi->getMPESourceID());
                for (int i = 0; i < 10; ++i) pumpSyntheticTestDevice (live, 4);
            };
            const auto mute = "track:" + live.snapshot().tracks[0].id + ":mute";
            ok (live.setMidiLearnTarget (mute), "learn the first track's activator");
            note (62, 100); note (62, 0);
            ok (live.setMidiLearnTarget ({}), "end learning");
            const auto liveMappings = live.snapshot().midiMappings;
            check (find (liveMappings, mute) != nullptr && find (liveMappings, mute)->note, "a played note maps through the input");
            check (! live.hasCapturedMidi(), "the note played while learning did not reach the armed track");
            note (62, 100); note (62, 0);
            check (live.snapshot().tracks[0].mute && ! live.hasCapturedMidi(), "the mapped note switches the activator and does not reach the track");
            note (60, 100); note (60, 0);
            check (live.hasCapturedMidi(), "an unmapped note still plays the track");
            // With Track off the input plays no track, though its mappings still act.
            check (live.isMidiInputTrack (name), "an input's Track switch is on by default, as the reference's is");
            ok (live.setMidiInputTrack (name, false), "switch the controller's Track off");
            check (! live.inputNames (true).contains (name), "tracks are not offered it");
            note (64, 100); note (64, 0);
            note (62, 100); note (62, 0);
            check (! live.snapshot().tracks[0].mute, "its mapped note still switches the activator back on");
            ok (live.setMidiInputTrack (name, true), "switch its Track on again");
            juce::StringArray created;
            ok (live.captureMidi (Session::CaptureDestination::arrangement, created), "capture what the track received");
            const auto snapshot = live.snapshot();
            std::vector<int> pitches;
            for (const auto& clip : snapshot.tracks[(size_t) keys].clips) for (const auto& n : clip.notes) pitches.push_back (n.pitch);
            check (pitches == std::vector<int> { 60 }, "the track received only the unmapped note");
            live.stop();
            devices.deleteVirtualMidiDevice (*midi); devices.rescanMidiDeviceListSynchronously();
        }
        {
            // A send maps too, moving its knob along the send law from Off to 0 dB.
            Session sends (false);
            ok (sends.addReturnTrack ("Echo"), "add a return");
            const auto echo = sends.snapshot().tracks.back().id;
            const auto target = "send:" + sends.snapshot().tracks[0].id + ":" + echo;
            check (sends.midiTargetRange (target) == std::pair { -100.0f, 0.0f } && ! sends.midiTargetRange ("send:" + echo + ":" + echo),
                   "a send's range is Off to 0 dB, and a return has no send to itself");
            ok (sends.setMidiLearnTarget (target), "learn the send"); ok (sends.handleMidiControl (1, 12, 0), "with CC 12"); ok (sends.setMidiLearnTarget ({}), "end learning");
            const auto level = [&] { return sends.snapshot().tracks[0].sends.at (0).gainDb; };
            ok (sends.handleMidiControl (1, 12, 64), "CC 12 at 64");
            check (near (level(), Session::sendProportionToValue (64 / 127.0f)) && level() > -21 && level() < -19, "the middle follows the send law (" + juce::String (level(), 2) + " dB)");
            ok (sends.handleMidiControl (1, 12, 127), "CC 12 at 127"); check (near (level(), 0), "full scale is 0 dB");
            ok (sends.handleMidiControl (1, 12, 0), "CC 12 at 0"); check (level() == -100, "zero turns the send Off");
            ok (sends.setKeyLearnTarget (target), "learn the send for a key"); bool used = false; ok (sends.handleMappedKey ('j', used), "with J");
            ok (sends.setKeyLearnTarget ({}), "end key learning");
            const auto project = root.getChildFile ("Mapped send").getChildFile ("Mapped send.halftrack");
            ok (sends.saveProject (project), "save the send's mappings");
            Session reopened (false);
            ok (reopened.loadProject (project), "reopen them");
            check (reopened.snapshot().midiMappings.size() == 1 && reopened.snapshot().midiMappings[0].target == target
                       && reopened.snapshot().keyMappings.size() == 1 && reopened.snapshot().keyMappings[0].target == target,
                   "the send's MIDI and key mappings reopen");
            ok (reopened.handleMidiControl (1, 12, 127), "CC 12 at 127 after reopening");
            check (near (reopened.snapshot().tracks[0].sends.at (0).gainDb, 0), "the reopened mapping moves the send");
        }
        {
            // Takeover Mode, ported from the research model of the reference's arithmetic: each case gives the model's own
            // single-precision result, from the control's value now, the controller's previous value and its value now.
            struct Case { const char* name; remotemap::Takeover mode; float from, to, current; std::optional<int> prior; int value; float expected; bool pickUp; };
            using M = remotemap::Takeover;
            const Case cases[] {
                { "None jumps", M::none, 0x0.0p+0f, 0x1.0p+0f, 0x1.333334p-2f, 10, 100, 0x1.93264cp-1f, false },
                { "Pick-Up waits", M::pickUp, 0x0.0p+0f, 0x1.0p+0f, 0x1.0p-1f, 10, 20, 0x1.0p-1f, true },
                { "Pick-Up takes a controller crossing the value", M::pickUp, 0x0.0p+0f, 0x1.0p+0f, 0x1.0p-1f, 60, 70, 0x1.1a3468p-1f, true },
                { "Pick-Up takes one within two steps", M::pickUp, 0x0.0p+0f, 0x1.0p+0f, 0x1.0p-1f, 0, 63, 0x1.fbf7fp-2f, true },
                { "Pick-Up waits for one above", M::pickUp, 0x0.0p+0f, 0x1.0p+0f, 0x1.0p-1f, 100, 66, 0x1.0p-1f, true },
                { "Pick-Up waits with no previous value", M::pickUp, 0x0.0p+0f, 0x1.0p+0f, 0x1.0p-1f, std::nullopt, 100, 0x1.0p-1f, false },
                { "a controller in step follows", M::pickUp, 0x0.0p+0f, 0x1.0p+0f, 0x0.0p+0f, 0, 90, 0x1.6ad5acp-1f, false },
                { "Value Scaling moves up in proportion", M::valueScaling, 0x0.0p+0f, 0x1.0p+0f, 0x1.0p-2f, 0, 64, 0x1.418306p-1f, false },
                { "and down", M::valueScaling, 0x0.0p+0f, 0x1.0p+0f, 0x1.8p-1f, 127, 64, 0x1.83060cp-2f, false },
                { "Value Scaling leaves a repeated value", M::valueScaling, 0x0.0p+0f, 0x1.0p+0f, 0x1.8p-1f, 40, 40, 0x1.8p-1f, false },
                { "Value Scaling follows within two steps", M::valueScaling, 0x0.0p+0f, 0x1.0p+0f, 0x1.0p-1f, 20, 64, 0x1.020408p-1f, false },
                { "Value Scaling in a narrower range", M::valueScaling, 0x1.99999ap-3f, 0x1.99999ap-1f, 0x1.666666p-1f, 10, 30, 0x1.6f26f2p-1f, false },
                { "Value Scaling reversed", M::valueScaling, 0x1.99999ap-1f, 0x1.99999ap-3f, 0x1.333334p-2f, 10, 30, 0x1.21b21cp-2f, false },
                { "Pick-Up reversed waits", M::pickUp, 0x1.99999ap-1f, 0x1.99999ap-3f, 0x1.333334p-2f, 10, 30, 0x1.333334p-2f, true },
                { "Pick-Up keeps a value outside the range at its end", M::pickUp, 0x1.99999ap-3f, 0x1.99999ap-1f, 0x1.e66666p-1f, 10, 30, 0x1.99999ap-1f, true },
                { "Value Scaling reaches the top with the controller", M::valueScaling, 0x0.0p+0f, 0x1.0p+0f, 0x1.333334p-2f, 100, 127, 0x1.0p+0f, false },
                { "and the bottom", M::valueScaling, 0x0.0p+0f, 0x1.0p+0f, 0x1.666666p-1f, 30, 0, 0x0.0p+0f, false },
            };
            for (const auto& c : cases)
            {
                const auto move = remotemap::takeover (c.mode, c.current, c.prior ? std::optional<float> ((float) *c.prior) : std::nullopt, (float) c.value,
                                                   std::min (c.from, c.to), std::max (c.from, c.to), c.to < c.from);
                check (move.value == c.expected && move.pickUp == c.pickUp, juce::String ("takeover: ") + c.name + ", " + juce::String (move.value, 7) + " as the model's");
            }
            // In a Session: the volume mapped to CC 7 of one input, then set to -12 dB with the mouse.
            Session t (false);
            const auto volumeTarget = "mixer:" + t.snapshot().tracks[0].id + ":volume";
            const auto level = [&] { return t.snapshot().tracks[0].gainDb; };
            const auto atValue = [] (int v) { return Session::automationProportionToValue (Target::volume, v / 127.0f); };
            ok (t.setMidiLearnTarget (volumeTarget), "learn the volume for takeover"); ok (t.handleMidiControl (1, 7, 0, "Knobs"), "with CC 7 at 0 on Knobs");
            ok (t.setMidiLearnTarget ({}), "end learning");
            const auto choose = [&] (Session::TakeoverMode mode) { auto p = t.preferences(); p.takeoverMode = mode; ok (t.setPreferences (p), "choose a Takeover Mode"); };
            choose (Session::TakeoverMode::pickUp);
            ok (t.setTrackGain (0, -12), "the mouse sets -12 dB");
            auto steps = t.undoSteps().size();
            ok (t.handleMidiControl (1, 7, 10, "Knobs"), "the controller moves to 10, below the fader");
            const auto waiting = t.snapshot().pickUp;
            check (near (level(), -12) && t.undoSteps().size() == steps && waiting && waiting->target == volumeTarget && near (waiting->expected, -12)
                       && near (waiting->received, atValue (10)), "Pick-Up leaves the fader and reports it waiting, with no undo step");
            ok (t.handleMidiControl (1, 7, 100, "Knobs"), "the controller passes the fader on its way to 100");
            check (near (level(), atValue (100)) && ! t.snapshot().pickUp, "Pick-Up takes it over and the wait ends");
            ok (t.handleMidiControl (1, 7, 90, "Knobs"), "it moves on to 90");
            check (near (level(), atValue (90)), "and the fader follows a controller in step");
            rest(); ok (t.setTrackGain (0, -12), "the mouse sets -12 dB again");
            ok (t.handleMidiControl (1, 7, 100, "Pads"), "another input's first message of CC 7");
            check (near (level(), -12), "is not known to be in step, so it waits: each input remembers its own controllers");
            choose (Session::TakeoverMode::valueScaling);
            ok (t.handleMidiControl (1, 7, 20, "Knobs"), "Value Scaling: Knobs' controller from 90 down to 20");
            const auto position = Session::automationValueToProportion (Target::volume, -12);
            const auto scaled = remotemap::takeover (remotemap::Takeover::valueScaling, position, 90.0f, 20.0f, 0, 1, false).value;
            check (near (level(), Session::automationProportionToValue (Target::volume, scaled)) && level() < -12,
                   "Value Scaling moves the fader down as far toward the bottom, in proportion, as the controller went");
            choose (Session::TakeoverMode::none);
            ok (t.handleMidiControl (1, 7, 64, "Knobs"), "None: the controller at 64");
            check (near (level(), atValue (64)), "None jumps the fader to the controller");
            rest();
            // Choices always jump: band 1's mode of an Eight-band EQ.
            choose (Session::TakeoverMode::pickUp);
            ok (t.addNativeEffect (0, Session::EffectKind::equalizer), "add an Eight-band EQ for takeover");
            const int bandType = effectParameterIndex (Session::EffectKind::equalizer, "band1Type");
            const auto typeTarget = "device:" + t.snapshot().tracks[0].plugins.back().id + ":" + juce::String (bandType);
            ok (t.setMidiLearnTarget (typeTarget), "learn band 1's mode"); ok (t.handleMidiControl (1, 30, 0, "Knobs"), "with CC 30 at 0"); ok (t.setMidiLearnTarget ({}), "end learning");
            const auto range = t.midiTargetRange (typeTarget);
            auto settings = t.snapshot().tracks[0].plugins.back().effect;
            check (range.has_value(), "the mode has a range");
            settings.values[(size_t) bandType] = range->second;
            ok (t.setNativeEffectSettings (t.snapshot().tracks[0].plugins.back().id, settings), "the last mode, with the mouse");
            ok (t.handleMidiControl (1, 30, 10, "Knobs"), "CC 30 from 0 to 10, below it");
            check (t.snapshot().tracks[0].plugins.back().effect.values[(size_t) bandType] == std::round (range->first + (range->second - range->first) * 10 / 127.0f),
                   "with Pick-Up, a choice still jumps to the controller");
        }
        {
            // Relative and 14-bit map modes, ported from the research model: each case gives the model's own result.
            using Mode = remotemap::Mode;
            struct Steps { int mode, value; float expected; };
            const Steps steps[] {
                { 1, 1, 0x1.0p+0f }, { 1, 65, -0x1.0p+0f }, { 1, 7, 0x1.4p+3f }, { 1, 9, 0x1.9p+5f }, { 1, 10, 0x1.4p+4f }, { 1, 0, 0x1.4p+4f },
                { 2, 65, 0x1.0p+0f }, { 2, 63, -0x1.0p+0f }, { 2, 71, 0x1.4p+4f }, { 2, 64, 0x1.9p+5f }, { 3, 127, -0x1.0p+0f }, { 3, 65, 0x1.9p+5f },
                { 3, 66, -0x1.9p+5f }, { 4, 65, 0x1.0p+0f }, { 4, 1, -0x1.0p+0f }, { 4, 0, -0x1.9p+5f }, { 6, 64, 0x1.0p+6f }, { 6, 127, -0x1.f8p+5f },
                { 7, 60, -0x1.0p+2f }, { 8, 127, -0x1.0p+0f }, { 8, 65, -0x1.f8p+5f }, { 9, 3, -0x1.8p+1f }, { 9, 66, 0x1.0p+1f }, { 0, 99, 0.0f }, { 5, 99, 0.0f },
            };
            bool stepsMatch = true;
            for (const auto& c : steps) stepsMatch = stepsMatch && remotemap::relativeSteps ((Mode) c.mode, (float) c.value) == c.expected;
            check (stepsMatch, "25 relative values decode to the model's steps, through its tables in the accelerated modes");
            struct Relative { int mode, value; float current, lower, upper; bool stepped, reverse; float expected; };
            const Relative moves[] {
                { 1, 1, 0x1.0p-1f, 0.0f, 0x1.0p+0f, false, false, 0x1.02p-1f }, { 2, 68, 0x1.4p+3f, 0.0f, 0x1.fcp+6f, false, false, 0x1.7f8p+3f },
                { 1, 9, 0x1.fae148p-1f, 0.0f, 0x1.0p+0f, false, false, 0x1.0p+0f }, { 1, 1, 0x1.0p-1f, 0.0f, 0x1.0p+0f, false, true, 0x1.fcp-2f },
                { 1, 9, 0x1.8p+1f, 0.0f, 0x1.4p+3f, true, false, 0x1.0p+2f }, { 3, 127, 0x1.333334p-2f, 0x1.99999ap-3f, 0x1.99999ap-1f, false, false, 0x1.30cccep-2f },
                { 6, 64, 0x1.99999ap-4f, 0.0f, 0x1.0p+0f, false, false, 0x1.666666p-2f }, { 9, 3, 0x1.666666p-1f, 0.0f, 0x1.0p+0f, false, false, 0x1.606666p-1f },
                { 4, 0, 0x1.4p+2f, 0.0f, 0x1.cp+2f, true, true, 0x1.8p+2f }, { 8, 65, 0x1.0p-1f, 0.0f, 0x1.0p+0f, false, false, 0x1.04p-2f },
            };
            bool movesMatch = true;
            for (const auto& c : moves) movesMatch = movesMatch && remotemap::relative ((Mode) c.mode, (float) c.value, c.current, c.lower, c.upper, c.stepped, c.reverse) == c.expected;
            check (movesMatch, "ten relative moves give the model's values: a 256th of the range a step, clamped, reversed, or a step of a stepped control");
            const int values[] { 0, 1, 63, 64, 65, 127 };
            const char* const on[] { "000111", "111100", "000111", "111100", "000011", "000111", "111100", "111100", "000000", "000000" };
            bool switchesMatch = true;
            for (int m = 0; m < 10; ++m) for (int v = 0; v < 6; ++v) switchesMatch = switchesMatch && remotemap::switchesOn ((Mode) m, (float) values[v]) == (on[m][v] == '1');
            check (switchesMatch, "each mode switches buttons as the model's table does");
            check (remotemap::fourteenBit (64, 0) == 0x1.fc07fp+5f && remotemap::fourteenBit (127, 127) == 0x1.fcp+6f
                       && remotemap::fourteenBit (remotemap::unknownValue, remotemap::unknownValue) == -0x1.fc07fp-8f,
                   "a 14-bit pair gives the model's value from 0 to 127, and its value for halves not known");
            const auto learned = [] (int cc, int value, std::optional<int> prior, std::optional<int> mapped) { const auto l = remotemap::learn (cc, value, prior, mapped); return std::pair { l.controller, (int) l.mode }; };
            check (learned (7, 1, 1, {}) == std::pair { 7, 1 } && learned (7, 63, 63, {}) == std::pair { 7, 2 } && learned (7, 127, 127, {}) == std::pair { 7, 3 }
                       && learned (7, 65, 65, {}) == std::pair { 7, 4 } && learned (7, 64, 64, {}) == std::pair { 7, 0 } && learned (7, 5, 4, {}) == std::pair { 7, 0 }
                       && learned (39, 10, 3, 7) == std::pair { 7, 5 } && learned (8, 10, 3, 40) == std::pair { 8, 5 } && learned (7, 10, std::nullopt, std::nullopt) == std::pair { 7, 0 },
                   "learning detects the model's modes: repeated 1, 63, 127 and 65, and a controller 32 from the mapped one");

            // In a Session: an encoder learned on the volume.
            Session r (false);
            const auto first = r.snapshot().tracks[0].id, gainTarget = "mixer:" + first + ":volume";
            const auto mapped = [&] (const juce::String& target)
            {
                const auto shown = r.snapshot();
                const auto* m = find (shown.midiMappings, target);
                return m != nullptr ? std::optional<Session::MidiMapping> (*m) : std::nullopt;
            };
            ok (r.setMidiLearnTarget (gainTarget), "learn the volume with an encoder");
            ok (r.handleMidiControl (1, 20, 1, "Encoders"), "it turns once: CC 20 at 1");
            auto stepsBefore = r.undoSteps().size();
            ok (r.handleMidiControl (1, 20, 1, "Encoders"), "and again: CC 20 at 1 again");
            check (mapped (gainTarget) && mapped (gainTarget)->mode == Session::MapMode::signedBit && r.undoSteps().size() == stepsBefore,
                   "the repeated 1 makes it Relative (Signed Bit), changing the mode outside the undo history");
            ok (r.setMidiLearnTarget ({}), "end learning");
            ok (r.setTrackGain (0, -12), "the mouse sets -12 dB");
            const auto proportion = [] (float db) { return Session::automationValueToProportion (Target::volume, db); };
            const auto fromProportion = [] (float p) { return Session::automationProportionToValue (Target::volume, p); };
            const auto turned = [&] (float before, int value) { return fromProportion (remotemap::relative (Mode::signedBit, (float) value, proportion (before), 0, 1, false, false)); };
            auto expected = turned (-12, 1);
            ok (r.handleMidiControl (1, 20, 1, "Encoders"), "a turn up");
            check (near (r.snapshot().tracks[0].gainDb, expected) && r.snapshot().tracks[0].gainDb > -12, "moves the fader a 256th of its travel up");
            expected = turned (r.snapshot().tracks[0].gainDb, 7);
            ok (r.handleMidiControl (1, 20, 7, "Encoders"), "a fast turn: 7");
            check (near (r.snapshot().tracks[0].gainDb, expected), "moves it ten steps, through the Signed Bit table");
            auto prefs = r.preferences(); prefs.takeoverMode = Session::TakeoverMode::pickUp; ok (r.setPreferences (prefs), "choose Pick-Up");
            expected = turned (r.snapshot().tracks[0].gainDb, 65);
            ok (r.handleMidiControl (1, 20, 65, "Encoders"), "a turn down: 65");
            check (near (r.snapshot().tracks[0].gainDb, expected), "moves it a step down, whatever the Takeover Mode");
            prefs.takeoverMode = Session::TakeoverMode::none; ok (r.setPreferences (prefs), "back to None");
            stepsBefore = r.undoSteps().size();
            ok (r.setMidiMappingMode (gainTarget, Session::MapMode::twosComplement), "choose Relative (2's Comp.)");
            check (mapped (gainTarget)->mode == Session::MapMode::twosComplement && r.undoSteps().size() == stepsBefore + 1, "the chosen mode is one undo step");
            const auto before = r.snapshot().tracks[0].gainDb;
            ok (r.handleMidiControl (1, 20, 127, "Encoders"), "127 in 2's Comp.");
            check (r.snapshot().tracks[0].gainDb < before, "turns it down a step");
            check (r.setMidiMappingMode (gainTarget, Session::MapMode::absolute14).wasOk(), "CC 20 can be the coarse half of a 14-bit pair");
            ok (r.setMidiMappingMode (gainTarget, Session::MapMode::absolute), "back to Absolute");
            // A 14-bit pair on pan: learned from CC 10 and then CC 42.
            const auto panTarget = "mixer:" + first + ":pan";
            ok (r.setMidiLearnTarget (panTarget), "learn pan"); ok (r.handleMidiControl (1, 10, 64, "Encoders"), "with CC 10");
            ok (r.handleMidiControl (1, 42, 0, "Encoders"), "then CC 42, 32 above it");
            ok (r.setMidiLearnTarget ({}), "end learning");
            check (mapped (panTarget) && mapped (panTarget)->controller == 10 && mapped (panTarget)->mode == Session::MapMode::absolute14,
                   "the pair maps as Absolute (14bit) on CC 10");
            ok (r.setTrackPan (0, -1), "pan hard left");
            ok (r.handleMidiControl (1, 10, 100, "Encoders"), "the coarse half alone");
            check (near (r.snapshot().tracks[0].pan, -1), "moves nothing until the fine half comes");
            ok (r.handleMidiControl (1, 42, 64, "Encoders"), "the fine half");
            const auto panAt = Session::automationProportionToValue (Target::pan, remotemap::absolute (remotemap::fourteenBit (100, 64), 0, 1, false));
            check (near (r.snapshot().tracks[0].pan, panAt, 1e-4), "together they place pan at (100 x 128 + 64) / 129 of 127");
            check (r.setMidiMappingMode (panTarget, Session::MapMode::signedBit).wasOk() && r.setMidiMappingMode ("mixer:missing:pan", Session::MapMode::signedBit).failed(),
                   "a mapping's mode can be chosen, and an unmapped control's cannot");
            // Buttons: a relative encoder switches solo on as it turns up and off as it turns down.
            const auto solo = "track:" + first + ":solo";
            ok (r.setMidiLearnTarget (solo), "learn solo"); ok (r.handleMidiControl (1, 30, 1, "Encoders"), "with CC 30"); ok (r.handleMidiControl (1, 30, 1, "Encoders"), "turned again");
            ok (r.setMidiLearnTarget ({}), "end learning");
            check (mapped (solo)->mode == Session::MapMode::signedBit, "solo's encoder is Relative (Signed Bit)");
            check (r.handleMidiControl (1, 30, 1, "Encoders").wasOk() && r.snapshot().tracks[0].solo, "turning it up switches solo on");
            check (r.handleMidiControl (1, 30, 65, "Encoders").wasOk() && ! r.snapshot().tracks[0].solo, "and down, off");
            // The modes reopen, and damaged ones are refused.
            const auto project = root.getChildFile ("Modes").getChildFile ("Modes.halftrack");
            ok (project.getParentDirectory().createDirectory(), "create the modes project folder");
            const auto modes = r.snapshot().midiMappings;
            ok (r.saveProject (project), "save the modes");
            Session reopened (false);
            ok (reopened.loadProject (project), "reopen them");
            check (reopened.snapshot().midiMappings == modes, "the map modes reopen");
            const auto damage = [&] (const juce::String& name, const std::function<void (juce::XmlElement&)>& change)
            {
                auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "parse the modes project");
                change (*xml->getChildByName ("EDIT")->getChildByName ("HALFTRACKMIDIMAP"));
                const auto damaged = project.getSiblingFile (name + ".halftrack"); check (xml->writeTo (damaged), "write " + name);
                check (reopened.loadProject (damaged).failed() && reopened.snapshot().midiMappings == modes, name + " is refused, keeping the open project");
            };
            damage ("Unknown mode", [] (juce::XmlElement& m) { m.getFirstChildElement()->setAttribute ("mode", 10); });
            damage ("High pair", [] (juce::XmlElement& m) { m.getFirstChildElement()->setAttribute ("mode", 5); m.getFirstChildElement()->setAttribute ("controller", 100); });
            damage ("Note mode", [] (juce::XmlElement& m) { m.getFirstChildElement()->setAttribute ("note", 1); m.getFirstChildElement()->setAttribute ("mode", 1); });
        }
        std::cout << "MIDI mapping passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL MIDI mapping: " << e.what() << std::endl; return 1; }
}
}
