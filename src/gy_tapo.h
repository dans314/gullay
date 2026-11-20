// Gullay: the Tapographic Delay engine.
// Adapted from 4ms/Mutable Instruments-lineage TapoDelay::MultitapDelay (Matthias Puech,
// MIT; in 4ms metamodule-core-modules a4fd5336, 4ms/core/tapo). The taps, the tap allocator,
// the faders, the random time LFOs and the buffer are the module's own files, unchanged
// (libs/fourms/tapo). This class re-does MultitapDelay::Process for the plugin:
//   - the taps are summed into two groups, early (part A) and late (part B), so each can be
//     moved on its own; the pattern as a whole is Lead;
//   - what is fed back passes through a hook (the plugin's loop effects) and may exceed 1;
//   - patterns come from the plugin (24 slots, the module's factory patterns to start), and
//     the tap times are tied to the host tempo (one pattern length = a number of beats).
// Copyright 2026 Otis Kanno. GPL-3.0-or-later (the module's files keep their MIT notices).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "tapo/tap_allocator.hh"
#include "tapo/resources.h"
#include "tapo/stmlib/utils/random.h"

namespace otiskanno::gullay
{
static constexpr int tapoSlots = 25, // 24 factory patterns + 1 tapped by hand
                     tapoFactorySlots = 24, tapoBlock = (int)TapoDelay::kBlockSize; // 64
struct TapoPattern
{
    int size{0};
    float time[TapoDelay::kMaxTaps]{};  // in samples at 48 kHz, as the module stores them
    float velo[TapoDelay::kMaxTaps]{};
    int type[TapoDelay::kMaxTaps]{};    // 0 amp, 1 low-pass, 2 band-pass
    float pan[TapoDelay::kMaxTaps]{};
};
// Gullay's own row D, in the spirit of the module's patterns (the module ships these slots empty):
// D1 bouncing ball, D2 dotted swing, D3 clave, D4 three against four, D5 Fibonacci, D6 swell
inline TapoPattern gullayRowD(int d)
{
    TapoPattern p;
    auto add = [&](double t, double v, int type, double pan) {
        if (p.size >= TapoDelay::kMaxTaps)
            return;
        p.time[p.size] = (float)t, p.velo[p.size] = (float)std::clamp(v, 0.0, 1.0), p.type[p.size] = type, p.pan[p.size] = (float)std::clamp(pan, 0.0, 1.0);
        ++p.size;
    };
    const double L = 240000.0; // the module's longest pattern, in samples
    switch (d)
    {
    case 0: // a ball dropped: each bounce sooner and softer
    {
        double gap = L * 0.22, t = 0;
        for (int i = 0; i < 16; ++i)
            t += gap, add(t, 1.0 - i * 0.055, 1, (i & 1) ? 0.75 : 0.25), gap *= 0.8;
        break;
    }
    case 1: // dotted swing: long, short, long, short, accents on the long
        for (int i = 0, k = 0; i < 16; ++i)
            k += (i & 1) ? 1 : 3, add(L * k / 32.0, (i & 1) ? 0.45 : 0.95, 0, (i & 1) ? 0.7 : 0.3);
        break;
    case 2: // son clave 3-2 over two bars, played twice, band-passed like a wood block
    {
        const int hits[5] = {0, 3, 6, 10, 12};
        for (int r = 0; r < 2; ++r)
            for (int h : hits)
                add(L * (r * 16 + h + 1) / 32.0, h == 0 ? 1.0 : 0.75, 2, h < 8 ? 0.35 : 0.65);
        break;
    }
    case 3: // three against four: the threes on the left, the fours on the right, louder where they meet
        for (int i = 1; i <= 12; ++i)
        {
            const bool three = i % 4 == 0, four = i % 3 == 0;
            if (three || four)
                add(L * i / 12.0, three && four ? 1.0 : 0.6, three && four ? 1 : 0, three && four ? 0.5 : (three ? 0.15 : 0.85));
        }
        break;
    case 4: // Fibonacci gaps: 1, 1, 2, 3, 5, 8 … each echo further and quieter
    {
        double a = 1, b = 1, t = 0, unit = L / 54.0;
        for (int i = 0; i < 8; ++i)
            t += a * unit, add(t, 1.0 / (1.0 + i * 0.35), 1, i & 1 ? 0.8 : 0.2), std::swap(a, b), b += a;
        break;
    }
    default: // a swell: even steps that rise then fall, swept left to right
        for (int i = 0; i < 16; ++i)
            add(L * (i + 1) / 16.0, 0.15 + 0.85 * std::sin(3.14159265 * (i + 0.5) / 16.0), 1, i / 15.0);
        break;
    }
    // never past the module's longest pattern (its memory is sized for that)
    float last = 0;
    for (int t = 0; t < p.size; ++t)
        last = std::max(last, p.time[t]);
    if (last > (float)L)
        for (int t = 0; t < p.size; ++t)
            p.time[t] *= (float)L / last;
    return p;
}

// the module's factory patterns (slots 1..18; 19..24 start empty)
inline TapoPattern tapoFactoryPattern(int slot)
{
    using namespace TapoDelay;
    TapoPattern p;
    if (slot < 0 || slot >= tapoSlots)
        return p;
    if (slot >= 18 && slot < 24 && lut_preset_sizes[slot] == 0)
        return gullayRowD(slot - 18);
    p.size = std::clamp<int>(lut_preset_sizes[slot], 0, kMaxTaps);
    for (int t = 0; t < p.size; ++t)
    {
        int i = t + kMaxTaps * slot;
        p.time[t] = lut_preset_times[i], p.velo[t] = lut_preset_velos[i];
        p.type[t] = std::clamp<int>(lut_preset_types[i], 0, 2), p.pan[t] = lut_preset_pans[i];
    }
    return p;
}

class GyTapo
{
  public:
    using F = TapoDelay::FloatFrame;
    // not real time
    void allocate(uint32_t samplesPow2)
    {
        mem.assign(samplesPow2, 0);
        buffer.Init(mem.data(), (int32_t)mem.size());
        for (auto &t : taps)
            t.Init(48000);
        alloc.Init(taps);
        dc.Init();
        repeat.Init();
    }
    void setSampleRate(double sr)
    {
        sampleRate = sr;
        for (auto &t : taps)
            t.Init((uint32_t)sr);
        alloc.Init(taps);
        dc.Init();
        dc.set_f_q<TapoDelay::stmlib::FREQUENCY_FAST>((float)(10.0 / sr), 0.6f);
        loaded = -1;
    }
    void clear()
    {
        buffer.Clear();
        std::fill(std::begin(fbBuf), std::end(fbBuf), 0.f);
    }
    // load a pattern (fades the old taps out and the new ones in over morph seconds)
    void load(const TapoPattern &p, int id, float morphSec)
    {
        TapoDelay::Slot s{};
        s.size = (uint8_t)std::clamp(p.size, 0, (int)TapoDelay::kMaxTaps);
        for (int t = 0; t < s.size; ++t)
            s.taps[(size_t)t] = {p.time[t], p.velo[t], (TapoDelay::VelocityType)std::clamp(p.type[t], 0, 2), p.pan[t]};
        alloc.set_fade_time(std::max(0.001f, morphSec) * (float)sampleRate); // in samples
        alloc.Load(&s);
        loaded = id;
        maxTime48 = 0;
        for (int t = 0; t < s.size; ++t)
            maxTime48 = std::max(maxTime48, p.time[t]);
    }
    int loadedId() const { return loaded; }
    // a pattern's own length (its last tap), in seconds at the module's rate
    static float naturalSeconds(const TapoPattern &p)
    {
        float m = 0;
        for (int t = 0; t < p.size; ++t)
            m = std::max(m, p.time[t]);
        return m > 0 ? m / 48000.f : 0.5f;
    }
    float patternLength48() const { return maxTime48; }

