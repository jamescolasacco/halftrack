#include "ArrangementAutomation.h"
#include "NativeEffects.h"
#include <algorithm>
#include <functional>

namespace halftrack::arrangementAutomation
{
namespace
{
constexpr double maximumBeat = 1000000;
const juce::Colour envelopeColour { 0xffff6a6a }, inactiveColour { 0xff8e8e8e }, pointFill { 0xff1d1d1d };

void addNative (std::vector<Target>& result, const Session::PluginInfo& plugin)
{
    if (plugin.isNativeEffect && ! plugin.isRack)
    {
        const auto fields = effectParameters (plugin.effect.kind);
        for (size_t i = 0; i < fields.size(); ++i)
        {
            Target t; t.kind = Target::Kind::native; t.pluginId = plugin.id; t.parameter = (int) i; t.effect = plugin.effect.kind;
            t.key = "device:" + plugin.id + ":" + juce::String ((int) i); t.device = plugin.name; t.control = fields[i].label;
            t.manual = plugin.effect.values[i];
            for (const auto& a : plugin.automation) if (a.parameter == (int) i) t.lane = a.lane;
            result.push_back (std::move (t));
        }
    }
    if (plugin.isRack)
    {
        // A rack's macros are its controls; its chains' devices follow.
        for (int m = 0; m < Session::rackMacroCount; ++m)
        {
            Target t; t.kind = Target::Kind::native; t.pluginId = plugin.id; t.parameter = m; t.effect = plugin.effect.kind;
            t.key = "device:" + plugin.id + ":" + juce::String (m); t.device = plugin.name;
            t.control = plugin.macroNames[(size_t) m].isNotEmpty() ? plugin.macroNames[(size_t) m] : "Macro " + juce::String (m + 1);
            t.manual = plugin.effect.values[(size_t) m];
            for (const auto& a : plugin.automation) if (a.parameter == m) t.lane = a.lane;
            result.push_back (std::move (t));
        }
        for (const auto* list : { &plugin.chains, &plugin.returns })
            for (const auto& chain : *list) for (const auto& inner : chain.devices) addNative (result, inner);
    }
}
}

juce::String volumeKey (const Session::TrackInfo& track) { return "mixer:" + track.id + ":volume"; }

std::vector<Target> targets (Session& session, const Session::TrackInfo& track, const std::vector<Session::TrackInfo>& tracks)
{
    std::vector<Target> result;
    Target volume; volume.kind = Target::Kind::volume; volume.trackId = track.id; volume.key = volumeKey (track);
    volume.device = "Mixer"; volume.control = "Track Volume"; volume.lane = track.volumeAutomation; volume.manual = track.gainDb;
    Target pan = volume; pan.kind = Target::Kind::pan; pan.key = "mixer:" + track.id + ":pan"; pan.control = "Track Panning";
    pan.lane = track.panAutomation; pan.manual = track.pan;
    result.push_back (volume); result.push_back (pan);
    // The mixer's sends, each named for its return.
    if (! track.isReturn)
        for (const auto& send : track.sends)
        {
            Target t = volume; t.kind = Target::Kind::send; t.returnId = send.returnTrackId; t.key = "send:" + track.id + ":" + send.returnTrackId;
            t.control = send.returnTrackId;
            for (const auto& other : tracks) if (other.id == send.returnTrackId) t.control = other.name;
            t.lane = send.automation; t.manual = send.gainDb;
            result.push_back (std::move (t));
        }
    if (track.instrument.isNativeEffect) addNative (result, track.instrument);
    for (const auto& plugin : track.plugins)
    {
        if (plugin.isNativeEffect) { addNative (result, plugin); continue; }
        if (plugin.isUtility || plugin.missing) continue;
        const auto parameters = session.pluginParameters (plugin.id);
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            Target t; t.kind = Target::Kind::external; t.pluginId = plugin.id; t.parameter = (int) i;
            t.key = "device:" + plugin.id + ":" + juce::String ((int) i); t.device = plugin.name; t.control = parameters[i].name;
            t.manual = parameters[i].value;
            for (const auto& a : plugin.automation) if (a.parameter == (int) i) t.lane = a.lane;
            result.push_back (std::move (t));
        }
    }
    for (auto& t : result) t.trackId = track.id;
    return result;
}

std::optional<Target> find (Session& session, const Session::TrackInfo& track, const std::vector<Session::TrackInfo>& tracks, const juce::String& key)
{
    for (auto& t : targets (session, track, tracks)) if (t.key == key) return t;
    return std::nullopt;
}

