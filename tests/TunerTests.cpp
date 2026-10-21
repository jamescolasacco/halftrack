#include "NativeEffects.h"
#include "Session.h"
#include "Tuner.h"
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

// The Tuner: pitch detection on generated tones (sines across the range, offsets in cents, other
// reference pitches, a harmonic-rich saw) and refusal of silence, noise and out-of-range pitches;
// then the device in a project, where it passes audio unchanged and reads what it processed, offline
// and from a live input.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
constexpr double pi = 3.14159265358979323846;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS tuner: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
std::vector<float> tone (double frequency, double rate, int frames = tuner::analysisFrames, bool saw = false, double amplitude = .5)
{
    std::vector<float> x ((size_t) frames);
    for (int n = 0; n < frames; ++n)
    {
        const auto phase = frequency * n / rate;
        x[(size_t) n] = (float) (amplitude * (saw ? 2 * (phase - std::floor (phase + .5)) : std::sin (2 * pi * phase)));
    }
    return x;
}
// The cents a frequency lies from its nearest note at the reference.
double expectedCents (double frequency, double reference)
{ const auto semitones = 12 * std::log2 (frequency / reference) + 69; return (semitones - std::round (semitones)) * 100; }
bool reads (const std::vector<float>& x, double rate, double reference, int note, double cents, double tolerance = .5)
{
    const auto r = tuner::detect (x.data(), (int) x.size(), rate, reference);
    return r && r->note == note && std::abs (r->cents - cents) <= tolerance;
}
void write (const juce::File& f, double frequency)
{
    juce::AudioBuffer<float> x (2, 48000);
    for (int n = 0; n < x.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) x.setSample (c, n, (float) (.4 * std::sin (2 * pi * frequency * n / 48000)));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render " + file.getFileName());
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
juce::Component* findNamed (juce::Component& parent, const juce::String& name)
{
    if (parent.getName() == name) return &parent;
    for (auto* child : parent.getChildren()) if (auto* found = findNamed (*child, name)) return found;
    return nullptr;
}
juce::String text (juce::Component& editor, const char* name)
{ auto* label = dynamic_cast<juce::Label*> (findNamed (editor, name)); return label ? label->getText() : "<missing>"; }
}

