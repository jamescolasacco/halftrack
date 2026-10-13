#include "Session.h"
#include "SubtractiveSynth.h"
#include <tracktion_engine/tracktion_engine.h>
#include <map>

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

class Behaviour final : public te::EngineBehaviour
{
public:
    explicit Behaviour (bool devices) : useDevices (devices) {}
    bool autoInitialiseDeviceManager() override { return useDevices; }
    bool shouldOpenAudioInputByDefault() override { return false; }
    bool addSystemAudioIODeviceTypes() override { return useDevices; }
    bool canScanPluginsOutOfProcess() override { return useDevices; }
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
    explicit PluginWindow (te::Plugin& p)
        : DocumentWindow (p.getName(), juce::Colour (0xff262724), closeButton), plugin (p)
    {
        if (auto external = dynamic_cast<te::ExternalPlugin*> (&p))
            if (auto processor = external->getAudioPluginInstance())
            {
                auto* editor = processor->createEditorAndMakeActive();
                if (editor == nullptr)
                    editor = new juce::GenericAudioProcessorEditor (*processor);
                setContentOwned (editor, true);
                setResizable (editor->isResizable(), false);
            }
        setUsingNativeTitleBar (true);
        centreWithSize (juce::jmax (getWidth(), 320), juce::jmax (getHeight(), 200));
        setVisible (true);
    }
    ~PluginWindow() override { clearContentComponent(); }
    void closeButtonPressed() override { plugin.windowState->closeWindowExplicitly(); }
private:
    te::Plugin& plugin;
};

