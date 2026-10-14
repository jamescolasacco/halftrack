#pragma once
#include "Session.h"
namespace halftrack
{
std::unique_ptr<juce::Component> makeUtilityEditor (Session&, const juce::String& pluginId);
#if HALFTRACK_TESTS_ENABLED
int runUtilityEditorTests (const juce::File&);
#endif
}
