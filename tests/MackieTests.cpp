#include "MackieControl.h"
#include "Session.h"
#include <iostream>
#include <stdexcept>

// The Mackie Control protocol: what a surface's faders, V-Pots, buttons and jog wheel say, and the
// feedback that moves its faders, lights its buttons and rings and writes its scribble strips.
// Then a surface on two virtual MIDI ports the test makes, driving a Session: faders, pans, Mute,
// transport and banking, one undo step per touched fader move, and the feedback that follows.
// Platforms without virtual MIDI ports skip that part.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS mackie: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
}

int runMackieProtocolChecks()
{
    using namespace mackie;
    // Faders: pitch bend on channels 1-8, the master on 9.
    auto a = parse (juce::MidiMessage::pitchWheel (3, 16383));
    check (a && a->kind == Action::Kind::fader && a->strip == 2 && std::abs (a->position - 1.0f) < 1e-6f, "strip 3's fader at the top");
    a = parse (juce::MidiMessage::pitchWheel (9, 8192));
    check (a && a->strip == 8 && std::abs (a->position - .5f) < .001f, "the master fader halfway");
    check (! parse (juce::MidiMessage::pitchWheel (10, 0)), "pitch bend on other channels is not a fader");
    // V-Pots and the jog wheel turn relatively.
    a = parse (juce::MidiMessage::controllerEvent (1, 17, 3));
    check (a && a->kind == Action::Kind::vpot && a->strip == 1 && a->ticks == 3, "strip 2's V-Pot three ticks clockwise");
    a = parse (juce::MidiMessage::controllerEvent (1, 23, 0x40 | 2));
    check (a && a->strip == 7 && a->ticks == -2, "strip 8's V-Pot two ticks counterclockwise");
    a = parse (juce::MidiMessage::controllerEvent (1, 60, 0x41));
    check (a && a->kind == Action::Kind::jog && a->ticks == -1, "the jog wheel back a tick");
    check (! parse (juce::MidiMessage::controllerEvent (1, 7, 64)), "other controllers are not the surface's");
    // Buttons press with a note on and release with velocity 0.
    const std::vector<std::tuple<int, Button, int>> buttons { { 0, Button::recArm, 0 }, { 13, Button::solo, 5 }, { 19, Button::mute, 3 }, { 31, Button::select, 7 },
        { 33, Button::vpotPush, 1 }, { 46, Button::bankLeft, 0 }, { 47, Button::bankRight, 0 }, { 48, Button::channelLeft, 0 }, { 49, Button::channelRight, 0 },
        { 86, Button::cycle, 0 }, { 89, Button::click, 0 }, { 91, Button::rewind, 0 }, { 92, Button::fastForward, 0 }, { 93, Button::stop, 0 },
        { 94, Button::play, 0 }, { 95, Button::record, 0 }, { 104, Button::faderTouch, 0 }, { 112, Button::faderTouch, 8 } };
    for (const auto& [note, button, strip] : buttons)
    {
        a = parse (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127));
        check (a && a->kind == Action::Kind::button && a->button == button && a->strip == strip && a->pressed && noteFor (button, strip) == note,
               "note " + juce::String (note) + " presses its button");
    }
    a = parse (juce::MidiMessage::noteOn (1, 94, (juce::uint8) 0));
    check (a && a->button == Button::play && ! a->pressed, "velocity 0 releases it");
    // Feedback.
    const auto fader = faderPosition (4, .5f);
    check (fader.isPitchWheel() && fader.getChannel() == 5 && std::abs (fader.getPitchWheelValue() - 8192) <= 1, "a fader moves to a position on its channel");
    const auto light = buttonLight (Button::mute, 2, true), dark = buttonLight (Button::mute, 2, false);
    check (light.isNoteOn() && light.getNoteNumber() == 18 && light.getVelocity() == 127 && dark.getRawData()[2] == 0, "a button lights and goes dark");
    check (vpotRing (0, 0).getControllerNumber() == 48 && vpotRing (0, 0).getControllerValue() == (0x10 | 6) && vpotRing (7, -1).getControllerValue() == (0x10 | 1)
               && vpotRing (7, 1).getControllerValue() == (0x10 | 11), "a V-Pot ring shows centre, hard left and hard right");
    check (meter (3, 1.0f).getRawData()[1] == ((3 << 4) | 12) && meter (3, 0.0f).getRawData()[1] == (3 << 4), "a meter shows 0 dB full and silence empty");
    const auto text = display (1, 14, stripText ("Vocals Lead"));
    const auto* raw = text.getRawData();
    check (text.isSysEx() && raw[1] == 0x00 && raw[3] == 0x66 && raw[4] == 0x14 && raw[5] == 0x12 && raw[6] == 56 + 14
               && juce::String::fromUTF8 ((const char*) raw + 7, 7) == "Vocals ", "the scribble strip gets a name fitted to seven characters");
    std::cout << "Mackie protocol passed" << std::endl;
    return 0;
}

