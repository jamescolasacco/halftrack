#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace halftrack
{
namespace
{
using Mode = Session::SendMode;
void check (bool value, const juce::String& message)
{
    if (! value) throw std::runtime_error (message.toStdString());
    std::cout << "PASS sends: " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{ check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : "")); }
float db (double linear) { return linear == 0 ? -100.0f : static_cast<float> (20.0 * std::log10 (linear)); }
juce::AudioBuffer<float> read (const juce::File& file, int frames)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader && reader->sampleRate == 48000 && reader->numChannels == 2 && reader->lengthInSamples == frames,
           "complete stereo render dimensions");
    juce::AudioBuffer<float> audio (2, frames);
    check (reader->read (&audio, 0, frames, 0, true, true), "decode complete signal");
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i)
            if (! std::isfinite (audio.getSample (c, i))) throw std::runtime_error ("non-finite decoded sample");
    return audio;
}
struct Fixture
{
    juce::File file;
    juce::AudioBuffer<float> audio;
};
Fixture fixture (const juce::File& root, const juce::String& name, int seed, int frames = 48000)
{
    Fixture f { root.getChildFile (name + ".wav"), juce::AudioBuffer<float> (2, frames) };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i)
            f.audio.setSample (c, i, static_cast<float> ((i * (37 + seed) + c * 13 + seed) % 97 - 48) / 1024.0f);
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (f.file.createOutputStream());
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
        check (writer && writer->writeFromAudioSampleBuffer (f.audio, 0, frames), "write independent source " + name);
    }
    f.audio = read (f.file, frames);
    return f;
}
juce::AudioBuffer<float> mix (const Fixture& a, const Fixture& b, double al, double ar, double bl = 0, double br = 0)
{
    check (a.audio.getNumSamples() == b.audio.getNumSamples(), "oracle source lengths agree");
    juce::AudioBuffer<float> result (2, a.audio.getNumSamples());
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < result.getNumSamples(); ++i)
            result.setSample (c, i, static_cast<float> (a.audio.getSample (c, i) * (c == 0 ? al : ar)
                                                      + b.audio.getSample (c, i) * (c == 0 ? bl : br)));
    return result;
}
void render (Session& session, const juce::File& root, const juce::String& name, const juce::AudioBuffer<float>& expected)
{
    const auto file = root.getChildFile (name + ".wav");
    ok (session.exportWav (file), "render " + name);
    const auto actual = read (file, expected.getNumSamples());
    float maximum = 0;
    bool finite = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < expected.getNumSamples(); ++i)
        {
            const auto value = actual.getSample (c, i), wanted = expected.getSample (c, i);
            finite = finite && std::isfinite (value) && std::isfinite (wanted);
            maximum = std::max (maximum, std::abs (value - wanted));
        }
    check (finite && maximum < 0.000002f, name + " all samples match, max error " + juce::String (maximum, 9));
}
float sendGain (const Session::TrackInfo& track, const juce::String& destination)
{
    for (const auto& send : track.sends) if (send.returnTrackId == destination) return send.gainDb;
    throw std::runtime_error ("send destination missing from snapshot");
}
juce::PluginDescription plugin (Session& session, const char* variable)
{
    const auto* path = std::getenv (variable);
    check (path != nullptr && juce::File (path).exists(), "controlled plug-in fixture is configured");
    juce::OwnedArray<juce::PluginDescription> descriptions;
    for (auto* format : session.pluginFormats().getFormats())
        if (format->getName() == "VST3") format->findAllTypesForFile (descriptions, juce::File (path).getFullPathName());
    check (descriptions.size() == 1, "identify only the generated VST3 fixture");
    return *descriptions[0];
}
void routing (const juce::File& root, const Fixture& a, const Fixture& b)
{
    Session s (false);
    ok (s.importAudio (a.file, 0), "import first source"); ok (s.importAudio (b.file, 1), "import second source");
    ok (s.addReturnTrack ("Shared room"), "create return");
    auto state = s.snapshot();
    const auto returnID = state.tracks[2].id;
    check (state.tracks[2].isReturn && ! state.tracks[2].isMidi && state.tracks[2].sendMode == Mode::post
           && state.tracks[2].sends.empty() && sendGain (state.tracks[0], returnID) == -100,
           "return defaults to Post, incoming sends Off, no outgoing sends");
    render (s, root, "off", mix (a, b, 1, 1, 1, 1));
    check (s.setTrackSendGain (0, returnID, 0.01f).failed()
           && s.setTrackSendGain (0, returnID, -101.0f).failed()
           && s.setTrackSendGain (0, returnID, std::numeric_limits<float>::quiet_NaN()).failed(), "reject invalid user send levels");
    ok (s.setTrackSendGain (0, returnID, 0), "unity send");
    render (s, root, "unity-send", mix (a, b, 2, 2, 1, 1));
    const auto unity = root.getChildFile ("Unity.halftrack");
    ok (s.saveProject (unity), "save exact unity send");
    ok (s.loadProject (unity), "reopen native float unity boundary");
    render (s, root, "reopened-unity-send", mix (a, b, 2, 2, 1, 1));
    ok (s.setTrackSendGain (0, returnID, db (0.25)), "quarter send");
    render (s, root, "quarter-send", mix (a, b, 1.25, 1.25, 1, 1));
    ok (s.setTrackGain (0, db (0.5)), "half source fader");
    render (s, root, "post-fader", mix (a, b, 0.625, 0.625, 1, 1));
    ok (s.setReturnSendMode (returnID, Mode::pre), "select Pre on destination");
    render (s, root, "pre-fader", mix (a, b, 0.75, 0.75, 1, 1));
    ok (s.setTrackPan (0, -1), "pan source left");
    const double left = std::sqrt (2.0);
    render (s, root, "pre-pan", mix (a, b, left * 0.5 + 0.25, 0.25, 1, 1));
    ok (s.setReturnSendMode (returnID, Mode::post), "select Post after pan");
    render (s, root, "post-pan", mix (a, b, left * 0.625, 0, 1, 1));
    ok (s.setTrackMute (0, true), "mute source with Post send");
    render (s, root, "post-source-mute", mix (a, b, 0, 0, 1, 1));
    ok (s.setReturnSendMode (returnID, Mode::pre), "Pre ignores source activator");
    render (s, root, "pre-source-mute", mix (a, b, 0.25, 0.25, 1, 1));
    ok (s.setTrackMute (2, true), "mute return output");
    render (s, root, "return-mute", mix (a, b, 0, 0, 1, 1));
    s.undo(); render (s, root, "undo-return-mute", mix (a, b, 0.25, 0.25, 1, 1));
    s.redo(); render (s, root, "redo-return-mute", mix (a, b, 0, 0, 1, 1));
    s.undo();
    ok (s.setTrackMute (0, false), "unmute source"); ok (s.setTrackGain (0, 0), "restore fader"); ok (s.setTrackPan (0, 0), "restore center");
    ok (s.setTrackSendGain (1, returnID, db (0.5)), "second source feeds shared return");
    render (s, root, "two-sources", mix (a, b, 1.25, 1.25, 1.5, 1.5));
    ok (s.setTrackSolo (0, true), "solo first source");
    render (s, root, "source-solo", mix (a, b, 1.25, 1.25));
    ok (s.setTrackSolo (2, true), "also solo return under documented combined-solo default");
    render (s, root, "combined-source-return-solo", mix (a, b, 1.25, 1.25, 0.5, 0.5));
    ok (s.setTrackSolo (0, false), "leave only return soloed");
    render (s, root, "return-solo", mix (a, b, 0.25, 0.25, 0.5, 0.5));
    ok (s.setTrackMute (0, true), "explicit mute remains independent from solo");
    render (s, root, "return-solo-pre-muted-source", mix (a, b, 0.25, 0.25, 0.5, 0.5));
    ok (s.setReturnSendMode (returnID, Mode::post), "Post blocks explicitly muted source during return solo");
    render (s, root, "return-solo-post-muted-source", mix (a, b, 0, 0, 0.5, 0.5));
    ok (s.setTrackSolo (2, false), "clear return solo"); ok (s.setTrackMute (0, false), "clear source mute");
    ok (s.addReturnTrack ("Second return"), "create independent return");
    const auto secondID = s.snapshot().tracks[3].id;
    ok (s.setTrackSendGain (0, secondID, db (0.5)), "send to second destination");
    ok (s.setTrackGain (3, db (0.5)), "half second return fader");
    render (s, root, "two-returns", mix (a, b, 1.5, 1.5, 1.5, 1.5));
    const auto gain = plugin (s, "HALFTRACK_TEST_PLUGIN");
    ok (s.addPlugin (0, gain), "source effect precedes send taps");
    ok (s.addPlugin (2, gain), "return effect follows aux input");
    ok (s.setPluginParameter (2, 0, 0, 0.25f), "quarter return processing");
    render (s, root, "source-and-return-effects", mix (a, b, 0.65625, 0.65625, 1.125, 1.125));
    ok (s.setReturnSendMode (returnID, Mode::pre), "Pre still follows source effects");
    render (s, root, "pre-is-post-effects", mix (a, b, 0.65625, 0.65625, 1.125, 1.125));
    ok (s.setPluginEnabled (2, 0, false), "bypass actual return processor");
    render (s, root, "return-effect-bypass", mix (a, b, 0.75, 0.75, 1.5, 1.5));
    check (s.setTrackSendGain (2, secondID, 0).failed() && s.setTrackSendGain (2, returnID, 0).failed(), "reject return-origin sends including self feedback");
    check (s.importAudio (a.file, 2).failed() && s.importAudioToSlot (a.file, 2, 0).failed()
           && s.addMidiClip (returnID, 0, 1).failed() && s.addMidiClipToSlot (returnID, 0).failed()
           && s.setTrackArmed (2, true).failed() && s.setTrackInput (2, "").failed()
           && s.launchSlot (2, 0).failed(), "return rejects clips, recording inputs, and launcher commands");
}
void history (const juce::File& root, const Fixture& a, const Fixture& b)
{
    {
        Session pristine (false);
        check (! pristine.canUndo(), "pristine return test has no undo history");
        ok (pristine.addReturnTrack ("First return"), "create return in pristine project");
        const auto id = pristine.snapshot().tracks[2].id;
        pristine.undo();
        check (pristine.snapshot().tracks.size() == 2 && ! pristine.canUndo(), "one undo removes all pristine return creation actions");
        pristine.redo();
        check (pristine.snapshot().tracks[2].id == id, "pristine return redo preserves identity");
    }
    Session s (false); ok (s.importAudio (a.file, 0), "history source");
    ok (s.addReturnTrack ("History return"), "undoable return creation");
    const auto id = s.snapshot().tracks[2].id;
    s.undo(); check (s.snapshot().tracks.size() == 2, "undo removes new return");
    s.redo(); check (s.snapshot().tracks[2].id == id, "redo preserves return identity");
    s.beginEditGesture ("Send drag");
    ok (s.setTrackSendGain (0, id, db (0.1)), "send gesture begins");
    ok (s.setTrackSendGain (0, id, db (0.2)), "send gesture advances");
    ok (s.setTrackSendGain (0, id, db (0.25)), "send gesture ends");
    s.endEditGesture();
    render (s, root, "gesture-final", mix (a, b, 1.25, 1.25));
    s.undo();
    render (s, root, "one-undo-entire-send-gesture", mix (a, b, 1, 1));
    check (sendGain (s.snapshot().tracks[0], id) == -100, "one gesture undo restores Off");
    s.redo(); render (s, root, "redo-send-gesture", mix (a, b, 1.25, 1.25));
    ok (s.setTrackGain (0, db (0.5)), "history half fader");
    ok (s.setReturnSendMode (id, Mode::pre), "undoable send tap change");
    render (s, root, "history-pre", mix (a, b, 0.75, 0.75));
    s.undo(); render (s, root, "undo-tap", mix (a, b, 0.625, 0.625));
    s.redo(); render (s, root, "redo-tap", mix (a, b, 0.75, 0.75));
    ok (s.renameTrack (2, "Changed return"), "undoable return rename");
    s.undo(); check (s.snapshot().tracks[2].name == "History return", "undo return rename");
    s.redo(); check (s.snapshot().tracks[2].name == "Changed return", "redo return rename");
    const auto gain = plugin (s, "HALFTRACK_TEST_PLUGIN");
    ok (s.addPlugin (2, gain), "history return effect");
    render (s, root, "before-return-delete", mix (a, b, 0.625, 0.625));
    ok (s.removeTrack (2), "delete return and all incoming sends together");
    check (s.snapshot().tracks[0].sends.empty(), "no orphan send in snapshot");
    render (s, root, "deleted-return", mix (a, b, 0.5, 0.5));
    ok (s.addReturnTrack ("Replacement"), "create replacement without ghost feed");
    const auto replacement = s.snapshot().tracks[2].id;
    check (replacement != id && sendGain (s.snapshot().tracks[0], replacement) == -100, "replacement has fresh stable identity and Off send");
    render (s, root, "replacement-no-ghost", mix (a, b, 0.5, 0.5));
    s.undo(); s.undo();
    check (s.snapshot().tracks[2].id == id && s.snapshot().tracks[2].plugins.size() == 1, "undo deletion restores original return and effect");
    render (s, root, "undo-return-delete", mix (a, b, 0.625, 0.625));
    s.redo(); render (s, root, "redo-return-delete", mix (a, b, 0.5, 0.5));
}
juce::XmlElement* findID (juce::XmlElement& node, const juce::String& id)
{
    if (node.getStringAttribute ("id") == id) return &node;
    for (auto* child : node.getChildIterator()) if (auto* found = findID (*child, id)) return found;
    return nullptr;
}
juce::XmlElement* aux (juce::XmlElement& track, const juce::String& type)
{
    for (auto* child : track.getChildIterator()) if (child->hasTagName ("PLUGIN") && child->getStringAttribute ("type") == type) return child;
    throw std::runtime_error ("serialized aux fixture node missing");
}
void persistence (const juce::File& root, const Fixture& a, const Fixture& b)
{
    Session s (false); ok (s.importAudio (a.file, 0), "portable first source"); ok (s.importAudio (b.file, 1), "portable second source");
    ok (s.addReturnTrack ("Portable A"), "first portable return"); ok (s.addReturnTrack ("Portable B"), "second portable return");
    const auto firstID = s.snapshot().tracks[2].id, secondID = s.snapshot().tracks[3].id;
    ok (s.setTrackSendGain (0, firstID, db (0.25)), "persist first send");
    ok (s.setTrackSendGain (1, secondID, db (0.5)), "persist second send");
    ok (s.setReturnSendMode (firstID, Mode::pre), "persist Pre mode");
    ok (s.setTrackMute (0, true), "persist source activator"); ok (s.setTrackSolo (2, true), "persist return solo");
    const auto original = root.getChildFile ("Original").getChildFile ("Routing.halftrack");
    ok (s.saveProject (original), "save routing project");
    const auto moved = root.getChildFile ("Moved");
    check (original.getParentDirectory().copyDirectoryTo (moved), "move complete routing project and media");
    ok (s.newProject(), "clear routing graph");
    check (original.getParentDirectory().deleteRecursively(), "remove original project location");
    const auto saved = moved.getChildFile ("Routing.halftrack");
    ok (s.loadProject (saved), "reopen relocated routing project");
    const auto state = s.snapshot();
    check (state.tracks[2].id == firstID && state.tracks[3].id == secondID && state.tracks[2].sendMode == Mode::pre
           && state.tracks[0].mute && state.tracks[2].solo && state.tracks[0].clips[0].file.isAChildOf (moved)
           && ! state.dirty && ! s.canUndo(), "stable routing identities, mute/solo, relative media, and clean history reopen");
    render (s, root, "portable-solo-pre", mix (a, b, 0.25, 0.25));
    const auto sourceID = state.tracks[0].id;
    const auto oldName = state.tracks[0].name;
    ok (s.renameTrack (0, "Keep this unsaved edit"), "prepare load preservation probe");
    const auto text = saved.loadFileAsString();
    int count = 0;
    juce::XmlElement* invalidDocument = nullptr;
    auto invalid = [&] (const juce::String& name, std::function<void(juce::XmlElement&, juce::XmlElement&, juce::XmlElement&)> mutate)
    {
        auto xml = juce::XmlDocument::parse (text);
        check (xml != nullptr, "parse original routing document");
        auto* source = findID (*xml, sourceID); auto* first = findID (*xml, firstID); auto* second = findID (*xml, secondID);
        check (source && first && second, "find routing fixture tracks");
        invalidDocument = xml.get();
        mutate (*source, *first, *second);
        const auto file = saved.getSiblingFile ("invalid-routing-" + juce::String (++count) + ".halftrack");
        check (xml->writeTo (file), "write malformed " + name);
        check (s.loadProject (file).failed(), "reject malformed " + name);
        auto kept = s.snapshot();
        check (kept.tracks[0].id == sourceID && kept.tracks[0].name == "Keep this unsaved edit" && kept.dirty && s.canUndo(), "invalid route load preserves active unsaved graph");
        s.undo(); check (s.snapshot().tracks[0].name == oldName, "failed route load preserves real undo action");
        s.redo(); check (s.snapshot().tracks[0].name == "Keep this unsaved edit", "failed route load preserves real redo action");
    };
    invalid ("noncanonical return identity", [] (auto& source, auto& first, auto&)
    {
        first.setAttribute ("id", "bad-id"); aux (source, "auxsend")->setAttribute ("halftrackSendReturnID", "bad-id");
    });
    invalid ("aliased return recording target", [&] (auto&, auto& first, auto&)
    {
        auto* device = invalidDocument->getChildByName ("EDIT")->getChildByName ("INPUTDEVICES")->createNewChildElement ("INPUTDEVICE");
        device->setAttribute ("deviceID", "generated-routing-input");
        auto* input = device->createNewChildElement ("INPUTDEVICEDESTINATION");
        input->setAttribute ("targetID", "0" + first.getStringAttribute ("id"));
    });
    invalid ("return output routed back to source", [] (auto&, auto& first, auto&)
    {
        first.getChildByName ("OUTPUTDEVICES")->getChildByName ("DEVICE")->setAttribute ("name", "track 1");
    });
    invalid ("return identity collides with ordinary track", [] (auto& source, auto& first, auto&)
    {
        first.setAttribute ("id", source.getStringAttribute ("id"));
        aux (source, "auxsend")->setAttribute ("halftrackSendReturnID", source.getStringAttribute ("id"));
    });
    invalid ("aliased return slot recording target", [&] (auto&, auto& first, auto&)
    {
        auto* slot = first.getChildByName ("CLIPSLOTS")->getChildByName ("CLIPSLOT");
        const auto id = slot->getStringAttribute ("id");
        slot->setAttribute ("id", "0" + id);
        auto* device = invalidDocument->getChildByName ("EDIT")->getChildByName ("INPUTDEVICES")->createNewChildElement ("INPUTDEVICE");
        device->setAttribute ("deviceID", "generated-routing-input");
        device->createNewChildElement ("INPUTDEVICEDESTINATION")->setAttribute ("targetID", id);
    });
    invalid ("return virtual output feeds source", [&] (auto& source, auto& first, auto&)
    {
        auto* device = invalidDocument->getChildByName ("EDIT")->getChildByName ("INPUTDEVICES")->createNewChildElement ("INPUTDEVICE");
        device->setAttribute ("sourceTrack", first.getStringAttribute ("id"));
        device->setAttribute ("type", "audio");
        device->createNewChildElement ("INPUTDEVICEDESTINATION")->setAttribute ("targetID", source.getStringAttribute ("id"));
    });
    invalid ("missing destination", [] (auto& source, auto&, auto&) { aux (source, "auxsend")->setAttribute ("halftrackSendReturnID", "missing"); });
    invalid ("duplicate bus", [] (auto&, auto& first, auto& second)
    {
        const auto bus = first.getIntAttribute ("halftrackReturnBus");
        second.setAttribute ("halftrackReturnBus", bus); aux (second, "auxreturn")->setAttribute ("busNum", bus);
    });
    invalid ("wrong native bus", [] (auto& source, auto&, auto&) { aux (source, "auxsend")->setAttribute ("busNum", 999); });
    invalid ("return-origin route", [] (auto& source, auto& first, auto&)
    {
        auto copy = std::make_unique<juce::XmlElement> (*aux (source, "auxsend")); copy->setAttribute ("id", "99999981"); first.addChildElement (copy.release());
    });
    invalid ("duplicate send", [] (auto& source, auto&, auto&)
    {
        auto copy = std::make_unique<juce::XmlElement> (*aux (source, "auxsend")); copy->setAttribute ("id", "99999982"); source.addChildElement (copy.release());
    });
    invalid ("invalid mode", [] (auto&, auto& first, auto&) { first.setAttribute ("halftrackReturnSendMode", 99); });
    invalid ("non-finite send gain", [] (auto& source, auto&, auto&) { aux (source, "auxsend")->setAttribute ("auxSendSliderPos", "nan"); });
    invalid ("return clip", [] (auto& source, auto& first, auto&)
    {
        auto* clip = source.getChildByName ("AUDIOCLIP"); check (clip != nullptr, "find clip for invalid return fixture");
        auto copy = std::make_unique<juce::XmlElement> (*clip); copy->setAttribute ("id", "99999983"); first.addChildElement (copy.release());
    });
    render (s, root, "after-invalid-loads", mix (a, b, 0.25, 0.25));
}
void latency (const juce::File& root, const Fixture& a, const Fixture& b, const juce::String& prefix)
{
    Session s (false); ok (s.importAudio (a.file, 0), "aux PDC first source"); ok (s.importAudio (b.file, 1), "aux PDC second source");
    ok (s.addReturnTrack ("Compensated return"), "aux PDC return");
    const auto id = s.snapshot().tracks[2].id;
    ok (s.setTrackSendGain (0, id, db (0.5)), "aux PDC first send"); ok (s.setTrackSendGain (1, id, db (0.25)), "aux PDC second send");
    const auto delay = plugin (s, "HALFTRACK_TEST_LATENCY_PLUGIN");
    const auto expected = mix (a, b, 1.5, 1.5, 1.25, 1.25);
    ok (s.addPlugin (2, delay), "127-sample latency on return");
    render (s, root, prefix + "return-latency", expected);
    ok (s.addPlugin (0, delay), "one delayed source enters same return as dry source");
    render (s, root, prefix + "unequal-send-latencies", expected);
    ok (s.setReturnSendMode (id, Mode::pre), "move compensated sends to Pre");
    render (s, root, prefix + "pre-unequal-latencies", expected);
    ok (s.setPluginEnabled (2, 0, false), "bypass return latency processor");
    render (s, root, prefix + "bypassed-return-latency", expected);
}
void midi (const juce::File& root)
{
    Session s (false); ok (s.addMidiTrack ("Synth source"), "MIDI send source");
    const auto id = s.snapshot().tracks[2].id;
    ok (s.addMidiClip (id, 0, 4), "arranged MIDI source");
    const auto clip = s.snapshot().tracks[2].clips[0].id;
    ok (s.addMidiNote (clip, 60, 0, 2, 0.8f), "sustained synth source note");
    const auto dry = root.getChildFile ("midi-dry.wav"); ok (s.exportWav (dry), "capture deterministic synth baseline");
    auto expected = read (dry, 96000);
    check (expected.getMagnitude (0, expected.getNumSamples()) > 0.01f, "MIDI oracle has actual audio");
    expected.applyGain (1.5f);
    ok (s.addReturnTrack ("Synth return"), "MIDI return");
    ok (s.setTrackSendGain (2, s.snapshot().tracks[3].id, db (0.5)), "post-instrument send");
    render (s, root, "midi-through-return", expected);
    const auto returned = s.snapshot().tracks[3].id;
    expected.applyGain (1.0f / 3.0f);
    ok (s.setTrackSolo (3, true), "MIDI return solo");
    render (s, root, "midi-return-solo", expected);
    ok (s.setReturnSendMode (returned, Mode::pre), "MIDI Pre send");
    ok (s.setTrackMute (2, true), "MIDI muted source keeps Pre feed");
    render (s, root, "midi-muted-pre-return", expected);
    ok (s.setReturnSendMode (returned, Mode::post), "MIDI Post send follows source mute");
    expected.clear();
    render (s, root, "midi-muted-post-return", expected);
}
void runningGraph (const juce::File& root)
{
    namespace te = tracktion;
    Session s (false);
    te::Engine* engine = nullptr;
    for (auto* candidate : te::Engine::getEngines())
        if (&candidate->getDeviceManager().deviceManager == &s.audioDeviceManager()) engine = candidate;
    check (engine != nullptr, "locate controlled running graph");
    const auto file = root.getChildFile ("continuous-source.wav");
    juce::AudioBuffer<float> source (2, 48000);
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (source.getWritePointer (c), 0.125f, 48000);
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
        check (writer && writer->writeFromAudioSampleBuffer (source, 0, 48000), "write continuous known signal");
    }
    ok (s.importAudio (file, 1), "running graph source");
    auto memory = std::make_unique<choc::buffer::InterleavedBuffer<float>> (2, 48000);
    for (unsigned int c = 0; c < 2; ++c)
        for (unsigned int i = 0; i < 48000; ++i) memory->getSample (c, i) = 0.125f;
    engine->getAudioFileManager().registerMemoryBuffer (s.snapshot().tracks[1].clips[0].file.getFullPathName().toStdString(), memory->getView(), 48000);
    te::HostedAudioDeviceInterface::Parameters parameters;
    parameters.sampleRate = 48000; parameters.blockSize = 32; parameters.inputChannels = 0;
    parameters.outputChannels = 2; parameters.useMidiDevices = false;
    te::test_utilities::EnginePlayer player (*engine, parameters);
    auto publish = [&]
    {
        for (auto* edit : engine->getActiveEdits().getEdits()) edit->dispatchPendingUpdatesSynchronously();
    };
    auto level = [&] (const juce::AudioBuffer<float>& buffer, int start, int length, float wanted, const juce::String& label)
    {
        check (buffer.getNumChannels() == 2 && start >= 0 && length > 0 && start + length <= buffer.getNumSamples(), label + " dimensions");
        float error = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto value = buffer.getSample (c, i);
                if (! std::isfinite (value)) throw std::runtime_error ("non-finite running routing sample");
                if (i >= start && i < start + length) error = std::max (error, std::abs (value - wanted));
            }
        check (error < 0.000002f, label + " max error " + juce::String (error, 9));
    };
    ok (s.addReturnTrack ("Running return"), "running return");
    const auto id = s.snapshot().tracks[2].id;
    ok (s.setReturnSendMode (id, Mode::pre), "running Pre tap");
    ok (s.setTrackSendGain (1, id, db (0.5)), "running half send");
    ok (s.setTrackGain (1, -100), "dry fader silent while Pre feeds return");
    ok (s.addPlugin (2, plugin (s, "HALFTRACK_TEST_LATENCY_PLUGIN")), "controlled return with 127-sample stored tail");
    publish(); ok (s.play(), "start synthetic callback graph");
    level (player.process (1024), 512, 512, 0.0625f, "running return plateau");
    ok (s.setTrackSolo (0, true), "solo an empty ordinary track with no send to this return");
    publish();
    const auto tail = player.process (256);
    level (tail, 0, 127, 0.0625f, "source solo preserves existing return tail independent of active feeds");
    level (tail, 127, 129, 0, "tail ends after exact fixture delay");
    ok (s.setTrackSolo (0, false), "release unrelated solo"); publish();
    level (player.process (1024), 512, 512, 0.0625f, "native graph restores source feed");
    ok (s.setTrackMute (1, true), "running source explicit mute"); publish();
    level (player.process (1024), 512, 512, 0.0625f, "running muted source retains Pre send");
    ok (s.setReturnSendMode (id, Mode::post), "running switch to Post"); publish();
    level (player.process (1024), 512, 512, 0, "running Post blocks muted source");
    s.undo(); publish();
    level (player.process (1024), 512, 512, 0.0625f, "running undo restores Pre graph");
    ok (s.removeTrack (2), "running delete return"); publish();
    level (player.process (1024), 512, 512, 0, "running deletion removes bus");
    s.undo(); publish();
    level (player.process (1024), 512, 512, 0.0625f, "running undo restores return and feed");
    s.stop();
}
}
int runSendReturnTests (const juce::File& destination)
{
    try
    {
        const auto root = destination.getChildFile ("sends-" + juce::Uuid().toString());
        ok (root.createDirectory(), "create private send/return artifacts");
        const auto a = fixture (root, "source-a", 1), b = fixture (root, "source-b", 9);
        persistence (root, a, b);
        history (root, a, b);
        runningGraph (root);
        routing (root, a, b);
        latency (root, a, b, "full-");
        const auto shortA = fixture (root, "short-a", 1, 64), shortB = fixture (root, "short-b", 9, 64);
        latency (root, shortA, shortB, "64-frame-");
        midi (root);
        std::cout << "Send/return acceptance passed. Artifacts: " << root.getFullPathName() << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL sends: " << error.what() << std::endl;
        return 1;
    }
}
}
