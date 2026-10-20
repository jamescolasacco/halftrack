#include "SubtractiveSynth.h"
#include "UtilityProcessor.h"
#include "NativeEffects.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#if defined (_WIN32)
 #include <malloc.h>
#endif

// Linked only in instrumented BUILD_TESTING builds. These hooks count C++
// replaceable allocation/deallocation calls on the guarded thread. They do not
// intercept malloc, framework allocators, locks, I/O, or system calls.
namespace halftrack::allocationAudit
{
thread_local bool enabled = false;
thread_local std::size_t allocations = 0, deallocations = 0;

void recordAllocation() noexcept { if (enabled) ++allocations; }
void recordDeallocation (void* p) noexcept { if (enabled && p) ++deallocations; }

void* allocate (std::size_t size, std::size_t alignment = 0)
{
    recordAllocation();
    for (;;)
    {
        void* memory = nullptr;
        if (alignment == 0)
            memory = std::malloc (size == 0 ? 1 : size);
        else
        {
            alignment = std::max (alignment, sizeof (void*));
           #if defined (_WIN32)
            memory = _aligned_malloc (size == 0 ? 1 : size, alignment);
           #else
            if (posix_memalign (&memory, alignment, size == 0 ? 1 : size) != 0) memory = nullptr;
           #endif
        }
        if (memory) return memory;
        if (auto handler = std::get_new_handler()) handler();
        else throw std::bad_alloc();
    }
}
void deallocate (void* p, bool aligned = false) noexcept
{
    recordDeallocation (p);
   #if defined (_WIN32)
    if (aligned) { _aligned_free (p); return; }
   #else
    (void) aligned;
   #endif
    std::free (p);
}
struct Counts { std::size_t allocations, deallocations; };
template <typename Function>
Counts measure (Function&& function)
{
    const bool previous = enabled;
    const auto oldAllocations = allocations, oldDeallocations = deallocations;
    enabled = true;
    try { function(); }
    catch (...) { enabled = previous; throw; }
    enabled = previous;
    return { allocations - oldAllocations, deallocations - oldDeallocations };
}
}

