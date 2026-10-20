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
}
