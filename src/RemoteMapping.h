#pragma once
#include <optional>

// Remote control arithmetic, ported from the research model of the reference's controller code (static reading of its
// code; docs/research/midi-mapping-contract.md): what a controller message does to the control it is mapped to, in an
// absolute or relative map mode, with takeover and 14-bit pairs, and the mode learning detects. As the reference's,
// everything is single precision, in the control's own scale between the mapping's lower and upper ends.
namespace halftrack::remotemap
{
// The reference's map modes, in its order (its TControllerMapMode): Absolute; Relative (Signed Bit), (BinOffset),
// (2's Comp.) and (Signed Bit 2), whose steps speed up through fixed tables; Absolute (14bit), a controller with the one
// 32 above it as its fine half; and the four relative modes again, taken linearly.
enum class Mode { absolute, signedBit, binaryOffset, twosComplement, signedBit2, absolute14, linearSignedBit, linearBinaryOffset, linearTwosComplement, linearSignedBit2 };
inline constexpr int modeCount = 10;
bool isAbsolute (Mode) noexcept;

// The control's value for an absolute controller value of 0 to 127 (0x1019c7678): from lower to upper, or upper to
// lower when the mapping is reversed.
float absolute (float value, float lower, float upper, bool reverse) noexcept;

enum class Takeover { none, pickUp, valueScaling };
struct Move
{
    float value = 0;     // The control's next value, within the mapping's range.
    bool pickUp = false; // Pick-Up judged the move, as the reference reports each one: picked up when value is the controller's.
};
// One absolute controller message (0x1019cd538): the control's value now, the controller's previous value from the same
// input, channel and controller (none while it is not known), and its value now. None jumps to the controller. Pick-Up
// and Value Scaling follow a controller already in step with the control; otherwise Pick-Up waits until the controller
// passes the control's value, and Value Scaling moves the value toward the end the controller moves to, in proportion,
// so both reach it together. With no previous value they leave the control as it is.
Move takeover (Takeover, float current, std::optional<float> prior, float value, float lower, float upper, bool reverse) noexcept;

// A relative controller value's signed steps (0x1019cca18): the accelerated modes' tables, or the value itself for the
// linear ones; none in an absolute mode.
float relativeSteps (Mode, float value) noexcept;
// A relative message's next value (0x1019c7678): a 256th of the range a step, or one step for a stepped control, from
// the control's value now, the other way for a reversed mapping, within the range.
float relative (Mode, float value, float current, float lower, float upper, bool stepped, bool reverse) noexcept;
// Whether a controller message switches a button on or presses it (0x1019ccc90).
bool switchesOn (Mode, float value) noexcept;
// A 14-bit pair's value, 0 to 127 in steps of 1/129 (0x1019d0a7c), from its coarse and fine halves; a half not yet known
// is -129, as the reference's inputs hold it, which gives what the reference's arithmetic gives.
float fourteenBit (int coarse, int fine) noexcept;
inline constexpr int unknownValue = -129;

// The mode learning detects for a controller message (0x102343568): a value repeated from the controller's last gives a
// relative mode by the value (1 Signed Bit, 63 BinOffset, 127 2's Comp., 65 Signed Bit 2); a controller 32 from the one
// the control already has on the channel makes the pair Absolute (14bit) on the lower of them; anything else, Absolute.
struct Learned { int controller = 0; Mode mode = Mode::absolute; };
Learned learn (int controller, int value, std::optional<int> prior, std::optional<int> mappedController) noexcept;
}
