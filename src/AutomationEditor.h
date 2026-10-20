#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

namespace halftrack
{
class Session;

// Message-thread editor for track volume/pan, native device control and master-volume automation.
class AutomationEditor final : public juce::Component
{
public:
    explicit AutomationEditor (Session&); // Opens the master-volume destination.
    AutomationEditor (Session&, const juce::String& trackId);
    // Opens a track's native device control (by parameter index) as the destination.
    AutomationEditor (Session&, const juce::String& trackId, const juce::String& pluginId, int parameter);
    ~AutomationEditor() override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
   #if HALFTRACK_TESTS_ENABLED
    friend int runAutomationEditorTests (const juce::File& artifactDirectory);
   #endif
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationEditor)
};

#if HALFTRACK_TESTS_ENABLED
int runAutomationEditorTests (const juce::File& artifactDirectory);
#endif
}
