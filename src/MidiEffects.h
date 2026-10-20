#pragma once
#include "Session.h"
#include <array>
#include <cstdint>

// Original note processors for the native MIDI effect kinds. All state is fixed
// size; process() performs bounded work and never allocates, locks or blocks.
namespace halftrack::midifx
{
struct Event
{
    int sample = 0;            // Offset inside the current block.
    uint8_t status = 0, data1 = 0, data2 = 0;
    uint32_t source = 0;       // Engine MPE source identity, carried through.
    bool isNoteOn() const noexcept { return (status & 0xf0) == 0x90 && data2 > 0; }
    bool isNoteOff() const noexcept { return (status & 0xf0) == 0x80 || ((status & 0xf0) == 0x90 && data2 == 0); }
    int channel() const noexcept { return status & 0x0f; } // 0-based.
    static Event noteOn (int sample, int channel, int pitch, int velocity, uint32_t source) noexcept;
    static Event noteOff (int sample, int channel, int pitch, uint32_t source) noexcept;
};
// Step lengths in beats shared by the arpeggiator and note-length rate menus.
constexpr std::array<double, 9> stepBeats { .125, 1.0 / 6, .25, 1.0 / 3, .5, 2.0 / 3, 1, 2, 4 };
// Semitone membership masks (bit d set when degree d is in the scale).
constexpr std::array<uint16_t, 13> scaleMasks {
    0b101010110101, 0b010110101101, 0b011010101101, 0b010110101011, 0b101011010101,
    0b011010110101, 0b010101101011, 0b100110101101, 0b101010101101, 0b001010010101,
    0b010010101001, 0b010011101001, 0b111111111111 };
int quantizeToScale (int pitch, int root, uint16_t mask) noexcept;

class Processor
{
public:
    static constexpr int maximumHeld = 64, maximumPending = 512;
    void prepare (double sampleRate) noexcept;
    void configure (const Session::EffectSettings&) noexcept;
    void reset() noexcept;
    // Releases everything this processor started, at the given block sample.
    int allNotesOff (int sample, Event* out, int capacity) noexcept;
    // `in` holds this block's note messages in time order. Output is time ordered;
    // note-offs precede note-ons at one sample. Returns the number written.
    // `timelineSeconds` is the block start while the transport plays, otherwise negative.
    int process (const Event* in, int inCount, int samples, double timelineSeconds, Event* out, int capacity) noexcept;
private:
    struct Held { int channel = 0, pitch = 0, velocity = 0; uint32_t source = 0; uint64_t order = 0; };
    struct Pending { uint64_t time = 0; int channel = 0, pitch = 0; uint32_t source = 0; };
    struct Output { int count = 0; std::array<int8_t, 7> pitches {}; };
    uint32_t random() noexcept;
    double unit() noexcept { return random() / 4294967296.0; }
    void emit (const Event&) noexcept;
    void sounding (int sample, int channel, int pitch, int velocity, uint32_t source) noexcept;
    void silence (int sample, int channel, int pitch, uint32_t source) noexcept;
    void mapped (const Event&) noexcept;
    void lengthen (const Event&) noexcept;
    void arpeggiate (const Event&) noexcept;
    void flushPending (uint64_t until) noexcept;
    void schedule (uint64_t time, int channel, int pitch, uint32_t source) noexcept;
    void step (int sample) noexcept;
    double stepSamples() const noexcept;
    Session::EffectSettings settings;
    double rate = 48000;
    uint64_t clock = 0, nextOrder = 0, arpStart = 0;
    uint32_t seed = 0x2545f491u;
    int alternate = 0, arpStep = 0, heldCount = 0, latchedCount = 0;
    bool arpRunning = false;
    std::array<Output, 16 * 128> outputs {};
    std::array<uint8_t, 16 * 128> refcount {};
    std::array<Held, maximumHeld> held {};
    std::array<Pending, maximumPending> pending {};
    int pendingCount = 0;
    Event* target = nullptr;
    int written = 0, room = 0, blockSamples = 0;
    // Arpeggiator: the sounding step note.
    int arpChannel = -1, arpPitch = -1;
};
}
