#include "UtilityProcessor.h"
#include "UtilityEditor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace halftrack
{
void useHardClipEdges();
namespace
{
using Settings = Session::UtilitySettings;
void check (bool condition, const juce::String& message)
{ if (! condition) throw std::runtime_error (message.toStdString()); std::cout << "PASS utility: " << message << std::endl; }
void ok (const juce::Result& result, const juce::String& message)
{ check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : "")); }
constexpr double pi = 3.14159265358979323846;
juce::AudioBuffer<float> source (int frames)
{
    juce::AudioBuffer<float> result (2, frames);
    for (int i = 0; i < frames; ++i)
    {
        result.setSample (0, i, static_cast<float> ((i * 37 + 9) % 113 - 56) / 1024.0f);
        result.setSample (1, i, static_cast<float> ((i * 17 + 3) % 97 - 48) / 1024.0f);
    }
    return result;
}
// Independent constant matrix, expressed directly in the source channels.
juce::AudioBuffer<float> matrix (const juce::AudioBuffer<float>& input, const Settings& s)
{
    juce::AudioBuffer<float> result (2, input.getNumSamples());
    double m = s.midSideMode ? 1 - std::max (0.0f, s.midSide) : 1;
    double d = s.midSideMode ? 1 + std::min (0.0f, s.midSide) : s.width;
    if (s.channel == Settings::Channel::left || s.channel == Settings::Channel::right) m = d = 1;
    if (s.mono) d = 0;
    const auto common = (m + d) / 2, cross = (m - d) / 2;
    const auto gain = s.mute || s.gainDb <= -100 ? 0.0 : std::pow (10.0, s.gainDb / 20.0);
    const auto powerOffset = std::sin (s.balance * pi * 0.5);
    const auto leftBalance = std::sqrt (1.0 - powerOffset), rightBalance = std::sqrt (1.0 + powerOffset);
    for (int i = 0; i < input.getNumSamples(); ++i)
    {
        double l = input.getSample (0, i) * (s.invertLeft ? -1.0 : 1.0);
        double r = input.getSample (1, i) * (s.invertRight ? -1.0 : 1.0);
        if (s.channel == Settings::Channel::left) r = l;
        if (s.channel == Settings::Channel::right) l = r;
        if (s.channel == Settings::Channel::swap) std::swap (l, r);
        result.setSample (0, i, static_cast<float> ((common * l + cross * r) * gain * leftBalance));
        result.setSample (1, i, static_cast<float> ((cross * l + common * r) * gain * rightBalance));
    }
    return result;
}
float difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) return 1e10f;
    float error = 0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            if (! std::isfinite (a.getSample (c, i)) || ! std::isfinite (b.getSample (c, i))) return 1e10f;
            error = std::max (error, std::abs (a.getSample (c, i) - b.getSample (c, i)));
        }
    return error;
}
juce::AudioBuffer<float> processed (const juce::AudioBuffer<float>& input, const Settings& settings, double rate = 48000)
{
    auto result = input; UtilityDSP dsp; dsp.prepare (rate); dsp.configure (settings);
    dsp.processSamples (result.getWritePointer (0), result.getWritePointer (1), result.getNumSamples()); return result;
}
void signalTests()
{
    const auto input = source (1027);
    float maximum = 0; int cases = 0;
    for (int channel = 0; channel < 4; ++channel)
        for (int inversions = 0; inversions < 4; ++inversions)
            for (int mode = 0; mode < 2; ++mode)
                for (int width = 0; width < 5; ++width)
                    for (int mono = 0; mono < 2; ++mono)
                    {
                        Settings s; s.channel = static_cast<Settings::Channel> (channel);
                        s.invertLeft = (inversions & 1) != 0; s.invertRight = (inversions & 2) != 0;
                        s.midSideMode = mode != 0; s.midSide = width * 0.5f - 1; s.width = static_cast<float> (width);
                        s.mono = mono != 0; s.balance = (width - 2) * 0.5f; s.gainDb = (inversions - 2) * 6.0f;
                        maximum = std::max (maximum, difference (processed (input, s), matrix (input, s))); ++cases;
                    }
    check (cases == 320 && maximum < 0.000001f, "320 independent channel/phase/width/mid-side/mono/balance matrix cases");
    juce::AudioBuffer<float> unity (2, 1); unity.setSample (0, 0, 1); unity.setSample (1, 0, 1);
    double balanceError = 0, powerError = 0;
    for (int position = -20; position <= 20; ++position)
    {
        Settings balance; balance.balance = position / 20.0f;
        const auto out = processed (unity, balance), expected = matrix (unity, balance);
        balanceError = std::max (balanceError, static_cast<double> (difference (out, expected)));
        powerError = std::max (powerError, std::abs (std::pow (out.getSample (0, 0), 2) + std::pow (out.getSample (1, 0), 2) - 2.0));
    }
    check (balanceError < 0.000001 && powerError < 0.000001, "41 balance positions follow independently derived unity-center constant-power gains");
    Settings settings;
    check (difference (processed (input, settings), input) == 0, "default utility is exactly transparent");
    settings.mute = true; auto zero = input; zero.clear();
    check (difference (processed (input, settings), zero) == 0, "mute produces exact silence");
    settings.mute = false; settings.gainDb = -100;
    check (difference (processed (input, settings), zero) == 0, "negative-infinity gain produces exact silence");
    settings.gainDb = 35;
    check (difference (processed (input, settings), matrix (input, settings)) < 0.000001f
        && processed (input, settings).getMagnitude (0, input.getNumSamples()) > 1, "gain headroom is not clipped at full scale");
    settings = {}; settings.bassAudition = true;
    check (difference (processed (input, settings), input) == 0, "bass audition is inactive while bass mono is off");

    juce::AudioBuffer<float> impulse (2, 32); impulse.clear(); impulse.setSample (0, 0, 1); impulse.setSample (1, 0, 1);
    settings = {}; settings.bassMono = settings.bassAudition = true;
    const auto bass = processed (impulse, settings);
    constexpr std::array<double, 16> golden { 4.7695178891211787e-07, 2.8467270051224994e-06, 8.4804823875154967e-06, 1.7751463176368688e-05, 3.0542623703929269e-05, 4.673878605188676e-05, 6.6226625028401335e-05, 8.8894653145629344e-05, 0.00011463320559798815, 0.00014333442524145176, 0.00017489224757421656, 0.00020920238571912257, 0.00024616231540826046, 0.00028567125997023689, 0.00032763017532061277, 0.00037194173495606848 };
    double error = 0;
    for (size_t i = 0; i < golden.size(); ++i)
        for (int c = 0; c < 2; ++c) error = std::max (error, std::abs (bass.getSample (c, (int) i) - golden[i]));
    check (error < 0.000000001, "bass audition matches independent impulse-response golden samples");
    impulse.setSample (1, 0, -1); settings.bassAudition = false;
    const auto side = processed (impulse, settings); error = 0;
    constexpr std::array<double, 16> highGolden { 0.98441460436788586, -0.030925979016599658, -0.030438292524008315, -0.029954482018551776, -0.029474547445924423, -0.028998488709176456, -0.028526305655291004, -0.0280579980622041, -0.027593565626260363, -0.027133007950097279, -0.026676324530951059, -0.026223514749377085, -0.025774577858378056, -0.025329512972932931, -0.024888319059919908, -0.024450994928426659 };
    for (size_t i = 0; i < golden.size(); ++i)
    {
        const auto expected = highGolden[i];
        error = std::max (error, std::abs (side.getSample (0, (int) i) - expected));
        error = std::max (error, std::abs (side.getSample (1, (int) i) + expected));
    }
    check (error < 0.00000004, "bass mono matches the independent third-order high-pass impulse");
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0 })
    {
        juce::AudioBuffer<float> dc (2, 10000); for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (dc.getWritePointer (c), c == 0 ? 0.25f : -0.125f, dc.getNumSamples());
        settings = {}; settings.dcFilter = true; auto actual = processed (dc, settings, rate);
        const auto g = std::tan (pi * 5 / rate), pole = (1 - g) / (1 + g), scale = (1 + pole) / 2; error = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < dc.getNumSamples(); ++i)
            error = std::max (error, std::abs (actual.getSample (c, i) - (c == 0 ? 0.25 : -0.125) * scale * scale * (i == 0 ? 1.0 : std::pow (pole, i - 1) * ((i + 1) * pole - i))));
        check (error < 0.00000003, "two-pole DC step follows its closed-form response at " + juce::String (rate));
    }
    // Known Butterworth magnitudes, independently expressed by filter order.
    for (const int frequency : { 20, 120, 480, 2000 })
    {
        constexpr int frames = 96000;
        juce::AudioBuffer<float> tone (2, frames);
        for (int i = 0; i < frames; ++i)
        {
            const auto sample = static_cast<float> (0.125 * std::sin (2 * pi * frequency * i / 48000));
            tone.setSample (0, i, sample); tone.setSample (1, i, -sample);
        }
        Settings bassSettings; bassSettings.bassMono = true;
        const auto high = processed (tone, bassSettings);
        for (int i = 0; i < frames; ++i) tone.setSample (1, i, tone.getSample (0, i));
        bassSettings.bassAudition = true; const auto low = processed (tone, bassSettings);
        const auto ratio = std::tan (pi * frequency / 48000) / std::tan (pi * 120 / 48000);
        const auto expectedLow = 1.0 / std::sqrt (1.0 + std::pow (ratio, 6));
        const auto expectedHigh = std::pow (ratio, 3) * expectedLow;
        double highPower = 0, lowPower = 0;
        for (int i = 48000; i < frames; ++i)
        { highPower += std::pow (high.getSample (0, i), 2); lowPower += std::pow (low.getSample (0, i), 2); }
        check (std::abs (std::sqrt (highPower / 24000) / 0.125 - expectedHigh) < 0.000002
            && std::abs (std::sqrt (lowPower / 24000) / 0.125 - expectedLow) < 0.000002,
            "third-order bass high/low magnitudes match the independent order law at " + juce::String (frequency) + " Hz");
        const auto mid = processed (tone, Settings { .bassMono = true });
        check (difference (mid, tone) == 0, "bass mono preserves the complete mid signal at " + juce::String (frequency) + " Hz");
    }
    Settings filtered; filtered.width = 2.5f; filtered.bassMono = true; filtered.dcFilter = true; filtered.bassFrequency = 250;
    auto whole = input, partitioned = input; UtilityDSP a, b; a.prepare (48000); b.prepare (48000); a.configure (filtered); b.configure (filtered);
    a.processSamples (whole.getWritePointer (0), whole.getWritePointer (1), whole.getNumSamples());
    for (int position = 0; position < partitioned.getNumSamples();)
    {
        const auto count = std::min (1 + position % 127, partitioned.getNumSamples() - position);
        b.processSamples (partitioned.getWritePointer (0, position), partitioned.getWritePointer (1, position), count); position += count;
    }
    check (difference (whole, partitioned) == 0, "filter state is bit-identical across arbitrary block partitions");
    b.reset(); auto reset = input; b.processSamples (reset.getWritePointer (0), reset.getWritePointer (1), reset.getNumSamples());
    check (difference (reset, whole) == 0, "reset restarts filters without losing settings");
    std::array<float, 512> l, r; l.fill (0.25f); r.fill (0.25f); UtilityDSP ramp; ramp.prepare (48000); ramp.configure ({}); ramp.processSamples (l.data(), r.data(), 1);
    settings = {}; settings.gainDb = static_cast<float> (20 * std::log10 (2)); ramp.configure (settings);
    l.fill (0.25f); r.fill (0.25f); ramp.processSamples (l.data(), r.data(), 512);
    const auto target = 0.25 * std::pow (10.0, settings.gainDb / 20.0); error = 0;
    for (size_t i = 0; i < l.size(); ++i) error = std::max (error, std::abs (l[i] - (0.25 + (target - 0.25) * std::min (1.0, (i + 1) / 240.0))));
    check (error < 0.00000004, "gain changes use an exact five-millisecond ramp without overshoot");
    // A mono switch must not leave the old side-channel low-pass ringing.
    UtilityDSP monoTransition; monoTransition.prepare (48000);
    settings = {}; settings.bassMono = true; monoTransition.configure (settings);
    l.fill (0.25f); r.fill (-0.25f); monoTransition.processSamples (l.data(), r.data(), 512);
    settings.mono = true; monoTransition.configure (settings);
    l.fill (0.25f); r.fill (-0.25f); monoTransition.processSamples (l.data(), r.data(), 512);
    bool silentSide = true;
    for (size_t i = 239; i < l.size(); ++i) silentSide = silentSide && l[i] == 0 && r[i] == 0;
    check (silentSide, "mono reaches exact silence for side-only input despite prior bass-filter history");
    // Muting follows the DC blocker so a stored DC tail cannot escape mute.
    UtilityDSP mutedDC; mutedDC.prepare (48000); settings = {}; settings.dcFilter = true; mutedDC.configure (settings);
    l.fill (0.25f); r.fill (0.125f); mutedDC.processSamples (l.data(), r.data(), 512);
    settings.mute = true; mutedDC.configure (settings); l.fill (0); r.fill (0); mutedDC.processSamples (l.data(), r.data(), 512);
    bool silentTail = true;
    for (size_t i = 239; i < l.size(); ++i) silentTail = silentTail && l[i] == 0 && r[i] == 0;
    check (silentTail, "mute silences an existing DC-filter tail after its bounded ramp");
    auto broken = input; broken.setSample (0, 0, std::numeric_limits<float>::quiet_NaN()); broken.setSample (1, 1, std::numeric_limits<float>::infinity());
    check (difference (processed (broken, filtered), processed (broken, filtered)) == 0, "non-finite source samples do not poison filter state");
}

