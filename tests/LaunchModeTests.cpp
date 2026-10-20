#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Clip launch modes and per-clip launch grids. Generated slot sources play
// through accelerated engine callbacks at 120 BPM and 48 kHz, so one beat is
// 24000 samples and every expected boundary is computed from that grid.
namespace halftrack
{
namespace
{
namespace te = tracktion;
using Mode = Session::LaunchMode;
using Grid = Session::ClipQuantisation;
using Launch = Session::LaunchSettings;
constexpr int beat = 24000;
void check (bool condition, const juce::String& name)
{
    if (! condition) throw std::runtime_error (name.toStdString());
    std::cout << "PASS launch modes: " << name << std::endl;
}
void ok (const juce::Result& result, const juce::String& name) { check (result.wasOk(), name + (result.failed() ? ": " + result.getErrorMessage() : "")); }
te::Engine& engineFor (Session& session)
{
    for (auto* engine : te::Engine::getEngines())
        if (&engine->getDeviceManager().deviceManager == &session.audioDeviceManager()) return *engine;
    throw std::runtime_error ("session engine not found");
}
te::HostedAudioDeviceInterface::Parameters parameters()
{
    te::HostedAudioDeviceInterface::Parameters p;
    p.sampleRate = 48000; p.blockSize = 128; p.inputChannels = 0; p.outputChannels = 2; p.useMidiDevices = false;
    return p;
}
using MemoryBuffers = std::vector<std::unique_ptr<choc::buffer::InterleavedBuffer<float>>>;
// Four 120 BPM beats of generated audio, written for import and registered for synchronous reads.
struct Fixture { juce::File file; juce::AudioBuffer<float> audio; };
Fixture fixture (const juce::File& root, const juce::String& name, const std::function<float (int)>& sample)
{
    Fixture f { root.getChildFile (name + ".wav"), juce::AudioBuffer<float> (2, 4 * beat) };
    for (int n = 0; n < f.audio.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) f.audio.setSample (c, n, sample (n));
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (f.file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    check (writer && writer->writeFromAudioSampleBuffer (f.audio, 0, f.audio.getNumSamples()), "write generated " + name);
    return f;
}
void registerFixture (MemoryBuffers& buffers, te::Engine& engine, const juce::File& file, const juce::AudioBuffer<float>& audio)
{
    auto buffer = std::make_unique<choc::buffer::InterleavedBuffer<float>> (2, (unsigned int) audio.getNumSamples());
    for (unsigned int c = 0; c < 2; ++c)
        for (unsigned int n = 0; n < (unsigned int) audio.getNumSamples(); ++n) buffer->getSample (c, n) = audio.getSample ((int) c, (int) n);
    engine.getAudioFileManager().registerMemoryBuffer (file.getFullPathName().toStdString(), buffer->getView(), 48000);
    buffers.push_back (std::move (buffer));
}
void level (const juce::AudioBuffer<float>& buffer, int start, int end, float expected, const juce::String& name)
{
    check (start >= 0 && start < end && end <= buffer.getNumSamples(), name + " window is inside the capture");
    float error = 0;
    for (int n = start; n < end; ++n) error = std::max (error, std::abs (buffer.getSample (0, n) - expected));
    check (error < 1.0e-4f, name + " (max error " + juce::String (error, 7) + ")");
}
int onset (const juce::AudioBuffer<float>& buffer)
{
    for (int n = 0; n < buffer.getNumSamples(); ++n) if (std::abs (buffer.getSample (0, n)) > 0.001f) return n;
    return -1;
}
// Burst starts, separated by at least a quarter beat of quiet.
std::vector<int> bursts (const juce::AudioBuffer<float>& buffer)
{
    std::vector<int> found; int quiet = beat / 4;
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        if (std::abs (buffer.getSample (0, n)) > 0.05f) { if (quiet >= beat / 4) found.push_back (n); quiet = 0; }
        else ++quiet;
    }
    return found;
}
juce::String list (const std::vector<int>& values) { juce::String s; for (auto v : values) s << v << " "; return s.trim(); }
Session::SlotInfo slot (Session& s, int track, int scene) { const auto snapshot = s.snapshot(); return snapshot.tracks[(size_t) track].slots[(size_t) scene]; }
Session::ClipInfo slotClip (Session& s, int track, int scene) { return slot (s, track, scene).clip; }

void settingsTests (const juce::File& root)
{
    Session session (false);
    const auto source = fixture (root, "settings-source", [] (int) { return .1f; });
    ok (session.importAudioToSlot (source.file, 0, 0), "import a Session clip");
    auto clip = slotClip (session, 0, 0);
    check (clip.launch == Launch {} && clip.launch.mode == Mode::trigger && clip.launch.quantisation == Grid::global && ! clip.launch.legato,
           "new clips trigger on the global grid without legato");
    ok (session.setClipLaunchSettings (clip.id, clip.launch), "re-applying the current settings succeeds");
    session.undo(); check (! session.snapshot().tracks[0].slots[0].hasClip, "re-applying the current settings adds no history");
    session.redo();
    ok (session.setClipLaunchSettings (clip.id, { Mode::gate, Grid::sixteenth, true }), "choose Gate on a sixteenth grid with legato");
    check (slotClip (session, 0, 0).launch == Launch { Mode::gate, Grid::sixteenth, true } && session.canUndo(), "launch settings are reported and undoable");
    session.undo(); check (slotClip (session, 0, 0).launch == Launch {}, "undo restores Trigger on the global grid");
    session.redo(); check (slotClip (session, 0, 0).launch == Launch { Mode::gate, Grid::sixteenth, true }, "redo reapplies the launch settings");
    check (session.setClipLaunchSettings ("missing", {}).failed(), "an unknown clip is rejected");
    check (session.setClipLaunchSettings (clip.id, { static_cast<Mode> (9), Grid::bar, false }).failed()
           && session.setClipLaunchSettings (clip.id, { Mode::toggle, static_cast<Grid> (40), false }).failed()
           && slotClip (session, 0, 0).launch.mode == Mode::gate, "out-of-range launch settings are rejected without changing the clip");
    ok (session.importAudio (source.file, 0), "add an arrangement clip");
    check (session.setClipLaunchSettings (session.snapshot().tracks[0].clips[0].id, {}).failed(), "arrangement clips have no launch settings");
    ok (session.setClipLaunchSettings (clip.id, { Mode::repeat, Grid::eightBars, false }), "choose Repeat on an eight-bar grid");
    const auto project = root.getChildFile ("Launch").getChildFile ("Launch.halftrack");
    ok (session.saveProject (project), "save launch settings"); ok (session.loadProject (project), "reopen launch settings");
    check (slotClip (session, 0, 0).launch == Launch { Mode::repeat, Grid::eightBars, false }, "launch mode and grid reopen");
    juce::String scene;
    ok (session.duplicateScene (session.snapshot().scenes[0].id, scene), "duplicate the configured scene");
    check (slotClip (session, 0, 1).launch == Launch { Mode::repeat, Grid::eightBars, false }, "duplicated clips keep their launch settings");
}

void modeTests (const juce::File& root)
{
    MemoryBuffers buffers; // Outlive the session and every graph reader.
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    const auto low = fixture (root, "low", [] (int) { return .12f; }), high = fixture (root, "high", [] (int) { return .28f; });
    ok (session.importAudioToSlot (low.file, 0, 0), "import the low clip"); ok (session.importAudioToSlot (high.file, 0, 1), "import the high clip");
    registerFixture (buffers, engine, slotClip (session, 0, 0).file, low.audio);
    registerFixture (buffers, engine, slotClip (session, 0, 1).file, high.audio);
    const auto lowId = slotClip (session, 0, 0).id, highId = slotClip (session, 0, 1).id;
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::bar), "one-bar global grid");
    ok (session.setClipLaunchSettings (lowId, { Mode::trigger, Grid::quarter, false }), "low clip launches on quarter notes");
    ok (session.play(), "start the transport");
    check (player.process (4800).getMagnitude (0, 0, 4800) == 0, "nothing plays before a launch");
    ok (session.pressSlot (0, 0), "press the low clip"); ok (session.releaseSlot (0, 0), "release the low clip");
    auto out = player.process (40000);
    check (std::abs (onset (out) - (beat - 4800)) <= 256, "a quarter-note clip ignores the one-bar global grid (onset " + juce::String (onset (out)) + ")");
    level (out, 21000, 40000, .12f, "Trigger keeps playing after its button is released");
    // 44800 samples have played; the next eighth note is at 48000.
    ok (session.setClipLaunchSettings (highId, { Mode::gate, Grid::eighth, false }), "high clip gates on eighth notes");
    ok (session.pressSlot (0, 1), "hold the high clip");
    out = player.process (9600);
    level (out, 512, 2800, .12f, "the low clip continues until the eighth-note boundary");
    level (out, 3600, 9600, .28f, "the gated clip replaces it on the boundary");
    ok (session.releaseSlot (0, 1), "release the gated clip");
    check (slot (session, 0, 1).playing, "a released gate clip plays until its stop boundary");
    out = player.process (12000); // Release at 54400, stop at 60000.
    level (out, 0, 5200, .28f, "the gated clip holds until the next eighth note");
    level (out, 6000, 12000, 0, "releasing the gate stops the clip on its own grid");
    check (! slot (session, 0, 1).playing && ! slot (session, 0, 0).playing, "the track is silent after the gate closes");
    // 66400 samples; the next quarter note is at 72000.
    ok (session.setClipLaunchSettings (lowId, { Mode::toggle, Grid::quarter, false }), "low clip toggles on quarter notes");
    ok (session.pressSlot (0, 0), "first Toggle press starts the clip"); ok (session.releaseSlot (0, 0), "Toggle ignores release");
    out = player.process (12000);
    level (out, 0, 5400, 0, "the toggle clip waits for its boundary");
    level (out, 6200, 12000, .12f, "the toggle clip starts and keeps playing after release");
    ok (session.pressSlot (0, 0), "second Toggle press stops the clip"); ok (session.releaseSlot (0, 0), "release after the second press");
    out = player.process (24000); // Pressed at 78400, stop at 96000.
    level (out, 0, 17200, .12f, "the toggle clip plays until its stop boundary");
    level (out, 18000, 24000, 0, "the second press stops the toggle clip");
    ok (session.pressSlot (0, 0), "queue a toggle launch"); ok (session.pressSlot (0, 0), "press again before the boundary");
    check (! slot (session, 0, 0).queued && ! slot (session, 0, 0).playing, "a second press cancels a queued toggle launch");
    check (player.process (48000).getMagnitude (0, 0, 48000) < 1.0e-6f, "a cancelled toggle launch never plays");
    ok (session.setClipLaunchSettings (lowId, { Mode::trigger, Grid::none, false }), "low clip launches immediately");
    ok (session.pressSlot (0, 0), "launch without a grid");
    out = player.process (4096);
    check (onset (out) >= 0 && onset (out) <= 256, "a clip set to None starts within a driver block despite the one-bar global grid");
    ok (session.setClipLaunchSettings (highId, { Mode::trigger, Grid::global, false }), "high clip follows the global grid");
    ok (session.launchSlot (0, 1), "queue the high clip on the global grid");
    // The capture has reached 154496 samples; the next bar starts at 192000.
    out = player.process (40000);
    level (out, 512, 37000, .12f, "a Global clip waits for the next bar");
    level (out, 38000, 40000, .28f, "a Global clip starts on the bar");
    session.stop();
}

