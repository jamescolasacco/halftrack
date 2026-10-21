#include "NativeEffects.h"
#include "Session.h"
#include <iostream>
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
        std::cout << "Presets passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL presets: " << e.what() << std::endl; return 1; }
}
}
