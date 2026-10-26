#include "NativeEffects.h"
#include "Session.h"
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>

// Device presets and the default set. Presets of a native effect, the Utility and an
// instrument must reload with exactly the saved settings in one undo step; damaged presets
// are rejected; a default set opens as a new, untitled, unchanged project.
namespace halftrack
{
namespace
{
using Kind = Session::EffectKind;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS presets: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
Session::PluginInfo last (Session& s, int track) { const auto snapshot = s.snapshot(); return snapshot.tracks[(size_t) track].plugins.back(); }
}

int runPresetTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("presets-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto library = root.getChildFile ("Presets");
        Session s (false);
        ok (s.addNativeEffect (0, Kind::saturator), "add a Saturator");
        auto saturator = last (s, 0);
        auto settings = saturator.effect;
        settings.values[(size_t) effectParameterIndex (Kind::saturator, "driveDb")] = 12;
        settings.values[(size_t) effectParameterIndex (Kind::saturator, "outputDb")] = -3;
        ok (s.setNativeEffectSettings (saturator.id, settings), "drive it 12 dB and turn it down 3 dB");
        const auto warm = library.getChildFile ("Saturator").getChildFile (juce::String ("Warm") + Session::presetExtension);
        ok (s.saveDevicePreset (saturator.id, warm, "Warm"), "save it as Warm");
        check (s.saveDevicePreset (saturator.id, warm, "Warm").failed(), "an existing preset is not replaced unless asked");
        ok (s.saveDevicePreset (saturator.id, warm, "Warm", true), "replacing it when asked succeeds");
        check (s.saveDevicePreset (saturator.id, warm, " ").failed() && s.saveDevicePreset ("missing", warm, "X", true).failed(), "empty names and unknown devices are rejected");
        Session::DevicePreset read;
        ok (Session::readDevicePreset (warm, read), "read the preset back");
        check (read.name == "Warm" && ! read.utility && read.effect.kind == Kind::saturator && read.effect.values == settings.values, "the preset holds the exact settings");

        ok (s.addAudioTrack ("Second"), "add a second audio track");
        const int second = (int) s.snapshot().tracks.size() - 1;
        juce::String loaded;
        ok (s.loadDevicePreset (second, warm, loaded), "load Warm onto the second track");
        check (last (s, second).id == loaded && last (s, second).effect.values == settings.values, "the loaded Saturator has the preset's settings");
        s.undo(); check (s.snapshot().tracks[(size_t) second].plugins.empty(), "one undo removes the loaded device");

        // The Utility.
        ok (s.addUtility (0), "add a Utility");
        auto utility = last (s, 0);
        auto u = utility.utility; u.gainDb = -6; u.width = .5f; u.mono = true; u.channel = Session::UtilitySettings::Channel::swap;
        ok (s.setUtilitySettings (utility.id, u), "set the Utility");
        const auto narrow = library.getChildFile ("Utility").getChildFile (juce::String ("Narrow") + Session::presetExtension);
        ok (s.saveDevicePreset (utility.id, narrow, "Narrow"), "save the Utility preset");
        ok (s.loadDevicePreset (second, narrow, loaded), "load it onto the second track");
        check (last (s, second).isUtility && last (s, second).utility == u, "the loaded Utility matches");

        // Instruments and MIDI effects need a MIDI track.
        ok (s.addMidiTrack ("Lead"), "add a MIDI track");
        const int lead = (int) s.snapshot().tracks.size() - 1;
        ok (s.setTrackInstrument (s.snapshot().tracks[(size_t) lead].id, Kind::polySynth), "play it from the Poly Synth");
        auto synth = s.snapshot().tracks[(size_t) lead].instrument.effect;
        {
            const auto fields = effectParameters (Kind::polySynth);
            for (size_t i = 0; i < 3; ++i)
            {
                auto v = fields[i].minimum + (fields[i].maximum - fields[i].minimum) * .25f;
                synth.values[i] = fields[i].choices || fields[i].integer ? std::round (v) : v;
            }
        }
        ok (s.setNativeEffectSettings (s.snapshot().tracks[(size_t) lead].instrument.id, synth), "edit the synth");
        const auto pad = library.getChildFile ("Poly Synth").getChildFile (juce::String ("Pad") + Session::presetExtension);
        ok (s.saveDevicePreset (s.snapshot().tracks[(size_t) lead].instrument.id, pad, "Pad"), "save the synth preset");
        check (s.loadDevicePreset (second, pad, loaded).failed(), "an instrument preset needs a MIDI track");
        ok (s.addMidiTrack ("Other"), "add another MIDI track");
        const int other = (int) s.snapshot().tracks.size() - 1;
        ok (s.loadDevicePreset (other, pad, loaded), "load the synth preset onto it");
        check (s.snapshot().tracks[(size_t) other].instrument.id == loaded && s.snapshot().tracks[(size_t) other].instrument.effect.values == synth.values,
               "the instrument is replaced by the preset's Poly Synth");
        s.undo(); check (! s.snapshot().tracks[(size_t) other].instrument.isNativeEffect, "one undo restores the built-in synth");
        ok (s.addNativeEffect (lead, Kind::arpeggiator), "add an arpeggiator");
        const auto arp = library.getChildFile (juce::String ("Arp") + Session::presetExtension);
        ok (s.saveDevicePreset (last (s, lead).id, arp, "Arp"), "save the arpeggiator preset");
        check (s.loadDevicePreset (0, arp, loaded).failed(), "a MIDI effect preset needs a MIDI track");

        // Hot-Swap: a device replaced in its place by a preset or a device of the same type, in one undo step.
        {
            const auto chain = s.snapshot().tracks[0].id;
            const auto devicesOf = [&] (int t) { const auto snapshot = s.snapshot(); return snapshot.tracks[(size_t) t].plugins; };
            const auto before = devicesOf (0);
            check (before.size() == 2 && before[0].effect.kind == Kind::saturator && before[1].isUtility, "track 1 holds a Saturator, then a Utility");
            juce::String swapped;
            auto changed = before[0].effect; changed.values[(size_t) effectParameterIndex (Kind::saturator, "driveDb")] = 3;
            ok (s.setNativeEffectSettings (before[0].id, changed), "change the Saturator");
            Session::HotSwapSource warmSource; warmSource.preset = warm;
            ok (s.hotSwapDevice (chain, before[0].id, warmSource, swapped), "hot-swap Warm onto it");
            check (swapped == before[0].id && devicesOf (0)[0].effect.values == settings.values, "a preset of its own kind loads into the device, keeping it");
            Session::HotSwapSource eq; eq.kind = Kind::eqThree;
            ok (s.hotSwapDevice (chain, before[0].id, eq, swapped), "hot-swap an EQ Three in");
            auto after = devicesOf (0);
            check (after.size() == 2 && after[0].id == swapped && after[0].effect.kind == Kind::eqThree && after[1].id == before[1].id,
                   "another audio effect takes its place, first in the chain");
            check (s.undoDescription().containsIgnoreCase ("hot-swap"), "as one Hot-swap step (" + s.undoDescription() + ")");
            s.undo();
            after = devicesOf (0);
            check (after.size() == 2 && after[0].id == before[0].id && after[0].effect.kind == Kind::saturator, "one undo brings the Saturator back in its place");
            Session::HotSwapSource narrowSource; narrowSource.preset = narrow;
            ok (s.hotSwapDevice (chain, before[1].id, warmSource, swapped), "hot-swap the Saturator preset onto the Utility");
            after = devicesOf (0);
            check (after.size() == 2 && after[1].effect.kind == Kind::saturator && after[1].effect.values == settings.values && after[0].id == before[0].id,
                   "an audio effect's preset can take the Utility's place, last in the chain");
            s.undo();
            Session::HotSwapSource padSource; padSource.preset = pad;
            check (s.hotSwapDevice (chain, before[0].id, padSource, swapped).failed() && devicesOf (0)[0].id == before[0].id, "an instrument cannot take an audio effect's place");
            const auto leadId = s.snapshot().tracks[(size_t) lead].id;
            Session::HotSwapSource fm; fm.kind = Kind::fmSynth;
            ok (s.hotSwapDevice (leadId, s.snapshot().tracks[(size_t) lead].instrument.id, fm, swapped), "hot-swap an FM Synth for the Poly Synth");
            check (s.snapshot().tracks[(size_t) lead].instrument.effect.kind == Kind::fmSynth && swapped == s.snapshot().tracks[(size_t) lead].instrument.id,
                   "an instrument takes the instrument's place");
            s.undo();
            check (s.snapshot().tracks[(size_t) lead].instrument.effect.kind == Kind::polySynth, "undo restores the Poly Synth");
            const auto arpId = last (s, lead).id;
            Session::HotSwapSource chord; chord.kind = Kind::chord;
            ok (s.hotSwapDevice (leadId, arpId, chord, swapped), "hot-swap a Chord for the arpeggiator");
            check (last (s, lead).effect.kind == Kind::chord && last (s, lead).id == swapped, "a MIDI effect takes a MIDI effect's place");
            juce::String unused;
            const auto refused = s.hotSwapDevice (leadId, swapped, narrowSource, unused);
            check (refused.failed() && refused.getErrorMessage().contains ("same type") && last (s, lead).id == swapped, "the Utility cannot take a MIDI effect's place");
            s.undo();
            check (s.hotSwapDevice ("missing", arpId, chord, swapped).failed() && s.hotSwapDevice (leadId, "missing", chord, swapped).failed(), "unknown chains and devices are refused");
        }

        // Device A/B comparison, as the reference's (its manual, section 23.2.2): two states of a built-in device's
        // controls, B starting as a copy of A; switching and copying are each one undo step.
        {
            ok (s.addNativeEffect (second, Kind::saturator), "a Saturator to compare");
            const auto id = last (s, second).id;
            const auto drive = (size_t) effectParameterIndex (Kind::saturator, "driveDb");
            const auto driveOf = [&] { return last (s, second).effect.values[drive]; };
            auto a = last (s, second).effect; a.values[drive] = 6; ok (s.setNativeEffectSettings (id, a), "A drives 6 dB");
            check (! last (s, second).compareB && s.switchDeviceCompareState ("missing").failed() && s.copyDeviceCompareState ("missing").failed(),
                   "a device shows A at first, and unknown devices are refused");
            ok (s.switchDeviceCompareState (id), "switch to B");
            check (last (s, second).compareB && driveOf() == 6 && s.undoDescription() == "Compare: Switch to B", "B starts as a copy of A, one undo step");
            auto b = a; b.values[drive] = 18; ok (s.setNativeEffectSettings (id, b), "B drives 18 dB");
            ok (s.switchDeviceCompareState (id), "switch back to A");
            check (! last (s, second).compareB && driveOf() == 6 && s.undoDescription() == "Compare: Switch to A", "A keeps its 6 dB");
            ok (s.switchDeviceCompareState (id), "and to B again");
            check (last (s, second).compareB && driveOf() == 18, "B keeps its 18 dB");
            s.undo(); check (! last (s, second).compareB && driveOf() == 6, "one undo returns to A");
            s.redo();
            ok (s.copyDeviceCompareState (id), "copy B to A");
            check (s.undoDescription() == "Compare: Copy B to A", "as one step");
            ok (s.switchDeviceCompareState (id), "switch to A");
            check (! last (s, second).compareB && driveOf() == 18, "A now has B's 18 dB");
            // An automated control's lane is overridden by a switch, as by a change by hand.
            ok (s.copyDeviceCompareState (id), "copy A to B");
            b.values[drive] = 3; ok (s.switchDeviceCompareState (id), "to B"); ok (s.setNativeEffectSettings (id, b), "B drives 3 dB");
            Session::AutomationLane lane; lane.points = { { 0, 0 }, { 4, 12 } };
            ok (s.setDeviceAutomation (id, (int) drive, lane), "automate the drive");
            ok (s.switchDeviceCompareState (id), "switch to A with the drive automated");
            check (! last (s, second).automation.empty() && ! last (s, second).automation[0].lane.enabled && s.snapshot().automationOverridden,
                   "switching overrides the automated drive's lane until Re-Enable Automation");
            // Both states reopen with the project; a damaged comparison is refused.
            const auto project = root.getChildFile ("Compare").getChildFile ("Compare.halftrack");
            ok (project.getParentDirectory().createDirectory(), "a folder for the project");
            ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
            check (! last (s, second).compareB && driveOf() == 18, "A reopens shown");
            ok (s.switchDeviceCompareState (id), "switch to B after reopening");
            check (last (s, second).compareB && driveOf() == 3, "and B kept its 3 dB");
            auto xml = juce::XmlDocument::parse (project);
            juce::XmlElement* stored = nullptr;
            std::function<void (juce::XmlElement&)> find = [&] (juce::XmlElement& e) { if (e.hasTagName ("HALFTRACKCOMPARESTATE")) stored = &e; for (auto* c : e.getChildIterator()) find (*c); };
            if (xml != nullptr) find (*xml);
            check (stored != nullptr && stored->hasAttribute ("driveDb"), "the project keeps the state not shown");
            stored->setAttribute ("driveDb", 900.0);
            const auto broken = root.getChildFile ("Compare broken.halftrack");
            check (xml->writeTo (broken), "write a comparison out of range");
            check (s.loadProject (broken).failed() && last (s, second).compareB, "it is refused, keeping the open project");
        }

        // Damaged presets.
        const auto corrupt = [&] (const juce::String& name, const std::function<void (juce::XmlElement&)>& change)
        {
            auto xml = juce::XmlDocument::parse (warm); check (xml != nullptr, "parse the preset");
            change (*xml);
            const auto broken = root.getChildFile (name + Session::presetExtension); check (xml->writeTo (broken), "write " + name);
            Session::DevicePreset unused;
            check (Session::readDevicePreset (broken, unused).failed() && s.loadDevicePreset (second, broken, loaded).failed(), name + " is rejected");
        };
        corrupt ("Version", [] (juce::XmlElement& x) { x.setAttribute ("version", 2); });
        corrupt ("Range", [] (juce::XmlElement& x) { x.getChildByName ("SETTINGS")->setAttribute ("driveDb", 500.0); });
        corrupt ("Text", [] (juce::XmlElement& x) { x.getChildByName ("SETTINGS")->setAttribute ("outputDb", "loud"); });
        corrupt ("Kind", [] (juce::XmlElement& x) { x.setAttribute ("kind", 999); });
        corrupt ("Rack", [] (juce::XmlElement& x) { x.setAttribute ("kind", (int) Kind::audioEffectRack); });

        // A default set.
        Session t (false);
        t.setTempo (100);
        ok (t.addMidiTrack ("Keys"), "add a MIDI track to the template"); ok (t.setTrackGain (0, -5), "turn the first track down");
        const auto defaults = root.getChildFile ("Default Set.halftrack");
        ok (t.saveDefaultSet (defaults), "save the default set");
        Session fresh (false);
        ok (fresh.addAudioTrack ("Scratch"), "make a change before New");
        ok (fresh.newProjectFromDefaultSet (defaults), "start a new project from the default set");
        const auto snapshot = fresh.snapshot();
        check (snapshot.tempo == 100 && snapshot.tracks.size() == 3 && snapshot.tracks.back().name == "Keys" && std::abs (snapshot.tracks[0].gainDb + 5) < 1e-3,
               "the new project has the template's tempo, tracks and mixer");
        check (snapshot.projectFile == juce::File() && ! snapshot.dirty && ! fresh.canUndo(), "it is untitled, unchanged and without history");
        // Live Clips, as the reference's: a clip with its track's devices in a file. Where no track is, or onto a track with
        // neither clips nor devices, its devices come too; onto another track the clip alone, keeping its mixer envelope.
        {
            Session live (false);
            ok (live.saveProject (root.getChildFile ("Live").getChildFile ("Live.halftrack")), "save a project for the Live Clips");
            ok (live.addMidiTrack ("Bass"), "a MIDI track");
            const int bass = (int) live.snapshot().tracks.size() - 1;
            const auto bassId = live.snapshot().tracks[(size_t) bass].id;
            ok (live.addNativeEffect (bass, Kind::saturator), "with a Saturator");
            const auto sat = last (live, bass);
            ok (live.addMidiClip (bassId, 0, 4), "a four-beat clip");
            const auto clipId = live.snapshot().tracks[(size_t) bass].clips[0].id;
            ok (live.addMidiNote (clipId, 40, 1, 1, .8f), "with a note");
            const int drive = effectParameterIndex (Kind::saturator, "driveDb");
            ok (live.setClipEnvelope (clipId, { { sat.id, drive }, Session::EnvelopeMode::modulation, { { 0, .5f }, { 4, -.5f } } }), "a Saturator envelope");
            ok (live.setClipEnvelope (clipId, { { {}, 0 }, Session::EnvelopeMode::modulation, { { 0, 1 }, { 4, .5f } } }), "and a volume envelope");
            const auto file = root.getChildFile ("Clips").getChildFile (juce::String ("Bassline") + Session::liveClipExtension);
            ok (live.saveLiveClip (clipId, file), "save it as a Live Clip");
            Session::LiveClip info;
            ok (live.readLiveClip (file, info), "read it back");
            check (info.midi && info.devices == 2 && std::abs (info.lengthBeats - 4) < 1e-9, "it is a four-beat MIDI clip with two devices, the synth and the Saturator");
            const auto saturatorOf = [] (const Session::TrackInfo& t) -> juce::String
            { for (const auto& p : t.plugins) if (p.effect.kind == Kind::saturator) return p.id; return {}; };
            const auto clipOf = [] (const Session::TrackInfo& t, const juce::String& id) -> std::optional<Session::ClipInfo>
            {
                for (const auto& c : t.clips) if (c.id == id) return c;
                for (const auto& slot : t.slots) if (slot.hasClip && slot.clip.id == id) return slot.clip;
                return std::nullopt;
            };
            const auto tracks = live.snapshot().tracks.size();
            juce::String loaded;
            ok (live.loadLiveClip (file, -1, 8, loaded), "drop it where no track is");
            auto snap = live.snapshot();
            check (snap.tracks.size() == tracks + 1, "a new track is made");
            auto made = snap.tracks.back();
            auto placed = clipOf (made, loaded);
            check (saturatorOf (made).isNotEmpty() && saturatorOf (made) != sat.id && placed && std::abs (placed->startBeat - 8) < 1e-6
                       && placed->notes.size() == 1 && placed->notes[0].pitch == 40 && placed->envelopes.size() == 2
                       && std::any_of (placed->envelopes.begin(), placed->envelopes.end(), [&] (const auto& e) { return e.target.pluginId == saturatorOf (made); }),
                   "with the devices, the clip at beat 8, and its envelope on the new track's Saturator");
            live.undo();
            check (live.snapshot().tracks.size() == tracks, "one undo removes the track and its clip");
            ok (live.addMidiTrack ("Empty"), "an empty MIDI track");
            const int empty = (int) live.snapshot().tracks.size() - 1;
            ok (live.loadLiveClipToSlot (file, empty, 0, loaded), "drop it into the empty track's slot");
            snap = live.snapshot();
            placed = clipOf (snap.tracks[(size_t) empty], loaded);
            check (saturatorOf (snap.tracks[(size_t) empty]).isNotEmpty() && placed && placed->notes.size() == 1 && placed->envelopes.size() == 2,
                   "the devices join the empty track, the clip in its slot with both envelopes");
            ok (live.loadLiveClip (file, bass, 8, loaded), "drop it onto the Bass track, which has devices");
            snap = live.snapshot();
            placed = clipOf (snap.tracks[(size_t) bass], loaded);
            int saturators = 0;
            for (const auto& p : snap.tracks[(size_t) bass].plugins) if (p.effect.kind == Kind::saturator) ++saturators;
            check (saturators == 1 && placed && placed->envelopes.size() == 1 && placed->envelopes[0].target.pluginId.isEmpty() && placed->envelopes[0].target.parameter == 0,
                   "the clip comes alone, keeping its volume envelope on the track's mixer");
            check (live.loadLiveClip (file, 0, 0, loaded).failed(), "a MIDI clip does not go onto an audio track");
            // Audio: its sample beside the file, collected into the project on loading.
            const auto tone = root.getChildFile ("tone.wav");
            {
                juce::AudioBuffer<float> x (1, 48000);
                for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (0, n, (float) (.25 * std::sin (2 * 3.14159265358979 * 440 * n / 48000)));
                juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (tone.createOutputStream());
                auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (24));
                check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a tone");
            }
            ok (live.importAudio (tone, 0, 0), "import it onto the first track");
            const auto audioId = live.snapshot().tracks[0].clips.back().id;
            const auto audioFile = root.getChildFile ("Clips").getChildFile (juce::String ("Tone") + Session::liveClipExtension);
            ok (live.saveLiveClip (audioId, audioFile), "save the audio clip as a Live Clip");
            check (audioFile.getSiblingFile ("Samples").getChildFile ("tone.wav").existsAsFile(), "its sample is copied beside it, into Samples");
            ok (live.loadLiveClip (audioFile, 0, 8, loaded), "load it onto the track at beat 8");
            placed = clipOf (live.snapshot().tracks[0], loaded);
            check (placed && placed->file.existsAsFile() && placed->file != tone && placed->file.isAChildOf (root.getChildFile ("Live")) && std::abs (placed->startBeat - 8) < 1e-6,
                   "its sample is collected into the project");
            const auto junk = root.getChildFile (juce::String ("Junk") + Session::liveClipExtension);
            check (junk.replaceWithText ("<HALFTRACKCLIP version=\"1\" kind=\"midi\" lengthBeats=\"-4\"/>"), "write a damaged Live Clip");
            const auto count = live.snapshot().tracks.size();
            check (live.readLiveClip (junk, info).failed() && live.loadLiveClip (junk, -1, 0, loaded).failed() && live.snapshot().tracks.size() == count,
                   "a damaged Live Clip is refused, changing nothing");
        }
        std::cout << "Presets passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL presets: " << e.what() << std::endl; return 1; }
}
}