void repeatTests (const juce::File& root)
{
    MemoryBuffers buffers;
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    // A single decaying 1 kHz burst at the start of a four-beat clip.
    const auto burst = fixture (root, "burst", [] (int n) { return n < 480 ? (float) (.5 * std::sin (2 * juce::MathConstants<double>::pi * 1000 * n / 48000) * std::exp (-n / 120.0)) : 0.0f; });
    ok (session.importAudioToSlot (burst.file, 0, 0), "import the burst clip");
    registerFixture (buffers, engine, slotClip (session, 0, 0).file, burst.audio);
    const auto id = slotClip (session, 0, 0).id;
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "no global grid");
    ok (session.setClipLaunchSettings (id, { Mode::trigger, Grid::eighth, false }), "trigger on eighth notes");
    ok (session.play(), "start the transport");
    player.process (4800);
    ok (session.pressSlot (0, 0), "trigger the burst clip");
    auto found = bursts (player.process (48000));
    check (found.size() == 1, "a triggered four-beat clip bursts once in two beats: " + list (found));
    const auto latency = found[0] - (beat / 2 - 4800);
    check (std::abs (latency) <= 256, "the trigger starts on the eighth-note boundary (offset " + juce::String (latency) + ")");
    ok (session.stopAllLauncherClips(), "stop the triggered clip"); player.process (12000);
    // 64800 samples; the next eighth note is at 72000.
    ok (session.setClipLaunchSettings (id, { Mode::repeat, Grid::eighth, false }), "repeat on eighth notes");
    ok (session.pressSlot (0, 0), "hold the repeat clip");
    const auto held = player.process (48000);
    found = bursts (held);
    check (found.size() == 4, "a held repeat clip retriggers every eighth note: " + list (found));
    for (size_t i = 0; i < found.size(); ++i)
        check (std::abs (found[i] - (7200 + (int) i * beat / 2) - latency) <= 16, "repeat " + juce::String ((int) i + 1) + " lands on the eighth-note grid");
    // A retrigger on a driver-block boundary (the second one here) must keep the whole transient.
    float transient = 0;
    for (int i = 1; i < 4; ++i)
        for (int n = -64; n < 640; ++n) transient = std::max (transient, std::abs (held.getSample (0, 7200 + i * beat / 2 + n) - held.getSample (0, 7200 + n)));
    check (transient < 1.0e-4f, "every retrigger reproduces the first burst, including one on a block boundary (max difference " + juce::String (transient, 6) + ")");
    ok (session.releaseSlot (0, 0), "release the repeat clip"); // At 112800, stop at 120000.
    auto after = player.process (24000);
    check (after.getMagnitude (0, 7200 + 1000 + std::max (latency, 0), 24000 - 8200 - std::max (latency, 0)) < 1.0e-4f,
           "releasing Repeat stops the clip at the next eighth note");
    ok (session.pressSlot (0, 0), "hold the repeat clip again");
    found = bursts (player.process (24000));
    check (! found.empty(), "the clip can repeat again after release");
    ok (session.launchSlot (0, 0), "an ordinary launch while held ends the repeat");
    player.process (12000);
    found = bursts (player.process (48000));
    check (found.empty(), "after an ordinary launch the clip plays through without repeating: " + list (found));
    ok (session.releaseSlot (0, 0), "releasing after the ordinary launch is ignored");
    check (slot (session, 0, 0).playing, "the clip keeps playing after the stale release");
    session.stop();
}

