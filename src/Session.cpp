#include "Session.h"
#include "SubtractiveSynth.h"
#include "UtilityProcessor.h"
#include "UtilityEditor.h"
#include "NativeEffects.h"
#include <tracktion_engine/tracktion_engine.h>
#include <map>
#include <set>
#include <cstdlib>

namespace halftrack
{
namespace te = tracktion;
namespace
{
using juce::File;
using juce::Result;
using juce::String;
const juce::Identifier monitorProperty { "halftrackMonitor" };
const juce::Identifier midiTrackProperty { "halftrackMidiTrack" };
const juce::Identifier midiNoteIDProperty { "halftrackNoteId" };
const juce::Identifier returnProperty { "halftrackReturn" };
const juce::Identifier returnBusProperty { "halftrackReturnBus" };
const juce::Identifier returnModeProperty { "halftrackReturnSendMode" };
const juce::Identifier sendReturnProperty { "halftrackSendReturnID" };

bool isReturn (const te::AudioTrack& track) { return track.state.getProperty (returnProperty, false); }
Session::SendMode sendMode (const te::AudioTrack& track)
{ return static_cast<Session::SendMode> (static_cast<int> (track.state.getProperty (returnModeProperty, 1))); }

te::AuxSendPlugin* findSend (te::AudioTrack& source, const String& destination)
{
    for (auto* plugin : source.pluginList.getPlugins())
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin))
            if (send->state.getProperty (sendReturnProperty).toString() == destination) return send;
    return nullptr;
}

// Validate routing before constructing native objects, so invalid projects cannot
// replace the active edit, load hidden media, or change its undo history.
Result validateRouting (const juce::ValueTree& state)
{
    struct Destination { int bus; Session::SendMode mode; };
    std::map<String, Destination> destinations;
    std::set<int> buses;
    std::set<String> returnInputTargets;
    std::vector<juce::ValueTree> tracks, routePlugins;
    auto number = [] (const juce::var& value, double low, double high, bool integral = false)
    {
        const auto text = value.toString().trim().toStdString();
        char* end = nullptr;
        const auto parsed = std::strtod (text.c_str(), &end);
        return ! text.empty() && end == text.c_str() + text.size() && std::isfinite (parsed)
            && parsed >= low && parsed <= high && (! integral || std::floor (parsed) == parsed);
    };
    const auto canonicalID = [] (const juce::var& value)
    {
        const auto id = te::EditItemID::fromVar (value);
        return id.isValid() && id.toString() == value.toString();
    };
    const auto collect = [&] (auto&& self, const juce::ValueTree& node) -> void
    {
        if (node.hasType (te::IDs::TRACK)) tracks.push_back (node);
        if (node.hasType (te::IDs::PLUGIN)
            && (node[te::IDs::type].toString() == te::AuxSendPlugin::xmlTypeName
                || node[te::IDs::type].toString() == te::AuxReturnPlugin::xmlTypeName)) routePlugins.push_back (node);
        for (const auto& child : node) self (self, child);
    };
    collect (collect, state);
    for (const auto& track : tracks)
    {
        if (! static_cast<bool> (track.getProperty (returnProperty, false)))
        {
            if (track.hasProperty (returnBusProperty) || track.hasProperty (returnModeProperty))
                return Result::fail ("Return metadata requires a return track.");
            continue;
        }
        if (! number (track[returnBusProperty], 0, 2147483646, true)
            || ! number (track[returnModeProperty], 0, 1, true)
            || static_cast<bool> (track.getProperty (midiTrackProperty, false)))
            return Result::fail ("The project has invalid return track settings.");
        const auto id = track[te::IDs::id].toString();
        const auto bus = static_cast<int> (track[returnBusProperty]);
        if (! canonicalID (track[te::IDs::id]) || destinations.contains (id) || ! buses.insert (bus).second)
            return Result::fail ("Return tracks require unique identities and buses.");
        destinations.emplace (id, Destination { bus, static_cast<Session::SendMode> (static_cast<int> (track[returnModeProperty])) });
        returnInputTargets.insert (id);
        const auto checkContents = [&] (auto&& self, const juce::ValueTree& node) -> bool
        {
            if (te::Clip::isClipState (node)) return false;
            if (node.hasType (te::IDs::CLIPSLOT))
            {
                if (! canonicalID (node[te::IDs::id])) return false;
                returnInputTargets.insert (node[te::IDs::id].toString());
            }
            for (const auto& child : node) if (! self (self, child)) return false;
            return true;
        };
        if (! checkContents (checkContents, track)) return Result::fail ("Return tracks require empty clip slots with valid identities.");
    }
    std::vector<juce::ValueTree> validated;
    for (const auto& track : tracks)
    {
        const auto destination = destinations.find (track[te::IDs::id].toString());
        const bool returned = destination != destinations.end();
        std::vector<juce::ValueTree> plugins;
        int mixer = -1, mixers = 0, returns = 0;
        bool routed = returned;
        for (const auto& child : track)
            if (child.hasType (te::IDs::PLUGIN))
            {
                const auto type = child[te::IDs::type].toString();
                if (type == "volume" || type == "halftrack-mixer") { mixer = static_cast<int> (plugins.size()); ++mixers; }
                if (type == te::AuxSendPlugin::xmlTypeName) routed = true;
                plugins.push_back (child);
            }
        if (routed && (mixers != 1 || ! canonicalID (track[te::IDs::id])))
            return Result::fail ("Routed tracks require a valid identity and exactly one track mixer.");
        if (routed)
            for (const auto& device : track.getChildWithName (te::IDs::OUTPUTDEVICES))
                if (device[te::IDs::name].toString().startsWithIgnoreCase ("track "))
                    return Result::fail ("Auxiliary-routed tracks cannot route their main output to another track.");
        std::set<String> targets;
        bool pastSourceEffects = false;
        for (int index = 0; index < static_cast<int> (plugins.size()); ++index)
        {
            const auto& plugin = plugins[static_cast<size_t> (index)];
            const auto type = plugin[te::IDs::type].toString();
            if (type == te::AuxReturnPlugin::xmlTypeName)
            {
                if (! returned || ++returns != 1 || index != 0 || ! canonicalID (plugin[te::IDs::id])
                    || ! number (plugin[te::IDs::busNum], 0, 2147483646, true)
                    || static_cast<int> (plugin[te::IDs::busNum]) != destination->second.bus
                    || ! static_cast<bool> (plugin.getProperty (te::IDs::enabled, true)))
                    return Result::fail ("The project has an invalid return input.");
                validated.push_back (plugin);
            }
            else if (type == te::AuxSendPlugin::xmlTypeName)
            {
                const auto target = plugin[sendReturnProperty].toString();
                const auto found = destinations.find (target);
                if (returned || found == destinations.end() || ! targets.insert (target).second
                    || ! canonicalID (plugin[te::IDs::id])
                    || ! number (plugin[te::IDs::busNum], 0, 2147483646, true)
                    || static_cast<int> (plugin[te::IDs::busNum]) != found->second.bus
                    || ! number (plugin[te::IDs::auxSendSliderPos], 0, te::decibelsToVolumeFaderPosition (0.0f))
                    || static_cast<bool> (plugin.getProperty (te::IDs::phase, false))
                    || ((index < mixer) != (found->second.mode == Session::SendMode::pre)))
                    return Result::fail ("The project has an invalid send route or tap position.");
                pastSourceEffects = true;
                validated.push_back (plugin);
            }
            else if (index == mixer) pastSourceEffects = true;
            else if (routed && pastSourceEffects && type != "level")
                return Result::fail ("Source and return effects must precede the mixer and send taps.");
        }
        if (returned && returns != 1) return Result::fail ("The return track is missing its input.");
    }
    if (validated.size() != routePlugins.size()) return Result::fail ("Auxiliary routing is only supported directly on audio tracks.");
    const auto checkInputs = [&] (auto&& self, const juce::ValueTree& node) -> bool
    {
        if (node.hasType (te::IDs::INPUTDEVICEDESTINATION) && returnInputTargets.contains (te::EditItemID::fromVar (node[te::IDs::targetID]).toString())) return false;
        if (node.hasType (te::IDs::INPUTDEVICE) && destinations.contains (te::EditItemID::fromVar (node[te::IDs::sourceTrack]).toString())) return false;
        for (const auto& child : node) if (! self (self, child)) return false;
        return true;
    };
    if (! checkInputs (checkInputs, state)) return Result::fail ("Return tracks cannot receive recording inputs.");
    return Result::ok();
}

// Keep the engine's automation, smoothing and gain range, but normalize its
// sine/cosine law to unity at center. A distinct type preserves older projects'
// serialized linear mixers rather than silently changing their sound.
class TrackMixer final : public te::VolumeAndPanPlugin
{
public:
    static constexpr const char* xmlTypeName = "halftrack-mixer";
    explicit TrackMixer (te::PluginCreationInfo info) : VolumeAndPanPlugin (info)
    {
        setPanLaw (te::PanLaw3dBCenter);
    }
    juce::String getPluginType() override { return xmlTypeName; }
    void applyToBuffer (const te::PluginRenderContext& context) override
    {
        VolumeAndPanPlugin::applyToBuffer (context);
        if (isEnabled() && context.destBuffer != nullptr)
            for (int channel = 0; channel < juce::jmin (2, context.destBuffer->getNumChannels()); ++channel)
                context.destBuffer->applyGain (channel, context.bufferStartSample,
                                               context.bufferNumSamples, 1.4142135623730951f);
    }
};

Result useTrackMixer (te::AudioTrack& track)
{
    auto* previous = track.getVolumePlugin();
    if (previous == nullptr) return Result::fail ("The track has no mixer.");
    const int index = track.pluginList.indexOf (previous);
    auto mixer = track.edit.getPluginCache().createNewPlugin (TrackMixer::xmlTypeName, {});
    if (mixer == nullptr) return Result::fail ("Could not create the track mixer.");
    previous->deleteFromParent();
    track.pluginList.insertPlugin (mixer, index, nullptr);
    return Result::ok();
}

// Arrangement and launcher clips share source ownership, but not playback placement.
juce::Array<te::Clip*> allTrackClips (te::AudioTrack& track)
{
    auto result = track.getClips();
    for (auto* slot : track.getClipSlotList().getClipSlots())
        if (auto* clip = slot->getClip()) result.add (clip);
    return result;
}