int runTunerTests (const juce::File& artifacts)
{
    try
    {
        // Detection.
        bool sines = true;
        for (const double f : { 41.2034, 55.0, 82.4069, 110.0, 196.0, 261.626, 440.0, 880.0, 1760.0, 3520.0, 4186.01 })
        {
            const auto semitones = 12 * std::log2 (f / 440) + 69;
            sines = sines && reads (tone (f, 48000), 48000, 440, (int) std::lround (semitones), expectedCents (f, 440));
        }
        check (sines, "sines from E1 to C8 read as their notes within half a cent");
        bool offsets = true;
        for (const double cents : { -49.0, -40.0, -12.5, 0.0, 7.0, 33.0, 49.0 })
            offsets = offsets && reads (tone (440 * std::pow (2.0, cents / 1200), 48000), 48000, 440, 69, cents);
        check (offsets, "offsets from -49 to +49 cents read within half a cent of A4");
        check (reads (tone (432, 48000), 48000, 432, 69, 0) && reads (tone (440, 48000), 48000, 432, 69, 1200 * std::log2 (440.0 / 432)),
               "a 432 Hz reference reads 432 Hz as A4 and 440 Hz as A4 +31.8 cents");
        check (reads (tone (440, 44100), 44100, 440, 69, 0) && reads (tone (440, 96000), 96000, 440, 69, 0), "44.1 and 96 kHz read alike");
        check (reads (tone (110, 48000, tuner::analysisFrames, true), 48000, 440, 45, 0, 1), "a harmonic-rich saw reads as its fundamental, A2");
        {
            std::vector<float> silence ((size_t) tuner::analysisFrames, 0.0f), noise ((size_t) tuner::analysisFrames);
            std::mt19937 random (7); std::uniform_real_distribution<float> uniform (-.5f, .5f);
            for (auto& v : noise) v = uniform (random);
            const auto quiet = tone (440, 48000, tuner::analysisFrames, false, 1e-5);
            check (! tuner::detect (silence.data(), (int) silence.size(), 48000, 440) && ! tuner::detect (noise.data(), (int) noise.size(), 48000, 440)
                   && ! tuner::detect (quiet.data(), (int) quiet.size(), 48000, 440),
                   "silence, white noise and a tone below -80 dBFS read nothing");
            const auto low = tone (20, 48000), high = tone (6000, 48000);
            check (! tuner::detect (low.data(), (int) low.size(), 48000, 440) && ! tuner::detect (high.data(), (int) high.size(), 48000, 440),
                   "20 Hz and 6 kHz are outside the range");
        }
        check (tuner::noteName (69) == "A4" && tuner::noteName (60) == "C4" && tuner::noteName (61, true) == "Db4" && tuner::noteName (21) == "A0"
               && tuner::noteName (108) == "C8", "note names follow MIDI octaves, A4 = 69");

        // The device in a project.
        const auto root = artifacts.getChildFile ("tuner-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("a440.wav"); write (source, 440);
        Session s (false);
        ok (s.importAudio (source, 0, 0), "import a 440 Hz tone");
        const auto without = render (s, root.getChildFile ("without.wav"));
        ok (s.addNativeEffect (0, Session::EffectKind::tuner), "add a Tuner");
        const auto tunerId = s.snapshot().tracks[0].plugins.back().id;
        check (std::string (effectCategory (Session::EffectKind::tuner)) == "Utilities" && effectIsAnalysis (Session::EffectKind::tuner)
               && s.snapshot().tracks[0].plugins.back().effect.values[0] == 440, "the Tuner is a Utilities device with a 440 Hz reference");
        check (! s.tunerReading (tunerId), "a Tuner that has processed nothing reads nothing");
        const auto with = render (s, root.getChildFile ("with.wav"));
        bool same = with.getNumSamples() == without.getNumSamples();
        for (int c = 0; same && c < 2; ++c) for (int n = 0; same && n < with.getNumSamples(); ++n) same = with.getSample (c, n) == without.getSample (c, n);
        check (same, "the Tuner passes audio through unchanged");
        auto reading = s.tunerReading (tunerId);
        check (reading && reading->note == 69 && std::abs (reading->cents) < .5 && std::abs (reading->frequency - 440) < .15,
               "after the render it reads A4 at 440 Hz");
        auto settings = s.snapshot().tracks[0].plugins.back().effect;
        settings.values[0] = 432; ok (s.setNativeEffectSettings (tunerId, settings), "reference 432 Hz");
        reading = s.tunerReading (tunerId);
        check (reading && reading->note == 69 && std::abs (reading->cents - 1200 * std::log2 (440.0 / 432)) < .5, "the reference changes the reading to A4 +31.8 cents");
        settings.values[0] = 400; check (s.setNativeEffectSettings (tunerId, settings).failed(), "references below 410 Hz are refused");
        check (! s.tunerReading ("missing") && ! s.tunerReading (s.snapshot().tracks[0].id), "unknown ids read nothing");
        {
            auto editor = makeNativeEffectEditor (s, tunerId, true, {});
            check (text (*editor, "Tuner note") == "A4" && text (*editor, "Tuner detail") == "+31.8 ct", "the inline Tuner shows A4 and +31.8 ct");
            if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
                if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
                {
                    juce::PNGImageFormat png; const auto output = directory.getChildFile ("tuner-inline.png");
                    if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                        && png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), *stream))
                        std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
                }
            settings.values[0] = 440; settings.values[1] = 1; ok (s.setNativeEffectSettings (tunerId, settings), "show Hz");
        }
        {
            auto editor = makeNativeEffectEditor (s, tunerId, false, {});
            check (text (*editor, "Tuner note") == "A4" && text (*editor, "Tuner detail") == "440.00 Hz", "the Tuner window shows A4 and 440.00 Hz");
        }

        // A live input: the synthetic device's stereo input carries 440 Hz on the left and 660 Hz on the
        // right. The Tuner hears their mono mix, which repeats every 1/220 s, so it reads A3.
        Session live (false, makeSyntheticTestDeviceType());
        const auto inputs = live.inputNames();
        check (! inputs.isEmpty(), "the synthetic device offers an input");
        ok (live.setTrackInput (0, inputs[0]), "route the input to the first track");
        ok (live.setTrackMonitor (0, Session::MonitorMode::on), "monitor it");
        ok (live.addNativeEffect (0, Session::EffectKind::tuner), "add a Tuner to it");
        const auto liveId = live.snapshot().tracks[0].plugins.back().id;
        pumpSyntheticTestDevice (live, 60);
        reading = live.tunerReading (liveId);
        check (reading && reading->note == 57 && std::abs (reading->frequency - 220) < .05, "the live Tuner reads the stopped transport's monitored input, A3 at 220 Hz");
        ok (live.setTrackMonitor (0, Session::MonitorMode::off), "stop monitoring");
        pumpSyntheticTestDevice (live, 60);
        check (! live.tunerReading (liveId), "without input it reads nothing");
        std::cout << "Tuner passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL tuner: " << e.what() << std::endl; return 1; }
}
}