void legatoTests (const juce::File& root, bool legato)
{
    MemoryBuffers buffers;
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    const auto first = fixture (root, "legato-first", [] (int) { return .12f; });
    const auto second = fixture (root, "legato-second", [] (int n) { return n < 2 * beat ? .1f : .3f; });
    ok (session.importAudioToSlot (first.file, 0, 0), "import the playing clip"); ok (session.importAudioToSlot (second.file, 0, 1), "import the legato clip");
    registerFixture (buffers, engine, slotClip (session, 0, 0).file, first.audio);
    registerFixture (buffers, engine, slotClip (session, 0, 1).file, second.audio);
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "one-beat global grid");
    ok (session.setClipLaunchSettings (slotClip (session, 0, 1).id, { Mode::trigger, Grid::global, legato }), legato ? "enable legato" : "leave legato off");
    ok (session.play(), "start the transport");
    player.process (4800);
    ok (session.pressSlot (0, 0), "launch the first clip"); // Starts at 24000.
    player.process (61200);
    // 66000 samples: the second clip starts at 72000, two beats into the first clip.
    ok (session.pressSlot (0, 1), "launch the second clip");
    const auto out = player.process (24000);
    level (out, 512, 5500, .12f, "the first clip plays until the boundary");
    if (legato) level (out, 8000, 24000, .3f, "a legato clip continues from the first clip's position");
    else level (out, 8000, 24000, .1f, "a non-legato clip starts from its beginning");
    session.stop();
}

