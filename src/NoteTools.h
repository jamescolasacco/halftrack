#pragma once

#include "Session.h"
#include <span>

// MIDI note tools after the reference's transformation and generative tools. Each tool is a
// pure function from notes (and the clip length) to notes; Session::applyNoteTool applies it.
namespace halftrack::notetools
{
struct Field
{
    const char* label;
    double minimum, maximum, initial;
    bool integer;
    const char* unit;
    const char* choices = nullptr; // "A|B|C": the value is the choice index.
};
std::span<const Field> fields (Session::NoteTool) noexcept;
const char* name (Session::NoteTool) noexcept;
bool generates (Session::NoteTool) noexcept; // Generators fill the clip instead of reshaping notes.
Session::NoteToolSettings defaults (Session::NoteTool) noexcept;
bool valid (const Session::NoteToolSettings&) noexcept;
// Beats for a grid choice: 1/32, 1/16, 1/8, 1/4 or 1/2 note.
double gridBeats (double choice) noexcept;
// The notes the tool produces. Transformations reshape `notes`; generators ignore them. Results
// lie inside [0, clipLengthBeats], are at least a 960th of a beat long and are ordered by start.
std::vector<Session::MidiNoteInfo> run (const Session::NoteToolSettings&, const std::vector<Session::MidiNoteInfo>& notes, double clipLengthBeats);
}
