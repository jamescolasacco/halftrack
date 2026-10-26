#include "MidiTakeover.h"
#include <cmath>

// Built without floating-point contraction (see CMakeLists.txt), so only the explicit std::fma calls fuse, where the
// reference's fused multiply-adds are.
namespace halftrack::takeover
{
// A 7-bit controller's steps, which the tolerances count in.
static constexpr float steps = 128.0f;

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

Move apply (Mode mode, float current, std::optional<float> prior, float value, float lower, float upper, bool reverse) noexcept
{
    const float now = absolute (value, lower, upper, reverse);
    const float control = upper < current ? upper : lower > current ? lower : current;
    Move move { now };
    if (mode != Mode::none)
    {
        move.value = control;
        if (prior)
        {
            const float before = absolute (*prior, lower, upper, reverse);
            const float half = ((upper - lower) * 0.5f) / steps;
            if (control - half <= before && control + half >= before) move.value = now;
            else if (mode == Mode::valueScaling) move.value = scaled (control, now, before, lower, upper);
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
}