void writeAudio (const juce::File& file, const juce::AudioBuffer<float>& audio)
{
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    check (writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "write generated source fixture");
}
juce::AudioBuffer<float> readAudio (const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader && reader->sampleRate == 48000 && reader->numChannels == 2, "rendered output has expected stereo format");
    juce::AudioBuffer<float> result (2, static_cast<int> (reader->lengthInSamples)); check (reader->read (&result, 0, result.getNumSamples(), 0, true, true), "read complete rendered output"); return result;
}
void render (Session& s, const juce::File& root, const char* name, const juce::AudioBuffer<float>& expected)
{
    const auto file = root.getChildFile (juce::String (name) + ".wav"); ok (s.exportWav (file), "export " + juce::String (name));
    const auto error = difference (readAudio (file), expected); check (error < 0.000002f, juce::String (name) + " matches every sample including active clip edges; max " + juce::String (error, 9));
}
juce::ValueTree utilityState (juce::ValueTree root)
{
    if (root.getProperty ("type").toString() == UtilityPlugin::xmlTypeName) return root;
    for (auto child : root) if (auto found = utilityState (child); found.isValid()) return found;
    return {};
}
void integrationTests (const juce::File& root)
{
    const auto input = source (4096); const auto sourceFile = root.getChildFile ("source.wav"); writeAudio (sourceFile, input);
    Session s (false); ok (s.importAudio (sourceFile, 0), "import source"); ok (s.addUtility (0), "insert utility");
    const auto id = s.snapshot().tracks[0].plugins[0].id;
    check (s.snapshot().tracks[0].plugins[0].isUtility, "snapshot identifies the built-in device");
    render (s, root, "transparent", input);
    Settings settings; settings.gainDb = -3; settings.width = 0.4f; settings.invertRight = true; settings.balance = -0.2f;
    ok (s.setUtilitySettings (id, settings), "edit utility signal path"); render (s, root, "matrix", matrix (input, settings));
    s.undo(); render (s, root, "undo-matrix", input); s.redo(); render (s, root, "redo-matrix", matrix (input, settings));
    ok (s.setPluginEnabled (0, 0, false), "bypass built-in utility"); render (s, root, "bypass", input);
    s.undo(); render (s, root, "undo-bypass", matrix (input, settings));
    const auto project = root.getChildFile ("Utility.halftrack"); ok (s.saveProject (project), "save utility project"); ok (s.loadProject (project), "reopen utility project");
    check (s.snapshot().tracks[0].plugins[0].id == id && s.snapshot().tracks[0].plugins[0].utility == settings, "stable identity and all edited properties reopen");
    render (s, root, "reopened-matrix", matrix (input, settings));
    check (! s.canUndo() && ! s.snapshot().dirty, "loaded utility project begins clean");
    ok (s.setUtilitySettings (id, settings), "unchanged settings are accepted");
    check (! s.canUndo() && ! s.snapshot().dirty, "unchanged settings do not invent an undo transaction");
    for (int variant = 0; variant < 7; ++variant)
    {
        auto invalid = settings;
        switch (variant) { case 0: invalid.width = 4.1f; break; case 1: invalid.gainDb = std::numeric_limits<float>::quiet_NaN(); break;
            case 2: invalid.balance = 1.1f; break; case 3: invalid.midSide = -1.1f; break; case 4: invalid.bassFrequency = 49; break;
            case 5: invalid.channel = static_cast<Settings::Channel> (4); break; case 6: invalid.gainDb = 36; break; }
        check (s.setUtilitySettings (id, invalid).failed() && ! s.canUndo() && ! s.snapshot().dirty && s.snapshot().tracks[0].plugins[0].utility == settings,
               "invalid utility command preserves values, clean state and history");
    }
    Settings all; all.gainDb = -6; all.balance = 0.25f; all.width = 3; all.midSide = -0.6f; all.bassFrequency = 333; all.channel = Settings::Channel::swap;
    all.midSideMode = all.invertLeft = all.invertRight = all.mono = all.bassMono = all.bassAudition = all.mute = all.dcFilter = true;
    ok (s.setUtilitySettings (id, all), "edit every owned setting"); s.undo(); check (s.snapshot().tracks[0].plugins[0].utility == settings, "one undo restores every setting"); s.redo();
    const auto fullProject = root.getChildFile ("All-settings.halftrack"); ok (s.saveProject (fullProject), "save every setting"); ok (s.loadProject (fullProject), "reopen every setting");
    check (s.snapshot().tracks[0].plugins[0].utility == all, "all fourteen controls round trip");
    ok (s.setUtilitySettings (id, settings), "create an undo/redo baseline"); s.undo();
    for (const auto& pair : { std::pair { "width", "nan" }, { "gainDb", "inf" }, { "bassFrequency", "501" }, { "channel", "0.5" }, { "mono", "2" }, { "balance", "garbage" } })
    {
        auto xml = juce::XmlDocument::parse (fullProject); check (xml != nullptr, "read disposable project XML"); auto tree = juce::ValueTree::fromXml (*xml);
        utilityState (tree).setProperty (pair.first, pair.second, nullptr); const auto invalid = root.getChildFile ("Invalid.halftrack"); check (tree.createXml()->writeTo (invalid), "write malformed fixture");
        check (s.loadProject (invalid).failed() && s.snapshot().tracks[0].plugins[0].utility == all && s.canRedo(), "invalid load preserves current project and redo");
    }
    Settings phase; phase.invertLeft = true; ok (s.setUtilitySettings (id, phase), "first chain device inverts left"); ok (s.addUtility (0), "append second utility before mixer");
    const auto second = s.snapshot().tracks[0].plugins[1].id; Settings leftOnly; leftOnly.channel = Settings::Channel::left; ok (s.setUtilitySettings (second, leftOnly), "second chain device selects left");
    render (s, root, "phase-then-left", matrix (matrix (input, phase), leftOnly));
    ok (s.movePlugin (0, 0, 1), "reorder actual device chain"); check (s.snapshot().tracks[0].plugins[0].id == second, "snapshot follows new signal order");
    render (s, root, "left-then-phase", matrix (matrix (input, leftOnly), phase)); s.undo(); render (s, root, "undo-order", matrix (matrix (input, phase), leftOnly)); s.redo();
    ok (s.saveProject (project), "save reordered chain"); ok (s.loadProject (project), "reopen reordered chain"); render (s, root, "reopened-order", matrix (matrix (input, leftOnly), phase));
    ok (s.removePlugin (0, 0), "remove utility"); render (s, root, "removed-left", matrix (input, phase)); s.undo(); render (s, root, "undo-remove", matrix (matrix (input, leftOnly), phase));

    Session returned (false); ok (returned.importAudio (sourceFile, 0), "import return-routing source"); ok (returned.addReturnTrack ("Utility return"), "add return");
    const auto tracks = returned.snapshot().tracks; const auto destination = tracks.back().id; const auto returnIndex = static_cast<int> (tracks.size()) - 1;
    ok (returned.setReturnSendMode (destination, Session::SendMode::pre), "set pre-fader return tap"); ok (returned.setTrackSendGain (0, destination, 0), "send source to return"); ok (returned.setTrackMute (0, true), "mute dry source");
    ok (returned.addUtility (returnIndex), "insert utility after return input"); Settings half; half.gainDb = static_cast<float> (20 * std::log10 (0.5));
    ok (returned.setUtilitySettings (returned.snapshot().tracks[(size_t) returnIndex].plugins[0].id, half), "set return utility gain"); render (returned, root, "return-utility", matrix (input, half));
    const auto returnProject = root.getChildFile ("Return.halftrack"); ok (returned.saveProject (returnProject), "save utility on return"); ok (returned.loadProject (returnProject), "reopen utility on return"); render (returned, root, "reopened-return", matrix (input, half));

    Session midi (false); ok (midi.addMidiTrack ("Utility instrument"), "create MIDI source"); auto midiTracks = midi.snapshot().tracks; const auto midiIndex = static_cast<int> (midiTracks.size()) - 1; const auto midiID = midiTracks.back().id;
    ok (midi.addMidiClip (midiID, 0, 0.25), "create short note clip"); const auto clipID = midi.snapshot().tracks[(size_t) midiIndex].clips[0].id;
    ok (midi.addMidiNote (clipID, 60, 0, 0.25, 0.5f), "draw utility test note"); const auto dry = root.getChildFile ("midi-dry.wav"); ok (midi.exportWav (dry), "render dry MIDI instrument"); const auto dryAudio = readAudio (dry);
    check (dryAudio.getMagnitude (0, dryAudio.getNumSamples()) > 0.001f, "MIDI fixture audibly non-silent");
    ok (midi.addUtility (midiIndex), "insert utility after the instrument"); ok (midi.setUtilitySettings (midi.snapshot().tracks[(size_t) midiIndex].plugins[0].id, half), "set post-instrument gain"); render (midi, root, "midi-utility", matrix (dryAudio, half));
}
}
int runUtilityTests (const juce::File& artifacts)
{
    try
    {
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
        const auto root = artifacts.getChildFile ("utility-" + juce::Uuid().toString()); ok (root.createDirectory(), "create disposable utility fixtures");
        signalTests(); integrationTests (root); runUtilityEditorTests (root);
        std::cout << "Utility acceptance passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL utility: " << e.what() << std::endl; return 1; }
}
}
