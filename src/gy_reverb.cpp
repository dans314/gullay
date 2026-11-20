// Gullay: the reverb flavours (see gy_reverb.h).
// Copyright 2026 Otis Kanno. GPL-3.0-or-later.
#ifndef BUFFER_SIZE
#define BUFFER_SIZE 64
#endif
#include "gy_reverb.h"
#include "Plateau/Dattorro.hpp"
#include "Parameters.h"
#include "DSP/ReverbController.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace otiskanno::gullay
{
struct GyReverb::Impl
{
    Dattorro plateau{192000.0, 16.0, 4.0};
    std::unique_ptr<Cloudseed::ReverbController> cloud;
    double sr{48000};
    GyReverbSettings last{};
    bool primed{false}, frozen{false};
    double cloudSent[Cloudseed::Parameter::COUNT];
    float inGain{1.f};
    void cloudSet(int id, double v)
    {
        if (std::fabs(cloudSent[id] - v) > 1e-4)
            cloudSent[id] = v, cloud->SetParameter(id, v);
    }
};

GyReverb::GyReverb() : impl(std::make_unique<Impl>())
{
    impl->cloud = std::make_unique<Cloudseed::ReverbController>(48000);
    for (auto &v : impl->cloudSent)
        v = -1;
    // a Cloud Seed base: long modulated lines, diffusion on, no dry, late tail only
    using namespace Cloudseed::Parameter;
    const double base[][2] = {{Interpolation, 1}, {LowCutEnabled, 1}, {HighCutEnabled, 1}, {InputMix, 0.25}, {DryOut, 0},
                              {EarlyOut, 0.4}, {LateOut, 0.85}, {TapEnabled, 1}, {TapCount, 0.4}, {TapDecay, 0.8}, {TapLength, 0.8},
                              {EarlyDiffuseEnabled, 1}, {EarlyDiffuseCount, 0.5}, {EarlyDiffuseDelay, 0.5}, {EarlyDiffuseModAmount, 0.25},
                              {EarlyDiffuseModRate, 0.25}, {LateMode, 1}, {LateLineCount, 1}, {LateDiffuseEnabled, 1}, {LateDiffuseCount, 0.6},
                              {LateDiffuseDelay, 0.4}, {LateDiffuseModAmount, 0.25}, {LateDiffuseModRate, 0.25}, {EqLowShelfEnabled, 0},
                              {EqHighShelfEnabled, 1}, {EqLowpassEnabled, 0}, {EqHighFreq, 0.5}, {EqHighGain, 0.75}, {EqCrossSeed, 0.5},
                              {SeedTap, 0.3}, {SeedDiffusion, 0.4}, {SeedDelay, 0.5}, {SeedPostDiffusion, 0.6}};
    for (auto &p : base)
        impl->cloudSet((int)p[0], p[1]);
}
GyReverb::~GyReverb() = default;

void GyReverb::setSampleRate(double sr)
{
    impl->sr = sr;
    impl->plateau.setSampleRate(sr);
    impl->cloud->SetSamplerate((int)std::lround(sr));
    // re-send everything so the lines are rebuilt at this rate
    for (int i = 0; i < Cloudseed::Parameter::COUNT; ++i)
    {
        double v = impl->cloudSent[i];
        if (v >= 0)
            impl->cloud->SetParameter(i, v);
    }
    impl->primed = false;
    clear();
}
void GyReverb::clear()
{
    impl->plateau.clear();
    impl->cloud->ClearBuffers();
}

void GyReverb::set(const GyReverbSettings &s)
{
    auto &I = *impl;
    int fl = std::clamp(s.flavour, 0, 1);
    if (fl != cur)
        cur = fl, clear();
    auto c01 = [](float v) { return std::clamp(v, 0.f, 1.f); };
    if (cur == 0)
    {
        // Plateau, mapped as Valley's module maps its knobs
        double size;
        if (s.tuned)
            size = std::clamp(0.0025 * std::pow(2.0, 5.0 * c01(s.size)) * std::exp2(s.tunedSemis / 12.0), 0.0025, 2.5);
        else
            size = std::clamp(0.01 + c01(s.size) * c01(s.size) * (4.0 - 0.01), 0.01, 4.0);
        I.plateau.setTimeScale(size);
        I.plateau.setPreDelay(0.5 * c01(s.preDelay));
        I.plateau.setInputFilterLowCutoffPitch(10.0 * c01(s.lowCut));
        I.plateau.setInputFilterHighCutoffPitch(10.0 * c01(s.highCut));
        I.plateau.enableInputDiffusion(true);
        double d = 0.1 + c01(s.decay) * (0.9999 - 0.1);
        d = 1.0 - d, d = 1.0 - d * d;
        I.plateau.setDecay(d);
        I.plateau.setTankDiffusion(10.0 * c01(s.diffusion));
        // the tank's own damping follows the cuts, kept below half the sample rate
        double maxP = 5.0 + std::log2(0.45 * I.sr / 440.0);
        I.plateau.setTankFilterLowCutFrequency(std::min(10.0 * c01(s.lowCut), maxP));
        I.plateau.setTankFilterHighCutFrequency(std::min(10.0 * c01(s.highCut), maxP));
        double ms = c01(s.modRate);
        I.plateau.setTankModSpeed(ms * ms * 99.0 + 1.0);
        I.plateau.setTankModDepth(16.0 * c01(s.modDepth));
        I.plateau.setTankModShape(0.001 + c01(s.modShape) * 0.998);
        if (s.freeze != I.frozen)
            I.frozen = s.freeze, I.plateau.freeze(s.freeze);
        I.inGain = 0.5f;
    }
    else
    {
        using namespace Cloudseed::Parameter;
        I.cloudSet(LateLineSize, 0.25 + 0.75 * c01(s.size));
        I.cloudSet(LateLineDecay, s.freeze ? 1.0 : 0.15 + 0.85 * c01(s.decay));
        I.cloudSet(TapPredelay, 0.5 * c01(s.preDelay));
        I.cloudSet(EarlyDiffuseFeedback, 0.4 + 0.5 * c01(s.diffusion));
        I.cloudSet(LateDiffuseFeedback, 0.4 + 0.5 * c01(s.diffusion));
        I.cloudSet(LateLineModAmount, c01(s.modDepth));
        I.cloudSet(LateLineModRate, c01(s.modRate));
        I.cloudSet(LowCut, c01(s.lowCut));
        I.cloudSet(HighCut, c01(s.highCut));
        I.inGain = s.freeze ? 0.f : 1.f; // Cloud Seed has no freeze: the tail holds with nothing new coming in
    }
    I.last = s;
}

void GyReverb::process(const float *inL, const float *inR, float *outL, float *outR, int n)
{
    auto &I = *impl;
    if (cur == 0)
    {
        for (int i = 0; i < n; ++i)
        {
            I.plateau.process(inL[i] * I.inGain, inR[i] * I.inGain);
            outL[i] = (float)I.plateau.getLeftOutput() * 2.f;
            outR[i] = (float)I.plateau.getRightOutput() * 2.f;
        }
    }
    else
    {
        float l[BUFFER_SIZE], r[BUFFER_SIZE];
        for (int i = 0; i < n; ++i)
            l[i] = inL[i] * I.inGain, r[i] = inR[i] * I.inGain;
        I.cloud->Process(l, r, outL, outR, n);
    }
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(outL[i]) || !std::isfinite(outR[i]))
            outL[i] = outR[i] = 0.f;
}
} // namespace otiskanno::gullay
