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
    void reset() { t.reset(); }
};

// Runaway: inside a loop. Drive boosts the repeats (up to +21 dB) into a soft saturator; Tame
// rides the level so self-oscillation (feedback above 100%) settles under the ceiling; the
// loop's low and high cuts keep the ringing musical.
// Polyphase all-pass half-band: 2x up and down (moved here from the engine; tested in tc_tests)
struct HalfBand
{
    static constexpr int M{6};
    static constexpr float ca[M]{0.036681502163648017f, 0.2746317593794541f, 0.56109896978791948f,
                                 0.769741833862266f,    0.8922608180038789f, 0.962094548378084f};
    static constexpr float cb[M]{0.13654762463195771f, 0.42313861743656667f, 0.6775400499741616f,
                                 0.839889624849638f,   0.9315419599631839f,  0.9878163707328971f};
    struct AP
    {
        float x1{0}, y1{0};
        inline float tick(float x, float a)
        {
            float y = x1 + a * (x - y1);
            x1 = x;
            y1 = y;
            return y;
        }
    };
    AP a[2][M], b[2][M];
    float old[2]{};
    // one sample in, two out (at twice the rate)
    inline void up(int ch, float x, float out[2])
    {
        float u = x + 1.0e-18f, v = u; // the tiny offset keeps the states out of denormals
        for (int j = 0; j < M; ++j)
        {
            u = a[ch][j].tick(u, ca[j]);
            v = b[ch][j].tick(v, cb[j]);
        }
        out[0] = u;
        out[1] = v;
    }
    // two samples in (at twice the rate), one out
    inline float down(int ch, float x0, float x1)
    {
        float u = x0 + 1.0e-18f, v = x1 + 1.0e-18f;
        for (int j = 0; j < M; ++j)
        {
            u = a[ch][j].tick(u, ca[j]);
            v = b[ch][j].tick(v, cb[j]);
        }
        float y = 0.5f * (u + old[ch]);
        old[ch] = v;
        return y;
    }
    void reset() { *this = HalfBand{}; }
};

struct Runaway
{
    float drive{0}, tame{0.6f}, ceilingDb{-12.f}, gUp{0.0002f};
    float hp[2]{}, lp[2]{}, env{0}, gain{1}, aHp{0.01f}, aLp{0.5f}, envA{0.001f}, envR{0.0001f};
    double sr{48000};
    void setRate(double s) { sr = s, envA = onePoleCoef(200.0, s), envR = onePoleCoef(0.4, s), gUp = onePoleCoef(1.5, s); }
    void setCuts(float lowHz, float highHz)
    {
        aHp = onePoleCoef(std::clamp(lowHz, 10.f, 2000.f), sr);
        aLp = onePoleCoef(std::clamp(highHz, 500.f, 20000.f), sr);
    }
    // cached from drive / ceilingDb / tame (they change once a control block at most)
    float cDrive{-1}, cCeil{1}, cTame{-1}, g{1}, ceil{0.25f}, norm{1};
    // Oversampling (Settings): 0 off, 1 = 2x, 2 = 4x, for the drive's tanh only (it is the one curve in the loop)
    int os{0};
    HalfBand u1, d1, u2, d2;
    inline float sat(int c, float v)
    {
        if (os <= 0)
            return std::tanh(v);
        float o[2];
        u1.up(c, v, o);
        if (os == 1)
            return d1.down(c, std::tanh(o[0]), std::tanh(o[1]));
        float w[2], y[2];
        for (int k = 0; k < 2; ++k)
        {
            u2.up(c, o[k], w);
            y[k] = d2.down(c, std::tanh(w[0]), std::tanh(w[1]));
        }
        return d1.down(c, y[0], y[1]);
    }
    inline void tick(float &l, float &r)
    {
        if (drive != cDrive)
        {
            cDrive = drive;
            g = std::pow(10.f, 21.f * std::clamp(drive, 0.f, 1.f) / 20.f);
            norm = 1.f / std::max(1.f, std::tanh(g) * 0.8f + 0.2f);
        }
        if (ceilingDb != cCeil)
            cCeil = ceilingDb, ceil = std::pow(10.f, std::clamp(ceilingDb, -36.f, -6.f) / 20.f);
        if (tame != cTame)
            cTame = tame, gUp = onePoleCoef(20.0 * std::pow(0.015, (double)std::clamp(tame, 0.f, 1.f)), sr); // 20 Hz .. 0.3 Hz
        float *x[2] = {&l, &r};
        float pk = 0;
        for (int c = 0; c < 2; ++c)
        {
            float v = *x[c] * g;
            if (cDrive > 0.f)
                v = sat(c, v) * norm; // saturation (the drive's push stays: it is what makes the loop run away)
            hp[c] += aHp * (v - hp[c]);
            v -= hp[c];
            lp[c] += aLp * (v - lp[c]);
            v = lp[c];
            *x[c] = v;
            pk = std::max(pk, std::fabs(v));
        }
        // the rider: above the ceiling the loop's gain comes down at once to hold it there, so any feedback
        // (to 120 %, and Drive's +21 dB) rings on at the ceiling instead of blowing up. Tame sets how slowly
        // the gain comes back: high Tame holds a smooth steady drone, low Tame lets it bite and sputter.
        if (pk > env)
            env = pk;
        else
            env += envR * (pk - env);
        float want = env > ceil ? ceil / env : 1.f;
        if (want < gain)
            gain = want;
        else
            gain += gUp * (want - gain);
        l *= gain, r *= gain;
    }
    void reset()
    {
        hp[0] = hp[1] = lp[0] = lp[1] = env = 0;
        gain = 1;
        u1.reset(), d1.reset(), u2.reset(), d2.reset();
    }
};

