// Gullay engine checks: no allocation on the audio path, finite output under random settings,
// and how much of one core the heaviest layout takes at 48 kHz / 256 samples.
//   built and run by run-tests.sh
#include "gy_engine.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>

using namespace otiskanno::gullay;

static std::atomic<long> gAllocs{0};
static std::atomic<bool> gCount{false}, gCountInside{false};
void *operator new(std::size_t n)
{
    if (gCountInside.load(std::memory_order_relaxed))
        gAllocs.fetch_add(1);
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }

static int failures = 0;
static void check(bool ok, const char *what, const char *detail = "")
{
    std::printf("%-5s %-46s %s\n", ok ? "PASS" : "FAIL", what, detail);
    if (!ok)
        ++failures;
}

// runs seconds of noise bursts; returns peak (or -1 on a non-finite sample)
static double run(Engine &e, Settings &s, double seconds, std::mt19937 &rng, double startBeats = 0)
{
    std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    const int n = 256, N = (int)(seconds * 48000);
    std::vector<float> L(n), R(n), oL(n), oR(n);
    HostTime ht;
    ht.playing = true, ht.beatsValid = true, ht.bpm = 120;
    double pk = 0;
    for (int p = 0; p < N; p += n)
    {
        for (int i = 0; i < n; ++i)
        {
            float env = ((p + i) / 6000) % 8 == 0 ? 1.f : 0.f;
            L[i] = u(rng) * env, R[i] = u(rng) * env;
        }
        ht.beats = startBeats + p / 24000.0;
        e.setHostTime(ht);
        // count allocations only inside the engine (the test's own buffers above are not the audio path)
        bool counting = gCount.load();
        gCountInside = counting;
        e.process(L.data(), R.data(), oL.data(), oR.data(), n, s);
        gCountInside = false;
        for (int i = 0; i < n; ++i)
        {
            if (!std::isfinite(oL[i]) || !std::isfinite(oR[i]))
                return -1;
            pk = std::max({pk, (double)std::fabs(oL[i]), (double)std::fabs(oR[i])});
        }
    }
    return pk;
}

static void randomise(Settings &s, std::mt19937 &rng)
{
    std::uniform_real_distribution<float> u(0.f, 1.f);
    auto pickI = [&](int n) { return (int)(u(rng) * n * 0.9999f); };
    auto &G = s.gy;
    G.slot1 = pickI(2), G.slot2 = pickI(3) - 1, G.slot2Mode = pickI(3);
    G.dld16 = u(rng) < 0.5f, G.degradeBits = 11 + 5 * u(rng), G.pattern = pickI(24), G.morphSec = 0.01f + u(rng) * 3;
    G.tape = u(rng) < 0.3f, G.tapeDrive = u(rng), G.tapeSat = u(rng), G.tapeBias = u(rng), G.crushPlace = pickI(4), G.memRateHz = u(rng) < 0.5f ? 44100.f : 5000.f + 39000.f * u(rng);
    G.tapoModAmount = u(rng), G.tapoModRate = u(rng), G.balance = 2 * u(rng) - 1;
    G.answerDiv = pickI(6), G.answerChance = u(rng), G.mirrorRatio = 0.25f + 3.75f * u(rng);
    for (int k = 0; k < 3; ++k)
    {
        G.filterModel[k] = pickI((int)filterCatalogue().size()), G.filterVariant[k] = 0, G.filterPlace[k] = pickI(3);
        G.filterCut[k] = -40 + 120 * u(rng), G.filterReso[k] = u(rng), G.filterMorph[k] = u(rng);
    }
    G.tiltIn = 24 * u(rng) - 12, G.tiltOut = 24 * u(rng) - 12;
    G.tame = u(rng), G.ceilingDb = -36 + 30 * u(rng), G.loopLowHz = 10 + 1000 * u(rng), G.loopHighHz = 500 + 19000 * u(rng);
    G.hold = u(rng) < 0.2f, G.crushBits = 2 + 14 * u(rng), G.crushRateHz = 1000 + 47000 * u(rng), G.crushMix = u(rng);
    G.crushSmooth = u(rng) < 0.5f, G.chipMode = u(rng) < 0.3f, G.crushPlace = pickI(2);
    for (int b = 0; b < 3; ++b)
        G.shift[b] = pickI(25) - 12, G.shiftMode[b] = pickI(3);
    G.reverbOn = u(rng) < 0.6f, G.rv.flavour = pickI(2), G.rv.size = u(rng), G.rv.decay = u(rng), G.rv.freeze = u(rng) < 0.1f, G.rv.tuned = u(rng) < 0.3f;
    G.rvSendDb = -40 * u(rng), G.rvLevelDb = -30 * u(rng), G.rvIntoDelaysDb = -60 + 50 * u(rng);
    G.meander = u(rng), G.spread = u(rng), G.tide = u(rng) < 0.3f, G.freeRun = u(rng) < 0.2f;
    s.routing = pickI(2), s.splitMode = pickI(2), s.specQuality = pickI(3);
    for (auto &M : s.mode)
    {
        M.mix = u(rng);
        for (int b = 0; b < 3; ++b)
        {
            auto &B = M.band[b];
            B.cutoff = 96 * u(rng) - 48, B.resonance = 1.2f * u(rng), B.morph = u(rng), B.drive = u(rng) * u(rng);
            B.levelDb = -20 + 26 * u(rng), B.pan = 2 * u(rng) - 1, B.sync = u(rng) < 0.6f, B.inf = u(rng) < 0.1f, B.reverse = u(rng) < 0.2f;
        }
    }
    s.sway.amount = u(rng), s.conductor.influence = u(rng), s.mutation.mutate = u(rng);
    s.tune.on = u(rng) < 0.4f, s.tune.beforeStrips = u(rng) < 0.5f;
    s.gy.reOn = u(rng) < 0.5f, s.gy.reLevelDb[0] = -20 * u(rng), s.gy.reTone[2] = 24 * u(rng) - 12; // v0.8 Reshape
    s.gy.ancOn = u(rng) < 0.5f, s.gy.ancGens = 1 + pickI(3), s.gy.ancGrow = pickI(2), s.gy.ancFrom = pickI(3), s.gy.ancChance = u(rng), s.gy.ancSpread = u(rng); // v0.7 Ancestors
    for (int k = 0; k < 4; ++k)
        s.slot[k] = {1 + pickI(10), 1 + pickI(numModTargetIds - 1), 2 * u(rng) - 1, pickI(3)};
    for (int i = 0; i < 3; ++i)
        s.lfo[i].target = 1 + pickI(numModTargetIds - 1), s.lfo[i].amount = 2 * u(rng) - 1, s.lfo[i].shape = 1 + pickI(9);
}

