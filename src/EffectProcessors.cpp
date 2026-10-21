#include "EffectProcessors.h"

namespace halftrack::effects
{
namespace
{
int rampSteps (double rate) noexcept { return std::max (1, static_cast<int> (rate * .005)); }
double decibels (double db) noexcept { return std::pow (10.0, db / 20.0); }
double onePole (double milliseconds, double rate) noexcept { return std::exp (-1.0 / (rate * std::max (milliseconds, 1.0e-6) * .001)); }
double sign (double x) noexcept { return x < 0 ? -1.0 : 1.0; }
std::array<double, 5> normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{ return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 }; }
struct Prewarp { double cosine, alpha, omega; };
Prewarp prewarp (double frequency, double q, double rate) noexcept
{
    const auto omega = 2 * pi * std::clamp (frequency, 5.0, rate * .45) / rate;
    return { std::cos (omega), std::sin (omega) / (2 * std::max (q, 1.0e-3)), omega };
}
}

double lfoRateHz (bool synchronized, double hertz, int division, double bpm) noexcept
{
    if (! synchronized) return hertz;
    const auto beats = lfoDivisionBeats[static_cast<size_t> (std::clamp (division, 0, static_cast<int> (lfoDivisionBeats.size()) - 1))];
    return std::clamp (bpm, 20.0, 999.0) / 60.0 / beats;
}

void Ramp::set (double next, bool immediate, int steps) noexcept
{
    target = next;
    if (immediate) { value = next; remaining = 0; }
    else remaining = std::max (1, steps);
}
double Ramp::tick() noexcept
{
    if (remaining > 0) { value += (target - value) / remaining; --remaining; }
    return value;
}

double Biquad::tick (double input) noexcept
{
    const auto out = finite (c[0] * input + z1);
    z1 = finite (c[1] * input - c[3] * out + z2); z2 = finite (c[2] * input - c[4] * out);
    return out;
}
std::array<double, 5> lowPassSection (double frequency, double q, double rate) noexcept
{
    const auto p = prewarp (frequency, q, rate);
    return normalise ((1 - p.cosine) / 2, 1 - p.cosine, (1 - p.cosine) / 2, 1 + p.alpha, -2 * p.cosine, 1 - p.alpha);
}
std::array<double, 5> highPassSection (double frequency, double q, double rate) noexcept
{
    const auto p = prewarp (frequency, q, rate);
    return normalise ((1 + p.cosine) / 2, -(1 + p.cosine), (1 + p.cosine) / 2, 1 + p.alpha, -2 * p.cosine, 1 - p.alpha);
}
std::array<double, 5> allPassSection (double frequency, double q, double rate) noexcept
{
    const auto p = prewarp (frequency, q, rate);
    return normalise (1 - p.alpha, -2 * p.cosine, 1 + p.alpha, 1 + p.alpha, -2 * p.cosine, 1 - p.alpha);
}
std::array<double, 5> peakSection (double frequency, double gainDb, double q, double rate) noexcept
{
    if (gainDb == 0) return { 1, 0, 0, 0, 0 };
    const auto p = prewarp (frequency, q, rate); const auto a = std::pow (10.0, gainDb / 40);
    return normalise (1 + p.alpha * a, -2 * p.cosine, 1 - p.alpha * a, 1 + p.alpha / a, -2 * p.cosine, 1 - p.alpha / a);
}
std::array<double, 5> lowShelfSection (double frequency, double gainDb, double q, double rate) noexcept
{
    if (gainDb == 0) return { 1, 0, 0, 0, 0 };
    const auto p = prewarp (frequency, q, rate); const auto a = std::pow (10.0, gainDb / 40), root = 2 * std::sqrt (a) * p.alpha;
    return normalise (a * ((a + 1) - (a - 1) * p.cosine + root), 2 * a * ((a - 1) - (a + 1) * p.cosine), a * ((a + 1) - (a - 1) * p.cosine - root),
                      (a + 1) + (a - 1) * p.cosine + root, -2 * ((a - 1) + (a + 1) * p.cosine), (a + 1) + (a - 1) * p.cosine - root);
}
std::array<double, 5> highShelfSection (double frequency, double gainDb, double q, double rate) noexcept
{
    if (gainDb == 0) return { 1, 0, 0, 0, 0 };
    const auto p = prewarp (frequency, q, rate); const auto a = std::pow (10.0, gainDb / 40), root = 2 * std::sqrt (a) * p.alpha;
    return normalise (a * ((a + 1) + (a - 1) * p.cosine + root), -2 * a * ((a - 1) + (a + 1) * p.cosine), a * ((a + 1) + (a - 1) * p.cosine - root),
                      (a + 1) - (a - 1) * p.cosine + root, 2 * ((a - 1) - (a + 1) * p.cosine), (a + 1) - (a - 1) * p.cosine - root);
}
std::array<double, 5> bandPassOctaves (double frequency, double octaves, double rate) noexcept
{
    const auto omega = 2 * pi * std::clamp (frequency, 5.0, rate * .45) / rate, s = std::sin (omega);
    const auto alpha = s * std::sinh (std::log (2.0) / 2 * std::clamp (octaves, .05, 12.0) * omega / s);
    return normalise (alpha, 0, -alpha, 1 + alpha, -2 * std::cos (omega), 1 - alpha);
}

void Lfo::setRate (double hertz, double rate) noexcept
{ increment = std::clamp (std::isfinite (hertz) ? hertz : 0.0, 0.0, rate * .5) / rate; }
double Lfo::value (double offsetCycles, int shape) const noexcept
{
    auto p = phase + offsetCycles; const auto whole = std::floor (p); p -= whole;
    switch (shape)
    {
        case 0: return std::sin (2 * pi * p);
        case 1: return p < .25 ? 4 * p : p < .75 ? 2 - 4 * p : 4 * p - 4;
        case 2: return 2 * p - 1;
        case 3: return 1 - 2 * p;
        case 4: return p < .5 ? 1.0 : -1.0;
        default:
        {
            // Hash the cycle index: every channel offset sees the same held sequence.
            auto h = (static_cast<uint32_t> (cycles + static_cast<uint64_t> (static_cast<int64_t> (whole))) + 0x9e3779b9u) * 2654435761u;
            h ^= h >> 16; h *= 2246822519u; h ^= h >> 13;
            return static_cast<double> (h >> 8) / 8388607.5 - 1.0;
        }
    }
}
void Lfo::syncTo (double seconds, double sampleRate) noexcept
{
    const auto total = seconds * increment * sampleRate; const auto whole = std::floor (total);
    if (! std::isfinite (total)) return;
    cycles = static_cast<uint64_t> (static_cast<int64_t> (whole)); phase = total - whole;
}
void Lfo::advance() noexcept
{
    phase += increment;
    if (phase >= 1) { const auto whole = std::floor (phase); phase -= whole; cycles += static_cast<uint64_t> (whole); }
}