Session::AudioClipSettings audioSettings (const te::WaveAudioClip& clip)
{
    return { clip.getGainDB(), clip.getFadeIn().inSeconds(), clip.getFadeOut().inSeconds(),
        static_cast<Session::FadeCurve> (clip.getFadeInType()), static_cast<Session::FadeCurve> (clip.getFadeOutType()) };
}
bool validAudioSettings (const Session::AudioClipSettings& s, double length)
{
    const auto curve = [] (auto v) { return static_cast<int> (v) >= 1 && static_cast<int> (v) <= 4; };
    return std::isfinite (s.gainDb) && s.gainDb >= -100 && s.gainDb <= 24
        && std::isfinite (s.fadeInSeconds) && std::isfinite (s.fadeOutSeconds)
        && s.fadeInSeconds >= 0 && s.fadeOutSeconds >= 0
        && s.fadeInSeconds + s.fadeOutSeconds <= length + 1e-10
        && curve (s.fadeInCurve) && curve (s.fadeOutCurve);
}
bool canEditAudioRange (const te::WaveAudioClip& clip)
{
    return clip.getClipSlot() == nullptr && ! clip.isLooping() && ! clip.getAutoTempo() && ! clip.getWarpTime()
        && ! clip.getIsReversed() && clip.getSpeedRatio() == 1 && ! clip.isGrouped();
}
Result validateAudioClips (const juce::ValueTree& state)
{
    if (state.hasType (te::IDs::AUDIOCLIP))
    {
        const auto parse = [&state] (const juce::Identifier& key, double fallback, double& result)
        {
            if (! state.hasProperty (key)) { result = fallback; return true; }
            const auto value = state[key].toString().trim().toStdString(); char* end = nullptr;
            result = std::strtod (value.c_str(), &end);
            return ! value.empty() && end == value.c_str()+value.size() && std::isfinite (result);
        };
        double gain = 0, in = 0, out = 0, length = 0, inType = 1, outType = 1;
        if (! parse (te::IDs::gain, 0, gain) || ! parse (te::IDs::fadeIn, 0, in)
            || ! parse (te::IDs::fadeOut, 0, out) || ! parse (te::IDs::length, 0, length)
            || ! parse (te::IDs::fadeInType, 1, inType) || ! parse (te::IDs::fadeOutType, 1, outType)
            || gain < -100 || gain > 24 || length < 0 || inType < 1 || inType > 4 || outType < 1 || outType > 4
            || std::floor (inType) != inType || std::floor (outType) != outType
            || ! validAudioSettings ({ static_cast<float> (gain), in, out,
                static_cast<Session::FadeCurve> (static_cast<int> (inType)),
                static_cast<Session::FadeCurve> (static_cast<int> (outType)) }, length))
            return Result::fail ("The project contains invalid audio clip gain or fades.");
    }
    for (const auto& child : state) if (auto r = validateAudioClips (child); r.failed()) return r;
    return Result::ok();
}

Session::ClipInfo describeClip (te::Clip& clip)
{
    Session::ClipInfo result;
    const auto position = clip.getPosition();
    result.id = clip.itemID.toString(); result.name = clip.getName();
    result.start = position.time.getStart().inSeconds(); result.length = position.time.getLength().inSeconds();
    result.sourceOffset = position.offset.inSeconds();
    if (clip.usesSourceFile()) result.file = clip.getSourceFileReference().getFile();
    if (auto* audio = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        result.isAudio = true; result.audio = audioSettings (*audio);
        result.sourceLength = audio->getSourceLength().inSeconds(); result.canEditAudioRange = canEditAudioRange (*audio);
        result.canEditAudioFades = audio->getClipSlot() == nullptr;
    }
    if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
    {
        result.isMidi = true;
        const auto beats = clip.edit.tempoSequence.toBeats (position.time);
        result.startBeat = beats.getStart().inBeats(); result.lengthBeats = beats.getLength().inBeats();
        for (auto* note : midi->getSequence().getNotes())
            result.notes.push_back ({ note->state.getProperty (midiNoteIDProperty).toString(), note->getNoteNumber(),
                                     note->getStartBeat().inBeats(), note->getLengthBeats().inBeats(), note->getVelocity() / 127.0f });
    }
    return result;
}

class Behaviour final : public te::EngineBehaviour
{
public:
    explicit Behaviour (bool devices) : useDevices (devices) {}
    bool autoInitialiseDeviceManager() override { return useDevices; }
    bool shouldOpenAudioInputByDefault() override { return false; }
    bool addSystemAudioIODeviceTypes() override { return useDevices; }
    bool canScanPluginsOutOfProcess() override { return useDevices; }
    // Pre sends and soloed returns still need content from a dry-muted source.
    bool shouldProcessMutedTracks() override { return true; }
    File getFileForNewAudioRecording (te::Track& track, const String& extension) override
    {
        auto base = te::EditFileOperations (track.edit).getEditFile();
        auto dir = base.getSiblingFile (base.getFileNameWithoutExtension() + " Assets");
        dir.createDirectory();
        return dir.getNonexistentChildFile (File::createLegalFileName (track.getName()) + " Take", extension, true);
    }
private:
    bool useDevices;
};

class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (te::Plugin& p, juce::AudioPluginInstance& processor)
        : DocumentWindow (p.getName(), juce::Colour (0xff262724), closeButton), plugin (p)
    {
        auto* editor = processor.createEditorAndMakeActive();
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (processor);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setUsingNativeTitleBar (true);
        centreWithSize (juce::jmax (getWidth(), 320), juce::jmax (getHeight(), 200));
        setVisible (true);
    }
    PluginWindow (te::Plugin& p, std::unique_ptr<juce::Component> editor)
        : DocumentWindow (p.getName(), juce::Colour (0xff262724), closeButton), plugin (p)
    {
        setContentOwned (editor.release(), true);
        setResizable (false, false); setUsingNativeTitleBar (true);
        centreWithSize (getWidth(), getHeight()); setVisible (true);
    }
    ~PluginWindow() override
    {
        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
            editor->getAudioProcessor()->editorBeingDeleted (editor);
        clearContentComponent();
    }
    void closeButtonPressed() override { plugin.windowState->closeWindowExplicitly(); }
private:
    te::Plugin& plugin;
};

class UIBehaviour final : public te::UIBehaviour
{
public:
    explicit UIBehaviour (Session& s, bool showProgress) : owner (s), showProgressWindow (showProgress) {}
    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& state) override
    {
        if (auto* p = dynamic_cast<te::Plugin::WindowState*> (&state))
            if (dynamic_cast<UtilityPlugin*> (&p->plugin))
                return std::make_unique<PluginWindow> (p->plugin, makeUtilityEditor (owner, p->plugin.itemID.toString()));
        if (auto* p = dynamic_cast<te::Plugin::WindowState*> (&state))
            if (dynamic_cast<NativeEffectPlugin*> (&p->plugin))
                return std::make_unique<PluginWindow> (p->plugin, makeNativeEffectEditor (owner, p->plugin.itemID.toString()));
        if (auto* p = dynamic_cast<te::Plugin::WindowState*> (&state))
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (&p->plugin))
                if (! external->isInitialisingAsync())
                    if (auto* processor = external->getAudioPluginInstance())
                        return std::make_unique<PluginWindow> (p->plugin, *processor);
        return {};
    }
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
    {
        // The engine builds and releases the render graph on the message thread.
        // Only runJob belongs on the worker, as in Tracktion's TaskRunner example.
        struct TaskRunner final : juce::Thread
        {
            explicit TaskRunner (te::ThreadPoolJobWithProgress& t) : Thread (t.getJobName()), job (t) {}
            ~TaskRunner() override
            {
                job.signalJobShouldExit();
                signalThreadShouldExit();
                // Never force-kill a thread while it is inside a plug-in.
                waitForThreadToExit (-1);
            }
            void run() override
            {
                while (! threadShouldExit() && ! job.shouldExit())
                    if (job.runJob() == juce::ThreadPoolJob::jobHasFinished) break;
            }
            te::ThreadPoolJobWithProgress& job;
        } runner (task);

        activeTask = &task;
        cancelled = false;
        double progress = 0;
        std::unique_ptr<juce::AlertWindow> window;
        if (showProgressWindow)
        {
            window = std::make_unique<juce::AlertWindow> (task.getJobName(), "Rendering audio...", juce::MessageBoxIconType::NoIcon);
            window->setEscapeKeyCancels (false);
            window->addProgressBarComponent (progress);
            window->addButton ("Cancel", 0);
            window->getButton (0)->onClick = [this, alert = window.get()]
            {
                cancelTask();
                alert->getButton (0)->setEnabled (false);
                alert->setMessage ("Cancelling export...");
            };
            window->enterModalState();
        }
        if (! runner.startThread()) cancelTask();
        while (runner.isThreadRunning())
        {
            progress = juce::jlimit (0.0, 1.0, static_cast<double> (task.getCurrentTaskProgress()));
            if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (10))
            {
                cancelTask();
                juce::Thread::sleep (2);
            }
            // Closing a native alert is also cancellation; keep the worker alive
            // until the engine observes its exit flag and cleans up its writer.
            if (window && ! window->isCurrentlyModal (false)) cancelTask();
        }
        if (window) { window->exitModalState (0); window->setVisible (false); }
        activeTask = nullptr;
    }
    void cancelTask() { if (activeTask) { cancelled = true; activeTask->signalJobShouldExit(); } }
    bool cancelled = false;
private:
    Session& owner;
    bool showProgressWindow;
    te::ThreadPoolJobWithProgress* activeTask = nullptr;
};

struct Meter
{
    explicit Meter (te::LevelMeasurer& m) : measurer (&m) { m.addClient (client); }
    ~Meter() { if (measurer != nullptr) measurer->removeClient (client); }
    juce::WeakReference<te::LevelMeasurer> measurer;
    te::LevelMeasurer::Client client;
    float read (int channel) { return juce::Decibels::decibelsToGain (client.getAndClearAudioLevel (channel).dB, -90.0f); }
};

juce::ValueTree findStateWithID (juce::ValueTree tree, const juce::var& id)
{
    if (tree.getProperty (te::IDs::id) == id)
        return tree;
    for (auto child : tree)
        if (auto found = findStateWithID (child, id); found.isValid())
            return found;
    return {};
}
}

