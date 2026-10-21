#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace halftrack
{
namespace
{
namespace te = tracktion;
void check (bool condition, const juce::String& name)
{
    if (! condition) throw std::runtime_error (name.toStdString());
    std::cout << "PASS launcher " << name << std::endl;
}
void ok (const juce::Result& result, const juce::String& name)
{
    check (result.wasOk(), name + (result.failed() ? ": " + result.getErrorMessage() : ""));
}
te::Engine& engineFor (Session& session)
{
    for (auto* engine : te::Engine::getEngines())
        if (&engine->getDeviceManager().deviceManager == &session.audioDeviceManager()) return *engine;
    throw std::runtime_error ("session engine not found");
}
void dispatchGraphChanges (Session& session)
{
    // Accelerated callbacks do not run the desktop event loop. Flush the same
    // engine edit update that its message-thread timer normally dispatches.
    for (auto* edit : engineFor (session).getActiveEdits().getEdits())
        edit->dispatchPendingUpdatesSynchronously();
}
juce::File constantFile (const juce::File& root, const juce::String& name, float value, int numSamples = 12000)
{
    const auto file = root.getChildFile (name + ".wav");
    juce::AudioBuffer<float> buffer (2, numSamples);
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (buffer.getWritePointer (c), value, numSamples);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    check (writer && writer->writeFromAudioSampleBuffer (buffer, 0, numSamples), "write generated source");
    return file;
}
using MemoryBuffers = std::vector<std::unique_ptr<choc::buffer::InterleavedBuffer<float>>>;
void registerConstant (MemoryBuffers& buffers, te::Engine& engine, const juce::File& file, float value, unsigned int numSamples = 12000)
{
    // Keep disk fixtures for collection/save validation, but use synchronous
    // readers for timing: accelerated callbacks must not test disk-cache waits.
    auto buffer = std::make_unique<choc::buffer::InterleavedBuffer<float>> (2, numSamples);
    for (unsigned int c = 0; c < 2; ++c)
        for (unsigned int f = 0; f < numSamples; ++f) buffer->getSample (c, f) = value;
    engine.getAudioFileManager().registerMemoryBuffer (file.getFullPathName().toStdString(), buffer->getView(), 48000);
    buffers.push_back (std::move (buffer));
}
void checkFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getSample (c, i)))
                throw std::runtime_error ("non-finite captured audio");
}
float magnitude (const juce::AudioBuffer<float>& buffer, int start, int length)
{
    check (start >= 0 && length > 0 && start + length <= buffer.getNumSamples(), "measurement window is inside captured output");
    checkFinite (buffer);
    return buffer.getMagnitude (0, start, length);
}
int onset (const juce::AudioBuffer<float>& buffer, int channel = 0)
{
    checkFinite (buffer);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (std::abs (buffer.getSample (channel, i)) > 0.001f) return i;
    return -1;
}
void level (const juce::AudioBuffer<float>& buffer, int start, int length, float expected, const juce::String& name)
{
    check (start >= 0 && start + length <= buffer.getNumSamples(), name + " bounds");
    float error = 0;
    checkFinite (buffer);
    for (int i = start; i < start + length; ++i) error = std::max (error, std::abs (buffer.getSample (0, i) - expected));
    check (error < 0.00001f, name + " max error " + juce::String (error, 8));
}
te::HostedAudioDeviceInterface::Parameters parameters()
{
    te::HostedAudioDeviceInterface::Parameters p;
    p.sampleRate = 48000; p.blockSize = 128; p.inputChannels = 0; p.outputChannels = 2; p.useMidiDevices = false;
    return p;
}
void structureTests (const juce::File& root, const juce::File& audio)
{
    Session session (false);
    auto initial = session.snapshot();
    check (initial.scenes.size() == 8 && initial.tracks[0].slots.size() == 8 && ! initial.dirty && ! session.canUndo(), "new project has eight clean scenes");
    ok (session.addScene ("Bridge"), "add scene");
    auto sceneId = session.snapshot().scenes.back().id;
    session.undo(); check (session.snapshot().scenes.size() == 8, "undo adds no orphan slots");
    session.redo(); check (session.snapshot().scenes.back().id == sceneId && session.snapshot().tracks[1].slots.size() == 9, "redo restores scene and slots");
    ok (session.renameScene (8, "Outro"), "rename scene");
    session.undo(); check (session.snapshot().scenes[8].name == "Bridge", "undo scene name"); session.redo();
    ok (session.importAudioToSlot (audio, 0, 0), "import audio into native slot");
    const auto audioId = session.snapshot().tracks[0].slots[0].clip.id;
    check (session.snapshot().tracks[0].clips.empty(), "slot import does not create arrangement clips");
    check (session.importAudioToSlot (audio, 0, 0).failed(), "occupied slot rejects implicit replacement");
    ok (session.clearSlot (0, 0), "clear slot"); session.undo();
    check (session.snapshot().tracks[0].slots[0].clip.id == audioId, "undo clear restores clip identity");
    ok (session.removeScene (0), "remove occupied scene"); session.undo();
    check (session.snapshot().tracks[0].slots[0].clip.id == audioId && session.snapshot().scenes.size() == 9, "undo scene deletion restores audio and row alignment");
    ok (session.addMidiTrack ("Keys"), "add instrument track after scenes");
    auto midiTrack = session.snapshot().tracks.back();
    check (midiTrack.slots.size() == 9, "new instrument has every scene slot");
    ok (session.addMidiClipToSlot (midiTrack.id, 1, 4), "create native MIDI slot");
    const auto midiId = session.snapshot().tracks.back().slots[1].clip.id;
    ok (session.addMidiNote (midiId, 60, 0, 1, 0.8f), "edit slot note through existing facade");
    auto noteId = session.snapshot().tracks.back().slots[1].clip.notes[0].id;
    ok (session.updateMidiNote (midiId, noteId, 64, 1, 0.5, 0.7f), "update slot note");
    session.undo(); check (session.snapshot().tracks.back().slots[1].clip.notes[0].pitch == 60, "undo slot note edit"); session.redo();
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "set launch quantisation");
    session.undo(); check (session.snapshot().launchQuantisation == Session::LaunchQuantisation::bar, "undo launch quantisation"); session.redo();
    const auto saved = root.getChildFile ("Portable").getChildFile ("Set.halftrack");
    ok (session.saveProject (saved), "save launcher project");
    const auto collected = session.snapshot().tracks[0].slots[0].clip.file;
    check (collected.existsAsFile() && collected.isAChildOf (saved.getParentDirectory()), "collect slot media into project");
    const auto moved = root.getChildFile ("Relocated");
    check (saved.getParentDirectory().copyDirectoryTo (moved), "copy portable project");
    ok (session.newProject(), "reset before portable reopen");
    check (saved.getParentDirectory().deleteRecursively(), "remove original project copy");
    ok (session.loadProject (moved.getChildFile ("Set.halftrack")), "open relocated launcher project");
    auto reopened = session.snapshot();
    check (reopened.scenes.size() == 9 && reopened.scenes.back().id == sceneId && reopened.scenes.back().name == "Outro", "persist scene identities and names");
    check (reopened.tracks[0].slots[0].clip.id == audioId && reopened.tracks[0].slots[0].clip.file.isAChildOf (moved), "persist portable slot media");
    check (reopened.tracks.back().slots[1].clip.id == midiId && reopened.tracks.back().slots[1].clip.notes[0].pitch == 64
           && reopened.launchQuantisation == Session::LaunchQuantisation::beat, "persist slot notes and launch grid");
    auto missing = reopened.tracks[0].slots[0].clip.file;
    auto held = missing.getSiblingFile ("temporarily-hidden.wav");
    check (missing.moveFileTo (held), "temporarily hide generated slot source");
    // As in the reference, missing media no longer refuses the project; the slot clip is marked.
    ok (session.loadProject (moved.getChildFile ("Set.halftrack")), "a project with missing slot media opens");
    check (session.snapshot().tracks[0].slots[0].clip.mediaMissing && session.missingMedia().contains (missing)
           && session.snapshot().tracks.back().slots[1].clip.id == midiId, "the slot clip is marked missing and the rest of the project opens");
    check (held.moveFileTo (missing), "restore generated source");
}
void saveAsAudioTests (const juce::File& root)
{
    MemoryBuffers buffers;
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    constexpr float slotLevel = 0.19f, arrangementLevel = 0.07f;
    const auto original = constantFile (root, "save-as-slot-source", slotLevel);
    const auto arrangement = constantFile (root, "save-as-arrangement-source", arrangementLevel, 48000);
    ok (session.importAudioToSlot (original, 0, 0), "import Save As slot source");
    ok (session.importAudio (arrangement, 0), "import Save As arrangement source");
    const auto first = root.getChildFile ("Slot original").getChildFile ("Same.halftrack");
    const auto second = root.getChildFile ("Slot relocated").getChildFile ("Same.halftrack");
    ok (session.saveProject (first), "save slot and arrangement before same-name relocation");
    const auto oldTrack = session.snapshot().tracks[0];
    ok (session.saveProject (second), "save audio to another directory with the same project name");
    const auto movedTrack = session.snapshot().tracks[0];
    for (const auto& pair : { std::pair { oldTrack.clips[0].file, movedTrack.clips[0].file },
                              std::pair { oldTrack.slots[0].clip.file, movedTrack.slots[0].clip.file } })
        check (pair.first != pair.second && pair.second.existsAsFile()
            && pair.first.getRelativePathFrom (first.getParentDirectory()) == pair.second.getRelativePathFrom (second.getParentDirectory()),
            "Save As changes the media location without changing its relative source text");
    auto* nativeTrack = te::getAudioTracks (*engine.getActiveEdits().getEdits()[0])[0];
    check (nativeTrack->getClips()[0]->getCurrentSourceFile() == movedTrack.clips[0].file,
        "same-name Save As updates the arrangement clip's cached current source");
    check (nativeTrack->getClipSlotList().getClipSlots()[0]->getClip()->getCurrentSourceFile() == movedTrack.slots[0].clip.file,
        "same-name Save As updates the slot clip's cached current source");
    check (first.getParentDirectory().deleteRecursively() && original.deleteFile() && arrangement.deleteFile(),
        "remove old generated arrangement and slot media after Save As");
    // Register only the new paths. Stale currentSourceFile paths cannot read
    // synchronous fixtures, and the generated original files are gone.
    registerConstant (buffers, engine, movedTrack.clips[0].file, arrangementLevel, 48000);
    registerConstant (buffers, engine, movedTrack.slots[0].clip.file, slotLevel);
    dispatchGraphChanges (session);
    check (movedTrack.clips[0].id == oldTrack.clips[0].id && movedTrack.slots[0].clip.id == oldTrack.slots[0].clip.id,
        "Save As retains the current arrangement and slot identities");
    const auto checkOutput = [&] (float expected, const juce::String& name)
    {
        player.process (1024); // Complete the native graph-start fade before measuring.
        const auto rendered = player.process (8192);
        check (rendered.getNumChannels() == 2 && rendered.getNumSamples() == 8192, name + " capture dimensions");
        checkFinite (rendered);
        float error = 0;
        for (int c = 0; c < rendered.getNumChannels(); ++c)
            for (int i = 0; i < rendered.getNumSamples(); ++i)
                error = std::max (error, std::abs (rendered.getSample (c, i) - expected));
        check (error < 0.00001f, name + " both-channel full-window error " + juce::String (error, 8));
    };
    ok (session.play(), "play current arrangement after same-name Save As without reopening");
    checkOutput (arrangementLevel, "current arrangement uses relocated media");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "Save As slot immediate launch");
    ok (session.launchSlot (0, 0), "launch current slot after same-name Save As without reopening");
    checkOutput (slotLevel, "current slot uses relocated media");
    session.stop();
}
void saveAsRemovedClipHistoryTests (const juce::File& root)
{
    MemoryBuffers buffers;
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    constexpr float arrangementLevel = 0.11f, slotLevel = 0.23f;
    const auto arrangement = constantFile (root, "undo-save-arrangement", arrangementLevel, 48000);
    const auto slot = constantFile (root, "undo-save-slot", slotLevel);
    ok (session.importAudio (arrangement, 0), "import arrangement for removed-clip history");
    ok (session.importAudioToSlot (slot, 1, 0), "import slot for removed-clip history");
    const auto first = root.getChildFile ("Undo original").getChildFile ("Set.halftrack");
    const auto second = root.getChildFile ("Undo relocated").getChildFile ("Set.halftrack");
    ok (session.saveProject (first), "save removed-clip history fixture");
    ok (session.loadProject (first), "load relative media with clean undo history");
    const auto before = session.snapshot();
    const auto arrangementId = before.tracks[0].clips[0].id;
    const auto slotId = before.tracks[1].slots[0].clip.id;
    ok (session.removeClip (0, 0), "remove arrangement clip before Save As");
    ok (session.clearSlot (1, 0), "remove slot clip before Save As");
    ok (session.saveProject (second), "Save As while both audio clips exist only in undo history");
    session.undo(); session.undo();
    auto restored = session.snapshot();
    check (restored.tracks[0].clips.size() == 1 && restored.tracks[1].slots[0].hasClip
           && restored.tracks[0].clips[0].id == arrangementId && restored.tracks[1].slots[0].clip.id == slotId,
           "undo after relocation restores both original clip identities");
    check (restored.tracks[0].clips[0].file == before.tracks[0].clips[0].file
           && restored.tracks[1].slots[0].clip.file == before.tracks[1].slots[0].clip.file
           && restored.tracks[0].clips[0].file.existsAsFile() && restored.tracks[1].slots[0].clip.file.existsAsFile(),
           "undo-only media paths remain anchored to existing sources across Save As");
    ok (session.saveProject (second), "collect restored undo media into the new project");
    auto savedXml = juce::XmlDocument::parse (second);
    int savedAudioSources = 0;
    const auto checkPortableSources = [&] (auto&& self, const juce::XmlElement& node) -> void
    {
        if (node.hasTagName (te::IDs::AUDIOCLIP.toString()))
        {
            const auto sourceText = node.getStringAttribute ("source");
            check (sourceText.isNotEmpty() && ! juce::File::isAbsolutePath (sourceText)
                   && second.getParentDirectory().getChildFile (sourceText).existsAsFile(),
                   "serialized restored audio remains project-relative and portable");
            ++savedAudioSources;
        }
        for (auto* child = node.getFirstChildElement(); child != nullptr; child = child->getNextElement()) self (self, *child);
    };
    check (savedXml != nullptr, "parse relocated undo-history project");
    checkPortableSources (checkPortableSources, *savedXml);
    check (savedAudioSources == 2, "portable check covers both arrangement and slot audio");
    check (session.canRedo() && ! session.canUndo(), "saving restored media preserves both deletion redo actions");
    session.redo(); session.redo();
    check (session.snapshot().tracks[0].clips.empty() && ! session.snapshot().tracks[1].slots[0].hasClip,
           "actual redo removes both restored clips after their media was collected");
    session.undo(); session.undo();
    restored = session.snapshot();
    const auto assets = second.getSiblingFile ("Set Assets");
    check (restored.tracks[0].clips[0].file.isAChildOf (assets) && restored.tracks[1].slots[0].clip.file.isAChildOf (assets),
           "repeated undo resolves restored clips to their newly collected media");
    check (first.getParentDirectory().deleteRecursively() && arrangement.deleteFile() && slot.deleteFile(),
           "remove generated old sources after restored media has been collected");
    registerConstant (buffers, engine, restored.tracks[0].clips[0].file, arrangementLevel, 48000);
    registerConstant (buffers, engine, restored.tracks[1].slots[0].clip.file, slotLevel);
    dispatchGraphChanges (session);
    ok (session.play(), "play arrangement restored from relocated undo history");
    player.process (1024);
    level (player.process (8192), 0, 8192, arrangementLevel, "restored arrangement reads retained media");
    ok (session.setTrackMute (0, true), "isolate restored slot playback");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "immediate restored slot launch");
    ok (session.launchSlot (1, 0), "play slot restored from relocated undo history");
    dispatchGraphChanges (session); player.process (1024);
    level (player.process (8192), 0, 8192, slotLevel, "restored slot reads retained media");
    session.stop();
}
void audioTests (const juce::File& root, const juce::File& first, const juce::File& second)
{
    MemoryBuffers buffers; // Outlive the session and every graph reader.
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    ok (session.importAudioToSlot (first, 0, 0), "audio slot A");
    ok (session.importAudioToSlot (second, 0, 1), "audio slot B");
    ok (session.importAudioToSlot (first, 1, 1), "second scene track");
    auto state = session.snapshot();
    registerConstant (buffers, engine, state.tracks[0].slots[0].clip.file, 0.12f);
    registerConstant (buffers, engine, state.tracks[0].slots[1].clip.file, 0.28f);
    registerConstant (buffers, engine, state.tracks[1].slots[1].clip.file, 0.12f);
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "one-beat launch grid");
    ok (session.play(), "start empty transport");
    auto silence = player.process (4800);
    check (magnitude (silence, 0, silence.getNumSamples()) == 0, "slots remain silent before launch");
    ok (session.launchSlot (0, 0), "queue slot A");
    check (session.snapshot().tracks[0].slots[0].queued, "queued state comes from launch handle");
    auto launched = player.process (40000);
    check (std::abs (onset (launched) - 19200) <= 256, "audio launch occurs on beat boundary within two driver blocks: " + juce::String (onset (launched)));
    level (launched, 0, 18500, 0, "no pre-boundary audio");
    level (launched, 21000, 15000, 0.12f, "slot audio loops across source end");
    ok (session.launchSlot (0, 1), "queue exclusive replacement");
    auto switched = player.process (9600);
    level (switched, 512, 2000, 0.12f, "previous clip continues before switch");
    level (switched, 4500, 4000, 0.28f, "replacement excludes previous clip after boundary");
    check (! session.snapshot().tracks[0].slots[0].playing && session.snapshot().tracks[0].slots[1].playing, "one active clip per track");
    ok (session.stopLauncherTrack (0), "queue track stop");
    auto stopped = player.process (30000);
    level (stopped, 512, 15000, 0.28f, "track plays until quantised stop");
    level (stopped, 19000, 10000, 0, "track silence after quantised stop");
    check (session.snapshot().playing, "clip stop leaves transport running");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::bar), "one-bar scene grid");
    ok (session.setTrackPan (0, -1), "isolate first scene track left");
    ok (session.setTrackPan (1, 1), "isolate second scene track right");
    ok (session.launchScene (1), "queue two-track scene");
    auto scene = player.process (40000);
    check (onset (scene, 0) >= 0 && std::abs (onset (scene, 0) - onset (scene, 1)) <= 1, "scene starts both tracks on the same output sample");
    check (std::abs (onset (scene, 0) - 11600) <= 256, "scene waits for next bar");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "immediate launch grid");
    ok (session.launchScene (0), "scene with empty cell stops second track");
    auto emptyStop = player.process (4096);
    checkFinite (emptyStop);
    check (emptyStop.getMagnitude (1, 1024, 3072) < 0.00001f && emptyStop.getMagnitude (0, 1024, 3072) > 0.1f, "empty scene slot stops previous clip on its track");
    ok (session.stopAllLauncherClips(), "stop every launcher clip");
    auto allStopped = player.process (4096);
    check (magnitude (allStopped, 1024, 3072) < 0.00001f, "stop-all audible output is silent");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::bar), "queue cancellation grid");
    ok (session.launchSlot (0, 0), "queue before transport stop"); session.stop();
    check (! session.snapshot().tracks[0].slots[0].queued, "transport stop cancels pending launch");
    ok (session.play(), "resume after cancelled launch");
    auto resumed = player.process (100000);
    check (magnitude (resumed, 1024, resumed.getNumSamples() - 1024) < 0.00001f, "cancelled launch cannot fire after transport resumes");
    ok (session.launchSlot (0, 0), "queue before relocation"); session.seek (0);
    check (! session.snapshot().tracks[0].slots[0].queued, "relocation cancels pending launch");
    session.stop();
    ok (session.importAudio (first, 0), "add separate arrangement clip");
    ok (session.setTrackPan (0, 0), "center arrangement track");
    registerConstant (buffers, engine, session.snapshot().tracks[0].clips[0].file, 0.12f);
    session.seek (0); ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "immediate override");
    ok (session.launchSlot (0, 1), "launcher overrides arrangement");
    auto override = player.process (4096);
    level (override, 1024, 2048, 0.28f, "launcher owns track audio");
    // Let the engine publish playback ownership, which is intentionally separate
    // from UI display switching and is also persisted in the edit state.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    check (session.snapshot().tracks[0].launcherActive, "engine reports launcher ownership");
    const auto output = root.getChildFile ("arrangement-export.wav");
    ok (session.exportWav (output), "export arrangement while launcher owns track");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (output));
    check (reader && reader->lengthInSamples == 12000, "arrangement export uses arrangement duration");
    juce::AudioBuffer<float> rendered (2, 12000);
    check (reader->read (&rendered, 0, 12000, 0, true, true), "read arrangement export");
    level (rendered, 1024, 10000, 0.12f, "export includes arrangement instead of slot");
    ok (session.returnToArrangement(), "return to arrangement");
    session.seek (0); ok (session.play(), "play returned arrangement");
    auto returned = player.process (4096);
    level (returned, 1024, 2048, 0.12f, "return restores arrangement audio");
    session.stop();
}
void ownershipTests (const juce::File& arrangement, const juce::File& slotSource)
{
    MemoryBuffers buffers;
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    ok (session.importAudio (arrangement, 0), "ownership arrangement fixture");
    ok (session.importAudioToSlot (slotSource, 0, 0), "ownership slot fixture");
    registerConstant (buffers, engine, session.snapshot().tracks[0].clips[0].file, 0.12f, 96000);
    registerConstant (buffers, engine, session.snapshot().tracks[0].slots[0].clip.file, 0.28f);
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "ownership beat grid");
    ok (session.play(), "start arrangement before launcher");
    auto initial = player.process (4800);
    level (initial, 512, 4000, 0.12f, "arrangement audible before queue");
    ok (session.launchSlot (0, 0), "quantised takeover with arrangement");
    auto transition = player.process (28000);
    level (transition, 512, 18000, 0.12f, "queued launch preserves arrangement until boundary");
    level (transition, 20000, 7500, 0.28f, "audio takeover excludes arrangement without UI timer");
    check (session.snapshot().tracks[0].launcherActive, "audio ownership visible before UI publication");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "ownership immediate stop");
    ok (session.stopLauncherTrack (0), "stop before UI publication");
    auto stopped = player.process (4096);
    level (stopped, 512, 3500, 0, "stop preserves launcher silence before UI publication");
    ok (session.addMidiTrack ("Rebuild"), "rebuild track graph during launcher ownership");
    dispatchGraphChanges (session);
    ok (session.play(), "apply rebuilt graph");
    auto rebuilt = player.process (4096);
    level (rebuilt, 512, 3500, 0, "rebuild preserves stopped launcher ownership");
    ok (session.returnToArrangement(), "explicit return before UI publication");
    auto returned = player.process (4096);
    level (returned, 512, 3500, 0.12f, "return restores arrangement before UI publication");
    ok (session.launchSlot (0, 0), "relaunch after return");
    auto replay = player.process (4096);
    level (replay, 512, 3500, 0.28f, "relaunch takes ownership again");
    ok (session.returnToArrangement(), "return from actively playing slot");
    auto activeReturn = player.process (4096);
    level (activeReturn, 512, 3500, 0.12f, "active return does not relatch stale ownership");
    ok (session.launchSlot (0, 0), "launch for delayed-stop ownership regression");
    player.process (256);
    auto* nativeTrack = te::getAudioTracks (*engine.getActiveEdits().getEdits()[0])[0];
    const auto staleClaim = nativeTrack->launcherOwnership.load (std::memory_order_acquire);
    nativeTrack->releaseLauncherOwnership();
    auto expected = staleClaim;
    check (! nativeTrack->launcherOwnership.compare_exchange_strong (expected, staleClaim | 1), "stale callback generation cannot undo Return");
    // Model the state observed when a native stop try-lock misses: Return has
    // released ownership, but the previous handle still reports playing.
    player.process (256);
    check ((nativeTrack->launcherOwnership.load (std::memory_order_acquire) & 1) == 0, "old playing handle cannot claim a returned generation");
    ok (session.returnToArrangement(), "finish delayed-stop simulation");
    session.stop();
}
void emptySlotTests (const juce::File& root, const juce::File& arrangement, const juce::File& slotSource)
{
    MemoryBuffers buffers;
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    ok (session.importAudio (arrangement, 0), "empty-target arrangement fixture");
    registerConstant (buffers, engine, session.snapshot().tracks[0].clips[0].file, 0.12f, 96000);
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "empty-target beat grid");
    const auto originalName = session.snapshot().tracks[0].name;
    ok (session.renameTrack (0, "Silent target"), "structural undo before empty launch");
    ok (session.play(), "start arrangement before empty target"); player.process (4800);
    ok (session.launchSlot (0, 0), "launch empty slot over arrangement");
    auto transition = player.process (28000);
    level (transition, 512, 18000, 0.12f, "empty target preserves pre-boundary arrangement");
    level (transition, 20000, 7500, 0, "empty target silences arrangement at beat");
    check (session.snapshot().tracks[0].launcherActive && session.snapshot().playing, "empty target owns track while transport continues");
    session.undo();
    check (session.snapshot().tracks[0].name == originalName && session.snapshot().tracks[0].clips.size() == 1, "empty performance command adds no structural undo step");
    session.redo();
    check (session.snapshot().tracks[0].name == "Silent target", "structural redo survives empty launch");
    session.stop(); ok (session.play(), "resume after empty target stop");
    auto resumed = player.process (4096);
    level (resumed, 512, 3500, 0, "transport stop preserves silent ownership");
    ok (session.addMidiTrack ("Graph change"), "rebuild while empty target owns track");
    dispatchGraphChanges (session);
    ok (session.play(), "apply empty-target graph rebuild");
    auto rebuilt = player.process (4096);
    level (rebuilt, 512, 3500, 0, "rebuilt graph retains silent ownership");
    ok (session.returnToArrangement(), "return after empty target"); session.seek (0);
    auto returned = player.process (4096);
    level (returned, 512, 3500, 0.12f, "Return restores arrangement after empty target");

    session.stop(); session.seek (0); ok (session.play(), "restart before all-empty scene"); player.process (4800);
    ok (session.launchScene (1), "launch all-empty scene");
    auto scene = player.process (28000);
    level (scene, 512, 18000, 0.12f, "all-empty scene waits for common boundary");
    level (scene, 20000, 7500, 0, "all-empty scene stops arrangement");
    check (session.snapshot().tracks[1].launcherActive && session.snapshot().tracks[2].launcherActive, "source-free audio and instrument tracks take silent ownership");
    ok (session.importAudio (arrangement, 1), "add arrangement after source-free takeover");
    registerConstant (buffers, engine, session.snapshot().tracks[1].clips[0].file, 0.12f, 96000);
    dispatchGraphChanges (session);
    ok (session.play(), "rebuild after adding previously absent source");
    auto inserted = player.process (4096);
    level (inserted, 512, 3500, 0, "new arrangement content stays silent under existing ownership");
    while (! session.snapshot().scenes.empty()) ok (session.removeScene (0), "remove scene while silence owns tracks");
    dispatchGraphChanges (session);
    ok (session.play(), "rebuild with no scenes");
    auto noScenes = player.process (4096);
    level (noScenes, 512, 3500, 0, "removing every scene preserves silent ownership");
    ok (session.returnToArrangement(), "return with no remaining scenes"); session.seek (0);
    auto noScenesReturn = player.process (4096);
    level (noScenesReturn, 512, 3500, 0.24f, "Return restores both arrangements after scene removal");
    ok (session.addScene ("Restored row"), "restore empty scene for portable save");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "immediate empty target");
    ok (session.launchScene (0), "silent ownership before save"); player.process (256);
    const auto file = root.getChildFile ("Empty takeover").getChildFile ("Set.halftrack");
    ok (session.saveProject (file), "save after empty performance command");
    session.stop(); ok (session.loadProject (file), "reopen empty-target project");
    const auto reopened = session.snapshot();
    check (reopened.scenes.size() == 1 && ! reopened.tracks[0].slots[0].hasClip && ! reopened.tracks[1].slots[0].hasClip,
           "empty takeover creates no hidden persisted clip");
    check (! reopened.tracks[0].launcherActive && ! reopened.tracks[1].launcherActive && ! session.canUndo(),
           "reopen clears transient ownership and undo history");
    registerConstant (buffers, engine, reopened.tracks[0].clips[0].file, 0.12f, 96000);
    registerConstant (buffers, engine, reopened.tracks[1].clips[0].file, 0.12f, 96000);
    session.seek (0); ok (session.play(), "play reopened arrangement");
    auto reopenedAudio = player.process (4096);
    level (reopenedAudio, 512, 3500, 0.24f, "reopened project plays original arrangements");
    session.stop();

    // Each cancellation must prevent the old boundary from firing after playback resumes.
    for (int cancellation = 0; cancellation < 3; ++cancellation)
    {
        ok (session.returnToArrangement(), "clear ownership before cancellation case");
        session.seek (0); ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "cancellation beat grid");
        ok (session.play(), "start cancellation case"); player.process (4800);
        ok (session.launchSlot (0, 0), "queue empty target for cancellation");
        if (cancellation == 0) { session.stop(); session.seek (0); ok (session.play(), "resume cancelled Stop"); }
        if (cancellation == 1) session.seek (0);
        if (cancellation == 2) ok (session.returnToArrangement(), "cancel queued empty target with Return");
        auto cancelled = player.process (40000);
        level (cancelled, 512, 39000, 0.24f, "cancelled empty target never silences arrangement");
        check (! session.snapshot().tracks[0].launcherActive, "cancelled empty target does not claim ownership");
        session.stop();
    }
    ok (session.importAudioToSlot (slotSource, 0, 0), "audible replacement for pending empty target");
    registerConstant (buffers, engine, session.snapshot().tracks[0].slots[0].clip.file, 0.28f);
    session.seek (0); ok (session.play(), "start replacement case"); player.process (4800);
    // The single remaining row is occupied on track 0, so add an empty target row.
    ok (session.addScene ("Empty"), "empty row beside audible slot");
    ok (session.launchSlot (0, 1), "queue empty target before audible replacement");
    ok (session.launchSlot (0, 0), "replace pending empty target with audible launch");
    auto replacement = player.process (28000);
    level (replacement, 20000, 7500, 0.4f, "audible launch replaces pending empty takeover");
    session.stop();
}