    // per block (control): the scale that makes the pattern last patternBeats at bpm
    float feedback{0.4f}, gain{1.f}, modAmount{0}, modRate{0.2f}, velocityParam{0.f};
    int degradeBits{16};
    float splitFrac{0.5f}; // taps before this share of the pattern are part A, the rest B
    bool repeatOn{false};
    void setLength(double patternSamples) // the pattern's last tap lands after this many samples
    {
        float base = maxTime48 > 0 ? maxTime48 * (float)(sampleRate / 48000.0) : (float)sampleRate;
        scaleTarget = (float)std::clamp(patternSamples / std::max(1.f, base), 0.01, 50.0) * (float)(sampleRate / 48000.0);
    }

    // one 64-sample block: in (mono), out: groups A and B (stereo each)
    template <class Hook>
    void process(const float *in, F *outA, F *outB, Hook &&hook)
    {
        using namespace TapoDelay;
        TapoDelay::stmlib::Random::Seed(rng);
        alloc.Poll();
        Parameters P{};
        // Tap wobble: the module's time LFO swings up to 0.5 s and its rate is a slope per block, so both
        // are mapped here to useful ranges: amount (squared) up to about 40 ms, rate from slow drift to a fast shimmer.
        float amt = std::clamp(modAmount, 0.f, 1.f);
        float rate = 0.00015f * std::exp2(std::clamp(modRate, 0.f, 1.f) * 9.f);
        P.gain = gain, P.scale = scaleTarget, P.feedback = feedback, P.modulation_amount = amt * amt * 0.08f,
        P.modulation_frequency = rate, P.morph = 0.f, P.drywet = 1.f, P.velocity_parameter = velocityParam;
        P.edit_mode = EDIT_OFF, P.panning_mode = PANNING_RANDOM;
        if (!primed)
            prev = P, primed = true;
        const float head = 0.5f;
        // feedback compensated by the taps' total volume, as the module does
        float comp = (float)alloc.total_volumes();
        comp = comp < 1.f ? 1.f : 1.f / std::sqrt(comp);
        fbComp += 0.05f * (comp - fbComp);
        float fb0 = prev.feedback, fb1 = feedback * fbComp, dfb = (fb1 - fb0) / (float)tapoBlock;
        P.feedback = fb1;
        float g0 = prev.gain, dg = (gain - g0) / (float)tapoBlock;
        // the repeat: the whole pattern read back at its length
        uint32_t maxT = (uint32_t)(alloc.max_time() * P.scale);
        if (repeatOn != repeatWas)
        {
            repeatWas = repeatOn;
            if (repeatOn)
                repeat.fade_in(0.02f * (float)sampleRate), repeatTime = std::max<uint32_t>(maxT, 1);
            else
                repeat.fade_out(0.02f * (float)sampleRate);
        }
        // 1. write: dry + feedback (through the hook) + repeat
        for (int i = 0; i < tapoBlock; ++i)
        {
            float rs = (float)buffer.ReadShort(repeatTime) / 32768.f / head;
            repeat.Process(rs);
            float s = g0 * in[i] + fb0 * fbBuf[i] + rs + (TapoDelay::stmlib::Random::GetFloat() - 0.5f) / 8192.f;
            s = TapoDelay::stmlib::SoftClip(s * head); // SoftLimit folds back past 3 (Gullay's feedback can go over 1)
            repeat.Prepare();
            int32_t q = TapoDelay::stmlib::Clip16((int32_t)(s * 32768.f));
            if (degradeBits < 16) // Degrade: the low bits dropped, as on the DLD's 16-bit memory
                q &= ~((1 << (16 - std::clamp(degradeBits, 4, 16))) - 1);
            buffer.Write((int16_t)q);
            g0 += dg, fb0 += dfb;
        }
        // 2. read the taps into their groups
        F a[tapoBlock], b[tapoBlock];
        for (int i = 0; i < tapoBlock; ++i)
            a[i] = b[i] = {0.f, 0.f};
        float split = alloc.max_time() * std::clamp(splitFrac, 0.f, 1.f);
        for (auto &t : taps)
        {
            if (!t.active())
                continue;
            t.Process(&prev, &P, &buffer, t.time() <= split ? a : b);
        }
        // 3. the feedback (mono, DC-blocked) goes through the hook; the groups go out
        for (int i = 0; i < tapoBlock; ++i)
        {
            outA[i] = {a[i].l / head, a[i].r / head};
            outB[i] = {b[i].l / head, b[i].r / head};
            float l = (a[i].l + b[i].l) / head, r = (a[i].r + b[i].r) / head;
            hook(l, r);
            fbBuf[i] = dc.Process<TapoDelay::stmlib::FILTER_MODE_HIGH_PASS>(l + r);
        }
        prev = P;
        rng = TapoDelay::stmlib::Random::state();
    }
    // the taps for the window: time 0..1 of the pattern, level, group
    int tapCount() const { return (int)TapoDelay::kMaxTaps; }

  private:
    std::vector<short> mem;
    TapoDelay::AudioBuffer buffer{};
    TapoDelay::Tap taps[TapoDelay::kMaxTaps]{};
    TapoDelay::TapAllocator alloc{};
    TapoDelay::stmlib::Svf dc{};
    TapoDelay::Fader repeat{};
    TapoDelay::Parameters prev{};
    bool primed{false}, repeatWas{false};
    uint32_t repeatTime{1}, rng{0x21};
    float fbBuf[tapoBlock]{}, fbComp{1.f}, scaleTarget{1.f}, maxTime48{0};
    double sampleRate{48000};
    int loaded{-1};
};
} // namespace otiskanno::gullay
