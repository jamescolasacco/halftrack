#pragma once
#include "Session.h"
#include <memory>
namespace halftrack
{
std::unique_ptr<juce::Component> makeAudioClipEditor (Session&, const juce::String& clipId);
#if HALFTRACK_TESTS_ENABLED
int runAudioClipEditorTests (const juce::File& source, const juce::File& artifacts);
#endif
}