void midiEmptySlotTests()
{
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    ok (session.addMidiTrack ("Empty MIDI target"), "MIDI empty-target track");
    auto track = session.snapshot().tracks.back();
    auto synth = track.synth; synth.attackSeconds = synth.decaySeconds = 0; synth.sustain = 1; synth.releaseSeconds = 0.01f;
    ok (session.setSynthSettings (track.id, synth), "MIDI empty-target envelope");
    ok (session.addMidiClip (track.id, 0, 8), "MIDI empty-target arrangement");
    ok (session.addMidiNote (session.snapshot().tracks.back().clips[0].id, 60, 0, 8, 1), "held note before empty target");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "MIDI empty-target beat grid");
    ok (session.play(), "start held note before empty target"); player.process (4800);
    ok (session.launchSlot (2, 0), "launch empty MIDI slot");
    auto output = player.process (40000);
    check (magnitude (output, 512, 18000) > 0.01f, "MIDI empty target preserves pre-boundary note");
    check (magnitude (output, 21000, 18000) < 0.00001f, "MIDI empty target releases held arrangement note");
    session.stop();
}
void quantisationTests (const juce::File& source)
{
    MemoryBuffers buffers;
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    session.setTempo (90);
    ok (session.importAudioToSlot (source, 0, 0), "tempo-aware launch source");
    registerConstant (buffers, engine, session.snapshot().tracks[0].slots[0].clip.file, 0.12f);
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "90 BPM launch grid");
    ok (session.play(), "start 90 BPM transport"); player.process (4800);
    ok (session.launchSlot (0, 0), "queue at 90 BPM");
    auto slow = player.process (40000);
    check (std::abs (onset (slow) - 27200) <= 256, "launch uses current 90 BPM beat duration");
    session.stop(); ok (session.returnToArrangement(), "release ownership before loop tests");
    session.setTempo (120); session.setLoop (true, 0.25, 0.75); session.seek (0.6);
    ok (session.play(), "start non-grid arrangement loop"); player.process (4800);
    ok (session.launchSlot (0, 0), "queue beat across loop wrap");
    auto wrapped = player.process (25000);
    check (std::abs (onset (wrapped) - 14400) <= 256, "beat launch wraps to first grid point inside loop");
    session.stop(); ok (session.returnToArrangement(), "release loop ownership");
    session.seek (0.6);
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::bar), "bar grid in sub-bar loop");
    ok (session.play(), "start short loop before bar launch"); player.process (4800);
    ok (session.launchSlot (0, 0), "queue bar with no bar point inside loop");
    auto shortLoop = player.process (10000);
    check (std::abs (onset (shortLoop) - 2400) <= 256, "short loop falls back to loop start boundary");
    session.stop(); ok (session.returnToArrangement(), "release before stopped launch");
    session.setLoop (false, 0, 8); session.seek (0);
    ok (session.launchSlot (0, 0), "bar launch starts stopped transport immediately");
    auto immediate = player.process (4096);
    check (onset (immediate) >= 0 && onset (immediate) <= 256, "stopped transport launch is immediate");
    session.stop();
}
void midiOwnershipTests (int slotPitch)
{
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    ok (session.addMidiTrack ("Held arrangement"), "MIDI ownership track");
    auto track = session.snapshot().tracks.back();
    auto synth = track.synth;
    synth.attackSeconds = synth.decaySeconds = 0; synth.sustain = 1; synth.releaseSeconds = 0.01f;
    ok (session.setSynthSettings (track.id, synth), "MIDI ownership envelope");
    ok (session.addMidiClip (track.id, 0, 8), "held arrangement clip");
    ok (session.addMidiNote (session.snapshot().tracks.back().clips[0].id, 60, 0, 8, 1), "held arrangement note");
    ok (session.addMidiClipToSlot (track.id, 0, 1), "short MIDI slot");
    ok (session.addMidiNote (session.snapshot().tracks.back().slots[0].clip.id, slotPitch, 0, 0.25, 1), "short slot note");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "MIDI ownership beat grid");
    ok (session.play(), "start held arrangement note");
    auto initial = player.process (4800);
    check (magnitude (initial, 512, 4000) > 0.01f, "arrangement note starts");
    ok (session.launchSlot (2, 0), "launch MIDI over held arrangement");
    auto transition = player.process (40000);
    check (magnitude (transition, 512, 18000) > 0.01f, "MIDI arrangement remains before boundary");
    check (magnitude (transition, 21000, 2000) > 0.01f, "slot note survives arrangement note-off at the boundary");
    check (magnitude (transition, 31000, 8000) < 0.00001f, "MIDI takeover releases held arrangement voice");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "MIDI ownership stop grid");
    ok (session.stopAllLauncherClips(), "stop after MIDI takeover");
    auto stopped = player.process (4096);
    check (magnitude (stopped, 1024, 3072) < 0.00001f, "MIDI stop leaves no arrangement or slot voices");
    ok (session.returnToArrangement(), "return MIDI ownership");
    session.seek (0);
    auto returned = player.process (4096);
    check (magnitude (returned, 1024, 3072) > 0.01f, "MIDI arrangement resumes on return and relocation");
    session.stop();
}
void midiTests()
{
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    ok (session.addMidiTrack ("Synth"), "create launcher instrument");
    const auto track = session.snapshot().tracks.back();
    ok (session.addMidiClipToSlot (track.id, 0, 1), "one-beat MIDI loop");
    const auto clip = session.snapshot().tracks.back().slots[0].clip;
    ok (session.addMidiNote (clip.id, 69, 0, 0.25, 1), "note in launched loop");
    auto synth = track.synth;
    synth.attackSeconds = synth.decaySeconds = 0; synth.sustain = 1; synth.releaseSeconds = 0.01f;
    ok (session.setSynthSettings (track.id, synth), "deterministic MIDI envelope");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "MIDI beat grid");
    ok (session.play(), "start MIDI transport"); player.process (4800);
    ok (session.launchSlot (2, 0), "queue native MIDI clip");
    auto output = player.process (70000);
    check (std::abs (onset (output) - 19200) <= 256, "MIDI synth launch occurs at requested beat");
    check (magnitude (output, 0, 18500) == 0, "MIDI produces no early sound");
    check (magnitude (output, 21000, 2000) > 0.01f && magnitude (output, 45000, 2000) > 0.01f, "MIDI notes repeat through native loop scheduler");
    check (magnitude (output, 31000, 7000) < 0.00001f, "MIDI note-off and release leave inter-note silence");
    auto slot = session.snapshot().tracks.back().slots[0];
    check (slot.playing && slot.playPositionBeats >= 0 && slot.playPositionBeats < 1, "MIDI playhead is clip-local and looped");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "immediate MIDI stop grid");
    ok (session.stopAllLauncherClips(), "stop MIDI loop");
    auto stopped = player.process (24000);
    check (magnitude (stopped, 3000, 21000) < 0.00001f, "MIDI stop has no stuck voice");
    session.stop();
}
void sceneOrderTests (const juce::File& root, const juce::File& first, const juce::File& second)
{
    MemoryBuffers memory;
    Session s (false); auto& engine = engineFor (s);
    te::test_utilities::EnginePlayer player (engine, parameters());
    ok (s.importAudioToSlot (first, 0, 0), "prepare first scene for ordering");
    ok (s.importAudioToSlot (second, 0, 1), "prepare second scene for ordering");
    registerConstant (memory, engine, s.snapshot().tracks[0].slots[0].clip.file, .12f);
    registerConstant (memory, engine, s.snapshot().tracks[0].slots[1].clip.file, .28f);
    ok (s.addMidiTrack ("Ordered MIDI"), "prepare MIDI row ordering");
    ok (s.addReturnTrack ("Ordered return"), "prepare return row ordering");
    const auto midiTrack = s.snapshot().tracks[2].id;
    ok (s.addMidiClipToSlot (midiTrack, 0, 4), "prepare ordered MIDI clip");
    const auto midiId = s.snapshot().tracks[2].slots[0].clip.id;
    ok (s.addMidiNote (midiId, 60, 0, 1, .8f), "prepare ordered MIDI note");
    ok (s.setTrackMute (2, true), "isolate ordered audio measurement");
    ok (s.renameScene (0, "First"), "name ordered first scene");
    ok (s.renameScene (1, "Second"), "name ordered second scene");
    const auto original = s.snapshot();
    const auto firstId = original.scenes[0].id, secondId = original.scenes[1].id;
    const auto checkOrder = [&] (const std::vector<int>& order, const juce::String& label)
    {
        const auto current = s.snapshot();
        check (current.scenes.size() == order.size(), label + " scene count");
        for (size_t row = 0; row < order.size(); ++row)
        {
            const auto from = (size_t) order[row];
            check (current.scenes[row].id == original.scenes[from].id && current.scenes[row].name == original.scenes[from].name, label + " scene identity/name");
            for (size_t track = 0; track < original.tracks.size(); ++track)
            {
                const auto& a = current.tracks[track].slots[row]; const auto& b = original.tracks[track].slots[from];
                check (a.id == b.id && a.hasClip == b.hasClip && (!a.hasClip || a.clip.id == b.clip.id), label + " complete slot identity");
                if (a.hasClip && a.clip.isMidi) check (a.clip.notes[0].id == b.clip.notes[0].id, label + " note identity");
            }
        }
    };
    ok (s.setLaunchQuantisation (Session::LaunchQuantisation::none), "immediate launch before move");
    ok (s.launchScene (0), "start scene before move"); player.process (1024);
    level (player.process (1024), 0, 1024, .12f, "scene audible before moving");
    ok (s.moveScene (firstId, 7), "move playing first scene to last row"); dispatchGraphChanges (s);
    checkOrder ({1,2,3,4,5,6,7,0}, "move to end");
    check (s.snapshot().tracks[0].slots[7].playing, "playing handle follows its original clip to the last row");
    level (player.process (4096), 512, 3500, .12f, "moving a playing scene preserves output");
    s.undo(); checkOrder ({0,1,2,3,4,5,6,7}, "undo move");
    level (player.process (4096), 512, 3500, .12f, "undo row movement keeps sound");
    s.redo(); checkOrder ({1,2,3,4,5,6,7,0}, "redo move");
    ok (s.setLaunchQuantisation (Session::LaunchQuantisation::beat), "queue with one-beat grid");
    ok (s.launchScene (0), "queue second scene at its reordered row");
    check (s.snapshot().tracks[0].slots[0].queued, "second clip is queued before moving");
    ok (s.moveScene (secondId, 6), "move the queued scene before its launch"); dispatchGraphChanges (s);
    checkOrder ({2,3,4,5,6,7,1,0}, "move queued scene");
    check (s.snapshot().tracks[0].slots[6].queued, "queued handle follows its clip identity");
    const auto switched = player.process (40000);
    level (switched, 512, 8000, .12f, "source remains audible before queued boundary");
    level (switched, 26000, 13000, .28f, "queued scene launches after moving");
    check (s.snapshot().tracks[0].slots[6].playing && !s.snapshot().tracks[0].slots[7].playing, "moved queued clip replaces the original source");
    s.stop(); const auto project = root.getChildFile ("scene-order.halftrack");
    ok (s.saveProject (project), "save reordered project"); ok (s.loadProject (project), "reopen reordered project");
    checkOrder ({2,3,4,5,6,7,1,0}, "persisted ordering");
    ok (s.moveScene (secondId, 6), "same row is a no-op");
    check (!s.snapshot().dirty && !s.canUndo(), "same-row move leaves clean document and history");
    ok (s.moveScene (firstId, 0), "move last scene to first"); s.undo();
    for (int invalid : {-1, 8}) check (s.moveScene (firstId, invalid).failed() && s.canRedo(), "invalid row preserves redo");
    check (s.moveScene ("missing", 0).failed() && s.canRedo(), "missing scene preserves redo");
    s.redo(); checkOrder ({0,2,3,4,5,6,7,1}, "rejected commands retain usable redo");
    ok (s.removeScene (0), "remove a captured move target");
    const auto replacement = s.snapshot().scenes[0].id;
    check (s.moveScene (firstId, 1).failed() && s.snapshot().scenes[0].id == replacement, "stale move cannot target the replacement scene");
}
void sceneCopyTests(const juce::File& root,const juce::File& source)
{
    MemoryBuffers memory;Session s(false);auto& engine=engineFor(s);te::test_utilities::EnginePlayer player(engine,parameters());
    ok(s.importAudioToSlot(source,0,0),"prepare scene audio");ok(s.importAudioToSlot(source,1,0),"prepare disabled scene audio");
    // Activation can prepare native readers even before transport starts. Register
    // synchronous generated sources before that preparation, not after it.
    for(int i=0;i<2;++i)registerConstant(memory,engine,s.snapshot().tracks[(size_t)i].slots[0].clip.file,.12f);
    ok(s.addMidiTrack("Scene instrument"),"prepare scene MIDI track");const auto track=s.snapshot().tracks[2].id;
    ok(s.addMidiClipToSlot(track,0,4),"prepare scene MIDI clip");const auto midi=s.snapshot().tracks[2].slots[0].clip.id;
    ok(s.addMidiNote(midi,60,0,1,.8f),"draw original scene note");ok(s.addReturnTrack("Scene return"),"prepare return row alignment");
    ok(s.renameScene(0,"Verse"),"name original scene");const auto sourceId=s.snapshot().scenes[0].id;
    const auto audio=s.snapshot().tracks[0].slots[0].clip.id;auto settings=s.snapshot().tracks[0].slots[0].clip.audio;settings.gainDb=-6;
    ok(s.setAudioClipSettings(audio,settings),"set source clip gain");ok(s.renameClip(audio,"Drums"),"name source clip");
    ok(s.setClipActive(s.snapshot().tracks[1].slots[0].clip.id,false),"disable second source clip");
    ok(s.setTrackPan(0,-1),"isolate audio left");ok(s.setTrackPan(2,1),"isolate synth right");
    ok(s.setLaunchQuantisation(Session::LaunchQuantisation::none),"set immediate scene launch");ok(s.launchScene(0),"play source scene");dispatchGraphChanges(s);player.process(1024);
    const float expected=.12f*std::sqrt(2.0f)*std::pow(10.0f,-6.0f/20.0f);level(player.process(1024),0,1024,expected,"source audio before duplication");
    const auto original=s.snapshot();juce::String newId="preserved";ok(s.duplicateScene(sourceId,newId),"duplicate playing scene after original");dispatchGraphChanges(s);
    const auto copied=s.snapshot();check(copied.scenes.size()==9&&copied.scenes[0].id==sourceId&&copied.scenes[1].id==newId&&newId!=sourceId&&copied.scenes[1].name=="Verse copy","scene copy gets new identity and next-row placement");
    for(size_t i=0;i<copied.tracks.size();++i)
    {
        const auto& old=original.tracks[i].slots[0];const auto& from=copied.tracks[i].slots[0];const auto& to=copied.tracks[i].slots[1];
        check(copied.tracks[i].slots.size()==9&&from.id==old.id&&to.id!=old.id&&to.hasClip==old.hasClip&&!to.playing&&!to.queued,"scene duplicates slots without retargeting or launching");
        for(size_t row=1;row<original.scenes.size();++row)check(copied.tracks[i].slots[row+1].id==original.tracks[i].slots[row].id,"later slot identities remain ordered");
        if(old.hasClip)check(to.clip.id!=old.clip.id&&from.clip.id==old.clip.id&&to.clip.name==old.clip.name&&to.clip.active==old.clip.active&&to.clip.length==old.clip.length&&to.clip.file==old.clip.file&&to.clip.audio==old.clip.audio,"copied clip retains state/media with independent identity");
    }
    const auto duplicateMidi=copied.tracks[2].slots[1].clip;check(duplicateMidi.notes.size()==1&&duplicateMidi.notes[0].id!=original.tracks[2].slots[0].clip.notes[0].id&&duplicateMidi.notes[0].pitch==60,"scene copy renews note identity");
    check(copied.tracks[0].slots[0].playing&&copied.tracks[2].slots[0].playing,"duplicate retains the existing live performance");
    auto running=player.process(4096);level(running,512,3500,expected,"duplication preserves running source audio");
    s.undo();check(s.snapshot().scenes.size()==8&&s.snapshot().scenes[0].id==sourceId,"one undo removes the entire scene copy");s.redo();check(s.snapshot().scenes[1].id==newId&&s.snapshot().tracks[2].slots[1].clip.notes[0].id==duplicateMidi.notes[0].id,"redo restores exact copied identities");
    ok(s.launchScene(1),"launch duplicated scene");dispatchGraphChanges(s);auto launched=player.process(12000);level(launched,512,11000,expected,"duplicated scene plays matching audio");check(launched.getMagnitude(1,0,12000)>.01,"duplicated scene plays its copied MIDI");
    auto note=duplicateMidi.notes[0];ok(s.updateMidiNote(duplicateMidi.id,note.id,72,note.startBeat,note.lengthBeats,note.velocity),"edit only copied scene note");check(s.snapshot().tracks[2].slots[0].clip.notes[0].pitch==60,"original MIDI remains independent");s.undo();
    s.stop();const auto project=root.getChildFile("scene-copy.halftrack");ok(s.saveProject(project),"collect copied scene project");ok(s.loadProject(project),"reopen copied scene");
    check(s.snapshot().scenes[1].id==newId&&s.snapshot().tracks[2].slots[1].clip.notes[0].id==duplicateMidi.notes[0].id&&!s.snapshot().tracks[0].slots[1].playing,"copy persists without auto-launch on reopen");
    ok(s.removeScene(0),"remove original scene");check(s.snapshot().scenes[0].id==newId&&s.snapshot().tracks[0].slots[0].clip.file.existsAsFile(),"copy survives original scene deletion");s.undo();
    juce::String rejected="preserved";check(s.duplicateScene("missing",rejected).failed()&&rejected=="preserved"&&s.canRedo(),"invalid scene preserves project, output and redo");
    juce::String empty;ok(s.duplicateScene(s.snapshot().scenes.back().id,empty),"duplicate empty last scene");check(s.snapshot().scenes.back().id==empty,"last-row duplicate appends safely");
    for(const auto& t:s.snapshot().tracks)check(!t.slots.back().hasClip,"empty rows stay empty on every track and return");
    const auto initialCount=s.snapshot().scenes.size();
    for(size_t i=initialCount;i<256;++i)if(s.addScene().failed())throw std::runtime_error("cannot prepare scene ceiling");
    ok(s.renameScene(0,"At limit"),"prepare scene-limit redo");s.undo();
    check(s.snapshot().scenes.size()==256&&s.duplicateScene(sourceId,rejected).failed()&&rejected=="preserved"&&s.canRedo(),"scene ceiling rejects copy without changing output or redo");
    s.redo();check(s.snapshot().scenes[0].name=="At limit"&&s.snapshot().scenes.size()==256,"rejected copy preserves usable rename history");
}
}
int runLauncherTests (const juce::File& requestedDirectory)
{
    try
    {
        const auto root = requestedDirectory.getChildFile ("launcher-" + juce::Uuid().toString());
        ok (root.createDirectory(), "create isolated test artifacts");
        const auto first = constantFile (root, "first", 0.12f), second = constantFile (root, "second", 0.28f);
        structureTests (root, first);
        sceneCopyTests(root,first);
        sceneOrderTests(root,first,second);
        saveAsAudioTests (root);
        saveAsRemovedClipHistoryTests (root);
        audioTests (root, first, second);
        const auto longArrangement = constantFile (root, "long-arrangement", 0.12f, 96000);
        ownershipTests (longArrangement, second);
        emptySlotTests (root, longArrangement, second);
        midiEmptySlotTests();
        quantisationTests (first);
        midiOwnershipTests (76);
        midiOwnershipTests (60);
        midiTests();
        std::cout << "Launcher acceptance tests passed. Artifacts: " << root.getFullPathName() << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL launcher " << error.what() << std::endl;
        return 1;
    }
}
}