namespace
{
struct Received final : juce::MidiInputCallback
{
    juce::CriticalSection lock;
    std::vector<juce::MidiMessage> messages;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override { const juce::ScopedLock sl (lock); messages.push_back (m); }
    std::vector<juce::MidiMessage> take() { const juce::ScopedLock sl (lock); auto r = std::move (messages); messages.clear(); return r; }
};
}

int runMackieTests (const juce::File&)
{
    if (runMackieProtocolChecks() != 0) return 1;
    try
    {
        const auto prefix = "Halftrack Test Surface " + juce::Uuid().toString().substring (0, 8);
        Received received;
        auto toHalftrack = juce::MidiOutput::createNewDevice (prefix + " Out");
        auto fromHalftrack = juce::MidiInput::createNewDevice (prefix + " In", &received);
        if (toHalftrack == nullptr || fromHalftrack == nullptr) { std::cout << "SKIP mackie surface: this system cannot make virtual MIDI ports" << std::endl; return 0; }
        fromHalftrack->start();
        Session s (false, makeSyntheticTestDeviceType(), prefix);
        const auto settle = [&] (int ms = 300) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
        // Waits for a result to arrive through the virtual ports, up to three seconds under load.
        const auto settleUntil = [&] (const std::function<bool()>& done) { for (int waited = 0; waited < 3000 && ! done(); waited += 20) settle (20); };
        const auto send = [&] (const juce::MidiMessage& m) { toHalftrack->sendMessageNow (m); };
        for (int i = 0; i < 50 && s.midiInputPorts().isEmpty(); ++i) settle (20); // The system lists a new port a moment later.
        check (s.midiInputPorts() == juce::StringArray { prefix + " Out" }, "the Session sees only the test's surface port among MIDI inputs (" + s.midiInputPorts().joinIntoString (", ") + ")");
        check (s.setControlSurface ({ "No Such Port", prefix + " In" }).failed(), "an unknown port is refused");
        ok (s.setControlSurface ({ prefix + " Out", prefix + " In" }), "choose the surface");
        settle();
        auto feedback = received.take();
        const auto names = s.snapshot().tracks;
        const auto hasText = [&] (const std::vector<juce::MidiMessage>& messages, const juce::String& text)
        {
            for (const auto& m : messages) if (m.isSysEx() && juce::String::fromUTF8 ((const char*) m.getSysExData() + 6, m.getSysExDataSize() - 6).contains (text)) return true;
            return false;
        };
        check (hasText (feedback, mackie::stripText (names[0].name).trim()), "the scribble strips show the tracks' names");
        const int unity = juce::roundToInt (Session::automationValueToProportion (Session::AutomationTarget::volume, 0) * 16383.0f);
        check (std::any_of (feedback.begin(), feedback.end(), [&] (const auto& m) { return m.isPitchWheel() && m.getChannel() == 1 && std::abs (m.getPitchWheelValue() - unity) <= 1; }),
               "track 1's fader moves to 0 dB");

        // A touched fader's moves are one undo step.
        const float minusSix = Session::automationValueToProportion (Session::AutomationTarget::volume, -6);
        send (juce::MidiMessage::noteOn (1, 104, (juce::uint8) 127));
        for (const float p : { minusSix + .1f, minusSix + .05f, minusSix }) send (mackie::faderPosition (0, p));
        send (juce::MidiMessage::noteOn (1, 104, (juce::uint8) 0));
        settle();
        settleUntil ([&] { return std::abs (s.snapshot().tracks[0].gainDb + 6) < .05f; });
        check (std::abs (s.snapshot().tracks[0].gainDb + 6) < .05f, "track 1's fader sets its volume to -6 dB (" + juce::String (s.snapshot().tracks[0].gainDb, 2) + ")");
        s.undo(); settle (100);
        check (std::abs (s.snapshot().tracks[0].gainDb) < .05f, "one undo restores 0 dB after the touched moves");
        s.redo();
        // The master fader and a V-Pot.
        send (mackie::faderPosition (8, minusSix)); settle();
        check (std::abs (s.snapshot().masterGainDb + 6) < .05f, "the master fader sets the master volume");
        send (juce::MidiMessage::controllerEvent (1, 17, 10)); settle();
        check (std::abs (s.snapshot().tracks[1].pan - .2f) < .001f, "strip 2's V-Pot pans track 2 right");
        received.take();
        send (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 127)); settle();
        check (std::abs (s.snapshot().tracks[1].pan) < .001f, "pressing it centres the pan");
        // Buttons light as they act.
        send (juce::MidiMessage::noteOn (1, 16, (juce::uint8) 127)); settle();
        feedback = received.take();
        check (s.snapshot().tracks[0].mute && std::any_of (feedback.begin(), feedback.end(), [] (const auto& m) { return m.isNoteOn() && m.getNoteNumber() == 16 && m.getVelocity() == 127; }),
               "Mute mutes track 1 and lights");
        send (juce::MidiMessage::noteOn (1, 16, (juce::uint8) 127)); settle();
        check (! s.snapshot().tracks[0].mute, "pressed again it unmutes");
        send (juce::MidiMessage::noteOn (1, 8, (juce::uint8) 127)); settle();
        check (s.snapshot().tracks[0].solo, "Solo solos track 1");
        send (juce::MidiMessage::noteOn (1, 8, (juce::uint8) 127)); settle();
        // Transport.
        pumpSyntheticTestDevice (s, 8);
        send (juce::MidiMessage::noteOn (1, 94, (juce::uint8) 127)); settle();
        pumpSyntheticTestDevice (s, 20);
        feedback = received.take();
        check (s.snapshot().playing && std::any_of (feedback.begin(), feedback.end(), [] (const auto& m) { return m.isNoteOn() && m.getNoteNumber() == 94 && m.getVelocity() == 127; }),
               "Play plays and lights");
        send (juce::MidiMessage::noteOn (1, 93, (juce::uint8) 127)); settle(); pumpSyntheticTestDevice (s, 8);
        check (! s.snapshot().playing, "Stop stops");
        s.seek (4); pumpSyntheticTestDevice (s, 8);
        check (std::abs (s.snapshot().position - 4) < 1e-6, "seek to 4 s");
        send (juce::MidiMessage::noteOn (1, 93, (juce::uint8) 127)); settle(); pumpSyntheticTestDevice (s, 8);
        // A slow machine can deliver the press later than the settle.
        for (int wait = 0; wait < 100 && std::abs (s.snapshot().position) >= 1e-6; ++wait) { settle (20); pumpSyntheticTestDevice (s, 1); }
        check (std::abs (s.snapshot().position) < 1e-6, "Stop while stopped returns to the start (" + juce::String (s.snapshot().position, 3) + " s)");
        const auto looping = s.snapshot().looping;
        send (juce::MidiMessage::noteOn (1, 86, (juce::uint8) 127)); settle();
        check (s.snapshot().looping != looping, "Cycle switches the loop");
        const auto clicking = s.snapshot().metronome.enabled;
        send (juce::MidiMessage::noteOn (1, 89, (juce::uint8) 127)); settle();
        check (s.snapshot().metronome.enabled != clicking, "Click switches the metronome");
        // Banks: with fewer than nine tracks the bank stays; one channel on shows the next track first.
        send (juce::MidiMessage::noteOn (1, 47, (juce::uint8) 127)); settle();
        check (s.controlSurfaceBank() == 0, "with eight tracks or fewer, Bank Right stays on the first bank");
        received.take();
        send (juce::MidiMessage::noteOn (1, 49, (juce::uint8) 127)); settle();
        check (s.controlSurfaceBank() == 1 && hasText (received.take(), mackie::stripText (names[1].name).trim()), "Channel Right moves the strips on by one, and the names follow");
        send (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 127)); settle();
        // Names follow renames.
        received.take();
        ok (s.renameTrack (0, "Drums"), "rename track 1");
        settle (200);
        check (hasText (received.take(), "Drums"), "the scribble strip shows the new name");
        ok (s.setControlSurface ({}), "remove the surface");
        check (s.controlSurface() == Session::ControlSurface {}, "no surface is chosen");
        fromHalftrack->stop();
        std::cout << "Mackie surface passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL mackie: " << e.what() << std::endl; return 1; }
}
}
