#include "SoundSimilarity.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <thread>

namespace halftrack::similarity
{
namespace
{
constexpr double analysedSeconds = 10.0;
constexpr int fftOrder = 11, fftSize = 1 << fftOrder, hop = fftSize / 2;
// The energy shares' bands: log-spaced from 30 Hz to 18 kHz, as hearing spaces pitch.
double bandEdge (int index) { return 30.0 * std::pow (18000.0 / 30.0, (double) index / Features::bandCount); }
float logOf (double value, double floor) { return (float) std::log (std::max (value, floor)); }
}

const char* Features::name (int index)
{
    static const char* names[11] { "Length", "Crest", "Attack", "Decay", "Sustain", "Centroid", "Centroid spread", "Flatness", "Rolloff",
                                   "Zero crossings", "Flux" };
    if (juce::isPositiveAndBelow (index, 10)) return names[index];
    if (index == count - 1) return names[10];
    return juce::isPositiveAndBelow (index, count) ? "Band energy" : "";
}

std::optional<Features> analyse (const juce::File& file, juce::AudioFormatManager& formats)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0) return std::nullopt;
    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * analysedSeconds));
    juce::AudioBuffer<float> audio ((int) std::max (1u, reader->numChannels), length);
    if (! reader->read (&audio, 0, length, 0, true, true)) return std::nullopt;
    auto features = analyse (audio, reader->sampleRate);
    // Length counts the whole file, not only the part analysed.
    if (features) features->values[0] = logOf ((double) reader->lengthInSamples / reader->sampleRate, 0.01);
    return features;
}

