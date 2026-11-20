// Gullay: one channel of the Dual Looping Delay.
// A float port of 4ms Company's LoopingDelay (metamodule-core-modules a4fd5336,
// 4ms/core/looping-delay/src/looping_delay.hh, GPL-3.0-or-later, (c) 2024 4ms Company).
// Same behaviour: the read head jumps to a new time under an equal-power crossfade (384
// samples at 48 kHz, as the module's default), INF freezes writing and loops between two
// points, Reverse swaps the heads (or the loop's ends) under a crossfade, the loop can scroll.
// Changes for the plugin: stereo frames in float memory; an optional 16-bit store with
// Degrade (fewer bits); the loop's return passes through a hook (the plugin's loop effects);
// feedback may go above 1 (the hook keeps it tame); host tempo sets the ping directly.
// Copyright 2026 Otis Kanno. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace otiskanno::gullay
{
class GyDld
{
  public:
    enum class Inf { Off, On, TransOn, TransOff };

    // not real time: frames of memory (stereo)
    void allocate(uint32_t frames)
    {
        size = std::max<uint32_t>(frames, 4096);
        memL.assign(size, 0.f);
        memR.assign(size, 0.f);
        clear();
    }
    void clear()
    {
        std::fill(memL.begin(), memL.end(), 0.f);
        std::fill(memR.begin(), memR.end(), 0.f);
        wr = 0, rd = 0, frd = 0, fwr = 0;
        readPhase = 0, writePhase = 0, writeState = WNone;
        queuedTime = 0, queuedLoopAddr = -1;
        inf = Inf::Off, reverse = false, revFade = false;
        divmult = 12000;
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0;
        rd = offset(wr, (int64_t)divmult, true);
    }
    void setSampleRate(double sr)
    {
        xfadeSamples = std::max(32.0, 384.0 * sr / 48000.0);
        rate = (float)(1.0 / xfadeSamples);
        dcA = (float)(1.0 - 2.0 * 3.14159265358979 * 10.0 / sr); // the module's DC block, ~10 Hz
    }
    uint32_t frames() const { return size; }

    // settings, per sample block (control rate)
    float feedback{0.5f}; // may exceed 1 (the hook keeps it in check)
    float delayFeed{1.f}; // the input into the loop
    bool quant16{false};  // 16-bit store (the module's memory)
    int degradeBits{16};  // 16..11 in 16-bit mode (more grit per pass)

    // the delay time in samples (from host tempo x ratio); crossfades if it changes
    void setTime(double samples)
    {
        double t = std::clamp(samples, 2.0, (double)size - 2.0 * xfadeSamples - 8.0);
        if (std::fabs(t - target) < 0.5)
            return;
        target = t;
        applyTime();
    }
    void setInf(bool on)
    {
        bool isOn = inf == Inf::On || inf == Inf::TransOn;
        if (on == isOn || writeState != WNone)
        {
            wantInf = on;
            return;
        }
        toggleInf();
        wantInf = on;
    }
    void setReverse(bool on)
    {
        wantRev = on;
        if (on != reverse && !revFade)
            toggleRev();
    }
    void scroll(float amt) // fraction of the loop
    {
        if (inf != Inf::On)
            return;
        int64_t len = loopLength();
        int64_t sh = (int64_t)(amt * (float)len);
        loopStart = offset(loopStart, sh, false), loopEnd = offset(loopEnd, sh, false);
    }
    bool infOn() const { return inf == Inf::On || inf == Inf::TransOn; }
    bool reversed() const { return reverse; }
    double timeSamples() const { return divmult; }
    // where the read head is within the loop (0..1) when INF is on, else -1
    float loopPos() const
    {
        if (!infOn())
            return -1.f;
        int64_t len = std::max<int64_t>(1, loopLength());
        int64_t d = reverse ? (int64_t)loopStart - (int64_t)rd : (int64_t)rd - (int64_t)loopStart;
        d = ((d % (int64_t)size) + (int64_t)size) % (int64_t)size;
        return std::clamp((float)d / (float)len, 0.f, 1.f);
    }

    // One frame. hook(l, r) shapes what comes back round before it is fed in again.
    template <class Hook>
    inline void tick(float inL, float inR, float &outL, float &outR, Hook &&hook)
    {
        // read: the main head and the fading head
        bool rrev = revFade ^ reverse;
        if (inf == Inf::Off)
            keepSpacing();
        else if (!crossfading())
        {
            if (!inLoop(rd))
                startXfade(loopStart);
        }
        float aL = memL[rd], aR = memR[rd];
        float bL = memL[frd], bR = memR[frd];
        float rl, rr;
        if (readPhase > 0.f)
        {
            float c = std::cos(readPhase * 1.5707963f), s = std::sin(readPhase * 1.5707963f);
            rl = aL * c + bL * s, rr = aR * c + bR * s;
        }
        else
            rl = aL, rr = aR;
        // INF: crossfade from the end back to the start of the loop
        if (inf != Inf::Off && !crossfading())
        {
            uint32_t mark = startFadeAddr();
            if (rd == mark)
                startLoopingXfade();
        }
        // advance the heads
        rd = step(rd, rrev);
        frd = step(frd, reverse);
        incReadFade();

        float l = rl, r = rr;
        hook(l, r);
        float wl = l * feedback + inL * delayFeed, wr2 = r * feedback + inR * delayFeed;
        // DC block, then the module's soft clip at 75% of full scale
        float yl = wl - dcX[0] + dcA * dcY[0], yr = wr2 - dcX[1] + dcA * dcY[1];
        dcX[0] = wl, dcX[1] = wr2, dcY[0] = std::fabs(yl) < 1e-20f ? 0.f : yl, dcY[1] = std::fabs(yr) < 1e-20f ? 0.f : yr;
        yl = softClip(yl), yr = softClip(yr);
        if (quant16)
            yl = q(yl), yr = q(yr);
        write(yl, yr);
        endReadFade();
        incWriteFade();
        outL = rl, outR = rr;
    }

  private:
    std::vector<float> memL, memR;
    uint32_t size{4096};
    uint32_t wr{0}, rd{0}, frd{0}, fwr{0};
    float readPhase{0}, writePhase{0}, rate{1.f / 384.f};
    double xfadeSamples{384};
    enum WState { WNone, WDown, WUp, WCross } writeState{WNone};
    double divmult{12000}, target{12000}, queuedTime{0};
    int64_t queuedLoopAddr{-1};
    Inf inf{Inf::Off};
    bool reverse{false}, revFade{false}, wantInf{false}, wantRev{false};
    uint32_t loopStart{0}, loopEnd{0};
    float dcX[2]{}, dcY[2]{}, dcA{0.9987f};

    static inline float softClip(float x) // the module's compressor: linear to 0.75, then 1/x
    {
        constexpr float T = 0.75f, K = T * (1.f - T);
        if (x > T)
            return 1.f - K / x;
        if (x < -T)
            return -1.f - K / x;
        return x;
    }
    inline float q(float x) const
    {
        int bits = std::clamp(degradeBits, 2, 16);
        float steps = (float)(1 << (bits - 1));
        return std::round(std::clamp(x, -1.f, 1.f) * steps) / steps;
    }
    uint32_t offset(uint32_t base, int64_t off, bool subtract) const
    {
        if (subtract)
            off = -off;
        int64_t v = ((int64_t)base + off) % (int64_t)size;
        if (v < 0)
            v += size;
        return (uint32_t)v;
    }
    uint32_t step(uint32_t a, bool rev) const { return rev ? (a == 0 ? size - 1 : a - 1) : (a + 1 == size ? 0 : a + 1); }
    bool crossfading() const { return readPhase >= rate; }
    void startXfade(uint32_t addr)
    {
        readPhase = rate;
        queuedTime = 0;
        frd = addr;
    }
    void incReadFade()
    {
        if (readPhase > 0.f)
            readPhase = std::min(1.f, readPhase + rate);
    }
    void endReadFade()
    {
        if (readPhase >= 1.f)
        {
            readPhase = 0.f;
            revFade = false;
            rd = frd;
            if (queuedTime > 0)
            {
                divmult = queuedTime;
                startXfade(readAddr(divmult));
            }
            else if (queuedLoopAddr >= 0)
            {
                startXfade((uint32_t)queuedLoopAddr);
                queuedLoopAddr = -1;
            }
            if (wantRev != reverse && !revFade)
                toggleRev();
        }
    }
    uint32_t readAddr(double t) const { return offset(wr, (int64_t)std::llround(t), !reverse); }
    void keepSpacing()
    {
        if (!crossfading())
        {
            uint32_t t = readAddr(divmult);
            if (t != rd)
                applyTime();
        }
    }
    void applyTime()
    {
        if (inf == Inf::Off)
        {
            if (crossfading())
            {
                if (divmult != target)
                    queuedTime = target;
            }
            else
            {
                divmult = target;
                uint32_t a = readAddr(divmult);
                if (a != rd)
                    startXfade(a);
            }
        }
        else
        {
            divmult = target;
            loopStart = offset(loopEnd, (int64_t)std::llround(divmult), !reverse);
            if (!inLoop(rd))
            {
                if (crossfading())
                    queuedLoopAddr = loopStart;
                else
                    startXfade(loopStart);
            }
        }
    }
    bool inLoop(uint32_t mid) const
    {
        uint32_t beg = loopStart, end = loopEnd;
        if (beg == end)
            return mid == beg;
        if (beg < end)
            return ((mid >= beg) && (mid <= end)) != reverse;
        return ((mid >= end) && (mid <= beg)) == reverse;
    }
    int64_t loopLength() const
    {
        uint32_t hi = loopEnd, lo = loopStart;
        if (reverse)
            std::swap(hi, lo);
        return hi > lo ? (int64_t)(hi - lo) : (int64_t)hi + (int64_t)size - lo;
    }
    uint32_t startFadeAddr() const
    {
        if (divmult < xfadeSamples)
            return loopEnd;
        return offset(loopEnd, (int64_t)xfadeSamples, !reverse);
    }
    void startLoopingXfade()
    {
        if (divmult <= xfadeSamples)
        {
            rd = loopStart;
            readPhase = 0.f;
            frd = offset(rd, 1, reverse);
        }
        else
        {
            int64_t len = (int64_t)loopEnd - (int64_t)loopStart;
            if (reverse)
                len = -len;
            startXfade(offset(rd, len, !reverse));
        }
        if (inf == Inf::TransOff)
            inf = Inf::Off;
    }
    void write(float l, float r)
    {
        if (inf == Inf::On)
        {
            wr = step(wr, reverse), fwr = wr; // the write head keeps moving, writing nothing
            return;
        }
        auto put = [&](uint32_t a, float gain)
        {
            memL[a] = memL[a] * (1.f - gain) + l * gain;
            memR[a] = memR[a] * (1.f - gain) + r * gain;
        };
        if (inf == Inf::TransOn)
        {
            if (writeState == WDown)
            {
                put(fwr, 1.f - writePhase);
                fwr = step(fwr, reverse);
                wr = fwr;
            }
            else
                wr = step(wr, reverse), fwr = wr;
            return;
        }
        if (writeState == WCross)
        {
            put(fwr, writePhase); // the swapped write head fades in
            fwr = step(fwr, reverse);
            wr = fwr;
        }
        else if (writeState == WUp)
        {
            put(fwr, writePhase);
            fwr = step(fwr, reverse);
            wr = fwr;
        }
        else
        {
            memL[wr] = l, memR[wr] = r;
            wr = step(wr, reverse);
            fwr = wr;
        }
    }
    void incWriteFade()
    {
        if (writePhase > 0.f)
        {
            writePhase += rate;
            if (writePhase > 1.f)
            {
                writePhase = 0.f;
                writeState = WNone;
                wr = fwr;
                if (inf == Inf::TransOn)
                    inf = Inf::On;
                bool isOn = inf == Inf::On || inf == Inf::TransOn;
                if (wantInf != isOn)
                    toggleInf();
            }
        }
    }
    void toggleRev()
    {
        reverse = !reverse;
        if (inf == Inf::Off)
        {
            startXfade(wr); // swap the heads
            fwr = rd;
            writePhase = rate;
            writeState = WCross;
        }
        else
        {
            uint32_t t = loopStart, pad = (uint32_t)xfadeSamples;
            loopStart = offset(loopEnd, pad, reverse);
            loopEnd = offset(t, pad, reverse);
            startXfade(rd);
        }
        revFade = true;
    }
    void toggleInf()
    {
        if (inf == Inf::On || inf == Inf::TransOn)
        {
            inf = Inf::TransOff;
            writePhase = rate;
            writeState = WUp;
            fwr = rd;
        }
        else
        {
            if (inf != Inf::TransOff)
            {
                loopStart = crossfading() ? frd : rd;
                loopEnd = offset(loopStart, (int64_t)std::llround(divmult), reverse);
            }
            writePhase = rate;
            writeState = WDown;
            fwr = wr;
            inf = Inf::TransOn;
        }
    }
};
} // namespace otiskanno::gullay
