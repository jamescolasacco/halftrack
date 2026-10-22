#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>

namespace halftrack
{
class Session;

class MainComponent final : public juce::Component
{
public:
    explicit MainComponent (Session&);
    ~MainComponent() override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void requestClose (std::function<void()> onClose);
    // Keeps a recovery copy of unsaved work in this file and offers to recover one left by a crash.
    void enableRecovery (const juce::File&);
    // The user library holds device presets (Presets/<device>/) and the default set for new projects.
    void setUserLibrary (const juce::File&);
   #if HALFTRACK_TESTS_ENABLED
    static int runInteractionTests();
    static int runExportInteractionTests();
    // Audio-thread separation: recording from a real-time device thread while the interface works.
    static int runSeparationStressTests (const juce::File& artifacts);
   #endif

private:
    class Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
}
