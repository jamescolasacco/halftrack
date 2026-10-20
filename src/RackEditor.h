#pragma once
#include "Session.h"
#include <functional>
#include <memory>

namespace halftrack
{
// The inline device view of a rack: its macros (with a mapping list), its chains (or a Drum
// Rack's pads), the key, velocity and chain select zone editor (or the selected pad's note,
// choke and sends) and the selected chain's devices. Every edit goes through the Session.
// showAutomation opens a control's automation lane (device id, parameter).
std::unique_ptr<juce::Component> makeRackEditor (Session&, const juce::String& rackId,
                                                 std::function<void (const juce::String&, int)> showAutomation = {});
#if HALFTRACK_TESTS_ENABLED
// Editor geometry for UI tests: a point on a zone editor row (its zone bar or its fade strip)
// at a value, and a pad's bounds in a Drum Rack's pad grid.
juce::Point<int> rackZonePoint (juce::Component& zoneEditor, int row, int value, bool fadeStrip);
juce::Rectangle<int> rackPadBounds (juce::Component& padGrid, int note);
#endif
}
