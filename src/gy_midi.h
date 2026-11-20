// Gullay: MIDI Learn (made for a Faderfox EC4, works with any controller).
//
// A mapping ties one controller (a CC, a 14-bit CC pair or an NRPN on one channel) to one parameter, with a
// range (lo..hi of the parameter's travel), a behaviour (knob, toggle, momentary) and pickup.
// The table is written by the window (main thread) and read by the audio thread: each mapping is two atomic
// words, the range written before the header, so the audio thread never sees a half-made mapping.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

enum MidiKind : int
{
    MkAbs7 = 0,  // ordinary CC 0..127
    MkAbs14,     // CC n (MSB) + CC n+32 (LSB)
    MkNrpn,      // CC 99 / 98 select, 6 / 38 data (14-bit)
    MkRelTwos,   // relative: 1..63 up, 127..65 down (two's complement; EC4 "Rel 1")
    MkRelOffset, // relative: 65.. up, ..63 down (binary offset; EC4 "Rel 2")
    MkRelSigned, // relative: 1..63 up, 65..127 down (sign bit)
    numMidiKinds
};
enum MidiBehaviour : int { MbKnob = 0, MbToggle, MbMomentary };

struct MidiMap
{
    bool valid{false};
    int ch{0}, num{0}, kind{MkAbs7}, beh{MbKnob};
    bool pickup{false};
    int param{0}; // index into paramDefs()
    float lo{0}, hi{1};
};

class GyMidiLearn
{
  public:
    static constexpr int kMax = 128;

