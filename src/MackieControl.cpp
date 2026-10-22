#include "MackieControl.h"
#include <cmath>

namespace halftrack::mackie
{
namespace
{
constexpr int recArmNote = 0, soloNote = 8, muteNote = 16, selectNote = 24, vpotPushNote = 32, touchNote = 104;
// A relative turn: bit 6 says counterclockwise, the low six bits how far.
int ticksOf (int value) { return (value & 0x40) != 0 ? -(value & 0x3f) : (value & 0x3f); }
}

int noteFor (Button button, int strip)
{
    switch (button)
    {
        case Button::recArm: return recArmNote + strip;
        case Button::solo: return soloNote + strip;
        case Button::mute: return muteNote + strip;
        case Button::select: return selectNote + strip;
        case Button::vpotPush: return vpotPushNote + strip;
        case Button::bankLeft: return 46;
        case Button::bankRight: return 47;
        case Button::channelLeft: return 48;
        case Button::channelRight: return 49;
        case Button::cycle: return 86;
        case Button::click: return 89;
        case Button::rewind: return 91;
        case Button::fastForward: return 92;
        case Button::stop: return 93;
        case Button::play: return 94;
        case Button::record: return 95;
        case Button::faderTouch: return touchNote + strip;
        case Button::other: break;
    }
    return -1;
}

std::optional<Action> parse (const juce::MidiMessage& m)
{
    Action a;
    if (m.isPitchWheel())
    {
        if (m.getChannel() > 9) return std::nullopt;
        a.kind = Action::Kind::fader; a.strip = m.getChannel() - 1; a.position = (float) m.getPitchWheelValue() / 16383.0f;
        return a;
    }
    if (m.isController())
    {
        const int cc = m.getControllerNumber();
        if (cc >= 16 && cc <= 23) { a.kind = Action::Kind::vpot; a.strip = cc - 16; a.ticks = ticksOf (m.getControllerValue()); return a; }
        if (cc == 60) { a.kind = Action::Kind::jog; a.ticks = ticksOf (m.getControllerValue()); return a; }
        return std::nullopt;
    }
    if (m.isNoteOnOrOff())
    {
        const int note = m.getNoteNumber();
        a.kind = Action::Kind::button; a.pressed = m.isNoteOn();
        if (note < 8) { a.button = Button::recArm; a.strip = note; }
        else if (note < 16) { a.button = Button::solo; a.strip = note - soloNote; }
        else if (note < 24) { a.button = Button::mute; a.strip = note - muteNote; }
        else if (note < 32) { a.button = Button::select; a.strip = note - selectNote; }
        else if (note < 40) { a.button = Button::vpotPush; a.strip = note - vpotPushNote; }
        else if (note >= touchNote && note <= touchNote + 8) { a.button = Button::faderTouch; a.strip = note - touchNote; }
        else
        {
            for (const auto b : { Button::bankLeft, Button::bankRight, Button::channelLeft, Button::channelRight, Button::cycle, Button::click,
                                  Button::rewind, Button::fastForward, Button::stop, Button::play, Button::record })
                if (noteFor (b) == note) { a.button = b; return a; }
            a.button = Button::other;
        }
        return a;
    }
    return std::nullopt;
}

juce::MidiMessage faderPosition (int strip, float position)
{
    return juce::MidiMessage::pitchWheel (juce::jlimit (0, 8, strip) + 1, juce::jlimit (0, 16383, juce::roundToInt (position * 16383.0f)));
}

juce::MidiMessage buttonLight (Button button, int strip, bool on)
{
    return juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, noteFor (button, strip)), (juce::uint8) (on ? 127 : 0));
}

juce::MidiMessage vpotRing (int strip, float pan)
{
    // Pan mode: one light of eleven, the middle one centred.
    const int light = 1 + juce::jlimit (0, 10, juce::roundToInt ((juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 5.0f));
    return juce::MidiMessage::controllerEvent (1, 48 + juce::jlimit (0, 7, strip), 0x10 | light);
}

juce::MidiMessage meter (int strip, float peak)
{
    // Twelve segments over 60 dB, 0 dB at the top.
    const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
    const int level = juce::jlimit (0, 12, juce::roundToInt ((db + 60.0f) / 5.0f));
    return juce::MidiMessage::channelPressureChange (1, (juce::jlimit (0, 7, strip) << 4) | level);
}

juce::MidiMessage display (int row, int column, const juce::String& text)
{
    std::vector<juce::uint8> data { 0x00, 0x00, 0x66, 0x14, 0x12, (juce::uint8) juce::jlimit (0, 111, row * 56 + column) };
    for (int i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];
        data.push_back ((juce::uint8) (c >= 0x20 && c < 0x7f ? c : '?'));
    }
    return juce::MidiMessage::createSysExMessage (data.data(), (int) data.size());
}

juce::String stripText (const juce::String& name)
{
    auto text = name.trim().substring (0, 6);
    while (text.length() < 7) text << ' ';
    return text;
}
}
