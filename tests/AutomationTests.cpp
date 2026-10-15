#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace halftrack
{
namespace
{
namespace te = tracktion;
using Target = Session::AutomationTarget;
using Lane = Session::AutomationLane;
void check (bool condition, const juce::String& message)
{
    if (! condition) throw std::runtime_error (message.toStdString());
    std::cout << "PASS automation: " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{ check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : "")); }
bool equal (const Lane& a, const Lane& b)
{
    if (a.enabled != b.enabled || a.points.size() != b.points.size()) return false;
    for (size_t i = 0; i < a.points.size(); ++i)
        if (std::abs (a.points[i].beat - b.points[i].beat) > 1e-8 || std::abs (a.points[i].value - b.points[i].value) > 2e-5f) return false;
    return true;
}
Lane lane (const Session& s, Target target, int track = 0)
{
    const auto info = s.snapshot().tracks[(size_t) track];
    return target == Target::volume ? info.volumeAutomation : info.panAutomation;
}
juce::File constantFile (const juce::File& root)
{
    const auto file = root.getChildFile (juce::Uuid().toString() + ".wav");
    juce::AudioBuffer<float> pcm (2, 96000);
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (pcm.getWritePointer (c), .125f, pcm.getNumSamples());
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    check (writer && writer->writeFromAudioSampleBuffer (pcm, 0, pcm.getNumSamples()), "write generated constant stereo source");
    return file;
}
juce::AudioBuffer<float> render (Session& s, const juce::File& root, double rate = 48000)
{
    const auto file = root.getChildFile (juce::Uuid().toString() + ".wav");
    ok (s.exportWav (file, rate, 32), "render automation through production export");
    juce::WavAudioFormat format; std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (file.createInputStream().release(), true));
    check (reader && reader->numChannels == 2 && reader->lengthInSamples == (int) std::round (2 * rate), "exact stereo render length");
    juce::AudioBuffer<float> result (2, (int) reader->lengthInSamples);
    check (reader->read (&result, 0, result.getNumSamples(), 0, true, true), "read rendered automation samples");
    for (int c = 0; c < 2; ++c) for (int n = 0; n < result.getNumSamples(); ++n)
        if (! std::isfinite (result.getSample (c, n))) throw std::runtime_error ("Nonfinite automated audio");
    return result;
}
void level (const juce::AudioBuffer<float>& audio, int channel, int first, int count, double expected, const juce::String& message)
{
    double error = 0;
    for (int n = first; n < first + count; ++n) error = std::max (error, std::abs (audio.getSample (channel, n) - expected));
    check (error < 2e-6, message + "; error=" + juce::String (error, 9));
}
void waveformTests (const juce::File& root)
{
    Session s (false); ok (s.importAudio (constantFile (root), 0), "import automation source");
    const auto id = s.snapshot().tracks[0].id;
    ok (s.setTrackGain (0, -6), "set independent manual volume");
    ok (s.setTrackPan (0, .25f), "set independent manual pan");
    const Lane volume { true, { { 0, -12 }, { 1, -12 }, { 1.001, 0 }, { 3, 0 } } };
    const Lane pan { true, { { 0, -1 }, { 1, -1 }, { 1.001, 1 }, { 3, 1 } } };
    ok (s.setTrackAutomation (id, Target::volume, volume), "write volume envelope");
    ok (s.setTrackAutomation (id, Target::pan, pan), "write stereo pan envelope");
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto audio = render (s, root, rate);
        const int early = (int) (.1 * rate), count = (int) (.25 * rate), late = (int) (.8 * rate);
        level (audio, 0, early, count, .125 * std::pow (10.0, -.6) * std::sqrt (2.0), "volume and left pan plateau at chosen rate");
        level (audio, 1, early, count, 0, "right channel is silent at first automated pan");
        level (audio, 0, late, count, 0, "left channel is silent after pan transition");
        level (audio, 1, late, count, .125 * std::sqrt (2.0), "volume and right pan plateau after transition");
    }
    auto disabledVolume = volume; disabledVolume.enabled = false;
    auto disabledPan = pan; disabledPan.enabled = false;
    ok (s.setTrackAutomation (id, Target::volume, disabledVolume), "disable volume reading without deleting points");
    ok (s.setTrackAutomation (id, Target::pan, disabledPan), "disable pan reading without deleting points");
    const auto bypass = render (s, root);
    const double manualGain = .125 * std::pow (10.0, -.3) * std::sqrt (2.0);
    level (bypass, 0, 4800, 80000, manualGain * std::cos (.625 * juce::MathConstants<double>::halfPi), "read bypass restores saved manual volume and left pan");
    level (bypass, 1, 4800, 80000, manualGain * std::sin (.625 * juce::MathConstants<double>::halfPi), "read bypass restores saved manual right pan");
    ok (s.setTrackAutomation (id, Target::volume, {}), "clear volume automation");
    ok (s.setTrackAutomation (id, Target::pan, {}), "clear pan automation");
    const auto cleared = s.snapshot();
    check (std::abs (cleared.tracks[0].gainDb + 6) < 2e-5 && std::abs (cleared.tracks[0].pan - .25f) < 1e-6, "clear restores exact manual fader values");
    ok (s.setTrackGain (0, -3), "manual volume editing returns after clearing");
    ok (s.setTrackPan (0, 0), "manual pan editing returns after clearing");
    s.undo(); s.undo(); s.undo(); s.undo();
    check (equal (lane (s, Target::volume), disabledVolume) && equal (lane (s, Target::pan), disabledPan), "undo manual edits and clears restores independent lane states");
    const auto restored = render (s, root);
    level (restored, 0, 4800, 80000, manualGain * std::cos (.625 * juce::MathConstants<double>::halfPi), "undo retains the original stored manual level");
}
void historyTests (const juce::File& root)
{
    Session s (false); const auto id = s.snapshot().tracks[0].id;
    const Lane first { true, { { 0, -18 }, { 4, 0 } } }, second { true, { { 0, -18 }, { 2, -9 }, { 8, 6 } } };
    ok (s.setTrackGain (0, -4), "set manual level before drawing");
    ok (s.setTrackAutomation (id, Target::volume, first), "draw first lane");
    s.undo(); check (lane (s, Target::volume).points.empty(), "one undo removes complete first envelope");
    check (std::abs (s.snapshot().tracks[0].gainDb + 4) < 2e-5, "first curve undo restores manual baseline");
    s.undo(); check (std::abs (s.snapshot().tracks[0].gainDb) < 2e-5, "undo crosses baseline creation and restores earlier manual fader history");
    s.redo(); check (std::abs (s.snapshot().tracks[0].gainDb + 4) < 2e-5, "redo restores earlier manual fader history");
    s.redo(); check (equal (lane (s, Target::volume), first), "redo reuses the native curve container");
    s.beginEditGesture ("Drag automation point");
    ok (s.setTrackAutomation (id, Target::volume, second), "drag automation intermediate");
    auto final = second; final.points[1].value = -6;
    ok (s.setTrackAutomation (id, Target::volume, final), "drag automation final"); s.endEditGesture();
    s.undo(); check (equal (lane (s, Target::volume), first), "one undo reverts entire drag gesture");
    const auto before = s.snapshot();
    ok (s.setTrackAutomation (id, Target::volume, lane (s, Target::volume)), "snapshot lane reapplied is a no-op");
    check (s.canRedo(), "no-op preserves pending redo");
    for (const Lane bad : { Lane { true, {{0,0},{0,1}} }, Lane { true, {{2,0},{1,1}} }, Lane { true, {{-1,0}} },
        Lane { true, {{1000001,0}} }, Lane { true, {{0,7}} }, Lane { true, {{0,-101}} },
        Lane { true, {{std::numeric_limits<double>::infinity(),0}} }, Lane { true, {{0,std::numeric_limits<float>::quiet_NaN()}} } })
        check (s.setTrackAutomation (id, Target::volume, bad).failed(), "reject malformed envelope atomically");
    check (s.setTrackAutomation (id, static_cast<Target> (99), first).failed()
        && s.setTrackAutomation ("missing", Target::volume, first).failed()
        && s.setTrackAutomation (id, Target::pan, { true, {{0,1.01f}} }).failed(), "reject unsupported target, stale identity and pan range");
    Lane excessive; excessive.points.resize (4097);
    for (size_t i=0; i<excessive.points.size(); ++i) excessive.points[i].beat = (double)i;
    check (s.setTrackAutomation (id, Target::volume, excessive).failed(), "reject point count beyond supported limit");
    check (s.canRedo() && equal (lane (s, Target::volume), first) && s.snapshot().projectGeneration == before.projectGeneration, "invalid edits preserve points, project and redo");
    s.redo(); check (equal (lane (s, Target::volume), final), "redo still applies gesture after rejections");
    check (s.setTrackGain (0, 0).failed(), "manual fader cannot overwrite one or many automation points");
    s.setTempo (60); check (equal (lane (s, Target::volume), final), "tempo change preserves musical beat positions");
    s.undo(); check (s.snapshot().tempo == 120 && equal (lane (s, Target::volume), final), "undo tempo preserves beat envelope");
    s.redo(); check (s.snapshot().tempo == 60 && equal (lane (s, Target::volume), final), "redo tempo preserves beat envelope");
    ok (s.tapTempo (10), "anchor tempo tap"); ok (s.tapTempo (10.25), "apply tap tempo");
    check (std::abs (s.snapshot().tempo - 240) < 1e-6 && equal (lane (s, Target::volume), final), "tap tempo also preserves beat envelope");
    juce::String copy; ok (s.duplicateTrack (id, copy), "duplicate automated track");
    check (equal (lane (s, Target::volume, 1), final), "duplicated track carries its envelope");
    ok (s.moveTrack (id, 2), "move automated track");
    auto single = Lane { true, {{1,-8}} }; ok (s.setTrackAutomation (id, Target::volume, single), "stable track identity follows reordering");
    check (equal (lane (s, Target::volume, 2), single) && equal (lane (s, Target::volume, 0), final), "edit changes original rather than copied row");
    const auto file = root.getChildFile ("automation-history.halftrack"); ok (s.saveProject (file), "save human-readable envelopes");
    ok (s.newProject(), "replace active project"); ok (s.loadProject (file), "reload automation project");
    check (!s.canUndo() && equal (lane (s, Target::volume, 2), single) && equal (lane (s, Target::volume, 0), final), "reload preserves both envelopes with clean history");
    ok (s.setTrackAutomation (id, Target::volume, {}), "clear reloaded single-point lane");
    check (std::abs (s.snapshot().tracks[2].gainDb + 4) < 2e-5, "reload retains original manual fader under automation");
    s.undo(); check (equal (lane (s, Target::volume, 2), single), "undo clear restores single-point automation without native overwrite");
    s.redo(); check (lane (s, Target::volume, 2).points.empty() && std::abs (s.snapshot().tracks[2].gainDb + 4) < 2e-5, "redo clear restores manual level again");
    ok (s.addReturnTrack(), "create automation return");
    const auto returnId = s.snapshot().tracks.back().id;
    ok (s.setTrackAutomation (returnId, Target::pan, {true,{{0,-1},{4,1}}}), "return mixer accepts pan envelopes");
    ok (s.addMidiTrack(), "create automation MIDI track");
    ok (s.setTrackAutomation (s.snapshot().tracks.back().id, Target::volume, first), "MIDI mixer accepts volume envelopes");
}
void malformedLoads (const juce::File& root)
{
    Session s (false); const auto id = s.snapshot().tracks[0].id;
    ok (s.setTrackAutomation (id, Target::volume, {true,{{0,-6},{4,0}}}), "prepare serialized validation fixture");
    const auto file = root.getChildFile ("automation-valid.halftrack"); ok (s.saveProject (file), "save validation fixture");
    auto xml = juce::XmlDocument::parse (file); check (xml != nullptr, "read serialized XML");
    const auto findCurve = [] (auto&& self, juce::XmlElement& node) -> juce::XmlElement*
    {
        if (node.hasTagName ("AUTOMATIONCURVE") && node.getStringAttribute ("paramID") == "volume" && node.getNumChildElements() > 0) return &node;
        for (auto* child : node.getChildIterator()) if (auto* found = self (self, *child)) return found;
        return nullptr;
    };
    for (int mutation = 0; mutation < 20; ++mutation)
    {
        auto bad = std::make_unique<juce::XmlElement> (*xml); auto* curve = findCurve (findCurve, *bad);
        check (curve != nullptr, "locate saved native volume lane"); auto* point = curve->getFirstChildElement();
        if (mutation == 0) point->setAttribute ("t", "nan");
        if (mutation == 1) point->setAttribute ("v", "inf");
        if (mutation == 2) point->setAttribute ("v", "1.1");
        if (mutation == 3) point->setAttribute ("c", ".5");
        if (mutation == 4) curve->getChildElement (1)->setAttribute ("t", "0");
        if (mutation == 5) curve->setAttribute ("bypass", ".5");
        if (mutation == 6) curve->setAttribute ("paramID", "unsupported");
        if (mutation == 7) point->setAttribute ("t", "-1");
        if (mutation == 8) point->setAttribute ("v", "garbage");
        if (mutation == 9) point->setTagName ("UNKNOWN");
        if (mutation == 10) curve->getChildElement (1)->setAttribute ("t", "2000000"); // valid seconds bound, invalid beats at 120 BPM.
        if (mutation == 11) point->setAttribute ("v", "0.000001");
        if (mutation == 12) curve->addChildElement (new juce::XmlElement (*point));
        const auto findOwner = [curve] (auto&& self, juce::XmlElement& node) -> juce::XmlElement*
        {
            for (auto* child : node.getChildIterator())
            {
                if (child == curve) return &node;
                if (auto* owner = self (self, *child)) return owner;
            }
            return nullptr;
        };
        auto* mixer = findOwner (findOwner, *bad); check (mixer != nullptr, "locate mixer owning invalid curve");
        if (mutation == 13) mixer->setAttribute ("halftrackManualVolume", "0.000001");
        if (mutation == 14) mixer->setAttribute ("halftrackManualVolume", "nan");
        if (mutation >= 15)
        {
            juce::MemoryOutputStream parameters;
            parameters.writeString (mutation == 16 ? "unsupported" : "volume");
            if (mutation != 17) parameters.writeFloat (mutation == 15 ? std::numeric_limits<float>::quiet_NaN() : .5f);
            if (mutation == 18) { parameters.writeString ("volume"); parameters.writeFloat (.4f); }
            if (mutation == 19) for (int i=0;i<300;++i) parameters.writeByte (0);
            mixer->setAttribute ("base64:parameters", parameters.getMemoryBlock().toBase64Encoding());
        }
        const auto invalid = root.getChildFile ("automation-invalid-" + juce::String (mutation) + ".halftrack");
        check (bad->writeTo (invalid), "write malformed fixture");
        const auto before = s.snapshot();
        check (s.loadProject (invalid).failed(), "reject malformed serialized automation " + juce::String (mutation));
        check (s.snapshot().projectGeneration == before.projectGeneration && equal (lane (s, Target::volume), before.tracks[0].volumeAutomation), "failed load preserves active project");
    }
}
void playbackModeRegressions (const juce::File& root)
{
    Session s (false); te::Engine* engine = nullptr;
    for (auto* candidate : te::Engine::getEngines())
        if (&candidate->getDeviceManager().deviceManager == &s.audioDeviceManager()) engine = candidate;
    check (engine != nullptr, "find native hosted engine");
    te::HostedAudioDeviceInterface::Parameters parameters; parameters.sampleRate=48000; parameters.blockSize=128;
    parameters.inputChannels=0; parameters.outputChannels=2; parameters.useMidiDevices=false;
    te::test_utilities::EnginePlayer player (*engine, parameters);
    ok (s.importAudio (constantFile (root), 0), "import hosted source");
    const auto id = s.snapshot().tracks[0].id;
    ok (s.setTrackGain (0, -4), "hosted manual volume baseline"); ok (s.setTrackPan (0, -.3f), "hosted manual pan baseline");
    const Lane volume {true,{{0,-18}}}, pan {true,{{0,.7f}}};
    ok (s.setTrackAutomation (id, Target::volume, volume), "one-point volume before duplication");
    ok (s.setTrackAutomation (id, Target::pan, pan), "one-point pan before duplication");
    ok (s.play(), "play single-point automation"); player.process (4096);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30); s.stop();
    juce::String copied; ok (s.duplicateTrack (id, copied), "duplicate a played single-point track");
    const bool initialCopy = equal (lane (s, Target::volume, 1), volume) && equal (lane (s, Target::pan, 1), pan);
    s.undo(); check (s.snapshot().tracks.size() == 2, "single-point duplication is one undo transaction");
    s.redo();
    const bool copies = initialCopy && equal (lane (s, Target::volume, 1), volume) && equal (lane (s, Target::pan, 1), pan);
    std::cout << "Single-point copied volume=" << lane(s,Target::volume,1).points[0].value
              << " pan=" << lane(s,Target::pan,1).points[0].value << std::endl;
    ok (s.setTrackAutomation (copied, Target::volume, {}), "clear copied single-point volume");
    ok (s.setTrackAutomation (copied, Target::pan, {}), "clear copied single-point pan");
    const bool manual = std::abs(s.snapshot().tracks[1].gainDb+4)<2e-5 && std::abs(s.snapshot().tracks[1].pan+.3f)<1e-6;
    ok (s.newProject(), "replace hosted project before manual-fader check");
    ok (s.play(), "play empty arrangement"); player.process (4096);
    ok (s.setTrackGain (0, -6), "move manual fader during playback"); player.process (4096);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30); s.stop(); juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    const bool noWrite = lane (s,Target::volume).points.empty();
    std::cout << "Manual-fader unexpected recorded point count=" << lane(s,Target::volume).points.size() << std::endl;
    check (copies && manual && noWrite, "played single-point copies retain curve and manual values; ordinary faders never write automation");
}
void beatBoundaryTests()
{
    Session s(false); const auto id=s.snapshot().tracks[0].id;
    for(double bpm : {20.0,22.0,999.0})
    {
        s.setTempo(bpm);
        const Lane exact {true,{{0,-100},{1000000,6}}};
        ok(s.setTrackAutomation(id,Target::volume,exact),"boundary tempo accepts legal beat and value endpoints");
        check(equal(lane(s,Target::volume),exact),"boundary tempo retains musical endpoint positions");
    }
    s.setTempo(120);
    const auto before=lane(s,Target::volume);
    const Lane tooClose {true,{{std::nextafter(100.0,0.0),-6},{100.0,0}}};
    check(s.setTrackAutomation(id,Target::volume,tooClose).failed(), "minimum beat spacing prevents later tempo changes collapsing points");
    s.setTempo(22);
    const Lane collapsed {true,{{std::nextafter(100.0,0.0),-6},{100.0,0}}};
    check(s.setTrackAutomation(id,Target::volume,collapsed).failed() && equal(lane(s,Target::volume),before),
        "reject distinct beats that collapse to the same native second before mutation");
}
void playbackPersistence (const juce::File& root)
{
    Session s (false); ok (s.importAudio (constantFile (root), 0), "import playback/history source");
    const auto id = s.snapshot().tracks[0].id;
    ok (s.setTrackGain (0, -4), "set manual baseline before first curve");
    const Lane curve {true,{{0,-18},{4,0}}};
    ok (s.setTrackAutomation (id, Target::volume, curve), "create first curve for dispatched-history regression");
    render (s, root);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    s.undo();
    check (lane (s, Target::volume).points.empty() && std::abs (s.snapshot().tracks[0].gainDb + 4) < 2e-5,
        "first-envelope undo after actual render restores original manual level");
    s.redo(); check (equal (lane (s, Target::volume), curve), "first-envelope redo restores points after playback");
    ok (s.setTrackPan (0, -.3f), "set saved manual pan before automation");
    const Lane pan {true,{{0,1},{4,-1}}}; ok (s.setTrackAutomation (id, Target::pan, pan), "create independently automated pan");
    render (s, root); juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    const auto file = root.getChildFile ("automation-after-playback.halftrack"); ok (s.saveProject (file), "save after automation changed instantaneous mixer values");
    ok (s.loadProject (file), "reload played automation");
    check (equal (lane (s, Target::volume), curve) && equal (lane (s, Target::pan), pan), "save after playback preserves both musical curves");
    ok (s.setTrackAutomation (id, Target::volume, {}), "clear played and reloaded volume lane");
    ok (s.setTrackAutomation (id, Target::pan, {}), "clear played and reloaded pan lane");
    check (std::abs (s.snapshot().tracks[0].gainDb + 4) < 2e-5 && std::abs (s.snapshot().tracks[0].pan + .3f) < 1e-6,
        "played-project reload and clear restore independent manual gain and pan");
    for (bool retainExplicitValues : {true, false})
    {
        auto legacy = juce::XmlDocument::parse (file); check (legacy != nullptr, "read played project for legacy fixture");
        const auto findMixer = [] (auto&& self, juce::XmlElement& node) -> juce::XmlElement*
        {
            if (node.hasAttribute ("halftrackManualVolume") && node.hasAttribute ("halftrackManualPan")) return &node;
            for (auto* child : node.getChildIterator()) if (auto* found = self (self, *child)) return found;
            return nullptr;
        };
        auto* mixer = findMixer (findMixer, *legacy); check (mixer != nullptr, "locate played mixer for migration");
        const double savedVolume = mixer->getDoubleAttribute ("volume"), savedPan = mixer->getDoubleAttribute ("pan");
        mixer->removeAttribute ("halftrackManualVolume"); mixer->removeAttribute ("halftrackManualPan");
        if (! retainExplicitValues) mixer->removeAttribute ("base64:parameters");
        const auto legacyFile = root.getChildFile (retainExplicitValues ? "automation-legacy-explicit.halftrack" : "automation-legacy-fallback.halftrack");
        check (legacy->writeTo (legacyFile), "write generated pre-envelope-editor project");
        ok (s.loadProject (legacyFile), "load legacy native automation without new metadata");
        check (equal (lane (s, Target::volume), curve) && equal (lane (s, Target::pan), pan), "legacy migration preserves musical curves");
        ok (s.setTrackAutomation (id, Target::volume, {}), "clear migrated volume lane");
        ok (s.setTrackAutomation (id, Target::pan, {}), "clear migrated pan lane");
        const auto migrated = s.snapshot().tracks[0];
        const double expectedVolume = retainExplicitValues ? -4 : savedVolume > 0 ? 20*std::log(savedVolume)+6 : -100;
        check (std::abs (migrated.gainDb - expectedVolume) < 2e-5 && std::abs (migrated.pan - (retainExplicitValues ? -.3 : savedPan)) < 1e-6,
            retainExplicitValues ? "legacy migration recovers original explicit manual values" : "legacy migration falls back to saved native values when original manual values are absent");
    }
}
void interpolationTests (const juce::File& root)
{
    // Formula independent of Session's helper: native linear fader-coordinate
    // interpolation is exponential in dB, and is not a linear amplitude ramp.
    for (float db : {-100.f,-60.f,-18.f,-6.f,0.f,6.f})
    {
        const auto expected = db <= -100 ? 0.0 : std::exp ((db - 6.0) / 20.0);
        check (std::abs (Session::automationValueToProportion (Target::volume, db) - expected) < 1e-7, "editor coordinate matches independently computed fader taper");
        check (std::abs (Session::automationProportionToValue (Target::volume, (float) expected) - db) < 2e-5, "fader coordinate round trip retains dB value");
    }
    Session s (false); ok (s.importAudio (constantFile (root), 0), "import ramp source");
    ok (s.setTrackAutomation (s.snapshot().tracks[0].id, Target::volume, {true,{{0,-18},{4,0}}}), "create musical volume ramp");
    const auto audio = render (s, root);
    // Native automation is updated by processing blocks and the mixer smooths
    // gain over 15 ms. Check that every middle sample remains near the analytic
    // curve; this allowance bounds its timing without claiming sample accuracy.
    double error = 0;
    for (int n = 4800; n < 90000; ++n)
    {
        const double fader = std::exp (-24.0/20) + (std::exp (-6.0/20) - std::exp (-24.0/20)) * n / 96000.0;
        const double expected = .125 * std::pow (10.0, (20 * std::log (fader) + 6) / 20);
        error = std::max (error, std::abs (audio.getSample (0,n) - expected));
    }
    check (error < .002, "rendered ramp follows native taper within block/smoothing bound; error=" + juce::String (error, 7));
}
}
int runAutomationTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("automation-" + juce::Uuid().toString()); ok (root.createDirectory(), "create isolated test directory");
        historyTests (root); malformedLoads (root); waveformTests (root); playbackPersistence (root); interpolationTests (root); beatBoundaryTests(); playbackModeRegressions (root); return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL automation: " << error.what() << std::endl; return 1; }
}
}
