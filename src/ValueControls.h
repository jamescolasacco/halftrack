#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

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

// A number field that drags as the reference's do: dragging up or down on one of its parts, the runs
// of digits in its text (a position's bar, beat and sixteenth, or a tempo's whole beats and
// hundredths), steps that part, at a tenth of the rate with Shift. A click without a drag, or a
// double-click, acts as the label's, and an uneditable field does not drag. The main window gives the
// field clicked last the value keys (see MainComponent): Up and Down step the part clicked last.
class ValueField : public juce::Label
{
public:
    using juce::Label::Label;
    // The pixels of drag a step takes. The reference's rate is unmeasured.
    static constexpr float pixelsPerStep = 3.0f;
    // A drag on a part, with the steps since it began, upward positive. Without it the field does not drag.
    std::function<void (int part, int steps)> onDrag;
    std::function<void()> onDragStart, onDragEnd;
    // What Delete does, if anything.
    std::function<void()> onReset;
    // The part clicked last, which the keys step.
    int part = 0;
    // Whether the parts run from coarse to fine, so that Shift with the arrow keys steps the last part,
    // for a fine adjustment. A time signature's two numbers do not.
    bool finestWithShift = true;

    // A step from the keys: one drag gesture of the given steps.
    void step (int steps, bool shift)
    {
        if (onDrag == nullptr || ! isEnabled() || ! isEditable()) return;
        const auto stepped = shift && finestWithShift ? juce::jmax (0, (int) partBounds().size() - 1) : part;
        if (onDragStart != nullptr) onDragStart();
        onDrag (stepped, steps);
        if (onDragEnd != nullptr) onDragEnd();
    }

    // Where each part's digits are drawn, in order.
    std::vector<juce::Rectangle<float>> partBounds()
    {
        const auto font = getLookAndFeel().getLabelFont (*this);
        const auto area = getLookAndFeel().getLabelBorderSize (*this).subtractedFrom (getLocalBounds()).toFloat();
        juce::GlyphArrangement glyphs;
        glyphs.addFittedText (font, getText(), area.getX(), area.getY(), area.getWidth(), area.getHeight(), getJustificationType(),
                              juce::jmax (1, (int) (area.getHeight() / font.getHeight())), getMinimumHorizontalScale());
        std::vector<juce::Rectangle<float>> parts;
        bool inPart = false;
        for (int i = 0; i < glyphs.getNumGlyphs(); ++i)
        {
            const auto& glyph = glyphs.getGlyph (i);
            const bool digit = juce::CharacterFunctions::isDigit (glyph.getCharacter());
            if (digit && ! inPart) parts.push_back (glyph.getBounds());
            else if (digit) parts.back() = parts.back().getUnion (glyph.getBounds());
            inPart = digit;
        }
        return parts;
    }
    // The part at a point across the field: the one under it, or the nearest.
    int partAt (float x)
    {
        const auto parts = partBounds();
        int nearest = 0;
        float distance = std::numeric_limits<float>::max();
        for (int i = 0; i < (int) parts.size(); ++i)
        {
            const auto& r = parts[(size_t) i];
            const auto d = x < r.getX() ? r.getX() - x : x > r.getRight() ? x - r.getRight() : 0.0f;
            if (d < distance) { distance = d; nearest = i; }
        }
        return nearest;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        part = partAt (e.position.x);
        travel = 0; steps = 0; lastY = e.position.y; dragging = false;
        juce::Label::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (onDrag == nullptr || ! isEnabled() || ! isEditable() || isBeingEdited() || e.mods.isPopupMenu()) { juce::Label::mouseDrag (e); return; }
        travel += (lastY - e.position.y) * (e.mods.isShiftDown() ? 0.1f : 1.0f);
        lastY = e.position.y;
        const auto now = (int) (travel / pixelsPerStep);
        if (now == steps) return;
        if (! std::exchange (dragging, true) && onDragStart != nullptr) onDragStart();
        steps = now;
        onDrag (part, steps);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (std::exchange (dragging, false)) { if (onDragEnd != nullptr) onDragEnd(); return; }
        juce::Label::mouseUp (e);
    }

private:
    float travel = 0, lastY = 0;
    int steps = 0;
    bool dragging = false;
};
} // namespace halftrack
