#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace halftrack
{
namespace
{
namespace te = tracktion;
constexpr auto formatName = "Halftrack Async Test";
constexpr auto failureMessage = "The generated plug-in intentionally failed to initialize.";

void check (bool condition, const juce::String& message)
{
    if (! condition) throw std::runtime_error (message.toStdString());
    std::cout << "PASS editor " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{
    check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : ""));
}
bool pumpUntil (const std::function<bool()>& ready)
{
    for (int i = 0; i < 100 && ! ready(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return ready();
}
juce::PluginDescription description (bool generic = false)
{
    juce::PluginDescription result;
    result.name = generic ? "Generated Generic Editor" : "Generated Async Editor";
    result.descriptiveName = result.name;
    result.pluginFormatName = formatName;
    result.fileOrIdentifier = generic ? "halftrack-test:generic" : "halftrack-test:custom";
    result.manufacturerName = "Halftrack Tests";
    result.version = "1";
    result.uniqueId = generic ? 102 : 101;
    result.deprecatedUid = result.uniqueId;
    result.numInputChannels = result.numOutputChannels = 2;
    return result;
}
struct InstanceLifetime
{
    std::atomic<int> editors { 0 };
};
struct Probe
{
    struct Pending
    {
        juce::PluginDescription description;
        juce::AudioPluginFormat::PluginCreationCallback callback;
    };
    std::deque<Pending> pending;
    const std::thread::id messageThread = std::this_thread::get_id();
    std::atomic<int> nativeCompletions { 0 }, offThreadCompletions { 0 }, offThreadDisposals { 0 };
    std::atomic<int> instances { 0 }, editors { 0 }, editorCreations { 0 }, lifetimeViolations { 0 };
};
class TestParameter final : public juce::HostedAudioProcessorParameter
{
public:
    float getValue() const override { return value; }
    void setValue (float v) override { value = juce::jlimit (0.0f, 1.0f, v); }
    float getDefaultValue() const override { return 0.5f; }
    juce::String getName (int) const override { return "Generated gain"; }
    juce::String getLabel() const override { return {}; }
    juce::String getParameterID() const override { return "generated-gain"; }
    juce::String getText (float v, int) const override { return juce::String (v, 2); }
    float getValueForText (const juce::String& text) const override { return text.getFloatValue(); }
private:
    float value = 0.5f;
};
class TestEditor final : public juce::AudioProcessorEditor
{
public:
    TestEditor (juce::AudioProcessor& processor, std::shared_ptr<Probe> p, std::shared_ptr<InstanceLifetime> l)
        : AudioProcessorEditor (processor), probe (std::move (p)), lifetime (std::move (l))
    {
        ++probe->editors; ++probe->editorCreations; ++lifetime->editors;
        gain.setName ("Generated editor gain"); gain.setRange (0, 1, 0.01);
        gain.setValue (processor.getParameters()[0]->getValue(), juce::dontSendNotification);
        gain.onValueChange = [this] { getAudioProcessor()->getParameters()[0]->setValueNotifyingHost ((float) gain.getValue()); };
        addAndMakeVisible (gain); setSize (340, 160);
    }
    ~TestEditor() override
    {
        if (getAudioProcessor()->getActiveEditor() == this) ++probe->lifetimeViolations;
        --lifetime->editors; --probe->editors;
    }
    void resized() override { gain.setBounds (20, 50, getWidth() - 40, 40); }
    juce::Slider gain;
private:
    std::shared_ptr<Probe> probe;
    std::shared_ptr<InstanceLifetime> lifetime;
};
class TestInstance final : public juce::AudioPluginInstance
{
public:
    TestInstance (juce::PluginDescription d, std::shared_ptr<Probe> p)
        : AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                               .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          desc (std::move (d)), probe (std::move (p)), lifetime (std::make_shared<InstanceLifetime>())
    {
        ++probe->instances;
        addHostedParameter (std::make_unique<TestParameter>());
    }
    ~TestInstance() override
    {
        if (lifetime->editors != 0) ++probe->lifetimeViolations;
        if (std::this_thread::get_id() != probe->messageThread) ++probe->offThreadDisposals;
        --probe->instances;
    }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = desc; }
    const juce::String getName() const override { return desc.name; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool supportsMPE() const override
    {
        // Tracktion queries this hook during native instance completion. These
        // device-free tests never render, so it records initialization itself.
        ++probe->nativeCompletions;
        if (std::this_thread::get_id() != probe->messageThread) ++probe->offThreadCompletions;
        return false;
    }
    juce::AudioProcessorEditor* createEditor() override
    { return hasEditor() ? new TestEditor (*this, probe, lifetime) : nullptr; }
    bool hasEditor() const override { return desc.fileOrIdentifier.endsWith ("custom"); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& dest) override
    { const auto v = getParameters()[0]->getValue(); dest.replaceAll (&v, sizeof (v)); }
    void setStateInformation (const void* data, int bytes) override
    {
        if (bytes == sizeof (float))
        {
            float value = 0; std::memcpy (&value, data, sizeof (value));
            if (std::isfinite (value)) getParameters()[0]->setValue (value);
        }
    }
private:
    juce::PluginDescription desc;
    std::shared_ptr<Probe> probe;
    std::shared_ptr<InstanceLifetime> lifetime;
};
class AsyncFormat final : public juce::AudioPluginFormat
{
public:
    explicit AsyncFormat (std::shared_ptr<Probe> p) : probe (std::move (p)) {}
    juce::String getName() const override { return formatName; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override {}
    bool fileMightContainThisPluginType (const juce::String& id) override { return id.startsWith ("halftrack-test:"); }
    juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return id; }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription& d) override { return fileMightContainThisPluginType (d.fileOrIdentifier); }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return true; }
private:
    void createPluginInstance (const juce::PluginDescription& d, double, int, PluginCreationCallback callback) override
    { probe->pending.push_back ({ d, std::move (callback) }); }
    std::shared_ptr<Probe> probe;
};
te::Engine& engineFor (Session& session)
{
    for (auto* engine : te::Engine::getEngines())
        if (&engine->getDeviceManager().deviceManager == &session.audioDeviceManager()) return *engine;
    throw std::runtime_error ("Test session engine not found");
}
te::ExternalPlugin* externalFor (Session& session)
{
    auto& edits = engineFor (session).getActiveEdits();
    for (auto* edit : edits.getEdits())
        for (auto* track : te::getAudioTracks (*edit))
            for (auto* plugin : track->pluginList.getPlugins())
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin)) return external;
    return nullptr;
}
void registerFormat (Session& session, const std::shared_ptr<Probe>& probe)
{
    session.pluginFormats().addFormat (std::make_unique<AsyncFormat> (probe));
    check (session.audioDeviceManager().getCurrentAudioDevice() == nullptr, "fixture opens no physical audio device");
}
void addPending (Session& session, const std::shared_ptr<Probe>& probe, bool generic = false)
{
    ok (session.addPlugin (0, description (generic)), "insert asynchronous fixture");
    check (pumpUntil ([&] { return ! probe->pending.empty(); }), "creation reaches controlled asynchronous callback");
    auto* plugin = externalFor (session);
    check (plugin != nullptr && plugin->isInitialisingAsync() && plugin->getAudioPluginInstance() == nullptr,
           "fixture is genuinely awaiting its processor");
}
void complete (const std::shared_ptr<Probe>& probe, bool success = true)
{
    check (! probe->pending.empty(), "completion has a pending request");
    auto pending = std::move (probe->pending.front()); probe->pending.pop_front();
    pending.callback (success ? std::make_unique<TestInstance> (pending.description, probe) : nullptr,
                      success ? juce::String() : juce::String (failureMessage));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
}
// The message thread does not dispatch while the worker delivers its callback.
// Waiting for callback return establishes that completion was queued before an
// undo or destruction; the deadline bounds a broken blocking implementation.
void completeFromWorker (const std::shared_ptr<Probe>& probe, bool success = true)
{
    check (! probe->pending.empty(), "worker completion has a pending request");
    auto pending = std::move (probe->pending.front()); probe->pending.pop_front();
    juce::WaitableEvent returned (true);
    std::exception_ptr failure;
    std::thread worker ([&, pending = std::move (pending)]() mutable
    {
        try
        {
            pending.callback (success ? std::make_unique<TestInstance> (pending.description, probe) : nullptr,
                              success ? juce::String() : juce::String (failureMessage));
        }
        catch (...) { failure = std::current_exception(); }
        returned.signal();
    });
    const bool nonblocking = returned.wait (2000);
    if (! nonblocking)
    {
        // Drain a broken blocking callback before reporting it. A worker must
        // never outlive the test's Session or turn CI into an unbounded join.
        if (! pumpUntil ([&] { return returned.wait (0); }))
        {
            std::cerr << "FAIL editor worker completion exceeded its deadline" << std::endl;
            std::_Exit (2);
        }
    }
    worker.join();
    if (failure) std::rethrow_exception (failure);
    check (probe->offThreadCompletions == 0, "native initialization runs on the message thread after worker delivery");
    check (nonblocking, "worker delivery returns without waiting for message dispatch");
}
void finishRestored (Session& session, const std::shared_ptr<Probe>& probe)
{
    check (pumpUntil ([&]
    {
        if (! probe->pending.empty()) complete (probe);
        auto* plugin = externalFor (session);
        return plugin != nullptr && plugin->getAudioPluginInstance() != nullptr;
    }), "restored plug-in becomes ready");
}
juce::DocumentWindow* visibleEditor (Session& session, bool generic = false)
{
    ok (session.showPluginEditor (0, 0), "open ready processor editor");
    auto* plugin = externalFor (session);
    auto* window = plugin != nullptr ? dynamic_cast<juce::DocumentWindow*> (plugin->windowState->pluginWindow.get()) : nullptr;
    check (window != nullptr && window->isVisible() && window->getContentComponent() != nullptr,
           "visible editor contains an actual component");
    auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (window->getContentComponent());
    check (editor != nullptr && editor->getAudioProcessor() == plugin->getAudioPluginInstance(),
           "editor content belongs to the ready processor");
    check (editor->getWidth() > 0 && editor->getHeight() > 0, "editor content has usable bounds");
    if (generic) check (dynamic_cast<juce::GenericAudioProcessorEditor*> (editor) != nullptr, "processor without custom UI receives generic parameter editor");
    else
    {
        auto* custom = dynamic_cast<TestEditor*> (editor);
        check (custom != nullptr, "custom processor editor is actually hosted");
        custom->gain.setValue (0.27, juce::sendNotificationSync);
        check (std::abs (plugin->getAudioPluginInstance()->getParameters()[0]->getValue() - 0.27f) < 0.00001f,
               "hosted editor changes its processor parameter");
    }
    return window;
}
void loadingAndSuccess (const std::shared_ptr<Probe>& probe)
{
    Session session (false); registerFormat (session, probe); addPending (session, probe);
    auto* plugin = externalFor (session);
    const auto result = session.showPluginEditor (0, 0);
    check (result.failed() && result.getErrorMessage().containsIgnoreCase ("loading"), "loading editor request fails with actionable state");
    check (! plugin->windowState->pluginWindow, "loading request does not create a blank window");
    auto direct = engineFor (session).getUIBehaviour().createPluginWindow (*plugin->windowState);
    check (! direct, "native window factory also rejects an absent processor");
    complete (probe); check (! plugin->isInitialisingAsync(), "successful completion clears loading");
    auto* window = visibleEditor (session);
    ok (session.showPluginEditor (0, 0), "repeat editor request");
    check (plugin->windowState->pluginWindow.get() == window && probe->editors == 1, "repeat request reuses one editor");
    session.closePluginEditors();
    check (probe->editors == 0 && plugin->getAudioPluginInstance()->getActiveEditor() == nullptr,
           "closing an editor unregisters it before destruction");
    window = visibleEditor (session);
    check (session.pluginEditorsShown(), "plug-in windows show by default");
    session.setPluginEditorsShown (false);
    check (! session.pluginEditorsShown() && ! window->isVisible() && plugin->windowState->pluginWindow.get() == window, "Plug-In Windows off hides the open window, keeping it");
    session.setPluginEditorsShown (true);
    check (session.pluginEditorsShown() && window->isVisible(), "and on shows it again");
    session.setPluginEditorsShown (false);
    ok (session.showPluginEditor (0, 0), "open the editor while the windows are hidden");
    check (session.pluginEditorsShown() && window->isVisible(), "opening an editor brings the plug-in windows back");
    juce::Component::SafePointer<juce::Component> editor (window->getContentComponent());
    ok (session.removePlugin (0, 0), "remove ready plug-in");
    check (editor == nullptr && probe->editors == 0, "removal destroys editor before processor retirement");
    session.undo(); finishRestored (session, probe); visibleEditor (session);
    session.redo(); check (probe->editors == 0 && externalFor (session) == nullptr, "redo removal closes restored editor");
}
void failedCreation (const std::shared_ptr<Probe>& probe)
{
    Session session (false); registerFormat (session, probe); addPending (session, probe);
    complete (probe, false); auto* plugin = externalFor (session);
    check (plugin != nullptr && ! plugin->isInitialisingAsync(), "failed asynchronous creation clears loading state");
    check (plugin->getLoadError() == failureMessage, "failure exposes the actual initialization error");
    const auto result = session.showPluginEditor (0, 0);
    check (result.failed() && result.getErrorMessage().contains (failureMessage), "failed processor editor request returns its load error");
    check (! plugin->windowState->pluginWindow, "failed processor creates no blank editor");
    auto direct = engineFor (session).getUIBehaviour().createPluginWindow (*plugin->windowState);
    check (! direct && probe->editorCreations == 0, "factory cannot reopen a failed processor");
}
void genericAndTrackLifecycle (const std::shared_ptr<Probe>& probe)
{
    Session session (false); registerFormat (session, probe); addPending (session, probe, true); complete (probe);
    auto* window = visibleEditor (session, true);
    juce::Component::SafePointer<juce::Component> content (window->getContentComponent());
    ok (session.removeTrack (0), "delete track with generic editor");
    check (content == nullptr, "track deletion closes generic editor");
    session.undo(); finishRestored (session, probe);
    window = visibleEditor (session, true); content = window->getContentComponent();
    ok (session.newProject(), "replace project with ready generic editor");
    check (content == nullptr && externalFor (session) == nullptr, "project replacement closes generic editor and removes processor");
}
void interruptedCreation (const std::shared_ptr<Probe>& probe, int operation, const juce::File& root)
{
    Session session (false); registerFormat (session, probe);
    const auto emptyProject = root.getChildFile ("Empty.halftrack");
    if (operation == 3) ok (session.saveProject (emptyProject), "save empty replacement project");
    addPending (session, probe);
    if (operation == 0) ok (session.removePlugin (0, 0), "remove before asynchronous completion");
    if (operation == 1) ok (session.removeTrack (0), "delete track before asynchronous completion");
    if (operation == 2) { session.undo(); check (session.canRedo(), "undo insertion creates redo history"); }
    if (operation == 3) ok (session.loadProject (emptyProject), "load replacement before asynchronous completion");
    check (externalFor (session) == nullptr, "mutation removes pending plug-in from active tracks");
    complete (probe);
    if (operation == 2) check (session.canRedo(), "detached completion preserves redo history");
    check (externalFor (session) == nullptr && probe->editorCreations == 0, "late completion cannot resurrect a removed editor or track");
    if (operation < 3)
    {
        if (operation == 2) session.redo(); else session.undo();
        finishRestored (session, probe); visibleEditor (session);
        ok (session.newProject(), "replace project after restored editor");
        check (probe->editors == 0, "restored editor closes during project replacement");
    }
}
void attachedCompletionPreservesRedo (const std::shared_ptr<Probe>& probe)
{
    Session session (false); registerFormat (session, probe); addPending (session, probe);
    const auto originalName = session.snapshot().tracks.front().name;
    ok (session.renameTrack (0, "Renamed during loading"), "rename track during asynchronous creation");
    session.undo();
    check (session.canRedo() && session.snapshot().tracks.front().name == originalName,
           "unrelated rename is available to redo before completion");
    complete (probe);
    check (externalFor (session) != nullptr && externalFor (session)->getAudioPluginInstance() != nullptr,
           "attached asynchronous processor becomes ready");
    check (session.canRedo(), "attached completion preserves unrelated redo history");
    session.redo();
    check (session.snapshot().tracks.front().name == "Renamed during loading",
           "unrelated rename can still be redone after completion");
}
void workerCompletion (const std::shared_ptr<Probe>& probe, bool success)
{
    Session session (false); registerFormat (session, probe); addPending (session, probe);
    completeFromWorker (probe, success);
    auto* plugin = externalFor (session);
    check (plugin != nullptr && plugin->isInitialisingAsync() && plugin->getAudioPluginInstance() == nullptr,
           "worker delivery leaves model changes queued until message dispatch");
    check (probe->nativeCompletions == 0, "native initialization has not run before queued dispatch");
    check (pumpUntil ([&] { return ! plugin->isInitialisingAsync(); }), "message dispatch completes the worker result");
    if (success)
    {
        check (probe->nativeCompletions == 1 && probe->offThreadCompletions == 0,
               "worker-created processor initializes exactly once on the message thread");
        visibleEditor (session);
    }
    else
    {
        check (plugin->getAudioPluginInstance() == nullptr && plugin->getLoadError() == failureMessage,
               "worker-reported failure reaches the model without a processor");
        check (session.showPluginEditor (0, 0).failed(), "worker failure creates no editor");
    }
}
void queuedWorkerCompletion (const std::shared_ptr<Probe>& probe, bool destroySession)
{
    auto session = std::make_unique<Session> (false);
    registerFormat (*session, probe); addPending (*session, probe);
    completeFromWorker (probe);
    check (probe->nativeCompletions == 0 && probe->instances == 1,
           "worker-created processor is waiting in the message queue");
    if (destroySession)
        session.reset();
    else
    {
        session->undo();
        check (session->canRedo() && externalFor (*session) == nullptr,
               "undo detaches the plug-in before queued completion");
    }
    check (pumpUntil ([&] { return probe->instances == 0; }),
           "queued result is disposed after its target was removed");
    check (probe->nativeCompletions == 0 && probe->offThreadDisposals == 0 && probe->editorCreations == 0,
           "discarded queued result never initializes and is destroyed on the message thread");
    if (session)
    {
        check (session->canRedo(), "queued completion preserves undo's redo history");
        session->redo(); finishRestored (*session, probe); visibleEditor (*session);
        check (probe->nativeCompletions == 1 && probe->offThreadCompletions == 0,
               "redo initializes one replacement after discarding the queued result");
    }
}
void destroyedSession (const std::shared_ptr<Probe>& probe)
{
    { Session session (false); registerFormat (session, probe); addPending (session, probe); }
    complete (probe);
    check (probe->instances == 0 && probe->editorCreations == 0, "completion after session destruction disposes processor without editor");
}
}
int runPluginEditorTests (const juce::File& requestedDirectory)
{
    int failures = 0;
    const auto root = requestedDirectory.getChildFile ("editors-" + juce::Uuid().toString());
    if (root.createDirectory().failed()) { std::cerr << "FAIL editor cannot create test directory" << std::endl; return 1; }
    auto run = [&] (const char* name, const std::function<void (const std::shared_ptr<Probe>&)>& test)
    {
        auto probe = std::make_shared<Probe>();
        try { test (probe); }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL editor " << name << ": " << error.what() << std::endl; }
        // Drain the dependency's deferred instance destruction after the Session dies.
        // Any abandoned fixture callback retains only a weak reference to that Session.
        while (! probe->pending.empty()) complete (probe, false);
        if (! pumpUntil ([&] { return probe->instances == 0 && probe->editors == 0; }) || probe->lifetimeViolations != 0)
        { ++failures; std::cerr << "FAIL editor " << name << ": editor/processor lifetime imbalance" << std::endl; }
    };
    run ("loading/success", loadingAndSuccess);
    run ("failure", failedCreation);
    run ("generic/track", genericAndTrackLifecycle);
    for (int operation = 0; operation < 4; ++operation)
        run ("interrupted creation", [&, operation] (const auto& probe) { interruptedCreation (probe, operation, root); });
    run ("session destruction", destroyedSession);
    run ("attached redo", attachedCompletionPreservesRedo);
    run ("worker success", [] (const auto& probe) { workerCompletion (probe, true); });
    run ("worker failure", [] (const auto& probe) { workerCompletion (probe, false); });
    run ("queued worker undo", [] (const auto& probe) { queuedWorkerCompletion (probe, false); });
    run ("queued worker destruction", [] (const auto& probe) { queuedWorkerCompletion (probe, true); });
    if (failures == 0) std::cout << "Plug-in editor acceptance tests passed." << std::endl;
    return failures == 0 ? 0 : 1;
}
}
