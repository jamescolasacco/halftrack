#include "ComputerKeyboard.h"
#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// The computer MIDI keyboard: its key layout, octave and velocity keys and their limits, held keys
// ending the notes they began; then the Computer Keyboard input through the synthetic device,
// played on a monitored track and through All MIDI Ins, recorded into a clip, and silenced when
// the keyboard is turned off.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS computer keyboard: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
float peak (Session& s, int blocks) { const auto x = captureSyntheticTestDevice (s, blocks); return x.getMagnitude (0, 0, x.getNumSamples()); }
// The note a key press plays, or -1.
int pressed (ComputerKeyboard& keys, juce::juce_wchar key)
{ const auto event = keys.press ((int) juce::CharacterFunctions::toUpperCase (key), key); return event ? event->note : -1; }
}

int runComputerKeyboardTests (const juce::File&)
{
    try
    {
        // The keys.
        check (ComputerKeyboard::offsetOf ('a') == 0 && ComputerKeyboard::offsetOf ('W') == 1 && ComputerKeyboard::offsetOf ('k') == 12
                   && ComputerKeyboard::offsetOf ('\'') == 17 && ComputerKeyboard::offsetOf ('q') == -1 && ComputerKeyboard::handles ('Z') && ! ComputerKeyboard::handles ('m'),
               "A to ' play an octave and a half from C, W to P the black keys; Z X C V are controls");
        {
            ComputerKeyboard keys;
            const auto first = keys.press ('A', 'a');
            check (first && first->on && first->note == 60 && std::abs (first->velocity - 100.0f / 127) < 1e-6f, "A plays C3 (note 60) at velocity 100");
            check (! keys.press ('A', 'a') && keys.release ([] (int) { return true; }).empty(), "a held key repeating plays nothing more");
            check (keys.release ([] (int code) { return code != 'A'; }) == std::vector<ComputerKeyboard::Event> { { 60, 0, false } } && ! keys.holding(), "releasing it ends note 60");
            check (pressed (keys, 'j') == 71 && pressed (keys, 'k') == 72 && pressed (keys, ';') == 76, "J is B3, K is C4 and ; is E4");
            keys.releaseAll();
        }
        {
            ComputerKeyboard keys;
            keys.press ('X', 'x');
            check (keys.octave() == 4 && pressed (keys, 'a') == 72, "X moves up an octave");
            keys.releaseAll();
            for (int i = 0; i < 9; ++i) keys.press ('Z', 'z');
            check (keys.octave() == -2 && pressed (keys, 'a') == 0, "Z moves down, to C-2 at the lowest");
            keys.releaseAll();
            for (int i = 0; i < 12; ++i) keys.press ('X', 'x');
            check (keys.octave() == 8 && pressed (keys, 'a') == 120 && pressed (keys, 'g') == 127 && pressed (keys, 'y') == -1, "and up to C8, where keys above G8 play nothing");
            keys.releaseAll();
        }
        {
            ComputerKeyboard keys;
            std::vector<int> values;
            for (int i = 0; i < 3; ++i) { keys.press ('V', 'v'); values.push_back (keys.velocity()); }
            for (int i = 0; i < 9; ++i) { keys.press ('C', 'c'); values.push_back (keys.velocity()); }
            keys.press ('V', 'v'); values.push_back (keys.velocity());
            check (values == std::vector<int> { 120, 127, 127, 120, 100, 80, 60, 40, 20, 1, 1, 1, 20 }, "V and C step the velocity by 20 between 1 and 127");
        }
        {
            ComputerKeyboard keys;
            pressed (keys, 'a'); keys.press ('X', 'x');
            check (keys.release ([] (int) { return false; }) == std::vector<ComputerKeyboard::Event> { { 60, 0, false } }, "a key held across an octave change ends the note it began");
        }

        // The input.
        Session s (false, makeSyntheticTestDeviceType());
        check (s.computerKeyboardNote (60, .8f, true).failed() && ! s.isComputerKeyboardEnabled(), "while it is off, notes are refused");
        ok (s.setComputerKeyboardEnabled (true), "turn the computer keyboard on");
        const auto midiInputs = s.inputNames (true);
        check (std::count (midiInputs.begin(), midiInputs.end(), juce::String (Session::computerKeyboardName)) == 1, "Computer Keyboard is a MIDI input, once");
        check (s.computerKeyboardNote (128, .8f, true).failed() && s.computerKeyboardNote (60, 1.5f, true).failed(), "notes and velocities out of range are refused");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const int keys = (int) s.snapshot().tracks.size() - 1;
        for (int t = 0; t < keys; ++t) ok (s.setTrackMute (t, true), "mute the other tracks");
        Session::SynthSettings sine; sine.waveform = 2; sine.attackSeconds = .002f; sine.sustain = 1; sine.releaseSeconds = .05f; sine.cutoffHz = 20000;
        ok (s.setSynthSettings (s.snapshot().tracks[(size_t) keys].id, sine), "a sine voice");
        ok (s.setTrackInput (keys, Session::computerKeyboardName), "Keys takes the Computer Keyboard");
        ok (s.setTrackMonitor (keys, Session::MonitorMode::on), "and monitors it");
        pumpSyntheticTestDevice (s, 8);
        check (peak (s, 10) < 1e-4f, "silent before a key");
        ok (s.computerKeyboardNote (60, .8f, true), "press a key");
        pumpSyntheticTestDevice (s, 4);
        check (peak (s, 20) > .05f, "the note sounds on the monitored track");
        ok (s.computerKeyboardNote (60, 0, false), "release it");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        check (peak (s, 10) < 1e-4f, "and it ends");

        // All MIDI Ins includes the computer keyboard.
        ok (s.setTrackInput (keys, "All MIDI Ins"), "Keys takes All MIDI Ins instead");
        ok (s.setTrackMonitor (keys, Session::MonitorMode::on), "and monitors it"); // A new input starts unmonitored.
        pumpSyntheticTestDevice (s, 8);
        ok (s.computerKeyboardNote (64, .8f, true), "press a key");
        pumpSyntheticTestDevice (s, 4);
        check (peak (s, 20) > .05f, "All MIDI Ins plays it");
        ok (s.computerKeyboardNote (64, 0, false), "release it");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);

        // Recording.
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-computer-keyboard-" + juce::Uuid().toString());
        ok (s.saveProject (root.getChildFile ("Keys").getChildFile ("Keys.halftrack")), "save a recording destination");
        ok (s.setTrackInput (keys, Session::computerKeyboardName), "Keys takes the Computer Keyboard again");
        ok (s.setTrackArmed (keys, true), "arm Keys");
        pumpSyntheticTestDevice (s, 8);
        s.seek (0); ok (s.record(), "record");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        ok (s.computerKeyboardNote (67, .8f, true), "play G3");
        pumpSyntheticTestDevice (s, blocksPerSecond / 2);
        ok (s.computerKeyboardNote (67, 0, false), "release it");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        s.stop(); pumpSyntheticTestDevice (s, 8);
        const auto clips = s.snapshot().tracks[(size_t) keys].clips;
        check (clips.size() == 1 && clips[0].notes.size() == 1 && clips[0].notes[0].pitch == 67 && std::abs (clips[0].notes[0].velocity - .8f) < .01f
                   && std::abs (clips[0].notes[0].lengthBeats - 1) < .1, "the recorded clip holds G3 at velocity 0.8 for about a beat");
        ok (s.setTrackArmed (keys, false), "disarm");
        ok (s.setTrackMonitor (keys, Session::MonitorMode::on), "monitor again");
        pumpSyntheticTestDevice (s, 8);

        // Three stops within 3.5 seconds of each other release held notes, as the reference's do (static reading).
        // Moving the play head releases them too, so once Stop has returned it to the start the note is held there.
        s.setInsertMarker (2);
        check (s.stopPressed() && s.snapshot().insertMarker == 0, "Stop while stopped returns the insert marker to the start");
        pumpSyntheticTestDevice (s, 8);
        ok (s.computerKeyboardNote (69, .8f, true), "hold A3");
        pumpSyntheticTestDevice (s, 4);
        check (peak (s, 20) > .05f, "it sounds");
        s.stopPressed(); pumpSyntheticTestDevice (s, 4);
        check (peak (s, 20) > .05f, "after a second stop the note still sounds");
        s.stopPressed(); pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        check (peak (s, 10) < 1e-4f, "a third within 3.5 seconds releases it");
        ok (s.computerKeyboardNote (69, 0, false), "let go of the key");
        ok (s.play(), "play");
        pumpSyntheticTestDevice (s, 4);
        check (! s.stopPressed() && ! s.snapshot().playing, "while playing, Stop stops");
        pumpSyntheticTestDevice (s, 8);

        // Turning the keyboard off ends its held notes.
        ok (s.computerKeyboardNote (72, .8f, true), "press a key");
        pumpSyntheticTestDevice (s, 4);
        check (peak (s, 20) > .05f, "it sounds");
        ok (s.setComputerKeyboardEnabled (false), "turn the keyboard off while it is held");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        check (peak (s, 10) < 1e-4f && s.computerKeyboardNote (72, 0, false).failed(), "the note ends, and keys are refused again");
        root.deleteRecursively();
        std::cout << "Computer keyboard passed" << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL computer keyboard: " << e.what() << std::endl; return 1; }
}
}
