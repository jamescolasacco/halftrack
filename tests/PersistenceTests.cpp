#include "Session.h"
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace halftrack
{
namespace
{
void check (bool condition, const juce::String& description)
{
    if (! condition) throw std::runtime_error (description.toStdString());
    std::cout << "PASS persistence: " << description << std::endl;
}
void ok (const juce::Result& result, const juce::String& description)
{
    check (result.wasOk(), description + (result.failed() ? ": " + result.getErrorMessage() : ""));
}
bool near (double actual, double expected) { return std::abs (actual - expected) < 0.001; }
void saveAsPreservesHistory (const juce::File& root)
{
    const auto source = root.getChildFile ("save-history-source.wav");
    {
        juce::AudioBuffer<float> signal (2, 4800);
        for (int sample = 0; sample < signal.getNumSamples(); ++sample)
            for (int channel = 0; channel < 2; ++channel)
                signal.setSample (channel, sample, ((sample + channel) % 2 == 0 ? 0.125f : -0.125f));
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
        check (writer && writer->writeFromAudioSampleBuffer (signal, 0, signal.getNumSamples()), "write original save-history audio");
    }
    juce::MemoryBlock expectedAudio;
    check (source.loadFileAsData (expectedAudio), "retain expected portable media bytes");
    Session session (false);
    ok (session.importAudio (source, 0), "import arrangement audio for save history");
    ok (session.importAudioToSlot (source, 1, 0), "import launcher audio for save history");
    ok (session.addMidiTrack ("Save history notes"), "create mixed-session MIDI track");
    const auto trackId = session.snapshot().tracks.back().id;
    ok (session.addMidiClip (trackId, 0, 4), "create mixed-session MIDI clip");
    const auto clipId = session.snapshot().tracks.back().clips.front().id;
    ok (session.addMidiNote (clipId, 60, 0, 1, 0.5f), "create mixed-session note");
    const auto firstDirectory = root.getChildFile ("Original folder");
    const auto first = firstDirectory.getChildFile ("Original.halftrack");
    ok (session.saveProject (first), "collect mixed-session media before history check");
    ok (session.loadProject (first), "begin clean mixed-session undo history");
    const auto note = session.snapshot().tracks.back().clips.front().notes.front();
    ok (session.updateMidiNote (clipId, note.id, 67, 0.5, 1.5, 0.75f), "prepare note edit before Save As");
    session.undo();
    check (session.canRedo() && ! session.canUndo(), "note redo is the only pending history action");
    const auto target = root.getChildFile ("New folder").getChildFile ("Renamed.halftrack");
    ok (session.saveProject (target), "Save As changes both project folder and media location");
    check (! session.snapshot().dirty && session.canRedo() && ! session.canUndo(),
           "successful mixed-session Save As preserves pending redo without adding an undo action");
    const auto assets = target.getSiblingFile ("Renamed Assets");
    const auto saved = session.snapshot();
    const auto arrangementId = saved.tracks[0].clips.front().id;
    const auto slotClipId = saved.tracks[1].slots[0].clip.id;
    const auto checkMedia = [&] (const Session::Snapshot& snapshot)
    {
        const auto& arrangement = snapshot.tracks[0].clips.front();
        const auto& slot = snapshot.tracks[1].slots[0].clip;
        check (arrangement.id == arrangementId && slot.id == slotClipId
               && arrangement.file.isAChildOf (assets) && slot.file.isAChildOf (assets),
               "arrangement and launcher identities resolve to new project-owned media");
        juce::MemoryBlock arrangementBytes, slotBytes;
        check (arrangement.file.loadFileAsData (arrangementBytes) && slot.file.loadFileAsData (slotBytes)
               && arrangementBytes == expectedAudio && slotBytes == expectedAudio,
               "both relocated media sources preserve every original byte");
    };
    checkMedia (saved);
    session.redo();
    auto edited = session.snapshot().tracks.back().clips.front().notes.front();
    check (edited.id == note.id && edited.pitch == 67 && near (edited.startBeat, 0.5) && near (edited.lengthBeats, 1.5),
           "actual note redo survives Save As media relinking");
    session.undo();
    check (session.snapshot().tracks.back().clips.front().notes.front().pitch == 60 && ! session.canUndo(),
           "undo after Save As reverses the note rather than media bookkeeping");
    ok (session.saveProject (target), "repeat save with a pending redo");
    check (session.canRedo() && ! session.canUndo() && ! session.snapshot().dirty, "unchanged media save retains history and clean state");
    session.redo();
    ok (session.saveProject (target), "save redone musical edit");
    check (session.canUndo(), "saving an applied note edit retains its undo");
    session.undo();
    check (session.snapshot().tracks.back().clips.front().notes.front().pitch == 60, "saved applied edit still undoes in one step");
    session.redo();
    checkMedia (session.snapshot());
    check (firstDirectory.deleteRecursively() && source.deleteFile(), "remove only original generated fixture sources");
    ok (session.loadProject (target), "reopen relocated project after original sources are gone");
    checkMedia (session.snapshot());
    check (session.snapshot().tracks.back().clips.front().notes.front().pitch == 67,
           "portable reopened project retains the saved redone note");
}

void launcherLoadValidation (const juce::File& root)
{
    Session session (false);
    const auto file = root.getChildFile ("Launcher-schema.halftrack");
    ok (session.saveProject (file), "save launcher schema fixture");
    const auto original = file.loadFileAsString();
    const auto trackId = session.snapshot().tracks.front().id;
    const auto originalName = session.snapshot().tracks.front().name;
    ok (session.renameTrack (0, "Preserve this unsaved edit"), "prepare active unsaved project");
    auto malformed = root.getChildFile ("Invalid-launcher.halftrack");
    auto retained = [&]
    {
        const auto state = session.snapshot();
        check (state.tracks.front().id == trackId && state.tracks.front().name == "Preserve this unsaved edit"
               && state.dirty && session.canUndo(), "invalid launcher load preserves identity and unsaved state");
        session.undo();
        check (session.snapshot().tracks.front().name == originalName, "failed load preserves the actual rename undo action");
        session.redo();
        check (session.snapshot().tracks.front().name == "Preserve this unsaved edit", "failed load preserves the actual rename redo action");
    };
    auto xml = juce::XmlDocument::parse (original);
    check (xml != nullptr, "parse launcher schema");
    auto* edit = xml->getChildByName ("EDIT");
    auto* scenes = edit ? edit->getChildByName ("SCENES") : nullptr;
    check (scenes != nullptr, "find scene list in project");
    while (scenes->getNumChildElements() < 257) scenes->createNewChildElement ("SCENE");
    check (xml->writeTo (malformed), "write excessive scene fixture");
    check (session.loadProject (malformed).failed(), "reject more than supported scene limit");
    retained();

    xml = juce::XmlDocument::parse (original);
    edit = xml->getChildByName ("EDIT");
    auto* track = edit->getChildByName ("TRACK");
    auto* slots = track ? track->getChildByName ("CLIPSLOTS") : nullptr;
    check (slots != nullptr, "find persistent clip slots");
    slots->createNewChildElement ("CLIPSLOT")->setAttribute ("id", "99999999");
    check (xml->writeTo (malformed), "write orphan slot fixture");
    check (session.loadProject (malformed).failed(), "reject slot row without matching scene");
    retained();

    xml = juce::XmlDocument::parse (original);
    edit = xml->getChildByName ("EDIT");
    track = edit->getChildByName ("TRACK");
    auto* firstSlot = track->getChildByName ("CLIPSLOTS")->getChildByName ("CLIPSLOT");
    check (firstSlot != nullptr, "find empty slot for malformed ownership fixture");
    firstSlot->createNewChildElement ("MIDICLIP")->setAttribute ("id", "99999990");
    firstSlot->createNewChildElement ("MIDICLIP")->setAttribute ("id", "99999991");
    check (xml->writeTo (malformed), "write two-clips-in-one-slot fixture");
    const auto duplicateClipResult = session.loadProject (malformed);
    check (duplicateClipResult.failed() && duplicateClipResult.getErrorMessage().contains ("more than one clip"),
           "reject hidden second clip before constructing engine slot objects");
    retained();

    xml = juce::XmlDocument::parse (original);
    edit = xml->getChildByName ("EDIT");
    track = edit->getChildByName ("TRACK");
    track->getChildByName ("CLIPSLOTS")->deleteAllChildElements();
    track->setAttribute ("playSlotClips", 1);
    check (xml->writeTo (malformed), "write legacy missing-empty-rows fixture");
    ok (session.loadProject (malformed), "fill legacy empty rows on load");
    const auto loaded = session.snapshot();
    check (loaded.scenes.size() == 8 && loaded.tracks.front().slots.size() == 8
           && ! loaded.tracks.front().launcherActive && ! loaded.dirty && ! session.canUndo(),
           "legacy row repair is clean and does not restore performance playback");
    for (const auto& slot : loaded.tracks.front().slots)
        check (! slot.hasClip, "missing empty row remains empty");
}

}

int runPersistenceTests (const juce::File& destination)
{
    try
    {
        auto root = destination.getChildFile ("persistence-" + juce::Uuid().toString());
        ok (root.createDirectory(), "create isolated fixture directory");
        Session session (false);
        ok (session.addMidiTrack ("Copper keys"), "create instrument track");
        auto before = session.snapshot();
        const int index = (int) before.tracks.size() - 1;
        const auto trackId = before.tracks.back().id;
        ok (session.addMidiClip (trackId, 4, 8), "create eight-beat MIDI clip at beat four");
        const auto clipId = session.snapshot().tracks.back().clips.back().id;
        ok (session.addMidiNote (clipId, 60, 0.5, 1.5, 0.75f), "create first note");
        ok (session.addMidiNote (clipId, 67, 2, 0.5, 0.55f), "create second note");
        const auto noteId = session.snapshot().tracks.back().clips.back().notes.front().id;
        auto synth = session.snapshot().tracks.back().synth;
        synth.waveform = 1; synth.pulseWidth = 0.37f; synth.cutoffHz = 1800; synth.resonance = 0.35f;
        synth.attackSeconds = 0.035f; synth.decaySeconds = 0.3f;
        synth.sustain = 0.4f; synth.releaseSeconds = 0.8f; synth.level = 0.12f;
        ok (session.setSynthSettings (trackId, synth), "store nondefault instrument controls");
        ok (session.setTrackGain (index, -4), "set mixer level");
        ok (session.setTrackPan (index, 0.25f), "set mixer pan");
        session.setTempo (97);
        auto project = root.getChildFile ("Copper.halftrack");
        ok (session.saveProject (project), "save versioned project");
        auto xml = juce::XmlDocument::parse (project);
        check (xml && xml->hasTagName ("HALFTRACK") && xml->getIntAttribute ("version") == 1
               && xml->getChildByName ("EDIT"), "project is human-readable versioned XML");
        check (! session.snapshot().dirty, "save clears dirty state");
        const auto preserved = project.loadFileAsString();
        auto blockedParent = root.getChildFile ("not-a-directory");
        check (blockedParent.replaceWithText ("guard"), "create failure fixture");
        check (session.saveProject (blockedParent.getChildFile ("CannotSave.halftrack")).failed(), "failed save reports error");
        check (project.loadFileAsString() == preserved, "failed save preserves existing project bytes");
        ok (session.newProject(), "clear in-memory model before reopen");
        ok (session.loadProject (project), "reopen MIDI project");
        auto reopened = session.snapshot();
        check (reopened.tracks.back().id == trackId && reopened.tracks.back().isMidi, "track identity/type survive reopen");
        check (reopened.tracks.back().clips.size() == 1, "one MIDI clip survives reopen");
        const auto& clip = reopened.tracks.back().clips.front();
        check (clip.isMidi && clip.id == clipId && near (clip.startBeat, 4) && near (clip.lengthBeats, 8), "clip identity and beat range survive reopen");
        check (clip.notes.size() == 2 && clip.notes.front().id == noteId, "note identity and count survive reopen");
        check (clip.notes.front().pitch == 60 && near (clip.notes.front().startBeat, 0.5)
               && near (clip.notes.front().lengthBeats, 1.5) && std::abs (clip.notes.front().velocity - 0.75f) < 0.01f,
               "note pitch/time/duration/velocity survive reopen");
        const auto& restoredSynth = reopened.tracks.back().synth;
        check (restoredSynth.waveform == 1 && near (restoredSynth.pulseWidth, 0.37) && near (restoredSynth.cutoffHz, 1800)
               && near (restoredSynth.resonance, 0.35) && near (restoredSynth.attackSeconds, 0.035)
               && near (restoredSynth.decaySeconds, 0.3) && near (restoredSynth.sustain, 0.4)
               && near (restoredSynth.releaseSeconds, 0.8) && near (restoredSynth.level, 0.12),
               "all instrument controls survive reopen");
        check (near (reopened.tempo, 97) && near (reopened.tracks.back().gainDb, -4)
               && near (reopened.tracks.back().pan, 0.25), "tempo and mixer survive reopen");
        check (! session.canUndo(), "reopen starts a clean undo history");
        ok (session.updateMidiNote (clipId, noteId, 64, 1, 2, 0.65f), "edit reopened note");
        session.undo();
        check (session.snapshot().tracks.back().clips.front().notes.front().pitch == 60, "undo restores reopened note");
        session.redo();
        check (session.snapshot().tracks.back().clips.front().notes.front().pitch == 64, "redo restores edited note");
        ok (session.saveProject (project), "save reopened edited project");
        auto malformed = root.getChildFile ("Broken.halftrack");
        check (malformed.replaceWithText ("<HALFTRACK version=\"1\"><EDIT>"), "write malformed fixture");
        check (session.loadProject (malformed).failed() && session.snapshot().tracks.back().id == trackId,
               "malformed load retains active MIDI project");
        if (const auto* pluginPath = std::getenv ("HALFTRACK_TEST_PLUGIN"))
        {
            juce::OwnedArray<juce::PluginDescription> descriptions;
            for (auto* format : session.pluginFormats().getFormats())
                if (format->getName() == "VST3")
                    format->findAllTypesForFile (descriptions, juce::File (pluginPath).getFullPathName());
            check (! descriptions.isEmpty(), "scan controlled plug-in for registry test");
            ok (session.addPlugin (index, *descriptions[0]), "append controlled effect after instrument");
            auto withPlugin = root.getChildFile ("Copper-with-effect.halftrack");
            ok (session.saveProject (withPlugin), "save external effect identity and state");
            ok (session.newProject(), "clear before missing registry test");
            session.knownPlugins().clear();
            // As in the reference, a plug-in that cannot load stays as a placeholder keeping its state.
            ok (session.loadProject (withPlugin), "a project whose plug-in is not scanned still opens");
            // The external plug-in's track and position.
            const auto locate = [&session] (int& track, int& position)
            {
                const auto snapshot = session.snapshot(); track = position = -1;
                for (size_t t = 0; t < snapshot.tracks.size(); ++t)
                    for (size_t p = 0; p < snapshot.tracks[t].plugins.size(); ++p)
                        if (! snapshot.tracks[t].plugins[p].isNativeEffect && ! snapshot.tracks[t].plugins[p].isUtility) { track = (int) t; position = (int) p; }
                return track >= 0 ? snapshot.tracks[(size_t) track].plugins[(size_t) position] : Session::PluginInfo {};
            };
            int pluginTrack = -1, pluginPosition = -1;
            const auto placeholder = locate (pluginTrack, pluginPosition);
            check (pluginTrack >= 0 && placeholder.missing && placeholder.name == descriptions[0]->name && placeholder.loadError.isNotEmpty(),
                   "the unscanned plug-in is a named placeholder that says why it could not load");
            check (session.showPluginEditor (pluginTrack, pluginPosition).failed() && session.setPluginParameter (pluginTrack, pluginPosition, 0, .5f).failed(),
                   "its editor and parameters are unavailable");
            const auto rendered = root.getChildFile ("Copper-placeholder.wav");
            ok (session.exportWav (rendered, 48000, 24, .2), "render through the placeholder");
            {
                juce::AudioFormatManager formats; formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (rendered));
                juce::AudioBuffer<float> audio (2, reader ? (int) reader->lengthInSamples : 0);
                if (reader) reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
                check (reader && audio.getMagnitude (0, audio.getNumSamples()) > .001f, "audio passes through the placeholder");
            }
            const auto resaved = root.getChildFile ("Copper-placeholder.halftrack");
            ok (session.saveProject (resaved), "save the project with its placeholder");
            const auto pluginElement = [] (const juce::File& file)
            {
                juce::StringPairArray attributes;
                const auto xml = juce::parseXML (file);
                std::function<void (const juce::XmlElement&)> visit = [&] (const juce::XmlElement& e)
                {
                    if (e.getTagName() == "PLUGIN" && e.getStringAttribute ("type") == "vst")
                        for (const auto* name : { "state", "uid", "filename", "name", "manufacturer" }) attributes.set (name, e.getStringAttribute (name));
                    for (const auto* child : e.getChildIterator()) visit (*child);
                };
                if (xml) visit (*xml);
                return attributes;
            };
            const auto original = pluginElement (withPlugin), kept = pluginElement (resaved);
            check (original.size() == 5 && original["uid"].isNotEmpty() && original["filename"].isNotEmpty() && kept == original,
                   "saving keeps the placeholder's identity and state unchanged");
            session.knownPlugins().addType (*descriptions[0]);
            ok (session.loadProject (resaved), "once scanned, the re-saved project opens with the plug-in");
            check (! locate (pluginTrack, pluginPosition).missing && pluginTrack >= 0, "and the plug-in loads from the kept state");
            ok (session.loadProject (withPlugin), "reopen after controlled plug-in has been registered");
            check (session.snapshot().tracks.back().id == trackId, "registered effect project retains MIDI identity");
        }
        saveAsPreservesHistory (root);
        launcherLoadValidation (root);
        std::cout << "Persistence acceptance passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL persistence: " << error.what() << std::endl;
        return 1;
    }
}
}
