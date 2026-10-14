#include "UtilityProcessor.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace halftrack
{
namespace
{
constexpr double pi = 3.14159265358979323846;
float finiteRange (float value, float low, float high, float fallback) noexcept
{ return std::isfinite (value) ? std::clamp (value, low, high) : fallback; }
Session::UtilitySettings sanitise (Session::UtilitySettings s) noexcept
{
    s.gainDb = finiteRange (s.gainDb, -100, 35, 0);
    s.balance = finiteRange (s.balance, -1, 1, 0);
    s.width = finiteRange (s.width, 0, 4, 1);
    s.midSide = finiteRange (s.midSide, -1, 1, 0);
    s.bassFrequency = finiteRange (s.bassFrequency, 50, 500, 120);
    s.channel = static_cast<Session::UtilitySettings::Channel> (std::clamp (static_cast<int> (s.channel), 0, 3));
    return s;
}
}

double UtilityDSP::Biquad::tick (double input, const std::array<double, 5>& c) noexcept
{
    const auto output = c[0] * input + z1;
    z1 = c[1] * input - c[3] * output + z2;
    z2 = c[2] * input - c[4] * output;
    return output;
}
double UtilityDSP::FirstOrder::tick (double input, const std::array<double, 3>& c) noexcept
{
    const auto output = c[0] * input + state;
    state = c[1] * input - c[2] * output;
    return output;
}
void UtilityDSP::prepare (double sampleRate) noexcept
{
    rate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, 768000.0) : 48000.0;
    // Bilinear high-pass, Q = 1/2: two coincident real poles at 5 Hz.
    const auto g = std::tan (pi * 5.0 / rate), norm = 1.0 / ((1.0 + g) * (1.0 + g));
    dcCoefficients = { norm, -2.0 * norm, norm, 2.0 * (g * g - 1.0) * norm, (1.0 - g) * (1.0 - g) * norm };
    reset();
    configure (settings);
}
void UtilityDSP::reset() noexcept
{
    firstLow = {}; firstHigh = {}; lowMid = {}; highSide = {}; dcFilters = {};
    processed = false; remaining = 0; current = target; step = {};
}
void UtilityDSP::configure (const Session::UtilitySettings& next) noexcept
{
    settings = sanitise (next);
    const double gain = settings.gainDb <= -100 ? 0.0 : std::pow (10.0, settings.gainDb / 20.0);
    double mid = 1, side = settings.width;
    if (settings.midSideMode)
    {
        mid = settings.midSide > 0 ? 1.0 - settings.midSide : 1.0;
        side = settings.midSide < 0 ? 1.0 + settings.midSide : 1.0;
    }
    if (settings.channel == Session::UtilitySettings::Channel::left || settings.channel == Session::UtilitySettings::Channel::right)
        mid = side = 1;
    if (settings.mono) side = 0;
    const auto angle = (static_cast<double> (settings.balance) + 1.0) * pi * 0.25;
    const auto leftBalance = settings.balance >= 1 ? 0.0 : std::sqrt (2.0) * std::cos (angle);
    const auto rightBalance = settings.balance <= -1 ? 0.0 : std::sqrt (2.0) * std::sin (angle);
    target = { gain * leftBalance, gain * rightBalance, mid, side, settings.mute ? 0.0 : 1.0 };
    if (! processed) { current = target; remaining = 0; step = {}; }
    else
    {
        remaining = std::max (1, static_cast<int> (std::lround (rate * 0.005)));
        for (size_t i = 0; i < step.size(); ++i) step[i] = (target[i] - current[i]) / remaining;
    }
    const auto k = std::tan (pi * settings.bassFrequency / rate);
    // A third-order Butterworth factors into a real pole and a Q=1 pair.
    const auto k2 = k * k, normal = 1.0 / (1.0 + k + k2), firstNormal = 1.0 / (1.0 + k);
    firstLowCoefficients = { k * firstNormal, k * firstNormal, (k - 1.0) * firstNormal };
    firstHighCoefficients = { firstNormal, -firstNormal, (k - 1.0) * firstNormal };
    lowPassCoefficients = { k2 * normal, 2.0 * k2 * normal, k2 * normal,
                            2.0 * (k2 - 1.0) * normal, (1.0 - k + k2) * normal };
    highPassCoefficients = { normal, -2.0 * normal, normal, lowPassCoefficients[3], lowPassCoefficients[4] };
}
void UtilityDSP::processSamples (float* left, float* right, int samples) noexcept
{
    if (left == nullptr || samples <= 0) return;
    processed = true;
    for (int i = 0; i < samples; ++i)
    {
        if (remaining > 0)
        {
            for (size_t index = 0; index < current.size(); ++index) current[index] += step[index];
            if (--remaining == 0) current = target;
        }
        double l = std::isfinite (left[i]) ? left[i] : 0;
        double r = right ? (std::isfinite (right[i]) ? right[i] : 0) : l;
        if (settings.invertLeft) l = -l;
        if (settings.invertRight) r = -r;
        switch (settings.channel)
        {
            case Session::UtilitySettings::Channel::left: r = l; break;
            case Session::UtilitySettings::Channel::right: l = r; break;
            case Session::UtilitySettings::Channel::swap: std::swap (l, r); break;
            default: break;
        }
        const auto rawMid = 0.5 * (l + r), rawSide = 0.5 * (l - r);
        // Keep filters on the unweighted signal. A completed mono/side ramp
        // must silence that component even when the filters still have history.
        const auto bassMid = lowMid.tick (firstLow.tick (rawMid, firstLowCoefficients), lowPassCoefficients);
        const auto trebleSide = highSide.tick (firstHigh.tick (rawSide, firstHighCoefficients), highPassCoefficients);
        const auto mid = rawMid * current[2];
        const auto side = (settings.bassMono ? trebleSide : rawSide) * current[3];
        if (settings.bassMono && settings.bassAudition) l = r = bassMid * current[2];
        else { l = mid + side; r = mid - side; }
        const double inputs[] { l * current[0], r * current[1] };
        double outputs[2] {};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto filtered = dcFilters[(size_t) channel].tick (inputs[channel], dcCoefficients);
            outputs[channel] = (settings.dcFilter ? filtered : inputs[channel]) * current[4];
        }
        constexpr auto limit = static_cast<double> (std::numeric_limits<float>::max());
        left[i] = static_cast<float> (std::clamp (outputs[0], -limit, limit));
        if (right) right[i] = static_cast<float> (std::clamp (outputs[1], -limit, limit));
    }
}
bool UtilityParameterQueue::push (const Session::UtilitySettings& packet) noexcept
{
    const auto write = writeIndex.load (std::memory_order_relaxed), next = (write + 1) % capacity;
    if (next == readIndex.load (std::memory_order_acquire)) return false;
    packets[write] = packet;
    writeIndex.store (next, std::memory_order_release);
    return true;
}
bool UtilityParameterQueue::pop (Session::UtilitySettings& packet) noexcept
{
    const auto read = readIndex.load (std::memory_order_relaxed);
    if (read == writeIndex.load (std::memory_order_acquire)) return false;
    packet = packets[read];
    readIndex.store ((read + 1) % capacity, std::memory_order_release);
    return true;
}
void UtilityParameterQueue::resetWhileStopped() noexcept
{ readIndex.store (0, std::memory_order_relaxed); writeIndex.store (0, std::memory_order_relaxed); }

