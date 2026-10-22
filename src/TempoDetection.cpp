#include "TempoDetection.h"
#include "WarpStretchers.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace halftrack::tempo
{
namespace
{
// Below this the comb finds no tempo clearly above the others: noise, tones, or rubato.
constexpr double steadyBeat = 1.3;
}

struct OnsetCurve::Spectrum
{
    Spectrum (int order, double rate) : fft (order), data ((size_t) (2 << order)), detector (rate) {}
    juce::dsp::FFT fft;
    std::vector<float> data;
    warp::TransientDetector detector;
};

OnsetCurve::OnsetCurve (double sampleRate)
    : rate (sampleRate > 0 ? sampleRate : 48000), hop (juce::jmax (16, (int) std::lround (rate * 512 / 48000)))
{
    int order = 4;
    while ((1 << order) < 2 * hop) ++order;
    size = 1 << order;
    bassBins = juce::jmax (1, (int) (200.0 * size / rate));
    spectrum = std::make_unique<Spectrum> (order, rate);
    window.resize ((size_t) size);
    for (int n = 0; n < size; ++n) window[(size_t) n] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / size));
    // Half a window of silence first, so the first value is centred on the start.
    recent.assign ((size_t) size, 0.0f);
    filled = size / 2;
    previous.assign ((size_t) (size / 2 + 1), 0.0f);
}
OnsetCurve::~OnsetCurve() = default;

void OnsetCurve::add (const float* const* channels, int numChannels, int numSamples)
{
    if (numChannels <= 0) return;
    auto& data = spectrum->data;
    for (int n = 0; n < numSamples; ++n)
    {
        float sum = 0, level = 0;
        for (int c = 0; c < numChannels; ++c) { const float v = channels[c][n]; sum += v; level += std::abs (v); }
        if (spectrum->detector.next (level / (float) numChannels, samples)) transientSeconds.push_back ((double) samples / rate);
        ++samples;
        recent[(size_t) filled++] = sum / (float) numChannels;
        if (filled < size) continue;
        // The rise of each bin's compressed magnitude since the last step.
        for (int i = 0; i < size; ++i) data[(size_t) i] = recent[(size_t) i] * window[(size_t) i];
        std::fill (data.begin() + size, data.end(), 0.0f);
        spectrum->fft.performFrequencyOnlyForwardTransform (data.data(), true);
        const float scale = 4.0f / (float) size; // A full-scale sine is about 1.
        float flux = 0, low = 0;
        for (int k = 1; k <= size / 2; ++k)
        {
            const float compressed = std::log1p (1000.0f * data[(size_t) k] * scale), rise = std::max (0.0f, compressed - previous[(size_t) k]);
            flux += rise;
            if (k <= bassBins) low += rise;
            previous[(size_t) k] = compressed;
        }
        curve.push_back (flux);
        bass.push_back (low);
        std::copy (recent.begin() + hop, recent.end(), recent.begin());
        filled = size - hop;
    }
}