void* operator new (std::size_t n) { return halftrack::allocationAudit::allocate (n); }
void* operator new[] (std::size_t n) { return halftrack::allocationAudit::allocate (n); }
void operator delete (void* p) noexcept { halftrack::allocationAudit::deallocate (p); }
void operator delete[] (void* p) noexcept { halftrack::allocationAudit::deallocate (p); }
void operator delete (void* p, std::size_t) noexcept { halftrack::allocationAudit::deallocate (p); }
void operator delete[] (void* p, std::size_t) noexcept { halftrack::allocationAudit::deallocate (p); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new (n); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new[] (n); } catch (...) { return nullptr; } }
void operator delete (void* p, const std::nothrow_t&) noexcept { ::operator delete (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { ::operator delete[] (p); }
#if defined (__cpp_aligned_new)
void* operator new (std::size_t n, std::align_val_t a) { return halftrack::allocationAudit::allocate (n, static_cast<std::size_t> (a)); }
void* operator new[] (std::size_t n, std::align_val_t a) { return halftrack::allocationAudit::allocate (n, static_cast<std::size_t> (a)); }
void operator delete (void* p, std::align_val_t) noexcept { halftrack::allocationAudit::deallocate (p, true); }
void operator delete[] (void* p, std::align_val_t) noexcept { halftrack::allocationAudit::deallocate (p, true); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { halftrack::allocationAudit::deallocate (p, true); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { halftrack::allocationAudit::deallocate (p, true); }
void* operator new (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new (n, a); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new[] (n, a); } catch (...) { return nullptr; } }
void operator delete (void* p, std::align_val_t a, const std::nothrow_t&) noexcept { ::operator delete (p, a); }
void operator delete[] (void* p, std::align_val_t a, const std::nothrow_t&) noexcept { ::operator delete[] (p, a); }
#endif

namespace halftrack
{
namespace
{
void requireRealtime (bool condition, const char* message)
{
    if (! condition) throw std::runtime_error (message);
    std::cout << "PASS " << message << '\n';
}
void noHeapCalls (allocationAudit::Counts counts, const char* message)
{
    if (counts.allocations != 0 || counts.deallocations != 0)
    {
        std::cerr << "Observed new/delete calls: " << counts.allocations << '/' << counts.deallocations << '\n';
        throw std::runtime_error (message);
    }
    std::cout << "PASS " << message << '\n';
}
Session::SynthSettings packet (int sequence)
{
    Session::SynthSettings result;
    result.cutoffHz = float (100 + sequence);
    result.resonance = float (sequence % 100) / 100.0f;
    result.waveform = sequence % 4;
    result.pulseWidth = float (sequence % 101) / 100.0f;
    return result;
}
Session::UtilitySettings utilityPacket (int sequence)
{
    Session::UtilitySettings result;
    result.gainDb = float (sequence % 136) - 100;
    result.balance = float (sequence % 201) / 100 - 1;
    result.width = float (sequence % 401) / 100;
    result.midSide = -result.balance;
    result.bassFrequency = float (sequence % 451) + 50;
    result.channel = static_cast<Session::UtilitySettings::Channel> (sequence % 4);
    result.midSideMode = (sequence & 1) != 0;
    result.invertLeft = (sequence & 2) != 0; result.invertRight = (sequence & 4) != 0;
    result.mono = (sequence & 8) != 0; result.bassMono = (sequence & 16) != 0;
    result.bassAudition = (sequence & 32) != 0; result.mute = (sequence & 64) != 0;
    result.dcFilter = (sequence & 128) != 0;
    return result;
}
void utilityRealtimeTests()
{
    UtilityDSP dsp; dsp.prepare (48000);
    std::array<float, 1024> left {}, right {};
    bool finite = true;
    const auto processing = allocationAudit::measure ([&]
    {
        for (int pass = 0; pass < 512; ++pass)
        {
            dsp.configure (utilityPacket (pass));
            for (const int samples : { 0, 1, 3, 64, 127, 256, 511, 1024 })
            {
                left.fill (0.125f); right.fill (-0.25f);
                dsp.processSamples (left.data(), right.data(), samples);
                for (int i = 0; i < samples; ++i)
                    finite = finite && std::isfinite (left[(size_t) i]) && std::isfinite (right[(size_t) i]);
            }
            dsp.processSamples (left.data(), nullptr, 1024);
            dsp.reset();
        }
    });
    noHeapCalls (processing, "owned utility settings, stereo/mono processing and reset have zero intercepted C++ new/delete calls");
    requireRealtime (finite, "utility parameter extremes, filter switches and variable blocks remain finite");
    UtilityParameterQueue queue;
    int accepted = 0, consumed = 0; bool coherent = true;
    const auto queueCalls = allocationAudit::measure ([&]
    {
        for (int i = 0; i < 64; ++i) if (queue.push (utilityPacket (i))) ++accepted;
        Session::UtilitySettings next;
        while (queue.pop (next)) { coherent = coherent && next == utilityPacket (consumed++); dsp.configure (next); }
    });
    noHeapCalls (queueCalls, "utility queue fill, overflow, drain and application have zero intercepted new/delete calls");
    requireRealtime (accepted == 63 && consumed == 63 && coherent, "utility queue preserves complete settings at its bounded capacity");
    constexpr int count = 10000;
    UtilityParameterQueue concurrent;
    std::atomic<bool> abort { false };
    std::thread producer ([&]
    {
        for (int i = 0; i < count && ! abort.load (std::memory_order_relaxed);)
            if (concurrent.push (utilityPacket (i))) ++i;
            else std::this_thread::yield();
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    int received = 0; allocationAudit::Counts calls {};
    while (received < count && coherent && std::chrono::steady_clock::now() < deadline)
    {
        Session::UtilitySettings next; bool popped = false;
        const auto iteration = allocationAudit::measure ([&] { popped = concurrent.pop (next); });
        calls.allocations += iteration.allocations; calls.deallocations += iteration.deallocations;
        if (! popped) { std::this_thread::yield(); continue; }
        coherent = next == utilityPacket (received++);
    }
    abort.store (true, std::memory_order_relaxed); producer.join();
    noHeapCalls (calls, "concurrent utility parameter consumer has zero intercepted new/delete calls");
    requireRealtime (received == count && coherent, "utility SPSC queue delivers 10000 complete ordered packets across threads");
}
}

void nativeEffectsRealtimeTests()
{
    for (int kind = 0; kind < Session::effectKindCount; ++kind)
    {
        NativeEffectDSP dsp; dsp.prepare (48000, static_cast<Session::EffectKind> (kind));
        auto settings = defaultEffectSettings (static_cast<Session::EffectKind> (kind));
        const auto fields = effectParameters (settings.kind);
        std::array<float, 1024> left {}, right {}; bool finite = true;
        const auto calls = allocationAudit::measure ([&]
        {
            for (int pass = 0; pass < 32; ++pass)
            {
                for (size_t i = 0; i < fields.size(); ++i) settings.values[i] = pass % 2 ? fields[i].minimum : fields[i].maximum;
                settings.tempoBpm = pass % 2 ? 20.0f : 999.0f;
                dsp.configure (settings);
                for (size_t i = 0; i < left.size(); ++i) { left[i] = (float) (i % 37) / 128; right[i] = -left[i] / 2; }
                dsp.processSamples (left.data(), pass % 2 ? right.data() : nullptr, 1 + pass * 31);
                for (auto value : left) finite = finite && std::isfinite (value);
                for (auto value : right) finite = finite && std::isfinite (value);
            }
            dsp.reset();
        });
        noHeapCalls (calls, "native effect parameter extremes, processing and reset allocate/free nothing");
        requireRealtime (finite, "native effect extrema and variable mono/stereo blocks remain finite");
    }
    for (int kind = static_cast<int> (Session::EffectKind::arpeggiator); kind <= static_cast<int> (Session::EffectKind::noteLength); ++kind)
    {
        midifx::Processor processor; processor.prepare (48000);
        auto settings = defaultEffectSettings (static_cast<Session::EffectKind> (kind));
        const auto fields = effectParameters (settings.kind);
        std::array<midifx::Event, 256> input {}; std::array<midifx::Event, 2048> output {};
        int generated = 0; bool ordered = true;
        const auto calls = allocationAudit::measure ([&]
        {
            const auto defaults = settings;
            for (int pass = 0; pass < 65; ++pass)
            {
                // The final pass uses defaults, where every kind must emit notes.
                for (size_t i = 0; i < fields.size(); ++i) settings.values[i] = pass == 64 ? defaults.values[i] : pass % 2 ? fields[i].minimum : fields[i].maximum;
                settings.tempoBpm = pass % 2 ? 20.0f : 999.0f;
                processor.configure (settings);
                for (int i = 0; i < 256; ++i)
                    input[(size_t) i] = i % 2 ? midifx::Event::noteOff (i, i % 16, (i * 7) % 128, 0) : midifx::Event::noteOn (i, i % 16, (i * 7) % 128, 1 + i % 127, 0);
                const auto count = processor.process (input.data(), 256, 512, pass % 3 == 0 ? -1.0 : pass * .01, output.data(), (int) output.size());
                generated += count;
                for (int i = 1; i < count; ++i) ordered = ordered && output[(size_t) i].sample >= output[(size_t) i - 1].sample;
            }
            generated += processor.allNotesOff (0, output.data(), (int) output.size());
            processor.reset();
        });
        noHeapCalls (calls, "MIDI effect extremes, note floods, timelines and panic allocate/free nothing");
        const auto text = juce::String (effectName (settings.kind)) + " output stays bounded and time ordered under note floods (generated "
                          + juce::String (generated) + (ordered ? ", ordered)" : ", unordered)");
        requireRealtime (generated > 0 && ordered, text.toRawUTF8());
    }
    for (int kind = static_cast<int> (Session::EffectKind::polySynth); kind < Session::effectKindCount; ++kind)
    {
        instruments::InstrumentDSP instrument; instrument.prepare (48000, static_cast<Session::EffectKind> (kind));
        auto settings = defaultEffectSettings (static_cast<Session::EffectKind> (kind));
        const auto fields = effectParameters (settings.kind);
        std::array<float, 512> left {}, right {}; bool finite = true;
        // The Sampler plays prepared samples and switches between them inside the audit.
        const bool sampler = settings.kind == Session::EffectKind::sampler;
        instruments::SampleData longSample, shortSample;
        for (int n = 0; n < 4800; ++n) { longSample.left.push_back ((float) std::sin (n * .05)); longSample.right.push_back ((float) std::cos (n * .03)); }
        shortSample.left.assign (3, .5f); shortSample.right.assign (3, -.5f); shortSample.sampleRate = 8000;
        if (sampler) instrument.setSample (&longSample);
        const auto calls = allocationAudit::measure ([&]
        {
            for (int pass = 0; pass < 24; ++pass)
            {
                for (size_t i = 0; i < fields.size(); ++i) settings.values[i] = pass % 2 ? fields[i].minimum : fields[i].maximum;
                instrument.configure (settings);
                if (sampler) instrument.setSample (pass % 5 == 4 ? &shortSample : pass % 7 == 6 ? nullptr : &longSample);
                for (int note = 24; note < 108; note += 3) instrument.noteOn (1 + note % 16, note, (float) (note % 7 + 1) / 7);
                instrument.pitchWheel (1, pass % 2 ? 0 : 16383); instrument.controlChange (1, 64, pass % 2 ? 0 : 127);
                instrument.render (left.data(), pass % 3 ? right.data() : nullptr, 1 + pass * 21);
                for (int note = 24; note < 108; note += 6) instrument.noteOff (1 + note % 16, note);
                instrument.controlChange (1 + pass % 16, pass % 2 ? 120 : 123, 0);
                instrument.render (left.data(), right.data(), 512);
                for (auto v : left) finite = finite && std::isfinite (v);
                for (auto v : right) finite = finite && std::isfinite (v);
                left.fill (0); right.fill (0);
            }
            instrument.reset();
        });
        noHeapCalls (calls, "native instrument extremes, voice floods, controllers and reset allocate/free nothing");
        const auto text = juce::String (effectName (settings.kind)) + " remains finite at parameter extremes";
        requireRealtime (finite, text.toRawUTF8());
    }
    EffectParameterQueue queue; auto value = defaultEffectSettings (Session::EffectKind::delay);
    int accepted = 0, received = 0; bool coherent = true;
    const auto calls = allocationAudit::measure ([&]
    {
        for (int i = 0; i < 64; ++i) { value.values[0] = (float) (i + 1); if (queue.push (value)) ++accepted; }
        Session::EffectSettings out;
        while (queue.pop (out)) { coherent = coherent && out.kind == value.kind && out.values[0] == ++received; }
    });
    noHeapCalls (calls, "native effects FIFO saturation and drain allocate/free nothing");
    requireRealtime (accepted == 63 && received == 63 && coherent, "native effects FIFO preserves bounded complete packets");
    EffectParameterQueue threaded; std::atomic<bool> abort { false };
    constexpr int count = 10000;
    std::thread producer ([&]
    {
        auto packet = defaultEffectSettings (Session::EffectKind::equalizer);
        for (int i = 0; i < count && ! abort.load();)
        { for (size_t j = 0; j < packet.values.size(); ++j) packet.values[j] = (float)(i * 34 + (int)j); if (threaded.push (packet)) ++i; }
    });
    received = 0; coherent = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (received < count && coherent && std::chrono::steady_clock::now() < deadline)
    {
        Session::EffectSettings out;
        if (! threaded.pop (out)) { std::this_thread::yield(); continue; }
        for (size_t j = 0; j < out.values.size(); ++j) coherent = coherent && out.values[j] == (float)(received*34+(int)j);
        ++received;
    }
    abort.store (true); producer.join();
    requireRealtime (received == count && coherent, "native effect FIFO delivers 10000 intact 34-value packets between threads");
}

int runRealtimeTests()
{
    try
    {
        const auto calibration = allocationAudit::measure ([]
        {
            auto* memory = ::operator new (32);
            ::operator delete (memory);
        });
        requireRealtime (calibration.allocations == 1 && calibration.deallocations == 1,
                         "allocation guard detects an explicit new/delete control case");

        SubtractiveSynthDSP synth;
        synth.prepare (48000);
        Session::SynthSettings settings;
        std::array<float, 1024> left {}, right {};
        constexpr std::array<int, 8> sizes { 0, 1, 3, 64, 127, 256, 511, 1024 };
        bool finite = true;
        const auto processing = allocationAudit::measure ([&]
        {
            for (int pass = 0; pass < 32; ++pass)
            {
                settings.waveform = pass % 4;
                settings.pulseWidth = float (pass % 3) * 0.5f;
                settings.cutoffHz = 100.0f + pass * 400.0f;
                synth.setSettings (settings);
                synth.pitchWheel (1, pass % 2 == 0 ? 0 : 16383);
                synth.pitchWheel (16, 4096);
                for (int note = 0; note < 24; ++note) synth.noteOn (1, 48 + note, 0.7f);
                synth.noteOn (16, 69, 0.7f);
                synth.controlChange (1, 64, 127);
                for (const int samples : sizes)
                {
                    synth.pitchWheel (1, samples * 15);
                    left.fill (0); right.fill (0);
                    synth.render (left.data(), right.data(), samples);
                    for (int sample = 0; sample < samples; ++sample)
                        finite = finite && std::isfinite (left[static_cast<std::size_t> (sample)])
                                        && std::isfinite (right[static_cast<std::size_t> (sample)]);
                }
                synth.controlChange (1, 123, 0);
                for (int note = 0; note < 24; ++note) synth.noteOff (1, 48 + note);
                synth.pitchWheel (1, 16383);
                synth.controlChange (1, 121, 0);
                synth.noteOff (16, 69);
                synth.pitchWheel (16, 0);
                synth.controlChange (1, 64, 0);
                synth.render (left.data(), nullptr, 1024);
                synth.controlChange (1, 120, 0);
                synth.reset();
            }
        });
        noHeapCalls (processing, "prepared owned synth processing has zero intercepted C++ new/delete calls");
        requireRealtime (finite && synth.activeVoices() == 0, "variable blocks and voice exhaustion remain finite and reset clears voices");

        SynthParameterQueue queue;
        bool overflowRejected = false, ordered = true;
        int accepted = 0, consumed = 0;
        const auto queueCalls = allocationAudit::measure ([&]
        {
            for (int i = 0; i < 64; ++i)
                if (queue.push (packet (i))) ++accepted;
                else overflowRejected = true;
            Session::SynthSettings next;
            while (queue.pop (next))
            {
                ordered = ordered && next.cutoffHz == packet (consumed).cutoffHz;
                ++consumed;
                synth.setSettings (next);
            }
        });
        noHeapCalls (queueCalls, "settings queue fill, overflow, drain, and application have zero intercepted new/delete calls");
        requireRealtime (accepted == 63 && consumed == 63 && ordered && overflowRejected,
                         "bounded settings queue rejects overflow and retains packet order");

        constexpr int packetCount = 10000;
        SynthParameterQueue concurrent;
        std::atomic<bool> abort { false };
        std::thread producer ([&]
        {
            for (int i = 0; i < packetCount && ! abort.load (std::memory_order_relaxed);)
                if (concurrent.push (packet (i))) ++i;
                else std::this_thread::yield();
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
        allocationAudit::Counts consumerCalls {};
        int received = 0;
        bool coherent = true;
        while (received < packetCount && coherent && std::chrono::steady_clock::now() < deadline)
        {
            Session::SynthSettings next;
            bool popped = false;
            const auto calls = allocationAudit::measure ([&] { popped = concurrent.pop (next); });
            consumerCalls.allocations += calls.allocations;
            consumerCalls.deallocations += calls.deallocations;
            if (! popped) { std::this_thread::yield(); continue; }
            const auto expected = packet (received);
            coherent = next.cutoffHz == expected.cutoffHz && next.resonance == expected.resonance && next.waveform == expected.waveform && next.pulseWidth == expected.pulseWidth;
            ++received;
        }
        abort.store (true, std::memory_order_relaxed);
        producer.join();
        noHeapCalls (consumerCalls, "concurrent settings-queue consumer has zero intercepted C++ new/delete calls");
        requireRealtime (coherent && received == packetCount, "SPSC queue delivers 10000 complete ordered packets across threads");
        utilityRealtimeTests();
        nativeEffectsRealtimeTests();
        std::cout << "Owned DSP allocation checks passed; malloc, locking, syscalls, external code and deadlines are not certified.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL realtime audit: " << error.what() << '\n';
        return 1;
    }
}
}
