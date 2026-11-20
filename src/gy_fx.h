// Gullay: small effects shared by the engine: tilt EQ, part tone, Runaway (drive, tame, loop
// cuts), the crusher, and the clean two-head pitch shifter used by Hymn's Shift.
// Copyright 2026 Otis Kanno. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace otiskanno::gullay
{
inline float onePoleCoef(double hz, double sr) { return (float)(1.0 - std::exp(-2.0 * 3.14159265358979 * std::clamp(hz, 1.0, 0.49 * sr) / sr)); }

// Tilt: a one-pole split at the pivot; lows down and highs up (or the reverse), +-tilt dB at the ends
struct Tilt
{
    float lp[2]{}, a{0.1f}, gl{1}, gh{1};
    void set(float tiltDb, float pivotHz, double sr)
    {
        a = onePoleCoef(pivotHz, sr);
        float t = std::clamp(tiltDb, -12.f, 12.f);
        gl = std::pow(10.f, -t / 40.f), gh = std::pow(10.f, t / 40.f);
    }
    inline void tick(float &l, float &r)
    {
        float *x[2] = {&l, &r};
        for (int c = 0; c < 2; ++c)
        {
            lp[c] += a * (*x[c] - lp[c]);
            float lo = lp[c], hi = *x[c] - lo;
            *x[c] = lo * gl + hi * gh;
        }
    }
    void reset() { lp[0] = lp[1] = 0; }
};

// a part's Tone: 0 dark .. 0.5 open .. 1 bright (a gentle tilt around 1.2 kHz)
struct PartTone
{
    Tilt t;
    void set(float tone, double sr) { t.set((std::clamp(tone, 0.f, 1.f) - 0.5f) * 18.f, 1200.f, sr); }
    inline void tick(float &l, float &r) { t.tick(l, r); }
};

// Runaway: inside a loop. Drive boosts the repeats (up to +21 dB) into a soft saturator; Tame
// rides the level so self-oscillation (feedback above 100%) settles under the ceiling; the
// loop's low and high cuts keep the ringing musical.
struct Runaway
{
    float drive{0}, tame{0.6f}, ceilingDb{-12.f};
    float hp[2]{}, lp[2]{}, env{0}, gain{1}, aHp{0.01f}, aLp{0.5f}, envA{0.001f}, envR{0.0001f};
    double sr{48000};
    void setRate(double s) { sr = s, envA = onePoleCoef(200.0, s), envR = onePoleCoef(2.0, s); }
    void setCuts(float lowHz, float highHz)
    {
        aHp = onePoleCoef(std::clamp(lowHz, 10.f, 2000.f), sr);
        aLp = onePoleCoef(std::clamp(highHz, 500.f, 20000.f), sr);
    }
    inline void tick(float &l, float &r)
    {
        float g = std::pow(10.f, 21.f * std::clamp(drive, 0.f, 1.f) / 20.f);
        float *x[2] = {&l, &r};
        float pk = 0;
        for (int c = 0; c < 2; ++c)
        {
            float v = *x[c] * g;
            if (drive > 0.f)
                v = std::tanh(v) / std::max(1.f, std::tanh(g) * 0.8f + 0.2f); // level-matched saturation
            hp[c] += aHp * (v - hp[c]);
            v -= hp[c];
            lp[c] += aLp * (v - lp[c]);
            v = lp[c];
            *x[c] = v;
            pk = std::max(pk, std::fabs(v));
        }
        // the rider: above the ceiling the gain comes down, softly and at a rate set by Tame
        float a = pk > env ? envA : envR;
        env += a * (pk - env);
        float ceil = std::pow(10.f, std::clamp(ceilingDb, -36.f, -6.f) / 20.f);
        float want = env > ceil ? ceil / env : 1.f;
        want = 1.f - std::clamp(tame, 0.f, 1.f) * (1.f - want);
        gain += (want < gain ? 0.01f : 0.0005f) * (want - gain);
        l *= gain, r *= gain;
    }
    void reset()
    {
        hp[0] = hp[1] = lp[0] = lp[1] = env = 0;
        gain = 1;
    }
};

// Crusher: bit depth and a sample-and-hold rate reduction (raw steps, or smoothed)
struct Crusher
{
    float bits{16}, rateHz{48000}, mix{0};
    bool smooth{false};
    double sr{48000}, ph{0};
    float hold[2]{}, sm[2]{};
    inline void tick(float &l, float &r)
    {
        if (mix <= 0.f)
            return;
        float *x[2] = {&l, &r};
        ph += std::clamp((double)rateHz, 200.0, sr) / sr;
        bool take = ph >= 1.0;
        if (take)
            ph -= std::floor(ph);
        float steps = std::exp2(std::clamp(bits, 2.f, 16.f) - 1.f);
        for (int c = 0; c < 2; ++c)
        {
            if (take)
                hold[c] = std::round(std::clamp(*x[c], -1.f, 1.f) * steps) / steps;
            float v = hold[c];
            if (smooth)
                sm[c] += 0.35f * (v - sm[c]), v = sm[c];
            *x[c] += mix * (v - *x[c]);
        }
    }
    void reset() { hold[0] = hold[1] = sm[0] = sm[1] = 0, ph = 0; }
};

// The clean shifter: two read heads half a window apart, sin^2 windows (they sum to 1), 150 ms
// windows. Mean added delay is half a window. Measured in Haint on pure tones: about -7..+15 cents.
struct CleanShifter
{
    std::vector<float> buf;
    int mask{0}, w{0};
    float phase{0}, ratio{1}, win{7200};
    void prepare(double sr)
    {
        int n = 1;
        while (n < (int)(0.4 * sr))
            n <<= 1;
        buf.assign((size_t)n, 0.f);
        mask = n - 1;
        win = (float)std::clamp(0.15 * sr, 512.0, (double)n / 2 - 4);
    }
    void reset()
    {
        std::fill(buf.begin(), buf.end(), 0.f);
        phase = 0;
    }
    void setSemitones(float st) { ratio = std::exp2(st / 12.f); }
    float delaySamples() const { return 0.5f * win; }
    inline float read(float d) const
    {
        float pos = (float)w - d;
        int i = (int)std::floor(pos);
        float f = pos - (float)i;
        float a = buf[(size_t)(i & mask)], b = buf[(size_t)((i + 1) & mask)];
        return a + f * (b - a);
    }
    inline float process(float x)
    {
        buf[(size_t)(w & mask)] = x;
        phase += (1.f - ratio) / win;
        phase -= std::floor(phase);
        float p2 = phase + 0.5f;
        p2 -= std::floor(p2);
        float s1 = std::sin(3.14159265f * phase), s2 = std::sin(3.14159265f * p2);
        float y = read(phase * win + 1.f) * s1 * s1 + read(p2 * win + 1.f) * s2 * s2;
        ++w;
        return y;
    }
};
} // namespace otiskanno::gullay
