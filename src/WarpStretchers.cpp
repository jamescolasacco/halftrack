#include "WarpStretchers.h"

#if __has_include(<signalsmith-stretch/signalsmith-stretch.h>)
 #include <signalsmith-stretch/signalsmith-stretch.h>
#else
 #include <tracktion_engine/3rd_party/signalsmith-stretch/signalsmith-stretch.h>
#endif

namespace halftrack::warp
{
TransientDetector::TransientDetector (double rate)
    : fastRelease ((float) std::exp (-1.0 / (rate * 0.005))), slowCoefficient ((float) (1.0 - std::exp (-1.0 / (rate * 0.05)))),
      shortest ((long long) (rate * 0.03))
{}
void TransientDetector::reset() { fast = slow = 0; last = 0; started = false; }
bool TransientDetector::next (float level, long long frame)
{
    const auto average = slow;
    fast = level > fast ? level : fast * fastRelease;
    slow += (level - slow) * slowCoefficient;
    if ((started && frame - last < shortest) || fast <= 0.01f || fast <= average * 4) return false;
    started = true; last = frame;
    return true;
}
std::vector<long long> findTransients (const juce::AudioBuffer<float>& audio, double rate)
{
    TransientDetector detector (rate);
    detector.mark (0);
    std::vector<long long> result;
    for (int n = 0; n < audio.getNumSamples(); ++n)
    {
        float level = 0;
        for (int c = 0; c < audio.getNumChannels(); ++c) level += std::abs (audio.getSample (c, n));
        if (detector.next (level / (float) juce::jmax (1, audio.getNumChannels()), n)) result.push_back (n);
    }
    return result;
}

namespace
{
namespace te = tracktion;
using Stretcher = te::TimeStretcher::Stretcher;
using Options = te::TimeStretcher::ElastiqueProOptions;

// Every stretcher returns one output block a call; this is the input that block needs, carrying
// the fraction so the long-run ratio is exact. The speed ratio is output length over input length.
struct BlockRatio
{
    int block = 0;
    float speed = 1;
    double carry = 0;
    int needed() const { return (int) std::floor (carry + block / (double) speed); }
    void consumed (int frames) { carry += block / (double) speed - frames; }
};
constexpr int fastestRatio = 16; // Blocks may need up to this many times their length of input.

// Tones and Complex Pro.
class SpectralStretcher final : public Stretcher
{
public:
    SpectralStretcher (double rate, int block, int channels, bool tones, const Options& options, bool realtime)
        : ratio { block }, keepFormants (! tones)
    {
        if (tones)
        {
            // Grain Size sets the analysis block, 4 ms a step, so the default 30 is the default 120 ms.
            const auto size = options.halftrack[grains::size] > 0 ? juce::jlimit (1.0f, 100.0f, options.halftrack[grains::size]) : 30.0f;
            const auto samples = juce::jmax (64, (int) (rate * size * 0.004));
            stretch.configure (channels, samples, juce::jmax (16, samples / 4), realtime);
        }
        else stretch.presetDefault (channels, (float) rate, realtime);
        formantShift = juce::jlimit (0.0f, 100.0f, options.halftrack[complexPro::formantShift]) / 100.0f;
    }
    bool isOk() const override { return true; }
    void reset() override { stretch.reset(); ratio.carry = 0; flushed = false; }
    bool setSpeedAndPitch (float speed, float semitones) override
    {
        ratio.speed = speed;
        stretch.setTransposeSemitones (semitones);
        // The formants move by their share of the transposition and otherwise stay where they are.
        if (keepFormants) stretch.setFormantSemitones (semitones * formantShift, true);
        return true;
    }
    int getFramesNeeded() const override { return ratio.needed(); }
    int getMaxFramesNeeded() const override { return std::max (stretch.inputLatency() + stretch.outputLatency(), ratio.block * fastestRatio + 1); }
    int processData (const float* const* in, int frames, float* const* out) override
    {
        stretch.process (in, frames, out, ratio.block);
        ratio.consumed (frames);
        return ratio.block;
    }
    int flush (float* const* out) override
    {
        if (flushed) return 0;
        flushed = true; stretch.flush (out, ratio.block); return ratio.block;
    }
    int getLatencySamples() const override { return stretch.inputLatency() + stretch.outputLatency(); }
    int getOutputLatencySamples() const override { return stretch.outputLatency(); }
private:
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    BlockRatio ratio;
    bool keepFormants = false, flushed = false;
    float formantShift = 0;
};

// The input history the sample-domain stretchers read, by absolute input frame.
class History
{
public:
    void allocate (int channels, int frames) { data.setSize (channels, frames); data.clear(); }
    void clear() { data.clear(); received = 0; }
    int channels() const { return data.getNumChannels(); }
    long long size() const { return data.getNumSamples(); }
    long long count() const { return received; }
    void append (const float* const* in, int frames)
    {
        for (int i = 0; i < frames; ++i, ++received)
            for (int c = 0; c < data.getNumChannels(); ++c)
                data.setSample (c, (int) (received % size()), in[c][i]);
    }
    // Linear interpolation; frames not yet received or no longer kept are silent.
    float read (int channel, double frame) const
    {
        const auto first = (long long) std::floor (frame);
        const auto t = (float) (frame - (double) first);
        return at (channel, first) * (1 - t) + at (channel, first + 1) * t;
    }
    float at (int channel, long long frame) const
    {
        if (frame < 0 || frame >= received || frame < received - size()) return 0;
        return data.getSample (channel, (int) (frame % size()));
    }
private:
    juce::AudioBuffer<float> data;
    long long received = 0;
};

// Texture: Hann grains at half overlap, each read from the input where the output stands, moved
// by up to half a grain at random by Flux. The seed is fixed, so a render repeats exactly.
class GrainStretcher final : public Stretcher
{
public:
    GrainStretcher (double rate, int block, int channels, const Options& options)
        : ratio { block }
    {
        const auto size = options.halftrack[grains::size] > 0 ? juce::jlimit (1.0f, 100.0f, options.halftrack[grains::size]) : 65.0f;
        grain = juce::jmax (64, (int) (rate * size * 0.004));
        hop = grain / 2;
        flux = juce::jlimit (0.0f, 100.0f, options.halftrack[grains::flux]) / 100.0f;
        window.resize ((size_t) grain);
        for (int i = 0; i < grain; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) grain);
        // Input a grain can read ahead of the output: four times its length when transposed up, and its jitter.
        lookahead = grain * maxRate + grain;
        history.allocate (channels, lookahead * 2 + block * fastestRatio + grain * maxRate);
        accumulator.setSize (channels, grain + block + 1);
        reset();
    }
    bool isOk() const override { return true; }
    void reset() override
    {
        history.clear(); accumulator.clear();
        produced = 0; nextGrain = 0; grainPosition = -lookahead; ratio.carry = 0; flushed = false;
        random.setSeed (0x48616c66);
    }
    bool setSpeedAndPitch (float speed, float semitones) override
    {
        ratio.speed = speed;
        pitch = juce::jlimit (1.0 / maxRate, (double) maxRate, std::pow (2.0, semitones / 12.0));
        return true;
    }
    int getFramesNeeded() const override { return ratio.needed(); }
    int getMaxFramesNeeded() const override { return ratio.block * fastestRatio + 1; }
    int processData (const float* const* in, int frames, float* const* out) override
    {
        history.append (in, frames);
        render (out);
        ratio.consumed (frames);
        return ratio.block;
    }
    int flush (float* const* out) override
    {
        if (flushed) return 0;
        flushed = true; render (out); return ratio.block;
    }
    int getLatencySamples() const override { return lookahead; }
private:
    static constexpr int maxRate = 4;
    BlockRatio ratio;
    History history;
    juce::AudioBuffer<float> accumulator; // Indexed by output frame, modulo its length.
    std::vector<float> window;
    juce::Random random;
    int grain = 0, hop = 0, lookahead = 0;
    float flux = 0;
    double pitch = 1, grainPosition = 0;
    long long produced = 0, nextGrain = 0;
    bool flushed = false;
    void render (float* const* out)
    {
        const auto length = accumulator.getNumSamples();
        for (int i = 0; i < ratio.block; ++i)
        {
            const auto frame = produced + i;
            while (nextGrain <= frame) addGrain();
            for (int c = 0; c < history.channels(); ++c)
            {
                auto* slot = accumulator.getWritePointer (c, (int) (frame % length));
                out[c][i] = *slot; *slot = 0;
            }
        }
        produced += ratio.block;
    }
    void addGrain()
    {
        const auto jitter = flux > 0 ? (random.nextFloat() * 2 - 1) * flux * (float) grain * 0.5f : 0.0f;
        const auto start = grainPosition + jitter;
        const auto length = accumulator.getNumSamples();
        for (int c = 0; c < history.channels(); ++c)
            for (int i = 0; i < grain; ++i)
                accumulator.addSample (c, (int) ((nextGrain + i) % length), history.read (c, start + i * pitch) * window[(size_t) i]);
        nextGrain += hop;
        grainPosition += hop / (double) ratio.speed;
    }
};

// Beats: transients found as the input arrives start segments, and each segment plays from its
// transient at its own speed when the output reaches that transient's stretched place. A segment
// that runs out before the next one starts falls silent or loops its second half; one that is
// cut short fades over 2 ms. The input is read half a second ahead, so a slowed segment plays
// in full while it stays within that.
class BeatsStretcher final : public Stretcher
{
public:
    BeatsStretcher (double rate, int block, int channels, const Options& options)
        : ratio { block }, detector (rate)
    {
        loop = (TransientLoop) juce::jlimit (0, 2, (int) options.halftrack[beats::loopMode]);
        fade = juce::jlimit (0.0f, 100.0f, options.halftrack[beats::fade]) / 100.0f;
        lookahead = (int) (rate * 0.5);
        crossfade = juce::jmax (8, (int) (rate * 0.002));
        history.allocate (channels, (int) (rate * 4) + block * fastestRatio);
        reset();
    }
    bool isOk() const override { return true; }
    void reset() override
    {
        history.clear();
        onsetCount = onsetFirst = 0; detector.reset();
        position = -lookahead; segmentStart = position; segmentOut = 0; tailLeft = 0;
        ratio.carry = 0; flushed = false;
        // The first segment starts where the input does.
        addOnset (0); detector.mark (0);
    }
    bool setSpeedAndPitch (float speed, float semitones) override
    {
        ratio.speed = speed;
        pitch = juce::jlimit (0.0625, 16.0, std::pow (2.0, semitones / 12.0));
        return true;
    }
    int getFramesNeeded() const override { return ratio.needed(); }
    int getMaxFramesNeeded() const override { return ratio.block * fastestRatio + 1; }
    int processData (const float* const* in, int frames, float* const* out) override
    {
        detect (in, frames);
        history.append (in, frames);
        render (out);
        ratio.consumed (frames);
        return ratio.block;
    }
    int flush (float* const* out) override
    {
        if (flushed) return 0;
        flushed = true; render (out); return ratio.block;
    }
    int getLatencySamples() const override { return lookahead; }
private:
    BlockRatio ratio;
    History history;
    TransientDetector detector;
    TransientLoop loop = TransientLoop::off;
    float fade = 0;
    int lookahead = 0, crossfade = 0;
    double pitch = 1, position = 0, segmentStart = 0, tailPosition = 0;
    long long segmentOut = 0;
    int tailLeft = 0;
    bool flushed = false;
    std::array<long long, 4096> onsets {};
    size_t onsetFirst = 0, onsetCount = 0;

