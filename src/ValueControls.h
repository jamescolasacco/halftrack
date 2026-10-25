#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace halftrack
{
// A knob or fader that drags as the reference's do: while Shift is held, a drag moves the value at a
// tenth of its usual rate, for finer resolution (its manual, section 42.6, Adjusting Values). The main
// window gives the control clicked last the reference's value keys (see MainComponent).
class ValueSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;
    // The share of its usual rate a drag keeps while Shift is held. The reference's is unmeasured.
    static constexpr float fineRate = 0.1f;
    // A value in semitones, which Shift with the arrow keys steps by octaves.
    bool semitones = false;
    // What Delete does in place of returning to the double-click value, for a control whose reset
    // does more.
    std::function<void()> onReset;

    void mouseDown (const juce::MouseEvent& e) override { last = dragged = e.position; juce::Slider::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override { juce::Slider::mouseDrag (scaled (e)); }
    void mouseUp (const juce::MouseEvent& e) override { juce::Slider::mouseUp (scaled (e)); }

private:
    // The pointer as the slider sees it: each movement counts in full, or by the fine rate with Shift.
    juce::Point<float> last, dragged;
    juce::MouseEvent scaled (const juce::MouseEvent& e)
    {
        dragged += (e.position - last) * (e.mods.isShiftDown() ? fineRate : 1.0f);
        last = e.position;
        return e.withNewPosition (dragged);
    }
};
} // namespace halftrack