struct Session::Impl
{
    explicit Impl (Session& s, bool devices, std::unique_ptr<juce::AudioIODeviceType> customDeviceType)
        : owner (s), engine (devices ? "Halftrack" : "Halftrack Tests", std::make_unique<UIBehaviour> (s, devices),
                            std::make_unique<Behaviour> (devices)), useDevices (devices)
    {
        engine.getPluginManager().createBuiltInType<TrackMixer>();
        engine.getPluginManager().createBuiltInType<SubtractiveSynthPlugin>();
        engine.getPluginManager().createBuiltInType<UtilityPlugin>();
        engine.getPluginManager().createBuiltInType<NativeEffectPlugin>();
        engine.getPluginManager().setUsesSeparateProcessForScanning (devices);
        if (customDeviceType)
        {
            auto& dm = engine.getDeviceManager();
            dm.deviceManager.addAudioDeviceType (std::move (customDeviceType));
            dm.initialise (2, 2);
            useDevices = true;
        }
        scratch = File::getSpecialLocation (File::tempDirectory).getChildFile ("Halftrack-" + juce::Uuid().toString());
        scratch.createDirectory();
        // JUCE treats a nonexistent base path as a directory when making relative paths.
        // Materialise the scratch edit location before Tracktion stores source references.
        scratch.getChildFile ("Untitled.halftrack").create();
    }
    ~Impl()
    {
        gesture.reset();
        meters.clear();
        edit.reset();
        // Unsaved sessions are deliberately retained in the temp directory for recovery.
    }
    te::AudioTrack* track (int index) const
    {
        auto tracks = te::getAudioTracks (*edit);
        return juce::isPositiveAndBelow (index, tracks.size()) ? tracks[index] : nullptr;
    }
    te::AudioTrack* track (const String& id) const
    {
        for (auto* t : te::getAudioTracks (*edit)) if (t->itemID.toString() == id) return t;
        return nullptr;
    }
    te::WaveAudioClip* audioClip (const String& id) const
    {
        te::WaveAudioClip* result = nullptr;
        for (auto* t : te::getAudioTracks (*edit)) for (auto* c : allTrackClips (*t))
            if (c->itemID.toString() == id)
            {
                if (result) return nullptr;
                result = dynamic_cast<te::WaveAudioClip*> (c);
            }
        return result;
    }
    te::MidiClip* midiClip (const String& id) const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* c : allTrackClips (*t)) if (c->itemID.toString() == id) return dynamic_cast<te::MidiClip*> (c);
        return nullptr;
    }
    te::ClipSlot* slot (int trackIndex, int sceneIndex) const
    {
        if (! juce::isPositiveAndBelow (sceneIndex, edit->getSceneList().getNumScenes())) return nullptr;
        if (auto* t = track (trackIndex); t && ! isReturn (*t)) return t->getClipSlotList().getClipSlots()[sceneIndex];
        return nullptr;
    }
    void cancelLaunches()
    {
        for (auto* t : te::getAudioTracks (*edit))
        {
            for (auto* slot : t->getClipSlotList().getClipSlots())
                if (auto* c = slot->getClip()) if (auto h = c->getLaunchHandle()) h->stop (std::nullopt);
            t->launcherSilence.stop (std::nullopt);
            // Cancel stale claims without releasing already acquired ownership.
            t->launcherOwnership.fetch_add (2, std::memory_order_acq_rel);
        }
    }
    std::optional<te::MonotonicBeat> launchBoundary() const
    {
        auto& transport = edit->getTransport();
        if (! transport.isPlaying() || edit->getLaunchQuantisation().type == te::LaunchQType::none) return {};
        if (auto* context = transport.getCurrentPlaybackContext())
            if (auto sync = context->getSyncPoint())
            {
                auto& quantisation = edit->getLaunchQuantisation();
                auto next = quantisation.getNext (sync->beat);
                auto distance = next - sync->beat;
                if (transport.looping)
                {
                    const auto loop = edit->tempoSequence.toBeats (transport.getLoopRange());
                    if (loop.contains (sync->beat) && next >= loop.getEnd())
                    {
                        auto wrapped = quantisation.getNext (loop.getStart());
                        if (! loop.contains (wrapped)) wrapped = loop.getStart();
                        distance = loop.getEnd() - sync->beat + (wrapped - loop.getStart());
                    }
                }
                return te::MonotonicBeat { sync->monotonicBeat.v + distance };
            }
        return {};
    }
    void queueTrack (te::AudioTrack& track, te::Clip* selected, std::optional<te::MonotonicBeat> boundary)
    {
        if (selected) track.launcherSilence.stop (boundary);
        else track.launcherSilence.play (boundary);
        for (auto* slot : track.getClipSlotList().getClipSlots())
            if (auto* c = slot->getClip())
                if (auto h = c->getLaunchHandle())
                {
                    if (c == selected) h->play (boundary);
                    else h->stop (boundary);
                }
    }
    static SubtractiveSynthPlugin* synth (te::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList.getPlugins())
            if (auto* instrument = dynamic_cast<SubtractiveSynthPlugin*> (plugin)) return instrument;
        return nullptr;
    }
    Result validateNote (te::MidiClip& clip, int pitch, double start, double length, float velocity) const
    {
        const auto beats = edit->tempoSequence.toBeats (clip.getPosition().time).getLength().inBeats();
        if (pitch < 0 || pitch > 127 || ! std::isfinite (start) || ! std::isfinite (length)
            || start < 0 || length < 1.0 / 960 || start + length > beats + 0.0000001
            || ! std::isfinite (velocity) || velocity <= 0 || velocity > 1)
            return Result::fail ("Notes need a pitch from 0 to 127, positive velocity, and a duration inside the MIDI clip.");
        return Result::ok();
    }
    te::Clip* clip (int t, int c) const
    {
        if (auto* tr = track (t))
        {
            auto clips = tr->getClips();
            if (juce::isPositiveAndBelow (c, clips.size())) return clips[c];
        }
        return nullptr;
    }
    juce::Array<te::Plugin*> plugins (int index) const
    {
        juce::Array<te::Plugin*> result;
        if (auto* t = track (index))
            for (auto* p : t->pluginList.getPlugins())
                if (dynamic_cast<te::ExternalPlugin*> (p) != nullptr || dynamic_cast<UtilityPlugin*> (p) != nullptr || dynamic_cast<NativeEffectPlugin*> (p) != nullptr) result.add (p);
        return result;
    }
    te::Plugin* plugin (int trackIndex, int pluginIndex) const
    {
        auto list = plugins (trackIndex);
        return juce::isPositiveAndBelow (pluginIndex, list.size()) ? list[pluginIndex] : nullptr;
    }
    File editLocation() const { return projectFile == File() ? scratch.getChildFile ("Untitled.halftrack") : projectFile; }
    File assets() const { auto f = editLocation(); return f.getSiblingFile (f.getFileNameWithoutExtension() + " Assets"); }
    void flushStructuralUpdates()
    {
        if (! edit) return;
        // Native sort actions must remain in the edit that triggered them, not
        // appear later as an unrelated Undo step when export pumps UI events.
        edit->getTrackList().dispatchPendingSort();
        edit->visitAllTracksRecursive ([] (te::Track& track)
        {
            if (auto* children = track.getSubTrackList()) children->dispatchPendingSort();
            if (auto* owner = dynamic_cast<te::ClipOwner*> (&track)) owner->dispatchPendingSort();
            if (auto* audio = dynamic_cast<te::AudioTrack*> (&track))
                for (auto* slot : audio->getClipSlotList().getClipSlots()) slot->dispatchPendingSort();
            return true;
        });
    }
    void changed()
    {
        flushStructuralUpdates();
        // Tracktion resets initialization changes on a deferred timer. Keep an
        // independent flag so that timer cannot clear a real user edit.
        explicitChanges = true;
        pristineNewDocument = false;
        edit->markAsChanged();
        owner.sendChangeMessage();
    }
    void transaction (const String& name)
    {
        flushStructuralUpdates();
        if (! gesture) edit->getUndoManager().beginNewTransaction (name);
    }
    void refreshRouting()
    {
        // Derived gates are published through native graph updates on the message
        // thread. They never replace user send levels or create undo actions.
        const auto tracks = te::getAudioTracks (*edit);
        bool anySolo = false, anySourceSolo = false;
        for (auto* t : tracks)
        {
            anySolo = anySolo || t->isSolo (false);
            anySourceSolo = anySourceSolo || (! isReturn (*t) && t->isSolo (false));
        }
        for (auto* destination : tracks)
        {
            if (! isReturn (*destination)) continue;
            for (auto* source : tracks)
            {
                if (isReturn (*source)) continue;
                if (auto* send = findSend (*source, destination->itemID.toString()))
                {
                    const bool selected = ! anySolo || source->isSolo (false) || destination->isSolo (false);
                    const bool audible = sendMode (*destination) == SendMode::pre || ! source->isMuted (false);
                    send->state.setProperty (te::IDs::enabled, selected && audible, nullptr);
                }
            }
            // Solo-in-place keeps every return output open, including effect
            // tails left by sources whose feeds are now Off or solo-suppressed.
            destination->state.setProperty (te::IDs::soloIsolate, anySourceSolo, nullptr);
        }
        edit->updateMuteSoloStatuses();
    }
    void refreshParameterValues()
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
                for (auto* parameter : p->getAutomatableParameters()) parameter->updateFromAttachedValue();
        for (auto* parameter : edit->getMasterVolumePlugin()->getAutomatableParameters()) parameter->updateFromAttachedValue();
        refreshRouting();
        edit->dispatchPendingUpdatesSynchronously();
    }
    void prepareEdit()
    {
        edit->editFileRetriever = [this] { return editLocation(); };
        flushStructuralUpdates();
        edit->playInStopEnabled = true;
        refreshRouting();
        if (useDevices) edit->getTransport().ensureContextAllocated();
        edit->dispatchPendingUpdatesSynchronously();
        edit->getUndoManager().clearUndoHistory();
        edit->resetChangedStatus();
        explicitChanges = false;
        owner.sendChangeMessage();
    }
    te::InputDeviceInstance* input (te::AudioTrack& t) const
    {
        for (auto* instance : edit->getAllInputDevices())
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice
                && instance->getTargets().contains (t.itemID)) return instance;
        return nullptr;
    }
    void refreshInputs()
    {
        if (! useDevices) return;
        edit->getTransport().ensureContextAllocated();
    }
    Meter& meter (const String& id, te::LevelMeasurer& m) const
    {
        auto& result = meters[id];
        if (! result || result->measurer.get() != &m) result = std::make_unique<Meter> (m);
        return *result;
    }
    Session& owner;
    te::Engine engine;
    bool useDevices;
    bool explicitChanges = false, pristineNewDocument = false, exporting = false;
    File scratch, projectFile;
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<te::Edit::UndoTransactionInhibitor> gesture;
    mutable std::map<String, std::unique_ptr<Meter>> meters;
};

Session::Session (bool enableAudioDevices, std::unique_ptr<juce::AudioIODeviceType> customDeviceType)
    : impl (std::make_unique<Impl> (*this, enableAudioDevices, std::move (customDeviceType))) { newProject(); }
Session::~Session() { closePluginEditors(); }

Session::Snapshot Session::snapshot() const
{
    Snapshot s;
    auto& e = *impl->edit;
    auto& transport = e.getTransport();
    s.projectFile = impl->projectFile;
    s.name = s.projectFile == File() ? "Untitled" : s.projectFile.getFileNameWithoutExtension();
    s.dirty = impl->explicitChanges || (! impl->pristineNewDocument && e.hasChangedSinceSaved());
    s.playing = transport.isPlaying(); s.recording = transport.isRecording();
    s.looping = transport.looping;
    s.position = transport.getPosition().inSeconds();
    s.duration = e.getLength().inSeconds();
    s.tempo = e.tempoSequence.getTempo (0)->getBpm();
    s.loopStart = transport.getLoopRange().getStart().inSeconds();
    s.loopEnd = transport.getLoopRange().getEnd().inSeconds();
    s.cpu = impl->engine.getDeviceManager().getCpuUsage();
    for (auto* scene : e.getSceneList().getScenes())
        s.scenes.push_back ({ scene->state.getProperty ("halftrackSceneID").toString(), scene->name.get() });
    const auto quantisation = e.getLaunchQuantisation().type.get();
    s.launchQuantisation = quantisation == te::LaunchQType::none ? LaunchQuantisation::none
        : quantisation == te::LaunchQType::quarter ? LaunchQuantisation::beat : LaunchQuantisation::bar;
    int index = 0;
    for (auto* t : te::getAudioTracks (e))
    {
        TrackInfo info;
        info.id = t->itemID.toString(); info.name = t->getName(); info.colour = t->getColour();
        info.mute = t->isMuted (false); info.solo = t->isSolo (false);
        info.isMidi = t->state.getProperty (midiTrackProperty, false);
        info.isReturn = isReturn (*t);
        if (info.isReturn) info.sendMode = sendMode (*t);
        else for (auto* destination : te::getAudioTracks (e))
            if (isReturn (*destination))
            {
                const auto id = destination->itemID.toString();
                auto* send = findSend (*t, id);
                info.sends.push_back ({ id, send ? send->getGainDb() : -100.0f });
            }
        if (auto* synth = Impl::synth (*t)) info.synth = synth->getSettings();
        if (auto* v = t->getVolumePlugin()) { info.gainDb = v->getVolumeDb(); info.pan = v->getPan(); }
        if (auto* input = impl->input (*t))
        {
            info.inputName = input->getInputDevice().getName();
            info.armed = input->isRecordingEnabled (t->itemID);
            info.monitor = static_cast<MonitorMode> (input->getInputDevice().getMonitorMode());
        }
        if (auto* level = t->getLevelMeterPlugin())
        {
            auto& m = impl->meter (info.id, level->measurer);
            info.peakL = m.read (0); info.peakR = m.read (1);
        }
        for (auto* c : t->getClips()) info.clips.push_back (describeClip (*c));
        info.launcherActive = t->playSlotClips.get() || (t->launcherOwnership.load (std::memory_order_acquire) & 1) != 0;
        for (auto* slot : t->getClipSlotList().getClipSlots())
        {
            SlotInfo slotInfo;
            slotInfo.id = slot->itemID.toString();
            if (auto* c = slot->getClip())
            {
                slotInfo.hasClip = true; slotInfo.clip = describeClip (*c);
                if (auto h = c->getLaunchHandle())
                {
                    slotInfo.playing = s.playing && h->getPlayingStatus() == te::LaunchHandle::PlayState::playing;
                    slotInfo.queued = h->getQueuedStatus() == te::LaunchHandle::QueueState::playQueued;
                    slotInfo.stopQueued = h->getQueuedStatus() == te::LaunchHandle::QueueState::stopQueued;
                    if (slotInfo.playing)
                        if (auto played = h->getPlayedRange())
                        {
                            const auto loop = c->getLoopRangeBeats();
                            slotInfo.playPositionBeats = played->getLength().inBeats();
                            if (loop.getLength().inBeats() > 0)
                                slotInfo.playPositionBeats = loop.getStart().inBeats()
                                    + std::fmod (slotInfo.playPositionBeats, loop.getLength().inBeats());
                        }
                }
            }
            info.slots.push_back (std::move (slotInfo));
        }
        for (auto* p : impl->plugins (index++))
        {
            PluginInfo device { p->itemID.toString(), p->getName(), p->isEnabled() };
            if (auto* utility = dynamic_cast<UtilityPlugin*> (p)) { device.isUtility = true; device.utility = utility->readSettings(); }
            if (auto* effect = dynamic_cast<NativeEffectPlugin*> (p)) { device.isNativeEffect = true; device.effect = effect->readSettings(); }
            info.plugins.push_back (std::move (device));
        }
        s.tracks.push_back (std::move (info));
    }
    if (auto* output = impl->engine.getDeviceManager().getDefaultWaveOutDevice())
    {
        auto& m = impl->meter ("master", output->levelMeasurer);
        s.masterPeakL = m.read (0); s.masterPeakR = m.read (1);
    }
    return s;
}

