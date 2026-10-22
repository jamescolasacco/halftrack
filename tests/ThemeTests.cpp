#include "Theme.h"
#include <iostream>
#include <stdexcept>

// Interface themes: each maps every Dark colour and back again, and keeps text, secondary text,
// selection and meters readable against the panels they sit on, by WCAG 2 contrast ratio (4.5
// for text, 3 for controls and large marks); disabled text stays legible and visibly dimmer.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS themes: " << text << std::endl; }
bool near (juce::Colour a, juce::Colour b, int tolerance)
{
    return std::abs (a.getRed() - b.getRed()) <= tolerance && std::abs (a.getGreen() - b.getGreen()) <= tolerance
        && std::abs (a.getBlue() - b.getBlue()) <= tolerance && a.getAlpha() == b.getAlpha();
}
// The interface's palette, as MainComponent.cpp authors it for Dark.
constexpr juce::uint32 background = 0xff1d1d1d, panel = 0xff2d2d2d, raised = 0xff3e3e3e, line = 0xff141414, ink = 0xffdadada,
                       muted = 0xff969696, orange = 0xffffab3d, red = 0xffed5249, green = 0xff48d17e, blue = 0xff3d8bff;
}

int runThemeTests (const juce::File&)
{
    try
    {
        using themes::Kind;
        check (themes::current() == Kind::dark, "Dark is the theme at first");
        for (int k = 0; k < themes::kindCount; ++k)
        {
            const auto kind = (Kind) k;
            const juce::String name = themes::name (kind);
            const auto c = [kind] (juce::uint32 dark) { return themes::colour (dark, kind); };
            // Every colour maps back to its Dark original, to rounding.
            bool inverse = true;
            for (const auto dark : { background, panel, raised, line, ink, muted, orange, red, green, blue, 0xff000000u, 0xffffffffu, 0x80303030u, 0xffe49a58u, 0xff5a6f78u })
                inverse = inverse && near (themes::toDark (c (dark), kind), juce::Colour (dark), 2);
            check (inverse, name + " maps every colour back to its Dark original");
            check (kind != Kind::dark || (c (panel) == juce::Colour (panel) && c (orange) == juce::Colour (orange)), name + " keeps Dark's colours as authored");
            const auto ratio = [&] (juce::uint32 front, juce::uint32 back) { return themes::contrast (c (front), c (back)); };
            const auto report = [&] (double value, double minimum, const juce::String& what)
            { check (value >= minimum, name + ": " + what + " " + juce::String (value, 2) + ":1, at least " + juce::String (minimum, 1)); };
            report (std::min ({ ratio (ink, background), ratio (ink, panel), ratio (ink, raised) }), 7.0, "text on backgrounds, panels and buttons");
            report (std::min (ratio (muted, panel), ratio (muted, background)), 4.5, "secondary text");
            report (ratio (orange, raised), 3.0, "selected menu text");
            report (std::min (ratio (orange, panel), ratio (blue, panel)), 3.0, "selection and mapping accents");
            // WCAG sets no minimum for inactive controls: disabled text stays legible and clearly dimmer.
            const auto disabled = themes::contrast (c (ink).withAlpha (0.5f), c (panel));
            report (disabled, 2.5, "disabled text");
            check (disabled < 0.6 * ratio (ink, panel), name + ": disabled text is clearly dimmer than enabled text");
            report (std::min (ratio (red, background), ratio (green, background)), 3.0, "clip and meter marks");
            report (ratio (line, panel), kind == Kind::highContrast ? 1.3 : 1.2, "separators");
        }
        // Light inverts the gray ramp: panels light, text dark.
        check (themes::colour (panel, Kind::light).getPerceivedBrightness() > 0.7f && themes::colour (ink, Kind::light).getPerceivedBrightness() < 0.2f,
               "Light shows light panels and dark text");
        check (themes::colour (panel, Kind::highContrast).getPerceivedBrightness() < juce::Colour (panel).getPerceivedBrightness()
                   && themes::colour (ink, Kind::highContrast).getPerceivedBrightness() > juce::Colour (ink).getPerceivedBrightness(),
               "High Contrast darkens panels and brightens text");
        std::cout << "Themes passed" << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL themes: " << e.what() << std::endl; return 1; }
}
}
