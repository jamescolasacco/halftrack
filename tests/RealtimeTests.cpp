#include "SubtractiveSynth.h"
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
    result.waveform = sequence % 2;
    return result;
}
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
                settings.waveform = pass % 2;
                settings.cutoffHz = 100.0f + pass * 400.0f;
                synth.setSettings (settings);
                for (int note = 0; note < 24; ++note) synth.noteOn (1, 48 + note, 0.7f);
                synth.controlChange (1, 64, 127);
                for (const int samples : sizes)
                {
                    left.fill (0); right.fill (0);
                    synth.render (left.data(), right.data(), samples);
                    for (int sample = 0; sample < samples; ++sample)
                        finite = finite && std::isfinite (left[static_cast<std::size_t> (sample)])
                                        && std::isfinite (right[static_cast<std::size_t> (sample)]);
                }
                for (int note = 0; note < 24; ++note) synth.noteOff (1, 48 + note);
                synth.controlChange (1, 64, 0);
                synth.render (left.data(), nullptr, 1024);
                synth.controlChange (1, 123, 0);
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
            coherent = next.cutoffHz == expected.cutoffHz && next.resonance == expected.resonance && next.waveform == expected.waveform;
            ++received;
        }
        abort.store (true, std::memory_order_relaxed);
        producer.join();
        noHeapCalls (consumerCalls, "concurrent settings-queue consumer has zero intercepted C++ new/delete calls");
        requireRealtime (coherent && received == packetCount, "SPSC queue delivers 10000 complete ordered packets across threads");
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
