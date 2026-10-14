#include "SubtractiveSynth.h"
#include <algorithm>
#include <cmath>

namespace halftrack
{
void SubtractiveSynthDSP::prepare (double rate) noexcept
{
    sampleRate = std::isfinite (rate) && rate >= 8000 && rate <= 384000 ? rate : 48000;
    for (int pitch = 0; pitch < 128; ++pitch)
        frequencies[static_cast<size_t> (pitch)] = 440.0 * std::exp2 ((pitch - 69) / 12.0);
    reset();
    setSettings (settings);
}
void SubtractiveSynthDSP::reset() noexcept
{
    voices = {};
    sustainPedal = {};
    nextAge = 0;
}
void SubtractiveSynthDSP::setSettings (const Session::SynthSettings& next) noexcept
{
    settings = next;
    const auto g = std::tan (juce::MathConstants<double>::pi * std::clamp (double (settings.cutoffHz), 20.0, sampleRate * 0.45) / sampleRate);
    const auto damping = 2.0 - 1.95 * settings.resonance;
    a1 = 1.0 / (1.0 + g * (g + damping));
    a2 = g * a1;
    a3 = g * a2;
    attackStep = float (1.0 / std::max (1.0, sampleRate * settings.attackSeconds));
    decayStep = float ((1.0 - settings.sustain) / std::max (1.0, sampleRate * settings.decaySeconds));
}
double SubtractiveSynthDSP::polyBlep (double phase, double increment) noexcept
{
    if (phase < increment)
    {
        const auto t = phase / increment;
        return 2 * t - t * t - 1;
    }
    if (phase > 1 - increment)
    {
        const auto t = (phase - 1) / increment;
        return t * t + 2 * t + 1;
    }
    return 0;
}
void SubtractiveSynthDSP::noteOn (int channel, int pitch, float velocity) noexcept
{
    if (channel < 1 || channel > 16 || pitch < 0 || pitch > 127) return;
    if (velocity <= 0) { noteOff (channel, pitch); return; }
    Voice* chosen = nullptr;
    for (auto& voice : voices)
        if (voice.stage == 0) { chosen = &voice; break; }
    if (! chosen)
    {
        chosen = &voices[0];
        for (auto& voice : voices) if (voice.age < chosen->age) chosen = &voice;
    }
    *chosen = {};
    chosen->channel = channel; chosen->pitch = pitch; chosen->keyDown = true;
    chosen->increment = std::min (0.45, frequencies[static_cast<size_t> (pitch)] / sampleRate);
    chosen->velocity = std::clamp (velocity, 0.0f, 1.0f);
    chosen->stage = 1; chosen->age = ++nextAge;
}
void SubtractiveSynthDSP::release (Voice& voice) noexcept
{
    voice.stage = 4;
    voice.releaseStep = float (voice.envelope / std::max (1.0, sampleRate * settings.releaseSeconds));
}
void SubtractiveSynthDSP::noteOff (int channel, int pitch) noexcept
{
    if (channel < 1 || channel > 16) return;
    for (auto& voice : voices)
        if (voice.stage != 0 && voice.channel == channel && voice.pitch == pitch && voice.keyDown)
        {
            voice.keyDown = false;
            if (! sustainPedal[static_cast<size_t> (channel - 1)]) release (voice);
        }
}
void SubtractiveSynthDSP::controlChange (int channel, int controller, int value) noexcept
{
    if (channel < 1 || channel > 16) return;
    if (controller == 64 || controller == 121)
    {
        auto& pedal = sustainPedal[static_cast<size_t> (channel - 1)];
        const bool down = controller == 64 && value >= 64;
        if (pedal && ! down)
            for (auto& voice : voices)
                if (voice.channel == channel && voice.stage != 0 && voice.stage != 4 && ! voice.keyDown)
                    release (voice);
        pedal = down;
    }
    else if (controller == 120)
    {
        for (auto& voice : voices) if (voice.channel == channel) voice = {};
    }
    else if (controller == 123)
    {
        for (auto& voice : voices)
            if (voice.channel == channel && voice.stage != 0 && voice.keyDown)
            {
                voice.keyDown = false;
                if (! sustainPedal[static_cast<size_t> (channel - 1)]) release (voice);
            }
    }
}
void SubtractiveSynthDSP::render (float* left, float* right, int samples) noexcept
{
    for (int sample = 0; sample < samples; ++sample)
    {
        double mixed = 0;
        for (auto& voice : voices)
        {
            if (voice.stage == 0) continue;
            if (voice.stage == 1)
            {
                voice.envelope = std::min (1.0f, voice.envelope + attackStep);
                if (voice.envelope >= 1) voice.stage = 2;
            }
            else if (voice.stage == 2)
            {
                voice.envelope = std::max (settings.sustain, voice.envelope - decayStep);
                if (voice.envelope <= settings.sustain) voice.stage = 3;
            }
            else if (voice.stage == 3) voice.envelope = settings.sustain;
            else
            {
                voice.envelope = std::max (0.0f, voice.envelope - voice.releaseStep);
                if (voice.envelope <= 0.000001f) { voice = {}; continue; }
            }
            const auto phase = voice.phase;
            auto oscillator = 2 * phase - 1 - polyBlep (phase, voice.increment);
            if (settings.waveform == 1)
            {
                auto shifted = phase + 0.5;
                if (shifted >= 1) shifted -= 1;
                oscillator = (phase < 0.5 ? 1.0 : -1.0) + polyBlep (phase, voice.increment) - polyBlep (shifted, voice.increment);
            }
            voice.phase += voice.increment;
            if (voice.phase >= 1) voice.phase -= 1;
            const auto v3 = oscillator - voice.filter2;
            const auto v1 = a1 * voice.filter1 + a2 * v3;
            const auto v2 = voice.filter2 + a2 * voice.filter1 + a3 * v3;
            voice.filter1 = 2 * v1 - voice.filter1;
            voice.filter2 = 2 * v2 - voice.filter2;
            if (std::abs (voice.filter1) < 1.0e-24) voice.filter1 = 0;
            if (std::abs (voice.filter2) < 1.0e-24) voice.filter2 = 0;
            mixed += v2 * voice.envelope * voice.velocity * settings.level;
        }
        if (left) left[sample] += static_cast<float> (mixed);
        if (right) right[sample] += static_cast<float> (mixed);
    }
}
int SubtractiveSynthDSP::activeVoices() const noexcept
{
    int result = 0;
    for (const auto& voice : voices) if (voice.stage != 0) ++result;
    return result;
}
bool SynthParameterQueue::push (const Session::SynthSettings& settings) noexcept
{
    const auto write = writeIndex.load (std::memory_order_relaxed);
    const auto next = (write + 1) % capacity;
    if (next == readIndex.load (std::memory_order_acquire)) return false;
    packets[write] = settings;
    writeIndex.store (next, std::memory_order_release);
    return true;
}
bool SynthParameterQueue::pop (Session::SynthSettings& settings) noexcept
{
    const auto read = readIndex.load (std::memory_order_relaxed);
    if (read == writeIndex.load (std::memory_order_acquire)) return false;
    settings = packets[read];
    readIndex.store ((read + 1) % capacity, std::memory_order_release);
    return true;
}

void SynthParameterQueue::resetWhileStopped() noexcept
{
    readIndex.store (0, std::memory_order_relaxed);
    writeIndex.store (0, std::memory_order_relaxed);
}

SubtractiveSynthPlugin::SubtractiveSynthPlugin (tracktion::PluginCreationInfo info) : Plugin (info)
{
    publishSettings();
    startTimerHz (60);
}
SubtractiveSynthPlugin::~SubtractiveSynthPlugin()
{
    stopTimer();
    notifyListenersOfDeletion();
}
Session::SynthSettings SubtractiveSynthPlugin::getSettings() const
{
    Session::SynthSettings s;
    s.cutoffHz = state.getProperty ("cutoffHz", s.cutoffHz);
    s.resonance = state.getProperty ("resonance", s.resonance);
    s.attackSeconds = state.getProperty ("attackSeconds", s.attackSeconds);
    s.decaySeconds = state.getProperty ("decaySeconds", s.decaySeconds);
    s.sustain = state.getProperty ("sustain", s.sustain);
    s.releaseSeconds = state.getProperty ("releaseSeconds", s.releaseSeconds);
    s.level = state.getProperty ("level", s.level);
    s.waveform = state.getProperty ("waveform", s.waveform);
    const Session::SynthSettings defaults;
    auto safe = [] (float value, float low, float high, float fallback)
    { return std::isfinite (value) ? std::clamp (value, low, high) : fallback; };
    s.cutoffHz = safe (s.cutoffHz, 20, 20000, defaults.cutoffHz);
    s.resonance = safe (s.resonance, 0, 1, defaults.resonance);
    s.attackSeconds = safe (s.attackSeconds, 0, 10, defaults.attackSeconds);
    s.decaySeconds = safe (s.decaySeconds, 0, 10, defaults.decaySeconds);
    s.sustain = safe (s.sustain, 0, 1, defaults.sustain);
    s.releaseSeconds = safe (s.releaseSeconds, 0, 20, defaults.releaseSeconds);
    s.level = safe (s.level, 0, 1, defaults.level);
    s.waveform = std::clamp (s.waveform, 0, 1);
    return s;
}
void SubtractiveSynthPlugin::setSettings (const Session::SynthSettings& s)
{
    writingSettings = true;
    auto* undo = getUndoManager();
    state.setProperty ("cutoffHz", s.cutoffHz, undo);
    state.setProperty ("resonance", s.resonance, undo);
    state.setProperty ("attackSeconds", s.attackSeconds, undo);
    state.setProperty ("decaySeconds", s.decaySeconds, undo);
    state.setProperty ("sustain", s.sustain, undo);
    state.setProperty ("releaseSeconds", s.releaseSeconds, undo);
    state.setProperty ("level", s.level, undo);
    state.setProperty ("waveform", s.waveform, undo);
    writingSettings = false;
    publishSettings();
}
void SubtractiveSynthPlugin::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    Plugin::valueTreePropertyChanged (tree, property);
    if (tree == state && ! writingSettings) publishSettings();
}
void SubtractiveSynthPlugin::publishSettings()
{
    pendingSettings = getSettings();
    pending = ! parameters.push (pendingSettings);
}
void SubtractiveSynthPlugin::timerCallback()
{
    if (pending) pending = ! parameters.push (pendingSettings);
}
void SubtractiveSynthPlugin::prepareForOfflineRender()
{
    JUCE_ASSERT_MESSAGE_THREAD
    // Session holds ScopedRenderStatus: no processing thread can touch this
    // queue. Replace any backlog with the current model, still through the FIFO,
    // so a fast offline render cannot start with obsolete controls.
    parameters.resetWhileStopped();
    publishSettings();
}
void SubtractiveSynthPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    synth.prepare (sampleRate);
}
void SubtractiveSynthPlugin::reset() { synth.reset(); }
void SubtractiveSynthPlugin::applyToBuffer (const tracktion::PluginRenderContext& context)
{
    // RT BEGIN: fixed-storage FIFO and DSP only; no model/UI work here.
    const juce::ScopedNoDenormals noDenormals;
    Session::SynthSettings next;
    bool changed = false;
    for (int count = 0; count < 64 && parameters.pop (next); ++count) changed = true;
    if (changed) synth.setSettings (next);
    if (! context.destBuffer || context.bufferNumSamples <= 0) return;
    auto& buffer = *context.destBuffer;
    buffer.clear (context.bufferStartSample, context.bufferNumSamples);
    auto* left = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0, context.bufferStartSample) : nullptr;
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, context.bufferStartSample) : nullptr;
    int position = 0;
    if (context.bufferForMidiMessages)
    {
        if (context.bufferForMidiMessages->isAllNotesOff) synth.reset();
        for (const auto& message : *context.bufferForMidiMessages)
        {
            const auto eventSample = std::clamp (static_cast<int> (std::llround ((message.getTimeStamp() + context.midiBufferOffset) * sampleRate)), position, context.bufferNumSamples);
            synth.render (left ? left + position : nullptr, right ? right + position : nullptr, eventSample - position);
            position = eventSample;
            if (message.isNoteOn()) synth.noteOn (message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
            else if (message.isNoteOff()) synth.noteOff (message.getChannel(), message.getNoteNumber());
            else if (message.isController()) synth.controlChange (message.getChannel(), message.getControllerNumber(), message.getControllerValue());
        }
    }
    synth.render (left ? left + position : nullptr, right ? right + position : nullptr, context.bufferNumSamples - position);
    // RT END
}
}