std::optional<Estimate> estimate (const OnsetCurve& curve, double minimumBpm, double maximumBpm)
{
    if (! (minimumBpm > 0 && maximumBpm > minimumBpm)) return std::nullopt;
    const auto& raw = curve.values();
    const double step = curve.secondsPerValue();
    const int count = (int) raw.size();
    // Eight beats at the slowest tempo at least, less the step lost at the end.
    if (count * step < 7.5 * 60 / minimumBpm) return std::nullopt;

    // Onset strength: the rise above the curve's mean over 0.2 s, so a steady sound counts for nothing.
    const auto strengthOf = [&] (const std::vector<float>& values)
    {
        std::vector<double> prefix ((size_t) count + 1, 0.0);
        for (int j = 0; j < count; ++j) prefix[(size_t) j + 1] = prefix[(size_t) j] + values[(size_t) j];
        const int around = juce::jmax (1, (int) std::lround (0.1 / step));
        std::vector<float> result ((size_t) count);
        for (int j = 0; j < count; ++j)
        {
            const int lo = juce::jmax (0, j - around), hi = juce::jmin (count, j + around + 1);
            result[(size_t) j] = std::max (0.0f, values[(size_t) j] - (float) ((prefix[(size_t) hi] - prefix[(size_t) lo]) / (hi - lo)));
        }
        return result;
    };
    auto onset = strengthOf (raw);
    // Ripples far below the sound's onsets are no beat: a held tone has one onset, then none.
    const float loudest = *std::max_element (onset.begin(), onset.end());
    for (auto& v : onset) if (v < 0.01f * loudest) v = 0;

    // The rough tempo: the autocorrelation of the smoothed onsets at one to four beats.
    std::vector<float> smooth ((size_t) count);
    for (int j = 0; j < count; ++j)
    {
        constexpr float weights[] = { 1, 4, 6, 4, 1 };
        float sum = 0;
        for (int k = -2; k <= 2; ++k) sum += weights[k + 2] * onset[(size_t) juce::jlimit (0, count - 1, j + k)];
        smooth[(size_t) j] = sum / 16;
    }
    const int longest = juce::jmin (count - 1, (int) std::ceil (4 * 60 / minimumBpm / step) + 2);
    std::vector<double> correlation ((size_t) longest + 1);
    for (int lag = 0; lag <= longest; ++lag)
    {
        double sum = 0;
        for (int j = 0; j + lag < count; ++j) sum += (double) smooth[(size_t) j] * smooth[(size_t) (j + lag)];
        correlation[(size_t) lag] = sum / (count - lag);
    }
    const auto correlationAt = [&] (double lag)
    {
        const int i = (int) lag;
        if (i >= longest) return correlation[(size_t) longest];
        const double f = lag - i;
        return correlation[(size_t) i] * (1 - f) + correlation[(size_t) i + 1] * f;
    };
    std::vector<double> combs;
    double rough = minimumBpm, roughScore = -1;
    for (double bpm = minimumBpm; bpm < maximumBpm; bpm += 0.05)
    {
        const double period = 60 / bpm / step;
        double score = 0;
        for (const double k : { 0.5, 1.0, 1.5, 2.0, 3.0, 4.0 }) score += correlationAt (k * period);
        combs.push_back (score);
        if (score > roughScore) { roughScore = score; rough = bpm; }
    }
    auto middle = combs;
    std::nth_element (middle.begin(), middle.begin() + (long) middle.size() / 2, middle.end());
    const double median = middle[middle.size() / 2];
    if (! (median > 0) || roughScore / median < steadyBeat) return std::nullopt;

    // The beat grid: the period and phase at which the onsets of the whole sound pile up most, the
    // bass's onsets weighing as much as all of them so the beat falls on kicks rather than hats.
    auto beatOnsets = onset;
    {
        const auto bassOnset = strengthOf (curve.bassValues());
        double all = 0, low = 0;
        for (int j = 0; j < count; ++j) { all += onset[(size_t) j]; low += bassOnset[(size_t) j]; }
        if (low > all * 1e-3)
            for (int j = 0; j < count; ++j) beatOnsets[(size_t) j] += (float) (all / low) * bassOnset[(size_t) j];
    }
    struct Grid { double peak = 0, phase = 0; };
    const auto grid = [&] (double bpm)
    {
        const double period = 60 / bpm / step;
        const int bins = juce::jmax (8, (int) std::lround (period * 2));
        std::vector<double> histogram ((size_t) bins, 0.0);
        for (int j = 0; j < count; ++j)
        {
            if (beatOnsets[(size_t) j] <= 0) continue;
            const double at = std::fmod ((double) j, period) / period * bins;
            const int i = (int) at;
            const double f = at - i;
            histogram[(size_t) (i % bins)] += beatOnsets[(size_t) j] * (1 - f);
            histogram[(size_t) ((i + 1) % bins)] += beatOnsets[(size_t) j] * f;
        }
        std::vector<double> smoothed ((size_t) bins);
        for (int i = 0; i < bins; ++i)
            smoothed[(size_t) i] = histogram[(size_t) ((i + bins - 1) % bins)] + 2 * histogram[(size_t) i] + histogram[(size_t) ((i + 1) % bins)];
        const int top = (int) (std::max_element (smoothed.begin(), smoothed.end()) - smoothed.begin());
        const double before = smoothed[(size_t) ((top + bins - 1) % bins)], peak = smoothed[(size_t) top], after = smoothed[(size_t) ((top + 1) % bins)];
        const double curvature = before - 2 * peak + after;
        const double offset = curvature < 0 ? juce::jlimit (-0.5, 0.5, 0.5 * (before - after) / curvature) : 0.0;
        return Grid { peak, std::fmod ((top + offset + bins) / bins * period, period) };
    };
    std::vector<double> fine;
    const double from = rough * 0.985, to = rough * 1.015, fineStep = 0.01;
    for (double bpm = from; bpm <= to; bpm += fineStep) fine.push_back (grid (bpm).peak);
    const int best = (int) (std::max_element (fine.begin(), fine.end()) - fine.begin());
    double bpm = from + best * fineStep;
    if (best > 0 && best + 1 < (int) fine.size())
    {
        const double curvature = fine[(size_t) best - 1] - 2 * fine[(size_t) best] + fine[(size_t) best + 1];
        if (curvature < 0) bpm += juce::jlimit (-0.5, 0.5, 0.5 * (fine[(size_t) best - 1] - fine[(size_t) best + 1]) / curvature) * fineStep;
    }
    const auto beats = grid (bpm);
    // The grid's beats in seconds, from the first at or after the start.
    double firstGridBeat = beats.phase * step, beatSeconds = 60 / bpm;

    // The tempo and phase from the transients on the grid's beats: a least-squares line through
    // them, twice, each time keeping only those within 20 ms of the line.
    const auto& transients = curve.transients();
    const auto nearestTransient = [&] (double at, double within)
    {
        const auto after = std::lower_bound (transients.begin(), transients.end(), at);
        std::optional<double> found;
        if (after != transients.end() && *after - at < within) { found = *after; within = *after - at; }
        if (after != transients.begin() && at - *(after - 1) < within) found = *(after - 1);
        return found;
    };
    // How far the tempo may be out, relatively: six standard errors of the line's slope, as a played
    // loop's timing errors run together rather than each on its own.
    double uncertainty = 0.015;
    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<std::pair<double, double>> points;
        for (int i = 0; firstGridBeat + i * beatSeconds < curve.seconds(); ++i)
            if (const auto t = nearestTransient (firstGridBeat + i * beatSeconds, 0.02)) points.push_back ({ (double) i, *t });
        const double n = (double) points.size();
        if (n < 8) break;
        double meanI = 0, meanT = 0;
        for (const auto& [i, t] : points) { meanI += i / n; meanT += t / n; }
        double spread = 0, covariance = 0;
        for (const auto& [i, t] : points) { spread += (i - meanI) * (i - meanI); covariance += (i - meanI) * (t - meanT); }
        if (spread <= 0) break;
        const double slope = covariance / spread;
        if (std::abs (slope / beatSeconds - 1) > 0.01) break;
        double residuals = 0;
        for (const auto& [i, t] : points) residuals += std::pow (t - meanT - slope * (i - meanI), 2);
        uncertainty = 6 * std::sqrt (residuals / (n - 2) / spread) / slope;
        beatSeconds = slope;
        firstGridBeat = meanT - slope * meanI;
        // A beat on the very start can fit a hair before it.
        while (firstGridBeat < -0.02) firstGridBeat += beatSeconds;
        while (firstGridBeat - beatSeconds >= -0.02) firstGridBeat -= beatSeconds;
    }
    bpm = std::round (60 / beatSeconds * 100) / 100;

    // The first beat: the first on the grid with an onset near the sound's strong ones.
    std::vector<float> strength;
    for (double at = firstGridBeat; at < curve.seconds(); at += beatSeconds)
    {
        const int j = juce::jmax (0, (int) std::lround (at / step));
        float m = 0;
        for (int k = juce::jmax (0, j - 2); k <= juce::jmin (count - 1, j + 2); ++k) m = std::max (m, beatOnsets[(size_t) k]);
        strength.push_back (m);
    }
    if (strength.empty()) return std::nullopt;
    auto sorted = strength;
    std::sort (sorted.begin(), sorted.end());
    const float threshold = 0.25f * sorted[(size_t) ((double) (sorted.size() - 1) * 0.9)];
    const int firstIndex = juce::jmin ((int) strength.size() - 1,
                                       (int) (std::find_if (strength.begin(), strength.end(), [&] (float s) { return s >= threshold && s > 0; }) - strength.begin()));
    const double onGrid = firstGridBeat + firstIndex * beatSeconds;
    const double first = std::max (0.0, nearestTransient (onGrid, 0.03).value_or (onGrid));
    // A loop cut to whole bars: when the sound holds a whole number of 4/4 bars, up to 16, within
    // how far the tempo may be out, the tempo is the one that fills them.
    const double fromFirst = curve.seconds(), beatsHeld = fromFirst * bpm / 60, bars = std::round (beatsHeld / 4);
    if (bars >= 1 && bars <= 16 && std::abs (beatsHeld / (bars * 4) - 1) < juce::jlimit (0.0005, 0.015, uncertainty))
    {
        const double filled = bars * 4 * 60 / fromFirst;
        if (filled >= minimumBpm && filled < maximumBpm) bpm = std::round (filled * 100) / 100;
    }
    return Estimate { bpm, first, roughScore / median };
}