double StateVariable::tick (double input, double g, double k, int mode) noexcept
{
    const auto a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const auto v3 = input - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = finite (2 * v1 - ic1); ic2 = finite (2 * v2 - ic2);
    switch (mode)
    {
        case 0: return v2;
        case 1: return input - k * v1 - v2;
        case 2: return k * v1;
        default: return input - k * v1;
    }
}

double Saturator::shape (int curve, double x) noexcept
{
    switch (curve)
    {
        case 0: return std::tanh (x);
        case 1: return std::abs (x) < 1 ? std::sin (pi / 2 * x) : sign (x);
        case 2: return x / std::sqrt (1 + x * x);
        case 3: return std::abs (x) < 1 ? 1.5 * x - .5 * x * x * x : sign (x);
        case 4: return std::sin (pi / 2 * x);
        default: return std::clamp (x, -1.0, 1.0);
    }
}
double Saturator::softClip (double x) noexcept
{
    constexpr double knee = .5;
    const auto magnitude = std::abs (x);
    return magnitude <= knee ? x : sign (x) * (knee + (1 - knee) * std::tanh ((magnitude - knee) / (1 - knee)));
}
void Saturator::prepare (double sampleRate) noexcept { rate = sampleRate; dcPole = std::exp (-2 * pi * 10 / rate); reset(); }
void Saturator::configure (const float* v, bool immediate) noexcept
{
    drive.set (decibels (v[0]), immediate, rampSteps (rate)); curve = static_cast<int> (v[1]);
    output.set (decibels (v[2]), immediate, rampSteps (rate)); dc = v[3] != 0; clip = v[4] != 0;
}
void Saturator::reset() noexcept { dcInput = {}; dcOutput = {}; drive.remaining = output.remaining = 0; drive.value = drive.target; output.value = output.target; }
void Saturator::tick (double& left, double& right) noexcept
{
    const auto d = drive.tick(), o = output.tick();
    double* channels[] { &left, &right };
    for (size_t c = 0; c < 2; ++c)
    {
        auto y = shape (curve, *channels[c] * d);
        if (dc) { const auto in = y; y = finite (in - dcInput[c] + dcPole * dcOutput[c]); dcInput[c] = in; dcOutput[c] = y; }
        y *= o;
        *channels[c] = clip ? softClip (y) : y;
    }
}

void Overdrive::prepare (double sampleRate) noexcept
{ rate = sampleRate; dcPole = std::exp (-2 * pi * 10 / rate); release = onePole (20, rate); reset(); }
void Overdrive::configure (const float* v, bool) noexcept
{
    for (auto& b : band) b.c = bandPassOctaves (v[0], v[1], rate);
    drive = decibels (v[2] * 36.0); bias = .2 * v[2]; dynamics = v[4];
    tone = std::exp (-2 * pi * std::min (400 * std::pow (50.0, static_cast<double> (v[3])), rate * .45) / rate);
}
void Overdrive::reset() noexcept
{
    for (auto& b : band) b.clear();
    toneState = {}; dcInput = {}; dcOutput = {}; inputLevel = {}; shapedLevel = {};
}
void Overdrive::tick (double& left, double& right) noexcept
{
    double* channels[] { &left, &right };
    const auto offset = std::tanh (bias);
    for (size_t c = 0; c < 2; ++c)
    {
        const auto filtered = band[c].tick (*channels[c]);
        const auto shaped = std::tanh (drive * filtered + bias) - offset;
        inputLevel[c] = std::max (std::abs (filtered), inputLevel[c] * release);
        shapedLevel[c] = std::max (std::abs (shaped), shapedLevel[c] * release);
        const auto restore = std::min (4.0, inputLevel[c] / std::max (shapedLevel[c], 1.0e-9));
        const auto dynamic = shaped * ((1 - dynamics) + dynamics * restore);
        toneState[c] = finite ((1 - tone) * dynamic + tone * toneState[c]);
        const auto y = finite (toneState[c] - dcInput[c] + dcPole * dcOutput[c]);
        dcInput[c] = toneState[c]; dcOutput[c] = y; *channels[c] = y;
    }
}

void AutoFilter::prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }
void AutoFilter::configure (const float* v, float bpm, bool immediate) noexcept
{
    const auto nextMode = static_cast<int> (v[0]); const auto nextSteep = v[1] != 0;
    if (nextMode != mode || nextSteep != steep) for (auto& channel : filters) for (auto& f : channel) f.clear();
    mode = nextMode; steep = nextSteep;
    logFrequency.set (std::log2 (static_cast<double> (v[2])), immediate, rampSteps (rate));
    k = 1 / qFromResonance (v[3]); lfoAmount = v[4];
    lfo.setRate (lfoRateHz (v[6] != 0, v[5], static_cast<int> (v[7]), bpm), rate);
    shape = static_cast<int> (v[8]); phaseOffset = v[9] / 360.0; envelopeAmount = v[10];
    attack = onePole (v[11], rate); release = onePole (v[12], rate);
}
void AutoFilter::reset() noexcept
{
    for (auto& channel : filters) for (auto& f : channel) f.clear();
    lfo.reset(); envelope = 0; logFrequency.value = logFrequency.target; logFrequency.remaining = 0;
}
void AutoFilter::syncTo (double seconds) noexcept { lfo.syncTo (seconds, rate); }
void AutoFilter::tick (double& left, double& right) noexcept
{
    const auto detected = std::max (std::abs (left), std::abs (right));
    const auto coefficient = detected > envelope ? attack : release;
    envelope = coefficient * envelope + (1 - coefficient) * detected;
    const auto base = logFrequency.tick();
    const auto secondK = mode < 2 ? std::sqrt (2.0) : k;
    double* channels[] { &left, &right };
    for (size_t c = 0; c < 2; ++c)
    {
        const auto octaves = lfoAmount * 4 * lfo.value (static_cast<double> (c) * phaseOffset, shape) + envelopeAmount * 4 * std::min (envelope, 1.0);
        const auto frequency = std::clamp (std::exp2 (base + octaves), 20.0, rate * .49);
        const auto g = std::tan (pi * frequency / rate);
        auto y = filters[c][0].tick (*channels[c], g, k, mode);
        if (steep) y = filters[c][1].tick (y, g, secondK, mode);
        *channels[c] = y;
    }
    lfo.advance();
}

