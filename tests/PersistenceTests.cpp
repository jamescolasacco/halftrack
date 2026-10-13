#include "Session.h"
#include <cmath>
#include <cstdlib>
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
        synth.waveform = 1; synth.cutoffHz = 1800; synth.resonance = 0.35f;
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
        check (restoredSynth.waveform == 1 && near (restoredSynth.cutoffHz, 1800)
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
            auto unavailable = session.loadProject (withPlugin);
            check (unavailable.failed() && unavailable.getErrorMessage().contains ("Scan / Manage")
                   && session.snapshot().tracks.size() == 2, "unscanned plug-in load is actionable and preserves active project");
            session.knownPlugins().addType (*descriptions[0]);
            ok (session.loadProject (withPlugin), "reopen after controlled plug-in has been registered");
            check (session.snapshot().tracks.back().id == trackId, "registered effect project retains MIDI identity");
        }
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