std::optional<Estimate> estimate (const juce::AudioBuffer<float>& audio, double sampleRate, double minimumBpm, double maximumBpm)
{
    OnsetCurve curve (sampleRate);
    curve.add (audio.getArrayOfReadPointers(), audio.getNumChannels(), audio.getNumSamples());
    return estimate (curve, minimumBpm, maximumBpm);
}

bool OnsetCurve::add (juce::AudioFormatReader& reader, double maximumSeconds)
{
    if (reader.sampleRate <= 0 || reader.lengthInSamples <= 0 || reader.numChannels == 0) return false;
    const int channels = juce::jlimit (1, 2, (int) reader.numChannels);
    juce::AudioBuffer<float> block (channels, 65536);
    const auto total = std::min<juce::int64> (reader.lengthInSamples, (juce::int64) (juce::jmax (0.0, maximumSeconds) * reader.sampleRate));
    for (juce::int64 at = 0; at < total; at += block.getNumSamples())
    {
        const int n = (int) std::min<juce::int64> (block.getNumSamples(), total - at);
        if (! reader.read (&block, 0, n, at, true, channels > 1)) return false;
        add (block.getArrayOfReadPointers(), channels, n);
    }
    return true;
}

std::optional<Estimate> estimate (juce::AudioFormatReader& reader, double maximumSeconds, double minimumBpm, double maximumBpm)
{
    OnsetCurve curve (reader.sampleRate);
    if (! curve.add (reader, maximumSeconds)) return std::nullopt;
    return estimate (curve, minimumBpm, maximumBpm);
}
}