class UIBehaviour final : public te::UIBehaviour
{
public:
    explicit UIBehaviour (bool showProgress) : showProgressWindow (showProgress) {}
    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& state) override
    {
        if (auto* p = dynamic_cast<te::Plugin::WindowState*> (&state))
            return std::make_unique<PluginWindow> (p->plugin);
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
        : owner (s), engine (devices ? "Halftrack" : "Halftrack Tests", std::make_unique<UIBehaviour> (devices),
                            std::make_unique<Behaviour> (devices)), useDevices (devices)
    {
        engine.getPluginManager().createBuiltInType<SubtractiveSynthPlugin>();
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
    te::MidiClip* midiClip (const String& id) const
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* c : t->getClips()) if (c->itemID.toString() == id) return dynamic_cast<te::MidiClip*> (c);
        return nullptr;
    }
    static SubtractiveSynthPlugin* synth (te::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList.getPlugins())
            if (auto* instrument = dynamic_cast<SubtractiveSynthPlugin*> (plugin)) return instrument;
        return nullptr;
    }
    static te::MidiNote* note (te::MidiClip& clip, const String& id)
    {
        for (auto* n : clip.getSequence().getNotes()) if (n->state.getProperty (midiNoteIDProperty).toString() == id) return n;
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
                if (dynamic_cast<te::ExternalPlugin*> (p) != nullptr) result.add (p);
        return result;
    }
    te::Plugin* plugin (int trackIndex, int pluginIndex) const
    {
        auto list = plugins (trackIndex);
        return juce::isPositiveAndBelow (pluginIndex, list.size()) ? list[pluginIndex] : nullptr;
    }
    File editLocation() const { return projectFile == File() ? scratch.getChildFile ("Untitled.halftrack") : projectFile; }
    File assets() const { auto f = editLocation(); return f.getSiblingFile (f.getFileNameWithoutExtension() + " Assets"); }
    void changed()
    {
        // Tracktion resets initialization changes on a deferred timer. Keep an
        // independent flag so that timer cannot clear a real user edit.
        explicitChanges = true;
        pristineNewDocument = false;
        edit->markAsChanged();
        owner.sendChangeMessage();
    }
    void transaction (const String& name) { if (! gesture) edit->getUndoManager().beginNewTransaction (name); }
    void refreshParameterValues()
    {
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* p : t->pluginList.getPlugins())
                for (auto* parameter : p->getAutomatableParameters()) parameter->updateFromAttachedValue();
        for (auto* parameter : edit->getMasterVolumePlugin()->getAutomatableParameters()) parameter->updateFromAttachedValue();
        edit->dispatchPendingUpdatesSynchronously();
    }
    void prepareEdit()
    {
        edit->editFileRetriever = [this] { return editLocation(); };
        edit->playInStopEnabled = true;
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
    bool explicitChanges = false, pristineNewDocument = false;
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
    int index = 0;
    for (auto* t : te::getAudioTracks (e))
    {
        TrackInfo info;
        info.id = t->itemID.toString(); info.name = t->getName(); info.colour = t->getColour();
        info.mute = t->isMuted (false); info.solo = t->isSolo (false);
        info.isMidi = t->state.getProperty (midiTrackProperty, false);
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
        for (auto* c : t->getClips())
        {
            auto pos = c->getPosition();
            ClipInfo clipInfo;
            clipInfo.id = c->itemID.toString(); clipInfo.name = c->getName();
            clipInfo.start = pos.time.getStart().inSeconds(); clipInfo.length = pos.time.getLength().inSeconds();
            clipInfo.sourceOffset = pos.offset.inSeconds();
            if (c->usesSourceFile()) clipInfo.file = c->getSourceFileReference().getFile();
            if (auto* midi = dynamic_cast<te::MidiClip*> (c))
            {
                clipInfo.isMidi = true;
                const auto beats = e.tempoSequence.toBeats (pos.time);
                clipInfo.startBeat = beats.getStart().inBeats(); clipInfo.lengthBeats = beats.getLength().inBeats();
                for (auto* note : midi->getSequence().getNotes())
                    clipInfo.notes.push_back ({ note->state.getProperty (midiNoteIDProperty).toString(), note->getNoteNumber(),
                                               note->getStartBeat().inBeats(), note->getLengthBeats().inBeats(), note->getVelocity() / 127.0f });
            }
            info.clips.push_back (std::move (clipInfo));
        }
        for (auto* p : impl->plugins (index++)) info.plugins.push_back ({ p->itemID.toString(), p->getName(), p->isEnabled() });
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
    impl->prepareEdit();
    impl->pristineNewDocument = true;
    return Result::ok();
}

Result Session::loadProject (const File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (! xml || ! xml->hasTagName ("HALFTRACK") || xml->getIntAttribute ("version") != 1)
        return Result::fail ("This is not a supported Halftrack project.");
    auto* editXml = xml->getChildByName ("EDIT");
    if (! editXml) return Result::fail ("The project has no session data.");
    auto state = juce::ValueTree::fromXml (*editXml);
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
        for (auto* c : t->getClips())
            if (c->usesSourceFile() && ! c->getSourceFileReference().getFile().existsAsFile())
                return Result::fail ("Missing audio file: " + c->getSourceFileReference().getFile().getFullPathName());
    endEditGesture(); closePluginEditors(); stop(); impl->meters.clear();
    impl->projectFile = file;
    impl->pristineNewDocument = false;
    impl->edit = std::move (candidate);
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
        for (auto* c : t->getClips())
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
        clip->getSourceFileReference().setToFile (source, te::SourceFileReference::PathStyle::alwaysRelative, false);
    impl->edit->resetChangedStatus();
    impl->explicitChanges = false;
    sendChangeMessage();
    return Result::ok();
}

Result Session::addAudioTrack (String name)
{
    impl->transaction ("Add audio track");
    const auto count = te::getAudioTracks (*impl->edit).size();
    impl->edit->ensureNumberOfAudioTracks (count + 1);
    auto* t = impl->track (count);
    if (! t) return Result::fail ("Could not create an audio track.");
    t->setName (name.isEmpty() ? "Audio " + String (count + 1) : name);
    const juce::uint32 colours[] = { 0xffd0a35e, 0xff86a395, 0xff899bbb, 0xffb98579, 0xffaaa179, 0xffa58aaa };
    t->setColour (juce::Colour (colours[count % 6]));
    t->getVolumePlugin()->setVolumeDb (0);
    impl->changed();
    return Result::ok();
}
Result Session::addMidiTrack (String name)
{
    impl->transaction ("Add MIDI track");
    const auto count = te::getAudioTracks (*impl->edit).size();
    impl->edit->ensureNumberOfAudioTracks (count + 1);
    auto* track = impl->track (count);
    if (! track) return Result::fail ("Could not create a MIDI track.");
    track->setName (name.isEmpty() ? "MIDI " + String (count + 1) : name);
    track->setColour (juce::Colour (0xff9d91bc));
    track->state.setProperty (midiTrackProperty, true, &impl->edit->getUndoManager());
    track->getVolumePlugin()->setVolumeDb (0);
    auto synth = impl->edit->getPluginCache().createNewPlugin (SubtractiveSynthPlugin::xmlTypeName, {});
    if (! synth) return Result::fail ("Could not create the built-in synthesizer.");
    track->pluginList.insertPlugin (synth, 0, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::addMidiClip (const String& trackId, double start, double length)
{
    auto* track = impl->track (trackId);
    if (! track || ! static_cast<bool> (track->state.getProperty (midiTrackProperty))) return Result::fail ("Select a MIDI track first.");
    if (! std::isfinite (start) || ! std::isfinite (length) || start < 0 || length <= 0 || start + length > 1000000)
        return Result::fail ("Enter a positive MIDI clip duration and a valid start beat.");
    impl->transaction ("Add MIDI clip");
    const auto time = impl->edit->tempoSequence.toTime (te::BeatRange { te::BeatPosition::fromBeats (start), te::BeatPosition::fromBeats (start + length) });
    auto clip = track->insertMIDIClip ("MIDI clip", time, nullptr);
    if (! clip) return Result::fail ("Could not create the MIDI clip.");
    clip->setSyncType (te::Clip::syncBarsBeats);
    impl->changed(); return Result::ok();
}
Result Session::addMidiNote (const String& clipId, int pitch, double start, double length, float velocity)
{
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    auto valid = impl->validateNote (*clip, pitch, start, length, velocity); if (valid.failed()) return valid;
    if (clip->getSequence().getNumNotes() >= 100000) return Result::fail ("The MIDI clip has reached its note limit.");
    impl->transaction ("Add MIDI note");
    auto* undo = &impl->edit->getUndoManager();
    auto* note = clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (start), te::BeatDuration::fromBeats (length),
                                             juce::jlimit (1, 127, juce::roundToInt (velocity * 127)), 0, undo);
    if (! note) return Result::fail ("Could not add the MIDI note.");
    note->state.setProperty (midiNoteIDProperty, juce::Uuid().toString(), undo);
    impl->changed(); return Result::ok();
}
Result Session::updateMidiNote (const String& clipId, const String& noteId, int pitch, double start, double length, float velocity)
{
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    auto* note = Impl::note (*clip, noteId); if (! note) return Result::fail ("MIDI note not found.");
    auto valid = impl->validateNote (*clip, pitch, start, length, velocity); if (valid.failed()) return valid;
    impl->transaction ("Edit MIDI note");
    auto* undo = &impl->edit->getUndoManager();
    note->setNoteNumber (pitch, undo);
    note->setStartAndLength (te::BeatPosition::fromBeats (start), te::BeatDuration::fromBeats (length), undo);
    note->setVelocity (juce::jlimit (1, 127, juce::roundToInt (velocity * 127)), undo);
    impl->changed(); return Result::ok();
}
Result Session::removeMidiNote (const String& clipId, const String& noteId)
{
    auto* clip = impl->midiClip (clipId); if (! clip) return Result::fail ("MIDI clip not found.");
    auto* note = Impl::note (*clip, noteId); if (! note) return Result::fail ("MIDI note not found.");
    impl->transaction ("Remove MIDI note");
    clip->getSequence().removeNote (*note, &impl->edit->getUndoManager());
    impl->changed(); return Result::ok();
}
Result Session::setSynthSettings (const String& trackId, const SynthSettings& settings)
{
    auto* track = impl->track (trackId); auto* synth = track ? Impl::synth (*track) : nullptr;
    if (! synth) return Result::fail ("Built-in synthesizer not found.");
    auto valid = [] (float value, float low, float high) { return std::isfinite (value) && value >= low && value <= high; };
    if (! valid (settings.cutoffHz, 20, 20000) || ! valid (settings.resonance, 0, 1)
        || ! valid (settings.attackSeconds, 0, 10) || ! valid (settings.decaySeconds, 0, 10)
        || ! valid (settings.sustain, 0, 1) || ! valid (settings.releaseSeconds, 0, 20)
        || ! valid (settings.level, 0, 1) || settings.waveform < 0 || settings.waveform > 1)
        return Result::fail ("Synthesizer settings are outside their supported range.");
    impl->transaction ("Synthesizer settings"); synth->setSettings (settings); impl->changed(); return Result::ok();
}
Result Session::removeTrack (int index)
{
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before removing tracks.");
    closePluginEditors(); impl->meters.clear(); impl->transaction ("Remove track");
    impl->edit->deleteTrack (t); impl->changed(); return Result::ok();
}
Result Session::importAudio (const File& source, int index, double start)
{
    if (! source.existsAsFile()) return Result::fail ("The audio file does not exist.");
    te::AudioFile audio (impl->engine, source);
    if (! audio.isValid() || audio.getLength() <= 0) return Result::fail ("This audio file could not be read.");
    if (! std::isfinite (start) || start < 0) return Result::fail ("Clip start must be zero or greater.");
    if (index < 0) { auto r = addAudioTrack (source.getFileNameWithoutExtension()); if (r.failed()) return r; index = te::getAudioTracks (*impl->edit).size() - 1; }
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (static_cast<bool> (t->state.getProperty (midiTrackProperty))) return Result::fail ("Import audio onto an audio track.");
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
Result Session::moveClip (int t, int c, double start)
{
    auto* clip = impl->clip (t, c); if (! clip) return Result::fail ("Clip not found.");
    if (! std::isfinite (start) || start < 0) return Result::fail ("Clip start must be zero or greater.");
    impl->transaction ("Move clip"); clip->setStart (te::TimePosition::fromSeconds (start), false, true); impl->changed(); return Result::ok();
}
Result Session::removeClip (int t, int c)
{
    auto* clip = impl->clip (t, c); if (! clip) return Result::fail ("Clip not found.");
    impl->transaction ("Remove clip"); clip->removeFromParent(); impl->changed(); return Result::ok();
}
Result Session::renameTrack (int index, const String& name)
{
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (name.trim().isEmpty()) return Result::fail ("Enter a track name.");
    impl->transaction ("Rename track"); t->setName (name.trim()); impl->changed(); return Result::ok();
}
Result Session::setTrackGain (int index, float value)
{
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (! std::isfinite (value)) return Result::fail ("Invalid gain.");
    impl->transaction ("Track gain"); t->getVolumePlugin()->setVolumeDb (juce::jlimit (-100.0f, 12.0f, value)); impl->changed(); return Result::ok();
}
Result Session::setTrackPan (int index, float value)
{
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    if (! std::isfinite (value)) return Result::fail ("Invalid pan.");
    impl->transaction ("Track pan"); t->getVolumePlugin()->setPan (juce::jlimit (-1.0f, 1.0f, value)); impl->changed(); return Result::ok();
}
Result Session::setTrackMute (int index, bool value)
{
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    impl->transaction ("Track mute"); t->setMute (value); impl->changed(); return Result::ok();
}
Result Session::setTrackSolo (int index, bool value)
{
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    impl->transaction ("Track solo"); t->setSolo (value); impl->changed(); return Result::ok();
}

juce::StringArray Session::inputNames() const
{
    juce::StringArray names;
    for (auto* d : impl->engine.getDeviceManager().getWaveInputDevices()) names.add (d->getName());
    return names;
}
Result Session::setTrackInput (int index, const String& name)
{
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
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
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    auto* input = impl->input (*t); if (! input) return Result::fail ("Select an audio input before arming this track.");
    input->setRecordingEnabled (t->itemID, value); impl->changed(); return Result::ok();
}
Result Session::setTrackMonitor (int index, MonitorMode mode)
{
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    auto* input = impl->input (*t); if (! input) return Result::fail ("Select an audio input before enabling monitoring.");
    input->getInputDevice().setMonitorMode (static_cast<te::InputDevice::MonitorMode> (mode));
    t->state.setProperty (monitorProperty, static_cast<int> (mode), &impl->edit->getUndoManager());
    impl->edit->restartPlayback(); impl->changed(); return Result::ok();
}
Result Session::play()
{
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio output in Audio Settings.");
    impl->edit->getTransport().play (false); sendChangeMessage(); return Result::ok();
}
void Session::stop() { impl->edit->getTransport().stop (false, false); sendChangeMessage(); }
Result Session::record()
{
    if (! audioDeviceManager().getCurrentAudioDevice()) return Result::fail ("Choose an audio device in Audio Settings.");
    bool armed = false;
    for (auto* t : te::getAudioTracks (*impl->edit)) if (auto* d = impl->input (*t)) armed |= d->isRecordingEnabled (t->itemID);
    if (! armed) return Result::fail ("Assign an input and arm at least one track before recording.");
    auto r = impl->assets().createDirectory(); if (r.failed()) return r;
    impl->edit->getTransport().record (false);
    if (! impl->edit->getTransport().isRecording()) return Result::fail ("Recording could not start. Check the audio input and microphone permission.");
    impl->changed(); return Result::ok();
}
void Session::seek (double seconds) { if (std::isfinite (seconds)) impl->edit->getTransport().setPosition (te::TimePosition::fromSeconds (juce::jmax (0.0, seconds))); }
void Session::setTempo (double bpm)
{
    if (! std::isfinite (bpm)) return;
    impl->transaction ("Tempo"); impl->edit->tempoSequence.getTempo (0)->setBpm (juce::jlimit (20.0, 999.0, bpm)); impl->changed();
}
void Session::setLoop (bool enabled, double start, double end)
{
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
    endEditGesture();
    impl->edit->getUndoManager().beginNewTransaction (name);
    impl->gesture = std::make_unique<te::Edit::UndoTransactionInhibitor> (*impl->edit);
}
void Session::endEditGesture()
{
    if (! impl->gesture) return;
    impl->gesture.reset();
    impl->edit->getUndoManager().beginNewTransaction();
}
void Session::undo() { endEditGesture(); closePluginEditors(); impl->edit->getUndoManager().undo(); impl->refreshParameterValues(); sendChangeMessage(); }
void Session::redo() { endEditGesture(); closePluginEditors(); impl->edit->getUndoManager().redo(); impl->refreshParameterValues(); sendChangeMessage(); }
bool Session::canUndo() const { return impl->edit->getUndoManager().canUndo(); }
bool Session::canRedo() const { return impl->edit->getUndoManager().canRedo(); }

Result Session::exportWav (const File& file, double rate, int bits, double tailSeconds)
{
    if (impl->edit->getTransport().isRecording()) return Result::fail ("Stop recording before exporting.");
    if (rate < 8000 || rate > 384000 || ! std::isfinite (rate) || (bits != 16 && bits != 24 && bits != 32)) return Result::fail ("Unsupported export format.");
    if (! std::isfinite (tailSeconds) || tailSeconds < 0 || tailSeconds > 60) return Result::fail ("Export tail must be between 0 and 60 seconds.");
    endEditGesture();
    double end = 0;
    for (auto* t : te::getAudioTracks (*impl->edit)) for (auto* c : t->getClips()) end = juce::jmax (end, c->getPosition().time.getEnd().inSeconds());
    if (end <= 0) return Result::fail ("Import or record audio before exporting.");
    end += tailSeconds;
    auto r = file.getParentDirectory().createDirectory(); if (r.failed()) return r;
    juce::TemporaryFile output (file);
    impl->edit->dispatchPendingUpdatesSynchronously();
    // Nesting is supported by the engine. This outer scope drains playback
    // before resetting owned control queues and restores it only after export.
    const te::Edit::ScopedRenderStatus renderStatus (*impl->edit, true);
    for (auto* track : te::getAudioTracks (*impl->edit))
        if (auto* synth = Impl::synth (*track)) synth->prepareForOfflineRender();
    te::Renderer::Parameters p (*impl->edit);
    p.destFile = output.getFile(); p.audioFormat = impl->engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = bits; p.sampleRateForAudio = rate; p.blockSizeForAudio = 512;
    p.time = { te::TimePosition::fromSeconds (0), te::TimePosition::fromSeconds (end) };
    p.tracksToDo = te::toBitSet (te::getAllTracks (*impl->edit));
    p.usePlugins = true; p.useMasterPlugins = true; p.canRenderInMono = false;
    p.checkNodesForAudio = false; p.ditheringEnabled = false;
    auto& ui = static_cast<UIBehaviour&> (impl->engine.getUIBehaviour());
    ui.cancelled = false;
    auto rendered = te::Renderer::renderToFile ("Export Halftrack mix", p);
    if (ui.cancelled) return Result::fail ("Export cancelled.");
    if (! rendered.existsAsFile() || ! output.overwriteTargetFileWithTemporary()) return Result::fail ("The WAV export could not be completed.");
    sendChangeMessage(); return Result::ok();
}

void Session::cancelExport() { static_cast<UIBehaviour&> (impl->engine.getUIBehaviour()).cancelTask(); }

juce::AudioDeviceManager& Session::audioDeviceManager() { return impl->engine.getDeviceManager().deviceManager; }
juce::KnownPluginList& Session::knownPlugins() { return impl->engine.getPluginManager().knownPluginList; }
juce::AudioPluginFormatManager& Session::pluginFormats() { return impl->engine.getPluginManager().pluginFormatManager; }
File Session::pluginScanDeadMansFile() const
{
    auto folder = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Halftrack");
    folder.createDirectory(); return folder.getChildFile ("plugin-scan-in-progress.txt");
}
Result Session::addPlugin (int index, const juce::PluginDescription& description)
{
    auto* t = impl->track (index); if (! t) return Result::fail ("Track not found.");
    impl->transaction ("Add plug-in");
    // Tracktion resolves its persisted plug-in identity through this registry.
    knownPlugins().addType (description);
    auto plugin = impl->edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, description);
    if (! plugin) return Result::fail ("The plug-in could not be loaded.");
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get());
    if (! external || (! external->getAudioPluginInstance() && ! external->isInitialisingAsync()))
        return Result::fail (external ? external->getLoadError() : "The plug-in could not be instantiated.");
    const auto insertIndex = Impl::synth (*t) ? 1 : 0;
    t->pluginList.insertPlugin (plugin, insertIndex, nullptr);
    impl->changed(); return Result::ok();
}
Result Session::setPluginEnabled (int t, int p, bool enabled)
{
    auto* plugin = impl->plugin (t, p); if (! plugin) return Result::fail ("Plug-in not found.");
    impl->transaction ("Bypass plug-in"); plugin->setEnabled (enabled); impl->changed(); return Result::ok();
}
Result Session::removePlugin (int t, int p)
{
    auto* plugin = impl->plugin (t, p); if (! plugin) return Result::fail ("Plug-in not found.");
    plugin->windowState->closeWindowExplicitly(); impl->transaction ("Remove plug-in"); plugin->deleteFromParent(); impl->changed(); return Result::ok();
}
Result Session::setPluginParameter (int t, int p, int parameter, float value)
{
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
    auto* plugin = impl->plugin (t, p); if (! plugin) return Result::fail ("Plug-in not found.");
    plugin->windowState->showWindowExplicitly();
    if (! plugin->windowState->isWindowShowing()) return Result::fail ("This plug-in has no available editor.");
    return Result::ok();
}
void Session::closePluginEditors()
{
    if (impl && impl->edit)
        for (auto* t : te::getAudioTracks (*impl->edit)) for (auto* p : t->pluginList.getPlugins()) p->windowState->closeWindowExplicitly();
}
}