std::optional<Features> analyse (const juce::AudioBuffer<float>& audio, double rate)
{
    const int n = std::min (audio.getNumSamples(), (int) (rate * analysedSeconds));
    if (n <= 0 || rate <= 0 || audio.getNumChannels() == 0) return std::nullopt;
    std::vector<float> mono ((size_t) n, 0.0f);
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int i = 0; i < n; ++i) mono[(size_t) i] += audio.getSample (c, i) / (float) audio.getNumChannels();
    double sumSquares = 0, peak = 0;
    int crossings = 0;
    for (int i = 0; i < n; ++i)
    {
        sumSquares += (double) mono[(size_t) i] * mono[(size_t) i];
        peak = std::max (peak, (double) std::abs (mono[(size_t) i]));
        if (i > 0 && (mono[(size_t) i - 1] < 0) != (mono[(size_t) i] < 0)) ++crossings;
    }
    const double rms = std::sqrt (sumSquares / n);
    if (peak < 1.0e-5) return std::nullopt; // Silence is like nothing.

    // The envelope in 5 ms windows: when it peaks, how much comes after, how much remains at the end.
    const int window = std::max (1, (int) (rate * 0.005));
    std::vector<double> envelope;
    for (int start = 0; start < n; start += window)
    {
        double energy = 0;
        const int end = std::min (n, start + window);
        for (int i = start; i < end; ++i) energy += (double) mono[(size_t) i] * mono[(size_t) i];
        envelope.push_back (std::sqrt (energy / (end - start)));
    }
    const auto loudest = (int) std::distance (envelope.begin(), std::max_element (envelope.begin(), envelope.end()));
    int attack = 0;
    while (attack < loudest && envelope[(size_t) attack] < 0.9 * envelope[(size_t) loudest]) ++attack;
    double after = 0, late = 0;
    for (size_t i = (size_t) loudest; i < envelope.size(); ++i) after += envelope[i] * envelope[i];
    for (size_t i = (size_t) loudest + (envelope.size() - (size_t) loudest) / 2; i < envelope.size(); ++i) late += envelope[i] * envelope[i];
    double tail = 0;
    const auto lastThird = envelope.size() - std::max<size_t> (1, envelope.size() / 3);
    for (size_t i = lastThird; i < envelope.size(); ++i) tail = std::max (tail, envelope[i]);

    // The spectrum, frame by frame.
    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> hann ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> frame ((size_t) fftSize * 2), magnitude ((size_t) fftSize / 2 + 1), previous ((size_t) fftSize / 2 + 1, 0.0f);
    std::array<double, Features::bandCount> bands {};
    double centroidSum = 0, centroidSquares = 0, flatnessSum = 0, rolloffSum = 0, fluxSum = 0, totalEnergy = 0;
    int frames = 0;
    const double binHz = rate / fftSize;
    for (int start = 0; start == 0 || start + fftSize <= n; start += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < fftSize && start + i < n; ++i) frame[(size_t) i] = mono[(size_t) (start + i)];
        hann.multiplyWithWindowingTable (frame.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);
        double energy = 0, weighted = 0, logSum = 0, flux = 0;
        for (size_t bin = 1; bin < magnitude.size(); ++bin)
        {
            magnitude[bin] = frame[bin];
            const double power = (double) magnitude[bin] * magnitude[bin];
            energy += power; weighted += power * bin * binHz;
            logSum += std::log (power + 1.0e-12);
            flux += std::max (0.0f, magnitude[bin] - previous[bin]);
            const double hz = bin * binHz;
            if (hz >= bandEdge (0) && hz < bandEdge (Features::bandCount))
                bands[(size_t) juce::jlimit (0, Features::bandCount - 1, (int) (std::log (hz / 30.0) / std::log (18000.0 / 30.0) * Features::bandCount))] += power;
        }
        previous = magnitude;
        if (energy <= 1.0e-12) continue;
        const double centroid = std::log (std::max (weighted / energy, 20.0));
        centroidSum += centroid; centroidSquares += centroid * centroid;
        const double bins = (double) (magnitude.size() - 1);
        flatnessSum += std::exp (logSum / bins) / (energy / bins);
        double cumulative = 0;
        size_t rolloff = 1;
        while (rolloff < magnitude.size() && cumulative < 0.85 * energy) { cumulative += (double) magnitude[rolloff] * magnitude[rolloff]; ++rolloff; }
        rolloffSum += std::log (std::max (rolloff * binHz, 20.0));
        fluxSum += flux / std::sqrt (energy);
        totalEnergy += energy;
        ++frames;
    }
    if (frames == 0 || totalEnergy <= 0) return std::nullopt;
    Features f;
    auto& v = f.values;
    v[0] = logOf (n / rate, 0.01);
    v[1] = (float) (20.0 * std::log10 (peak / std::max (rms, 1.0e-9)));
    v[2] = logOf (attack * window / rate, 0.001);
    v[3] = (float) (after > 0 ? late / after : 0);
    v[4] = (float) (envelope[(size_t) loudest] > 0 ? tail / envelope[(size_t) loudest] : 0);
    const double centroidMean = centroidSum / frames;
    v[5] = (float) centroidMean;
    v[6] = (float) std::sqrt (std::max (0.0, centroidSquares / frames - centroidMean * centroidMean));
    v[7] = (float) (flatnessSum / frames);
    v[8] = (float) (rolloffSum / frames);
    v[9] = logOf (crossings / (n / rate), 1.0);
    double bandTotal = 0;
    for (auto band : bands) bandTotal += band;
    // Each band's share of the energy, in decibels so quiet bands still count.
    for (size_t band = 0; band < bands.size(); ++band) v[10 + band] = (float) (10.0 * std::log10 (bandTotal > 0 ? bands[band] / bandTotal + 1.0e-6 : 1.0e-6));
    v[Features::count - 1] = (float) (fluxSum / frames);
    for (auto& value : v) if (! std::isfinite (value)) value = 0;
    return f;
}

void Library::add (const juce::File& file, const Features& features)
{
    sounds.emplace_back (file, features);
    scaled = false;
}

void Library::scale() const
{
    // Each value on the library's own scale: its mean and spread across the sounds.
    mean.fill (0); spread.fill (0);
    if (sounds.empty()) { scaled = true; return; }
    for (const auto& [file, features] : sounds) for (int i = 0; i < Features::count; ++i) mean[(size_t) i] += features.values[(size_t) i];
    for (auto& m : mean) m /= (float) sounds.size();
    for (const auto& [file, features] : sounds)
        for (int i = 0; i < Features::count; ++i) { const auto d = features.values[(size_t) i] - mean[(size_t) i]; spread[(size_t) i] += d * d; }
    for (auto& s : spread) s = std::sqrt (s / (float) sounds.size());
    scaled = true;
}