    static uint64_t packHead(const MidiMap &m)
    {
        return (m.valid ? 1ull : 0ull) | ((uint64_t)(m.ch & 15) << 1) | ((uint64_t)(m.num & 0x3fff) << 5) | ((uint64_t)(m.kind & 7) << 19) |
               ((uint64_t)(m.beh & 3) << 22) | ((uint64_t)(m.pickup ? 1 : 0) << 24) | ((uint64_t)(m.param & 0xffff) << 25);
    }
    static uint32_t packRange(float lo, float hi)
    {
        auto q = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.f, 1.f) * 65535.f); };
        return q(lo) | (q(hi) << 16);
    }
    MidiMap get(int i) const
    {
        MidiMap m;
        uint64_t h = head[i].load(std::memory_order_acquire);
        uint32_t r = range[i].load(std::memory_order_relaxed);
        m.valid = h & 1, m.ch = (int)((h >> 1) & 15), m.num = (int)((h >> 5) & 0x3fff), m.kind = (int)((h >> 19) & 7);
        m.beh = (int)((h >> 22) & 3), m.pickup = (h >> 24) & 1, m.param = (int)((h >> 25) & 0xffff);
        m.lo = (float)(r & 0xffff) / 65535.f, m.hi = (float)(r >> 16) / 65535.f;
        return m;
    }
    // main thread
    void set(int i, const MidiMap &m)
    {
        if (i < 0 || i >= kMax)
            return;
        head[i].store(0, std::memory_order_release);
        range[i].store(packRange(m.lo, m.hi), std::memory_order_relaxed);
        head[i].store(packHead(m), std::memory_order_release);
        picked[i].store(false, std::memory_order_relaxed);
    }
    void remove(int i) { if (i >= 0 && i < kMax) head[i].store(0, std::memory_order_release); }
    void clear() { for (int i = 0; i < kMax; ++i) head[i].store(0, std::memory_order_release); }
    int count() const { int n = 0; for (int i = 0; i < kMax; ++i) n += get(i).valid; return n; }
    // arm Learn: the next controller moved is tied to this parameter (index), read as this kind
    void arm(int param, int kind) { learnKind.store(kind); learnParam.store(param); }
    void disarm() { learnParam.store(-1); }
    int armed() const { return learnParam.load(); }
    // set by the audio thread after a Learn: the slot it filled (the window polls it)
    std::atomic<int> lastLearned{-1};

    // text form, one mapping per line (projects and the default set): "M ch num kind beh pickup paramId lo hi"
    template <class IdOf>
    std::string save(IdOf idOf) const
    {
        std::string s;
        char b[128];
        for (int i = 0; i < kMax; ++i)
        {
            MidiMap m = get(i);
            if (!m.valid)
                continue;
            std::snprintf(b, sizeof(b), "M %d %d %d %d %d %u %.5f %.5f\n", m.ch, m.num, m.kind, m.beh, m.pickup ? 1 : 0, (unsigned)idOf(m.param), m.lo, m.hi);
            s += b;
        }
        return s;
    }
    // one line (without the "M "); returns false if it does not parse or the parameter is unknown
    template <class IndexOf>
    bool loadLine(const char *line, IndexOf indexOf)
    {
        MidiMap m;
        unsigned id = 0;
        int pk = 0;
        if (std::sscanf(line, "%d %d %d %d %d %u %f %f", &m.ch, &m.num, &m.kind, &m.beh, &pk, &id, &m.lo, &m.hi) != 8)
            return false;
        m.param = indexOf(id);
        if (m.param < 0 || m.kind < 0 || m.kind >= numMidiKinds)
            return false;
        m.pickup = pk != 0, m.valid = true;
        for (int i = 0; i < kMax; ++i)
            if (!get(i).valid)
            {
                set(i, m);
                return true;
            }
        return false;
    }

    // ---- audio thread
    // Feeds one MIDI 1.0 message. get(param) -> the parameter's position 0..1; put(param, pos 0..1) moves it.
    // Returns true if the message was a controller this table (or Learn) used.
    template <class Get, class Put>
    bool feed(const uint8_t *msg, Get get01, Put put01)
    {
        const int status = msg[0] & 0xf0, ch = msg[0] & 0x0f;
        if (status != 0xb0)
            return false;
        const int cc = msg[1] & 0x7f, val = msg[2] & 0x7f;
        auto &st = chan[ch];
        // collect 14-bit and NRPN parts
        int nrpnNum = -1, nrpnVal = -1;
        if (cc == 99) { st.nrpnMsb = val; st.dataMsb = -1; return true; }
        if (cc == 98) { st.nrpnLsb = val; st.dataMsb = -1; return true; }
        if (cc == 6 && st.nrpnMsb >= 0 && st.nrpnLsb >= 0) { st.dataMsb = val; nrpnNum = (st.nrpnMsb << 7) | st.nrpnLsb; nrpnVal = val << 7; }
        else if (cc == 38 && st.nrpnMsb >= 0 && st.nrpnLsb >= 0 && st.dataMsb >= 0) { nrpnNum = (st.nrpnMsb << 7) | st.nrpnLsb; nrpnVal = (st.dataMsb << 7) | val; }
        if (cc < 32)
            st.msb[cc] = val;

        // Learn
        int lp = learnParam.load(std::memory_order_relaxed);
        if (lp >= 0)
        {
            MidiMap m;
            m.valid = true, m.ch = ch, m.param = lp, m.lo = 0, m.hi = 1;
            m.kind = learnKind.load(std::memory_order_relaxed);
            if (nrpnNum >= 0)
                m.kind = MkNrpn, m.num = nrpnNum;
            else if (cc >= 32 && cc < 64 && m.kind == MkAbs14)
                return true; // the LSB half: wait for the MSB
            else
            {
                if (m.kind == MkNrpn)
                    m.kind = MkAbs7;
                m.num = cc;
                if (m.kind == MkAbs14 && cc >= 32)
                    m.kind = MkAbs7;
            }
            // re-learning the same control or parameter replaces it
            int slot = -1;
            for (int i = 0; i < kMax; ++i)
            {
                MidiMap o = this->get(i);
                if (o.valid && ((o.ch == m.ch && o.num == m.num && o.kind == m.kind) || o.param == m.param))
                    head[i].store(0, std::memory_order_release);
            }
            for (int i = 0; i < kMax && slot < 0; ++i)
                if (!this->get(i).valid)
                    slot = i;
            if (slot >= 0)
            {
                set(slot, m);
                lastLearned.store(slot);
                probeN[slot] = m.kind == MkAbs7 ? 1 : 0, probeFlags[slot] = 7;
            }
            learnParam.store(-1);
            return true;
        }

        bool used = false;
        for (int i = 0; i < kMax; ++i)
        {
            uint64_t h = head[i].load(std::memory_order_acquire);
            if (!(h & 1))
                continue;
            MidiMap m = this->get(i);
            if (m.ch != ch)
                continue;
            float pos = -1.f; // 0..1 across the mapping's range, or <0: nothing to set
            float delta = 0.f;
            bool rel = false;
            switch (m.kind)
            {
            case MkAbs7:
                if (cc != m.num) continue;
                if (probeN[i] > 0 && probeN[i] < 8)
                {
                    const bool twos = (val >= 1 && val <= 4) || val >= 124, off = val >= 60 && val <= 68 && val != 64, sgn = (val >= 1 && val <= 4) || (val >= 65 && val <= 68);
                    probeFlags[i] &= (uint8_t)((twos ? 1 : 0) | (off ? 2 : 0) | (sgn ? 4 : 0));
                    if (!probeFlags[i])
                        probeN[i] = 0; // an ordinary knob after all
                    else if (++probeN[i] >= 6)
                    {
                        m.kind = (probeFlags[i] & 2) ? MkRelOffset : (probeFlags[i] & 1) ? MkRelTwos : MkRelSigned;
                        set(i, m);
                        probeN[i] = 0;
                        continue; // read as relative from the next message on
                    }
                }
                pos = (float)val / 127.f;
                break;
            case MkAbs14:
                if (cc == m.num) { pos = (float)((val << 7) | lsb[ch][m.num & 31]) / 16383.f; }
                else if (cc == m.num + 32) { lsb[ch][m.num & 31] = (uint8_t)val; pos = (float)((st.msb[m.num & 31] << 7) | val) / 16383.f; }
                else continue;
                break;
            case MkNrpn:
                if (nrpnNum != m.num) continue;
                pos = (float)nrpnVal / 16383.f;
                break;
            case MkRelTwos:
            case MkRelOffset:
            case MkRelSigned:
            {
                if (cc != m.num) continue;
                int d = 0;
                if (m.kind == MkRelTwos) d = val < 64 ? val : val - 128;
                else if (m.kind == MkRelOffset) d = val - 64;
                else d = val < 64 ? val : -(val - 64);
                delta = (float)d / 127.f;
                rel = true;
                break;
            }
            default:
                continue;
            }
            used = true;
            const float lo = m.lo, hi = m.hi;
            float cur = get01(m.param);
            float target;
            if (m.beh == MbToggle)
            {
                bool press = rel ? delta > 0 : pos >= 0.5f;
                bool was = btn[i];
                btn[i] = press;
                if (!press || was)
                    continue;
                target = cur > 0.5f * (lo + hi) ? lo : hi;
            }
            else if (m.beh == MbMomentary)
            {
                bool press = rel ? delta > 0 : pos >= 0.5f;
                target = press ? hi : lo;
            }
            else if (rel)
            {
                float span = hi - lo;
                float inRange = span != 0.f ? (cur - lo) / span : 0.f;
                inRange = std::clamp(inRange + delta, 0.f, 1.f);
                target = lo + inRange * span;
            }
            else
            {
                target = lo + std::clamp(pos, 0.f, 1.f) * (hi - lo);
                if (m.pickup && !picked[i].load(std::memory_order_relaxed))
                {
                    // move only once the controller has come to (or passed) where the knob already is
                    float lastPos = last[i];
                    last[i] = target;
                    bool near = std::fabs(target - cur) < 0.03f;
                    bool crossed = lastPos >= 0.f && ((lastPos - cur) * (target - cur) <= 0.f);
                    if (!near && !crossed)
                        continue;
                    picked[i].store(true, std::memory_order_relaxed);
                }
            }
            put01(m.param, std::clamp(target, 0.f, 1.f));
        }
        return used;
    }
    // a parameter moved some other way (window, automation): pickup has to catch it again
    void unpick(int param)
    {
        for (int i = 0; i < kMax; ++i)
            if (get(i).valid && get(i).param == param)
                picked[i].store(false, std::memory_order_relaxed), last[i] = -1.f;
    }

  private:
    std::atomic<uint64_t> head[kMax]{};
    std::atomic<uint32_t> range[kMax]{};
    std::atomic<bool> picked[kMax]{};
    float last[kMax]{};
    bool btn[kMax]{};
    // after Learn, the first few messages of a plain CC mapping tell whether the control is an endless encoder
    // (it only ever sends small steps around 0/127, 64, or 0/64): then the mapping becomes that relative kind
    uint8_t probeN[kMax]{}, probeFlags[kMax]{};
    std::atomic<int> learnParam{-1}, learnKind{MkAbs7};
    struct Chan
    {
        int nrpnMsb{-1}, nrpnLsb{-1}, dataMsb{-1};
        uint8_t msb[32]{};
    } chan[16];
    uint8_t lsb[16][32]{};
};