Result Session::newProject()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    endEditGesture();
    closePluginEditors();
    if (impl->edit) impl->edit->getTransport().stop (false, false);
    impl->meters.clear();
    impl->projectFile = File{};
    auto state = te::createEmptyEdit (impl->engine);
    auto options = te::Edit::Options { impl->engine, state, te::ProjectItemID::fromProperty (state, te::IDs::projectID) };
    options.numAudioTracks = 0; options.defaultMasterVolumedB = 0;
    options.editFileRetriever = [this] { return impl->editLocation(); };
    impl->edit = std::make_unique<te::Edit> (std::move (options));
    impl->edit->getMasterVolumePlugin()->setVolumeDb (0);
    impl->edit->tempoSequence.getTempo (0)->setBpm (120);
    impl->edit->getTransport().setLoopRange ({ te::TimePosition::fromSeconds (0), te::TimePosition::fromSeconds (8) });
    for (auto* t : te::getAudioTracks (*impl->edit)) impl->edit->deleteTrack (t);
    addAudioTrack ("Audio 1"); addAudioTrack ("Audio 2");
    impl->edit->getSceneList().ensureNumberOfScenes (8);
    for (auto* scene : impl->edit->getSceneList().getScenes())
    {
        scene->state.setProperty ("halftrackSceneID", juce::Uuid().toString(), nullptr);
        scene->name = "Scene " + String (scene->getIndex() + 1);
    }
    impl->prepareEdit();
    impl->pristineNewDocument = true;
    return Result::ok();
}

Result Session::loadProject (const File& file)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto xml = juce::XmlDocument::parse (file);
    if (! xml || ! xml->hasTagName ("HALFTRACK") || xml->getIntAttribute ("version") != 1)
        return Result::fail ("This is not a supported Halftrack project.");
    auto* editXml = xml->getChildByName ("EDIT");
    if (! editXml) return Result::fail ("The project has no session data.");
    auto state = juce::ValueTree::fromXml (*editXml);
    // Reject malformed launcher ownership before native constructors can build
    // hidden clips or assert on a slot containing more than one clip.
    int savedSceneCount = 0;
    for (const auto& child : state.getChildWithName (te::IDs::SCENES))
        if (child.hasType (te::IDs::SCENE)) ++savedSceneCount;
    if (savedSceneCount > 256) return Result::fail ("This project exceeds the supported scene count.");
    const auto validateSlots = [&] (auto&& self, const juce::ValueTree& node) -> Result
    {
        if (node.hasType (te::IDs::CLIPSLOTS))
        {
            int rowCount = 0;
            for (const auto& slot : node)
                if (slot.hasType (te::IDs::CLIPSLOT))
                {
                    ++rowCount;
                    int clipCount = 0;
                    for (const auto& clip : slot)
                        if (te::Clip::isClipState (clip) && ++clipCount > 1)
                            return Result::fail ("A session slot cannot contain more than one clip.");
                }
            if (rowCount > savedSceneCount)
                return Result::fail ("This project has clip slots without matching scenes.");
        }
        for (const auto& child : node)
            if (auto result = self (self, child); result.failed()) return result;
        return Result::ok();
    };
    if (auto result = validateSlots (validateSlots, state); result.failed()) return result;
    if (auto result = validateRouting (state); result.failed()) return result;
    if (auto result = UtilityPlugin::validateState (state); result.failed()) return result;
    if (auto result = NativeEffectPlugin::validateState (state); result.failed()) return result;
    if (auto result = validateAudioClips (state); result.failed()) return result;
    auto id = te::ProjectItemID::fromProperty (state, te::IDs::projectID);
    if (! id.isValid()) id = te::ProjectItemID::createNewID (te::ProjectID{});
    auto options = te::Edit::Options { impl->engine, state, id };
    options.numAudioTracks = 0;
    options.editFileRetriever = [file] { return file; };
    auto candidate = te::Edit::createEdit (std::move (options));
    if (! candidate) return Result::fail ("This project could not be loaded.");
    for (auto* t : te::getAudioTracks (*candidate))
        for (auto* p : t->pluginList.getPlugins())
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (p))
                if (! external->isInitialisingAsync() && external->getAudioPluginInstance() == nullptr)
                    return Result::fail ("Could not load plug-in " + external->getName()
                        + ". Open Scan / Manage Plug-ins and scan its installed format, then reopen this project. "
                        + external->getLoadError());
    for (auto* t : te::getAudioTracks (*candidate))
        for (auto* c : allTrackClips (*t))
            if (c->usesSourceFile())
            {
                auto& reference = c->getSourceFileReference();
                const auto resolved = reference.getFile();
                if (! resolved.existsAsFile()) return Result::fail ("Missing audio file: " + resolved.getFullPathName());
                // In-memory undo can retain detached clip trees across Save As.
                // Anchor their media before edits; only the saved copy is relative.
                reference.source.setValue (resolved.getFullPathName(), nullptr);
            }
    endEditGesture(); closePluginEditors(); stop(); impl->meters.clear();
    impl->projectFile = file;
    impl->pristineNewDocument = false;
    impl->edit = std::move (candidate);
    for (auto* t : te::getAudioTracks (*impl->edit))
    {
        t->getClipSlotList().ensureNumberOfSlots (impl->edit->getSceneList().getNumScenes());
        t->playSlotClips = false;
    }
    for (auto* scene : impl->edit->getSceneList().getScenes())
        if (! scene->state.hasProperty ("halftrackSceneID"))
            scene->state.setProperty ("halftrackSceneID", juce::Uuid().toString(), nullptr);
    impl->prepareEdit();
    impl->refreshInputs();
    for (auto* t : te::getAudioTracks (*impl->edit))
        if (auto* input = impl->input (*t))
        {
            input->setRecordingEnabled (t->itemID, false);
            input->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::off);
        }
    impl->edit->resetChangedStatus();
    return Result::ok();
}

Result Session::saveProject (const File& destination)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before saving.");
    auto file = destination.withFileExtension (".halftrack");
    auto assets = file.getSiblingFile (file.getFileNameWithoutExtension() + " Assets");
    auto directoryResult = assets.createDirectory();
    if (directoryResult.failed()) return directoryResult;
    impl->edit->flushState();
    auto state = impl->edit->state.createCopy();
    std::map<String, File> copied;
    std::vector<std::pair<te::Clip*, File>> sources;
    for (auto* t : te::getAudioTracks (*impl->edit))
        for (auto* c : allTrackClips (*t))
        {
            if (! c->usesSourceFile()) continue;
            auto original = c->getSourceFileReference().getFile();
            if (! original.existsAsFile()) return Result::fail ("Missing audio file: " + original.getFullPathName());
            File target;
            if (original.isAChildOf (assets)) target = original;
            else if (auto it = copied.find (original.getFullPathName()); it != copied.end()) target = it->second;
            else
            {
                target = assets.getNonexistentChildFile (original.getFileNameWithoutExtension(), original.getFileExtension(), true);
                if (! original.copyFileTo (target)) return Result::fail ("Could not collect audio: " + original.getFileName());
                copied[original.getFullPathName()] = target;
            }
            auto node = findStateWithID (state, c->state.getProperty (te::IDs::id));
            node.setProperty (te::IDs::source, target.getRelativePathFrom (file.getParentDirectory()), nullptr);
            sources.emplace_back (c, target);
        }
    juce::XmlElement document ("HALFTRACK");
    document.setAttribute ("version", 1);
    document.addChildElement (state.createXml().release());
    juce::TemporaryFile temp (file);
    if (! document.writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
        return Result::fail ("Could not write the project file.");
    impl->projectFile = file;
    impl->edit->editFileRetriever = [this] { return impl->editLocation(); };
    for (auto& [clip, source] : sources)
        // Keep running and undo-retained sources independent of the project
        // directory. The written copy above remains relative and portable.
        // Native listeners refresh playback, but relocation adds no undo action.
        clip->getSourceFileReference().source.setValue (source.getFullPathName(), nullptr);
    impl->edit->resetChangedStatus();
    impl->explicitChanges = false;
    sendChangeMessage();
    return Result::ok();
}