std::vector<Library::Match> Library::similarTo (const Features& query, int limit, const juce::File& excluding) const
{
    if (! scaled) scale();
    std::vector<Match> matches;
    for (const auto& [file, features] : sounds)
    {
        if (file == excluding) continue;
        float distance = 0;
        for (int i = 0; i < Features::count; ++i)
        {
            // A value the library does not vary in tells sounds apart by nothing.
            if (spread[(size_t) i] <= 1.0e-6f) continue;
            const auto d = (features.values[(size_t) i] - query.values[(size_t) i]) / spread[(size_t) i];
            distance += d * d;
        }
        matches.push_back ({ file, std::sqrt (distance) });
    }
    std::sort (matches.begin(), matches.end(), [] (const Match& a, const Match& b)
               { return a.distance != b.distance ? a.distance < b.distance : a.file.getFullPathName() < b.file.getFullPathName(); });
    if ((int) matches.size() > limit) matches.resize ((size_t) std::max (0, limit));
    return matches;
}

std::vector<Library::Match> Library::similarTo (const juce::File& file, int limit) const
{
    for (const auto& [known, features] : sounds) if (known == file) return similarTo (features, limit, file);
    return {};
}

struct FolderIndex::State
{
    mutable std::mutex lock;
    juce::File folder;
    Library library;
    std::map<juce::String, std::pair<juce::String, Features>> measured; // path -> (size and time, features)
    std::atomic<bool> cancelled { false }, analysing { false };
    std::atomic<int> analysed { 0 }, found { 0 }, unreadable { 0 };
};

FolderIndex::~FolderIndex() { cancel(); }

void FolderIndex::cancel()
{
    if (state != nullptr) state->cancelled = true;
}

void FolderIndex::start (const juce::File& folder)
{
    // Keep what was measured in the same folder; a new folder starts afresh.
    auto previous = state;
    cancel();
    state = std::make_shared<State>();
    state->folder = folder;
    if (previous != nullptr && previous->folder == folder)
    {
        const std::lock_guard<std::mutex> guard (previous->lock);
        state->measured = previous->measured;
    }
    state->analysing = true;
    std::thread ([shared = state]
    {
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        auto files = shared->folder.findChildFiles (juce::File::findFiles | juce::File::ignoreHiddenFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        files.sort();
        if (files.size() > maximumSounds) files.removeRange (maximumSounds, files.size());
        shared->found = files.size();
        for (const auto& file : files)
        {
            if (shared->cancelled) break;
            const auto stamp = juce::String (file.getSize()) + ":" + juce::String (file.getLastModificationTime().toMilliseconds());
            std::optional<Features> features;
            {
                const std::lock_guard<std::mutex> guard (shared->lock);
                if (auto known = shared->measured.find (file.getFullPathName()); known != shared->measured.end() && known->second.first == stamp) features = known->second.second;
            }
            if (! features) features = analyse (file, formats);
            const std::lock_guard<std::mutex> guard (shared->lock);
            if (features) { shared->library.add (file, *features); shared->measured[file.getFullPathName()] = { stamp, *features }; }
            else ++shared->unreadable;
            ++shared->analysed;
        }
        shared->analysing = false;
    }).detach();
}

FolderIndex::Status FolderIndex::status() const
{
    if (state == nullptr) return {};
    return { state->analysing.load(), state->analysed.load(), state->found.load(), state->unreadable.load() };
}

juce::File FolderIndex::folder() const { return state != nullptr ? state->folder : juce::File(); }

std::vector<Library::Match> FolderIndex::similarTo (const juce::File& file, int limit) const
{
    if (state == nullptr) return {};
    std::optional<Features> features;
    {
        const std::lock_guard<std::mutex> guard (state->lock);
        if (auto known = state->measured.find (file.getFullPathName()); known != state->measured.end()) features = known->second.second;
    }
    if (! features)
    {
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        features = analyse (file, formats);
    }
    if (! features) return {};
    const std::lock_guard<std::mutex> guard (state->lock);
    return state->library.similarTo (*features, limit, file);
}
}