void AutoPan::prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }
void AutoPan::configure (const float* v, float bpm, bool immediate) noexcept
{
    amount.set (v[0], immediate, rampSteps (rate));
    lfo.setRate (lfoRateHz (v[2] != 0, v[1], static_cast<int> (v[3]), bpm), rate);
    shape = static_cast<int> (v[4]); phase = v[5] / 360.0; offset = v[6] / 360.0;
}
void AutoPan::reset() noexcept { lfo.reset(); amount.value = amount.target; amount.remaining = 0; }
void AutoPan::syncTo (double seconds) noexcept { lfo.syncTo (seconds, rate); }
void AutoPan::tick (double& left, double& right) noexcept
{
    const auto depth = amount.tick();
    left *= channelGain (depth, lfo.value (offset, shape));
    right *= channelGain (depth, lfo.value (offset + phase, shape));
    lfo.advance();
}

void ModulatedDelay::prepare (double sampleRate, double maximumMs)
{
    rate = sampleRate;
    for (auto& b : buffers) b.assign (static_cast<size_t> (std::ceil (rate * maximumMs * .001)) + 4, 0);
    reset();
}
void ModulatedDelay::configure (double delayMs, double depthMs, double rateHz, double feedbackAmount, double spreadDegrees, int voiceCount, bool immediate) noexcept
{
    delay.set (delayMs, immediate, rampSteps (rate)); depth.set (depthMs, immediate, rampSteps (rate));
    feedback.set (feedbackAmount, immediate, rampSteps (rate)); lfo.setRate (rateHz, rate);
    spread = spreadDegrees / 360.0; voices = std::clamp (voiceCount, 1, 3);
}
void ModulatedDelay::reset() noexcept
{
    for (auto& b : buffers) std::fill (b.begin(), b.end(), 0);
    cursor = 0; lfo.reset();
    for (auto* r : { &delay, &depth, &feedback }) { r->value = r->target; r->remaining = 0; }
}
void ModulatedDelay::syncTo (double seconds) noexcept { lfo.syncTo (seconds, rate); }
double ModulatedDelay::tap (size_t channel, double frames) const noexcept
{
    const auto& data = buffers[channel]; if (data.size() < 4) return 0;
    frames = std::clamp (frames, 1.0, static_cast<double> (data.size() - 2));
    const auto whole = static_cast<size_t> (frames); const auto fraction = frames - static_cast<double> (whole);
    const auto a = (cursor + data.size() - whole) % data.size(), b = (a + data.size() - 1) % data.size();
    return data[a] * (1 - fraction) + data[b] * fraction;
}
void ModulatedDelay::tick (double& left, double& right) noexcept
{
    if (buffers[0].size() < 4) return;
    const auto base = delay.tick(), swing = depth.tick(), amount = feedback.tick();
    double* channels[] { &left, &right };
    for (size_t c = 0; c < 2; ++c)
    {
        double sum = 0;
        for (int voice = 0; voice < voices; ++voice)
        {
            const auto offset = static_cast<double> (c) * spread + static_cast<double> (voice) / voices;
            const auto ms = base + swing * .5 * (1 + lfo.value (offset, 0));
            sum += tap (c, ms * rate * .001);
        }
        const auto y = sum / voices;
        buffers[c][cursor] = finite (*channels[c] + amount * y);
        *channels[c] = y;
    }
    cursor = (cursor + 1) % buffers[0].size(); lfo.advance();
}

double Phaser::allPassCoefficient (double frequency, double rate) noexcept
{ const auto t = std::tan (pi * std::clamp (frequency, 10.0, rate * .45) / rate); return (t - 1) / (t + 1); }
void Phaser::prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }
void Phaser::configure (const float* v, bool immediate) noexcept
{
    const auto nextStages = 2 * (static_cast<int> (v[0]) + 1);
    if (nextStages != stages) { state = {}; last = {}; }
    stages = nextStages; logCenter.set (std::log2 (static_cast<double> (v[1])), immediate, rampSteps (rate));
    depth = v[2]; lfo.setRate (v[3], rate); feedback.set (v[4], immediate, rampSteps (rate)); spread = v[5] / 360.0;
}
void Phaser::reset() noexcept
{
    state = {}; last = {}; lfo.reset();
    for (auto* r : { &logCenter, &feedback }) { r->value = r->target; r->remaining = 0; }
}
void Phaser::syncTo (double seconds) noexcept { lfo.syncTo (seconds, rate); }
void Phaser::tick (double& left, double& right) noexcept
{
    const auto center = logCenter.tick(), amount = feedback.tick();
    double* channels[] { &left, &right };
    for (size_t c = 0; c < 2; ++c)
    {
        const auto a = allPassCoefficient (std::exp2 (center + depth * 3 * lfo.value (static_cast<double> (c) * spread, 0)), rate);
        auto x = *channels[c] + amount * last[c];
        for (int s = 0; s < stages; ++s)
        {
            auto& z = state[c][static_cast<size_t> (s)];
            const auto y = a * x + z; z = finite (x - a * y); x = y;
        }
        last[c] = finite (x); *channels[c] = last[c];
    }
    lfo.advance();
}