Result Session::addAudioTrack (String name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->transaction ("Add audio track");
    const auto count = te::getAudioTracks (*impl->edit).size();
    impl->edit->ensureNumberOfAudioTracks (count + 1);
    auto* t = impl->track (count);
    if (! t) return Result::fail ("Could not create an audio track.");
    t->setName (name.isEmpty() ? "Audio " + String (count + 1) : name);
    const juce::uint32 colours[] = { 0xffd0a35e, 0xff86a395, 0xff899bbb, 0xffb98579, 0xffaaa179, 0xffa58aaa };
    t->setColour (juce::Colour (colours[count % 6]));
    if (auto result = useTrackMixer (*t); result.failed()) return result;
    t->getVolumePlugin()->setVolumeDb (0);
    t->getClipSlotList().ensureNumberOfSlots (impl->edit->getSceneList().getNumScenes());
    impl->changed();
    return Result::ok();
}
Result Session::addMidiTrack (String name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->transaction ("Add MIDI track");
    const auto count = te::getAudioTracks (*impl->edit).size();
    impl->edit->ensureNumberOfAudioTracks (count + 1);
    auto* track = impl->track (count);
    if (! track) return Result::fail ("Could not create a MIDI track.");
    track->setName (name.isEmpty() ? "MIDI " + String (count + 1) : name);
    track->setColour (juce::Colour (0xff9d91bc));
    track->state.setProperty (midiTrackProperty, true, &impl->edit->getUndoManager());
    if (auto result = useTrackMixer (*track); result.failed()) return result;
    track->getVolumePlugin()->setVolumeDb (0);
    auto synth = impl->edit->getPluginCache().createNewPlugin (SubtractiveSynthPlugin::xmlTypeName, {});
    if (! synth) return Result::fail ("Could not create the built-in synthesizer.");
    track->pluginList.insertPlugin (synth, 0, nullptr);
    track->getClipSlotList().ensureNumberOfSlots (impl->edit->getSceneList().getNumScenes());
    impl->changed(); return Result::ok();
}
Result Session::addReturnTrack (String name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->transaction ("Add return track");
    auto input = impl->edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
    auto* aux = dynamic_cast<te::AuxReturnPlugin*> (input.get());
    if (! aux) return Result::fail ("Could not create the return input.");
    std::set<int> used;
    for (auto* t : te::getAudioTracks (*impl->edit)) if (isReturn (*t)) used.insert (static_cast<int> (t->state[returnBusProperty]));
    int bus = 0;
    while (used.contains (bus)) ++bus;
    const auto count = te::getAudioTracks (*impl->edit).size();
    impl->edit->ensureNumberOfAudioTracks (count + 1);
    auto* track = impl->track (count);
    if (! track) return Result::fail ("Could not create the return track.");
    auto* undo = &impl->edit->getUndoManager();
    track->state.setProperty (returnProperty, true, undo);
    track->state.setProperty (returnBusProperty, bus, undo);
    track->state.setProperty (returnModeProperty, static_cast<int> (SendMode::post), undo);
    track->setName (name.trim().isEmpty() ? "Return " + String (static_cast<int> (used.size()) + 1) : name.trim());
    track->setColour (juce::Colour (0xffb9a283));
    if (auto result = useTrackMixer (*track); result.failed()) return result;
    track->getVolumePlugin()->setVolumeDb (0);
    aux->busNumber = bus;
    track->pluginList.insertPlugin (input, 0, nullptr);
    track->getClipSlotList().ensureNumberOfSlots (impl->edit->getSceneList().getNumScenes());
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::setTrackSendGain (int index, const String& destinationID, float decibels)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* source = impl->track (index); auto* destination = impl->track (destinationID);
    if (! source || isReturn (*source) || ! destination || ! isReturn (*destination))
        return Result::fail ("Sends must connect an audio or MIDI track to a return track.");
    if (! std::isfinite (decibels) || decibels < -100.0f || decibels > 0)
        return Result::fail ("Send gain must be between Off (-100 dB) and 0 dB.");
    auto* send = findSend (*source, destinationID);
    if (! send && decibels == -100.0f) return Result::ok();
    impl->transaction ("Send level");
    if (! send)
    {
        auto plugin = impl->edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
        send = dynamic_cast<te::AuxSendPlugin*> (plugin.get());
        if (! send) return Result::fail ("Could not create the send.");
        send->state.setProperty (sendReturnProperty, destinationID, &impl->edit->getUndoManager());
        send->busNumber = static_cast<int> (destination->state[returnBusProperty]);
        send->setGainDb (-100.0f);
        const auto mixer = source->pluginList.indexOf (source->getVolumePlugin());
        source->pluginList.insertPlugin (plugin, mixer + (sendMode (*destination) == SendMode::post ? 1 : 0), nullptr);
    }
    send->setGainDb (decibels);
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::setReturnSendMode (const String& destinationID, SendMode mode)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* destination = impl->track (destinationID);
    if (! destination || ! isReturn (*destination)) return Result::fail ("Return track not found.");
    if (mode != SendMode::pre && mode != SendMode::post) return Result::fail ("Unknown send mode.");
    if (mode == sendMode (*destination)) return Result::ok();
    impl->transaction ("Return send mode");
    destination->state.setProperty (returnModeProperty, static_cast<int> (mode), &impl->edit->getUndoManager());
    for (auto* source : te::getAudioTracks (*impl->edit))
        if (auto* send = findSend (*source, destinationID))
        {
            te::Plugin::Ptr keepAlive (send);
            send->deleteFromParent();
            const auto mixer = source->pluginList.indexOf (source->getVolumePlugin());
            source->pluginList.insertPlugin (keepAlive, mixer + (mode == SendMode::post ? 1 : 0), nullptr);
        }
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::addMidiClip (const String& trackId, double start, double length)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackId);
    if (! track || isReturn (*track) || ! static_cast<bool> (track->state.getProperty (midiTrackProperty))) return Result::fail ("Select a MIDI track first.");
    if (! std::isfinite (start) || ! std::isfinite (length) || start < 0 || length <= 0 || start + length > 1000000)
        return Result::fail ("Enter a positive MIDI clip duration and a valid start beat.");
    impl->transaction ("Add MIDI clip");
    const auto time = impl->edit->tempoSequence.toTime (te::BeatRange { te::BeatPosition::fromBeats (start), te::BeatPosition::fromBeats (start + length) });
    auto clip = track->insertMIDIClip ("MIDI clip", time, nullptr);
    if (! clip) return Result::fail ("Could not create the MIDI clip.");
    clip->setSyncType (te::Clip::syncBarsBeats);
    impl->changed(); return Result::ok();
}
Result Session::addScene (String name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing scenes.");
    auto& list = impl->edit->getSceneList();
    const auto count = list.getNumScenes();
    if (count >= 256) return Result::fail ("This project has reached the scene limit.");
    impl->transaction ("Add scene");
    list.ensureNumberOfScenes (count + 1);
    auto* scene = list.getScenes()[count];
    scene->state.setProperty ("halftrackSceneID", juce::Uuid().toString(), &impl->edit->getUndoManager());
    scene->name = name.trim().isEmpty() ? "Scene " + String (count + 1) : name.trim();
    impl->changed(); return Result::ok();
}
Result Session::removeScene (int index)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing scenes.");
    auto& list = impl->edit->getSceneList();
    if (! juce::isPositiveAndBelow (index, list.getNumScenes())) return Result::fail ("Scene not found.");
    impl->transaction ("Remove scene");
    list.deleteScene (*list.getScenes()[index]);
    impl->changed(); return Result::ok();
}
Result Session::renameScene (int index, const String& name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto& list = impl->edit->getSceneList();
    if (! juce::isPositiveAndBelow (index, list.getNumScenes())) return Result::fail ("Scene not found.");
    if (name.trim().isEmpty()) return Result::fail ("Enter a scene name.");
    impl->transaction ("Rename scene"); list.getScenes()[index]->name = name.trim();
    impl->changed(); return Result::ok();
}
Result Session::importAudioToSlot (const File& source, int trackIndex, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing slots.");
    auto* slot = impl->slot (trackIndex, sceneIndex);
    if (! slot) return Result::fail ("Slot not found.");
    if (slot->getClip()) return Result::fail ("Clear this slot before importing another clip.");
    if (static_cast<bool> (slot->track.state.getProperty (midiTrackProperty))) return Result::fail ("Import audio onto an audio track.");
    te::AudioFile audio (impl->engine, source);
    if (! audio.isValid() || audio.getLength() <= 0) return Result::fail ("This audio file could not be read.");
    auto assets = impl->assets(); auto result = assets.createDirectory(); if (result.failed()) return result;
    auto file = source.isAChildOf (assets) ? source : assets.getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), true);
    if (file != source && ! source.copyFileTo (file)) return Result::fail ("Could not copy the imported audio.");
    impl->transaction ("Import session clip");
    const te::TimeRange range { te::TimePosition::fromSeconds (0), te::TimePosition::fromSeconds (audio.getLength()) };
    auto clip = te::insertWaveClip (*slot, source.getFileNameWithoutExtension(), file, { range, {} }, te::DeleteExistingClips::no);
    if (! clip) return Result::fail ("Could not create the audio clip.");
    clip->getSourceFileReference().setToFile (file, te::SourceFileReference::PathStyle::alwaysAbsolute, false);
    clip->setAutoTempo (true); clip->setAutoPitch (false); clip->setUsesProxy (false);
    clip->setLoopRange (range);
    clip->setFadeIn (te::TimeDuration::fromSeconds (0)); clip->setFadeOut (te::TimeDuration::fromSeconds (0));
    impl->changed(); return Result::ok();
}
Result Session::addMidiClipToSlot (const String& trackId, int sceneIndex, double length)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing slots.");
    auto* track = impl->track (trackId);
    if (! track || isReturn (*track) || ! static_cast<bool> (track->state.getProperty (midiTrackProperty))) return Result::fail ("Select a MIDI track first.");
    if (! juce::isPositiveAndBelow (sceneIndex, impl->edit->getSceneList().getNumScenes())) return Result::fail ("Scene not found.");
    auto* slot = track->getClipSlotList().getClipSlots()[sceneIndex];
    if (! slot) return Result::fail ("Slot not found.");
    if (slot->getClip()) return Result::fail ("Clear this slot before creating another clip.");
    if (! std::isfinite (length) || length <= 0 || length > 1000000) return Result::fail ("Enter a positive clip length in beats.");
    impl->transaction ("Add session MIDI clip");
    const te::BeatRange beats { te::BeatPosition::fromBeats (0), te::BeatPosition::fromBeats (length) };
    auto clip = te::insertMIDIClip (*slot, "MIDI clip", impl->edit->tempoSequence.toTime (beats));
    if (! clip) return Result::fail ("Could not create the MIDI clip.");
    clip->setSyncType (te::Clip::syncBarsBeats); clip->setLoopRangeBeats (beats);
    impl->changed(); return Result::ok();
}
Result Session::clearSlot (int trackIndex, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before editing slots.");
    auto* slot = impl->slot (trackIndex, sceneIndex);
    if (! slot) return Result::fail ("Slot not found.");
    if (auto* clip = slot->getClip())
    {
        impl->transaction ("Clear session clip"); clip->removeFromParent(); impl->changed();
    }
    return Result::ok();
}
Result Session::setLaunchQuantisation (LaunchQuantisation quantisation)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    te::LaunchQType type;
    switch (quantisation)
    {
        case LaunchQuantisation::none: type = te::LaunchQType::none; break;
        case LaunchQuantisation::beat: type = te::LaunchQType::quarter; break;
        case LaunchQuantisation::bar: type = te::LaunchQType::bar; break;
        default: return Result::fail ("Unknown launch quantisation.");
    }
    impl->transaction ("Launch quantisation"); impl->edit->getLaunchQuantisation().type = type;
    impl->changed(); return Result::ok();
}
Result Session::launchSlot (int trackIndex, int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* slot = impl->slot (trackIndex, sceneIndex);
    if (! slot) return Result::fail ("Slot not found.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio output in Audio Settings.");
    impl->edit->dispatchPendingUpdatesSynchronously();
    impl->edit->getTransport().ensureContextAllocated();
    impl->queueTrack (*impl->track (trackIndex), slot->getClip(), impl->launchBoundary());
    if (! impl->edit->getTransport().isPlaying()) impl->edit->getTransport().play (false);
    sendChangeMessage(); return Result::ok();
}
Result Session::launchScene (int sceneIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! juce::isPositiveAndBelow (sceneIndex, impl->edit->getSceneList().getNumScenes())) return Result::fail ("Scene not found.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio output in Audio Settings.");
    impl->edit->dispatchPendingUpdatesSynchronously();
    impl->edit->getTransport().ensureContextAllocated();
    const auto boundary = impl->launchBoundary();
    for (auto* track : te::getAudioTracks (*impl->edit))
        if (! isReturn (*track)) impl->queueTrack (*track, track->getClipSlotList().getClipSlots()[sceneIndex]->getClip(), boundary);
    if (! impl->edit->getTransport().isPlaying()) impl->edit->getTransport().play (false);
    sendChangeMessage(); return Result::ok();
}
Result Session::stopLauncherTrack (int trackIndex)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackIndex); if (! track) return Result::fail ("Track not found.");
    if (isReturn (*track)) return Result::fail ("Return tracks do not launch clips.");
    impl->queueTrack (*track, nullptr, impl->launchBoundary());
    sendChangeMessage(); return Result::ok();
}
Result Session::stopAllLauncherClips()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    const auto boundary = impl->launchBoundary();
    for (auto* track : te::getAudioTracks (*impl->edit))
        if (! isReturn (*track)) impl->queueTrack (*track, nullptr, boundary);
    sendChangeMessage(); return Result::ok();
}
Result Session::returnToArrangement()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    impl->cancelLaunches();
    for (auto* track : te::getAudioTracks (*impl->edit))
    {
        track->playSlotClips = false;
        track->releaseLauncherOwnership();
    }
    sendChangeMessage(); return Result::ok();
}