// Crusher: bit depth and a sample-and-hold rate reduction (raw steps, or smoothed)
struct Crusher
{
    float bits{16}, rateHz{48000}, mix{0};
    float tilt{0};  // -1..1: which end takes the grit (emphasis before the crush, de-emphasis after)
    float noise{0}; // 0..1: a hiss floor, up to about -34 dBFS
    bool smooth{false};
    double sr{48000}, ph{0};
    float hold[2]{}, sm[2]{}, lo[2]{}, lo2[2]{};
    float nEnv[2]{}; // v0.8.1: the hiss follows the sound (so silence, and a Kill, stay silent)
    uint32_t seed{0x9E3779B9u};
    inline float white()
    {
        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
        return (float)(int32_t)seed * (1.f / 2147483648.f);
    }
    inline void tick(float &l, float &r)
    {
        if (mix <= 0.f && noise <= 0.f)
            return;
        float *x[2] = {&l, &r};
        const float aSplit = (float)std::clamp(2.0 * 3.14159265 * 900.0 / sr, 0.001, 1.0);
        const float gHi = std::exp2(std::clamp(tilt, -1.f, 1.f) * -2.f); // tilt right: highs go in quieter, come out louder
        float dry[2] = {l, r};
        if (tilt != 0.f)
            for (int c = 0; c < 2; ++c)
            {
                lo[c] += aSplit * (*x[c] - lo[c]);
                *x[c] = lo[c] / gHi + (*x[c] - lo[c]) * gHi;
            }
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
            if (tilt != 0.f)
            {
                lo2[c] += aSplit * (v - lo2[c]);
                v = lo2[c] * gHi + (v - lo2[c]) / gHi;
            }
            float y = dry[c] + mix * (v - dry[c]);
            if (noise > 0.f)
            {
                // v0.8: up to -26 dBFS of hiss. v0.8.1: only under sound: it fades in with the signal (full from about
                // -26 dBFS) and out over about half a second after it; fed back, it dies away instead of feeding itself
                nEnv[c] = std::max(std::fabs(dry[c]), nEnv[c] * 0.99996f);
                y += noise * 0.05f * white() * std::min(1.f, nEnv[c] * 20.f);
            }
            *x[c] = y;
        }
    }
    void reset() { hold[0] = hold[1] = sm[0] = sm[1] = lo[0] = lo[1] = lo2[0] = lo2[1] = nEnv[0] = nEnv[1] = 0, ph = 0; }
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