float toProportion (const Target& t, float value)
{
    switch (t.kind)
    {
        case Target::Kind::volume: return Session::automationValueToProportion (Session::AutomationTarget::volume, value);
        case Target::Kind::pan: return Session::automationValueToProportion (Session::AutomationTarget::pan, value);
        case Target::Kind::send: return Session::sendValueToProportion (value);
        case Target::Kind::native: return Session::deviceAutomationValueToProportion (t.effect, t.parameter, value);
        case Target::Kind::external: return value;
    }
    return value;
}
float fromProportion (const Target& t, float proportion)
{
    proportion = std::clamp (proportion, 0.0f, 1.0f);
    switch (t.kind)
    {
        case Target::Kind::volume: return Session::automationProportionToValue (Session::AutomationTarget::volume, proportion);
        case Target::Kind::pan: return Session::automationProportionToValue (Session::AutomationTarget::pan, proportion);
        case Target::Kind::send: return Session::sendProportionToValue (proportion);
        case Target::Kind::native: return Session::deviceAutomationProportionToValue (t.effect, t.parameter, proportion);
        case Target::Kind::external: return proportion;
    }
    return proportion;
}
juce::String format (const Target& t, float value)
{
    switch (t.kind)
    {
        case Target::Kind::volume: case Target::Kind::send: return value <= -99.99f ? juce::String ("-inf dB") : juce::String (value, 1) + " dB";
        case Target::Kind::pan: return Session::panText (value);
        case Target::Kind::external: return juce::String (value * 100, 1) + "%";
        case Target::Kind::native:
        {
            const auto fields = effectParameters (t.effect);
            if (! juce::isPositiveAndBelow (t.parameter, (int) fields.size())) return juce::String (value, 2);
            const auto& f = fields[(size_t) t.parameter];
            if (f.choices != nullptr)
            {
                const auto options = juce::StringArray::fromTokens (f.choices, "|", {});
                return options[juce::jlimit (0, options.size() - 1, juce::roundToInt (value))];
            }
            if (effectShowsPercent (f)) return juce::String (value * 100, 1) + "%";
            return juce::String (value, std::abs (value) >= 100 || f.integer ? 0 : 2) + juce::String (f.unit);
        }
    }
    return {};
}
juce::Result write (Session& session, const Target& t, const Session::AutomationLane& lane)
{
    switch (t.kind)
    {
        case Target::Kind::volume: return session.setTrackAutomation (t.trackId, Session::AutomationTarget::volume, lane);
        case Target::Kind::pan: return session.setTrackAutomation (t.trackId, Session::AutomationTarget::pan, lane);
        case Target::Kind::send: return session.setSendAutomation (t.trackId, t.returnId, lane);
        case Target::Kind::native: case Target::Kind::external: return session.setDeviceAutomation (t.pluginId, t.parameter, lane);
    }
    return juce::Result::fail ("Unknown automation destination.");
}

