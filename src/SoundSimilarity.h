#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace halftrack::similarity
{
// What a sound is like, from its first ten seconds: how long and loud it is, how fast it starts
// and dies away, where its energy sits in the spectrum and how noisy it is. Halftrack's own
// measure, not the reference's. Every value is finite; similar sounds lie close together once
// each value is put on the library's own scale.
struct Features
{
    static constexpr int bandCount = 16;
    static constexpr int count = 11 + bandCount;
    std::array<float, count> values {};
    static const char* name (int index);
};

// Reads a file and measures it; nothing when it cannot be read or holds no audio.
std::optional<Features> analyse (const juce::File&, juce::AudioFormatManager&);
// Measures samples already in memory, mixed to mono (the analysis of a file reads into this).
std::optional<Features> analyse (const juce::AudioBuffer<float>&, double sampleRate);

// A library's sounds and their features, with each value's spread across the library, so that
// sounds can be compared on equal terms whatever a value's units.
class Library
{
public:
    void add (const juce::File&, const Features&);
    int size() const { return (int) sounds.size(); }
    // The sounds nearest a sound, nearest first, without the sound itself.
    struct Match { juce::File file; float distance = 0; };
    std::vector<Match> similarTo (const Features&, int limit, const juce::File& excluding = {}) const;
    std::vector<Match> similarTo (const juce::File&, int limit) const;

private:
    std::vector<std::pair<juce::File, Features>> sounds;
    mutable bool scaled = false;
    mutable std::array<float, Features::count> mean {}, spread {};
    void scale() const;
};

// The sounds under a folder, analysed on a worker thread so the browser can show similar sounds
// while the rest are still being measured. The worker shares only its own state with the index,
// so a read blocked in the filesystem can outlive the browser. Measurements are kept by path,
// size and modification time, so asking again about the same folder measures only what changed.
class FolderIndex
{
public:
    static constexpr int maximumSounds = 5000;
    ~FolderIndex();
    void start (const juce::File& folder);
    void cancel();
    struct Status { bool analysing = false; int analysed = 0, found = 0, unreadable = 0; };
    Status status() const;
    juce::File folder() const;
    // The sounds measured so far that are nearest a file, measuring the file itself if need be.
    std::vector<Library::Match> similarTo (const juce::File&, int limit) const;

private:
    struct State;
    std::shared_ptr<State> state;
};
}
