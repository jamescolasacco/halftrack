#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Clip envelopes, rendered from a constant 0.5 clip at 120 BPM (two beats a second). Volume
// modulation scales the fader position, pan and device modulation offset the control, an unlinked
// envelope loops on its own length, and a Session clip's automation sets the control while it plays.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS clip envelopes: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
using Envelope = Session::ClipEnvelope;
using Mode = Session::EnvelopeMode;
struct Render
{
    juce::AudioBuffer<float> x;
    // The mean level of a channel over a tenth of a second around a time.
    float at (double seconds, int channel = 0) const
    {
        const int from = (int) ((seconds - .05) * 48000), to = (int) ((seconds + .05) * 48000);
        double sum = 0; for (int n = from; n < to; ++n) sum += std::abs (x.getSample (channel, n));
        return (float) (sum / (to - from));
    }
};
Render render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr && reader->numChannels == 2, "read the render");
    Render r { juce::AudioBuffer<float> (2, (int) reader->lengthInSamples) };
    reader->read (&r.x, 0, r.x.getNumSamples(), 0, true, true);
    return r;
}
bool near (float a, float b, float tolerance = 2e-3f) { return std::abs (a - b) <= tolerance * std::max (1.0f, std::abs (b)); }
juce::String ratio (float a, float b) { return juce::String (b != 0 ? a / b : 0.0f, 4); }
}

int runClipEnvelopeTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("clip-envelopes-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("half.wav");
        {
            juce::AudioBuffer<float> x (2, 8 * 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, .5f);
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write an 8 s constant 0.5 signal");
        }
        const auto out = root.getChildFile ("out.wav");
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.importAudio (source, 0, 0), "put the signal on track 1 at the start");
        const auto clipId = s.snapshot().tracks[0].clips[0].id;
        const auto dry = render (s, out);
        const float left = dry.at (1), right = dry.at (1, 1);
        check (left > .1f && near (dry.at (3), left), "the clip plays at a steady level");

        // Volume modulation: a step from the whole fader position to half of it at beat 4.
        using Target = Session::AutomationTarget;
        const auto halfFader = juce::Decibels::decibelsToGain (Session::automationProportionToValue (Target::volume, .5f * Session::automationValueToProportion (Target::volume, 0)));
        Envelope volume { { {}, 0 }, Mode::modulation, { { 0, 1 }, { 4, 1 }, { 4.001, .5f }, { 16, .5f } } };
        ok (s.setClipEnvelope (clipId, volume), "modulate the track volume from the clip");
        check (s.snapshot().tracks[0].clips[0].envelopes == std::vector<Envelope> { volume }, "the clip reports its envelope");
        auto r = render (s, out);
        check (near (r.at (1), left) && near (r.at (3), left * halfFader),
               "the level follows it: whole before beat 4, " + ratio (r.at (3), left) + " of it after (half the fader is " + juce::String (halfFader, 4) + ")");
        check (near ((float) s.snapshot().tracks[0].gainDb, 0), "the fader keeps its own level");
        s.undo();
        check (s.snapshot().tracks[0].clips[0].envelopes.empty() && near (render (s, out).at (3), left), "one undo removes the envelope");
        s.redo();
        check (near (render (s, out).at (3), left * halfFader), "redo restores it");
        ok (s.moveClip (0, 0, 2), "move the clip to 2 s");
        r = render (s, out);
        check (near (r.at (3), left) && near (r.at (5), left * halfFader), "a linked envelope moves with its clip");
        s.undo();
        juce::String rightHalf;
        ok (s.splitAudioClip (clipId, 1, rightHalf), "split the clip at 1 s");
        const auto split = s.snapshot().tracks[0].clips;
        check (split.size() == 2 && split[0].envelopes == std::vector<Envelope> { volume } && split[1].envelopes == std::vector<Envelope> { volume },
               "both halves keep the envelope, in content beats");
        check (near (render (s, out).at (3), left * halfFader), "and the step still falls at beat 4");
        ok (s.setClipEnvelope (rightHalf, { { {}, 0 }, Mode::modulation, {} }), "clear the right half's envelope");
        check (s.snapshot().tracks[0].clips[0].envelopes == std::vector<Envelope> { volume } && near (render (s, out).at (3), left), "the left half keeps its own");
        s.undo(); s.undo();
        juce::String copy;
        ok (s.duplicateAudioClip (clipId, copy), "duplicate the clip");
        const auto copied = s.snapshot().tracks[0].clips;
        check (copied.size() == 2 && copied[1].id == copy && copied[1].envelopes == std::vector<Envelope> { volume }, "the duplicate has the envelope");
        r = render (s, out);
        check (near (r.at (copied[1].start + 1), left) && near (r.at (copied[1].start + 3), left * halfFader), "and plays it from its own start");
        s.undo();

        // Modulation is relative to the track's automation.
        const auto trackId = s.snapshot().tracks[0].id;
        ok (s.setTrackAutomation (trackId, Target::volume, { true, { { 0, -6 } } }), "automate the track volume to -6 dB");
        const auto automatedHalf = juce::Decibels::decibelsToGain (Session::automationProportionToValue (Target::volume, .5f * Session::automationValueToProportion (Target::volume, -6)));
        r = render (s, out);
        check (near (r.at (1), left * juce::Decibels::decibelsToGain (-6.0f)) && near (r.at (3), left * automatedHalf),
               "the envelope halves the automated fader (" + ratio (r.at (3), left) + ", expected " + juce::String (automatedHalf, 4) + ")");
        s.undo();

        // Pan modulation: a single point holds its value, here a full swing right.
        ok (s.setClipEnvelope (clipId, { { {}, 1 }, Mode::modulation, { { 0, 1 } } }), "modulate the pan fully right");
        r = render (s, out);
        check (r.at (1) < 1e-3f && r.at (1, 1) > right, "the left channel falls silent and the right rises");
        ok (s.setClipEnvelope (clipId, { { {}, 1 }, Mode::modulation, {} }), "clear the pan envelope");
        check (s.snapshot().tracks[0].clips[0].envelopes.size() == 1 && near (render (s, out).at (1), left), "clearing it leaves the volume envelope only");

        // Unlinked envelopes loop on their own length: two beats whole, two beats at a quarter.
        const auto quarterFader = juce::Decibels::decibelsToGain (Session::automationProportionToValue (Target::volume, .25f * Session::automationValueToProportion (Target::volume, 0)));
        Envelope looped { { {}, 0 }, Mode::modulation, { { 0, 1 }, { 2, 1 }, { 2.001, .25f }, { 4, .25f } }, true, 4 };
        ok (s.setClipEnvelope (clipId, looped), "an unlinked four-beat envelope");
        r = render (s, out);
        check (near (r.at (.5), left) && near (r.at (1.5), left * quarterFader) && near (r.at (2.5), left) && near (r.at (3.5), left * quarterFader),
               "it repeats every four beats");
        looped.unlinked = false;
        ok (s.setClipEnvelope (clipId, looped), "link it to the clip");
        check (near (render (s, out).at (2.5), left * quarterFader), "linked, it holds its last value after beat 4");
        ok (s.setClipEnvelope (clipId, { { {}, 0 }, Mode::modulation, {} }), "clear the volume envelope");
        check (s.snapshot().tracks[0].clips[0].envelopes.empty(), "the clip has no envelopes");

        // Refusals change nothing.
        check (s.setClipEnvelope (clipId, { { {}, 0 }, Mode::modulation, { { 0, 1.5f } } }).failed(), "volume modulation above the fader is refused");
        check (s.setClipEnvelope (clipId, { { {}, 1 }, Mode::modulation, { { 1, 0 }, { 1, .5f } } }).failed(), "unordered points are refused");
        check (s.setClipEnvelope (clipId, { { {}, 0 }, Mode::automation, { { 0, -6 } } }).failed(), "automation envelopes are refused on Arrangement clips");
        check (s.setClipEnvelope (clipId, { { "missing", 0 }, Mode::modulation, { { 0, 0 } } }).failed(), "a control off the clip's track is refused");
        check (s.setClipEnvelope (clipId, { { {}, 4 }, Mode::modulation, { { 0, 0 } } }).failed(), "an unknown mixer control is refused");
        check (s.setClipEnvelope (clipId, { { {}, 0 }, Mode::modulation, { { 0, 1 } }, true, .5 }).failed(), "a loop shorter than a beat is refused");
        check (s.setClipEnvelope ("nothing", volume).failed() && s.snapshot().tracks[0].clips[0].envelopes.empty(), "an unknown clip is refused");

        // A native device control: the EQ's Output, moved down by half its range around 0 dB.
        ok (s.addNativeEffect (0, Session::EffectKind::equalizer), "add an EQ");
        const auto eq = s.snapshot().tracks[0].plugins.back().id;
        constexpr int output = 32;
        const auto lowered = juce::Decibels::decibelsToGain (Session::deviceAutomationProportionToValue (Session::EffectKind::equalizer, output,
                                                             Session::deviceAutomationValueToProportion (Session::EffectKind::equalizer, output, 0) - .25f));
        ok (s.setClipEnvelope (clipId, { { eq, output }, Mode::modulation, { { 0, -.5f } } }), "modulate the EQ's Output by half its range down");
        r = render (s, out);
        check (near (r.at (1), left * lowered), "the device plays at " + ratio (r.at (1), left) + " of the level (expected " + juce::String (lowered, 4) + ")");
        check (s.setClipEnvelope (clipId, { { eq, output }, Mode::modulation, { { 0, -1.5f } } }).failed(), "device modulation beyond its range is refused");
        {
            juce::String twin;
            ok (s.duplicateTrack (s.snapshot().tracks[0].id, twin), "duplicate the track");
            const auto state = s.snapshot();
            const auto& copy = state.tracks[1];
            check (copy.id == twin && copy.plugins.size() == 1 && copy.clips.size() == 1 && copy.clips[0].envelopes.size() == 1
                       && copy.clips[0].envelopes[0].target == Session::EnvelopeTarget { copy.plugins[0].id, output }
                       && state.tracks[0].clips[0].envelopes[0].target == Session::EnvelopeTarget { eq, output },
                   "a duplicated track's clip modulates its own EQ");
            s.undo();
        }
        s.undo(); s.undo();
        check (s.snapshot().tracks[0].plugins.empty() && near (render (s, out).at (1), left), "undo removes the envelope and the EQ");

        // An external plug-in's parameter, the controlled VST3 gain, from a half to a quarter.
        const auto* pluginPath = std::getenv ("HALFTRACK_TEST_PLUGIN");
        check (pluginPath != nullptr, "the controlled VST3 plug-in is configured");
        juce::OwnedArray<juce::PluginDescription> descriptions;
        for (auto* format : s.pluginFormats().getFormats())
            if (format->getName() == "VST3") format->findAllTypesForFile (descriptions, juce::File (juce::String (pluginPath)).getFullPathName());
        check (! descriptions.isEmpty(), "scan the controlled VST3 plug-in");
        ok (s.addPlugin (0, *descriptions[0]), "add the gain plug-in"); ok (s.setPluginParameter (0, 0, 0, .5f), "turn it to a half");
        const auto gain = s.snapshot().tracks[0].plugins[0].id;
        check (near (render (s, out).at (1), left * .5f), "the track plays at a half");
        ok (s.setClipEnvelope (clipId, { { gain, 0 }, Mode::modulation, { { 0, -.5f } } }), "modulate its gain down by half its range");
        check (near (render (s, out).at (1), left * .25f), "the track plays at a quarter");
        // A plug-in's own parameter changes are not undo steps.
        s.undo(); s.undo();
        r = render (s, out);
        check (s.snapshot().tracks[0].plugins.empty() && s.snapshot().tracks[0].clips[0].envelopes.empty() && near (r.at (1), left) && near (r.at (3), left),
               "undo removes the envelope and the plug-in");

        // Saved and reopened, and a malformed envelope refused without replacing the open project.
        ok (s.setClipEnvelope (clipId, volume), "modulate the volume again");
        const auto project = root.getChildFile ("Envelopes").getChildFile ("Envelopes.halftrack");
        ok (s.saveProject (project), "save the project");
        {
            Session reopened (false, makeSyntheticTestDeviceType());
            ok (reopened.loadProject (project), "reopen it");
            check (reopened.snapshot().tracks[0].clips[0].envelopes == std::vector<Envelope> { volume }, "the envelope is kept");
            r = render (reopened, out);
            check (near (r.at (1), left) && near (r.at (3), left * halfFader), "and plays the same");
            auto xml = juce::XmlDocument::parse (project);
            check (xml != nullptr, "read the saved project");
            bool changed = false;
            std::function<void (juce::XmlElement&)> visit = [&] (juce::XmlElement& e)
            {
                if (e.hasTagName ("AUTOMATIONCURVE") && e.getStringAttribute ("type") == "scale")
                    if (auto* point = e.getChildByName ("POINT")) { point->setAttribute ("v", 2.0); changed = true; }
                for (auto* child : e.getChildIterator()) visit (*child);
            };
            visit (*xml);
            const auto bad = root.getChildFile ("Bad").getChildFile ("Bad.halftrack");
            check (changed && bad.getParentDirectory().createDirectory().wasOk() && xml->writeTo (bad), "write a project whose volume modulation doubles the fader");
            check (reopened.loadProject (bad).failed() && reopened.snapshot().tracks[0].clips[0].envelopes == std::vector<Envelope> { volume },
                   "it is refused, keeping the open project");
        }
        s.undo();

        // A Session clip's automation sets the control while it plays, against the clip's level
        // played the same way without it.
        ok (s.importAudioToSlot (source, 0, 0), "put the signal in a Session slot");
        const auto slotClip = s.snapshot().tracks[0].slots[0].clip.id;
        const auto live = [&]
        {
            pumpSyntheticTestDevice (s, 8);
            ok (s.launchSlot (0, 0), "launch it");
            // Launches wait for the next bar.
            const auto started = [&] { const auto slot = s.snapshot().tracks[0].slots[0]; return slot.playing && ! slot.queued; };
            for (int i = 0; i < 2000 && ! started(); ++i) pumpSyntheticTestDevice (s, 1);
            check (started(), "it plays");
            pumpSyntheticTestDevice (s, 40);
            const auto played = captureSyntheticTestDevice (s, 100);
            s.stop(); pumpSyntheticTestDevice (s, 8);
            double sum = 0; for (int n = 0; n < played.getNumSamples(); ++n) sum += std::abs (played.getSample (0, n));
            return (float) (sum / played.getNumSamples());
        };
        const auto plain = live();
        check (plain > .1f, "the Session clip plays");
        ok (s.setClipEnvelope (slotClip, { { {}, 0 }, Mode::automation, { { 0, -12 } } }), "automate the track volume to -12 dB from the Session clip");
        const auto automated = live();
        const auto twelveDown = juce::Decibels::decibelsToGain (-12.0f);
        check (near (automated, plain * twelveDown), "it plays at " + ratio (automated, plain) + " of its level (expected " + juce::String (twelveDown, 4) + ")");
        std::cout << "Clip envelopes passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL clip envelopes: " << e.what() << std::endl; return 1; }
}
}