Result Session::addMidiNote (const String& clipId, int pitch, double start, double length, float velocity)
{
    juce::StringArray insertedIds;
    return addMidiNotes (clipId, { { {}, pitch, start, length, velocity } }, insertedIds);
}
Result Session::addMidiNotes (const String& clipId, const std::vector<MidiNoteInfo>& notes, juce::StringArray& insertedIds)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (notes.empty()) { insertedIds.clear(); return Result::ok(); }
    const auto count = clip->getSequence().getNumNotes();
    if (count >= 100000 || notes.size() > static_cast<size_t> (100000 - count))
        return Result::fail ("The MIDI clip has reached its note limit.");
    for (const auto& note : notes)
        if (auto valid = impl->validateNote (*clip, note.pitch, note.startBeat, note.lengthBeats, note.velocity); valid.failed())
            return valid;
    std::set<String> unavailableIds;
    for (auto* note : clip->getSequence().getNotes())
        unavailableIds.insert (note->state.getProperty (midiNoteIDProperty).toString());
    for (const auto& note : notes) unavailableIds.insert (note.id);
    auto prepared = notes;
    juce::StringArray newIds;
    for (auto& note : prepared)
    {
        do { note.id = juce::Uuid().toString(); } while (! unavailableIds.insert (note.id).second);
        newIds.add (note.id);
    }
    impl->transaction (prepared.size() == 1 ? "Add MIDI note" : "Add MIDI notes");
    auto* undo = &impl->edit->getUndoManager();
    for (const auto& value : prepared)
    {
        auto* note = clip->getSequence().addNote (value.pitch, te::BeatPosition::fromBeats (value.startBeat),
                                                 te::BeatDuration::fromBeats (value.lengthBeats),
                                                 juce::jlimit (1, 127, juce::roundToInt (value.velocity * 127)), 0, undo);
        // Native addNote synchronously creates an event for its new NOTE state.
        jassert (note != nullptr);
        note->state.setProperty (midiNoteIDProperty, value.id, undo);
    }
    insertedIds = std::move (newIds);
    impl->changed(); return Result::ok();
}
Result Session::updateMidiNote (const String& clipId, const String& noteId, int pitch, double start, double length, float velocity)
{
    return updateMidiNotes (clipId, { { noteId, pitch, start, length, velocity } });
}
Result Session::updateMidiNotes (const String& clipId, const std::vector<MidiNoteInfo>& notes)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (notes.empty()) return Result::ok();
    std::map<String, te::MidiNote*> existing;
    for (auto* note : clip->getSequence().getNotes())
    {
        const auto id = note->state.getProperty (midiNoteIDProperty).toString();
        auto [entry, inserted] = existing.emplace (id, note);
        if (! inserted) entry->second = nullptr; // A loaded duplicate ID is ambiguous.
    }
    struct Change { te::MidiNote* note; MidiNoteInfo value; int velocity; };
    std::vector<Change> changes;
    std::set<String> requested;
    for (const auto& value : notes)
    {
        if (value.id.isEmpty() || ! requested.insert (value.id).second)
            return Result::fail ("Each MIDI note needs a unique identifier.");
        const auto found = existing.find (value.id);
        if (found == existing.end() || found->second == nullptr)
            return Result::fail ("MIDI note not found or its identifier is ambiguous.");
        auto valid = impl->validateNote (*clip, value.pitch, value.startBeat, value.lengthBeats, value.velocity);
        if (valid.failed()) return valid;
        auto* note = found->second;
        const int velocity = juce::jlimit (1, 127, juce::roundToInt (value.velocity * 127));
        if (note->getNoteNumber() != value.pitch || note->getStartBeat().inBeats() != value.startBeat
            || note->getLengthBeats().inBeats() != value.lengthBeats || note->getVelocity() != velocity)
            changes.push_back ({ note, value, velocity });
    }
    // Resolve and validate the whole batch before touching native state or undo.
    if (changes.empty()) return Result::ok();
    impl->transaction ("Edit MIDI notes");
    auto* undo = &impl->edit->getUndoManager();
    for (const auto& change : changes)
    {
        change.note->setNoteNumber (change.value.pitch, undo);
        change.note->setStartAndLength (te::BeatPosition::fromBeats (change.value.startBeat),
                                        te::BeatDuration::fromBeats (change.value.lengthBeats), undo);
        change.note->setVelocity (change.velocity, undo);
    }
    impl->changed(); return Result::ok();
}
Result Session::transformMidiNotes (const String& clipId, const juce::StringArray& ids, NoteTransform operation, double grid)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (! std::isfinite (grid) || grid < 1.0/960 || grid > 16
        || static_cast<int> (operation) < 0 || static_cast<int> (operation) > 7)
        return Result::fail ("Invalid note transformation or grid.");
    if (ids.isEmpty()) return Result::ok();
    std::set<String> requested;
    for (const auto& id : ids) if (id.isEmpty() || ! requested.insert (id).second)
        return Result::fail ("Each selected note needs a unique identifier.");
    std::vector<MidiNoteInfo> notes;
    for (auto* note : clip->getSequence().getNotes())
    {
        const auto id = note->state.getProperty (midiNoteIDProperty).toString();
        if (requested.contains (id)) notes.push_back ({ id, note->getNoteNumber(), note->getStartBeat().inBeats(),
            note->getLengthBeats().inBeats(), note->getVelocity()/127.0f });
    }
    if (notes.size() != requested.size()) return Result::fail ("A selected note is missing or ambiguous.");
    const auto clipLength = clip->getLengthInBeats().inBeats();
    double first = notes.front().startBeat, last = first + notes.front().lengthBeats;
    int low = notes.front().pitch, high = low;
    for (const auto& note : notes)
    {
        first = std::min (first, note.startBeat); last = std::max (last, note.startBeat+note.lengthBeats);
        low = std::min (low, note.pitch); high = std::max (high, note.pitch);
    }
    const auto original = notes;
    for (auto& note : notes)
    {
        switch (operation)
        {
            case NoteTransform::quantizeStarts:
                note.startBeat = std::clamp (std::floor (note.startBeat/grid+.5)*grid, 0.0, std::max (0.0, clipLength-note.lengthBeats));
                break;
            case NoteTransform::quantizeEnds:
            {
                const auto end = std::min (clipLength, std::max (note.startBeat+std::min(grid,1.0/960),
                    std::floor ((note.startBeat+note.lengthBeats)/grid+.5)*grid));
                note.lengthBeats = std::max (1.0/960, end-note.startBeat); break;
            }
            case NoteTransform::reverse: note.startBeat = std::max (0.0, first+last-note.startBeat-note.lengthBeats); break;
            case NoteTransform::invertPitch: note.pitch = low+high-note.pitch; break;
            case NoteTransform::legato:
            {
                auto next = last;
                for (const auto& other : original) if (other.startBeat > note.startBeat+1e-10) next = std::min (next, other.startBeat);
                note.lengthBeats = next-note.startBeat; break;
            }
            case NoteTransform::halfTime:
            case NoteTransform::doubleTime:
            {
                const auto factor = operation == NoteTransform::halfTime ? .5 : 2.0;
                note.startBeat = first+(note.startBeat-first)*factor; note.lengthBeats *= factor; break;
            }
            case NoteTransform::fixedLength: note.lengthBeats = std::min (grid, clipLength-note.startBeat); break;
        }
    }
    // Existing batch validation rejects the complete edit before changing any note.
    return updateMidiNotes (clipId, notes);
}
Result Session::removeMidiNote (const String& clipId, const String& noteId)
{
    return removeMidiNotes (clipId, { noteId });
}
Result Session::removeMidiNotes (const String& clipId, const juce::StringArray& noteIds)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    if (noteIds.isEmpty()) return Result::ok();
    std::map<String, te::MidiNote*> existing;
    for (auto* note : clip->getSequence().getNotes())
    {
        auto [entry, inserted] = existing.emplace (note->state.getProperty (midiNoteIDProperty).toString(), note);
        if (! inserted) entry->second = nullptr;
    }
    std::set<String> requested;
    std::vector<te::MidiNote*> targets;
    for (const auto& id : noteIds)
    {
        if (id.isEmpty() || ! requested.insert (id).second)
            return Result::fail ("Each MIDI note needs a unique identifier.");
        const auto found = existing.find (id);
        if (found == existing.end() || found->second == nullptr)
            return Result::fail ("MIDI note not found or its identifier is ambiguous.");
        targets.push_back (found->second);
    }
    impl->transaction (targets.size() == 1 ? "Remove MIDI note" : "Remove MIDI notes");
    for (auto* note : targets) clip->getSequence().removeNote (*note, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setSynthSettings (const String& trackId, const SynthSettings& settings)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (trackId); auto* synth = track ? Impl::synth (*track) : nullptr;
    if (! synth) return Result::fail ("Built-in synthesizer not found.");
    auto valid = [] (float value, float low, float high) { return std::isfinite (value) && value >= low && value <= high; };
    if (! valid (settings.cutoffHz, 20, 20000) || ! valid (settings.resonance, 0, 1)
        || ! valid (settings.attackSeconds, 0, 10) || ! valid (settings.decaySeconds, 0, 10)
        || ! valid (settings.sustain, 0, 1) || ! valid (settings.releaseSeconds, 0, 20)
        || ! valid (settings.level, 0, 1) || ! valid (settings.pulseWidth, 0, 1)
        || settings.waveform < 0 || settings.waveform > 3)
        return Result::fail ("Synthesizer settings are outside their supported range.");
    impl->transaction ("Synthesizer settings"); synth->setSettings (settings); impl->changed(); return Result::ok();
}
Result Session::removeTrack (int index)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before removing tracks.");
    closePluginEditors(); impl->meters.clear(); impl->transaction ("Remove track");
    if (isReturn (*t))
        for (auto* source : te::getAudioTracks (*impl->edit))
            if (auto* send = findSend (*source, t->itemID.toString())) send->deleteFromParent();
    impl->edit->deleteTrack (t); impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::importAudio (const File& source, int index, double start)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! source.existsAsFile()) return Result::fail ("The audio file does not exist.");
    te::AudioFile audio (impl->engine, source);
    if (! audio.isValid() || audio.getLength() <= 0) return Result::fail ("This audio file could not be read.");
    if (! std::isfinite (start) || start < 0) return Result::fail ("Clip start must be zero or greater.");
    if (index < 0) { auto r = addAudioTrack (source.getFileNameWithoutExtension()); if (r.failed()) return r; index = te::getAudioTracks (*impl->edit).size() - 1; }
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (isReturn (*t) || static_cast<bool> (t->state.getProperty (midiTrackProperty))) return Result::fail ("Import audio onto an audio track.");
    auto assets = impl->assets(); auto r = assets.createDirectory(); if (r.failed()) return r;
    auto file = source.isAChildOf (assets) ? source : assets.getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), true);
    if (file != source && ! source.copyFileTo (file)) return Result::fail ("Could not copy the imported audio.");
    impl->transaction ("Import audio");
    auto clip = t->insertWaveClip (source.getFileNameWithoutExtension(), file,
        { { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (start + audio.getLength()) }, {} }, false);
    if (! clip) return Result::fail ("Could not create the audio clip.");
    clip->getSourceFileReference().setToFile (file, te::SourceFileReference::PathStyle::alwaysAbsolute, false);
    clip->setAutoTempo (false); clip->setAutoPitch (false);
    clip->setFadeIn (te::TimeDuration::fromSeconds (0)); clip->setFadeOut (te::TimeDuration::fromSeconds (0));
    impl->changed(); return Result::ok();
}
Result Session::setAudioClipSettings (const String& id, const AudioClipSettings& settings)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (! validAudioSettings (settings, clip->getPosition().getLength().inSeconds()))
        return Result::fail ("Clip gain or fades are outside their supported range. Fades must fit inside the clip.");
    const auto previous = audioSettings (*clip);
    if (clip->getClipSlot() && (previous.fadeInSeconds != settings.fadeInSeconds || previous.fadeOutSeconds != settings.fadeOutSeconds
        || previous.fadeInCurve != settings.fadeInCurve || previous.fadeOutCurve != settings.fadeOutCurve))
        return Result::fail ("Manual fade editing is available for Arrangement clips.");
    if (previous == settings && ! clip->getAutoCrossfade()) return Result::ok();
    impl->transaction ("Audio clip gain and fades");
    clip->setAutoCrossfade (false); clip->setGainDB (settings.gainDb);
    // Clear first so the engine's one-edge setters cannot rescale the other edge.
    clip->setFadeIn ({}); clip->setFadeOut (te::TimeDuration::fromSeconds (settings.fadeOutSeconds));
    clip->setFadeIn (te::TimeDuration::fromSeconds (settings.fadeInSeconds));
    clip->setFadeInType (static_cast<te::AudioFadeCurve::Type> (settings.fadeInCurve));
    clip->setFadeOutType (static_cast<te::AudioFadeCurve::Type> (settings.fadeOutCurve));
    impl->changed(); return Result::ok();
}
Result Session::setAudioClipRange (const String& id, double start, double length, double offset)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before trimming clips.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (! canEditAudioRange (*clip)) return Result::fail ("Range editing requires an unwarped, unlooped arrangement audio clip.");
    const auto sourceLength = clip->getSourceLength().inSeconds();
    if (! std::isfinite (start) || ! std::isfinite (length) || ! std::isfinite (offset)
        || start < 0 || start > 1e9 || length < .001 || length > 1e9 || offset < 0
        || offset + length > sourceLength + 1e-9 || ! validAudioSettings (audioSettings (*clip), length))
        return Result::fail ("The range must fit the source and existing fades, with at least 1 ms of audio.");
    const auto p = clip->getPosition();
    if (p.getStart().inSeconds() == start && p.getLength().inSeconds() == length && p.offset.inSeconds() == offset) return Result::ok();
    impl->transaction ("Trim audio clip");
    clip->setPosition ({ { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (start+length) }, te::TimeDuration::fromSeconds (offset) });
    impl->changed(); return Result::ok();
}
Result Session::splitAudioClip (const String& id, double at, String& rightId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before splitting clips.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    const auto p = clip->getPosition(); const auto s = audioSettings (*clip);
    const auto left = at-p.getStart().inSeconds(), right = p.getEnd().inSeconds()-at;
    if (! canEditAudioRange (*clip) || ! std::isfinite (at) || left < .002 || right < .002
        || left < s.fadeInSeconds || right < s.fadeOutSeconds)
        return Result::fail ("Split inside an unwarped arrangement clip, outside its fades, leaving at least 2 ms on each side.");
    impl->transaction ("Split audio clip");
    auto* second = clip->getClipTrack()->splitClip (*clip, te::TimePosition::fromSeconds (at));
    if (! second) return Result::fail ("The audio clip could not be split.");
    rightId = second->itemID.toString(); impl->changed(); return Result::ok();
}
Result Session::duplicateAudioClip (const String& id, String& duplicateId)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before duplicating clips.");
    auto* clip = impl->audioClip (id); if (! clip) return Result::fail ("Audio clip not found.");
    if (clip->getClipSlot()) return Result::fail ("Duplicate an arrangement audio clip; a Session slot holds one clip.");
    const auto end = clip->getPosition().getEnd();
    impl->transaction ("Duplicate audio clip");
    auto second = te::duplicateClip (*clip);
    if (! second) return Result::fail ("The audio clip could not be duplicated.");
    second->setStart (end, false, true); duplicateId = second->itemID.toString(); impl->changed(); return Result::ok();
}
Result Session::moveClip (int t, int c, double start)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->clip (t, c); if (! clip) return Result::fail ("Clip not found.");
    if (! std::isfinite (start) || start < 0) return Result::fail ("Clip start must be zero or greater.");
    impl->transaction ("Move clip"); clip->setStart (te::TimePosition::fromSeconds (start), false, true); impl->changed(); return Result::ok();
}
Result Session::removeClip (int t, int c)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* clip = impl->clip (t, c); if (! clip) return Result::fail ("Clip not found.");
    impl->transaction ("Remove clip"); clip->removeFromParent(); impl->changed(); return Result::ok();
}
Result Session::renameTrack (int index, const String& name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (name.trim().isEmpty()) return Result::fail ("Enter a track name.");
    impl->transaction ("Rename track"); t->setName (name.trim()); impl->changed(); return Result::ok();
}
Result Session::setTrackGain (int index, float value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (! std::isfinite (value)) return Result::fail ("Invalid gain.");
    impl->transaction ("Track gain"); t->getVolumePlugin()->setVolumeDb (juce::jlimit (-100.0f, 12.0f, value)); impl->changed(); return Result::ok();
}
Result Session::setTrackPan (int index, float value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (! std::isfinite (value)) return Result::fail ("Invalid pan.");
    impl->transaction ("Track pan"); t->getVolumePlugin()->setPan (juce::jlimit (-1.0f, 1.0f, value)); impl->changed(); return Result::ok();
}
Result Session::setTrackMute (int index, bool value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    impl->transaction ("Track mute");
    t->state.setProperty (te::IDs::mute, value, &impl->edit->getUndoManager());
    impl->refreshRouting(); impl->changed(); return Result::ok();
}
Result Session::setTrackSolo (int index, bool value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    impl->transaction ("Track solo");
    t->state.setProperty (te::IDs::solo, value, &impl->edit->getUndoManager());
    impl->refreshRouting(); impl->changed(); return Result::ok();
}

