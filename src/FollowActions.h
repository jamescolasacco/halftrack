#pragma once

#include "Session.h"
#include <cstdint>
#include <optional>
#include <span>

// Follow Actions after the reference's (static reading of its code; see the follow actions contract): its random draws,
// its choice between actions A and B, what each action plays in a group, and its chance fields. Pure functions; the
// Session schedules the launches.
namespace halftrack::followactions
{
// The reference's generator: a 32-bit linear congruential state, stepped by 1664525 and 1013904223 and drawn as a
// float from 0 up to 1 from its low 23 bits.
float draw (std::uint32_t& state) noexcept;
// The action a draw r picks, with its Jump target: B when r x (A + B) reaches A's chance (in single precision, as the
// reference fuses it), else A; No Action when both chances are 0.
std::pair<Session::FollowAction, int> choose (const Session::FollowSettings&, float r) noexcept;
// A group: a run of consecutive rows holding something (a track's slots holding clips, or scenes holding any clip),
// and the rows in it that can play (active clips; every scene in it).
struct Group { int first = 0, last = -1; std::vector<int> members; };
// The groups of a track's slots or of the scenes: empty rows split them.
std::vector<Group> groups (const std::vector<bool>& held, const std::vector<bool>& plays);
// The group whose run holds a row, if any.
const Group* groupOf (const std::vector<Group>&, int row) noexcept;
// What an action plays from the current row of a group: a row, stopTarget, or nothing. Previous and Next wrap, Any may
// pick the current row and Other never does, unless it is alone; Jump plays its row, from 0, wherever it is, or stops for
// -1, but nothing when the row is `excluded` (a slot that can neither play nor stop its track). Without a group nothing
// happens, not even Stop. r is the evaluation's second draw.
inline constexpr int stopTarget = -2;
std::optional<int> target (int current, Session::FollowAction, int jump, const Group*, std::span<const int> excluded, float r) noexcept;
// One evaluation, as a clip's or scene's start makes it: the two draws, A or B, then what it plays.
std::optional<int> evaluate (std::uint32_t& state, int current, const Session::FollowSettings&, const Group*, std::span<const int> excluded = {}) noexcept;
// Editing either chance to v: v rounded half away from zero, and the other chance 100 minus it.
std::pair<float, float> coupled (float v) noexcept;
// The chances as a load leaves them: A as its rounded share of the two in percent, and B the rest; both 0 give A 100
// and switch the Follow Action off.
Session::FollowSettings normalised (Session::FollowSettings) noexcept;
// Whether a Follow Action does anything: on, with an action other than No Action.
bool active (const Session::FollowSettings&) noexcept;
// A time in beats on the reference's grid of 1/3843840 beat, halves away from zero.
double tick (double beats) noexcept;
// The longest Follow Action Time and multiplier accepted.
inline constexpr double maximumTime = 16384;
inline constexpr int maximumMultiplier = (1 << 30) - 1;
bool valid (const Session::FollowSettings&) noexcept;
}