    void addOnset (long long frame)
    {
        if (onsetCount == onsets.size()) { onsetFirst = (onsetFirst + 1) % onsets.size(); --onsetCount; }
        onsets[(onsetFirst + onsetCount) % onsets.size()] = frame; ++onsetCount;
    }
    void detect (const float* const* in, int frames)
    {
        const auto first = history.count();
        for (int i = 0; i < frames; ++i)
        {
            float level = 0;
            for (int c = 0; c < history.channels(); ++c) level += std::abs (in[c][i]);
            if (detector.next (level / (float) juce::jmax (1, history.channels()), first + i)) addOnset (first + i);
        }
    }
    void render (float* const* out)
    {
        for (int i = 0; i < ratio.block; ++i)
        {
            // The output reaches the next transient's stretched place.
            while (onsetCount > 0 && (double) onsets[onsetFirst] <= position)
            {
                const auto onset = onsets[onsetFirst];
                onsetFirst = (onsetFirst + 1) % onsets.size(); --onsetCount;
                tailPosition = readPosition(); tailLeft = crossfade;
                segmentStart = (double) onset; segmentOut = 0;
            }
            const auto end = onsetCount > 0 ? (double) onsets[onsetFirst] : (double) history.count() - 1;
            const auto at = readPosition();
            const auto gain = envelopeGain (end);
            for (int c = 0; c < history.channels(); ++c)
            {
                float sample = 0;
                if (const auto from = source (at, end); from >= 0) sample = history.read (c, from) * gain;
                if (tailLeft > 0) sample += history.read (c, tailPosition) * (float) tailLeft / (float) crossfade;
                out[c][i] = sample;
            }
            if (tailLeft > 0) { --tailLeft; tailPosition += pitch; }
            ++segmentOut;
            position += 1.0 / ratio.speed;
        }
    }
    double readPosition() const { return segmentStart + (double) segmentOut * pitch; }
    // Where a segment reads, looping its second half once its audio runs out; -1 for silence.
    double source (double at, double end) const
    {
        if (at < end) return at;
        const auto half = (end - segmentStart) / 2;
        if (loop == TransientLoop::off || half < 32) return -1;
        const auto base = segmentStart + half, t = std::fmod (at - base, loop == TransientLoop::forward ? half : half * 2);
        return base + (t < half ? t : half * 2 - t);
    }
    // Transient Envelope: below 100 a segment fades out over a share of its length.
    float envelopeGain (double end) const
    {
        if (fade <= 0) return 1;
        const auto segment = std::max (1.0, (end - segmentStart) * ratio.speed);
        return (float) juce::jlimit (0.0, 1.0, 1.0 - (double) segmentOut / (segment * (1.0 - 0.9 * fade)));
    }
};
}

void registerStretchers()
{
    te::TimeStretcher::createHalftrackStretcher = [] (double rate, int block, int channels, te::TimeStretcher::Mode mode,
                                                      const Options& options, bool realtime) -> std::unique_ptr<Stretcher>
    {
        switch (mode)
        {
            case te::TimeStretcher::halftrackTones:      return std::make_unique<SpectralStretcher> (rate, block, channels, true, options, realtime);
            case te::TimeStretcher::halftrackComplexPro: return std::make_unique<SpectralStretcher> (rate, block, channels, false, options, realtime);
            case te::TimeStretcher::halftrackTexture:    return std::make_unique<GrainStretcher> (rate, block, channels, options);
            case te::TimeStretcher::halftrackBeats:      return std::make_unique<BeatsStretcher> (rate, block, channels, options);
            default: return {};
        }
    };
}
}