double ThreeBandEq::Chain::tick (double x) noexcept
{
    for (int i = 0; i < count; ++i) x = sections[static_cast<size_t> (i)].tick (x);
    return x;
}
void ThreeBandEq::prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }
void ThreeBandEq::configure (const float* v, bool immediate) noexcept
{
    const auto gain = [] (float db, float on) { return on != 0 && db > -70 ? decibels (db) : 0.0; };
    low.set (gain (v[0], v[6]), immediate, rampSteps (rate));
    mid.set (gain (v[1], v[7]), immediate, rampSteps (rate));
    high.set (gain (v[2], v[8]), immediate, rampSteps (rate));
    const double lowHz = v[3], highHz = std::max (v[3], v[4]);
    // Linkwitz-Riley: squared Butterworth. 24 dB uses Q 0.7071; 48 dB uses the
    // fourth-order Butterworth pair 0.5412 / 1.3066. The aligned low band uses the
    // matching all-pass, which equals the second crossover's summed response.
    const bool steep = v[5] != 0;
    const std::array<double, 4> q = steep ? std::array<double, 4> { .54119610, 1.30656296, .54119610, 1.30656296 }
                                          : std::array<double, 4> { .70710678, .70710678, 0, 0 };
    const int count = steep ? 4 : 2;
    for (size_t c = 0; c < 2; ++c)
    {
        for (auto* chain : { &lowLow[c], &lowHigh[c], &highLow[c], &highHigh[c], &align[c] })
            if (chain->count != (chain == &align[c] ? count / 2 : count)) { chain->clear(); chain->count = chain == &align[c] ? count / 2 : count; }
        for (int i = 0; i < count; ++i)
        {
            const auto s = static_cast<size_t> (i);
            lowLow[c].sections[s].c = lowPassSection (lowHz, q[s], rate);
            lowHigh[c].sections[s].c = highPassSection (lowHz, q[s], rate);
            highLow[c].sections[s].c = lowPassSection (highHz, q[s], rate);
            highHigh[c].sections[s].c = highPassSection (highHz, q[s], rate);
        }
        for (int i = 0; i < count / 2; ++i)
            align[c].sections[static_cast<size_t> (i)].c = allPassSection (highHz, q[static_cast<size_t> (i)], rate);
    }
}
void ThreeBandEq::reset() noexcept
{
    for (size_t c = 0; c < 2; ++c) for (auto* chain : { &lowLow[c], &lowHigh[c], &highLow[c], &highHigh[c], &align[c] }) chain->clear();
    for (auto* r : { &low, &mid, &high }) { r->value = r->target; r->remaining = 0; }
}
void ThreeBandEq::tick (double& left, double& right) noexcept
{
    const auto gl = low.tick(), gm = mid.tick(), gh = high.tick();
    double* channels[] { &left, &right };
    for (size_t c = 0; c < 2; ++c)
    {
        const auto x = *channels[c];
        const auto lowBand = align[c].tick (lowLow[c].tick (x)), upper = lowHigh[c].tick (x);
        *channels[c] = gl * lowBand + gm * highLow[c].tick (upper) + gh * highHigh[c].tick (upper);
    }
}

void GlueCompressor::prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }
void GlueCompressor::configure (const float* v, bool immediate) noexcept
{
    threshold = v[0]; slope = 1.0 / ratios[static_cast<size_t> (v[1])] - 1.0;
    attack = onePole (attackMs[static_cast<size_t> (v[2])], rate);
    automatic = static_cast<size_t> (v[3]) >= releaseSeconds.size();
    release = onePole (1000 * releaseSeconds[std::min (static_cast<size_t> (v[3]), releaseSeconds.size() - 1)], rate);
    fastRelease = onePole (100, rate); slowRelease = onePole (1200, rate);
    makeup.set (decibels (v[4]), immediate, rampSteps (rate)); range = v[5]; clip = v[6] != 0;
}
void GlueCompressor::reset() noexcept { envelope = fast = slow = 0; makeup.value = makeup.target; makeup.remaining = 0; }
void GlueCompressor::tick (double& left, double& right) noexcept
{
    const auto peak = std::max (std::abs (left), std::abs (right));
    const auto follow = [peak, this] (double& state, double releasing)
    { const auto c = peak > state ? attack : releasing; state = c * state + (1 - c) * peak; };
    double level;
    if (automatic) { follow (fast, fastRelease); follow (slow, slowRelease); level = .5 * (fast + slow); }
    else { follow (envelope, release); level = envelope; }
    const auto over = 20 * std::log10 (std::max (level, 1.0e-20)) - threshold;
    const auto reduction = over > 0 ? std::max (slope * over, -range) : 0.0;
    const auto gain = decibels (reduction) * makeup.tick();
    left *= gain; right *= gain;
    if (clip) { left = Saturator::softClip (left); right = Saturator::softClip (right); }
}

namespace
{
// Two 90-degree-offset all-pass cascades (Olli Niemitalo's published Hilbert
// network design); one path is delayed by a sample to complete the quadrature.
constexpr std::array<double, 4> pathA { .6923878, .9360654322959, .9882295226860, .9987488452737 };
constexpr std::array<double, 4> pathB { .4021921162426, .8561710882420, .9722909545651, .9952884791278 };
double allPassCascade (std::array<std::array<double, 4>, 4>& states, const std::array<double, 4>& coefficients, double x) noexcept
{
    for (size_t i = 0; i < 4; ++i)
    {
        auto& s = states[i]; // x1, x2, y1, y2
        const auto y = finite (coefficients[i] * coefficients[i] * (x + s[3]) - s[1]);
        s = { x, s[0], y, s[2] }; x = y;
    }
    return x;
}
}
void FrequencyShifter::Quadrature::process (double input, double& inPhase, double& quadrature) noexcept
{
    inPhase = delayed; delayed = allPassCascade (a, pathA, input);
    quadrature = allPassCascade (b, pathB, input);
}
void FrequencyShifter::prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }
void FrequencyShifter::configure (const float* v, bool immediate) noexcept
{ shift.set (v[0], immediate, rampSteps (rate)); ring = v[1] != 0; opposite = v[2] != 0; }
void FrequencyShifter::reset() noexcept { for (auto& n : networks) n.clear(); phase = 0; shift.value = shift.target; shift.remaining = 0; }
void FrequencyShifter::syncTo (double seconds) noexcept
{ const auto total = seconds * shift.value; if (std::isfinite (total)) phase = total - std::floor (total); }
void FrequencyShifter::tick (double& left, double& right) noexcept
{
    const auto c = std::cos (2 * pi * phase), s = std::sin (2 * pi * phase);
    double* channels[] { &left, &right };
    for (size_t channel = 0; channel < 2; ++channel)
    {
        double i = 0, q = 0; const auto x = *channels[channel];
        networks[channel].process (x, i, q);
        const auto direction = opposite && channel == 1 ? -1.0 : 1.0;
        *channels[channel] = ring ? x * c : i * c + direction * q * s;
    }
    phase += shift.tick() / rate; phase -= std::floor (phase);
}

void ChannelEq::prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }
void ChannelEq::configure (const float* v) noexcept
{
    highPass = v[0] != 0; lowPass = v[4] < 0;
    for (auto& channel : sections)
    {
        channel[0].c = highPassSection (highPassHz, butterworthQ, rate);
        channel[1].c = lowShelfSection (lowShelfHz, v[1], butterworthQ, rate);
        channel[2].c = peakSection (v[3], v[2], midQ, rate);
        channel[3].c = highShelfSection (highShelfHz, v[4], butterworthQ, rate);
        channel[4].c = lowPassSection (lowPassHz (v[4]), butterworthQ, rate);
    }
}
void ChannelEq::reset() noexcept { for (auto& channel : sections) for (auto& s : channel) s.clear(); }
void ChannelEq::tick (double& left, double& right) noexcept
{
    double* channels[] { &left, &right };
    for (size_t c = 0; c < 2; ++c)
    {
        auto& s = sections[c]; auto x = *channels[c];
        if (highPass) x = s[0].tick (x);
        x = s[3].tick (s[2].tick (s[1].tick (x)));
        if (lowPass) x = s[4].tick (x);
        *channels[c] = x;
    }
}

