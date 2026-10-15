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
   #if HALFTRACK_TESTS_ENABLED
    static int runInteractionTests();
    static int runExportInteractionTests();
   #endif

private:
    class Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
}
