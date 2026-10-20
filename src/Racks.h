#pragma once
#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

// Racks hold parallel chains of native devices inside one track device. The rack is a
// NativeEffectPlugin of a rack kind; its chains, a Drum Rack's returns and its macro mappings
// are child trees of that device's state, and every device in a chain is an ordinary
// NativeEffectPlugin that the rack processes itself. The message thread builds an immutable
// layout of the chains and publishes it through an atomic pointer; the audio thread adopts it at
// a block start and acknowledges it, and older layouts are freed only after that acknowledgement.
namespace halftrack
{
class NativeEffectPlugin;
namespace racks
{
using Kind = Session::EffectKind;
inline const juce::Identifier chainType { "RACKCHAIN" }, returnType { "RACKRETURN" }, mappingType { "MACROMAPPING" };
inline const juce::Identifier visibleMacrosProperty { "halftrackVisibleMacros" };
constexpr int maximumSlots = Session::maximumRackChains + Session::maximumRackReturns;
constexpr int maximumDepth = 4;            // A rack and up to three racks nested inside it.
constexpr double rampSeconds = .005;       // Chain gain, input and send changes ramp over 5 ms.
constexpr double chokeSeconds = .005;      // A choked instrument fades out over 5 ms.
constexpr int controlCount = std::tuple_size_v<decltype (Session::EffectSettings::values)>;

bool isRack (Kind) noexcept;
// Where a device sits in a chain: 0 MIDI effect, 1 instrument, 2 audio effect.
int deviceStage (Kind) noexcept;
// Whether a chain of this rack kind can hold the device kind (nested racks included).
bool chainAccepts (Kind rack, Kind device) noexcept;

// Zone arithmetic. A note or velocity outside the zone is filtered (gain 0). Inside a fade range
// the gain rises linearly from the zone's edge: value v in [low, fadeLow) has (v - low + 1) /
// (fadeLow - low + 1), and symmetrically above fadeHigh, so the gain would reach zero one step
// outside the zone.
bool validZone (const Session::RackZone&, int minimum, int maximum) noexcept;
bool inZone (const Session::RackZone&, int value) noexcept;
double zoneGain (const Session::RackZone&, int value) noexcept;
// A chain is reached while the selector is inside its zone. Outside it the chain's output is
// silent only on a side that ends in a fade range; otherwise it is left at full level so tails ring.
double selectorOutputGain (const Session::RackZone&, int selector) noexcept;
// The track mixer's law: sqrt(2) * sin, unity at the centre.
std::pair<double, double> panGains (double pan) noexcept;
double decibelGain (double decibels) noexcept; // -100 dB and below is silence.

// Chain settings live in named properties of a chain tree.
Session::RackChainSettings readChain (const juce::ValueTree&);
void writeChain (juce::ValueTree&, const Session::RackChainSettings&, juce::UndoManager*);
bool validChain (const Session::RackChainSettings&, Kind rack, bool isReturn) noexcept;
Session::MacroMapping readMapping (const juce::ValueTree&);
juce::ValueTree makeMapping (const Session::MacroMapping&);
// The range of a mapping target's parameter, in its units: a device control, chain volume, pan
// or send, or the chain selector. Empty when the target has no such parameter.
std::optional<std::pair<float, float>> targetRange (Kind targetKind, bool chainTarget, bool rackTarget, int parameter) noexcept;
// Knob position (0-1) of a chain parameter value and back: volume and sends use the fader taper.
float chainValueToProportion (int parameter, float value) noexcept;
float chainProportionToValue (int parameter, float proportion) noexcept;
// Validates a rack device's saved structure before a project replaces the open one.
juce::Result validateRack (const juce::ValueTree& rackState);

// Snapshot helpers for editors. A device anywhere in a snapshot (on a track or inside racks),
// with the rack directly holding it; the pointers live as long as the snapshot.
struct Located { const Session::PluginInfo* device = nullptr; const Session::PluginInfo* rack = nullptr; const Session::TrackInfo* track = nullptr; };
Located locate (const Session::Snapshot&, const juce::String& id);
// The value a rack's macro gives one of its targets, in the target's units, when a macro drives it.
std::optional<float> macroValue (const Session::PluginInfo& rack, const juce::String& targetId, int parameter);
// The macro driving a target, or -1.
int macroOf (const Session::PluginInfo& rack, const juce::String& targetId, int parameter);
// A macro's label: the name of its only target's control, otherwise "Macro N".
juce::String macroName (const Session::PluginInfo& rack, int macro);

class RackEngine final
{
public:
    explicit RackEngine (NativeEffectPlugin&);
    ~RackEngine();
    // Message thread.
    void structureChanged();
    void initialise (const tracktion::PluginInitialisationInfo&);
    void deinitialise();
    void reset();
    void prepareForOfflineRender();
    void refreshAutomation();
    void retire(); // Frees acknowledged layouts and releases devices that left the rack.
    double latencySeconds() const noexcept { return latency; }
    double tailSeconds() const noexcept { return tail; }
    // Devices directly in this rack's chains and returns, in order.
    std::vector<NativeEffectPlugin*> devices() const;
    // A device here or in a nested rack.
    NativeEffectPlugin* findDevice (const juce::String& id) const;
    // Audio thread.
    void process (const tracktion::PluginRenderContext&, const Session::EffectSettings& controls) noexcept;
    void chokeAt (int sample) noexcept; // Chokes every chain's instruments in the next block.
private:
    struct Device
    {
        tracktion::Plugin* plugin = nullptr;
        NativeEffectPlugin* native = nullptr;
        Kind kind = Kind::equalizer;
        bool enabled = true;
        int mapped = -1; // Index of this device's macro value array, or -1.
    };
    struct Chain
    {
        int slot = 0;
        uint32_t generation = 0;
        bool isReturn = false;
        Session::RackChainSettings settings;
        std::vector<Device> devices;
        int latency = 0, compensation = 0;
        std::array<std::vector<float>, 2> delay;
        int delayCursor = 0;
        int chokes = 0; // Audio thread: pending choke samples this chunk.
        std::array<int, 16> chokeSamples {};
    };
    struct Mapping
    {
        enum class Target { device, chainVolume, chainPan, chainSend, selector };
        Target target = Target::device;
        int macro = 0, index = 0, parameter = 0; // index: mapped device array or chain slot.
        Kind kind = Kind::equalizer;
        float minimum = 0, maximum = 1; // Knob positions.
    };
    struct Layout
    {
        std::vector<Chain> chains, returns;
        std::vector<Mapping> mappings;
        std::vector<std::array<float, controlCount>> values; // Macro-driven device controls, NaN where unmapped.
        std::array<int, maximumSlots> slotChain {}; // Slot to chain index (returns follow chains), or -1.
        std::vector<tracktion::Plugin::Ptr> holders;
        std::array<std::vector<float>, 2> dryDelay; // Delays the chains by the returns' latency.
        int dryCursor = 0, latency = 0;
        bool anySolo = false;
    };
    struct Ramp
    {
        double value = 0, target = 0;
        int remaining = 0;
        void set (double next, int length, bool immediate) noexcept;
        double tick() noexcept;
    };
    struct SlotState
    {
        uint32_t generation = 0;
        bool primed = false; // The first chunk starts at its targets instead of ramping.
        Ramp left, right, input;
        std::array<Ramp, Session::maximumRackReturns> sends;
    };
    struct Routed { int sample, chain; uint8_t status, data1, data2, size; };
    struct Held { uint8_t channel, note, played; int slot; uint32_t generation; };
    std::unique_ptr<Layout> build();
    void publish (bool restartGraph = true);
    void adopt() noexcept;
    void processChunk (const tracktion::PluginRenderContext&, int offset, int count, const Session::EffectSettings&) noexcept;
    void runChain (Chain&, const tracktion::PluginRenderContext&, int offset, int count, bool midiInput) noexcept;
    int route (const tracktion::PluginRenderContext&, int offset, int count, int selector) noexcept;
    void routeNote (int sample, uint8_t status, uint8_t note, uint8_t velocity, int selector) noexcept;
    NativeEffectPlugin& owner;
    const Kind kind;
    // Message thread.
    std::vector<tracktion::Plugin::Ptr> current, initialised;
    std::vector<std::unique_ptr<Layout>> layouts;
    std::array<juce::String, maximumSlots> slotIds;
    std::array<uint32_t, maximumSlots> slotGenerations {};
    std::optional<tracktion::PluginInitialisationInfo> info;
    double latency = 0, tail = 0;
    // Shared.
    std::atomic<Layout*> published { nullptr }, acknowledged { nullptr };
    static_assert (std::atomic<Layout*>::is_always_lock_free);
    // Audio thread.
    Layout* active = nullptr;
    std::array<SlotState, maximumSlots> slots {};
    juce::AudioBuffer<float> input, scratch, mix;
    std::array<juce::AudioBuffer<float>, Session::maximumRackReturns> returnMix;
    tracktion::MidiMessageArray chainMidi, midiOut;
    std::array<Routed, 4096> routed {};
    int routedCount = 0;
    std::array<Held, 1024> held {};
    int heldCount = 0;
    std::array<float, maximumSlots> volumeOverride {}, panOverride {};
    std::array<std::array<float, Session::maximumRackReturns>, maximumSlots> sendOverride {};
    std::array<int, 16> pendingChokes {};
    int pendingChokeCount = 0;
    int capacity = 0, rampLength = 240;
    double rate = 48000;
    JUCE_DECLARE_NON_COPYABLE (RackEngine)
};
}
}