double Pedal::shape (int type, double x) noexcept
{
    switch (type)
    {
        case 0: return std::tanh (x + .2) - std::tanh (.2);
        case 1: return 2 / pi * std::atan (3 * x);
        default: return std::tanh (4 * (x + .35 * x * x));
    }
}
void Pedal::prepare (double sampleRate) noexcept { rate = sampleRate; dcPole = std::exp (-2 * pi * dcHz / rate); reset(); }
void Pedal::configure (const float* v) noexcept
{
    type = (int) v[0]; drive = driveFor (v[1]); sub = v[7] != 0;
    const auto mid = (size_t) std::clamp ((int) v[5], 0, 2);
    for (auto& channel : eq)
    {
        channel[0].c = peakSection (bassHz, v[3], resonance (.7, v[3]), rate);
        channel[1].c = peakSection (midHz[mid], v[4], resonance (midQ[mid], v[4]), rate);
        channel[2].c = highShelfSection (trebleHz, v[6], resonance (.70710678118654752, v[6]), rate);
        channel[3].c = lowShelfSection (subHz, subDb, .70710678118654752, rate);
    }
}
void Pedal::reset() noexcept { for (auto& channel : eq) for (auto& s : channel) s.clear(); dcInput = {}; dcOutput = {}; }
void Pedal::tick (double& left, double& right) noexcept
{
    double* channels[] { &left, &right };
    for (size_t c = 0; c < 2; ++c)
    {
        const auto shaped = shape (type, *channels[c] * drive);
        // The asymmetric curves leave DC; a one-pole high-pass removes it.
        const auto blocked = shaped - dcInput[c] + dcPole * dcOutput[c];
        dcInput[c] = shaped; dcOutput[c] = finite (blocked);
        auto& s = eq[c];
        auto x = s[2].tick (s[1].tick (s[0].tick (dcOutput[c])));
        if (sub) x = s[3].tick (x);
        *channels[c] = x;
    }
}

void BeatRepeat::prepare (double sampleRate) noexcept
{
    rate = sampleRate;
    for (auto& channel : capture) channel.assign ((size_t) std::ceil (rate * longestSliceSeconds) + 4, 0.0f);
    reset();
}
void BeatRepeat::configure (const float* values, double tempo) noexcept
{
    const bool wasHeld = v[0] != 0;
    std::copy (values, values + v.size(), v.begin());
    bpm = std::clamp (tempo, 20.0, 999.0);
    const auto centre = (double) v[12], half = std::pow (2.0, (double) v[13] / 2);
    for (auto& channel : filter)
    {
        channel[0].c = highPassSection (std::max (20.0, centre / half), .70710678118654752, rate);
        channel[1].c = lowPassSection (std::min (rate * .45, centre * half), .70710678118654752, rate);
    }
    if (wasHeld && v[0] == 0 && held) { active = false; held = false; }
}
void BeatRepeat::reset() noexcept
{
    active = held = synced = false; frame = sliceIndex = 0; random = 0x9e3779b97f4a7c15ull;
    for (auto& channel : capture) std::fill (channel.begin(), channel.end(), 0.0f);
    for (auto& channel : filter) for (auto& s : channel) s.clear();
}
void BeatRepeat::syncTo (double seconds) noexcept
{
    const auto now = seconds * bpm / 60;
    const auto interval = intervalBeats[(size_t) std::clamp ((int) v[1], 0, 7)];
    const auto offset = std::fmod ((double) v[2] / 4, interval);
    // A start, seek or loop jump schedules the next trigger afresh and ends a timed repeat;
    // steady playback keeps its schedule.
    const bool jumped = beatStep == 0 || std::abs (now - beat) > beatStep * 2 + 1e-9;
    beatStep = bpm / 60 / rate;
    if (jumped)
    {
        nextTrigger = std::ceil ((now - offset) / interval - 1e-9) * interval + offset;
        if (! held) active = false;
    }
    beat = now; synced = true;
}
double BeatRepeat::nextUniform() noexcept
{
    random ^= random << 13; random ^= random >> 7; random ^= random << 17;
    return (double) (random >> 11) * (1.0 / 9007199254740992.0);
}
// The grid for a new slice: the chosen size, or with Variation one up to Variation/2.5 steps away.
int BeatRepeat::pickGrid() noexcept
{
    int grid = std::clamp ((int) v[5], 0, 7);
    if (v[7] > 0)
    {
        const int reach = (int) std::lround (v[7] / 2.5);
        grid = std::clamp (grid + (int) std::lround ((nextUniform() * 2 - 1) * reach), 0, 7);
    }
    if (v[6] != 0 && grid % 2 == 1) grid -= 1; // No Triplets keeps binary sizes.
    return grid;
}
void BeatRepeat::start (double at, bool hold) noexcept
{
    active = true; held = hold; frame = 0; sliceIndex = 0; sliceStart = 0; capturedFrames = 0;
    episodeEnd = at + (double) v[4] / 4;
    sliceFrames = std::min (gridBeats[(size_t) pickGrid()] * 60 / bpm * rate, (double) capture[0].size() - 2);
    for (auto& channel : filter) for (auto& s : channel) s.clear();
}
void BeatRepeat::tick (double& left, double& right) noexcept
{
    const auto mode = (int) v[14];
    const double in[] { left, right };
    if (! synced) { if (mode == 2) left = right = 0; active = active && held; return; }
    const auto interval = intervalBeats[(size_t) std::clamp ((int) v[1], 0, 7)];
    if (v[0] != 0 && ! held) start (beat, true);
    else if (! held && beat >= nextTrigger - 1e-9)
    {
        if (nextUniform() < v[3]) start (nextTrigger, false);
        while (nextTrigger <= beat + 1e-9) nextTrigger += interval;
    }
    if (active && ! held && beat >= episodeEnd - 1e-9) active = false;
    if (! active)
    {
        if (mode == 2) left = right = 0;
        beat += beatStep; return;
    }
    // Slice boundaries: Auto picks a new size after each repeat, and the 1/4, 1/8 and 1/16 modes at those notes.
    if (frame - sliceStart >= sliceFrames - 1e-9)
    {
        sliceStart += sliceFrames; ++sliceIndex;
        const auto variationMode = (int) v[8];
        const bool vary = variationMode == 4 || (variationMode >= 1 && variationMode <= 3
            && std::fmod (beat + 1e-9, std::array { 1.0, .5, .25 }[(size_t) variationMode - 1]) < beatStep * 1.5);
        if (vary && v[7] > 0) sliceFrames = std::min (gridBeats[(size_t) pickGrid()] * 60 / bpm * rate, capturedFrames);
    }
    const auto within = (double) frame - sliceStart;
    double out[2];
    if (sliceIndex == 0)
    {
        // The first slice is the input itself, captured as it passes.
        for (size_t c = 0; c < 2; ++c) { if ((size_t) frame < capture[c].size()) capture[c][(size_t) frame] = (float) in[c]; out[c] = in[c]; }
        capturedFrames = std::max (capturedFrames, (double) frame + 1);
    }
    else
    {
        const auto semitones = (double) v[9] - (double) v[10] * 12 * (double) (sliceIndex - 1);
        const auto read = within * std::pow (2.0, semitones / 12);
        const auto gain = std::pow (10.0, (double) v[15] / 20) * std::pow (1.0 - (double) v[16], (double) sliceIndex);
        for (size_t c = 0; c < 2; ++c)
        {
            const auto whole = (size_t) read; const auto fraction = read - (double) whole;
            const auto a = whole < (size_t) capturedFrames ? capture[c][whole] : 0.0f, b = whole + 1 < (size_t) capturedFrames ? capture[c][whole + 1] : a;
            auto x = (a + (b - a) * fraction) * gain;
            if (v[11] != 0) x = filter[c][1].tick (filter[c][0].tick (x));
            out[c] = x;
        }
    }
    if (mode == 0) { left = in[0] + (sliceIndex > 0 ? out[0] : 0); right = in[1] + (sliceIndex > 0 ? out[1] : 0); }
    else { left = out[0]; right = out[1]; }
    ++frame; beat += beatStep;
}

