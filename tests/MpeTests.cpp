#include "Session.h"
#include "NativeEffects.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// MPE note expression, rendered offline at 120 BPM with sine voices. One of two held notes bends
// up an octave over its first beat while the other keeps its pitch, through the built-in synth
// and the Poly Synth; the ramp passes through the half-octave on the way. Live playback through a
// MIDI Monitor shows each note on its own channel with its slide, pressure and 48-semitone range.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS mpe: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr double rate = 48000;
using Expression = Session::NoteExpression;
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr, "read the render");
    juce::AudioBuffer<float> x (2, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
// The magnitude of one frequency over a window (a single DFT bin).
double magnitude (const juce::AudioBuffer<float>& x, double hz, double from, double seconds)
{
    double re = 0, im = 0; const int a = (int) (from * rate), b = std::min (x.getNumSamples(), a + (int) (seconds * rate));
    for (int n = a; n < b; ++n)
    {
        const auto phase = juce::MathConstants<double>::twoPi * hz * n / rate;
        re += x.getSample (0, n) * std::cos (phase); im += x.getSample (0, n) * std::sin (phase);
    }
    return std::sqrt (re * re + im * im) / std::max (1, b - a);
}
double frequency (const juce::AudioBuffer<float>& x, double from, double seconds)
{
    const int a = (int) (from * rate), b = std::min (x.getNumSamples(), a + (int) (seconds * rate));
    int first = -1, last = -1, crossings = 0;
    for (int n = a + 1; n < b; ++n)
        if (x.getSample (0, n - 1) < 0 && x.getSample (0, n) >= 0) { if (first < 0) first = n; last = n; ++crossings; }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0;
}
double hz (int note) { return 440 * std::pow (2.0, (note - 69) / 12.0); }
namespace te = tracktion;
// A source-free virtual MIDI controller, as in the MIDI input suite.
struct Controller
{
    Session& session;
    te::DeviceManager& devices = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (session.audioDeviceManager()).engine.getDeviceManager();
    juce::String name = "Halftrack MPE controller " + juce::Uuid().toString();
    std::shared_ptr<te::VirtualMidiInputDevice> midi;
    explicit Controller (Session& owner) : session (owner)
    {
        ok (devices.createVirtualMidiDevice (name), "create a virtual controller");
        devices.rescanMidiDeviceListSynchronously();
        for (auto device : devices.getMidiInDevices()) if (device->getName() == name) midi = std::dynamic_pointer_cast<te::VirtualMidiInputDevice> (device);
        check (midi != nullptr, "the virtual controller is discovered");
        midi->setMIDIInputSourceDevices ({}); midi->setChannelToUse (0); midi->setMinimumLengthMs (0); midi->setManualAdjustmentMs (0);
    }
    ~Controller() { session.stop(); if (midi) { devices.deleteVirtualMidiDevice (*midi); devices.rescanMidiDeviceListSynchronously(); } }
    void now (const juce::MidiMessage& m)
    {
        auto message = m; message.setTimeStamp (devices.getCurrentStreamTime() + 256.0 / 48000 - midi->getAdjustSecs());
        midi->handleIncomingMidiMessage (message, midi->getMPESourceID());
    }
};
}

int runMpeTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("mpe-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto out = root.getChildFile ("out.wav");
        Session s (false);
        ok (s.addMidiTrack ("MPE"), "add a MIDI track");
        const int track = (int) s.snapshot().tracks.size() - 1;
        const auto trackId = s.snapshot().tracks[(size_t) track].id;
        for (int t = 0; t < track; ++t) ok (s.setTrackMute (t, true), "mute the other tracks");
        Session::SynthSettings sine; sine.waveform = 2; sine.attackSeconds = .002f; sine.sustain = 1; sine.cutoffHz = 20000; sine.resonance = 0;
        ok (s.setSynthSettings (trackId, sine), "sine voices");
        ok (s.addMidiClip (trackId, 0, 8), "an eight-beat clip");
        const auto clipId = s.snapshot().tracks[(size_t) track].clips[0].id;
        juce::StringArray ids;
        ok (s.addMidiNotes (clipId, { { {}, 60, 0, 8, .8f }, { {}, 67, 0, 8, .8f } }, ids), "C4 and G4 held together");
        const auto notes = [&] { return s.snapshot().tracks[(size_t) track].clips[0].notes; };
        const auto bendId = ids[0];

        auto x = render (s, out);
        check (magnitude (x, hz (60), 1, 1) > .005 && magnitude (x, hz (72), 1, 1) < magnitude (x, hz (60), 1, 1) / 20, "without expression C4 sounds C4");
        const std::vector<Session::ExpressionPoint> octave { { 0, 0 }, { 1, 12 } };
        ok (s.setMidiNoteExpression (clipId, bendId, Expression::pitchBend, octave), "bend C4 up an octave over its first beat");
        check (notes()[0].pitchBend == octave && notes()[1].pitchBend.empty(), "the note reports its bend, the other none");
        x = render (s, out);
        const auto bent = magnitude (x, hz (72), 1, 1), unbent = magnitude (x, hz (60), 1, 1), other = magnitude (x, hz (67), 1, 1);
        check (bent > unbent * 20 && other > bent * .5, "C4 then sounds C5 while G4 stays (" + juce::String (bent, 4) + " at C5, "
                                                          + juce::String (unbent, 5) + " at C4, " + juce::String (other, 4) + " at G4)");
        s.undo();
        x = render (s, out);
        check (notes()[0].pitchBend.empty() && magnitude (x, hz (60), 1, 1) > magnitude (x, hz (72), 1, 1) * 20, "one undo removes the bend");
        s.redo();

        // The ramp passes the half-octave halfway, alone on the clip.
        ok (s.removeMidiNotes (clipId, { ids[1] }), "remove G4");
        x = render (s, out);
        const auto halfway = frequency (x, .23, .04), end = frequency (x, 1, .5);
        check (std::abs (halfway / hz (66) - 1) < .03 && std::abs (end / hz (72) - 1) < .003,
               "halfway through the ramp it sounds near F#4 (" + juce::String (halfway, 1) + " Hz), then C5 (" + juce::String (end, 1) + " Hz)");
        check (magnitude (x, hz (72), 3.5, .4) > .005 && magnitude (x, hz (72), 4.4, .3) < 1e-4, "the bent note holds to its end at beat 8");

        // The Poly Synth bends the same way, with the range MPE playback gives each note's channel.
        ok (s.setTrackInstrument (trackId, Session::EffectKind::polySynth), "play it on the Poly Synth");
        auto poly = s.snapshot().tracks[(size_t) track].instrument;
        const auto fields = effectParameters (Session::EffectKind::polySynth);
        const auto set = [&] (const char* key, float value) { for (size_t i = 0; i < fields.size(); ++i) if (juce::String (fields[i].key) == key) poly.effect.values[i] = value; };
        set ("osc1Shape", 3); set ("osc2Level", 0); set ("filterType", 0); set ("lfoPitch", 0); set ("ampSustain", 1); set ("ampAttack", 2);
        ok (s.setNativeEffectSettings (poly.id, poly.effect), "a single sine oscillator");
        x = render (s, out);
        const auto polyEnd = frequency (x, 1, .5);
        check (std::abs (polyEnd / hz (72) - 1) < .003, "the Poly Synth's note bends to C5 too (" + juce::String (polyEnd, 1) + " Hz), though its own Bend range is 2");

        // Refusals change nothing.
        check (s.setMidiNoteExpression (clipId, bendId, Expression::pitchBend, { { 0, 60 } }).failed(), "a bend past 48 semitones is refused");
        check (s.setMidiNoteExpression (clipId, bendId, Expression::slide, { { 9, .5f } }).failed(), "a point after the note's end is refused");
        check (s.setMidiNoteExpression (clipId, bendId, Expression::pressure, { { 1, .5f }, { .5, .2f } }).failed(), "unordered points are refused");
        check (s.setMidiNoteExpression (clipId, "nothing", Expression::pressure, { { 0, .5f } }).failed() && notes()[0].pitchBend == octave, "an unknown note is refused");

        // Copies keep their expression; saved and reopened, and a malformed file refused.
        auto copy = notes()[0]; copy.startBeat = 4; copy.lengthBeats = 2;
        ok (s.addMidiNotes (clipId, { copy }, ids), "copy the note");
        check (notes().size() == 2 && notes()[1].pitchBend == octave, "the copy keeps the bend");
        ok (s.setMidiNoteExpression (clipId, notes()[0].id, Expression::slide, { { 0, .25f } }), "give it a slide");
        ok (s.setMidiNoteExpression (clipId, notes()[0].id, Expression::pressure, { { 0, .75f } }), "and a pressure");
        const auto project = root.getChildFile ("Mpe").getChildFile ("Mpe.halftrack");
        ok (s.saveProject (project), "save");
        {
            Session reopened (false);
            ok (reopened.loadProject (project), "reopen");
            const auto again = reopened.snapshot().tracks[(size_t) track].clips[0].notes;
            check (again.size() == 2 && again[0].pitchBend == octave && again[0].slide == std::vector<Session::ExpressionPoint> { { 0, .25f } }
                       && again[0].pressure == std::vector<Session::ExpressionPoint> { { 0, .75f } }, "the expression reopens");
            const auto text = project.loadFileAsString();
            const auto bad = root.getChildFile ("Bad").getChildFile ("Bad.halftrack");
            check (text.contains ("<PITCHBEND b=\"1.0\" v=\"12.0\"/>") && bad.getParentDirectory().createDirectory().wasOk()
                       && bad.replaceWithText (text.replace ("<PITCHBEND b=\"1.0\" v=\"12.0\"/>", "<PITCHBEND b=\"1.0\" v=\"100.0\"/>")),
                   "write a bend of 100 semitones");
            check (reopened.loadProject (bad).failed() && reopened.snapshot().tracks[(size_t) track].clips[0].notes[0].pitchBend == octave, "it is refused, keeping the open project");
        }

        // Live playback through a MIDI Monitor: each note on its own channel, with its range, slide and pressure.
        {
            Session live (false, makeSyntheticTestDeviceType());
            ok (live.loadProject (project), "open it for playback");
            ok (live.addNativeEffect (track, Session::EffectKind::midiMonitor), "monitor the MIDI the instrument receives");
            const auto monitor = live.snapshot().tracks[(size_t) track].plugins.back().id;
            pumpSyntheticTestDevice (live, 8);
            live.seek (0); ok (live.play(), "play");
            pumpSyntheticTestDevice (live, 600);
            live.stop();
            const auto reading = live.midiMonitor (monitor);
            check (reading.has_value(), "read the monitor");
            int firstChannel = 0, secondChannel = 0; bool range = false, slide = false, pressure = false;
            for (const auto& m : reading->messages)
            {
                const int type = m.status & 0xf0, channel = (m.status & 0x0f) + 1;
                if (type == 0x90 && m.data2 > 0) { if (firstChannel == 0) firstChannel = channel; else if (secondChannel == 0) secondChannel = channel; }
                if (type == 0xb0 && m.data1 == 6 && m.data2 == 48 && channel > 1) range = true;
                if (type == 0xb0 && m.data1 == 74 && m.data2 == 32 && channel == firstChannel) slide = true;
                if (type == 0xd0 && m.data1 == 95 && channel == firstChannel) pressure = true;
            }
            check (firstChannel > 1 && secondChannel > 1 && firstChannel != secondChannel,
                   "the notes play on channels of their own (" + juce::String (firstChannel) + " and " + juce::String (secondChannel) + ")");
            check (range && slide && pressure, "each gets a 48-semitone bend range, and the first its slide and pressure");
        }
        // Recording from an MPE controller keeps each note's own bend and pressure.
        {
            Session recorder (false, makeSyntheticTestDeviceType());
            ok (recorder.addMidiTrack ("Played"), "add a MIDI track to record");
            const int played = (int) recorder.snapshot().tracks.size() - 1;
            Controller controller (recorder);
            check (! recorder.isMidiInputMpe (controller.name), "inputs start without MPE");
            ok (recorder.setMidiInputMpe (controller.name, true), "turn on MPE for the controller");
            check (recorder.isMidiInputMpe (controller.name) && recorder.setMidiInputMpe ("nothing", true).failed(), "it reports MPE; unknown inputs are refused");
            ok (recorder.setTrackInput (played, controller.name), "route it"); ok (recorder.setTrackArmed (played, true), "arm the track");
            pumpSyntheticTestDevice (recorder, 8);
            recorder.seek (0); ok (recorder.record(), "record");
            for (int i = 0; i < 400 && recorder.snapshot().position < .25; ++i) pumpSyntheticTestDevice (recorder, 1);
            controller.now (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100));
            pumpSyntheticTestDevice (recorder, 20);
            controller.now (juce::MidiMessage::pitchWheel (2, 8192 + 2048)); // +12 semitones of 48
            controller.now (juce::MidiMessage::channelPressureChange (2, 64));
            pumpSyntheticTestDevice (recorder, 90);
            controller.now (juce::MidiMessage::noteOff (2, 60));
            pumpSyntheticTestDevice (recorder, 40);
            recorder.stop();
            const auto clips = recorder.snapshot().tracks[(size_t) played].clips;
            check (clips.size() == 1 && clips[0].notes.size() == 1 && clips[0].notes[0].pitch == 60, "the take holds the played note");
            const auto& taken = clips[0].notes[0];
            const auto bendTop = taken.pitchBend.empty() ? 0.0f : std::max_element (taken.pitchBend.begin(), taken.pitchBend.end(), [] (auto& a, auto& b) { return a.value < b.value; })->value;
            const auto pressureTop = taken.pressure.empty() ? 0.0f : std::max_element (taken.pressure.begin(), taken.pressure.end(), [] (auto& a, auto& b) { return a.value < b.value; })->value;
            check (std::abs (bendTop - 12) < .01f && std::abs (pressureTop - 64 / 127.0f) < .01f,
                   "with its own bend to " + juce::String (bendTop, 2) + " semitones and pressure " + juce::String (pressureTop, 3));
        }
        std::cout << "MPE passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL mpe: " << e.what() << std::endl; return 1; }
}
}
