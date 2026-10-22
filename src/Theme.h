#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace halftrack::themes
{
// Halftrack's interface themes. Every colour is authored for Dark; the others map it: neutral grays
// through the theme's gray ramp, and colours keeping their hue.
enum class Kind { dark, light, highContrast };
constexpr int kindCount = 3;
const char* name (Kind);

// The theme the interface draws with. Message thread only.
void set (Kind);
Kind current();

// A colour as the given (or current) theme shows it, from the colour Dark uses.
juce::Colour colour (juce::uint32 dark, Kind);
inline juce::Colour colour (juce::uint32 dark) { return colour (dark, current()); }

// The colour Dark uses for one the given theme shows: the inverse of colour, to rounding.
juce::Colour toDark (juce::Colour shown, Kind);

// Switches the theme and recolours the colours components have set on themselves, in every window
// on the desktop. Each component keeps its colours' Dark originals, so switching back and forth
// does not drift. Painting reads the theme as it draws; look-and-feel colours are the caller's.
void apply (Kind);
void recolour (juce::Component&, Kind from, Kind to);

// The WCAG 2 contrast ratio of two colours, each first drawn over an opaque backdrop.
double contrast (juce::Colour foreground, juce::Colour background);

// A colour authored for Dark that follows the current theme wherever a juce::Colour is wanted.
struct Colour
{
    juce::uint32 dark;
    operator juce::Colour() const { return colour (dark); }
    juce::Colour get() const { return colour (dark); }
    juce::Colour withAlpha (float alpha) const { return get().withAlpha (alpha); }
    juce::Colour withMultipliedAlpha (float alpha) const { return get().withMultipliedAlpha (alpha); }
    juce::Colour brighter (float amount = 0.4f) const { return get().brighter (amount); }
    juce::Colour darker (float amount = 0.4f) const { return get().darker (amount); }
    juce::Colour interpolatedWith (juce::Colour other, float proportion) const { return get().interpolatedWith (other, proportion); }
    juce::Colour withMultipliedBrightness (float amount) const { return get().withMultipliedBrightness (amount); }
    juce::Colour withMultipliedSaturation (float amount) const { return get().withMultipliedSaturation (amount); }
    juce::Colour contrasting (float amount = 1.0f) const { return get().contrasting (amount); }
    juce::uint32 getARGB() const { return get().getARGB(); }
};
}
