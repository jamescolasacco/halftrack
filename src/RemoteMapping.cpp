#include "RemoteMapping.h"
#include <array>
#include <cmath>

// Built without floating-point contraction (see CMakeLists.txt), so only the explicit std::fma calls fuse, where the
// reference's fused multiply-adds are.
namespace halftrack::remotemap
{
// A 7-bit controller's steps, which the tolerances count in.
static constexpr float steps = 128.0f;

bool isAbsolute (Mode mode) noexcept { return mode == Mode::absolute || mode == Mode::absolute14; }

float absolute (float value, float lower, float upper, bool reverse) noexcept
{
    const float x = reverse ? 127.0f - value : value;
    const float t = x / 127.0f;
    return std::fma (upper, t, lower * (1.0f - t));
}

// Value Scaling (inline at 0x1019cd648): within two steps the control follows the controller; otherwise it moves toward
// the end the controller moves to, keeping the ratio of their distances from it.
static float scaled (float control, float now, float before, float lower, float upper) noexcept
{
    const float range = upper - lower;
    const float snap = (range + range) / steps;
    if (control - snap <= now && control + snap >= now) return now;
    if (before == now) return control;
    // Both are worked out, as the reference's are, and the one not used may divide by zero.
    const float up = (upper - control) * ((now - before) / (upper - before));
    const float down = (control - lower) * ((before - now) / (before - lower));
    const float amount = before <= now ? up : down;
    const bool negative = before <= now ? up < 0.0f : down > 0.0f;
    return std::fma (amount, negative ? -1.0f : 1.0f, control);
}

Move takeover (Takeover mode, float current, std::optional<float> prior, float value, float lower, float upper, bool reverse) noexcept
{
    const float now = absolute (value, lower, upper, reverse);
    const float control = upper < current ? upper : lower > current ? lower : current;
    Move move { now };
    if (mode != Takeover::none)
    {
        move.value = control;
        if (prior)
        {
            const float before = absolute (*prior, lower, upper, reverse);
            const float half = ((upper - lower) * 0.5f) / steps;
            if (control - half <= before && control + half >= before) move.value = now;
            else if (mode == Takeover::valueScaling) move.value = scaled (control, now, before, lower, upper);
            else
            {
                // Pick-Up (0x1019cd374): the controller's move from its previous value passed the control's, or came
                // within two steps of it.
                const float tolerance = ((upper - lower) + (upper - lower)) / steps;
                const float low = now < before ? now : before, high = now >= before ? now : before;
                if (! (low - tolerance > control) && ! (high + tolerance < control)) move.value = now;
                move.pickUp = true;
            }
        }
    }
    move.value = move.value < lower ? lower : move.value > upper ? upper : move.value;
    return move;
}

// The accelerated modes' step sizes (0x1019d538c): a step counts as its entry, and one past the table as the default.
static constexpr std::array<float, 8> accelerated { 1, 2, 3, 4, 5, 8, 20, 50 };
static constexpr std::array<float, 9> signedBitSteps { 1, 2, 3, 4, 5, 8, 10, 20, 50 };
template <size_t n>
static float entry (const std::array<float, n>& table, int index, float otherwise) noexcept
{ return index >= 0 && index < (int) n ? table[(size_t) index] : otherwise; }

float relativeSteps (Mode mode, float value) noexcept
{
    const int v = (int) value;
    switch (mode)
    {
        case Mode::signedBit: return entry (signedBitSteps, v >= 65 ? v - 65 : v - 1, 20.0f) * (v >= 65 ? -1.0f : 1.0f);
        case Mode::binaryOffset: return entry (accelerated, v > 63 ? v - 65 : 63 - v, 50.0f) * (v <= 63 ? -1.0f : 1.0f);
        case Mode::twosComplement: return entry (accelerated, v < 66 ? v - 1 : 127 - v, 50.0f) * (v >= 66 ? -1.0f : 1.0f);
        case Mode::signedBit2: return entry (accelerated, v > 64 ? v - 65 : v - 1, 50.0f) * (v <= 64 ? -1.0f : 1.0f);
        case Mode::linearSignedBit: return v < 65 ? value : 64.0f - value;
        case Mode::linearBinaryOffset: return value + -64.0f;
        case Mode::linearTwosComplement: return v < 65 ? value : value + -128.0f;
        case Mode::linearSignedBit2: return v < 65 ? -value : value + -64.0f;
        case Mode::absolute: case Mode::absolute14: break;
    }
    return 0.0f;
}

float relative (Mode mode, float value, float current, float lower, float upper, bool stepped, bool reverse) noexcept
{
    const float s = relativeSteps (mode, value);
    float d = ((upper - lower) * 0.00390625f) * s;
    if (stepped) d = s > 0.0f ? 1.0f : -1.0f;
    if (reverse) d = -d;
    float r = d + current;
    r = upper < r ? upper : r;
    r = lower > r ? lower : r;
    // Stepped controls round half away from zero.
    return stepped ? std::round (r) : r;
}

bool switchesOn (Mode mode, float value) noexcept
{
    switch (mode)
    {
        case Mode::absolute: case Mode::absolute14: return value > 63.0f;
        // Linear BinOffset's entry is Signed Bit's, as the reference's table has it.
        case Mode::signedBit: case Mode::linearSignedBit: case Mode::linearBinaryOffset: return value < 65.0f;
        case Mode::binaryOffset: return value >= 64.0f;
        case Mode::twosComplement: return value <= 64.0f;
        case Mode::signedBit2: return value >= 65.0f;
        case Mode::linearTwosComplement: case Mode::linearSignedBit2: break;
    }
    return false;
}

float fourteenBit (int coarse, int fine) noexcept { return (float) ((coarse << 7) | fine) / 129.0f; }

Learned learn (int controller, int value, std::optional<int> prior, std::optional<int> mappedController) noexcept
{
    if (prior && *prior == value)
        return { controller, value == 1 ? Mode::signedBit : value == 63 ? Mode::binaryOffset : value == 127 ? Mode::twosComplement : value == 65 ? Mode::signedBit2 : Mode::absolute };
    if (mappedController && (controller == *mappedController + 32 || controller == *mappedController - 32))
        return { controller < 32 ? controller : controller - 32, Mode::absolute14 };
    return { controller, Mode::absolute };
}
}