void FilterDelay::prepare (double sampleRate) noexcept
{
    rate = sampleRate;
    for (auto& line : lines) line.buffer.assign ((size_t) std::ceil (rate * longestSeconds) + 4, 0.0f);
    reset();
}
void FilterDelay::configure (const float* v, double bpm) noexcept
{
    for (size_t k = 0; k < lines.size(); ++k)
    {
        const auto* p = v + k * controlsPerLine;
        auto& line = lines[k];
        line.on = p[0] != 0; line.filtered = p[1] != 0;
        const auto half = std::pow (2.0, (double) p[3] / 2);
        line.highPass.c = highPassSection (std::max (20.0, p[2] / half), .70710678118654752, rate);
        line.lowPass.c = lowPassSection (std::min (rate * .45, p[2] * half), .70710678118654752, rate);
        line.delay = std::clamp (delaySeconds (p, bpm) * rate, 1.0, (double) line.buffer.size() - 3);
        line.feedback = p[8];
        // Linear pan: hard left keeps only the left output, the centre keeps both at full level.
        const auto pan = (double) p[9], level = std::pow (10.0, (double) p[10] / 20);
        line.left = (pan <= 0 ? 1 : 1 - pan) * level; line.right = (pan >= 0 ? 1 : 1 + pan) * level;
    }
    const auto d = (double) v[3 * controlsPerLine];
    dry = d <= -70 ? 0 : std::pow (10.0, d / 20);
}
void FilterDelay::reset() noexcept
{
    for (auto& line : lines) { std::fill (line.buffer.begin(), line.buffer.end(), 0.0f); line.write = 0; line.last = 0; line.highPass.clear(); line.lowPass.clear(); }
}
void FilterDelay::tick (double& left, double& right) noexcept
{
    const double inputs[] { left, .5 * (left + right), right };
    double outL = dry * left, outR = dry * right;
    for (size_t k = 0; k < lines.size(); ++k)
    {
        auto& line = lines[k];
        if (line.buffer.empty()) continue;
        const auto size = line.buffer.size();
        // The echo leaving the line, read with linear interpolation.
        const auto whole = (size_t) line.delay; const auto fraction = line.delay - (double) whole;
        const auto a = line.buffer[(line.write + size - whole) % size], b = line.buffer[(line.write + size - whole - 1) % size];
        const double echo = line.on ? a + (b - a) * fraction : 0.0;
        auto into = (line.on ? inputs[k] : 0.0) + line.feedback * echo;
        if (line.filtered) into = line.lowPass.tick (line.highPass.tick (into));
        line.buffer[line.write] = (float) finite (into);
        line.write = (line.write + 1) % size;
        outL += echo * line.left; outR += echo * line.right;
    }
    left = outL; right = outR;
}

void Erosion::prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }
void Erosion::configure (const float* v) noexcept
{
    blend = std::clamp ((double) v[0], 0.0, 1.0);
    frequency = v[1]; increment = (double) v[1] / rate;
    depth = std::clamp ((double) v[2], 0.0, 1.0) * maximumDepthSeconds * rate;
    width = std::clamp ((double) v[4], 0.0, 1.0);
    const auto half = std::pow (2.0, (double) v[3] / 2);
    for (auto& channel : band)
    {
        channel[0].c = highPassSection (std::max (20.0, v[1] / half), .70710678118654752, rate);
        channel[1].c = lowPassSection (std::min (rate * .45, v[1] * half), .70710678118654752, rate);
    }
}
void Erosion::reset() noexcept
{
    phase = 0; write = 0; sample = 0;
    for (auto& h : history) h.fill (0.0f);
    for (auto& channel : band) for (auto& s : channel) s.clear();
}
void Erosion::syncTo (double seconds) noexcept
{
    const auto cycles = seconds * frequency;
    phase = cycles - std::floor (cycles);
    sample = (int64_t) std::llround (seconds * rate);
}
double Erosion::noise (int channel) noexcept
{
    // A counter-based generator: the same sample position always gives the same noise.
    auto r = (uint64_t) sample * 0x9e3779b97f4a7c15ull + (uint64_t) (channel + 1) * 0xbf58476d1ce4e5b9ull;
    r = (r ^ (r >> 30)) * 0xbf58476d1ce4e5b9ull; r = (r ^ (r >> 27)) * 0x94d049bb133111ebull; r ^= r >> 31;
    const auto white = (double) (r >> 11) * (2.0 / 9007199254740992.0) - 1;
    // Band-filtered noise keeps a modulation level comparable to the sine's.
    return band[(size_t) channel][1].tick (band[(size_t) channel][0].tick (white)) * 2;
}
void Erosion::tick (double& left, double& right) noexcept
{
    const double in[] { left, right };
    for (size_t c = 0; c < 2; ++c) history[c][write] = (float) in[c];
    if (depth > 0)
    {
        const auto sineLeft = std::sin (2 * pi * phase);
        const auto sineRight = (1 - width) * sineLeft + width * std::cos (2 * pi * phase);
        const auto noiseLeft = blend > 0 ? noise (0) : 0.0;
        const auto noiseRight = blend > 0 ? (1 - width) * noiseLeft + width * noise (1) : 0.0;
        const double modulators[] { (1 - blend) * sineLeft + blend * noiseLeft, (1 - blend) * sineRight + blend * noiseRight };
        double* out[] { &left, &right };
        for (size_t c = 0; c < 2; ++c)
        {
            // The delay swings between 0 and the depth, so the input is never read ahead.
            const auto delay = std::clamp (depth * .5 * (1 + modulators[c]), 0.0, (double) history[c].size() - 2);
            const auto whole = (size_t) delay; const auto fraction = delay - (double) whole;
            const auto a = history[c][(write + history[c].size() - whole) % history[c].size()], b = history[c][(write + history[c].size() - whole - 1) % history[c].size()];
            *out[c] = a + (b - a) * fraction;
        }
    }
    phase += increment; phase -= std::floor (phase);
    ++sample;
    write = (write + 1) % history[0].size();
}

