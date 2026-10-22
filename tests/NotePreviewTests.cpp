#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// The MIDI editor's Preview: the project setting (off at first, saved, not an undo step or an
// unsaved change), and preview notes, which sound on a MIDI track's instrument with the transport
// stopped and end when released, when Preview is switched off, or when the project is replaced.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS note preview: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
// The output's peak once a note has had time to sound, or to die away after it ends.
float settled (Session& s) { pumpSyntheticTestDevice (s, blocksPerSecond / 2); return pumpSyntheticTestDevice (s, blocksPerSecond / 4); }
}

int runNotePreviewTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("note-preview-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.addMidiTrack ("Keys"), "a MIDI track with the built-in synth");
        ok (s.addAudioTrack ("Audio"), "an audio track");
        juce::String keys, audio;
        for (const auto& t : s.snapshot().tracks) { if (t.name == "Keys") keys = t.id; if (t.name == "Audio") audio = t.id; }
        check (settled (s) < 1e-5f, "the project starts silent");

        // The setting.
        check (! s.notePreview(), "Preview starts off, as in the reference's default set");
        ok (s.saveProject (root.getChildFile ("Preview").getChildFile ("Preview.halftrack")), "save a project");
        const bool couldUndo = s.canUndo();
        s.setNotePreview (true);
        check (s.notePreview() && s.canUndo() == couldUndo && ! s.snapshot().dirty, "switching it on is not an undo step or an unsaved change");
        ok (s.saveProject (root.getChildFile ("Preview On").getChildFile ("Preview On.halftrack")), "save it as another project");
        {
            Session reopened (false);
            ok (reopened.loadProject (s.snapshot().projectFile), "reopen");
            check (reopened.notePreview(), "it is saved with the project");
        }

        // Preview notes.
        check (s.previewNote (audio, 60, .8f, true).failed() && s.previewNote ("missing", 60, .8f, true).failed()
                   && s.previewNote (keys, 128, .8f, true).failed() && s.previewNote (keys, 60, 1.5f, true).failed(),
               "only a MIDI track previews, with a note from 0 to 127 and a velocity from 0 to 1");
        check (! s.snapshot().playing, "the transport is stopped");
        ok (s.previewNote (keys, 69, .8f, true), "start A4");
        const auto sounding = settled (s);
        check (sounding > .01f, "a preview note sounds on the track's instrument with the transport stopped (peak " + juce::String (sounding, 4) + ")");
        ok (s.previewNote (keys, 69, 0, false), "end it");
        check (settled (s) < 1e-4f, "and ends when released");
        check (! s.snapshot().playing && s.snapshot().position == 0 && ! s.snapshot().dirty && s.canUndo() == couldUndo,
               "previewing leaves the transport, the project and its history alone");
        ok (s.previewNote (keys, 60, .8f, true), "start C4"); ok (s.previewNote (keys, 64, .8f, true), "and E4");
        check (settled (s) > .01f, "two notes sound together");
        s.stopNotePreviews();
        check (settled (s) < 1e-4f, "stopNotePreviews ends them all");
        ok (s.previewNote (keys, 67, .8f, true), "start G4");
        check (settled (s) > .01f, "it sounds");
        s.setNotePreview (false);
        check (settled (s) < 1e-4f && ! s.notePreview(), "switching Preview off ends it");
        ok (s.previewNote (keys, 67, 0, false), "a release after that is harmless");
        ok (s.previewNote (keys, 72, .8f, true), "start C5");
        ok (s.loadProject (root.getChildFile ("Preview").getChildFile ("Preview.halftrack")), "open the first save while it sounds");
        check (settled (s) < 1e-4f && ! s.notePreview(), "the replaced project's preview ends, and the opened one has Preview off as it was saved");
        ok (s.newProject(), "a new project");
        check (! s.notePreview(), "and a new project starts with Preview off");
        root.deleteRecursively();
        std::cout << "Note preview passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL note preview: " << e.what() << std::endl; return 1; }
}
}
