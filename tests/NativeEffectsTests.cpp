#include "NativeEffects.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace halftrack
{
namespace
{
using Kind = Session::EffectKind;
using Settings = Session::EffectSettings;
constexpr double pi = 3.14159265358979323846;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS native effects: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": "+r.getErrorMessage() : "")); }
juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& source, const Settings& s, double rate = 48000)
{
    auto out = source; NativeEffectDSP dsp; dsp.prepare (rate, s.kind); dsp.configure (s);
    dsp.processSamples (out.getWritePointer (0), out.getWritePointer (1), out.getNumSamples()); return out;
}
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1e10;
    double worst = 0;
    for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n)
    { if (! std::isfinite (a.getSample(c,n)) || ! std::isfinite (b.getSample(c,n))) return 1e10; worst = std::max (worst, std::abs ((double)a.getSample(c,n)-b.getSample(c,n))); }
    return worst;
}
juce::AudioBuffer<float> signal (int frames)
{
    juce::AudioBuffer<float> a (2, frames);
    for (int n = 0; n < frames; ++n) { a.setSample(0,n,(float)((n*37)%113-56)/256); a.setSample(1,n,(float)((n*17)%97-48)/256); }
    return a;
}
void equalizerTests()
{
    const auto input = signal (4096); auto s = defaultEffectSettings (Kind::equalizer);
    check (difference (run(input,s),input) == 0, "eight flat bands preserve every sample exactly");
    for (int band = 0; band < 8; ++band) for (float gain : { -18.0f, -6.0f, 6.0f, 18.0f })
    {
        s = defaultEffectSettings (Kind::equalizer); s.values[4*band+1] = 1000; s.values[4*band+2] = gain;
        juce::AudioBuffer<float> sine (2, 48000);
        for (int n = 0; n < sine.getNumSamples(); ++n) { sine.setSample(0,n,(float)(.01*std::sin(2*pi*1000*n/48000))); sine.setSample(1,n,sine.getSample(0,n)*-.5f); }
        const auto out = run (sine,s); double error = 0;
        for (int c = 0; c < 2; ++c) for (int n = 12000; n < 48000; ++n)
            error = std::max (error,std::abs(out.getSample(c,n)-sine.getSample(c,n)*std::pow(10.0,gain/20)));
        check (error < 1e-7, "bell center gain, stereo isolation, band "+juce::String(band+1)+" / "+juce::String(gain)+" dB");
    }
    for (int type = 2; type <= 7; ++type)
    {
        s = defaultEffectSettings (Kind::equalizer); s.values[0] = (float)type; s.values[1] = 1000; s.values[2] = 12;
        juce::AudioBuffer<float> probe (2, 48000);
        for (int n=0;n<48000;++n) { probe.setSample(0,n,.125f); probe.setSample(1,n,(n%2 ? -.125f : .125f)); }
        const auto out=run(probe,s);
        const double dc = type==2 ? std::pow(10.0,.6) : (type==4 || type==7 ? 0 : 1);
        const double nyquist = type==3 ? std::pow(10.0,.6) : (type==5 || type==7 ? 0 : 1);
        double error=0;for(int n=40000;n<48000;++n) { error=std::max(error,std::abs(out.getSample(0,n)-.125*dc));error=std::max(error,std::abs(out.getSample(1,n)-probe.getSample(1,n)*nyquist)); }
        check(error<1e-7,"independent DC/Nyquist limits for filter type "+juce::String(type));
    }
    for (int type : {4,5,6,7})
    {
        s=defaultEffectSettings(Kind::equalizer);s.values[0]=(float)type;s.values[1]=1000;
        juce::AudioBuffer<float> probe(2,48000);for(int n=0;n<48000;++n)for(int c=0;c<2;++c)probe.setSample(c,n,(float)(.01*std::sin(2*pi*1000*n/48000)));
        auto out=run(probe,s);double power=0;for(int n=24000;n<48000;++n)power+=out.getSample(0,n)*out.getSample(0,n);
        const auto amplitude=std::sqrt(2*power/24000)/.01;
        const double target=type==6 ? 0 : type==7 ? 1 : std::sqrt(.5);
        check(std::abs(amplitude-target)<1e-6,"cutoff/notch/bandpass center magnitude "+juce::String(type));
    }
}
void compressorTests()
{
    for (int detector=0;detector<2;++detector) for(float ratio : {1.0f,2.0f,4.0f,20.0f})
    {
        auto s=defaultEffectSettings(Kind::compressor);s.values[0]=-24;s.values[1]=ratio;s.values[2]=3;s.values[3]=80;s.values[4]=0;s.values[7]=(float)detector;
        juce::AudioBuffer<float> a(2,24000);for(int n=0;n<24000;++n){a.setSample(0,n,.5f);a.setSample(1,n,-.125f);}
        auto out=run(a,s);double error=0;
        for(int n=0;n<24000;++n)
        {
            const double fraction=-std::expm1(-(n+1)/(48000*.003));
            const auto level=.5*(detector ? std::sqrt(fraction) : fraction);
            const auto over=std::max(0.0,20*std::log10(level)+24);
            const auto gain=std::pow(10.0,over*(1/ratio-1)/20);
            error=std::max(error,std::abs(out.getSample(0,n)-.5*gain));error=std::max(error,std::abs(out.getSample(1,n)+.125*gain));
        }
        check(error<4e-8,"closed-form attack and linked stereo gain for detector/ratio "+juce::String(detector)+"/"+juce::String(ratio));
    }
    for(float db : {-36.0f,-24.0f,-21.0f,-18.0f,-15.0f,-12.0f,-6.0f})
    {
        auto s=defaultEffectSettings(Kind::compressor);s.values[0]=-18;s.values[1]=4;s.values[4]=12;
        const auto amplitude=std::pow(10.0,db/20);juce::AudioBuffer<float>a(2,48000);for(int c=0;c<2;++c)juce::FloatVectorOperations::fill(a.getWritePointer(c),(float)amplitude,48000);
        const auto out=run(a,s);const double x=db+18;const double reduction=x<=-6?0:x>=6?-.75*x:-.75*(x+6)*(x+6)/24;
        check(std::abs(out.getSample(0,47999)-amplitude*std::pow(10.0,reduction/20))<1e-7,"soft-knee transfer at "+juce::String(db)+" dBFS");
    }
}
void delayTests()
{
    for(double rate : {8000.0,44100.0,48000.0,96000.0})
    {
        auto s=defaultEffectSettings(Kind::delay);s.values[0]=10;s.values[1]=20;s.values[2]=0;s.values[3]=1;
        const int frames=(int)(rate*.05);juce::AudioBuffer<float>a(2,frames);a.clear();a.setSample(0,0,.5f);a.setSample(1,0,-.25f);
        const auto out=run(a,s,rate);bool exact=true;
        for(int n=0;n<frames;++n){exact=exact&&std::abs(out.getSample(0,n)-(n==(int)std::lround(rate*.01)?.5f:0))<1e-7f;exact=exact&&std::abs(out.getSample(1,n)-(n==(int)std::lround(rate*.02)?-.25f:0))<1e-7f;}
        check(exact,"independent left/right impulse timing at "+juce::String(rate));
    }
    auto s=defaultEffectSettings(Kind::delay);s.values[0]=s.values[1]=10;s.values[2]=.5f;s.values[3]=1;s.values[4]=1;
    juce::AudioBuffer<float>a(2,1800);a.clear();a.setSample(0,0,1);const auto out=run(a,s);
    const auto tone=std::exp(-2*pi*12000/48000.0);
    check(std::abs(out.getSample(0,480)-.5)<1e-7 && out.getSample(1,480)==0,"ping pong injects mono into first side");
    check(std::abs(out.getSample(1,960)-.25*(1-tone))<1e-7 && out.getSample(0,960)==0,"first feedback crosses channels with measured filter response");
    s.values[4]=0;s.values[0]=10.01f;s.values[2]=0;const auto fractional=run(a,s);const double t=s.values[0]*48;
    check(std::abs(fractional.getSample(0,(int)t)-(1-(t-std::floor(t)))) < 1e-6,"fractional delay distributes impulse between adjacent samples");
    s.values[0]=s.values[1]=10;s.values[6]=1;check(run(a,s).getMagnitude(0,1800)==0,"freeze rejects fresh input into an empty buffer");
}
void syncedDelayTests()
{
    constexpr std::array<double,11> beats { .25, 1.0/3, .375, .5, 2.0/3, .75, 1, 1.5, 2, 3, 4 };
    for (float bpm : {20.0f, 60.0f, 123.0f, 300.0f, 999.0f})
        for (int division=0;division<11;++division)
        {
            auto settings=defaultEffectSettings(Kind::delay);settings.values[2]=0;settings.values[3]=1;
            settings.values[8]=1;settings.values[9]=(float)division;settings.values[10]=(float)(10-division);settings.tempoBpm=bpm;
            const double times[] { beats[(size_t)division]*8000*60/bpm, beats[(size_t)(10-division)]*8000*60/bpm };
            const int count=(int)std::ceil(std::max(times[0],times[1]))+3;
            juce::AudioBuffer<float> impulse(2,count);impulse.clear();impulse.setSample(0,0,.5f);impulse.setSample(1,0,-.25f);
            const auto output=run(impulse,settings,8000);double error=0;
            for(int c=0;c<2;++c)for(int n=0;n<count;++n)
            {
                const auto fraction=times[c]-std::floor(times[c]);
                const auto weight=n==(int)times[c]?1-fraction:n==(int)times[c]+1?fraction:0;
                error=std::max(error,std::abs(output.getSample(c,n)-(c==0?.5:-.25)*weight));
            }
            check(error<1e-6,"tempo division impulse at "+juce::String(bpm)+" BPM / division "+juce::String(division));
        }
}
void reverbTests()
{
    auto s=defaultEffectSettings(Kind::reverb);s.values[0]=0;s.values[5]=1;
    juce::AudioBuffer<float>a(2,8000);a.clear();a.setSample(0,0,1);auto out=run(a,s,8000);
    const int first=238;
    bool silent=true;for(int n=0;n<first;++n)silent=silent&&out.getSample(0,n)==0&&out.getSample(1,n)==0;
    check(silent && std::abs(out.getSample(0,first)-.0625)<1e-8 && std::abs(out.getSample(1,first)-.0625)<1e-8,"independent first room reflection timing and stereo weights");
    const double loss=std::pow(10.0,-3.0*first/(8000*2));const double tone=std::exp(-2*pi*.45);
    check(std::abs(out.getSample(0,2*first)-.046875*(1-tone)*loss)<1e-7,"first recirculation follows orthogonal feedback and T60 damping");
    s.values[0]=25;auto delayed=run(a,s,8000);bool shifted=true;for(int n=0;n<7800;++n)for(int c=0;c<2;++c)shifted=shifted&&std::abs(delayed.getSample(c,n+200)-out.getSample(c,n))<1e-7;
    check(shifted,"predelay shifts every reflection by the requested samples");
    s.values[0]=0;s.values[4]=0;auto mono=run(a,s,8000);bool equal=true;for(int n=0;n<8000;++n)equal=equal&&mono.getSample(0,n)==mono.getSample(1,n);check(equal,"zero room width is exactly mono");
    s.values[4]=1;s.values[1]=.1f;auto shortTail=run(a,s,8000);
    check(shortTail.getRMSLevel(0,4000,4000)<out.getRMSLevel(0,4000,4000)*.01f,"short decay measurably reduces late energy");
}
void bitcrusherTests()
{
    // Hand-calculated signed four-bit codes, including ties, silence and saturation.
    constexpr std::array<float, 21> input { -1.25f, -1, -.9375f, -.875f, -.8125f, -.75f,
        -.1875f, -.125f, -.0625f, -.01f, 0, .01f, .0625f, .125f, .1875f,
        .75f, .8125f, .875f, .9375f, 1, 1.25f };
    constexpr std::array<float, 21> quantized { -1, -1, -1, -.875f, -.875f, -.75f,
        -.25f, -.125f, -.125f, 0, 0, 0, .125f, .125f, .25f,
        .75f, .875f, .875f, .875f, .875f, .875f };
    juce::AudioBuffer<float> source (2, 1021);
    for (int n = 0; n < source.getNumSamples(); ++n)
    { source.setSample (0, n, input[n % input.size()]); source.setSample (1, n, input[input.size()-1-n % input.size()]); }
    for (const double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 384000.0 })
        for (const int interval : { 1, 2, 3, 4, 64 })
        {
            auto settings = defaultEffectSettings (Kind::bitcrusher); settings.values[0] = 4; settings.values[1] = (float) interval;
            const auto out = run (source, settings, rate); bool exact = true;
            for (int n = 0; n < out.getNumSamples(); ++n)
            {
                const auto index = (n / interval * interval) % input.size();
                exact = exact && out.getSample (0, n) == quantized[index] && out.getSample (1, n) == quantized[input.size()-1-index];
            }
            check (exact, "known four-bit stereo codes and held frames at " + juce::String (rate) + " Hz / " + juce::String (interval) + " samples");
        }
    auto settings = defaultEffectSettings (Kind::bitcrusher); settings.values[0] = 4; settings.values[1] = 2.5f;
    const auto fractional = run (source, settings);
    constexpr int captured[] { 0, 0, 0, 3, 3, 5, 5, 5, 8, 8, 10, 10, 10, 13, 13, 15 };
    for (int n = 0; n < (int) std::size (captured); ++n)
        check (fractional.getSample (0, n) == quantized[captured[n]], "fractional sample hold keeps its continuous clock");
    juce::AudioBuffer<float> constant (2, 1024);
    juce::FloatVectorOperations::fill (constant.getWritePointer (0), -.5f, 1024);
    juce::FloatVectorOperations::fill (constant.getWritePointer (1), .5f, 1024);
    settings.values[2] = 20; settings.values[3] = -20; settings.values[4] = .25f;
    const auto driven = run (constant, settings); bool mixed = true;
    for (int n = 0; n < 1024; ++n)
        mixed = mixed && std::abs (driven.getSample (0, n) + .4) < 2e-8 && std::abs (driven.getSample (1, n) - .396875) < 2e-8;
    check (mixed, "drive precedes clipping and output gain applies only to the wet path");
    for (const float bits : { 1.0f, 2.0f, 8.0f, 16.0f, 24.0f })
    {
        settings = defaultEffectSettings (Kind::bitcrusher); settings.values[0] = bits; settings.values[1] = 1;
        juce::AudioBuffer<float> probe (2, 128); probe.clear(); probe.setSample (0, 0, -1); probe.setSample (1, 0, 1);
        const auto out = run (probe, settings);
        check (out.getSample (0, 0) == -1 && out.getSample (1, 0) == 1-std::exp2 (1-bits)
            && out.getMagnitude (1, 127) == 0, "signed bit-depth endpoints and exact digital silence at " + juce::String (bits) + " bits");
    }
    settings = defaultEffectSettings (Kind::bitcrusher); settings.values[0] = 4; settings.values[1] = 3;
    NativeEffectDSP dsp; dsp.prepare (48000, settings.kind); dsp.configure (settings);
    auto mono = source; dsp.processSamples (mono.getWritePointer (0), nullptr, mono.getNumSamples());
    const auto stereo = run (source, settings);
    check (juce::FloatVectorOperations::findMinAndMax (mono.getReadPointer (0), mono.getNumSamples()).getStart() == -1,
        "mono processing retains its negative endpoint");
    bool same = true; for (int n = 0; n < mono.getNumSamples(); ++n) same = same && mono.getSample (0, n) == stereo.getSample (0, n);
    check (same, "mono callback produces the same independent left-channel samples");
}
void gateTests()
{
    auto settings = defaultEffectSettings (Kind::gate);
    settings.values[0] = -12; settings.values[1] = 6;
    settings.values[2] = .5f; settings.values[3] = .25f; settings.values[4] = .5f;
    juce::AudioBuffer<float> source (2, 18);
    constexpr std::array<double, 18> opening { 0, 0, 0, 0, .25, .5, .75, 1, 1, 1, 1, 1, 1, 1, .75, .5, .25, 0 };
    for (int n = 0; n < 18; ++n)
    { source.setSample (0, n, n < 4 ? .2f : n < 8 ? .5f : n < 12 ? .2f : .05f); source.setSample (1, n, -.025f); }
    const auto output = run (source, settings, 8000); double error = 0;
    for (int c = 0; c < 2; ++c) for (int n = 0; n < 18; ++n)
        error = std::max (error, std::abs (output.getSample (c, n) - source.getSample (c, n) * opening[n]));
    check (error < 2e-8, "hand-calculated gate attack, hysteresis, exact hold frames and release apply identically to both channels");
    source.setSample (0, 15, .5f); const auto retriggered = run (source, settings, 8000);
    check (retriggered.getSample (0, 15) == .5f && retriggered.getSample (1, 15) == -.025f,
        "reopening during release continues from current gain without restarting the envelope");
    for (const double rate : { 8000.0, 44100.0, 48000.0, 96000.0 })
    {
        settings = defaultEffectSettings (Kind::gate); settings.values[0] = -12; settings.values[2] = 10;
        settings.values[3] = 0; settings.values[4] = 20;
        const int attackFrames = (int) std::ceil (rate * .01), releaseFrames = (int) std::ceil (rate * .02);
        juce::AudioBuffer<float> step (2, attackFrames + releaseFrames + 3);
        for (int n = 0; n < step.getNumSamples(); ++n)
        { step.setSample (0, n, .01f); step.setSample (1, n, n < attackFrames ? -.5f : -.01f); }
        const auto out = run (step, settings, rate); error = 0;
        for (int n = 0; n < out.getNumSamples(); ++n)
        {
            const double gain = n < attackFrames ? (n+1.0)/attackFrames : std::max (0.0, 1-(n-attackFrames+1.0)/releaseFrames);
            for (int c = 0; c < 2; ++c) error = std::max (error, std::abs (out.getSample (c, n) - step.getSample (c, n) * gain));
        }
        check (error < 4e-8, "independent closed-form envelope and right-channel detection at " + juce::String (rate) + " Hz");
    }
    settings = defaultEffectSettings (Kind::gate); settings.values[0] = -12;
    settings.values[2] = settings.values[3] = settings.values[4] = 0;
    settings.values[5] = -20; settings.values[7] = .25f;
    juce::AudioBuffer<float> alternating (2, 100);
    for (int n = 0; n < 100; ++n) { alternating.setSample (0, n, n % 2 ? .5f : .1f); alternating.setSample (1, n, -.01f); }
    for (const bool duck : { false, true })
    {
        settings.values[6] = duck ? 1.0f : 0.0f; const auto out = run (alternating, settings); error = 0;
        for (int n = 0; n < 100; ++n)
        {
            const bool open = n % 2 != 0; const double gain = .75 + .25 * ((open != duck) ? 1 : .1);
            for (int c = 0; c < 2; ++c) error = std::max (error, std::abs (out.getSample (c, n) - alternating.getSample (c, n) * gain));
        }
        check (error < 2e-8, "zero-time gate/duck switching retains the chosen floor and parallel dry path");
    }
}
void limiterTests()
{
    for (const double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 384000.0 })
    {
        auto settings = defaultEffectSettings (Kind::limiter); settings.values[0] = 20; settings.values[1] = -6; settings.values[2] = 25;
        const int latency = (int) std::ceil (rate * .001); const int count = latency * 5 + 211;
        auto source = signal (count); // Independent oracle scans each complete future window.
        for (int n = count/2; n < count; ++n) { source.setSample (0, n, .001f); source.setSample (1, n, -.002f); }
        const auto output = run (source, settings, rate); const double ceiling = std::pow (10.0, -6.0 / 20);
        double previousGain = 1, error = 0; bool belowCeiling = true;
        const double release = std::exp (-1.0 / (rate * .025));
        for (int n = 0; n < count; ++n)
        {
            double peak = 0;
            for (int at = std::max (0, n-latency); at <= n; ++at)
                for (int c = 0; c < 2; ++c) peak = std::max (peak, std::abs (source.getSample (c, at) * 10.0));
            const double gain = std::min (peak > ceiling ? ceiling/peak : 1.0, 1.0+(previousGain-1.0)*release); previousGain = gain;
            for (int c = 0; c < 2; ++c)
            {
                const double expected = n < latency ? 0 : source.getSample (c, n-latency)*10.0*gain;
                error = std::max (error, std::abs (output.getSample (c, n)-expected));
                belowCeiling = belowCeiling && std::abs (output.getSample (c, n)) <= ceiling + 1e-7;
            }
        }
        check (belowCeiling, "every channel/sample remains below the limiter ceiling");
        check (error < 4e-8, "complete independent lookahead-window oracle at " + juce::String (rate) + " Hz");
        settings = defaultEffectSettings (Kind::limiter); settings.values[1] = 0;
        juce::AudioBuffer<float> impulse (2, latency+2); impulse.clear(); impulse.setSample (0, 0, .5f); impulse.setSample (1, 0, -.25f);
        const auto delayed = run (impulse, settings, rate);
        bool exact = true; for (int n = 0; n < delayed.getNumSamples(); ++n)
            exact = exact && delayed.getSample (0, n) == (n == latency ? .5f : 0) && delayed.getSample (1, n) == (n == latency ? -.25f : 0);
        check (exact, "below-ceiling impulse is delayed exactly once with independent stereo amplitudes");
    }
}
juce::AudioBuffer<float> engineReference (const juce::AudioBuffer<float>& input, const Settings& settings)
{
    if (settings.kind != Kind::limiter) return run (input, settings);
    juce::AudioBuffer<float> padded (2, input.getNumSamples()+48); padded.clear();
    for (int c = 0; c < 2; ++c) padded.copyFrom (c, 0, input, c, 0, input.getNumSamples());
    const auto delayed = run (padded, settings);
    juce::AudioBuffer<float> aligned (2, input.getNumSamples());
    for (int c = 0; c < 2; ++c) aligned.copyFrom (c, 0, delayed, c, 48, input.getNumSamples());
    return aligned;
}
void lifecycleTests()
{
    auto input=signal(14000);
    for(int kind=0;kind<=static_cast<int>(Kind::limiter);++kind)
    {
        auto s=defaultEffectSettings(static_cast<Kind>(kind));if(kind==0){s.values[2]=8;s.values[1]=400;}
        const auto expected=run(input,s);auto split=input;NativeEffectDSP dsp;dsp.prepare(48000,s.kind);dsp.configure(s);
        for(int offset=0;offset<split.getNumSamples();){const auto count=std::min(1+offset%311,split.getNumSamples()-offset);dsp.processSamples(split.getWritePointer(0,offset),split.getWritePointer(1,offset),count);offset+=count;}
        check(difference(split,expected)==0,"block partition invariance for "+juce::String(effectName(s.kind)));
        dsp.reset();split=input;dsp.processSamples(split.getWritePointer(0),split.getWritePointer(1),split.getNumSamples());check(difference(split,expected)==0,"reset restores deterministic output");
        auto bad=input;bad.setSample(0,0,std::numeric_limits<float>::quiet_NaN());bad.setSample(1,10,std::numeric_limits<float>::infinity());check(difference(run(bad,s),run(bad,s))==0,"nonfinite input does not poison state");
        if (s.kind != Kind::limiter)
        { s.values[kind==0?33:kind==1?6:kind==2?3:kind==3?5:kind==4?4:7]=0;check(difference(run(input,s),input)==0,"zero wet is exact dry signal"); }
    }
}
void writeAudio(const juce::File& file,const juce::AudioBuffer<float>& audio)
{
    juce::WavAudioFormat format;std::unique_ptr<juce::OutputStream>stream(file.createOutputStream());auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));check(writer&&writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"write generated audio");
}
juce::AudioBuffer<float> readAudio(const juce::File& file, double rate = 48000)
{
    juce::AudioFormatManager f;f.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>r(f.createReaderFor(file));check(r&&r->sampleRate==rate&&r->numChannels==2,"read rendered stereo audio");juce::AudioBuffer<float>a(2,(int)r->lengthInSamples);check(r->read(&a,0,a.getNumSamples(),0,true,true),"decode all output frames");return a;
}
juce::ValueTree effectState(juce::ValueTree root)
{
    if(root.getProperty("type").toString()==NativeEffectPlugin::xmlTypeName)return root;
    for(auto child:root)if(auto found=effectState(child);found.isValid())return found;return {};
}
void projectTests(const juce::File& root)
{
    const auto input=signal(20000);const auto file=root.getChildFile("source.wav");writeAudio(file,input);
    for(int type=0;type<=static_cast<int>(Kind::limiter);++type)
    {
        const auto kind=static_cast<Kind>(type);Session s(false);ok(s.importAudio(file,0),"import effect fixture");ok(s.addNativeEffect(0,kind),"insert "+juce::String(effectName(kind)));
        const auto id=s.snapshot().tracks[0].plugins[0].id;auto settings=defaultEffectSettings(kind);
        const auto project=root.getChildFile("effect-"+juce::String(type)+".halftrack");
        ok(s.saveProject(project),"save default effect");ok(s.loadProject(project),"reopen default effect");
        check(s.snapshot().tracks[0].plugins[0].effect==settings,"default packet survives reopen");
        ok(s.setNativeEffectSettings(id,settings),"unchanged packet accepted");check(!s.canUndo()&&!s.snapshot().dirty,"unchanged packet does not dirty or invent history");
        auto invalid=settings;invalid.values[0]=std::numeric_limits<float>::quiet_NaN();check(s.setNativeEffectSettings(id,invalid).failed()&&!s.canUndo(),"invalid command preserves history");
        const auto fields=effectParameters(kind);
        for(size_t i=0;i<fields.size();++i)settings.values[i]=fields[i].choices?fields[i].maximum:fields[i].minimum+(fields[i].maximum-fields[i].minimum)*.37f;
        ok(s.setNativeEffectSettings(id,settings),"edit every device property");s.undo();check(s.snapshot().tracks[0].plugins[0].effect==defaultEffectSettings(kind),"one undo restores all properties");s.redo();
        ok(s.saveProject(project),"save all properties");ok(s.loadProject(project),"reopen all properties");check(s.snapshot().tracks[0].plugins[0].effect==settings,"every property round trips exactly");
        auto xml=juce::XmlDocument::parse(project);check(xml!=nullptr,"read disposable project XML");auto tree=juce::ValueTree::fromXml(*xml);effectState(tree).setProperty(fields[0].key,"nan",nullptr);const auto bad=root.getChildFile("bad.halftrack");check(tree.createXml()->writeTo(bad),"write malformed effect fixture");
        ok(s.setNativeEffectSettings(id,defaultEffectSettings(kind)),"make redo checkpoint");s.undo();check(s.loadProject(bad).failed()&&s.canRedo()&&s.snapshot().tracks[0].plugins[0].effect==settings,"malformed load retains current edit and redo");
        settings=defaultEffectSettings(kind);if(type==0)settings.values[2]=6;if(type==2){settings.values[0]=10;settings.values[1]=17;}if(kind==Kind::limiter){settings.values[0]=20;settings.values[1]=-6;}
        ok(s.setNativeEffectSettings(id,settings),"configure audible effect");
        const auto expected=engineReference(input,settings);const auto render=root.getChildFile("render-"+juce::String(type)+".wav");ok(s.exportWav(render),"render effect through engine");check(difference(readAudio(render),expected)<2e-6,"every exported sample matches the separately exercised processor");
        if (type == 2 || type == 3)
        {
            juce::AudioBuffer<float> padded (2, input.getNumSamples()+48000); padded.clear();
            for (int c=0;c<2;++c) padded.copyFrom(c,0,input,c,0,input.getNumSamples());
            const auto expectedTail=run(padded,settings);
            ok(s.exportWav(render,48000,24,1),"export full effect tail after clip end");
            const auto actualTail=readAudio(render);
            check(difference(actualTail,expectedTail)<2e-6 && actualTail.getMagnitude(input.getNumSamples(),48000)>1e-6,
                  "delay/reverb tail remains active beyond the last source frame");
        }
        ok(s.setPluginEnabled(0,0,false),"bypass effect");ok(s.exportWav(render),"render bypass");check(difference(readAudio(render),input)<2e-6,"bypass restores every dry sample");s.undo();
        ok(s.removePlugin(0,0),"remove effect");s.undo();ok(s.exportWav(render),"render undo removal");check(difference(readAudio(render),expected)<2e-6,"undo removal restores signal and identity");
    }
    {
        juce::AudioBuffer<float> heldInput (2, 20003);
        juce::FloatVectorOperations::fill (heldInput.getWritePointer (0), .125f, 20003);
        juce::FloatVectorOperations::fill (heldInput.getWritePointer (1), -.25f, 20003);
        const auto heldSource = root.getChildFile ("held-source.wav"); writeAudio (heldSource, heldInput);
        Session held (false); ok (held.importAudio (heldSource, 0), "import odd-length hold fixture");
        ok (held.addNativeEffect (0, Kind::bitcrusher), "insert tail-tested bitcrusher");
        auto settings = defaultEffectSettings (Kind::bitcrusher); settings.values[1] = 64;
        ok (held.setNativeEffectSettings (held.snapshot().tracks[0].plugins[0].id, settings), "hold 64 frames across source boundary");
        const auto output = root.getChildFile ("held-tail.wav"); ok (held.exportWav (output, 48000, 24, .01), "render hold tail beyond final source frame");
        const auto actual = readAudio (output); check (actual.getNumSamples() == 20483, "hold tail retains exact selected export duration");
        double error = 0;
        for (int n = 0; n < actual.getNumSamples(); ++n)
        {
            error = std::max (error, std::abs (actual.getSample (0, n) - (n < 20032 ? .125 : 0)));
            error = std::max (error, std::abs (actual.getSample (1, n) - (n < 20032 ? -.25 : 0)));
        }
        check (error < 2e-6, "sample hold completes its last captured interval, then returns to exact silence");
    }
    for (const int frames : { 1, 7, 48, 49, 64, 257 })
    {
        juce::AudioBuffer<float> edges (2, frames); edges.clear();
        edges.setSample (0, 0, .25f); edges.setSample (1, frames-1, -.125f);
        const auto inputFile = root.getChildFile ("limiter-edges-" + juce::String (frames) + ".wav"); writeAudio (inputFile, edges);
        Session edge (false); ok (edge.importAudio (inputFile, 0), "import edge-latency fixture");
        const auto dryFile = root.getChildFile ("limiter-dry.wav"), limitedFile = root.getChildFile ("limiter-compensated.wav");
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            if (rate != 48000 && frames != 64) continue;
            if (!edge.snapshot().tracks[0].plugins.empty()) ok (edge.removePlugin (0, 0), "remove previous rate's limiter");
            ok (edge.exportWav (dryFile, rate, 24), "render dry edge reference at requested rate");
            ok (edge.addNativeEffect (0, Kind::limiter), "insert actual lookahead device");
            auto settings = defaultEffectSettings (Kind::limiter); settings.values[1] = 0;
            ok (edge.setNativeEffectSettings (edge.snapshot().tracks[0].plugins[0].id, settings), "leave sub-ceiling edge signal unchanged");
            ok (edge.exportWav (limitedFile, rate, 24), "render with native lookahead compensation");
            check (difference (readAudio (limitedFile, rate), readAudio (dryFile, rate)) < 2e-6,
                "native compensation preserves every frame, including sub-lookahead clips and resampled edges");
            if (frames == 7 && rate == 48000)
            {
                ok (edge.addNativeEffect (0, Kind::limiter), "chain two independent lookahead devices");
                ok (edge.exportWav (limitedFile), "render seven-frame clip through 96 samples of native latency");
                check (difference (readAudio (limitedFile), readAudio (dryFile)) < 2e-6, "chained native latency preserves the complete short signal");
            }
        }
    }
    {
        juce::AudioBuffer<float> edges (2, 257); edges.clear(); edges.setSample (0, 0, .125f); edges.setSample (1, 256, -.0625f);
        const auto source = root.getChildFile ("parallel-limiter-source.wav"); writeAudio (source, edges);
        Session parallel (false); ok (parallel.importAudio (source, 0), "import latency branch");
        ok (parallel.importAudio (source, 1), "import independently aligned dry branch");
        const auto dry = root.getChildFile ("parallel-dry.wav"), wet = root.getChildFile ("parallel-limited.wav");
        ok (parallel.exportWav (dry), "render reference parallel sum");
        ok (parallel.addNativeEffect (0, Kind::limiter), "limit only one parallel track");
        ok (parallel.exportWav (wet), "render independently delayed branch");
        check (difference (readAudio (wet), readAudio (dry)) < 2e-6, "native lookahead keeps the dry and processed tracks sample-aligned");
        ok (parallel.addReturnTrack ("Limited return"), "add shared return path");
        const auto returnId = parallel.snapshot().tracks.back().id;
        ok (parallel.setTrackSendGain (0, returnId, -6), "feed delayed source into shared return");
        ok (parallel.exportWav (dry), "render reference source and return mix");
        ok (parallel.addNativeEffect (2, Kind::limiter), "add independent lookahead on return");
        ok (parallel.exportWav (wet), "render serial and parallel return compensation");
        check (difference (readAudio (wet), readAudio (dry)) < 2e-6, "return lookahead preserves the complete source/return mix");
        ok (parallel.setPluginEnabled (0, 0, false), "bypass source limiter while return remains delayed");
        ok (parallel.exportWav (wet), "render changing branch latency");
        check (difference (readAudio (wet), readAudio (dry)) < 2e-6, "bypass recalculates native branch latency without shifting the mix");
    }
    {
        Session synced(false);ok(synced.importAudio(file,0),"import tempo-follow source");ok(synced.addNativeEffect(0,Kind::delay),"add tempo-follow delay");
        auto settings=defaultEffectSettings(Kind::delay);settings.values[2]=0;settings.values[3]=1;settings.values[8]=1;settings.values[9]=settings.values[10]=0;
        const auto id=synced.snapshot().tracks[0].plugins[0].id;ok(synced.setNativeEffectSettings(id,settings),"enable synchronized timing");
        for(float bpm : {120.0f,60.0f,240.0f,300.01f,500.5f,999.0f})
        {
            synced.setTempo(bpm);const auto actualSettings=synced.snapshot().tracks[0].plugins[0].effect;
            check(actualSettings.tempoBpm==bpm,"snapshot uses project tempo without a device tempo property");
            const auto rendered=root.getChildFile("tempo-delay.wav");ok(synced.exportWav(rendered),"export immediately after tempo edit");
            check(difference(readAudio(rendered),run(input,actualSettings))<2e-6,"export refreshes derived tempo before processing");
        }
        const auto project=root.getChildFile("synced-delay.halftrack");ok(synced.saveProject(project),"save synchronized project");ok(synced.loadProject(project),"reopen synchronized project");
        check(synced.snapshot().tracks[0].plugins[0].effect.tempoBpm==999,"reopened delay derives saved project tempo");
        settings.tempoBpm=120;ok(synced.setNativeEffectSettings(id,settings),"stale derived tempo cannot override project tempo");
        check(!synced.snapshot().dirty&&!synced.canUndo(),"unchanged device values with stale derived tempo remain a no-op");
    }
    Session chain(false);ok(chain.importAudio(file,0),"import chain source");ok(chain.addNativeEffect(0,Kind::equalizer),"insert EQ");ok(chain.addNativeEffect(0,Kind::compressor),"insert compressor");
    auto first=chain.snapshot().tracks[0].plugins[0];auto gain=first.effect;gain.values[32]=12;ok(chain.setNativeEffectSettings(first.id,gain),"set pre-compression gain");
    const auto one=root.getChildFile("gain-first.wav"),two=root.getChildFile("gain-last.wav");ok(chain.exportWav(one),"render gain before compressor");ok(chain.movePlugin(0,0,1),"move actual effect order");ok(chain.exportWav(two),"render gain after compressor");check(difference(readAudio(one),readAudio(two))>.01,"order changes nonlinear output");chain.undo();ok(chain.exportWav(two),"undo device order");check(difference(readAudio(one),readAudio(two))==0,"undo order restores bit-identical output");
    Session midi(false);ok(midi.addMidiTrack("Effect instrument"),"add MIDI track");const auto tracks=midi.snapshot().tracks;const int track=(int)tracks.size()-1;ok(midi.addMidiClip(tracks.back().id,0,1),"add MIDI fixture");const auto clip=midi.snapshot().tracks.back().clips[0].id;ok(midi.addMidiNote(clip,60,0,1,.6f),"add generated note");
    for(int k=0;k<=static_cast<int>(Kind::limiter);++k)ok(midi.addNativeEffect(track,static_cast<Kind>(k)),"insert device after instrument");const auto result=root.getChildFile("midi-effects.wav");ok(midi.exportWav(result,48000,24,1),"render instrument through all native devices and tail");auto rendered=readAudio(result);check(rendered.getMagnitude(0,rendered.getNumSamples())>.001,"instrument chain remains audible");
    ok(midi.addReturnTrack("Effect return"),"create return");const int ret=(int)midi.snapshot().tracks.size()-1;for(int k=0;k<=static_cast<int>(Kind::limiter);++k)ok(midi.addNativeEffect(ret,static_cast<Kind>(k)),"insert device after return input");
}
}
int runNativeEffectsTests(const juce::File& artifacts)
{
    try {const auto root=artifacts.getChildFile("effects-"+juce::Uuid().toString());ok(root.createDirectory(),"create fixtures");equalizerTests();compressorTests();delayTests();syncedDelayTests();reverbTests();bitcrusherTests();gateTests();limiterTests();lifecycleTests();projectTests(root);runNativeEffectEditorTests(root);std::cout<<"Native effects passed: "<<root.getFullPathName()<<std::endl;return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL native effects: "<<e.what()<<std::endl;return 1;}
}
}
