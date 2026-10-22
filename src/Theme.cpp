#include "Theme.h"
#include <atomic>
#include <cmath>

namespace halftrack::themes
{
namespace
{
std::atomic<int> active { 0 };
float linear (float channel) { return channel <= 0.04045f ? channel / 12.92f : std::pow ((channel + 0.055f) / 1.055f, 2.4f); }
float encoded (float linearValue) { return linearValue <= 0.0031308f ? linearValue * 12.92f : 1.055f * std::pow (linearValue, 1.0f / 2.4f) - 0.055f; }
// Light's gray ramp inverts luminance about the contrast formula's offset, (L + 0.05)(L' + 0.05) = k:
// every contrast ratio between two grays is kept, the darkest gray becomes the lightest, and
// applying it twice gives the gray back. k puts Dark's background (0x1d) at a light 0xe4.
float lightGray (float value)
{
    constexpr float k = 0.0517f;
    const float inverted = juce::jlimit (0.0f, 1.0f, k / (linear (value / 255.0f) + 0.05f) - 0.05f);
    return 255.0f * encoded (inverted);
}
// High Contrast's gray ramp keeps the darks apart, so separators still show on near-black panels,
// and pushes the lights toward white: near-black compresses, darks spread by a fifth, lights
// close up on white. Each piece inverts; nothing maps between them.
float highContrastGray (float v)
{
    if (v < 14.0f) return v * 0.4f;
    if (v < 128.0f) return 5.6f + (v - 14.0f) * 1.2f;
    return 255.0f - (255.0f - v) * 0.4f;
}
float highContrastGrayInverse (float v)
{
    if (v <= 5.6f) return v / 0.4f;
    if (v <= 142.4f) return 14.0f + (v - 5.6f) / 1.2f;
    return 255.0f - (255.0f - v) / 0.4f;
}
double luminance (juce::Colour c) { return 0.2126 * linear (c.getFloatRed()) + 0.7152 * linear (c.getFloatGreen()) + 0.0722 * linear (c.getFloatBlue()); }
}

const char* name (Kind kind)
{
    switch (kind)
    {
        case Kind::light: return "Light";
        case Kind::highContrast: return "High Contrast";
        case Kind::dark: break;
    }
    return "Dark";
}

void set (Kind kind) { active = juce::jlimit (0, kindCount - 1, static_cast<int> (kind)); }
Kind current() { return static_cast<Kind> (active.load()); }

juce::Colour colour (juce::uint32 dark, Kind kind)
{
    const juce::Colour c (dark);
    if (kind == Kind::dark) return c;
    const auto r = c.getRed(), g = c.getGreen(), b = c.getBlue();
    const bool neutral = std::max ({ r, g, b }) - std::min ({ r, g, b }) <= 12;
    if (neutral)
    {
        // Grays: Light inverts the ramp, so dark panels become light ones and light text dark;
        // High Contrast spreads the darks and pushes the lights toward white.
        const float v = (r + g + b) / 3.0f;
        const float mapped = kind == Kind::light ? lightGray (v) : highContrastGray (v);
        const auto level = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (mapped));
        return juce::Colour (level, level, level, c.getAlpha());
    }
    // Colours keep their hue: on Light they darken so accents and meters stand out from light
    // panels; High Contrast keeps them as they are.
    return kind == Kind::light ? c.withMultipliedBrightness (0.5f) : c;
}

juce::Colour toDark (juce::Colour shown, Kind kind)
{
    if (kind == Kind::dark) return shown;
    const auto r = shown.getRed(), g = shown.getGreen(), b = shown.getBlue();
    if (std::max ({ r, g, b }) - std::min ({ r, g, b }) <= 12)
    {
        const float v = (r + g + b) / 3.0f;
        const float dark = kind == Kind::light ? lightGray (v) : highContrastGrayInverse (v);
        const auto level = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (dark));
        return juce::Colour (level, level, level, shown.getAlpha());
    }
    return kind == Kind::light ? shown.withMultipliedBrightness (2.0f) : shown;
}

void recolour (juce::Component& component, Kind from, Kind to)
{
    // The engine of setColour: each colour a component sets is a "jcclr_<id>" property.
    auto& properties = component.getProperties();
    juce::StringArray ids;
    for (const auto& property : properties)
        if (const auto name = property.name.toString(); name.startsWith ("jcclr_")) ids.add (name.substring (6));
    for (const auto& id : ids)
    {
        const juce::Identifier shown ("jcclr_" + id), original ("halftrackDarkColour_" + id);
        // A colour the component has set since its original was kept is the one to follow.
        const juce::Colour now ((juce::uint32) (int) properties[shown]);
        if (! properties.contains (original) || colour ((juce::uint32) (int) properties[original], from) != now) properties.set (original, (int) toDark (now, from).getARGB());
        component.setColour (id.getHexValue32(), colour ((juce::uint32) (int) properties[original], to));
    }
    for (auto* child : component.getChildren()) recolour (*child, from, to);
}

void apply (Kind next)
{
    const auto from = current();
    if (from == next) return;
    set (next);
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* window = desktop.getComponent (i)) { recolour (*window, from, next); window->repaint(); }
}

double contrast (juce::Colour foreground, juce::Colour background)
{
    const auto backdrop = juce::Colours::black.overlaidWith (background);
    const auto front = backdrop.overlaidWith (foreground);
    const auto a = luminance (front), b = luminance (backdrop);
    return (std::max (a, b) + 0.05) / (std::min (a, b) + 0.05);
}
}
