#pragma once

#include <optional>
#include <vector>

// Capture MIDI's analysis of notes played with the transport stopped, after the reference's (static reading of its
// code; see the Capture MIDI contract): it finds the tempo they were played at, fits a tempo map to them, and places
// them in a loop of one to eight bars. Pure functions; the Session makes the clip and sets the tempo.
namespace halftrack::capturetempo
{
struct Note
{
    int pitch = 60;
    double velocity = 100;
    double ms = 0, durationMs = 0; // When it was played and how long it was held.
    bool user = true;              // Played by hand, as opposed to generated.
    double beat = 0, durationBeats = 0; // Where the analysis places it.
};
// What the project tells the analysis, after the reference's capture context: keep its tempo, take it as a candidate
// with a bonus because the project has other clips (or a controlled tempo), or take it as a hint.
enum class Mode { keep, setHasClips, hint };
struct Context { double tempo = 120; Mode mode = Mode::setHasClips; };
struct Capture
{
    double tempo = 120;
    std::vector<Note> notes; // In beats from the clip's start, a bar line at or before the first note.
    double loopStart = 0, loopEnd = 4; // In the clip's beats; the start marker is at the loop start.
};
// The notes a capture keeps from a track's history, in the order played: those that started before the press and have
// a length, after the last silence of more than 20 seconds, and none more than 30 seconds before the first played
// note after it.
std::vector<Note> trimHistory (std::vector<Note>, std::optional<double> pressMs = std::nullopt);
// One track's capture: its notes in the order played (at least one), the bar's length in beats, and the context, or
// none to find the tempo freely between 78 and 164 BPM.
Capture capture (std::vector<Note>, double bar, std::optional<Context>);
// Times as the reference keeps them: a twelfth-of-a-beat grid without 1/12, 5/12, 7/12 and 11/12 (beatPosition, in
// twelfths), its engine grid of 1/3843840 beat (engineRound), and rounding to a quantum, halves away from zero.
int beatPosition (double beats) noexcept;
double engineRound (double beats) noexcept;
double roundTo (double x, double quantum) noexcept;
}