UtilityPlugin::UtilityPlugin (tracktion::PluginCreationInfo info) : Plugin (info)
{ publish(); startTimerHz (30); }
UtilityPlugin::~UtilityPlugin() { stopTimer(); }
bool UtilityPlugin::validSettings (const Session::UtilitySettings& s) noexcept
{
    return std::isfinite (s.gainDb) && s.gainDb >= -100 && s.gainDb <= 35
        && std::isfinite (s.balance) && s.balance >= -1 && s.balance <= 1
        && std::isfinite (s.width) && s.width >= 0 && s.width <= 4
        && std::isfinite (s.midSide) && s.midSide >= -1 && s.midSide <= 1
        && std::isfinite (s.bassFrequency) && s.bassFrequency >= 50 && s.bassFrequency <= 500
        && static_cast<int> (s.channel) >= 0 && static_cast<int> (s.channel) <= 3;
}
juce::Result UtilityPlugin::validateState (const juce::ValueTree& tree)
{
    if (tree.hasType (tracktion::IDs::PLUGIN) && tree[tracktion::IDs::type].toString() == xmlTypeName)
    {
        struct Field { const char* name; double low, high; bool integral; };
        constexpr Field fields[] { { "gainDb", -100, 35, false }, { "balance", -1, 1, false }, { "width", 0, 4, false },
            { "midSide", -1, 1, false }, { "bassFrequency", 50, 500, false }, { "channel", 0, 3, true },
            { "midSideMode", 0, 1, true }, { "invertLeft", 0, 1, true }, { "invertRight", 0, 1, true }, { "mono", 0, 1, true },
            { "bassMono", 0, 1, true }, { "bassAudition", 0, 1, true }, { "mute", 0, 1, true }, { "dcFilter", 0, 1, true } };
        for (const auto& field : fields)
            if (tree.hasProperty (field.name))
            {
                const auto text = tree[field.name].toString().trim().toStdString();
                char* end = nullptr;
                const auto value = std::strtod (text.c_str(), &end);
                if (text.empty() || end != text.c_str() + text.size() || ! std::isfinite (value)
                    || value < field.low || value > field.high || (field.integral && std::floor (value) != value))
                    return juce::Result::fail ("The project contains invalid channel utility settings.");
            }
    }
    for (const auto& child : tree)
        if (auto result = validateState (child); result.failed()) return result;
    return juce::Result::ok();
}
Session::UtilitySettings UtilityPlugin::readSettings() const
{
    Session::UtilitySettings s;
    s.gainDb = state.getProperty ("gainDb", 0.0f); s.balance = state.getProperty ("balance", 0.0f);
    s.width = state.getProperty ("width", 1.0f); s.midSide = state.getProperty ("midSide", 0.0f);
    s.bassFrequency = state.getProperty ("bassFrequency", 120.0f);
    s.channel = static_cast<Session::UtilitySettings::Channel> (static_cast<int> (state.getProperty ("channel", 0)));
    s.midSideMode = state.getProperty ("midSideMode", false);
    s.invertLeft = state.getProperty ("invertLeft", false); s.invertRight = state.getProperty ("invertRight", false);
    s.mono = state.getProperty ("mono", false); s.bassMono = state.getProperty ("bassMono", false);
    s.bassAudition = state.getProperty ("bassAudition", false); s.mute = state.getProperty ("mute", false);
    s.dcFilter = state.getProperty ("dcFilter", false);
    return sanitise (s);
}
void UtilityPlugin::writeSettings (const Session::UtilitySettings& s)
{
    writing = true;
    auto* undo = getUndoManager();
    state.setProperty ("gainDb", s.gainDb, undo); state.setProperty ("balance", s.balance, undo);
    state.setProperty ("width", s.width, undo); state.setProperty ("midSide", s.midSide, undo);
    state.setProperty ("bassFrequency", s.bassFrequency, undo); state.setProperty ("channel", static_cast<int> (s.channel), undo);
    state.setProperty ("midSideMode", s.midSideMode, undo); state.setProperty ("invertLeft", s.invertLeft, undo);
    state.setProperty ("invertRight", s.invertRight, undo); state.setProperty ("mono", s.mono, undo);
    state.setProperty ("bassMono", s.bassMono, undo); state.setProperty ("bassAudition", s.bassAudition, undo);
    state.setProperty ("mute", s.mute, undo); state.setProperty ("dcFilter", s.dcFilter, undo);
    writing = false;
    publish();
}
void UtilityPlugin::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    Plugin::valueTreePropertyChanged (tree, property);
    if (tree == state && ! writing) publish();
}
void UtilityPlugin::publish()
{ pendingSettings = readSettings(); pending = ! parameters.push (pendingSettings); }
void UtilityPlugin::timerCallback()
{ if (pending) pending = ! parameters.push (pendingSettings); }
void UtilityPlugin::prepareForOfflineRender()
{
    JUCE_ASSERT_MESSAGE_THREAD
    parameters.resetWhileStopped(); publish();
}
void UtilityPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{ sampleRate = info.sampleRate; processor.prepare (sampleRate); }
void UtilityPlugin::reset() { processor.reset(); }
void UtilityPlugin::applyToBuffer (const tracktion::PluginRenderContext& context)
{
    const juce::ScopedNoDenormals noDenormals;
    Session::UtilitySettings next;
    bool changed = false;
    for (int count = 0; count < 64 && parameters.pop (next); ++count) changed = true;
    if (changed) processor.configure (next);
    if (context.destBuffer == nullptr || context.bufferNumSamples <= 0 || context.destBuffer->getNumChannels() == 0) return;
    auto* left = context.destBuffer->getWritePointer (0, context.bufferStartSample);
    auto* right = context.destBuffer->getNumChannels() > 1 ? context.destBuffer->getWritePointer (1, context.bufferStartSample) : nullptr;
    processor.processSamples (left, right, context.bufferNumSamples);
}
}
