#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// External Sync, through a virtual MIDI port the test makes and sends MIDI Clock to in real time,
// while the synthetic device is pumped in step. The Session sees only that port. With the port's
// Sync switch on and External Sync off, the clock's tempo is read but not followed; with it on, the
// tempo follows the clock at 120 and then 90 BPM, the transport keeps the clock's position, and
// Start, Stop, Song Position and Continue move it. The test's own ticks jitter by a few
// milliseconds, so tempos are checked to half a BPM. Systems that cannot make virtual ports skip.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS external sync: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
}

int runExternalSyncTests (const juce::File&)
{
    try
    {
        const auto prefix = "Halftrack Sync " + juce::Uuid().toString().substring (0, 8);
        auto clock = juce::MidiOutput::createNewDevice (prefix + " Clock");
        if (clock == nullptr)
        {
            std::cout << "External sync skipped: this system cannot make a virtual MIDI port" << std::endl;
            return 0;
        }
        const auto port = prefix + " Clock";
        Session s (false, makeSyntheticTestDeviceType(), prefix);
        for (int wait = 0; wait < 100 && ! s.midiInputPorts().contains (port); ++wait) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check (s.midiInputPorts().contains (port), "the test's clock appears as a MIDI input");
        s.setTempo (100);
        const bool couldUndo = s.canUndo();
        ok (s.setMidiInputSync (port, true), "switch on its Sync");
        check (s.isMidiInputSync (port) && ! s.isExternalSync(), "it sends clock; External Sync is still off");

        // Sends clock ticks at a tempo for a time, pumping the device in real time beside them.
        const auto run = [&] (double bpm, double seconds)
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            const double tick = 60000.0 / (bpm * 24), block = 256000.0 / 48000;
            int ticks = 0, blocks = 0;
            for (auto now = start; now - start < seconds * 1000; now = juce::Time::getMillisecondCounterHiRes())
            {
                if (now >= start + ticks * tick) { clock->sendMessageNow (juce::MidiMessage::midiClock()); ++ticks; }
                if (now >= start + blocks * block) { pumpSyntheticTestDevice (s, 1); ++blocks; }
                else juce::Thread::yield();
            }
        };
        const auto beats = [&] { return s.snapshot().position * s.snapshot().tempo / 60; };
        run (120, 1.5);
        check (s.externalClock().receiving && std::abs (s.externalClock().bpm - 120) < .5 && std::abs (s.snapshot().tempo - 100) < 1e-9,
               "with External Sync off the clock's 120 BPM is read but not followed (" + juce::String (s.externalClock().bpm, 2) + " BPM)");
        ok (s.setExternalSync (true), "turn External Sync on");
        run (120, 1.5);
        check (std::abs (s.snapshot().tempo - 120) < .5, "the tempo follows the clock: " + juce::String (s.snapshot().tempo, 2) + " BPM");
        s.setTempo (130);
        check (std::abs (s.snapshot().tempo - 120) < .5, "the tempo cannot be set otherwise meanwhile");

        clock->sendMessageNow (juce::MidiMessage::midiStart());
        run (120, 2);
        check (s.snapshot().playing && std::abs (beats() - 4) < .15, "Start plays from the beginning, two seconds giving four beats (" + juce::String (beats(), 3) + ")");
        run (90, 3);
        check (std::abs (s.snapshot().tempo - 90) < .5 && std::abs (beats() - 8.5) < .2,
               "at 90 BPM the tempo follows (" + juce::String (s.snapshot().tempo, 2) + ") and the transport keeps the clock's 8.5 beats (" + juce::String (beats(), 3) + ")");
        clock->sendMessageNow (juce::MidiMessage::midiStop());
        run (90, .3);
        check (! s.snapshot().playing, "Stop stops it");
        clock->sendMessageNow (juce::MidiMessage::songPositionPointer (32));
        run (90, .3);
        check (std::abs (beats() - 8) < 1e-3, "Song Position moves it to the eighth beat while stopped (" + juce::String (beats(), 4) + ")");
        clock->sendMessageNow (juce::MidiMessage::midiContinue());
        run (90, 1);
        check (s.snapshot().playing && std::abs (beats() - 9.5) < .15, "Continue plays on from there (" + juce::String (beats(), 3) + ")");
        clock->sendMessageNow (juce::MidiMessage::midiStop());
        run (90, .3);
        check (s.canUndo() == couldUndo, "none of it is an undo step");

        ok (s.setExternalSync (false), "turn External Sync off");
        run (140, 1.5);
        check (std::abs (s.snapshot().tempo - 90) < .5 && std::abs (s.externalClock().bpm - 140) < .5, "off again, a 140 BPM clock leaves the tempo where it was");
        s.setTempo (110);
        check (std::abs (s.snapshot().tempo - 110) < 1e-9, "and the tempo can be set again");
        ok (s.setMidiInputSync (port, false), "switch the port's Sync off");
        run (140, .2);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
        check (! s.externalClock().receiving, "its clock is no longer read");
        check (s.setMidiInputSync ("No such port", true).failed(), "a missing port is refused");
        std::cout << "External sync passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL external sync: " << e.what() << std::endl; return 1; }
}
}
