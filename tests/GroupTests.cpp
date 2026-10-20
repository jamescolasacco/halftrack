#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Group tracks sum their members through their own mixer. Renders compare with
// the ungrouped mix and with independent gain/pan arithmetic.
namespace halftrack
{
namespace
{
constexpr double rate = 48000;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS groups: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
void write (const juce::File& f, float left, float right, int frames)
{
    juce::AudioBuffer<float> a (2, frames);
    for (int n = 0; n < frames; ++n) { a.setSample (0, n, left * (float) std::sin (n * .05)); a.setSample (1, n, right * (float) std::cos (n * .031)); }
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (a, 0, frames), "write generated source");
}
juce::AudioBuffer<float> render (Session& s, const juce::File& f)
{
    ok (s.exportWav (f, rate, 32), "render mix");
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    check (r != nullptr, "decode mix"); juce::AudioBuffer<float> a (2, (int) r->lengthInSamples); r->read (&a, 0, a.getNumSamples(), 0, true, true); return a;
}
// Compare inside the sources, leaving the engine's start/end settling outside the window.
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, float leftScale = 1, float rightScale = 1)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1e9;
    double worst = 0;
    for (int n = 2000; n < a.getNumSamples() - 2000; ++n)
    {
        worst = std::max (worst, (double) std::abs (a.getSample (0, n) - leftScale * b.getSample (0, n)));
        worst = std::max (worst, (double) std::abs (a.getSample (1, n) - rightScale * b.getSample (1, n)));
    }
    return worst;
}
double peak (const juce::AudioBuffer<float>& a) { return std::max (a.getMagnitude (0, 0, a.getNumSamples()), a.getMagnitude (1, 0, a.getNumSamples())); }
}
int runGroupTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("groups-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto a = root.getChildFile ("a.wav"), b = root.getChildFile ("b.wav"), c = root.getChildFile ("c.wav"), mix = root.getChildFile ("mix.wav");
        write (a, .2f, .1f, 24000); write (b, .1f, .15f, 24000); write (c, .05f, .05f, 24000);
        Session s (false);
        ok (s.importAudio (a, 0), "import first member"); ok (s.importAudio (b, 1), "import second member"); ok (s.addAudioTrack ("Outside"), "add an outside track");
        ok (s.importAudio (c, 2), "import the outside source");
        const auto outside = s.snapshot().tracks[2].id;
        ok (s.setTrackMute (2, true), "mute the outside track for the member-only reference");
        const auto reference = render (s, mix);
        check (peak (reference) > .1, "reference mix is audible");
        const auto before = s.snapshot();
        juce::String group;
        check (s.groupTracks ({}, group).failed() && s.groupTracks ({ before.tracks[0].id, before.tracks[0].id }, group).failed(), "empty and duplicate group requests are rejected");
        ok (s.groupTracks ({ before.tracks[1].id, before.tracks[0].id }, group), "group the first two tracks");
        auto snapshot = s.snapshot();
        check (snapshot.groups.size() == 1 && snapshot.groups[0].id == group && snapshot.groups[0].memberIds == juce::StringArray { before.tracks[0].id, before.tracks[1].id },
               "the group lists its members in track order");
        check (snapshot.tracks.size() == before.tracks.size() && snapshot.tracks[0].id == before.tracks[0].id && snapshot.tracks[2].id == outside
               && snapshot.tracks[0].groupId == group && snapshot.tracks[1].groupId == group && snapshot.tracks[2].groupId.isEmpty(),
               "track order and indices are unchanged, with membership reported per track");
        check (difference (render (s, mix), reference) < 1e-6, "a unity group renders the same sum as the ungrouped tracks");
        check (s.groupTracks ({ before.tracks[0].id }, group).failed(), "grouped tracks cannot be grouped again");
        ok (s.setGroupGain (group, -6), "lower the group");
        const auto gain = std::pow (10.0f, -6.0f / 20);
        check (difference (render (s, mix), reference, gain, gain) < 1e-6, "group gain scales the summed members");
        s.undo(); check (std::abs (s.snapshot().groups[0].gainDb) < 1e-4, "undo restores the group gain");
        s.redo(); check (std::abs (s.snapshot().groups[0].gainDb + 6) < 1e-4, "redo lowers the group again");
        ok (s.setGroupGain (group, 0), "restore group gain"); ok (s.setGroupPan (group, -1), "pan the group hard left");
        s.undo(); check (std::abs (s.snapshot().groups[0].pan) < 1e-4, "undo centres the group pan"); s.redo();
        check (difference (render (s, mix), reference, std::sqrt (2.0f), 0) < 1e-5, "the group pan follows the constant-power law with unity centre");
        ok (s.setGroupPan (group, 0), "centre the group");
        ok (s.setGroupMute (group, true), "mute the group");
        check (peak (render (s, mix)) < 1e-7 && ! s.snapshot().tracks[0].mute && s.snapshot().groups[0].mute, "muting the group silences its members without muting them");
        ok (s.setGroupMute (group, false), "unmute the group");
        ok (s.setTrackMute (2, false), "unmute the outside track");
        ok (s.setGroupSolo (group, true), "solo the group");
        check (difference (render (s, mix), reference) < 1e-6, "soloing the group leaves only its members audible");
        ok (s.setGroupSolo (group, false), "unsolo the group");
        ok (s.setTrackGain (1, -12), "index-based track commands still address grouped tracks");
        check (std::abs (s.snapshot().tracks[1].gainDb + 12) < 1e-4, "grouped track gain changes");
        s.undo();
        ok (s.renameGroup (group, "Drums"), "rename the group");
        ok (s.setGroupFolded (group, true), "fold the group");
        check (s.snapshot().groups[0].name == "Drums" && s.snapshot().groups[0].folded, "name and fold state are reported");
        const auto project = root.getChildFile ("groups.halftrack");
        ok (s.setGroupGain (group, -3), "set a group level to save");
        ok (s.saveProject (project), "save grouped project"); ok (s.loadProject (project), "reopen grouped project");
        snapshot = s.snapshot();
        check (snapshot.groups.size() == 1 && snapshot.groups[0].name == "Drums" && snapshot.groups[0].folded && std::abs (snapshot.groups[0].gainDb + 3) < 1e-4
               && snapshot.groups[0].memberIds.size() == 2, "groups reopen with name, fold, level and members");
        group = snapshot.groups[0].id;
        ok (s.setGroupGain (group, 0), "restore unity after reopening"); ok (s.setTrackMute (2, true), "mute outside track again");
        ok (s.ungroupTracks (group), "ungroup");
        snapshot = s.snapshot();
        check (snapshot.groups.empty() && snapshot.tracks.size() == 3 && snapshot.tracks[0].groupId.isEmpty() && snapshot.tracks[0].id == before.tracks[0].id,
               "ungrouping restores top-level tracks in order");
        check (difference (render (s, mix), reference) < 1e-6, "ungrouped tracks render the original mix");
        s.undo(); check (s.snapshot().groups.size() == 1, "undo restores the group");
        s.redo(); check (s.snapshot().groups.empty(), "redo ungroups again");
        ok (s.addReturnTrack ("Room"), "add a return");
        check (s.groupTracks ({ s.snapshot().tracks.back().id }, group).failed(), "returns cannot be grouped");
        Session midi (false); ok (midi.addMidiTrack ("Keys"), "add MIDI track");
        const auto keys = midi.snapshot().tracks.back().id;
        ok (midi.addMidiClip (keys, 0, 4), "add clip"); { const auto snap = midi.snapshot(); ok (midi.addMidiNote (snap.tracks.back().clips[0].id, 60, 0, 2, .8f), "add note"); }
        const auto alone = render (midi, mix);
        ok (midi.groupTracks ({ keys }, group), "group the MIDI track");
        check (difference (render (midi, mix), alone) < 1e-6 && peak (alone) > .01, "an instrument renders identically through its group");
        std::cout << "Groups passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL groups: " << e.what() << std::endl; return 1; }
}
}