int main()
{
    std::mt19937 rng(7);
    // 1. no allocation once running, through layout changes and every effect
    {
        auto *e = new Engine();
        e->setSampleRate(48000);
        auto *s = new Settings();
        run(*e, *s, 0.5, rng);
        gAllocs = 0, gCount = true;
        for (int k = 0; k < 12; ++k)
        {
            randomise(*s, rng);
            run(*e, *s, 0.4, rng, k * 2.0);
        }
        gCount = false;
        char d[64];
        std::snprintf(d, sizeof(d), "%ld allocations", gAllocs.load());
        check(gAllocs.load() == 0, "no allocation on the audio path (12 layouts)", d);
        delete s;
        delete e;
    }
    // 2. random settings: always finite, never past the limiter
    {
        auto *e = new Engine();
        e->setSampleRate(48000);
        auto *s = new Settings();
        int bad = 0;
        double worst = 0;
        for (int k = 0; k < 60; ++k)
        {
            randomise(*s, rng);
            double pk = run(*e, *s, 0.6, rng, k * 1.0);
            if (pk < 0 || pk > 1.0)
                ++bad;
            worst = std::max(worst, pk);
        }
        char d[64];
        std::snprintf(d, sizeof(d), "%d bad of 60, loudest %.3f", bad, worst);
        check(bad == 0, "60 random settings: finite and under 0 dBFS", d);
        float g[8];
        e->debugGains(g);
        std::snprintf(d, sizeof(d), "%g times", g[7]);
        check(g[7] == 0, "the safety net never had to step in", d);
        delete s;
        delete e;
    }
    // 3. the heaviest layout: two engines, spectral, Cloud Seed, every effect and all the movement
    {
        auto *e = new Engine();
        e->setSampleRate(48000);
        auto *s = new Settings();
        s->gy.slot1 = SlotTapo, s->gy.slot2 = SlotDld, s->routing = Parallel, s->splitMode = 1, s->specQuality = 1;
        s->gy.reverbOn = true, s->gy.rv.flavour = 1, s->gy.filterModel[0] = 3, s->gy.filterPlace[0] = FiltLoop, s->gy.filterModel[1] = 5, s->gy.filterPlace[1] = FiltLoop, s->gy.crushMix = 0.5f;
        s->gy.shiftMode[1] = ShiftEach, s->gy.shift[1] = 7, s->tune.on = true;
        s->sway.amount = 0.5f, s->conductor.influence = 0.5f, s->mutation.mutate = 0.5f;
        for (int i = 0; i < 3; ++i)
            s->lfo[i].target = 7 + i, s->lfo[i].amount = 0.3f, s->lfo[i].shape = 1;
        run(*e, *s, 0.5, rng);
        auto t0 = std::chrono::steady_clock::now();
        run(*e, *s, 10.0, rng);
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        char d[96];
        std::snprintf(d, sizeof(d), "%.1f %% of one core (this machine)", 100.0 * sec / 10.0);
        check(sec < 10.0 * 0.5, "heaviest layout runs well under real time", d);
        delete s;
        delete e;
    }
    std::printf("\n%s: %d failure%s\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
