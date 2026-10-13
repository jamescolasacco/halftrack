#include "Session.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace halftrack
{
namespace
{
void check (bool condition, const juce::String& text)
{
    if (! condition) throw std::runtime_error (text.toStdString());
    std::cout << "PASS latency: " << text << std::endl;
}
void ok (const juce::Result& result, const juce::String& text)
{ check (result.wasOk(), text + (result.failed() ? ": " + result.getErrorMessage() : "")); }
juce::AudioBuffer<float> read (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader && reader->sampleRate == 48000 && reader->numChannels == 2 && reader->lengthInSamples == 48000,
           "render retains exactly 48000 stereo frames");
    juce::AudioBuffer<float> audio (2, 48000);
    check (reader->read (&audio, 0, 48000, 0, true, true), "decode complete render");
    return audio;
}
void verifyFixture (Session& session, const juce::PluginDescription& description)
{
    juce::String error;
    auto processor = session.pluginFormats().createPluginInstance (description, 48000, 511, error);
    check (processor != nullptr, "instantiate latency oracle: " + error);
    processor->setPlayConfigDetails (2, 2, 48000, 511);
    processor->setNonRealtime (true);
    processor->prepareToPlay (48000, 511);
    check (processor->getLatencySamples() == 127, "fixture reports its actual 127-sample delay");
    constexpr int count = 4096;
    juce::AudioBuffer<float> stimulus (2, count);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < count; ++sample)
            stimulus.setSample (channel, sample, static_cast<float> ((sample * 37 + channel * 13) % 97 - 48) / 512.0f);
    const std::array<int, 4> blockSizes { 1, 63, 256, 511 };
    int position = 0, block = 0;
    float errorMaximum = 0;
    while (position < count)
    {
        const int size = std::min (blockSizes[static_cast<size_t> (block++ % 4)], count - position);
        juce::AudioBuffer<float> audio (2, size);
        for (int channel = 0; channel < 2; ++channel)
            audio.copyFrom (channel, 0, stimulus, channel, position, size);
        juce::MidiBuffer midi;
        processor->processBlock (audio, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < size; ++sample)
            {
                const int source = position + sample - 127;
                const float expected = source < 0 ? 0.0f : stimulus.getSample (channel, source);
                errorMaximum = std::max (errorMaximum, std::abs (audio.getSample (channel, sample) - expected));
            }
        position += size;
    }
    check (errorMaximum == 0, "actual VST3 fixture delays both channels exactly across variable block sizes");
    processor->reset();
    juce::AudioBuffer<float> silence (2, 256); silence.clear();
    juce::MidiBuffer midi;
    processor->processBlock (silence, midi);
    check (silence.getMagnitude (0, 256) == 0, "reset removes all retained delay state");
    processor->releaseResources();
}
}

int runLatencyTests (const juce::File& destination)
{
    try
    {
        const auto* configured = std::getenv ("HALFTRACK_TEST_LATENCY_PLUGIN");
        check (configured != nullptr, "nonzero-latency VST3 fixture path is supplied");
        const auto plugin = juce::File (configured);
        check (plugin.exists(), "latency fixture exists");
        const auto root = destination.getChildFile ("latency-" + juce::Uuid().toString());
        ok (root.createDirectory(), "create isolated latency artifacts");
        Session session (false);
        juce::OwnedArray<juce::PluginDescription> descriptions;
        for (auto* format : session.pluginFormats().getFormats())
            if (format->getName() == "VST3") format->findAllTypesForFile (descriptions, plugin.getFullPathName());
        check (descriptions.size() == 1, "scan exactly one controlled latency plug-in");
        verifyFixture (session, *descriptions[0]);

        juce::AudioBuffer<float> reference (2, 48000); reference.clear();
        for (int frame : { 4097, 8192, 16383, 24000, 40000 })
            for (int offset = 0; offset < 17; ++offset)
            {
                reference.setSample (0, frame + offset, (offset % 2 ? -1.0f : 1.0f) * 0.125f);
                reference.setSample (1, frame + offset, (offset % 3 ? 1.0f : -1.0f) * 0.0625f);
            }
        const auto source = root.getChildFile ("latency-source.wav");
        {
            juce::WavAudioFormat format;
            std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
            check (writer && writer->writeFromAudioSampleBuffer (reference, 0, 48000), "write independent impulse/burst fixture");
        }
        reference = read (source);
        ok (session.importAudio (source, 0), "import delay compensation stimulus");
        auto render = [&] (float expectedGain, const juce::String& name)
        {
            const auto output = root.getChildFile (name + ".wav");
            ok (session.exportWav (output), "render " + name);
            const auto actual = read (output);
            float maximum = 0;
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 48000; ++sample)
                    maximum = std::max (maximum, std::abs (actual.getSample (channel, sample)
                        - reference.getSample (channel, sample) * expectedGain));
            check (maximum < 0.000002f, name + " complete-window PCM alignment, max error " + juce::String (maximum, 9));
        };
        render (1, "dry-reference");
        ok (session.addPlugin (0, *descriptions[0]), "insert actual 127-sample latency plug-in");
        render (1, "one-delayed-track");
        ok (session.importAudio (source, 1), "add parallel dry track");
        render (2, "parallel-dry-and-delayed");
        ok (session.addPlugin (0, *descriptions[0]), "cascade second latency plug-in");
        render (2, "parallel-dry-and-254-sample-chain");
        const auto project = root.getChildFile ("Compensated.halftrack");
        ok (session.saveProject (project), "save latency graph");
        ok (session.loadProject (project), "reopen latency graph");
        render (2, "reopened-latency-chain");
        ok (session.setPluginEnabled (0, 0, false), "bypass first delayed processor");
        render (2, "bypassed-delay-in-chain");
        ok (session.removePlugin (0, 1), "remove second delayed processor");
        render (2, "removed-delay-with-bypassed-first");
        std::cout << "Nonzero-latency acceptance passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL latency: " << error.what() << std::endl;
        return 1;
    }
}
}