juce::StringArray Session::inputNames() const
{
    juce::StringArray names;
    for (auto* d : impl->engine.getDeviceManager().getWaveInputDevices()) names.add (d->getName());
    return names;
}
Result Session::setTrackInput (int index, const String& name)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (isReturn (*t)) return Result::fail ("Return tracks cannot receive recording inputs.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before changing inputs.");
    // Enabling a device rebuilds the engine's device list, invalidating its pointers.
    // Do so only for the input explicitly selected by the user, then re-query it.
    for (auto* device : impl->engine.getDeviceManager().getWaveInputDevices())
        if (device->getName() == name)
        {
            if (! device->isEnabled()) device->setEnabled (true);
            break;
        }
    impl->refreshInputs();
    te::InputDeviceInstance* selected = nullptr;
    for (auto* d : impl->edit->getAllInputDevices())
        if (d->getInputDevice().getDeviceType() == te::InputDevice::waveDevice && d->getInputDevice().getName() == name) selected = d;
    if (name.isNotEmpty() && selected == nullptr) return Result::fail ("Enable this input in Audio Settings first.");
    impl->transaction ("Assign input");
    for (auto* d : impl->edit->getAllInputDevices())
        if (d->getTargets().contains (t->itemID)) { [[maybe_unused]] auto r = d->removeTarget (t->itemID, &impl->edit->getUndoManager()); }
    if (selected)
    {
        auto result = selected->setTarget (t->itemID, true, &impl->edit->getUndoManager(), 0);
        if (! result) return Result::fail (result.error());
        selected->setRecordingEnabled (t->itemID, false);
        selected->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::off);
    }
    impl->changed(); return Result::ok();
}
Result Session::setTrackArmed (int index, bool value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (isReturn (*t)) return Result::fail ("Return tracks cannot receive recording inputs.");
    auto* input = impl->input (*t); if (! input) return Result::fail ("Select an audio input before arming this track.");
    input->setRecordingEnabled (t->itemID, value); impl->changed(); return Result::ok();
}
Result Session::setTrackMonitor (int index, MonitorMode mode)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (isReturn (*t)) return Result::fail ("Return tracks cannot receive recording inputs.");
    auto* input = impl->input (*t); if (! input) return Result::fail ("Select an audio input before enabling monitoring.");
    input->getInputDevice().setMonitorMode (static_cast<te::InputDevice::MonitorMode> (mode));
    t->state.setProperty (monitorProperty, static_cast<int> (mode), &impl->edit->getUndoManager());
    impl->edit->restartPlayback(); impl->changed(); return Result::ok();
}
Result Session::play()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio output in Audio Settings.");
    impl->edit->getTransport().play (false); sendChangeMessage(); return Result::ok();
}
void Session::stop()
{
    if (impl->exporting) return;
    impl->cancelLaunches();
    impl->edit->getTransport().stop (false, false); sendChangeMessage();
}
Result Session::record()
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio device in Audio Settings.");
    bool armed = false;
    for (auto* t : te::getAudioTracks (*impl->edit)) if (auto* d = impl->input (*t)) armed |= d->isRecordingEnabled (t->itemID);
    if (! armed) return Result::fail ("Assign an input and arm at least one track before recording.");
    auto r = impl->assets().createDirectory(); if (r.failed()) return r;
    impl->edit->getTransport().record (false);
    if (! impl->edit->getTransport().isRecording()) return Result::fail ("Recording could not start. Check the audio input and microphone permission.");
    impl->changed(); return Result::ok();
}
void Session::seek (double seconds)
{
    if (impl->exporting) return;
    if (std::isfinite (seconds))
    {
        impl->cancelLaunches();
        impl->edit->getTransport().setPosition (te::TimePosition::fromSeconds (juce::jmax (0.0, seconds)));
    }
}
void Session::setTempo (double bpm)
{
    if (impl->exporting) return;
    if (! std::isfinite (bpm)) return;
    impl->transaction ("Tempo"); impl->edit->tempoSequence.getTempo (0)->setBpm (juce::jlimit (20.0, 999.0, bpm)); impl->changed();
}
void Session::setLoop (bool enabled, double start, double end)
{
    if (impl->exporting) return;
    if (! std::isfinite (start) || ! std::isfinite (end) || start < 0 || end <= start) return;
    auto& t = impl->edit->getTransport();
    impl->transaction ("Loop range");
    auto* undo = &impl->edit->getUndoManager();
    // Transport defaults to non-undoable navigation. User-edited loop bounds
    // are document edits, so supply the edit's undo manager explicitly.
    t.loopPoint1.setValue (te::TimePosition::fromSeconds (start), undo);
    t.loopPoint2.setValue (te::TimePosition::fromSeconds (end), undo);
    t.looping.setValue (enabled, undo);
    impl->changed();
}
void Session::beginEditGesture (const String& name)
{
    if (impl->exporting) return;
    endEditGesture();
    impl->edit->getUndoManager().beginNewTransaction (name);
    impl->gesture = std::make_unique<te::Edit::UndoTransactionInhibitor> (*impl->edit);
}
void Session::endEditGesture()
{
    if (impl->exporting) return;
    impl->flushStructuralUpdates();
    if (! impl->gesture) return;
    impl->gesture.reset();
    impl->edit->getUndoManager().beginNewTransaction();
}
void Session::undo()
{
    if (impl->exporting) return;
    endEditGesture(); closePluginEditors(); impl->edit->getUndoManager().undo(); impl->refreshParameterValues(); sendChangeMessage();
}
void Session::redo()
{
    if (impl->exporting) return;
    endEditGesture(); closePluginEditors(); impl->edit->getUndoManager().redo(); impl->refreshParameterValues(); sendChangeMessage();
}
bool Session::canUndo() const { return ! impl->exporting && impl->edit->getUndoManager().canUndo(); }
bool Session::canRedo() const { return ! impl->exporting && impl->edit->getUndoManager().canRedo(); }

