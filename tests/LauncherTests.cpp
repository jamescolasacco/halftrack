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
    check (session.loadProject (moved.getChildFile ("Set.halftrack")).failed(), "missing slot media rejects load");
    check (session.snapshot().tracks.back().slots[1].clip.id == midiId, "rejected load preserves current project");
    check (held.moveFileTo (missing), "restore generated source");
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
}
int runLauncherTests (const juce::File& requestedDirectory)
{
    try
    {
        const auto root = requestedDirectory.getChildFile ("launcher-" + juce::Uuid().toString());
        ok (root.createDirectory(), "create isolated test artifacts");
        const auto first = constantFile (root, "first", 0.12f), second = constantFile (root, "second", 0.28f);
        structureTests (root, first);
        audioTests (root, first, second);
        const auto longArrangement = constantFile (root, "long-arrangement", 0.12f, 96000);
        ownershipTests (longArrangement, second);
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
