#include "SoundSimilarity.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

// Sound similarity, evaluated against a library the test generates: eight each of kicks, toms,
// snares, claps, hi-hats, basses, plucks and pads, each a variation drawn from a fixed seed. The
// pairs (kick and tom, snare and clap, pluck between bass and pad) make neighbours hard to tell
// apart. For each sound, the five nearest others should mostly be of its kind. The analysis is deterministic, gives nothing for
// silence or a file it cannot read, and the cost per sound is reported.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS similarity: " << text << std::endl; }
constexpr double rate = 48000;
juce::AudioBuffer<float> sound (const juce::String& kind, juce::Random& random)
{
    const auto between = [&random] (double low, double high) { return low + random.nextDouble() * (high - low); };
    const double seconds = kind == "kick" || kind == "tom" ? between (.6, 1.0) : kind == "snare" || kind == "clap" ? between (.3, .5)
                         : kind == "hat" ? between (.15, .3) : kind == "bass" ? between (1.0, 2.0) : kind == "pluck" ? between (1.0, 1.5) : between (3.0, 4.0);
    juce::AudioBuffer<float> x (1, (int) (seconds * rate));
    const double level = between (.4, .9);
    double phase = 0, low = 0, previous = 0, previousHigh = 0;
    const double decay = kind == "kick" || kind == "tom" ? between (.15, .45) : kind == "snare" || kind == "clap" ? between (.08, .18)
                       : kind == "hat" ? between (.02, .07) : kind == "pluck" ? between (.25, .6) : 0;
    const double tone = kind == "snare" ? between (170, 230) : kind == "bass" ? between (40, 80) : kind == "tom" ? between (90, 220) : kind == "pluck" ? between (200, 600) : 0;
    double band = 0, bandLow = 0;
    const std::array<double, 4> chord { between (200, 300), between (350, 500), between (550, 700), between (750, 900) };
    for (int i = 0; i < x.getNumSamples(); ++i)
    {
        const double t = i / rate;
        double v = 0;
        if (kind == "kick")
        {
            phase += juce::MathConstants<double>::twoPi * (50 + 110 * std::exp (-t / .03)) / rate;
            v = std::sin (phase) * std::exp (-t / decay);
        }
        else if (kind == "tom")
        {
            phase += juce::MathConstants<double>::twoPi * tone * (1 + .3 * std::exp (-t / .05)) / rate;
            v = std::sin (phase) * std::exp (-t / decay);
        }
        else if (kind == "clap")
        {
            // Three short bursts, then a tail, of noise around 1-2 kHz.
            const double noise = random.nextDouble() * 2 - 1;
            bandLow += .3 * (noise - bandLow); band += .05 * (bandLow - band);
            const double bursts = t < .03 ? (std::fmod (t, .01) < .004 ? 1.0 : .2) : std::exp (-(t - .03) / decay);
            v = 3 * (bandLow - band) * bursts;
        }
        else if (kind == "pluck")
        {
            for (int h = 1; h <= 6; ++h) v += std::sin (juce::MathConstants<double>::twoPi * tone * h * t) * std::exp (-t * h / decay) / h;
            v *= .6;
        }
        else if (kind == "snare")
        {
            const double noise = random.nextDouble() * 2 - 1;
            v = (.65 * noise + .35 * std::sin (juce::MathConstants<double>::twoPi * tone * t)) * std::exp (-t / decay);
        }
        else if (kind == "hat")
        {
            // White noise differenced twice leaves its highs.
            const double noise = random.nextDouble() * 2 - 1, high = noise - previous;
            v = (high - previousHigh) * .5 * std::exp (-t / decay);
            previous = noise; previousHigh = high;
        }
        else if (kind == "bass")
        {
            const double saw = 2 * std::fmod (tone * t, 1.0) - 1;
            low += .05 * (saw - low);
            v = low * std::min (1.0, t / .005) * std::min (1.0, (seconds - t) / .05);
        }
        else
        {
            for (const auto f : chord) v += .25 * std::sin (juce::MathConstants<double>::twoPi * f * t);
            v *= std::min (1.0, t / .8) * std::min (1.0, (seconds - t) / .8);
        }
        x.setSample (0, i, (float) (level * v));
    }
    return x;
}
juce::File write (const juce::File& file, const juce::AudioBuffer<float>& x)
{
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (x.getNumChannels()).withBitsPerSample (24));
    if (! writer || ! writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples())) throw std::runtime_error ("could not write " + file.getFullPathName().toStdString());
    return file;
}
}

int runSimilarityTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("similarity-" + juce::Uuid().toString()); check (root.createDirectory().wasOk(), "create fixtures");
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        juce::Random random (0x5eed);
        const juce::StringArray kinds { "kick", "tom", "snare", "clap", "hat", "bass", "pluck", "pad" };
        std::map<juce::String, juce::String> kindOf;
        similarity::Library library;
        double analysing = 0;
        for (const auto& kind : kinds)
            for (int i = 0; i < 8; ++i)
            {
                const auto file = write (root.getChildFile (kind + "-" + juce::String (i) + ".wav"), sound (kind, random));
                const auto start = std::chrono::steady_clock::now();
                const auto features = similarity::analyse (file, formats);
                analysing += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
                check (features.has_value(), "analyse " + file.getFileName());
                library.add (file, *features);
                kindOf[file.getFullPathName()] = kind;
            }
        check (library.size() == 64, "a library of 64 sounds");

        // Relevance: of each sound's five nearest, how many are of its kind.
        double total = 0;
        for (const auto& kind : kinds)
        {
            double precision = 0;
            for (int i = 0; i < 8; ++i)
            {
                const auto file = root.getChildFile (kind + "-" + juce::String (i) + ".wav");
                const auto matches = library.similarTo (file, 5);
                check (matches.size() == 5 && std::none_of (matches.begin(), matches.end(), [&] (const auto& m) { return m.file == file; }), "five matches for " + file.getFileName() + ", not itself");
                int same = 0;
                for (const auto& m : matches) same += kindOf[m.file.getFullPathName()] == kind;
                precision += same / 5.0;
            }
            precision /= 8;
            total += precision;
            check (precision >= 0.5, kind + ": " + juce::String (precision * 100, 0) + "% of the five nearest are of its kind");
        }
        total /= kinds.size();
        check (total >= 0.7, "across the library, " + juce::String (total * 100, 1) + "% of the five nearest are of the same kind");
        std::cout << "SIMILARITY cost: " << juce::String (analysing / 64, 2) << " ms per sound (" << juce::String (analysing, 0) << " ms for 64, sounds 0.15-4 s)" << std::endl;
        check (analysing / 64 < 250, "analysis takes " + juce::String (analysing / 64, 2) + " ms a sound");

        // Determinism, silence, stereo and unreadable files.
        const auto kick = root.getChildFile ("kick-0.wav");
        const auto first = similarity::analyse (kick, formats), second = similarity::analyse (kick, formats);
        check (first && second && first->values == second->values, "the same file measures the same twice");
        for (const auto value : first->values) check (std::isfinite (value), "every value is finite");
        juce::AudioBuffer<float> silence (1, 48000); silence.clear();
        check (! similarity::analyse (write (root.getChildFile ("silence.wav"), silence), formats), "silence has no features");
        check (! similarity::analyse (root.getChildFile ("missing.wav"), formats), "a missing file has none");
        const auto text = root.getChildFile ("notes.wav"); text.replaceWithText ("not audio");
        check (! similarity::analyse (text, formats), "a file that is not audio has none");
        juce::AudioBuffer<float> mono (1, 0), stereo (2, 0);
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (kick));
            mono.setSize (1, (int) reader->lengthInSamples); reader->read (&mono, 0, mono.getNumSamples(), 0, true, false);
            stereo.setSize (2, mono.getNumSamples());
            stereo.copyFrom (0, 0, mono, 0, 0, mono.getNumSamples()); stereo.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());
        }
        const auto asStereo = similarity::analyse (write (root.getChildFile ("kick-stereo.wav"), stereo), formats);
        check (asStereo && library.similarTo (*asStereo, 1).front().file == kick, "the same kick in stereo finds the mono kick nearest");
        std::cout << "Similarity passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL similarity: " << e.what() << std::endl; return 1; }
}

// Manual evaluation against a real library whose subfolders are categories (Kick, Snare, ...):
// up to perCategory sounds from each, chosen by a fixed seed, and for each the share of its five
// nearest that are of its category. Prints the results and writes them as JSON beside the report.
int runSimilarityEvaluation (const juce::File& library, int perCategory, const juce::File& report)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    similarity::Library sounds;
    std::map<juce::String, juce::String> categoryOf;
    juce::StringArray categories;
    double analysing = 0; int analysed = 0, unreadable = 0;
    juce::Random random (0x5eed);
    for (const auto& folder : library.findChildFiles (juce::File::findDirectories, false))
    {
        auto files = folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac");
        files.sort();
        if (files.size() < 6) continue;
        for (int i = files.size() - 1; i > 0; --i) files.swap (i, random.nextInt (i + 1));
        categories.add (folder.getFileName());
        for (int i = 0; i < std::min (perCategory, files.size()); ++i)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto features = similarity::analyse (files[i], formats);
            analysing += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
            if (! features) { ++unreadable; continue; }
            ++analysed;
            sounds.add (files[i], *features);
            categoryOf[files[i].getFullPathName()] = folder.getFileName();
        }
    }
    juce::DynamicObject::Ptr results = new juce::DynamicObject();
    double total = 0; int counted = 0;
    for (const auto& category : categories)
    {
        double precision = 0; int members = 0;
        for (const auto& [path, of] : categoryOf)
        {
            if (of != category) continue;
            int same = 0;
            for (const auto& m : sounds.similarTo (juce::File (path), 5)) same += categoryOf[m.file.getFullPathName()] == category;
            precision += same / 5.0; ++members;
        }
        if (members == 0) continue;
        precision /= members; total += precision * members; counted += members;
        results->setProperty (category, precision);
        std::cout << "EVALUATION " << category << ": " << juce::String (precision * 100, 1) << "% of the five nearest share the category (" << members << " sounds)" << std::endl;
    }
    const double overall = counted > 0 ? total / counted : 0, chance = categories.isEmpty() ? 0 : 1.0 / categories.size();
    std::cout << "EVALUATION overall: " << juce::String (overall * 100, 1) << "% (" << counted << " sounds in " << categories.size() << " categories; about "
              << juce::String (chance * 100, 1) << "% by chance), " << juce::String (analysed > 0 ? analysing / analysed : 0, 2) << " ms a sound, " << unreadable << " unreadable" << std::endl;
    juce::DynamicObject::Ptr summary = new juce::DynamicObject();
    summary->setProperty ("categories", juce::var (results.get())); summary->setProperty ("overall", overall); summary->setProperty ("sounds", counted);
    summary->setProperty ("msPerSound", analysed > 0 ? analysing / analysed : 0); summary->setProperty ("unreadable", unreadable);
    if (report != juce::File()) report.replaceWithText (juce::JSON::toString (juce::var (summary.get())));
    return counted > 0 ? 0 : 1;
}
}