Result Session::exportWav (const File& file, double rate, int bits, double tailSeconds)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before exporting.");
    if (rate < 8000 || rate > 384000 || ! std::isfinite (rate) || (bits != 16 && bits != 24 && bits != 32)) return Result::fail ("Unsupported export format.");
    if (! std::isfinite (tailSeconds) || tailSeconds < 0 || tailSeconds > 60) return Result::fail ("Export tail must be between 0 and 60 seconds.");
    endEditGesture();
    double end = 0;
    for (auto* t : te::getAudioTracks (*impl->edit)) for (auto* c : t->getClips()) end = juce::jmax (end, c->getPosition().time.getEnd().inSeconds());
    if (end <= 0) return Result::fail ("Add audio or MIDI clips before exporting.");
    end += tailSeconds;
    auto r = file.getParentDirectory().createDirectory(); if (r.failed()) return r;
    juce::TemporaryFile output (file);
    // Native menu callbacks still dispatch during a modal progress window. Hold
    // this guard until the worker and render scopes have completely released the
    // edit; a reentrant New, Undo or Export must never mutate their graph.
    const juce::ScopedValueSetter<bool> exporting (impl->exporting, true);
    impl->edit->dispatchPendingUpdatesSynchronously();
    // Nesting is supported by the engine. This outer scope drains playback
    // before resetting owned control queues and restores it only after export.
    const te::Edit::ScopedRenderStatus renderStatus (*impl->edit, true);
    for (auto* track : te::getAudioTracks (*impl->edit))
    {
        if (auto* synth = Impl::synth (*track)) synth->prepareForOfflineRender();
        for (auto* plugin : track->pluginList.getPlugins())
        {
            if (auto* utility = dynamic_cast<UtilityPlugin*> (plugin)) utility->prepareForOfflineRender();
            if (auto* effect = dynamic_cast<NativeEffectPlugin*> (plugin)) effect->prepareForOfflineRender();
        }
    }
    te::Renderer::Parameters p (*impl->edit);
    p.destFile = output.getFile(); p.audioFormat = impl->engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = bits; p.sampleRateForAudio = rate; p.blockSizeForAudio = 512;
    p.time = { te::TimePosition::fromSeconds (0), te::TimePosition::fromSeconds (end) };
    p.tracksToDo = te::toBitSet (te::getAllTracks (*impl->edit));
    for (auto* track : te::getAudioTracks (*impl->edit))
        for (auto* clip : track->getClips()) p.allowedClips.add (clip);
    p.usePlugins = true; p.useMasterPlugins = true; p.canRenderInMono = false;
    p.checkNodesForAudio = false; p.ditheringEnabled = false;
    auto& ui = static_cast<UIBehaviour&> (impl->engine.getUIBehaviour());
    ui.cancelled = false;
    auto rendered = te::Renderer::renderToFile ("Export Halftrack mix", p);
    if (ui.cancelled) return Result::fail ("Export cancelled.");
    if (! rendered.existsAsFile() || ! output.overwriteTargetFileWithTemporary()) return Result::fail ("The WAV export could not be completed.");
    sendChangeMessage(); return Result::ok();
}

bool Session::isExporting() const { return impl->exporting; }
void Session::cancelExport() { static_cast<UIBehaviour&> (impl->engine.getUIBehaviour()).cancelTask(); }

juce::AudioDeviceManager& Session::audioDeviceManager() { return impl->engine.getDeviceManager().deviceManager; }
juce::KnownPluginList& Session::knownPlugins() { return impl->engine.getPluginManager().knownPluginList; }
juce::AudioPluginFormatManager& Session::pluginFormats() { return impl->engine.getPluginManager().pluginFormatManager; }
File Session::pluginScanDeadMansFile() const
{
    auto folder = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Halftrack");
    folder.createDirectory(); return folder.getChildFile ("plugin-scan-in-progress.txt");
}
Result Session::addNativeEffect (int index, EffectKind kind)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (index); if (! track) return Result::fail ("Track not found.");
    if (effectParameters (kind).empty()) return Result::fail ("Unknown native effect.");
    juce::ValueTree state (te::IDs::PLUGIN);
    state.setProperty (te::IDs::type, NativeEffectPlugin::xmlTypeName, nullptr);
    state.setProperty ("effectKind", static_cast<int> (kind), nullptr);
    auto plugin = impl->edit->getPluginCache().createNewPlugin (state);
    if (! plugin) return Result::fail ("The native effect could not be created.");
    int insertion = track->pluginList.size();
    for (int i = 0; i < track->pluginList.size(); ++i)
        if (dynamic_cast<te::VolumeAndPanPlugin*> (track->pluginList[i]) || dynamic_cast<te::AuxSendPlugin*> (track->pluginList[i]))
        { insertion = i; break; }
    impl->transaction ("Add " + String (effectName (kind)));
    track->pluginList.insertPlugin (plugin, insertion, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setNativeEffectSettings (const String& id, const EffectSettings& settings)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! validEffectSettings (settings)) return Result::fail ("Effect settings are outside their supported ranges.");
    for (auto* track : te::getAudioTracks (*impl->edit))
        for (auto* plugin : track->pluginList.getPlugins())
            if (plugin->itemID.toString() == id)
                if (auto* effect = dynamic_cast<NativeEffectPlugin*> (plugin))
                {
                    const auto previous = effect->readSettings();
                    if (previous.kind != settings.kind) return Result::fail ("Effect type cannot change during an edit.");
                    if (previous.values == settings.values) return Result::ok();
                    impl->transaction ("Edit " + String (effectName (settings.kind)));
                    effect->writeSettings (settings); impl->changed(); return Result::ok();
                }
    return Result::fail ("Native effect not found.");
}
Result Session::addUtility (int index)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* track = impl->track (index); if (! track) return Result::fail ("Track not found.");
    auto plugin = impl->edit->getPluginCache().createNewPlugin (UtilityPlugin::xmlTypeName, {});
    if (! plugin) return Result::fail ("The channel utility could not be created.");
    int insertion = track->pluginList.size();
    for (int i = 0; i < track->pluginList.size(); ++i)
        if (dynamic_cast<te::VolumeAndPanPlugin*> (track->pluginList[i]) || dynamic_cast<te::AuxSendPlugin*> (track->pluginList[i]))
        { insertion = i; break; }
    impl->transaction ("Add channel utility");
    track->pluginList.insertPlugin (plugin, insertion, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setUtilitySettings (const String& id, const UtilitySettings& settings)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    if (! UtilityPlugin::validSettings (settings)) return Result::fail ("Channel utility settings are outside their supported ranges.");
    for (auto* track : te::getAudioTracks (*impl->edit))
        for (auto* plugin : track->pluginList.getPlugins())
            if (plugin->itemID.toString() == id)
                if (auto* utility = dynamic_cast<UtilityPlugin*> (plugin))
                {
                    if (utility->readSettings() == settings) return Result::ok();
                    impl->transaction ("Edit channel utility"); utility->writeSettings (settings);
                    impl->changed(); return Result::ok();
                }
    return Result::fail ("Channel utility not found.");
}
Result Session::movePlugin (int t, int p, int destination)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto list = impl->plugins (t);
    if (! juce::isPositiveAndBelow (p, list.size()) || ! juce::isPositiveAndBelow (destination, list.size()))
        return Result::fail ("Device position not found.");
    if (p == destination) return Result::ok();
    auto state = list[p]->state; auto parent = state.getParent();
    if (parent != list[destination]->state.getParent()) return Result::fail ("Devices must be on the same track.");
    impl->transaction ("Move device");
    parent.moveChild (parent.indexOf (state), parent.indexOf (list[destination]->state), &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::addPlugin (int index, const juce::PluginDescription& description)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    impl->transaction ("Add plug-in");
    // Tracktion resolves its persisted plug-in identity through this registry.
    knownPlugins().addType (description);
    auto plugin = impl->edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, description);
    if (! plugin) return Result::fail ("The plug-in could not be loaded.");
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get());
    if (! external || (! external->getAudioPluginInstance() && ! external->isInitialisingAsync()))
        return Result::fail (external ? external->getLoadError() : "The plug-in could not be instantiated.");
    const auto insertIndex = (isReturn (*t) || Impl::synth (*t)) ? 1 : 0;
    t->pluginList.insertPlugin (plugin, insertIndex, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setPluginEnabled (int t, int p, bool enabled)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* plugin = impl->plugin (t, p); if (! plugin) return Result::fail ("Plug-in not found.");
    impl->transaction ("Bypass plug-in"); plugin->setEnabled (enabled); impl->changed(); return Result::ok();
}
Result Session::removePlugin (int t, int p)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* plugin = impl->plugin (t, p); if (! plugin) return Result::fail ("Plug-in not found.");
    plugin->windowState->closeWindowExplicitly(); impl->transaction ("Remove plug-in"); plugin->deleteFromParent(); impl->changed(); return Result::ok();
}
Result Session::setPluginParameter (int t, int p, int parameter, float value)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* plugin = impl->plugin (t, p);
    if (! plugin || ! std::isfinite (value)) return Result::fail ("Plug-in parameter not found.");
    auto* processor = plugin->getWrappedAudioProcessor();
    if (! processor || ! juce::isPositiveAndBelow (parameter, processor->getParameters().size()))
        return Result::fail ("Plug-in parameter not found.");
    auto* param = processor->getParameters()[parameter];
    param->beginChangeGesture(); param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value)); param->endChangeGesture();
    impl->edit->pluginChanged (*plugin); impl->changed(); return Result::ok();
}
Result Session::showPluginEditor (int t, int p)
{
    if (impl->exporting) return Result::fail ("Finish or cancel the current export first.");
    auto* plugin = impl->plugin (t, p); if (! plugin) return Result::fail ("Plug-in not found.");
    if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin))
    {
        if (external->isInitialisingAsync())
            return Result::fail ("This plug-in is still loading. Try opening its editor again when loading completes.");
        if (external->getAudioPluginInstance() == nullptr)
        {
            const auto error = external->getLoadError();
            return Result::fail (error.isNotEmpty() ? error : "This plug-in could not be loaded.");
        }
    }
    plugin->windowState->showWindowExplicitly();
    auto* window = dynamic_cast<PluginWindow*> (plugin->windowState->pluginWindow.get());
    if (window == nullptr || ! window->isVisible() || window->getContentComponent() == nullptr)
        return Result::fail ("This plug-in has no available editor.");
    return Result::ok();
}
void Session::closePluginEditors()
{
    if (impl->exporting) return;
    if (impl && impl->edit)
        for (auto* t : te::getAudioTracks (*impl->edit)) for (auto* p : t->pluginList.getPlugins()) p->windowState->closeWindowExplicitly();
}
}