double Resonators::frequencyOf (const float* v, int k) noexcept
{
    const auto* r1 = v + first;
    double note = (double) r1[1] + (double) r1[2] / 100;
    if (k > 0) { const auto* r = v + first + k * controlsPerResonator; note += (double) r[1] + (double) r[2] / 100; }
    return std::max (lowestHz, 440 * std::pow (2.0, (note - 69) / 12));
}
void Resonators::prepare (double sampleRate) noexcept
{
    rate = sampleRate;
    for (auto& comb : combs) for (auto& channel : comb.buffer) channel.assign ((size_t) std::ceil (rate / lowestHz) + 4, 0.0f);
    reset();
}
void Resonators::configure (const float* v) noexcept
{
    filterType = (int) v[0];
    const auto hz = (double) v[1];
    const auto section = filterType == 0 ? lowPassSection (hz, .70710678118654752, rate) : filterType == 1 ? bandPassOctaves (hz, 1, rate)
                       : filterType == 2 ? highPassSection (hz, .70710678118654752, rate) : std::array<double, 5> {};
    for (auto& s : input)
    {
        if (filterType == 3)
        {
            // A notch: the band-pass subtracted from the input, formed as one section.
            const auto b = bandPassOctaves (hz, 1, rate);
            s.c = { 1 - b[0], b[3] - b[1], b[4] - b[2], b[3], b[4] };
        }
        else s.c = section;
    }
    halfPeriod = v[2] != 0;
    const auto decaySeconds = std::max (.01, (double) v[3]);
    const bool constant = v[4] != 0;
    const auto cutoff = 500 * std::pow (40.0, std::clamp ((double) v[5], 0.0, 1.0));
    const auto pole = std::exp (-2 * pi * std::min (cutoff, rate * .45) / rate);
    for (int k = 0; k < count; ++k)
    {
        const auto* r = v + first + k * controlsPerResonator;
        auto& comb = combs[(size_t) k];
        const auto f = frequencyOf (v, k);
        comb.on = r[0] != 0; comb.gain = r[3]; comb.colour = pole;
        // The loop is one period (Mode A) or half a period with negative feedback (Mode B), less the
        // colour filter's phase delay at the pitch.
        // Both modes resonate at the note itself.
        const auto omega = 2 * pi * f / rate;
        const auto lag = std::atan2 (pole * std::sin (omega), 1 - pole * std::cos (omega)) / omega;
        comb.delay = std::clamp (rate / f / (halfPeriod ? 2 : 1) - lag, 1.0, (double) comb.buffer[0].size() - 3);
        const auto ringSeconds = constant ? decaySeconds : decaySeconds * constReferenceHz / f;
        const auto loopSeconds = comb.delay / rate;
        comb.feedback = (halfPeriod ? -1 : 1) * std::pow (10.0, -3 * loopSeconds / ringSeconds);
    }
    width = std::clamp ((double) v[first + count * controlsPerResonator], 0.0, 1.0);
    output = std::pow (10.0, (double) v[first + count * controlsPerResonator + 1] / 20);
}
void Resonators::reset() noexcept
{
    for (auto& comb : combs) { for (auto& channel : comb.buffer) std::fill (channel.begin(), channel.end(), 0.0f); comb.write = 0; comb.state = {}; }
    for (auto& s : input) s.clear();
}
void Resonators::tick (double& left, double& right) noexcept
{
    const double in[] { input[0].tick (left), input[1].tick (right) };
    double out[] { 0, 0 };
    for (int k = 0; k < count; ++k)
    {
        auto& comb = combs[(size_t) k];
        if (! comb.on || comb.buffer[0].empty()) continue;
        // I takes both channels, II and IV the left, III and V the right.
        const bool takesLeft = k == 0 || k % 2 == 1, takesRight = k == 0 || (k > 0 && k % 2 == 0);
        const auto size = comb.buffer[0].size();
        for (size_t c = 0; c < 2; ++c)
        {
            if ((c == 0 && ! takesLeft) || (c == 1 && ! takesRight)) continue;
            auto& line = comb.buffer[c];
            const auto whole = (size_t) comb.delay; const auto fraction = comb.delay - (double) whole;
            const auto a = line[(comb.write + size - whole) % size], b = line[(comb.write + size - whole - 1) % size];
            const auto delayed = a + (b - a) * fraction;
            comb.state[c] = (1 - comb.colour) * delayed + comb.colour * comb.state[c];
            const auto y = in[c] + comb.feedback * comb.state[c];
            line[comb.write] = (float) finite (y);
            out[c] += y * comb.gain;
        }
        comb.write = (comb.write + 1) % size;
    }
    // Width 0 sums the resonances to mono.
    const auto mid = .5 * (out[0] + out[1]);
    left = (mid + width * (out[0] - mid)) * output; right = (mid + width * (out[1] - mid)) * output;
}

