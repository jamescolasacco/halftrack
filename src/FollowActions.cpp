#include "FollowActions.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// Built with floating-point contraction off (CMakeLists.txt), so only the std::fma calls fuse, as the reference's do.
namespace halftrack::followactions
{
using Action = Session::FollowAction;

float draw (std::uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    const std::uint32_t bits = 0x3f800000u | (state & 0x7fffffu);
    float f;
    std::memcpy (&f, &bits, sizeof f); // From 1 up to 2.
    return std::nextafter (std::fabs (std::fma (f, 2.0f, -3.0f)), 0.0f);
}

std::pair<Action, int> choose (const Session::FollowSettings& s, float r) noexcept
{
    const auto a = s.chanceA, b = s.chanceB;
    if (a == 0 && b == 0) return { Action::noAction, std::numeric_limits<int>::min() };
    // The chances are weights: B when r x (A + B) >= A, ties going to B.
    return std::fma (r, b, std::fma (r, a, -a)) >= 0 ? std::pair { s.actionB, s.jumpB } : std::pair { s.actionA, s.jumpA };
}

std::vector<Group> groups (const std::vector<bool>& held, const std::vector<bool>& plays)
{
    std::vector<Group> result;
    for (int i = 0; i < (int) held.size(); ++i)
    {
        if (! held[(size_t) i]) continue;
        Group group;
        group.first = i;
        for (; i < (int) held.size() && held[(size_t) i]; ++i)
            if (i < (int) plays.size() && plays[(size_t) i]) group.members.push_back (i);
        group.last = i - 1;
        result.push_back (std::move (group));
    }
    return result;
}

const Group* groupOf (const std::vector<Group>& all, int row) noexcept
{
    for (const auto& group : all) if (row >= group.first && row <= group.last) return &group;
    return nullptr;
}

std::optional<int> target (int current, Action action, int jump, const Group* group, std::span<const int> excluded, float r) noexcept
{
    if (group == nullptr || group->members.empty()) return std::nullopt;
    const auto& members = group->members;
    const int n = (int) members.size();
    // Where the current row is among the members, or past them when it is not one.
    const int p = (int) (std::find (members.begin(), members.end(), current) - members.begin());
    const auto at = [&] (long long i) { return members[(size_t) std::clamp (i, 0LL, (long long) n - 1)]; };
    const auto wrap = [n] (int i) { return ((i % n) + n) % n; };
    switch (action)
    {
        case Action::noAction: return std::nullopt;
        case Action::stop:     return stopTarget;
        case Action::again:    return current;
        case Action::previous: return at (wrap (p - 1));
        case Action::next:     return at (wrap (p + 1));
        case Action::first:    return members.front();
        case Action::last:     return members.back();
        case Action::any:      return at ((long long) static_cast<unsigned> (static_cast<float> (n) * r));
        case Action::other:
        {
            if (n == 1) return current;
            auto i = (long long) static_cast<unsigned> (static_cast<float> (n - 1) * r);
            if (p <= i) ++i;
            return at (i);
        }
        case Action::jump:
            if (jump < 0) return stopTarget;
            if (std::find (excluded.begin(), excluded.end(), jump) != excluded.end()) return std::nullopt;
            return jump;
    }
    return std::nullopt;
}

std::optional<int> evaluate (std::uint32_t& state, int current, const Session::FollowSettings& settings, const Group* group, std::span<const int> excluded) noexcept
{
    const auto first = draw (state);
    const auto [action, jump] = choose (settings, first);
    const auto second = draw (state);
    return target (current, action, jump, group, excluded, second);
}

std::pair<float, float> coupled (float v) noexcept
{
    const auto rounded = std::round (std::clamp (v, 0.0f, 100.0f));
    return { rounded, 100.0f - rounded };
}

Session::FollowSettings normalised (Session::FollowSettings s) noexcept
{
    const float a = s.chanceA, b = s.chanceB;
    if (a == 0 && b == 0) { s.chanceA = 100; s.enabled = false; return s; }
    const float share = (a + b) / 100.0f;
    s.chanceA = std::round (a / share);
    s.chanceB = 100.0f - s.chanceA;
    return s;
}

bool active (const Session::FollowSettings& s) noexcept
{
    return s.enabled && (s.actionA != Action::noAction || s.actionB != Action::noAction);
}

double tick (double beats) noexcept { return std::round (beats * 3843840.0) / 3843840.0; }

bool valid (const Session::FollowSettings& s) noexcept
{
    const auto action = [] (Action a) { return (int) a >= 0 && (int) a <= (int) Action::jump; };
    const auto chance = [] (float c) { return std::isfinite (c) && c >= 0 && c <= 100; };
    const auto jump = [] (int j) { return j >= -1 && j <= (1 << 23); };
    return action (s.actionA) && action (s.actionB) && chance (s.chanceA) && chance (s.chanceB) && jump (s.jumpA) && jump (s.jumpB)
        && std::isfinite (s.timeBeats) && s.timeBeats >= 0.25 && s.timeBeats <= maximumTime && s.multiplier >= 1 && s.multiplier <= maximumMultiplier;
}
}