void groupSlotTests (const juce::File& root)
{
    MemoryBuffers buffers;
    Session session (false);
    auto& engine = engineFor (session);
    te::test_utilities::EnginePlayer player (engine, parameters());
    // Distinct constant levels make every combination of playing tracks identifiable.
    const auto a = fixture (root, "member-a", [] (int) { return .1f; }), b = fixture (root, "member-b", [] (int) { return .02f; });
    const auto a2 = fixture (root, "member-a2", [] (int) { return .2f; }), outside = fixture (root, "outside", [] (int) { return .4f; });
    ok (session.addAudioTrack ("Outside"), "add an outside track");
    ok (session.importAudioToSlot (a.file, 0, 0), "first member clip"); ok (session.importAudioToSlot (b.file, 1, 0), "second member clip");
    ok (session.importAudioToSlot (a2.file, 0, 1), "first member's second-scene clip"); ok (session.importAudioToSlot (outside.file, 2, 0), "outside clip");
    registerFixture (buffers, engine, slotClip (session, 0, 0).file, a.audio); registerFixture (buffers, engine, slotClip (session, 1, 0).file, b.audio);
    registerFixture (buffers, engine, slotClip (session, 0, 1).file, a2.audio); registerFixture (buffers, engine, slotClip (session, 2, 0).file, outside.audio);
    juce::String group;
    ok (session.groupTracks ({ session.snapshot().tracks[0].id, session.snapshot().tracks[1].id }, group), "group the first two tracks");
    check (session.launchGroupSlot ("missing", 0).failed() && session.launchGroupSlot (group, 99).failed() && session.stopGroup ("missing").failed(),
           "unknown groups and scenes are rejected");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "immediate grid");
    ok (session.play(), "start the transport"); ok (session.launchSlot (2, 0), "start the outside clip");
    player.process (4800);
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "one-beat grid");
    ok (session.launchGroupSlot (group, 0), "launch the group's first slot"); // 4800 samples in, next beat at 24000.
    auto out = player.process (24000);
    level (out, 512, 19000, .4f, "the outside clip plays alone until the boundary");
    level (out, 19800, 24000, .52f, "both member clips start together on the boundary beside the outside clip");
    check (slot (session, 0, 0).playing && slot (session, 1, 0).playing && slot (session, 2, 0).playing, "the members and the outside clip all play");
    ok (session.launchGroupSlot (group, 1), "launch the group's second slot"); // 28800 samples in, next beat at 48000.
    out = player.process (24000);
    level (out, 512, 18800, .52f, "the first group row plays until the boundary");
    level (out, 19800, 24000, .6f, "the second row switches the first member and stops the member without a clip");
    check (slot (session, 0, 1).playing && ! slot (session, 1, 0).playing && slot (session, 2, 0).playing, "only the group's members change");
    ok (session.stopGroup (group), "stop the group"); // 52800 samples in, next beat at 72000.
    out = player.process (24000);
    level (out, 512, 18800, .6f, "the group plays until its stop boundary");
    level (out, 19800, 24000, .4f, "stopping the group leaves the outside clip playing");
    session.stop();
}
}

int runLaunchModeTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("launch-modes-" + juce::Uuid().toString());
        check (root.createDirectory().wasOk(), "create fixtures");
        settingsTests (root);
        modeTests (root);
        repeatTests (root);
        legatoTests (root, false);
        legatoTests (root, true);
        groupSlotTests (root);
        std::cout << "Launch modes passed: " << root.getFullPathName() << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL launch modes: " << e.what() << std::endl; return 1; }
}
}