juce::Point<float> Envelope::position (const Target& t, juce::Rectangle<float> area, const Session::AutomationPoint& p) const
{ return { xOfBeat (p.beat), area.getBottom() - toProportion (t, p.value) * area.getHeight() }; }
Session::AutomationPoint Envelope::pointAt (const Target& t, juce::Rectangle<float> area, juce::Point<float> at, bool snap) const
{
    auto beat = std::clamp (beatOfX (at.x), 0.0, maximumBeat);
    if (snap && snapBeat) beat = snapBeat (beat);
    return { beat, fromProportion (t, (area.getBottom() - at.y) / std::max (1.0f, area.getHeight())) };
}
int Envelope::hitPoint (const Target& t, juce::Rectangle<float> area, juce::Point<float> at) const
{
    for (size_t i = 0; i < t.lane.points.size(); ++i)
        if (position (t, area, t.lane.points[i]).getDistanceFrom (at) <= hitRadius) return (int) i;
    return -1;
}
std::optional<size_t> Envelope::hitSegment (const Target& t, juce::Rectangle<float> area, juce::Point<float> at) const
{
    const auto& points = t.lane.points;
    if (points.empty())
    {
        const auto y = area.getBottom() - toProportion (t, t.manual) * area.getHeight();
        return std::abs (at.y - y) <= hitRadius ? std::optional<size_t> (0) : std::nullopt;
    }
    // The line runs flat before the first breakpoint and after the last.
    size_t right = 0;
    while (right < points.size() && xOfBeat (points[right].beat) < at.x) ++right;
    return std::abs (at.y - yAt (t, area, right, at.x)) <= hitRadius ? std::optional<size_t> (right) : std::nullopt;
}
float Envelope::yAt (const Target& t, juce::Rectangle<float> area, size_t right, float x) const
{
    const auto& points = t.lane.points;
    if (right == 0) return position (t, area, points.front()).y;
    if (right >= points.size()) return position (t, area, points.back()).y;
    const auto a = position (t, area, points[right - 1]), b = position (t, area, points[right]);
    if (b.x <= a.x) return a.y;
    const auto from = toProportion (t, points[right - 1].value), to = toProportion (t, points[right].value);
    return area.getBottom() - Session::curveProportion (from, to, points[right - 1].curve, (x - a.x) / (b.x - a.x)) * area.getHeight();
}
// Option (Alt elsewhere) curves a segment; Cmd on the Mac, Alt elsewhere, bypasses snapping, as the
// reference's do.
static bool curvesSegment (juce::ModifierKeys mods) { return mods.isAltDown(); }
static bool bypassesSnap (juce::ModifierKeys mods)
{
   #if JUCE_MAC
    return mods.isCommandDown();
   #else
    return mods.isAltDown();
   #endif
}
void Envelope::paint (juce::Graphics& g, juce::Rectangle<float> area, const Target& t) const
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.toNearestInt());
    const auto colour = t.lane.enabled || t.lane.points.empty() ? envelopeColour : inactiveColour;
    juce::Path path;
    if (t.lane.points.empty())
    {
        const auto y = area.getBottom() - toProportion (t, t.manual) * area.getHeight();
        path.startNewSubPath (area.getX(), y); path.lineTo (area.getRight(), y);
    }
    else
    {
        path.startNewSubPath (area.getX(), position (t, area, t.lane.points.front()).y);
        for (size_t i = 0; i < t.lane.points.size(); ++i)
        {
            const auto to = position (t, area, t.lane.points[i]);
            // A curved segment is drawn through points along it.
            if (i > 0 && t.lane.points[i - 1].curve != 0)
            {
                const auto from = position (t, area, t.lane.points[i - 1]);
                const int steps = juce::jlimit (2, 64, (int) ((to.x - from.x) / 4));
                for (int k = 1; k < steps; ++k) { const auto x = from.x + (to.x - from.x) * (float) k / (float) steps; path.lineTo (x, yAt (t, area, i, x)); }
            }
            path.lineTo (to);
        }
        path.lineTo (area.getRight(), position (t, area, t.lane.points.back()).y);
    }
    g.setColour (colour.withAlpha (t.lane.points.empty() ? .55f : 1.0f));
    g.strokePath (path, juce::PathStrokeType (1.5f));
    for (size_t i = 0; i < t.lane.points.size(); ++i)
    {
        const auto p = position (t, area, t.lane.points[i]);
        if (p.x < area.getX() - hitRadius || p.x > area.getRight() + hitRadius) continue;
        g.setColour (pointFill); g.fillEllipse (p.x - 3.5f, p.y - 3.5f, 7, 7);
        g.setColour ((int) i == dragged ? juce::Colours::white : colour); g.drawEllipse (p.x - 3.5f, p.y - 3.5f, 7, 7, 1.5f);
    }
}
void Envelope::begin (Session& session, const Target& t)
{
    if (gesture) return;
    session.beginEditGesture ("Edit " + t.control + " automation");
    gesture = true; gestureKey = t.key;
}
bool Envelope::mouseDown (Session& session, const Target& t, juce::Rectangle<float> area, const juce::MouseEvent& e)
{
    if (! area.expanded (0, hitRadius).contains (e.position) || ! e.mods.isLeftButtonDown()) return false;
    moved = false; downAt = e.position; dragged = hitPoint (t, area, e.position); pressedPoint = dragged >= 0; curving = segment = -1;
    original = t.lane.points; group.clear();
    if (pressedPoint)
    {
        // A breakpoint inside the time selection drags every breakpoint in it.
        if (const auto range = selectedBeats ? selectedBeats (t.trackId) : std::nullopt;
            range && t.lane.points[(size_t) dragged].beat >= range->first - 1e-9 && t.lane.points[(size_t) dragged].beat <= range->second + 1e-9)
            for (size_t i = 0; i < t.lane.points.size(); ++i)
                if (t.lane.points[i].beat >= range->first - 1e-9 && t.lane.points[i].beat <= range->second + 1e-9) group.push_back (i);
        return true; // A click releases it; a drag moves it.
    }
    if (const auto segment = hitSegment (t, area, e.position))
    {
        // Option-dragging a segment between breakpoints curves it; Shift-dragging moves it.
        if (curvesSegment (e.mods))
        {
            if (*segment > 0 && *segment < t.lane.points.size()) { curving = (int) *segment - 1; curveFrom = t.lane.points[(size_t) curving].curve; }
            return true;
        }
        if (e.mods.isShiftDown())
        {
            if (*segment > 0 && *segment < t.lane.points.size()) this->segment = (int) *segment - 1;
            return true;
        }
        // Clicking a segment adds a breakpoint on it, under the pointer.
        auto lane = t.lane;
        auto point = pointAt (t, area, e.position, ! bypassesSnap (e.mods));
        if (lane.points.size() >= 4096) return true;
        const auto at = std::lower_bound (lane.points.begin(), lane.points.end(), point.beat, [] (const auto& p, double beat) { return p.beat < beat; });
        if (at != lane.points.end() && std::abs (at->beat - point.beat) < 1e-6) { dragged = (int) std::distance (lane.points.begin(), at); return true; }
        // The new breakpoint's segment keeps the curve of the one it splits.
        if (at != lane.points.begin()) point.curve = std::prev (at)->curve;
        dragged = (int) std::distance (lane.points.begin(), at);
        lane.points.insert (at, point);
        begin (session, t);
        if (write (session, t, lane).failed()) dragged = -1;
        return true;
    }
    return false;
}
bool Envelope::mouseDrag (Session& session, const Target& t, juce::Rectangle<float> area, const juce::MouseEvent& e)
{
    if (curving >= 0 && (size_t) curving + 1 < t.lane.points.size())
    {
        // Dragging up raises the segment's middle, dragging down lowers it.
        auto lane = t.lane;
        const bool rising = toProportion (t, lane.points[(size_t) curving + 1].value) > toProportion (t, lane.points[(size_t) curving].value);
        const auto up = (downAt.y - e.position.y) / std::max (1.0f, area.getHeight());
        lane.points[(size_t) curving].curve = std::clamp (curveFrom + (rising ? -1.0f : 1.0f) * up * 2.0f, -1.0f, 1.0f);
        moved = true;
        begin (session, t); write (session, t, lane);
        return true;
    }
    if (segment >= 0 && (size_t) segment + 1 < original.size() && original.size() == t.lane.points.size())
    {
        // Both its ends move up or down with the pointer.
        auto lane = t.lane;
        const auto rise = (downAt.y - e.position.y) / std::max (1.0f, area.getHeight());
        for (const auto i : { (size_t) segment, (size_t) segment + 1 })
            lane.points[i].value = fromProportion (t, toProportion (t, original[i].value) + rise);
        moved = true;
        begin (session, t); write (session, t, lane);
        return true;
    }
    if (dragged < 0 || (size_t) dragged >= t.lane.points.size()) return dragged >= 0;
    if (! moved && e.position.getDistanceFrom (downAt) < 3) return true;
    moved = true;
    if (group.size() > 1 && original.size() == t.lane.points.size())
    {
        // The selection's breakpoints move together, staying between the breakpoints around them.
        auto lane = t.lane;
        const auto& from = original[(size_t) dragged];
        const auto to = pointAt (t, area, e.position, ! bypassesSnap (e.mods));
        auto shift = to.beat - from.beat;
        const auto first = group.front(), last = group.back();
        const auto lowest = first == 0 ? -original[first].beat : original[first - 1].beat + 1e-6 - original[first].beat;
        const auto highest = last + 1 == original.size() ? maximumBeat - original[last].beat : original[last + 1].beat - 1e-6 - original[last].beat;
        shift = std::clamp (shift, lowest, std::max (lowest, highest));
        const auto rise = toProportion (t, to.value) - toProportion (t, from.value);
        for (const auto i : group)
        {
            lane.points[i].beat = original[i].beat + shift;
            lane.points[i].value = fromProportion (t, toProportion (t, original[i].value) + rise);
        }
        begin (session, t); write (session, t, lane);
        return true;
    }
    auto lane = t.lane;
    const auto index = (size_t) dragged;
    auto point = pointAt (t, area, e.position, ! bypassesSnap (e.mods));
    point.curve = lane.points[index].curve;
    // Shift gives a finer vertical step from where the drag began.
    if (e.mods.isShiftDown())
    {
        const auto start = toProportion (t, lane.points[index].value);
        const auto delta = (downAt.y - e.position.y) / std::max (1.0f, area.getHeight()) * .1f;
        point.value = fromProportion (t, start + delta); point.beat = lane.points[index].beat;
    }
    const auto lowest = index == 0 ? 0.0 : lane.points[index - 1].beat + 1e-6;
    const auto highest = index + 1 == lane.points.size() ? maximumBeat : lane.points[index + 1].beat - 1e-6;
    point.beat = std::clamp (point.beat, lowest, std::max (lowest, highest));
    lane.points[index] = point;
    begin (session, t);
    write (session, t, lane);
    return true;
}
bool Envelope::mouseUp (Session& session, const Target& t)
{
    const bool handled = dragged >= 0 || curving >= 0 || segment >= 0 || gesture;
    curving = segment = -1; group.clear();
    // Clicking a breakpoint without dragging it deletes it.
    if (pressedPoint && ! moved && dragged >= 0 && (size_t) dragged < t.lane.points.size())
    {
        auto lane = t.lane; lane.points.erase (lane.points.begin() + dragged);
        begin (session, t); write (session, t, lane);
    }
    dragged = -1; pressedPoint = false;
    if (gesture) { session.endEditGesture(); gesture = false; }
    return handled;
}
bool Envelope::doubleClick (Session& session, const Target& t, juce::Rectangle<float> area, const juce::MouseEvent& e)
{
    if (! area.contains (e.position) || hitPoint (t, area, e.position) >= 0) return false;
    auto lane = t.lane;
    // Option-double-clicking a curved segment straightens it.
    if (curvesSegment (e.mods))
    {
        if (const auto segment = hitSegment (t, area, e.position); segment && *segment > 0 && *segment < lane.points.size())
        {
            lane.points[*segment - 1].curve = 0;
            session.beginEditGesture ("Straighten " + t.control + " automation"); write (session, t, lane); session.endEditGesture();
        }
        return true;
    }
    const auto point = pointAt (t, area, e.position, ! bypassesSnap (e.mods));
    if (lane.points.size() >= 4096) return true;
    const auto at = std::lower_bound (lane.points.begin(), lane.points.end(), point.beat, [] (const auto& p, double beat) { return p.beat < beat; });
    if (at != lane.points.end() && std::abs (at->beat - point.beat) < 1e-6) return true;
    lane.points.insert (at, point);
    session.beginEditGesture ("Add " + t.control + " breakpoint"); write (session, t, lane); session.endEditGesture();
    return true;
}
Session::AutomationLane Envelope::simplified (const Target& t, double from, double to, float tolerance)
{
    auto lane = t.lane;
    std::vector<size_t> inside;
    for (size_t i = 0; i < lane.points.size(); ++i) if (lane.points[i].beat >= from - 1e-9 && lane.points[i].beat <= to + 1e-9) inside.push_back (i);
    if (inside.size() < 3) return lane;
    // Ramer-Douglas-Peucker over the lane's proportions, keeping the ends.
    std::vector<bool> keep (inside.size(), false);
    keep.front() = keep.back() = true;
    std::function<void (size_t, size_t)> reduce = [&] (size_t a, size_t b)
    {
        if (b <= a + 1) return;
        const auto& pa = lane.points[inside[a]]; const auto& pb = lane.points[inside[b]];
        float worst = 0; size_t at = a;
        for (size_t k = a + 1; k < b; ++k)
        {
            const auto& p = lane.points[inside[k]];
            const auto along = (p.beat - pa.beat) / std::max (1e-12, pb.beat - pa.beat);
            const auto line = toProportion (t, pa.value) + (float) along * (toProportion (t, pb.value) - toProportion (t, pa.value));
            const auto distance = std::abs (toProportion (t, p.value) - line);
            if (distance > worst) { worst = distance; at = k; }
        }
        if (worst > tolerance) { keep[at] = true; reduce (a, at); reduce (at, b); }
    };
    reduce (0, inside.size() - 1);
    std::vector<Session::AutomationPoint> points;
    size_t k = 0;
    for (size_t i = 0; i < lane.points.size(); ++i)
    {
        const bool in = k < inside.size() && inside[k] == i;
        if (! in || keep[k]) points.push_back (lane.points[i]);
        if (in) ++k;
    }
    lane.points = std::move (points);
    return lane;
}
}
