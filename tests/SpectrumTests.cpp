#include "NativeEffects.h"
#include "Session.h"
#include "Spectrum.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// The Spectrum device. Tones centred on bins read their exact levels: a full-scale sine is 0 dB,
// one at a tenth of full scale -20 dB. Then, in a project, a stereo file with different tones on
// its left and right reads per channel and as their mean, at each block size, while the device
// passes audio unchanged.
namespace halftrack
{
namespace
{
constexpr double pi = 3.14159265358979323846, rate = 48000;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS spectrum: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
std::vector<float> tones (int count, std::initializer_list<std::pair<double, double>> parts)
{
    std::vector<float> x ((size_t) count, 0.0f);
    for (int n = 0; n < count; ++n) for (const auto& [hz, amplitude] : parts) x[(size_t) n] += (float) (amplitude * std::sin (2 * pi * hz * n / rate));
    return x;
}
double binHz (int block, int bin) { return bin * rate / block; }
void write (const juce::File& f, double left, double right)
{
    juce::AudioBuffer<float> x (2, 2 * 48000);
    for (int n = 0; n < x.getNumSamples(); ++n)
    { x.setSample (0, n, (float) (.5 * std::sin (2 * pi * left * n / rate))); x.setSample (1, n, (float) (.5 * std::sin (2 * pi * right * n / rate))); }
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32, 0), "render " + file.getFileName());
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
juce::Component* findNamed (juce::Component& parent, const juce::String& name)
{
    if (parent.getName() == name) return &parent;
    for (auto* child : parent.getChildren()) if (auto* found = findNamed (*child, name)) return found;
    return nullptr;
}
}

int runSpectrumTests (const juce::File& artifacts)
{
    try
    {
        // Analysis.
        {
            const auto full = spectrum::analyse (tones (4096, { { binHz (4096, 50), 1.0 } }).data(), 4096);
            check (full.size() == 2049 && std::abs (full[50]) < .01 && full[49] < full[50] && full[51] < full[50] && full[300] < -120,
                   "a full-scale sine on bin 50 reads 0 dB there, with nothing far away");
            const auto tenth = spectrum::analyse (tones (4096, { { binHz (4096, 50), .1 } }).data(), 4096);
            check (std::abs (tenth[50] + 20) < .01, "a tenth of full scale reads -20 dB");
            const auto pair = spectrum::analyse (tones (4096, { { binHz (4096, 50), .5 }, { binHz (4096, 200), .25 } }).data(), 4096);
            check (std::abs (pair[50] + 6.0206) < .01 && std::abs (pair[200] + 12.0412) < .01 && pair[125] < -120, "two tones are resolved at their levels");
            bool sizes = true;
            for (const int block : spectrum::blockSizes)
            {
                const auto reading = spectrum::analyse (tones (block, { { binHz (block, 10), 1.0 } }).data(), block);
                sizes = sizes && (int) reading.size() == block / 2 + 1 && std::abs (reading[10]) < .01;
            }
            std::vector<float> silence (4096, 0.0f);
            check (sizes && spectrum::analyse (silence.data(), 3000).empty() && spectrum::analyse (silence.data(), 4096)[10] == spectrum::floorDb,
                   "every block size from 1024 to 16384 reads exactly, other sizes read nothing, and silence reads the floor");
        }

        // The device in a project.
        const auto root = artifacts.getChildFile ("spectrum-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("stereo.wav");
        const double left = binHz (4096, 50), right = binHz (4096, 120);
        write (source, left, right);
        Session s (false);
        ok (s.importAudio (source, 0, 0), "import a stereo file with different tones left and right");
        const auto without = render (s, root.getChildFile ("without.wav"));
        ok (s.addNativeEffect (0, Session::EffectKind::spectrum), "add a Spectrum");
        const auto id = s.snapshot().tracks[0].plugins.back().id;
        check (! s.spectrum (id) && std::string (effectCategory (Session::EffectKind::spectrum)) == "Utilities" && effectIsAnalysis (Session::EffectKind::spectrum),
               "a Utilities analysis device that has processed nothing reads nothing");
        const auto with = render (s, root.getChildFile ("with.wav"));
        bool same = with.getNumSamples() == without.getNumSamples();
        for (int c = 0; same && c < 2; ++c) for (int n = 0; same && n < with.getNumSamples(); ++n) same = with.getSample (c, n) == without.getSample (c, n);
        check (same, "the Spectrum passes audio through unchanged");
        auto reading = s.spectrum (id);
        // The render's last block ends partly past the clip, so levels are compared within a reading.
        check (reading && std::abs (reading->binHz - rate / 4096) < 1e-9 && reading->decibels.size() == 2049
               && std::abs (reading->decibels[50] - reading->decibels[120]) < .05 && reading->decibels[50] > -13 && reading->decibels[50] < -12,
               "L+R reads both tones alike, at about half their level");
        const auto mean = reading->decibels[50];
        auto settings = s.snapshot().tracks[0].plugins.back().effect;
        settings.values[1] = 0; ok (s.setNativeEffectSettings (id, settings), "Channel L");
        reading = s.spectrum (id);
        check (reading && std::abs (reading->decibels[50] - mean - 6.0206) < .05 && reading->decibels[120] < reading->decibels[50] - 40,
               "the left channel holds its tone at twice the mean's level, the right tone over 40 dB down");
        settings.values[1] = 1; ok (s.setNativeEffectSettings (id, settings), "Channel R");
        reading = s.spectrum (id);
        check (reading && std::abs (reading->decibels[120] - mean - 6.0206) < .05 && reading->decibels[50] < reading->decibels[120] - 40, "and the right channel the reverse");
        const auto loudest = [] (const std::vector<float>& bins) { return (int) (std::max_element (bins.begin() + 1, bins.end()) - bins.begin()); };
        settings.values[0] = 0; ok (s.setNativeEffectSettings (id, settings), "Block 1024");
        reading = s.spectrum (id);
        check (reading && reading->decibels.size() == 513 && std::abs (reading->binHz - 46.875) < 1e-9 && loudest (reading->decibels) == 30, "a 1024 block has 513 bins 46.875 Hz apart, the tone on bin 30");
        settings.values[0] = 4; ok (s.setNativeEffectSettings (id, settings), "Block 16384");
        reading = s.spectrum (id);
        check (reading && reading->decibels.size() == 8193 && loudest (reading->decibels) == 480, "a 16384 block finds it on bin 480, at four times the resolution");
        check (! s.spectrum ("missing") && ! s.spectrum (s.snapshot().tracks[0].id), "unknown ids read nothing");
        settings.values[0] = 2; settings.values[1] = 0; ok (s.setNativeEffectSettings (id, settings), "back to 4096 on the left channel");
        {
            auto editor = makeNativeEffectEditor (s, id, true, {});
            auto* peak = dynamic_cast<juce::Label*> (findNamed (*editor, "Spectrum peak"));
            check (peak != nullptr && peak->getText().startsWith ("586 Hz"), "the inline Spectrum names the loudest bin, 586 Hz");
            if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
                if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
                {
                    juce::PNGImageFormat png; const auto output = directory.getChildFile ("spectrum-inline.png");
                    if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                        && png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), *stream))
                        std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
                }
        }
        std::cout << "Spectrum passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL spectrum: " << e.what() << std::endl; return 1; }
}
}
