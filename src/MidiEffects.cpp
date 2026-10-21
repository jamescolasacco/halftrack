#include "MidiEffects.h"
#include <algorithm>
#include <cmath>

namespace halftrack::midifx
{
namespace
{
using Kind = Session::EffectKind;
int clampNote (int v) noexcept { return std::clamp (v, 0, 127); }
bool validNote (int v) noexcept { return v >= 0 && v <= 127; }
int rounded (float v) noexcept { return static_cast<int> (std::lround (v)); }
}

Event Event::noteOn (int sample, int channel, int pitch, int velocity, uint32_t source) noexcept
{ return { sample, static_cast<uint8_t> (0x90 | (channel & 15)), static_cast<uint8_t> (pitch & 127), static_cast<uint8_t> (std::clamp (velocity, 1, 127)), source }; }
Event Event::noteOff (int sample, int channel, int pitch, uint32_t source) noexcept
{ return { sample, static_cast<uint8_t> (0x80 | (channel & 15)), static_cast<uint8_t> (pitch & 127), 0, source }; }

int quantizeToScale (int pitch, int root, uint16_t mask) noexcept
{
    if ((mask & 0xfff) == 0) return pitch;
    const auto relative = pitch - root;
    const auto octave = relative >= 0 ? relative / 12 : -((11 - relative) / 12);
    const auto degree = relative - octave * 12;
    const auto inScale = [mask] (int d) { return (mask >> (((d % 12) + 12) % 12)) & 1; };
    // Nearest scale degree; equal distances resolve downward.
    for (int distance = 0; distance < 12; ++distance)
    {
        if (inScale (degree - distance)) return root + octave * 12 + degree - distance;
        if (inScale (degree + distance)) return root + octave * 12 + degree + distance;
    }
    return pitch;
}

void Processor::prepare (double sampleRate) noexcept { rate = std::isfinite (sampleRate) && sampleRate > 0 ? sampleRate : 48000; reset(); }
void Processor::configure (const Session::EffectSettings& next) noexcept
{
    const bool wasHolding = settings.kind == Kind::arpeggiator && settings.values[4] != 0;
    settings = next;
    // Turning Hold off with no physical keys releases the latched pattern.
    if (wasHolding && settings.values[4] == 0 && latchedCount == 0) heldCount = 0;
}
void Processor::reset() noexcept
{
    outputs = {}; refcount = {}; heldCount = latchedCount = pendingCount = 0;
    clock = nextOrder = arpStart = 0; seed = 0x2545f491u; alternate = arpStep = 0;
    arpRunning = false; arpChannel = arpPitch = -1;
    controlsKnown = false; lastTimeline = -1;
}
// CC Control's controls: Mod Wheel, Pitch Bend, Pressure, Custom A (a switch and its CC), Custom B
// to M (a value and a CC each), then the MIDI channel. Messages go out at the block start, before
// the block's notes.
void Processor::sendControls (double timelineSeconds) noexcept
{
    const auto& v = settings.values;
    const bool started = timelineSeconds >= 0 && (lastTimeline < 0 || timelineSeconds + 1e-9 < lastTimeline);
    lastTimeline = timelineSeconds >= 0 ? timelineSeconds + blockSamples / rate : -1;
    if (! controlsKnown) { std::copy (v.begin(), v.begin() + sentControls.size(), sentControls.begin()); controlsKnown = true; if (! started) return; }
    const bool all = started || v[29] != sentControls[29];
    const auto channel = std::clamp (static_cast<int> (v[29]) - 1, 0, 15);
    const auto differs = [&] (size_t i) { return all || v[i] != sentControls[i]; };
    const auto cc = [&] (int number, int value)
    { emit ({ 0, static_cast<uint8_t> (0xb0 | channel), static_cast<uint8_t> (std::clamp (number, 0, 127)), static_cast<uint8_t> (std::clamp (value, 0, 127)), 0 }); };
    if (differs (0)) cc (1, static_cast<int> (v[0]));
    if (differs (1))
    {
        const auto bend = std::clamp (static_cast<int> (v[1]) + 8192, 0, 16383);
        emit ({ 0, static_cast<uint8_t> (0xe0 | channel), static_cast<uint8_t> (bend & 0x7f), static_cast<uint8_t> (bend >> 7), 0 });
    }
    if (differs (2)) emit ({ 0, static_cast<uint8_t> (0xd0 | channel), static_cast<uint8_t> (std::clamp (static_cast<int> (v[2]), 0, 127)), 0, 0 });
    if (differs (3) || differs (4)) cc (static_cast<int> (v[4]), v[3] != 0 ? 127 : 0);
    for (size_t k = 0; k < 12; ++k)
        if (differs (5 + 2 * k) || differs (6 + 2 * k)) cc (static_cast<int> (v[6 + 2 * k]), static_cast<int> (v[5 + 2 * k]));
    std::copy (v.begin(), v.begin() + sentControls.size(), sentControls.begin());
}
uint32_t Processor::random() noexcept
{ seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
void Processor::emit (const Event& e) noexcept
{ if (target != nullptr && written < room) target[written++] = e; }
void Processor::sounding (int sample, int channel, int pitch, int velocity, uint32_t source) noexcept
{
    auto& count = refcount[static_cast<size_t> (channel * 128 + pitch)];
    if (count > 0) emit (Event::noteOff (sample, channel, pitch, source)); // Retrigger a shared output pitch.
    if (count < 255) ++count;
    emit (Event::noteOn (sample, channel, pitch, velocity, source));
}
void Processor::silence (int sample, int channel, int pitch, uint32_t source) noexcept
{
    auto& count = refcount[static_cast<size_t> (channel * 128 + pitch)];
    if (count == 0) return;
    if (--count == 0) emit (Event::noteOff (sample, channel, pitch, source));
}

void Processor::mapped (const Event& e) noexcept
{
    const auto channel = e.channel(), input = static_cast<int> (e.data1);
    auto& entry = outputs[static_cast<size_t> (channel * 128 + input)];
    for (int i = 0; i < entry.count; ++i) silence (e.sample, channel, entry.pitches[static_cast<size_t> (i)], e.source);
    entry.count = 0;
    if (! e.isNoteOn()) return;
    const auto& v = settings.values; const int velocity = e.data2;
    const auto add = [&] (int pitch, int outVelocity)
    {
        if (! validNote (pitch)) return;
        for (int i = 0; i < entry.count; ++i) if (entry.pitches[static_cast<size_t> (i)] == pitch) return;
        entry.pitches[static_cast<size_t> (entry.count++)] = static_cast<int8_t> (pitch);
        sounding (e.sample, channel, pitch, outVelocity, e.source);
    };
    switch (settings.kind)
    {
        case Kind::chord:
            add (input, velocity);
            for (size_t i = 0; i < 6; ++i)
                if (const auto shift = rounded (v[i]); shift != 0)
                    add (input + shift, std::clamp (static_cast<int> (std::lround (velocity * v[6 + i])), 1, 127));
            break;
        case Kind::scale:
            add (quantizeToScale (input, rounded (v[0]), scaleMasks[static_cast<size_t> (v[1])]) + rounded (v[2]), velocity);
            break;
        case Kind::pitch:
            if (input >= rounded (v[1]) && input <= rounded (v[2])) add (input + rounded (v[0]), velocity);
            break;
        case Kind::velocity:
        {
            const int mode = rounded (v[0]), inLow = rounded (v[1]), inHigh = std::max (inLow, rounded (v[2]));
            if (mode == 1 && (velocity < inLow || velocity > inHigh)) break;
            double out = rounded (v[6]);
            if (mode != 2)
            {
                const auto span = std::max (1, inHigh - inLow);
                const auto x = std::clamp ((velocity - inLow) / static_cast<double> (span), 0.0, 1.0);
                const auto curved = std::pow (x, std::exp2 (-2.0 * v[5]));
                out = rounded (v[3]) + curved * (rounded (v[4]) - rounded (v[3]));
            }
            if (v[7] > 0) out += v[7] * (2 * unit() - 1);
            add (input, std::clamp (static_cast<int> (std::lround (out)), 1, 127));
            break;
        }
        case Kind::random:
        {
            int offset = 0;
            if (unit() < v[0])
            {
                const int choices = std::max (1, rounded (v[1]));
                const int k = rounded (v[4]) == 1 ? 1 + (alternate++ % choices) : 1 + static_cast<int> (random() % static_cast<uint32_t> (choices));
                const int sign = rounded (v[3]);
                const int direction = sign == 0 ? 1 : sign == 1 ? -1 : (random() & 1 ? 1 : -1);
                offset = direction * k * rounded (v[2]);
            }
            add (clampNote (input + offset), velocity);
            break;
        }
        default: add (input, velocity); break;
    }
}

double Processor::stepSamples() const noexcept
{
    const auto& v = settings.values; const double bpm = std::clamp (static_cast<double> (settings.tempoBpm), 20.0, 999.0);
    if (settings.kind == Kind::arpeggiator) return stepBeats[static_cast<size_t> (v[1])] * 60.0 / bpm * rate;
    const auto base = v[1] != 0 ? stepBeats[static_cast<size_t> (v[3])] * 60.0 / bpm * rate : v[2] * .001 * rate;
    return std::max (1.0, base * v[4]);
}
void Processor::schedule (uint64_t time, int channel, int pitch, uint32_t source) noexcept
{
    if (pendingCount < maximumPending) pending[static_cast<size_t> (pendingCount++)] = { time, channel, pitch, source };
}
void Processor::flushPending (uint64_t until) noexcept
{
    while (pendingCount > 0)
    {
        int earliest = 0;
        for (int i = 1; i < pendingCount; ++i) if (pending[static_cast<size_t> (i)].time < pending[static_cast<size_t> (earliest)].time) earliest = i;
        const auto p = pending[static_cast<size_t> (earliest)];
        if (p.time > until) return;
        const auto sample = static_cast<int> (std::clamp<int64_t> (static_cast<int64_t> (p.time - std::min (p.time, clock)), 0, std::max (0, blockSamples - 1)));
        emit (Event::noteOff (sample, p.channel, p.pitch, p.source));
        if (p.channel == arpChannel && p.pitch == arpPitch) arpChannel = arpPitch = -1;
        pending[static_cast<size_t> (earliest)] = pending[static_cast<size_t> (--pendingCount)];
    }
}
void Processor::lengthen (const Event& e) noexcept
{
    const auto channel = e.channel(), pitch = static_cast<int> (e.data1);
    const bool onRelease = settings.values[0] != 0;
    auto& stored = outputs[static_cast<size_t> (channel * 128 + pitch)];
    int velocity = e.data2;
    if (onRelease)
    {
        if (e.isNoteOn()) { stored.count = 1; stored.pitches[0] = static_cast<int8_t> (velocity); return; }
        if (stored.count == 0) return;
        velocity = stored.pitches[0]; stored.count = 0;
    }
    else if (! e.isNoteOn()) return; // Generated lengths replace incoming releases.
    for (int i = 0; i < pendingCount; ++i)
        if (pending[static_cast<size_t> (i)].channel == channel && pending[static_cast<size_t> (i)].pitch == pitch)
        {
            emit (Event::noteOff (e.sample, channel, pitch, e.source));
            pending[static_cast<size_t> (i)] = pending[static_cast<size_t> (--pendingCount)];
            break;
        }
    emit (Event::noteOn (e.sample, channel, pitch, velocity, e.source));
    schedule (clock + static_cast<uint64_t> (e.sample) + static_cast<uint64_t> (std::llround (stepSamples())), channel, pitch, e.source);
}
void Processor::arpeggiate (const Event& e) noexcept
{
    const auto channel = e.channel(), pitch = static_cast<int> (e.data1);
    const bool hold = settings.values[4] != 0;
    const auto find = [&] { for (int i = 0; i < heldCount; ++i) if (held[static_cast<size_t> (i)].channel == channel && held[static_cast<size_t> (i)].pitch == pitch) return i; return -1; };
    if (e.isNoteOn())
    {
        if (hold && latchedCount == 0) heldCount = 0; // A new gesture replaces the latched chord.
        ++latchedCount;
        if (find() < 0 && heldCount < maximumHeld) held[static_cast<size_t> (heldCount++)] = { channel, pitch, e.data2, e.source, nextOrder++ };
    }
    else
    {
        latchedCount = std::max (0, latchedCount - 1);
        if (! hold) if (const auto i = find(); i >= 0) { std::copy (held.begin() + i + 1, held.begin() + heldCount, held.begin() + i); --heldCount; }
    }
}
void Processor::step (int sample) noexcept
{
    if (heldCount == 0) return;
    const auto& v = settings.values;
    std::array<Held, maximumHeld> order {};
    std::copy (held.begin(), held.begin() + heldCount, order.begin());
    const int style = rounded (v[0]);
    if (style == 6) std::sort (order.begin(), order.begin() + heldCount, [] (const Held& a, const Held& b) { return a.order < b.order; });
    else std::sort (order.begin(), order.begin() + heldCount, [] (const Held& a, const Held& b) { return a.pitch != b.pitch ? a.pitch < b.pitch : a.channel < b.channel; });
    const int octaves = rounded (v[3]) + 1, size = heldCount * octaves, k = arpStep++;
    int index = 0;
    const auto upDown = [size] (int n) { if (size == 1) return 0; const int period = 2 * size - 2, i = n % period; return i < size ? i : period - i; };
    const auto converge = [size] (int j) { return j % 2 == 0 ? j / 2 : size - 1 - j / 2; };
    switch (style)
    {
        case 0: case 6: index = k % size; break;
        case 1: index = size - 1 - k % size; break;
        case 2: index = upDown (k); break;
        case 3: index = size - 1 - upDown (k); break;
        case 4: index = converge (k % size); break;
        case 5: index = converge (size - 1 - k % size); break;
        default: index = static_cast<int> (random() % static_cast<uint32_t> (size)); break;
    }
    const auto& note = order[static_cast<size_t> (index % heldCount)];
    const int outPitch = note.pitch + 12 * (index / heldCount);
    if (! validNote (outPitch)) return;
    for (int i = 0; i < pendingCount; ++i)
        if (pending[static_cast<size_t> (i)].channel == note.channel && pending[static_cast<size_t> (i)].pitch == outPitch)
        {
            emit (Event::noteOff (sample, note.channel, outPitch, note.source));
            pending[static_cast<size_t> (i)] = pending[static_cast<size_t> (--pendingCount)];
            break;
        }
    emit (Event::noteOn (sample, note.channel, outPitch, v[5] != 0 ? rounded (v[6]) : note.velocity, note.source));
    arpChannel = note.channel; arpPitch = outPitch;
    const auto gate = std::max (1.0, std::round (stepSamples() * v[2]));
    schedule (clock + static_cast<uint64_t> (sample) + static_cast<uint64_t> (gate), note.channel, outPitch, note.source);
}

int Processor::allNotesOff (int sample, Event* out, int capacity) noexcept
{
    target = out; written = 0; room = capacity;
    for (int channel = 0; channel < 16; ++channel) for (int pitch = 0; pitch < 128; ++pitch)
        if (refcount[static_cast<size_t> (channel * 128 + pitch)] > 0) emit (Event::noteOff (sample, channel, pitch, 0));
    for (int i = 0; i < pendingCount; ++i) emit (Event::noteOff (sample, pending[static_cast<size_t> (i)].channel, pending[static_cast<size_t> (i)].pitch, pending[static_cast<size_t> (i)].source));
    const auto now = clock; reset(); clock = now;
    target = nullptr; return written;
}

int Processor::process (const Event* in, int inCount, int samples, double timelineSeconds, Event* out, int capacity) noexcept
{
    target = out; written = 0; room = capacity; blockSamples = std::max (0, samples);
    const auto kind = settings.kind;
    if (kind == Kind::ccControl) sendControls (timelineSeconds);
    if (kind == Kind::noteLength)
    {
        for (int i = 0; i < inCount; ++i)
        {
            flushPending (clock + static_cast<uint64_t> (std::max (0, in[i].sample)));
            if (in[i].isNoteOn() || in[i].isNoteOff()) lengthen (in[i]);
        }
        if (blockSamples > 0) flushPending (clock + static_cast<uint64_t> (blockSamples - 1));
    }
    else if (kind == Kind::arpeggiator)
    {
        // Steps sit on the transport's beat grid while playing; otherwise the
        // first key of a gesture anchors a free-running grid on the sample clock.
        const auto length = stepSamples();
        const bool timeline = timelineSeconds >= 0;
        const auto base = timeline ? static_cast<long long> (std::llround (timelineSeconds * rate)) : static_cast<long long> (clock);
        const auto stepPosition = [&] (long long k)
        { return (timeline ? 0 : static_cast<long long> (arpStart)) + static_cast<long long> (std::ceil (static_cast<double> (k) * length - 1.0e-6)); };
        const auto firstStepFrom = [&] (long long position)
        {
            auto k = static_cast<long long> (std::floor (static_cast<double> (position - (timeline ? 0 : static_cast<long long> (arpStart))) / length));
            while (stepPosition (k) < position) ++k;
            while (stepPosition (k - 1) >= position) --k;
            return stepPosition (k);
        };
        int next = 0, sample = 0;
        for (;;)
        {
            const auto boundary = arpRunning ? static_cast<int> (std::min<long long> (firstStepFrom (base + sample) - base, blockSamples)) : blockSamples;
            if (next < inCount)
            {
                const auto eventSample = std::clamp (in[next].sample, sample, std::max (sample, blockSamples - 1));
                if (eventSample <= boundary)
                {
                    flushPending (clock + static_cast<uint64_t> (eventSample));
                    if (in[next].isNoteOn() || in[next].isNoteOff()) arpeggiate (in[next]);
                    ++next; sample = eventSample;
                    if (heldCount > 0 && ! arpRunning)
                    {
                        arpRunning = true; arpStep = 0;
                        if (! timeline) arpStart = clock + static_cast<uint64_t> (eventSample);
                    }
                    if (heldCount == 0) arpRunning = false;
                    continue;
                }
            }
            if (boundary >= blockSamples) break;
            flushPending (clock + static_cast<uint64_t> (boundary));
            step (boundary);
            sample = boundary + 1;
        }
        if (blockSamples > 0) flushPending (clock + static_cast<uint64_t> (blockSamples - 1));
    }
    else
        for (int i = 0; i < inCount; ++i) if (in[i].isNoteOn() || in[i].isNoteOff()) mapped (in[i]);
    clock += static_cast<uint64_t> (blockSamples);
    target = nullptr;
    return written;
}
}