void VinylDistortion::prepare (double sampleRate) noexcept { rate = sampleRate; dcPole = std::exp (-2 * pi * 10 / rate); reset(); }
void VinylDistortion::configure (const float* v) noexcept
{
    const auto global = (double) v[7];
    tracing = 8 * (double) v[0] * global; pinch = 16 * (double) v[3] * global;
    for (auto& s : tracingBand) s.c = bandPassOctaves (v[1], octavesForQ (v[2]), rate);
    for (auto& s : pinchBand) s.c = bandPassOctaves (v[4], octavesForQ (v[5]), rate);
    stereo = v[6] != 0; soft = v[8] == 0;
    density = (double) v[9];
    volume = v[10] <= -70 ? 0 : std::pow (10.0, (double) v[10] / 20);
}
void VinylDistortion::reset() noexcept
{
    for (auto* bands : { &tracingBand, &pinchBand }) for (auto& s : *bands) s.clear();
    dcInput = {}; dcOutput = {}; sample = 0; clickLevel = 0;
}
void VinylDistortion::syncTo (double seconds) noexcept { sample = (int64_t) std::llround (seconds * rate); }
// One sample of crackle: a click starts with probability Density / rate and decays over about a millisecond.
double VinylDistortion::click() noexcept
{
    auto r = (uint64_t) sample * 0x9e3779b97f4a7c15ull + 0x632be59bd9b4e019ull;
    r = (r ^ (r >> 30)) * 0xbf58476d1ce4e5b9ull; r = (r ^ (r >> 27)) * 0x94d049bb133111ebull; r ^= r >> 31;
    const auto chance = (double) (r >> 11) * (1.0 / 9007199254740992.0);
    if (chance < density / rate) clickLevel = (r & 1 ? 1.0 : -1.0) * (.5 + .5 * (double) ((r >> 1) & 1023) / 1023);
    const auto out = clickLevel;
    clickLevel *= std::exp (-1 / (.001 * rate));
    return out;
}
void VinylDistortion::tick (double& left, double& right) noexcept
{
    double* channels[] { &left, &right };
    const auto crackle = volume > 0 && density > 0 ? click() * volume : 0.0;
    for (size_t c = 0; c < 2; ++c)
    {
        const auto x = *channels[c];
        const auto t = tracingBand[c].tick (x), p = pinchBand[c].tick (x);
        auto even = tracing * t * t, odd = pinch * p * p * p;
        if (soft) { even = std::tanh (even); odd = std::tanh (odd); }
        // The squared band carries DC; a 10 Hz high-pass removes it.
        const auto blocked = even - dcInput[c] + dcPole * dcOutput[c];
        dcInput[c] = even; dcOutput[c] = finite (blocked);
        const auto phase = stereo && c == 1 ? -1.0 : 1.0;
        *channels[c] = x + dcOutput[c] + phase * odd + crackle;
    }
    ++sample;
}

double AlignDelay::delayFrames (const float* v, int channel, double rate) noexcept
{
    const auto side = v[1] != 0 ? 0 : channel; // Link: the left settings for both.
    switch ((int) v[0])
    {
        case 0: return (double) v[2 + side] * .001 * rate;
        case 1: return (double) v[4 + side];
        default:
        {
            const auto metres = (double) v[6 + side] * (v[8] != 0 ? .3048 : 1.0);
            const auto celsius = v[10] != 0 ? ((double) v[9] - 32) * 5 / 9 : (double) v[9];
            return metres / speedOfSound (celsius) * rate;
        }
    }
}
void AlignDelay::prepare (double sampleRate) noexcept
{
    rate = sampleRate;
    for (auto& b : buffer) b.assign ((size_t) std::ceil (rate * longestSeconds) + 4, 0.0f);
    reset();
}
void AlignDelay::configure (const float* v) noexcept
{
    for (int c = 0; c < 2; ++c) delay[(size_t) c] = std::clamp (delayFrames (v, c, rate), 0.0, (double) buffer[0].size() - 3);
}
void AlignDelay::reset() noexcept { for (auto& b : buffer) std::fill (b.begin(), b.end(), 0.0f); write = 0; }
void AlignDelay::tick (double& left, double& right) noexcept
{
    if (buffer[0].empty()) return;
    double* channels[] { &left, &right };
    const auto size = buffer[0].size();
    for (size_t c = 0; c < 2; ++c)
    {
        buffer[c][write] = (float) *channels[c];
        const auto whole = (size_t) delay[c]; const auto fraction = delay[c] - (double) whole;
        const auto a = buffer[c][(write + size - whole) % size], b = buffer[c][(write + size - whole - 1) % size];
        *channels[c] = fraction == 0 ? a : a + (b - a) * fraction;
    }
    write = (write + 1) % size;
}

double DynamicTube::tube (int model, double bias, double x) noexcept
{
    const auto m = (size_t) std::clamp (model, 0, 2);
    // Below the model's threshold the tube is clean; above it the offset grows with Bias. Model C
    // also always rounds the waveform.
    const auto engaged = std::max (0.0, bias - thresholds[m]) / (1 - thresholds[m]);
    const auto offset = 1.5 * engaged;
    if (m == 2) return std::tanh (x + offset) - std::tanh (offset);
    if (engaged <= 0) return x;
    const auto curved = std::tanh (x + offset) - std::tanh (offset);
    const auto blend = std::min (1.0, engaged * (m == 1 ? 2.0 : 1.5));
    return x + blend * (curved - x);
}
void DynamicTube::prepare (double sampleRate) noexcept { rate = sampleRate; dcPole = std::exp (-2 * pi * 10 / rate); reset(); }
void DynamicTube::configure (const float* v) noexcept
{
    model = (int) v[0]; drive = std::pow (10.0, (double) v[1] / 20); bias = v[2]; envelopeAmount = v[3];
    attack = std::exp (-1 / (std::max (.1, (double) v[4]) * .001 * rate)); release = std::exp (-1 / (std::max (1.0, (double) v[5]) * .001 * rate));
    const auto tilt = 12 * (double) v[6];
    for (auto& s : emphasis) s.c = highShelfSection (1000, tilt, .70710678118654752, rate);
    for (auto& s : deemphasis) s.c = highShelfSection (1000, -tilt, .70710678118654752, rate);
}
void DynamicTube::reset() noexcept
{
    follower = 0; dcInput = {}; dcOutput = {};
    for (auto* set : { &emphasis, &deemphasis }) for (auto& s : *set) s.clear();
}
void DynamicTube::tick (double& left, double& right) noexcept
{
    const auto level = std::max (std::abs (left), std::abs (right)) * drive;
    follower = level > follower ? attack * follower + (1 - attack) * level : release * follower + (1 - release) * level;
    const auto effective = std::clamp (bias + envelopeAmount * std::min (1.0, follower), 0.0, 1.0);
    double* channels[] { &left, &right };
    for (size_t c = 0; c < 2; ++c)
    {
        const auto shaped = tube (model, effective, emphasis[c].tick (*channels[c] * drive));
        const auto blocked = shaped - dcInput[c] + dcPole * dcOutput[c];
        dcInput[c] = shaped; dcOutput[c] = finite (blocked);
        *channels[c] = deemphasis[c].tick (dcOutput[c]);
    }
}
}

