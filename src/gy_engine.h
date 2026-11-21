/*
 * Three's Company - audio engine (phase 1)
 * Otis Kanno, 2026.
 *
 * Three filters, three drives, two ways to wire them:
 *   Parallel: a 3-band Linkwitz-Riley crossover; each band has its own filter and drive.
 *   Series:   Low strip -> Mid strip -> High strip on the full signal (the Mid strip sits
 *             in the middle of the chain).
 * Each routing keeps its own complete set of settings (Settings::mode[0] and mode[1]).
 *
 * The filter core is a port of
 * "Two Filters" by Paul Walker (BaconPaul), https://github.com/baconpaul/two-filters,
 * Copyright 2024-2026 Paul Walker and various authors. Taken from there, lightly reshaped:
 * the filter catalogue, the way sst-filters is set up and fed coefficients, the output
 * limiter, the resonance guard, the warm saturator and the non-finite recovery.
 *
 * The filters are the Surge Synth Team's sst-filters (GPL3), so this engine and anything
 * built from it is released under the GNU GPL v3. See LICENSE.
 *
 * Phase 1: crossover, three filters, per-band drive, guards and limiter, both routings,
 * smart band-edge limiting.
 * Phase 2: pan per strip, feedback and noise per routing, and three things shared
 * by both routings: Sway (slow LFOs that never line up), the Conductor (Mid passes its movement
 * on to Low and High) and Mutation (when something happens in Mid, Low and High answer, and the
 * answers drift over the bars). Host modulation still arrives through Settings::mod.
 * Phase 3: three sequencer lanes (bipolar steps, Euclid, swing, offset, Clock or Free) each with a
 * lock lane (P-locks), three LFOs (made shapes or a drawn curve), an envelope follower that can
 * listen to a sidechain, and a 16-slot mod matrix over all of them.
 */

#ifndef THREES_COMPANY_ENGINE_H
#define THREES_COMPANY_ENGINE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sst/filters++.h"
#include "gy_dld.h"
#include "gy_tapo.h"
#include "gy_tape.h"
#include "gy_fx.h"
#include "gy_reverb.h"

namespace otiskanno::gullay
{
namespace sfpp = sst::filtersplusplus;

static constexpr int blockSize{8}; // control rate: once every 8 host samples
static constexpr int numBands{3};
static constexpr int maxOsStages{2}; // 0 = off, 1 = 2x, 2 = 4x
enum BandIndex { Low = 0, Mid = 1, High = 2 };
enum Routing { Parallel = 0, Series = 1 };
enum DriveType { Warm = 0, Push, Bite, Fuzz, Fold, Rectify, Crush, numDriveTypes };

inline const char *driveName(int t)
{
    static const char *n[numDriveTypes] = {"Warm", "Push", "Bite", "Fuzz", "Fold", "Rectify", "Crush"};
    return n[std::clamp(t, 0, (int)numDriveTypes - 1)];
}

// ---------------------------------------------------------------------------
// Filter catalogue. Model 0 is "None".
// ---------------------------------------------------------------------------
enum struct PassKind { Open, LowPass, HighPass, BandPass };

struct ModelInfo
{
    sfpp::FilterModel model{sfpp::FilterModel::None};
    std::string name;
    std::vector<sfpp::ModelConfig> configs;
    std::vector<std::string> labels;
    std::vector<PassKind> kinds; // what the smart edge limiting treats each variant as
    bool zplane{false};          // our own morphing filter (not from sst-filters)
};

// ---------------------------------------------------------------------------
// Z-Plane, built the way the Morpheus manual describes its filter: one resonant low-pass section
// and six parametric EQ sections in series (14 poles), with whole filters stored as frames.
//   Morph       the realtime axis: moves from frame A to frame B. As in a Z-plane filter, it is
//               the poles and zeros that move (radius and angle in the z-plane), so peaks glide
//               from one frequency to the next instead of fading out and in.
//   Cutoff      Frequency Tracking: slides the whole shape up and down (designed at 1 kHz).
//   Resonance   Transform 2: the size and sharpness of the peaks and notches (the manual's cube
//               example), from flattened to sharper than designed.
// Unlike the Morpheus, where Frequency Tracking and Transform 2 are set at note-on, all three
// move in real time here.
// ---------------------------------------------------------------------------
static constexpr int zSections = 6, zStages = zSections + 1; // stage 0 is the low-pass
struct ZFrame
{
    float f[zSections], g[zSections], q[zSections]; // Hz at 1 kHz, dB, Q
    float lpF{0}, lpQ{0.707f};                      // the low-pass section: Hz at 1 kHz (0 = open), Q
};
struct ZDesign
{
    const char *name;
    ZFrame a, b;
};
inline const ZDesign *zDesigns(int &count)
{
    static const ZDesign d[] = {
        {"Vowel Ah > Ee", {{730, 1090, 2440, 3400, 150, 9000}, {14, 11, 8, 4, -6, -10}, {7, 8, 9, 9, 0.7f, 0.7f}},
                          {{270, 2290, 3010, 3700, 150, 9000}, {13, 12, 9, 4, -6, -10}, {7, 9, 10, 9, 0.7f, 0.7f}}},
        {"Vowel Oh > Ah", {{570, 840, 2410, 3300, 150, 9000}, {14, 11, 7, 3, -6, -12}, {7, 8, 9, 9, 0.7f, 0.7f}},
                          {{730, 1090, 2440, 3400, 150, 9000}, {14, 11, 8, 4, -6, -10}, {7, 8, 9, 9, 0.7f, 0.7f}}},
        {"Vowel Oo > Eh", {{300, 870, 2240, 3200, 150, 9000}, {15, 10, 6, 3, -6, -14}, {7, 8, 9, 9, 0.7f, 0.7f}},
                          {{530, 1840, 2480, 3500, 150, 9000}, {14, 12, 8, 4, -6, -10}, {7, 9, 9, 9, 0.7f, 0.7f}}},
        {"Phaser Notches", {{180, 520, 1500, 4300, 9000, 13000}, {-24, -24, -24, -24, -12, -6}, {2.5f, 2.5f, 2.5f, 2.5f, 2, 2}},
                           {{360, 1040, 3000, 8600, 12000, 15000}, {-24, -24, -24, -24, -12, -6}, {2.5f, 2.5f, 2.5f, 2.5f, 2, 2}}},
        {"Comb Shift", {{250, 500, 750, 1000, 1250, 1500}, {9, 9, 8, 8, 7, 7}, {14, 16, 18, 20, 22, 24}},
                       {{370, 740, 1110, 1480, 1850, 2220}, {9, 9, 8, 8, 7, 7}, {14, 16, 18, 20, 22, 24}}},
        {"Talking Bass", {{180, 420, 1200, 2600, 5200, 9000}, {15, -6, -12, -18, -24, -24}, {3, 1, 1, 1, 1, 1}},
                         {{900, 1600, 2800, 4200, 7000, 11000}, {14, 4, -6, -14, -20, -24}, {3, 1.5f, 1, 1, 1, 1}}},
        {"Bell Ring", {{300, 828, 1620, 2679, 4002, 5592}, {12, 10, 8, 7, 6, 5}, {25, 25, 25, 25, 25, 25}},
                      {{520, 1435, 2808, 4644, 6937, 9693}, {12, 10, 8, 7, 6, 5}, {25, 25, 25, 25, 25, 25}}},
        {"Hollow > Bright", {{400, 1000, 2500, 6000, 120, 12000}, {-18, -12, 6, 8, -6, 0}, {1.5f, 1.5f, 2, 2, 0.7f, 1}},
                            {{250, 700, 3500, 9000, 120, 12000}, {8, -14, -10, 10, 0, 6}, {1.5f, 1.5f, 2, 2, 0.7f, 1}}},
        // the manual's example: a low-pass filter morphing into a band-pass
        {"Lowpass > Bandpass", {{60, 150, 400, 1000, 2500, 6000}, {0, 0, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1}, 1200, 0.9f},
                               {{60, 150, 400, 1000, 2500, 6000}, {-24, -20, -12, 6, 0, 0}, {0.8f, 0.8f, 1, 2, 1, 1}, 1400, 4.f}},
        {"Resonant Sweep", {{500, 1000, 2000, 4000, 8000, 12000}, {0, 0, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1}, 250, 6.f},
                           {{500, 1000, 2000, 4000, 8000, 12000}, {0, 0, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1}, 6000, 6.f}},
        // after shapes described in the Morpheus manual's filter reference (frequencies from the descriptions)
        {"BroadRes", {{80, 160, 320, 2000, 6000, 10000}, {0, 0, 0, 0, 0, 0}, {3, 3, 3, 4, 4, 4}},
                     {{80, 160, 320, 2000, 6000, 10000}, {-24, -24, -24, 12, 12, 12}, {3, 3, 3, 4, 4, 4}}},
        {"Rubber Hose", {{47, 95, 190, 380, 750, 1500}, {-3, 2, 2, 2, 2, 2}, {4, 5, 5, 5, 5, 5}},
                        {{47, 95, 190, 380, 750, 1500}, {-24, 12, 12, 12, 11, 10}, {4, 5, 5, 5, 5, 5}}},
        {"Swingshift", {{349, 494, 698, 988, 1397, 1976}, {0, 0, 0, 0, 0, 0}, {8, 8, 8, 8, 8, 8}, 250, 0.7f},
                       {{349, 494, 698, 988, 1397, 1976}, {12, 12, 11, 10, 8, 6}, {8, 8, 8, 8, 8, 8}}},
        {"500 Up", {{500, 1000, 1500, 2000, 2500, 3000}, {3, -3, 3, -3, 3, -3}, {6, 6, 6, 6, 6, 6}},
                   {{500, 1000, 1500, 2000, 2500, 3000}, {10, -24, 10, -24, 10, -24}, {6, 6, 6, 6, 6, 6}}},
        {"Odd > Even", {{110, 330, 550, 770, 990, 1210}, {12, 12, 11, 10, 9, 8}, {12, 14, 16, 18, 20, 22}},
                       {{220, 440, 660, 880, 1100, 1320}, {12, 12, 11, 10, 9, 8}, {12, 14, 16, 18, 20, 22}}},
        {"Guitar Body", {{100, 210, 390, 800, 2200, 4800}, {9, 7, -6, 4, 3, -8}, {6, 8, 3, 4, 3, 1.5f}, 7000, 0.7f},
                        {{120, 260, 470, 1100, 2700, 5600}, {6, 10, -9, 7, 6, -4}, {5, 9, 4, 5, 4, 1.5f}, 9000, 0.7f}},
    };
    count = (int)(sizeof(d) / sizeof(d[0]));
    return d;
}

// Which part of the spectrum a filter type lets through. "Open" types (notch, allpass,
// peak, comb, phaser, shelves...) cannot mute a band, so they are never limited.
inline PassKind passKindOf(sfpp::FilterModel m, const sfpp::ModelConfig &c)
{
    using P = sfpp::Passband;
    switch (c.pt)
    {
    case P::LP:
    case P::NotchAndLP:
    case P::PhaserAndLP:
    case P::LowLowLow:
        return PassKind::LowPass;
    case P::HP:
    case P::HighHighHigh:
        return PassKind::HighPass;
    case P::BP:
    case P::LowHighLow:
    case P::HighLowHigh:
        return PassKind::BandPass;
    case P::UNSUPPORTED:
        // ladder models carry no passband tag: they are low-pass
        if (m == sfpp::FilterModel::VintageLadder || m == sfpp::FilterModel::DiodeLadder ||
            m == sfpp::FilterModel::VemberLadder || m == sfpp::FilterModel::OBXD_4Pole)
            return PassKind::LowPass;
        return PassKind::Open;
    default:
        return PassKind::Open;
    }
}

inline const std::vector<ModelInfo> &filterCatalogue()
{
    // Deliberately never destroyed (no static destructor order problems at unload).
    static const std::vector<ModelInfo> &cat = *new std::vector<ModelInfo>([]()
    {
        std::vector<ModelInfo> r;
        for (auto m : sfpp::Filter::availableModels())
        {
            ModelInfo mi;
            mi.model = m;
            mi.name = sfpp::toString(m);
            if (m != sfpp::FilterModel::None)
                mi.configs = sfpp::Filter::availableModelConfigurations(m, true);
            for (auto &c : mi.configs)
            {
                auto lab = c.toString();
                mi.labels.push_back(lab.empty() ? std::string("Default") : lab);
                mi.kinds.push_back(passKindOf(m, c));
            }
            r.push_back(std::move(mi));
        }
        if (r.empty() || r[0].model != sfpp::FilterModel::None)
        {
            ModelInfo none;
            none.name = "None";
            r.insert(r.begin(), none);
        }
        {
            ModelInfo z;
            z.name = "Z-Plane";
            z.zplane = true;
            int n = 0;
            const ZDesign *d = zDesigns(n);
            for (int i = 0; i < n; ++i)
                z.configs.push_back(sfpp::ModelConfig{}), z.labels.push_back(d[i].name), z.kinds.push_back(PassKind::Open);
            r.push_back(std::move(z));
        }
        return r;
    }());
    return cat;
}

inline int variantIndexFor(int modelIndex, float variant01)
{
    const auto &cat = filterCatalogue();
    if (modelIndex < 0 || modelIndex >= (int)cat.size())
        return 0;
    int n = (int)cat[(size_t)modelIndex].configs.size();
    if (n <= 1)
        return 0;
    int i = (int)std::lround(std::clamp(variant01, 0.f, 1.f) * (float)(n - 1));
    return std::clamp(i, 0, n - 1);
}

inline bool morphUsed(int modelIndex, int variantIndex)
{
    const auto &cat = filterCatalogue();
    if (modelIndex <= 0 || modelIndex >= (int)cat.size() || cat[(size_t)modelIndex].configs.empty())
        return false;
    const auto &mi = cat[(size_t)modelIndex];
    if (mi.zplane)
        return true;
    int v = std::clamp(variantIndex, 0, (int)mi.configs.size() - 1);
    return sfpp::Filter::coefficientsExtraCount(mi.model, mi.configs[(size_t)v]) > 0;
}

// ---------------------------------------------------------------------------
// Settings. Cutoffs are in semitones from A440 (-60 .. 70).
// ---------------------------------------------------------------------------
// A part (TC's strip, re-used). The six knobs keep TC's names so all of TC's modulation works
// on them unchanged; their meanings in Gullay:
//   cutoff    -> Time: semitones of time (12 = twice as long), 0 = one beat (A, B) / x1 (Lead)
//   resonance -> Feedback: 0..1.2 (A, B; above 1.0 is Runaway) / Lead: a master x0..1.2
//   morph     -> Tone: 0 dark .. 0.5 open .. 1 bright
//   drive     -> Drive: 0..1 = up to +21 dB on the repeats (inside the loop)
//   levelDb, pan
struct BandSettings
{
    float cutoff{0}, resonance{0.45f}, morph{0.5f}, drive{0}, levelDb{0}, pan{0};
    bool mute{false}, solo{false};
    bool sync{true};     // Time snaps to the DLD's ratios (1, 1.5, 2, 3 .. 16 and their inverses)
    bool inf{false};     // DLD: INF (hold and loop) / Tapo: Repeat
    bool reverse{false}; // DLD: reverse
    float stripMix{1};   // unused (kept for TC's code)
    float modDepth{1};   // 0 .. 2: how much the movement moves this part
};
static constexpr int numBankBands = 8;

// Offsets added on top of the knobs: host modulation (Bitwig), and inside the engine Sway,
// the Conductor and Mutation (later the sequencer lanes too).
struct BandMod
{
    float cutoff{0}, resonance{0}, morph{0}, drive{0}, levelDb{0}, pan{0};
};

struct ModeSettings
{
    BandSettings band[numBands];
    float xoverLoHz{250.f}, xoverHiHz{3000.f}; // Parallel only
    float inputDb{0}, outputDb{0};
    float mix{1}; // 0 dry .. 1 wet
    // feedback (low-passed, DC-blocked, soft-saturated, never runs away)
    bool feedbackOn{false};
    float feedback{0};       // 0..1
    float feedbackToneHz{6000};
    // noise, fed in before the filters so it rings with them
    bool noiseOn{false};
    float noise{0};      // 0..1 (cube law)
    float noiseColor{0}; // 0 white .. 1 dark
};

// Sway: four slow LFOs at rates that never line up (1 : golden ratio : root 2 : pi/4).
static constexpr float swayBarsTable[7] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f};
struct SwaySettings
{
    float amount{0};         // 0..1
    float rateHz{0.12f};     // Free: speed of the slowest LFO
    bool sync{false};        // Sync: one slow cycle per syncBars bars, locked to Bitwig's song position
    int syncIndex{4};        // into swayBarsTable (4 = 4 bars)
    bool stepped{false};     // Stepped: holds a value, then jumps (8 steps per slow cycle)
    float slew{0.4f};        // Stepped: how much of a step the jump takes (0 = hard)
    float follow{0};         // 0..1: louder input = more sway
    float depthSemis{12.f};  // cutoff movement at full Sway
    // what Sway moves (each 0..1): cutoff by depthSemis, the others over their own ranges
    float toCut{1.f}, toRes{0}, toMorph{0}, toDrive{0}, toLevel{0}, toPan{0};
    // each band's own feel (the main Sway knob still sets how much for all of them)
    struct Band
    {
        int motion{0};      // 0 = as the main setting, 1 Smooth, 2 Stepped
        float rateMul{1.f}; // its speed against the main rate (0.25 .. 4)
        float depth{1.f};   // 0..1: how much of the main Sway this band gets
        float slew{0.4f};   // Stepped: how much of a step the jump takes
    } band[numBands];
};

// The Conductor: Mid passes its own cutoff movement on to Low and High.
struct ConductorSettings
{
    float influence{0}; // 0..1: how much of Mid's movement is passed on
    float lean{0.5f};   // 0 all to Low .. 1 all to High
    // sporadic, but on the beat
    int grid{0};        // 0 = continuous, else 1 + index into condGridBeats: Mid's movement is passed on in steps
    int divLow{1}, divHigh{1}; // clock dividers: Low / High take a new value every Nth step
    float chaos{0};     // 0 = Mid's own movement .. 1 = random jumps (one per step)
    float range{12};    // semitones of the random jumps, bursts and third wheel
    float chance{1};    // chance that a step passes anything on
    int condition{0};   // Elektron-style bar conditions (see condConditions)
    float bursts{0};    // chance per bar of a burst: a beat of fast 1/32 jumps
    float mischief{0};  // Jack, the third wheel: now and then knocks a strip slightly out of whack
    int musical{0};     // jumps snap to: 0 off, 1 semitones, 2 fifths, 3 octaves
    int seed{1};
    // Jack: when one strip moves, another moves the opposite way
    float jack{0};      // 0..1
    int jackPair{0};    // see jackPairs
    int jackParam{0};   // which knob answers: cutoff, reso, morph, drive, level, pan
};
// 1/64, 1/32T, 1/32, 1/16T, 1/16, 1/16D, 1/8T, 1/8, 1/8D, 1/4T, 1/4, 1/4D, 1/2, 1 bar, 2 bars, 4 bars, 8 bars (quarter notes)
static constexpr int numCondGrid = 17;
static constexpr float condGridBeats[numCondGrid] = {0.0625f, 1.f / 12.f, 0.125f, 1.f / 6.f, 0.25f, 0.375f, 1.f / 3.f, 0.5f, 0.75f, 2.f / 3.f, 1.f, 1.5f, 2.f, 4.f, 8.f, 16.f, 32.f};
// conditions: {A, B} = act on bar A of every B bars ({0,0} always)
static constexpr int numCondConditions = 12;
static constexpr int condConditions[numCondConditions][2] = {{0, 0}, {1, 2}, {2, 2}, {1, 3}, {2, 3}, {3, 3}, {1, 4}, {2, 4}, {3, 4}, {4, 4}, {1, 8}, {8, 8}};

// Mutation: when something happens in Mid, Low and High answer.
enum MutTrigger { TrigLevel = 0, TrigCutoff, TrigGuard, TrigStep, TrigFollower, TrigChaperone, numMutTriggers };
enum MutMode { MutOff = 0, MutTandem, MutContrast };
enum MutTarget
{
    TgtCutoff = 0, TgtReso, TgtDrive, TgtLevel, TgtPan,
    // Gullay v0.3: more for the answers to move
    TgtMutTone, TgtMutPattern, TgtMutFilter, TgtMutCrush, TgtMutHymn, TgtMutShift, TgtMutDegrade, TgtMutTilt, TgtMutBand,
    numMutTargets
};
static constexpr int mutEveryTable[5] = {1, 2, 4, 8, 16};
struct MutResponse
{
    int mode{MutOff};
    int target{TgtCutoff};
    float amount{0.5f}; // 0..1 (cutoff 24 st, reso 0.5, drive 0.5, level 12 dB, pan 1)
    float chance{1.f};  // 0..1
    int delaySteps{0};  // 0..4 sixteenths
};
struct MutationSettings
{
    int trigger{TrigLevel};
    float sensitivity{0.5f};
    int holdSteps{1};     // at least this many sixteenths between events
    MutResponse resp[2];  // [0] Low, [1] High
    float mutate{0};      // 0..1: how far the answers drift each period
    int everyIndex{2};    // into mutEveryTable (bars)
    bool freeze{false};
    int seed{0};
};

// ---------------- Phase 3: modulation sources and the matrix ----------------
static constexpr int numLanes = 3, numSteps = 32, numLfos = 3, numSlots = 16;
static_assert(numSteps == 32, "GySettings::plockLen is sized for 32 steps");
enum ModSourceId { SrcNone = 0, SrcSeqLow, SrcSeqMid, SrcSeqHigh, SrcLfo1, SrcLfo2, SrcLfo3, SrcEnv, SrcRipLow, SrcRipMid, SrcRipHigh, numModSources };
// Targets: 0 none, then 6 per strip (Low 1-6, Mid 7-12, High 13-18), then the shared ones.
enum BandParamId { PCut = 0, PRes, PMorph, PDrive, PLevel, PPan, numBandParams };
enum ModTargetId
{
    TgtNone = 0,
    TgtSplitLo = 1 + numBands * numBandParams, // Spectral split point
    TgtSplitHi,     // Balance (Parallel / Spectral)
    TgtFeedback,    // Runaway Tame
    TgtNoise,       // Crusher Mix
    TgtMix,
    TgtSwayAmt,
    TgtModDepth,
    TgtSpecBalance, TgtSpecSharp,
    TgtStripDepthLow, TgtStripDepthMid, TgtStripDepthHigh, // each part's Mod Depth
    TgtTuneAmount, TgtTuneRing, TgtTuneTilt,
    TgtTuneSlope,
    // Gullay
    TgtFiltCut, TgtFiltRes, TgtFiltMorph,
    TgtTiltIn, TgtTiltOut,
    TgtPattern, TgtMorph, TgtDegrade,
    TgtCrushBits, TgtCrushRate,
    TgtRvSize, TgtRvDecay, TgtRvLevel, TgtRvMod, TgtRvSend,
    TgtShiftA, TgtShiftLead, TgtShiftB,
    TgtCeiling,
    TgtSpecXHi, TgtSpecLowLvl, TgtSpecMidLvl, TgtSpecHighLvl,
    TgtFiltCutA, TgtFiltResA, TgtFiltMorphA, TgtFiltCutB, TgtFiltResB, TgtFiltMorphB,
    numModTargetIds
};
constexpr int bandTarget(int band, int param) { return 1 + band * numBandParams + param; }
static constexpr float morphDivBeats[8] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f, 32.f};
// how far 100% moves each kind of target
inline float targetScale(int t)
{
    if (t >= 1 && t < TgtSplitLo)
    {
        switch ((t - 1) % numBandParams)
        {
        case PCut: return 24.f;   // semitones of time (two octaves)
        case PLevel: return 24.f; // dB
        default: return 1.f;      // feedback, tone, drive, pan: the whole range
        }
    }
    switch (t)
    {
    case TgtSplitLo: return 24.f;
    case TgtFiltCut: case TgtFiltCutA: case TgtFiltCutB: return 48.f;
    case TgtTiltIn: case TgtTiltOut: return 12.f;
    case TgtPattern: return 24.f;
    case TgtMorph: return 2.f;
    case TgtDegrade: return 5.f;
    case TgtCrushBits: return 14.f;
    case TgtCrushRate: return 48.f;
    case TgtRvLevel: case TgtRvSend: return 24.f;
    case TgtShiftA: case TgtShiftLead: case TgtShiftB: return 12.f;
    case TgtCeiling: return 30.f;
    case TgtSpecXHi: return 24.f;
    case TgtSpecLowLvl: case TgtSpecMidLvl: case TgtSpecHighLvl: return 24.f;
    default: return 1.f;
    }
}

// sequencer step lengths in quarter notes: 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2, 1 bar
static constexpr int numRippleSync = 14;
static constexpr float rippleSyncBeats[numRippleSync] = {0.0625f, 0.125f, 0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f, 1.f / 6.f, 1.f / 3.f, 2.f / 3.f, 0.75f, 1.5f};
static constexpr int numLaneRates = 20;
static constexpr float laneRateBeats[numLaneRates] = {0.125f, 1.f / 6.f, 0.25f, 1.f / 3.f, 0.5f, 2.f / 3.f, 1.f, 2.f, 4.f,
                                                      // added later (kept at the end so saved projects keep their rates)
                                                      0.0625f, 1.f / 12.f, 0.375f, 0.75f, 1.5f, 3.f, 8.f / 3.f, 0.1875f,
                                                      8.f, 16.f, 32.f}; // 2, 4, 8 bars: slow, evolving steps
// a sample-and-hold to a lower rate, then a 2-pole low-pass (Butterworth) as a sampler's output filter
// Sync on a part's Time: note values, straight, dotted (D) and triplet (T), in beats (a quarter note = 1)
struct NoteDiv { double beats; const char *name; };
static constexpr NoteDiv noteDivs[] = {
    {1. / 16, "1/64"}, {1. / 12, "1/32T"}, {1. / 8, "1/32"}, {1. / 6, "1/16T"}, {3. / 16, "1/32D"}, {1. / 4, "1/16"}, {1. / 3, "1/8T"},
    {3. / 8, "1/16D"}, {1. / 2, "1/8"}, {2. / 3, "1/4T"}, {3. / 4, "1/8D"}, {1., "1/4"}, {4. / 3, "1/2T"}, {1.5, "1/4D"}, {2., "1/2"},
    {8. / 3, "1/1T"}, {3., "1/2D"}, {4., "1 bar"}, {16. / 3, "2 bars T"}, {6., "1/1D"}, {8., "2 bars"}, {12., "3 bars"}, {16., "4 bars"}};
// Sync on Lead's Stretch: the stretches that keep A and B on note values
static constexpr NoteDiv leadDivs[] = {{1. / 16, "x1/16"}, {1. / 12, "x1/12"}, {1. / 8, "x1/8"}, {1. / 6, "x1/6"}, {3. / 16, "x3/16"},
    {1. / 4, "x1/4"}, {1. / 3, "x1/3"}, {3. / 8, "x3/8"}, {1. / 2, "x1/2"}, {2. / 3, "x2/3"}, {3. / 4, "x3/4"}, {1., "x1"}, {4. / 3, "x4/3"},
    {1.5, "x1.5"}, {2., "x2"}, {8. / 3, "x8/3"}, {3., "x3"}, {4., "x4"}, {16. / 3, "x16/3"}, {6., "x6"}, {8., "x8"}, {12., "x12"}, {16., "x16"}};
template <size_t N> inline const NoteDiv &nearestDiv(const NoteDiv (&t)[N], double r)
{
    size_t best = 0;
    for (size_t i = 1; i < N; ++i)
        if (std::fabs(std::log(t[i].beats / r)) < std::fabs(std::log(t[best].beats / r)))
            best = i;
    return t[best];
}
struct MemDecimator
{
    float ph{1.f}, hl{0}, hr{0};
    float b0{1}, b1{0}, b2{0}, a1{0}, a2{0}, z1[2]{}, z2[2]{};
    void reset() { ph = 1.f, hl = hr = 0, z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    void setLowpass(float fc, float sr)
    {
        float w = 2.f * 3.14159265f * std::clamp(fc, 20.f, 0.49f * sr) / sr, c = std::cos(w), al = std::sin(w) / (2.f * 0.70710678f), a0 = 1.f + al;
        b0 = (1.f - c) * 0.5f / a0, b1 = (1.f - c) / a0, b2 = b0, a1 = -2.f * c / a0, a2 = (1.f - al) / a0;
    }
    inline void tick(float &l, float &r, float step)
    {
        ph += step;
        if (ph >= 1.f)
            ph -= 1.f, hl = l, hr = r;
        float x[2] = {hl, hr};
        for (int c = 0; c < 2; ++c)
        {
            float y = b0 * x[c] + z1[c];
            z1[c] = b1 * x[c] - a1 * y + z2[c];
            z2[c] = b2 * x[c] - a2 * y;
            x[c] = y;
        }
        l = x[0], r = x[1];
    }
};
struct LaneSettings
{
    float step[numSteps]{}; // -1..1: down or up from the knob
    int length{16};         // 1..32
    int direction{0};       // 0 forward, 1 reverse, 2 ping-pong, 3 random (the same every time through)
    int rateIndex{2};       // into laneRateBeats
    int euclid{0};          // 0 Off, 1 Gate (rests are 0), 2 Trig (hits are +1)
    int hits{4}, rotate{0};
    float smooth{0};        // 0..1 of a step to glide
    float depth{1};         // scales the whole lane (its own target and the matrix)
    int target{TgtNone};    // the lane's own target
    bool freeRun{false};    // Clock (tempo, song position) or Free (speedHz)
    float speedHz{4};       // Free: steps per second
    float swing{0.5f};      // 0.5 straight .. 0.75
    float offset{0};        // -0.5..0.5 of a step, positive = later
    int lockTarget{TgtNone};
    float lockGlideMs{2};
    LaneSettings()
    {
        for (auto &r : plock)
            for (auto &v : r)
                v = std::numeric_limits<float>::quiet_NaN();
    }
    // Elektron-style parameter locks: per step, any of the band's six knobs (absolute values;
    // NaN = not locked). Locks ride this lane's steps (or its own length / step size below).
    float plock[numBandParams][numSteps];
    int lockLength{0};      // 0 = the sequencer's length, else 1..32
    int lockRateIndex{0};   // 0 = the sequencer's rate, else 1 + index into laneRateBeats
    float sway{0};          // 0..1: Sway pushes the timing ahead of and behind the grid (up to 4 steps)
};
enum Retrigger { RetrigSongPos = 0, RetrigBar, Retrig2Bars, Retrig4Bars, RetrigOnStart };

// LFO cycle lengths: 1/16, 1/8, 1/4, 1/2 (quarter notes), then 1, 2, 4, 8 bars
static constexpr float lfoSyncLen[8] = {0.25f, 0.5f, 1.f, 2.f, 1.f, 2.f, 4.f, 8.f};
static constexpr bool lfoSyncBars[8] = {false, false, false, false, true, true, true, true};
enum LfoShape { ShapeCustom = 0, ShapeSine, ShapeTriangle, ShapeRamp, ShapeSquare, ShapePump, ShapeSwell, ShapeBounce, ShapeStairs, ShapeSH, numShapes };
enum LfoMode { LfoSync = 0, LfoFree, LfoRetrig };
struct LfoSettings
{
    int shape{ShapeSine};
    int mode{LfoSync};
    int syncIndex{4};   // into lfoSyncLen (4 = 1 bar)
    float rateHz{1.f};  // Free
    float phase{0};     // 0..1
    float smooth{0};    // 0..1
    bool unipolar{false};
    float sway{0};      // 0..1: Sway (Mid's) pushes the phase ahead and behind (up to a quarter cycle)
    int target{0};      // its own destination (like a lane's), besides the matrix
    float amount{0};    // -1..1
    // the Steps shape (Hydrasynth-style step LFO, set like an arpeggio)
    float step[16]{};   // -1..1
    int stepCount{8};   // 1..16
    float glide{0};     // 0..1 of a step
    int quantize{0};    // 0 off, 1 semitones, 2 major, 3 minor, 4 pentatonic, 5 octaves (on a 24-semitone scale)
};
static constexpr int ShapeSteps = numShapes; // after the table shapes
struct CurvePoint
{
    float x, y, t; // x 0..1, y -1..1, t bends the segment that ends here (1 straight)
};
static constexpr int maxCurvePoints = 32;

enum EnvSource { EnvInput = 0, EnvSidechain, EnvLow, EnvMid, EnvHigh };
struct EnvSettings
{
    int source{EnvInput};
    float sensDb{6.f}, attackMs{5.f}, releaseMs{150.f};
    float scGainDb{0.f}, scHpHz{20.f}, scLpHz{20000.f};
};
struct ModSlot
{
    int source{SrcNone};
    int target{TgtNone};
    float amount{0}; // -1..1
    int swing{0};    // 0 as the source moves, 1 both ways (around the knob), 2 one way (up from the knob)
};

// Bitwig's transport, handed over at the start of every host block.
struct HostTime
{
    bool playing{false};
    bool beatsValid{false};
    double bpm{120};
    double beats{0}; // song position in quarter notes
    int num{4}, den{4};
};

// Tune: narrow resonators on the notes of a key, so the pitched part of whatever comes in rings out.
// Its own idea, after the Torso S-4's RING (a tuned filter bank); not a copy of it.
struct TuneSettings
{
    bool on{false};
    int root{9};             // 0 C .. 11 B
    int scale{10};           // see tuneScaleNotes (10 = Minor)
    bool beforeStrips{false};// true: rings first and the strips filter it; false: rings after the strips
    float amount{0.4f};      // 0..1
    bool layer{true};        // true Layer: the dry sound stays whole, the ringing goes on top; false Blend: 0 dry .. 1 only the ringing
    float ringMs{400};       // how long a note rings (time constant), 20..4000 ms
    int lowest{36}, highest{96}; // MIDI notes
    float tilt{0};           // -1 favour low notes .. +1 favour high
    float detuneCents{0};    // 0..50: each note pulled a little off pitch
    float width{0.5f};       // 0..1: alternate notes to the left and the right
    int motion{0};           // 0 Still, 1 Wave, 2 Drift
    float motionHz{0.25f};
    bool motionSync{false};  // true: Motion follows the song (motionDiv), false: free (motionHz)
    int motionDiv{9};        // into tuneDivBeats (9 = 1 bar)
    float slope{0};          // -1 darker .. +1 brighter: a tilt EQ on the ringing (the dry sound is left alone)
    float pivotHz{1000};     // where Slope pivots
};
// Motion's synced lengths: quarter notes, or (negative) bars
static constexpr int numTuneDivs = 14;
static constexpr double tuneDivBeats[numTuneDivs] = {0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0, -1, -2, -4, -8, -16};


// ----- Gullay: everything outside the three parts --------------------------------------
enum SlotType { SlotDld = 0, SlotTapo = 1 };
enum Slot2Mode { S2Engine = 0, S2CallResponse, S2Mirror, S2Stutter, S2Freeze, S2Octave }; // v0.5: Shadow's modes
enum FilterPlace { FiltIn = 0, FiltOut, FiltLoop };
enum CrushPlace { CrushLoop = 0, CrushOut, CrushIn, CrushBoth }; // Both = In and Out
enum ShiftMode { ShiftOff = 0, ShiftOnce, ShiftEach };
static constexpr int numAnswerDivs = 6;
static constexpr double answerDivBeats[numAnswerDivs] = {0.25, 0.5, 0.75, 1.0, 2.0, 4.0};
static constexpr int numRiverPass = 7;
static constexpr double riverPassBars[numRiverPass] = {0.25, 0.5, 1, 2, 4, 8, 16};
struct GySettings
{
    int slot1{SlotDld};
    int slot2{-1};          // -1 off (one engine), else SlotDld / SlotTapo
    int slot2Mode{S2Engine};// with one engine: part B is the engine's own, Call and Response, or Mirror
    bool dld16{true};       // the DLD's 16-bit memory (else clean float)
    bool tape{false};       // Tape: every repeat through CHOW Tape's hysteresis model
    float tapeDrive{0.5f}, tapeSat{0.5f}, tapeBias{0.5f};
    float degradeBits{16};  // 16..11 (16-bit mode)
    float memRateHz{44100}; // the memory's sample rate: 44.1 kHz (off) down to 5 kHz, both engines
    int pattern{0};         // Tapo pattern 0..23
    float morphSec{0.35f};  // Tapo morph between patterns
    bool morphSync{false};  // morph in note values instead of seconds
    int morphDiv{2};        // into morphDivBeats
    // the kill switch: clears every loop (and the reverb tail); auto kill after a runaway lasts killAfter s
    bool kill{false}, autoKill{true};
    float killAfterSec{30};
    // how far each mod source reaches, wherever it is used (Seq A, Lead, B, LFO 1-3, Follower, River A, Lead, B)
    float srcDepth[10]{1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    // Hymn's shape on the ringing: 0 off, 1 Gate (open while the sound is over Threshold), 2 Decay (each hit rings, then fades)
    int hymnShape{0};
    float hymnThreshDb{-30}, hymnHoldMs{30}, hymnDecayMs{250};
    // the Chaperone's lock-in: replays the last N steps it played, over and over
    bool chapLock{false};
    int chapLockSteps{16};
    // P-locks: how many steps each step's locks last (Elektron style), per lane
    int plockLen[3][32]{};
    // Hymn: only the ringing (no dry sound, no unringing echoes) when Tune is on, at Amount 100 %
    bool tuneOnly{false};
    // the spectral split: two split points (semitones re 440 Hz) and the three bands' levels (dB)
    float specLoSemi{-9.79f}, specHiSemi{33.24f}, specLvlDb[3]{0, 0, 0};
    float tapoModAmount{0.f}, tapoModRate{0.3f}, tapoVelocity{0.3f};
    // slot 2's Tapographic, when slot 2 is a Tapo too (slot 1's settings are the ones above)
    struct Tapo2
    {
        int pattern{0};
        float morphSec{0.35f};
        bool morphSync{false};
        int morphDiv{2};
        float modAmount{0.f}, modRate{0.3f}, velocity{0.3f};
    } tapo2;
    float balance{0};       // Parallel / Spectral: -1 slot 1 .. +1 slot 2
    // Call and Response / Mirror
    int answerDiv{1};
    float answerChance{1}, mirrorRatio{1};
    // filter (TC's models), tilts
    // one filter per path: 0 A, 1 Lead, 2 B. Lead's is the old shared one: In before everything, Out on all
    // the echoes, Loop in every repeat. A's and B's: In before their engine, Out on their echoes, Loop in their repeats.
    int filterModel[3]{0, 0, 0}, filterVariant[3]{0, 0, 0}, filterPlace[3]{FiltOut, FiltOut, FiltOut};
    float filterSend[3]{1, 1, 1}, filterMix[3]{1, 1, 1}; // v0.8: how much goes into each filter; how much of its result you hear
    float filterCut[3]{24, 24, 24}, filterReso[3]{0.2f, 0.2f, 0.2f}, filterMorph[3]{0, 0, 0};
    float tiltIn{0}, pivotIn{800}, tiltOut{0}, pivotOut{1600};
    // Runaway
    float tame{0.6f}, ceilingDb{-12}, loopLowHz{80}, loopHighHz{9000}, returnSec{1.2f};
    bool hold{false}, dldGlide{true};
    // crusher
    float crushBits{16}, crushRateHz{48000}, crushMix{0};
    bool crushSmooth{false}, chipMode{false};
    int crushPlace{CrushOut};
    float memTilt{0}, memNoise{0};
    // v0.5: Hymn's envelope, Varispeed, the Chain's Cross and Wire, Oscillate, the MIDI mod sources
    float hymnAttackMs{5}, hymnSustain{0.6f}, hymnReleaseMs{400};
    bool varispeed{false};
    float variGlideMs{300};
    float chainCross{0}, wireTone{0}, wireDrive{0};
    bool oscillate{false}, oscCut{false};
    float modWheel{0}, velocity{0};
    // v0.7 Ancestors: a family tree of echoes. Each echo has two children (one sooner, one later), each child two
    // more, up to three generations (14 branches), fanned across the stereo field and darker with each generation
    bool ancOn{false};
    int ancGens{2}, ancGrow{0}, ancFrom{0}; // generations 1..3; grow 0 = always, 1 = on Lead's calls; from 0 = all echoes, 1 = A, 2 = B
    float ancSpread{0.5f}, ancFan{0.6f}, ancFade{0.5f}, ancDarken{0.4f}, ancChance{1.f}, ancLevelDb{0.f};
    // v0.8 Reshape: the echoes split into Low, Mid and High after the engines (and after Lead), each with its own level,
    // pan and tone, then summed. Frequency crossovers (no latency), the same as Split's Freq mode.
    bool reOn{false};
    float reLoHz{250.f}, reHiHz{2500.f}, reLevelDb[3]{0, 0, 0}, rePan[3]{0, 0, 0}, reTone[3]{0, 0, 0};
    // Hymn: shifts per part (the key and Tune are TC's Tune settings)
    int shift[3]{0, 0, 0}, shiftMode[3]{ShiftOff, ShiftOff, ShiftOff};
    bool settle{true};
    // reverb
    bool reverbOn{false};
    GyReverbSettings rv;
    float rvSendDb{-12}, rvLevelDb{-6}, rvIntoDelaysDb{-60};
    bool rvFollowKey{false};
    // the River (the three currents: TC's Ripple sources)
    int riverPass{4};       // into riverPassBars (4 = 4 bars per pass down the river)
    bool riverSync{true};
    float riverHz{0.1f}, meander{0.7f}, spread{0.25f};
    bool tide{false};
    int tideBars{8};
    // transport
    bool freeRun{false};    // Free: own tempo, ignores the host's song position
    float freeBpm{120};
    bool freePlay{true};
};

struct Settings
{
    int routing{Parallel};
    int splitMode{0};      // Parallel only: 0 crossover filters, 1 spectral (FFT)
    int specBy{0};         // spectral: 0 by frequency, 1 tonal (tones / hits / air)
    float specBalance{0};  // -1..1
    float specSharp{0.6f}; // 0..1
    int specQuality{1};    // FFT frame at the host rate: 0 1024, 1 2048, 2 4096, 3 8192
    ModeSettings mode[2]; // [Parallel], [Series]: each routing keeps its own settings
    BandMod mod[numBands];
    int oversample{1};     // 0 off, 1 = 2x, 2 = 4x
    float modDepth{1};     // 0 .. 2: master depth for everything that moves (P-locks excepted)
    bool smartEdges{true}; // keep a filter from muting its own band / the chain
    bool autoGain{true};   // level-match the drives
    bool limiter{true};    // guards + output limiter
    SwaySettings sway;
    ConductorSettings conductor;
    MutationSettings mutation;
    LaneSettings lane[numLanes];
    int retrigger{RetrigSongPos};
    LfoSettings lfo[numLfos];
    EnvSettings env;
    ModSlot slot[numSlots];
    TuneSettings tune;
    GySettings gy;
};

// Builds a 512-point table from curve points (shared by the made shapes and drawn curves).
static constexpr int curveN = 512;
inline void renderCurve(const CurvePoint *p, int n, float *table)
{
    if (n < 2)
    {
        for (int i = 0; i <= curveN; ++i)
            table[i] = n == 1 ? p[0].y : 0.f;
        return;
    }
    int k = 0;
    for (int i = 0; i <= curveN; ++i)
    {
        float x = (float)i / curveN;
        while (k < n - 2 && x > p[k + 1].x)
            ++k;
        const auto &a = p[k], &b = p[k + 1];
        float w = b.x - a.x;
        float u = w > 1.0e-6f ? std::clamp((x - a.x) / w, 0.f, 1.f) : 1.f;
        float t = std::clamp(b.t, 0.05f, 20.f);
        table[i] = std::clamp(a.y + (b.y - a.y) * std::pow(u, t), -1.f, 1.f);
    }
}
inline void shapeTable(int shape, float *table)
{
    auto pts = [&](std::initializer_list<CurvePoint> l) { renderCurve(l.begin(), (int)l.size(), table); };
    switch (shape)
    {
    case ShapeSine:
        for (int i = 0; i <= curveN; ++i)
            table[i] = (float)std::sin(2.0 * 3.14159265358979 * i / curveN);
        break;
    case ShapeTriangle: pts({{0, 0, 1}, {0.25f, 1, 1}, {0.75f, -1, 1}, {1, 0, 1}}); break;
    case ShapeRamp: pts({{0, -1, 1}, {1, 1, 1}}); break;
    case ShapeSquare: pts({{0, 1, 1}, {0.5f, 1, 1}, {0.5f, -1, 1}, {1, -1, 1}}); break;
    case ShapePump: pts({{0, -1, 1}, {0.85f, 1, 0.35f}, {1, 1, 1}}); break;
    case ShapeSwell: pts({{0, -1, 1}, {0.9f, 1, 3}, {0.9f, -1, 1}, {1, -1, 1}}); break;
    case ShapeBounce:
        pts({{0, 1, 1}, {0.28f, -1, 2}, {0.46f, 0.35f, 0.5f}, {0.62f, -1, 2}, {0.74f, -0.35f, 0.5f}, {0.84f, -1, 2},
             {0.92f, -0.75f, 0.5f}, {1, -1, 2}});
        break;
    case ShapeStairs: pts({{0, -1, 1}, {0.25f, -1, 1}, {0.25f, -0.33f, 1}, {0.5f, -0.33f, 1}, {0.5f, 0.33f, 1}, {0.75f, 0.33f, 1}, {0.75f, 1, 1}, {1, 1, 1}}); break;
    case ShapeSH:
    {
        static constexpr float v[8] = {0.42f, -0.81f, 0.13f, 0.95f, -0.37f, 0.66f, -0.58f, -0.12f};
        for (int i = 0; i <= curveN; ++i)
            table[i] = v[std::min(7, i * 8 / curveN)];
        break;
    }
    default: // the starting drawn curve
        pts({{0, 0, 1}, {0.12f, 0.9f, 0.5f}, {0.3f, 0.2f, 2}, {0.42f, 0.6f, 1}, {0.62f, -0.8f, 0.45f}, {0.8f, -0.2f, 2.2f}, {1, 0, 1}});
        break;
    }
}

static inline float hzToSemi(double hz) { return (float)(12.0 * std::log2(hz / 440.0)); }
static inline double semiToHz(double st) { return 440.0 * std::pow(2.0, st / 12.0); }
static inline float dbToLin(float db) { return db <= -60.f ? 0.f : std::pow(10.f, db / 20.f); }

// Smooth "eases against a floor": above floor+K the value is untouched, below floor-K it
// rests on the floor, and in between it bends over with no corner.
static inline float softFloor(float x, float fl, float K)
{
    if (x >= fl + K)
        return x;
    if (x <= fl - K)
        return fl;
    float d = fl + K - x;
    return x + d * d / (4.f * K);
}
static inline float softCeil(float x, float c, float K) { return -softFloor(-x, -c, K); }

// ---------------------------------------------------------------------------
// Spectral split (Parallel's other way of splitting): a short-time FFT divides the sound into
// three parts, either by frequency (steep, no phase shift) or by character (Tonal: steady tones,
// hits, and the noisy air in between). The three masks always add up to one, so with the strips
// left alone the sound comes back whole, one FFT frame later.
//   frame N = 2048 at the host rate (more when oversampled), hop N/4, Hann in and out.
//   Left and right ride in one complex FFT (l + i r); a real, symmetric mask keeps them apart.
// ---------------------------------------------------------------------------
struct SpectralSplit
{
    static constexpr int maxN = 32768, histT = 8; // up to 8192 at the host rate, x4 when oversampled
    int N{2048}, hop{512}, bins{1025};
    float sr{48000};
    // settings, from the control thread
    int by{0};                 // 0 frequency, 1 tonal
    float f1{250}, f2{3000};   // split points (Hz) for frequency mode
    float balance{0};          // -1..1: tonal mode favours hits .. tones; frequency mode moves both splits
    float sharp{0.6f};         // 0 soft .. 1 steep (frequency) / clean separation (tonal)
    // state
    float inL[maxN]{}, inR[maxN]{};          // the last N inputs (ring)
    float dryL[maxN]{}, dryR[maxN]{};        // the dry signal, N late (ring)
    float acc[3][2][2 * maxN]{};             // overlap-add per band, per channel
    float ready[3][2][maxN / 4]{};           // the hop being played out
    float re[maxN]{}, im[maxN]{}, yr[maxN]{}, yi[maxN]{};
    float win[maxN]{}, twr[maxN / 2]{}, twi[maxN / 2]{};
    int rev[maxN]{};
    float mag[maxN / 2 + 1]{}, hist[histT][maxN / 2 + 1]{}, mask[3][maxN / 2 + 1]{};
    int w{0}, filled{0}, rd{0}, histPos{0}, frames{0};

    int hostN{2048};
    void setup(double rate, int osf, int frameAtHost)
    {
        sr = (float)rate;
        hostN = frameAtHost;
        N = std::min(maxN, frameAtHost * std::max(1, osf));
        hop = N / 4, bins = N / 2 + 1;
        for (int i = 0; i < N; ++i)
            win[i] = 0.5f - 0.5f * std::cos(6.283185307f * (float)i / (float)N); // periodic Hann
        int lg = 0;
        while ((1 << lg) < N)
            ++lg;
        for (int i = 0; i < N; ++i)
        {
            int r = 0;
            for (int b = 0; b < lg; ++b)
                r |= ((i >> b) & 1) << (lg - 1 - b);
            rev[i] = r;
        }
        for (int i = 0; i < N / 2; ++i)
            twr[i] = std::cos(6.283185307f * (float)i / (float)N), twi[i] = -std::sin(6.283185307f * (float)i / (float)N);
        reset();
    }
    void reset()
    {
        std::memset(inL, 0, sizeof(inL)), std::memset(inR, 0, sizeof(inR));
        std::memset(dryL, 0, sizeof(dryL)), std::memset(dryR, 0, sizeof(dryR));
        std::memset(acc, 0, sizeof(acc)), std::memset(ready, 0, sizeof(ready));
        std::memset(hist, 0, sizeof(hist)), std::memset(mask, 0, sizeof(mask));
        w = filled = rd = histPos = frames = 0;
    }
    int latency() const { return N; }
    // in-place radix-2 FFT (inverse: conjugate twiddles, no scaling)
    void fft(float *xr, float *xi, bool inverse)
    {
        for (int i = 0; i < N; ++i)
            if (rev[i] > i)
                std::swap(xr[i], xr[rev[i]]), std::swap(xi[i], xi[rev[i]]);
        for (int len = 2; len <= N; len <<= 1)
        {
            int half = len >> 1, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < half; ++j)
                {
                    float wr = twr[j * step], wi = inverse ? -twi[j * step] : twi[j * step];
                    int a = i + j, b = a + half;
                    float tr = xr[b] * wr - xi[b] * wi, ti = xr[b] * wi + xi[b] * wr;
                    xr[b] = xr[a] - tr, xi[b] = xi[a] - ti;
                    xr[a] += tr, xi[a] += ti;
                }
        }
    }
    static float smooth01(float x) { x = std::clamp(x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
    void makeMasks()
    {
        if (by == 0)
        {
            // frequency: soft-edged bands, from half an octave wide (soft) to a twentieth (steep)
            float width = 0.5f - 0.45f * std::clamp(sharp, 0.f, 1.f);
            float shift = std::exp2(std::clamp(balance, -1.f, 1.f)); // Balance slides both splits up to an octave
            float a = f1 * shift, b = f2 * shift;
            for (int k = 0; k < bins; ++k)
            {
                float f = std::max(1.f, (float)k * sr / (float)N);
                float hiA = smooth01(std::log2(f / a) / width + 0.5f), hiB = smooth01(std::log2(f / b) / width + 0.5f);
                mask[0][k] = 1.f - hiA, mask[2][k] = hiB, mask[1][k] = std::max(0.f, hiA - hiB);
            }
            return;
        }
        // tonal: steady tones are the median over time; hits the median across frequency
        float med[histT];
        float bal = std::exp2(2.f * std::clamp(balance, -1.f, 1.f)), sep = 1.f + 3.f * std::clamp(sharp, 0.f, 1.f);
        int nh = std::min(frames, histT);
        for (int k = 0; k < bins; ++k)
        {
            for (int t = 0; t < nh; ++t)
                med[t] = hist[t][k];
            float H = 0.f;
            if (nh > 0)
            {
                std::nth_element(med, med + nh / 2, med + nh);
                H = med[nh / 2];
            }
            float win17[17];
            int c = 0;
            for (int j = std::max(0, k - 8); j <= std::min(bins - 1, k + 8); ++j)
                win17[c++] = mag[j];
            std::nth_element(win17, win17 + c / 2, win17 + c);
            float P = win17[c / 2];
            float h2 = bal * H * H, p2 = P * P / bal;
            float wh = h2 / (h2 + p2 + 1e-18f), wp = 1.f - wh;
            float wn = std::pow(std::clamp(1.f - std::fabs(wh - wp), 0.f, 1.f), sep);
            float m0 = wh * (1.f - wn), m1 = wp * (1.f - wn), m2 = wn;
            // a little smoothing from frame to frame keeps it from warbling
            mask[0][k] += 0.55f * (m0 - mask[0][k]);
            mask[1][k] += 0.55f * (m1 - mask[1][k]);
            mask[2][k] = 1.f - mask[0][k] - mask[1][k];
        }
    }
    void frame()
    {
        for (int n = 0; n < N; ++n)
        {
            int i = (w + n) % N; // oldest first
            re[n] = inL[i] * win[n], im[n] = inR[i] * win[n];
        }
        fft(re, im, false);
        // each channel's magnitude from the packed spectrum
        for (int k = 0; k < bins; ++k)
        {
            int nk = (N - k) % N;
            float lr = 0.5f * (re[k] + re[nk]), li = 0.5f * (im[k] - im[nk]);
            float rr = 0.5f * (im[k] + im[nk]), ri = -0.5f * (re[k] - re[nk]);
            mag[k] = std::sqrt(lr * lr + li * li) + std::sqrt(rr * rr + ri * ri);
        }
        if (by == 1)
        {
            std::memcpy(hist[histPos], mag, sizeof(float) * (size_t)bins);
            histPos = (histPos + 1) % histT;
            frames = std::min(frames + 1, histT);
        }
        makeMasks();
        const float norm = 1.f / (1.5f * (float)N); // Hann in and out at 75% overlap sums to 1.5
        for (int b = 0; b < 3; ++b)
        {
            for (int k = 0; k < N; ++k)
            {
                int kk = k <= N / 2 ? k : N - k;
                yr[k] = re[k] * mask[b][kk], yi[k] = im[k] * mask[b][kk];
            }
            fft(yr, yi, true);
            for (int n = 0; n < N; ++n)
            {
                acc[b][0][n] += yr[n] * win[n] * norm;
                acc[b][1][n] += yi[n] * win[n] * norm;
            }
            for (int c = 0; c < 2; ++c)
            {
                std::memcpy(ready[b][c], acc[b][c], sizeof(float) * (size_t)hop);
                std::memmove(acc[b][c], acc[b][c] + hop, sizeof(float) * (size_t)(N - hop));
                std::memset(acc[b][c] + N - hop, 0, sizeof(float) * (size_t)hop);
            }
        }
        rd = 0;
    }
    // one sample in; the three bands and the dry signal out, N samples late
    inline void tick(float l, float r, float dryInL, float dryInR, float bl[3], float br[3], float &dl, float &dr)
    {
        for (int b = 0; b < 3; ++b)
            bl[b] = ready[b][0][rd], br[b] = ready[b][1][rd];
        dl = dryL[w], dr = dryR[w];
        dryL[w] = dryInL, dryR[w] = dryInR;
        inL[w] = l, inR[w] = r;
        w = (w + 1) % N;
        ++rd;
        if (++filled >= hop)
        {
            filled = 0;
            frame();
        }
    }
};

// ---------------------------------------------------------------------------
// 3-band Linkwitz-Riley (24 dB/oct) crossover that sums back flat.
// TPT state-variable sections (Butterworth Q), safe to sweep.
//   low  = AP(f2) . LP4(f1)
//   mid  = LP4(f2) . HP4(f1)
//   high = HP4(f2) . HP4(f1)
//   low + mid + high = AP(f2) . AP(f1): flat magnitude. dry() gives that same phase to the
//   dry signal so Mix never combs.
// ---------------------------------------------------------------------------
struct Crossover
{
    struct SVF
    {
        static constexpr double k{1.4142135623730951};
        double g{0}, a1{0}, a2{0}, a3{0};
        double ic1[2]{}, ic2[2]{};
        void set(double fc, double sr)
        {
            fc = std::clamp(fc, 10.0, sr * 0.45);
            g = std::tan(3.14159265358979323846 * fc / sr);
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void coefsFrom(const SVF &o) { g = o.g, a1 = o.a1, a2 = o.a2, a3 = o.a3; }
        inline void tick(int ch, double x, double &lp, double &bp, double &hp)
        {
            double v3 = x + 1.0e-20 - ic2[ch]; // the tiny offset keeps the states out of denormals
            double v1 = a1 * ic1[ch] + a2 * v3;
            double v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.0 * v1 - ic1[ch];
            ic2[ch] = 2.0 * v2 - ic2[ch];
            lp = v2;
            bp = v1;
            hp = x - k * v1 - v2;
        }
        inline double allpass(int ch, double x)
        {
            double lp, bp, hp;
            tick(ch, x, lp, bp, hp);
            return x - 2.0 * k * bp;
        }
        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
    };
    SVF s1, s1lp, s1hp, s2, s2lp, s2hp, apLow, apDry1, apDry2;

    void set(double f1, double f2, double sr)
    {
        s1.set(f1, sr);
        s1lp.coefsFrom(s1);
        s1hp.coefsFrom(s1);
        apDry1.coefsFrom(s1);
        s2.set(f2, sr);
        s2lp.coefsFrom(s2);
        s2hp.coefsFrom(s2);
        apLow.coefsFrom(s2);
        apDry2.coefsFrom(s2);
    }
    inline void split(int ch, double x, double &low, double &mid, double &high)
    {
        double lp, bp, hp, t, l4, h4;
        s1.tick(ch, x, lp, bp, hp);
        s1lp.tick(ch, lp, l4, bp, t);
        s1hp.tick(ch, hp, t, bp, h4);
        double lp2, hp2;
        s2.tick(ch, h4, lp2, bp, hp2);
        s2lp.tick(ch, lp2, mid, bp, t);
        s2hp.tick(ch, hp2, t, bp, high);
        low = apLow.allpass(ch, l4);
    }
    inline double dry(int ch, double x) { return apDry2.allpass(ch, apDry1.allpass(ch, x)); }
    void reset()
    {
        for (auto *s : {&s1, &s1lp, &s1hp, &s2, &s2lp, &s2hp, &apLow, &apDry1, &apDry2})
            s->reset();
    }
};

// ---------------------------------------------------------------------------
// Resonance guard. Compares a level with a reference
// level (held ~1 s). When the output rises more than `boost` above the reference, or keeps
// ringing with nothing coming in, it is turned down fast (~2 ms) and let back up slowly
// (~150 ms). Anything under the floor (-18 dBFS RMS) is left alone.
// ---------------------------------------------------------------------------
struct Guard
{
    static constexpr float floorRms{0.126f};
    static constexpr float kneeDb{6.f};
    float ref2{0}, out2{0}, gain{1}, gmin{1};
    float refAtt{0}, refRel{0}, outC{0}, down{0}, up{0};
    void setRate(double sr)
    {
        auto c = [sr](float ms) { return 1.f - std::exp(-1.f / (ms * 0.001f * (float)sr)); };
        refAtt = c(10.f);
        refRel = c(1000.f);
        outC = c(30.f);
        down = c(2.f);
        up = c(150.f);
    }
    void reset() { ref2 = out2 = 0.f, gain = 1.f; }
    inline void process(float refMS, float boost, float &l, float &r)
    {
        ref2 += (refMS > ref2 ? refAtt : refRel) * (refMS - ref2);
        float o = 0.5f * (l * l + r * r);
        out2 += outC * (o - out2);
        if (!std::isfinite(ref2) || !std::isfinite(out2))
            ref2 = out2 = 0.f;
        if (ref2 < 1.0e-12f)
            ref2 = 0.f;
        if (out2 < 1.0e-12f)
            out2 = 0.f;
        float allowed = std::max(std::sqrt(ref2) * boost, floorRms);
        float outLv = std::sqrt(out2);
        float target = 1.f;
        if (outLv > allowed * 0.7079f) // only do the dB maths inside or above the knee
        {
            float over = 20.f * std::log10(outLv / allowed);
            float red = 0.f;
            if (over > -0.5f * kneeDb && over < 0.5f * kneeDb)
                red = (over + 0.5f * kneeDb) * (over + 0.5f * kneeDb) / (2.f * kneeDb);
            else if (over >= 0.5f * kneeDb)
                red = over;
            if (red > 0.f)
                target = std::pow(10.f, -red / 20.f);
        }
        gain += (target < gain ? down : up) * (target - gain);
        gmin = std::min(gmin, gain);
        l *= gain;
        r *= gain;
    }
};

// ---------------------------------------------------------------------------
// 2x up / down sampler: a polyphase half-band IIR (two chains of six all-pass sections,
// about 100 dB of rejection, pass band flat to 0.49 of the lower Nyquist).
//
// Why not sst-filters' HalfRateFilter: its per-sample down-sampler
// (process_sample_D2) returns one all-pass chain only, so it never low-passes before dropping
// samples and the aliasing comes straight back. Measured with a fuzzed 5 kHz sine, the
// loudest alias sat at -17 dB with oversampling off, 2x and 4x alike. This one is tested in
// tests/tc_tests.cpp ("oversampling cuts aliasing").
// ---------------------------------------------------------------------------
// (HalfBand now lives in gy_fx.h, so the loop's drive can use it too)

// Feedback loop conditioning: the fed-back signal is low-passed,
// DC-blocked and soft-saturated, and the loop gain tops out at 0.2, so it warms up and never
// runs away.
struct FbConditioner
{
    float lp{0}, x1{0}, y1{0};
    float process(float x, float lpCoef)
    {
        lp += lpCoef * (x - lp);
        float y = lp - x1 + 0.995f * y1;
        x1 = lp;
        y1 = std::fabs(y) < 1.0e-20f ? 0.f : y;
        return y;
    }
    void reset() { lp = x1 = y1 = 0; }
};

// Deterministic random numbers: the same seed and the same count always give the same value,
// so a mutated arrangement plays back (and bounces) the same every time.
static inline uint64_t mix64(uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static inline float rand01(uint64_t a, uint64_t b, uint64_t c)
{
    return (float)((mix64(a * 0x100000001B3ull ^ mix64(b * 31 + c)) >> 40) / 16777216.0);
}

// A value that glides in a straight line to its target over one control block.
// A linear ramp that stops at its target (it never runs past it, however often it is ticked).
struct Ramp
{
    float v{0}, d{0};
    int n{0};
    void to(float target, int steps)
    {
        n = std::max(1, steps);
        d = (target - v) / (float)n;
    }
    void snap(float x) { v = x, d = 0, n = 0; }
    inline float tick()
    {
        float r = v;
        if (n > 0)
        {
            v += d;
            if (--n == 0)
                d = 0;
        }
        return r;
    }
};

// the notes of each scale (semitones above the root); Tune's scale list, in the order the window shows it
// First the root with one interval (it rings in every octave of the range), then two chords, then the scales.
// "Down" means the interval below the root: a fifth down from A is D (the same notes as a fourth up).
static constexpr int numTuneScales = 17;
inline const std::vector<int> &tuneScaleNotes(int scale)
{
    static const std::vector<int> sc[numTuneScales] = {
        {0},          // Octaves
        {0, 7},       // Fifths up
        {0, 5},       // Fifths down
        {0, 4},       // Major 3rds up
        {0, 8},       // Major 3rds down
        {0, 3},       // Minor 3rds up
        {0, 9},       // Minor 3rds down
        {0, 4, 7},    // Major chord
        {0, 3, 7},    // Minor chord
        {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 10},
        {0, 3, 5, 7, 10}, {0, 3, 5, 6, 7, 10}, {0, 2, 4, 6, 8, 10}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
    return sc[std::clamp(scale, 0, numTuneScales - 1)];
}
// ----- Tune: up to 85 resonators (Chromatic, C1..C8), each a band-pass state-variable filter (stable when moved)
class TuneBank
{
  public:
    static constexpr int maxNotes = 85;
    void setRate(double sr)
    {
        fs = sr;
        envA = (float)(1.0 - std::exp(-1.0 / (0.3 * sr))); // level followers: about 0.3 s
        reset();
    }
    void reset()
    {
        for (auto &r : res)
            r = {};
        envIn = envOut = 0.f;
        lpL = lpR = 0.f;
        configured = false;
    }
    // control rate: recompute the notes and their coefficients when the settings change
    void configure(const TuneSettings &t, float ringMs, float tilt)
    {
        const auto &notes = tuneScaleNotes(t.scale);
        int lo = std::clamp(std::min(t.lowest, t.highest), 24, 108), hi = std::clamp(std::max(t.lowest, t.highest), 24, 108);
        bool same = configured && lo == cLo && hi == cHi && t.root == cRoot && t.scale == cScale && std::fabs(ringMs - cRing) < 0.5f &&
                    std::fabs(t.detuneCents - cDet) < 0.01f && std::fabs(tilt - cTilt) < 0.001f && std::fabs(t.width - cWidth) < 0.001f;
        if (same)
            return;
        int n = 0;
        for (int m = lo; m <= hi && n < maxNotes; ++m)
        {
            int pc = ((m - t.root) % 12 + 12) % 12;
            bool in = false;
            for (int nk : notes)
                in = in || nk == pc;
            if (!in)
                continue;
            float det = t.detuneCents * (rand01(31, (uint64_t)m, 7) * 2.f - 1.f);
            double f = 440.0 * std::pow(2.0, (m - 69 + det / 100.0) / 12.0);
            f = std::min(f, 0.45 * fs);
            // ring time -> bandwidth: a resonator of bandwidth B (Hz) decays with time constant 1/(pi B)
            double bw = 1.0 / (3.14159265358979 * std::max(0.02, ringMs / 1000.0));
            double q = std::clamp(f / (bw * 1.554), 2.0, 3000.0); // 1.554: two stages in a row ring about as long as one of bandwidth bw
            double g = std::tan(3.14159265358979 * f / fs), k = 1.0 / q;
            auto &R = res[n];
            if (!R.used || std::fabs(R.f - (float)f) > 0.01f)
                for (int c = 0; c < 4; ++c)
                    R.ic1[c] = R.ic2[c] = 0.f; // a new note starts quiet
            R.used = true, R.f = (float)f, R.midi = m;
            R.k = (float)k;
            R.a1 = (float)(1.0 / (1.0 + g * (g + k))), R.a2 = (float)(g * R.a1), R.a3 = (float)(g * R.a2);
            float pos = hi > lo ? (float)(m - lo) / (float)(hi - lo) : 0.5f;
            R.tiltGain = std::pow(10.f, tilt * (pos - 0.5f) * 24.f / 20.f); // +-12 dB across the range
            float side = (n & 1) ? 1.f : -1.f, pan = 0.5f + 0.5f * side * std::clamp(t.width, 0.f, 1.f);
            R.gl = std::sqrt(1.f - pan), R.gr = std::sqrt(pan);
            ++n;
        }
        for (int i = n; i < maxNotes; ++i)
            res[i].used = false;
        count = n;
        // longer rings pass less of a full mix, so they may be lifted more: +18 dB at 400 ms, up to +30 dB at 4 s
        capGain = std::min(32.f, 8.f * std::sqrt(std::max(1.f, ringMs / 400.f)));
        configured = true, cLo = lo, cHi = hi, cRoot = t.root, cScale = t.scale, cRing = ringMs, cDet = t.detuneCents, cTilt = tilt, cWidth = t.width;
    }
    // control rate: the motion through the notes (a swell that travels, or a slow random drift).
    // Free: pos runs at hz. Synced: pos is the song position in divisions, so a bar always looks the same.
    // Wave: one sweep through the notes per cycle. Drift: a new level every quarter cycle (free)
    // or every division (synced).
    void moveControl(int motion, float hz, int samples, bool sync, double syncPos)
    {
        if (sync)
            pos = std::max(0.0, syncPos);
        else
        {
            pos += hz * samples / fs;
            if (pos >= 1048576.0)
                pos -= 1048576.0;
        }
        const double ph = pos - std::floor(pos), q = sync ? pos : pos * 4.0;
        for (int i = 0; i < count; ++i)
        {
            float m = 1.f;
            if (motion == 1)
                m = 0.35f + 0.65f * (0.5f + 0.5f * (float)std::cos(6.283185307 * (ph - (double)i / std::max(1, count) * 1.5)));
            else if (motion == 2)
            {
                long t = (long)std::floor(q);
                float u = (float)(q - (double)t), a = rand01(53, (uint64_t)t, (uint64_t)i), b = rand01(53, (uint64_t)(t + 1), (uint64_t)i);
                u = u * u * (3.f - 2.f * u);
                m = 0.25f + 0.75f * (a + (b - a) * u);
            }
            else if (motion == 3) // Pulse: every note swells together
                m = 0.3f + 0.7f * (0.5f + 0.5f * (float)std::cos(6.283185307 * ph));
            else if (motion == 4) // Arp: one note at a time, low to high
            {
                const int n = std::max(1, count);
                const double at = ph * n;
                const float d = (float)std::fabs(at - (double)i - 0.5);
                m = 0.15f + 0.85f * std::max(0.f, 1.f - d * 1.6f);
            }
            else if (motion == 5) // Fall: the swell runs down through the notes
                m = 0.35f + 0.65f * (0.5f + 0.5f * (float)std::cos(6.283185307 * (ph + (double)i / std::max(1, count) * 1.5)));
            else if (motion == 6) // Sparkle: a few notes flash on each step
            {
                long t = (long)std::floor(q * 2.0);
                const float u = (float)(q * 2.0 - (double)t);
                // v0.8: a short attack and a decay that ends at the resting level, so no flash starts or stops with a jump
                const float att = std::min(1.f, u / 0.04f);
                m = rand01(71, (uint64_t)t, (uint64_t)i) > 0.7f ? 0.2f + 0.8f * att * (1.f - u) : 0.2f;
            }
            res[i].motion = m;
        }
    }
    double motionPos() const { return pos; }
    // control rate: Slope, a tilt EQ on the ringing. A one-pole split at the pivot; the lows go down
    // as the highs go up (up to 9 dB each way), so the ringing gets darker or brighter, not louder.
    void setTone(float slope, float pivotHz)
    {
        float sl = std::clamp(slope, -1.f, 1.f), f = std::clamp(pivotHz, 40.f, (float)(0.4 * fs));
        toneA = 1.f - std::exp(-6.2831853f * f / (float)fs);
        tLo = std::pow(10.f, -9.f * sl / 20.f), tHi = std::pow(10.f, 9.f * sl / 20.f);
        toneOn = std::fabs(sl) > 1.0e-4f || std::fabs(gLo - 1.f) > 1.0e-4f || std::fabs(gHi - 1.f) > 1.0e-4f;
    }
    // audio rate: the ringing of one stereo sample, level-matched to what came in
    inline void tick(float &l, float &r, float amount, bool layer)
    {
        float sl = 0.f, sr = 0.f;
        const float in[2] = {l, r};
        for (int i = 0; i < count; ++i)
        {
            auto &R = res[i];
            float o[2];
            for (int c = 0; c < 2; ++c)
            {
                // two band-passes in a row (0 dB at the note): steep sides, so what is out of key stays out
                float x = in[c];
                for (int st = 0; st < 2; ++st)
                {
                    float &c1 = R.ic1[st * 2 + c], &c2 = R.ic2[st * 2 + c];
                    float v3 = x - c2, v1 = R.a1 * c1 + R.a2 * v3, v2 = c2 + R.a2 * c1 + R.a3 * v3;
                    c1 = 2.f * v1 - c1, c2 = 2.f * v2 - c2;
                    x = R.k * v1;
                }
                o[c] = x;
            }
            R.motionSm += 0.02f * (R.motion - R.motionSm); // v0.8: the wave's level, smoothed per sample (~1 ms)
            float w = R.tiltGain * R.motionSm;
            sl += (o[0] * R.gl + o[1] * R.gl) * w, sr += (o[0] * R.gr + o[1] * R.gr) * w;
        }
        if (toneOn)
        {
            gLo += 0.002f * (tLo - gLo), gHi += 0.002f * (tHi - gHi); // smoothed, so moving Slope doesn't click
            lpL += toneA * (sl - lpL), lpR += toneA * (sr - lpR);
            sl = lpL * gLo + (sl - lpL) * gHi, sr = lpR * gLo + (sr - lpR) * gHi;
        }
        // keep the ringing at about the level of the input (slow followers, so notes can still swell and fade)
        envIn += envA * (0.5f * (l * l + r * r) - envIn);
        envOut += envA * (0.5f * (sl * sl + sr * sr) - envOut);
        float gmatch = std::clamp(std::sqrt((envIn + 1.0e-12f) / (envOut + 1.0e-12f)), 0.f, capGain); // capped, so what is out of key stays quieter
        float a = std::clamp(amount, 0.f, 1.f);
        const float dry = layer ? 1.f : 1.f - a;
        l = l * dry + sl * gmatch * a;
        r = r * dry + sr * gmatch * a;
        if (!std::isfinite(l) || !std::isfinite(r))
        {
            reset();
            l = r = 0.f;
        }
    }
    int notes() const { return count; }
    int noteMidi(int i) const { return res[i].midi; }
    float noteLevel(int i) const { return res[i].tiltGain * res[i].motion; }

  private:
    struct Res
    {
        bool used{false};
        int midi{0};
        float f{0}, k{1}, a1{0}, a2{0}, a3{0};
        float ic1[4]{}, ic2[4]{}; // [stage * 2 + channel]
        float tiltGain{1}, motion{1}, motionSm{1}, gl{0.7f}, gr{0.7f};
    };
    Res res[maxNotes];
    int count{0};
    double fs{48000}, pos{0};
    float toneA{0.1f}, tLo{1}, tHi{1}, gLo{1}, gHi{1}, lpL{0}, lpR{0};
    bool toneOn{false};
    float envIn{0}, envOut{0}, envA{0.0004f}, capGain{8.f};
    bool configured{false};
    int cLo{0}, cHi{0}, cRoot{-1}, cScale{-1};
    float cRing{0}, cDet{0}, cTilt{0}, cWidth{0};
};

class Engine
{
  public:
    ~Engine() { delete spec; }
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Engine()
    {
        (void)filterCatalogue(); // build the catalogue off the audio thread
        std::memset(combDelays, 0, sizeof(combDelays));
        for (int sh = 0; sh < numShapes; ++sh)
            shapeTable(sh, shapeTables[sh]);
        for (int i = 0; i < numLfos; ++i)
        {
            shapeTable(ShapeCustom, customTable[i][0]);
            shapeTable(ShapeCustom, customTable[i][1]);
            curveActive[i].store(0);
        }
        allocateCore();
    }

    // Not real-time: call from prepare().
    void setSampleRate(double sr)
    {
        sampleRate = sr;
        knobAlpha = (float)(1.0 - std::exp(-(double)blockSize / (0.005 * sampleRate)));
        fadeBlocks = std::max(4, (int)std::ceil(0.006 * sampleRate / blockSize));
        finalGuard.setRate(sampleRate);
        tune.setRate(sampleRate);
        osStages = 0;
        configureRates();
        prepareCore();
        reset();
    }

    // Clear all audio state.
    void reset()
    {
        tune.reset();
        needsSnapBank = true;
        for (auto &b : bs)
            b.ripPh = 0, b.bkLo = -1e9f, b.bkOn = false;
        for (auto &h : hrUp)
            h.reset();
        for (auto &h : hrDn)
            h.reset();
        for (int i = 0; i < numFilt; ++i)
            setupFilter(i);
        for (auto &t : tape)
            t.prepare(sampleRate);
        for (int i = 0; i < numFilt; ++i)
            filtSendSm[i] = filtMixSm[i] = 1.f;
        xover.reset();
        spec->reset();
        finalGuard.reset();
        for (auto &b : bs)
        {
            b.guard.reset();
            b.dcX[0] = b.dcX[1] = b.dcY[0] = b.dcY[1] = 0;
            for (int c = 0; c < 2; ++c)
                for (int kb = 0; kb < numBankBands; ++kb)
                    b.bkS1[c][kb] = b.bkS2[c][kb] = 0.f;
        }
        limGain = 1.f;
        ctr = 0;
        needsSnap = true;
        clearAudioState();
        clearCore();
        resetModulation();
        fadeState = FadeIdle;
        fade.snap(1.f);
        curRouting = -1;
    }

    // A drawn curve for an LFO (main thread). Takes effect at the next control block.
    void setCustomCurve(int lfo, const CurvePoint *p, int n)
    {
        if (lfo < 0 || lfo >= numLfos)
            return;
        int back = 1 - curveActive[lfo].load(std::memory_order_acquire);
        renderCurve(p, std::clamp(n, 0, maxCurvePoints), customTable[lfo][back]);
        curveActive[lfo].store(back, std::memory_order_release);
    }

    // In-place safe. scL / scR: the sidechain, or null.
    void process(const float *inL, const float *inR, float *outL, float *outR, int n,
                 const Settings &s, const float *scL = nullptr, const float *scR = nullptr)
    {
        for (int i = 0; i < n; ++i)
        {
            if (ctr == 0)
                processControl(s);
            float l = inL[i], r = inR[i], ol, orr;
            if (!std::isfinite(l) || !std::isfinite(r))
                l = r = 0.f;
            inPeak = std::max(inPeak, std::max(std::fabs(l), std::fabs(r)));
            if (scL)
            {
                // sidechain: high-pass then low-pass, one pole each, then its peak
                float x = 0.5f * (scL[i] + (scR ? scR[i] : scL[i]));
                if (!std::isfinite(x))
                    x = 0.f;
                x *= scGain;
                scHp = scHpA * (scHp + x - scHpX);
                scHpX = x;
                scLp += scLpA * (scHp - scLp);
                if (std::fabs(scHp) < 1.0e-20f)
                    scHp = 0.f;
                if (std::fabs(scLp) < 1.0e-20f)
                    scLp = 0.f;
                scPeak = std::max(scPeak, std::fabs(scLp));
            }
            coreTick(l, r, ol, orr);
            // Safety net: anything non-finite means silence for the rest of this control
            // block and a clean rebuild at the next one.
            if (!std::isfinite(ol) || !std::isfinite(orr) || recovering)
            {
                if (!recovering)
                    ++recoverCount;
                ol = orr = 0.f;
                recovering = true;
            }
            ol = std::clamp(ol, -4.f, 4.f);
            orr = std::clamp(orr, -4.f, 4.f);
            limit(ol, orr, s.limiter); // always delays by the look-ahead, so the latency never changes
            if (std::fabs(ol) < 1.0e-20f)
                ol = 0.f;
            if (std::fabs(orr) < 1.0e-20f)
                orr = 0.f;
            outL[i] = ol;
            outR[i] = orr;
            ctr = (ctr + 1) % blockSize;
        }
    }

    // Call at the start of every host block with that block's transport.
    void setHostTime(const HostTime &t)
    {
        bpm = (t.bpm > 1 && t.bpm < 1000) ? t.bpm : 120.0;
        beatsPerBar = 4.0 * std::max(1, t.num) / std::max(1, t.den);
        if (freeRun) // Gullay: Free runs on its own tempo, whatever the host does
        {
            bpm = freeBpm;
            playing = freePlay;
            return;
        }
        if (t.playing && t.beatsValid && std::isfinite(t.beats))
            beats = t.beats; // anchored to the song while playing; free-runs at tempo otherwise
        playing = t.playing;
    }

    struct MutEvent
    {
        int dir{0}, low{0}, high{0}; // +1 up, -1 down, 0 none
    };

    // ----- read-only info for the screen -------------------------------------------------
    float swayValue(int b) const { return swOut[b]; }            // -1..1 before depth
    float conductorPassed(int b) const { return condCut[b]; }    // semitones passed on from Mid
    int chaperoneSteps() const { return chapSteps; }             // counts each step it passes on (for the window)
    float mutationOffset(int r) const { return respVal[r]; }     // -1..1, [0] Low, [1] High
    const MutResponse &mutationAnswer(int r) const { return effResp[r]; } // after Mutate
    MutEvent mutationHistory(int ago) const { return hist[(histPos + 15 - std::clamp(ago, 0, 15)) % 16]; }
    unsigned mutationEvents() const { return eventCount; }
    long mutationPeriod() const { return mutK; }          // how many times Mutate has changed the answers (at this song position)
    float mutationPeriodFrac() const { return mutFrac; }  // 0..1 through the current period
    // phase 3
    int laneStep(int l) const { return laneIdx[l]; }      // the step sounding now
    int lockStep(int l) const { return lockIdx[l]; }      // the lock lane's step now
    float laneValue(int l) const { return laneOut[l]; }   // -1..1 after Euclid and smoothing (before depth)
    float lfoValue(int i) const { return lfoOut[i]; }     // -1..1 (or 0..1 unipolar)
    float lfoPhase(int i) const { return (float)lfoPh[i]; }
    float envValue() const { return efOut; }               // 0..1
    float sidechainLevel() const { return scShow; }        // 0..1
    float targetOffset(int t) const { return (t > 0 && t < numModTargetIds) ? tgtOff[t] : 0.f; }
    float displayBand(int b, int param) const { return dispBand[b][param]; } // modulated knob values
    float displayGlobal(int t) const { return (t >= TgtSplitLo && t < numModTargetIds) ? dispGlobal[t - TgtSplitLo] : 0.f; }
    // Tune, for the window: which notes ring and how strongly
    int tuneNotes() const { return tuneOn ? tune.notes() : 0; }
    int tuneNoteMidi(int i) const { return tune.noteMidi(i); }
    double tuneMotionPos() const { return tune.motionPos(); }
    float tuneNoteLevel(int i) const { return tune.noteLevel(i); }
    float feedbackReturn() const { return std::max(std::fabs(fbL), std::fabs(fbR)); } // what goes back round now
    float displayCutoff(int b) const { return dispCo[b]; } // after modulation and smart limiting
    float bankGainNow(int b, int k) const { return dispBankGain[b][k]; }
    float bankHz(int b, int k) const { return dispBankHz[b][k]; }
    // copies the latest n analyser samples of one signal (0..2 strips, 3 output), oldest first
    void readAnalyser(int which, float *dst, int n) const
    {
        uint32_t w = anaWrite.load(std::memory_order_acquire);
        n = std::min(n, anaSize);
        for (int i = 0; i < n; ++i)
            dst[i] = ana[which][(w - (uint32_t)n + (uint32_t)i) & (anaSize - 1)].load(std::memory_order_relaxed);
    }
    float displayReso(int b) const { return dispRe[b]; }
    float edgePress(int b) const { return press[b]; } // 0..1: how hard the knob leans on its limit
    float bandGuardGain(int b) const { return bs[b].guard.gain; }
    float outputGuardGain() const { return finalGuard.gain; }
    float limiterGain() const { return limGain; }
    float xoverLoHz() const { return (float)curF1; }
    float xoverHiHz() const { return (float)curF2; }
    int activeRouting() const { return curRouting; }
    // samples of delay at the host rate (the spectral split's frame), for the host to compensate
    int latencySamples() const { return tapoBlock + (spectralOn ? spec->hostN : 0); }
    bool spectralActive() const { return spectralOn; }
    PassKind bandKind(int b) const { return kind[b]; }

    static constexpr float limCeiling{0.8912509f}; // -1 dBFS
    static constexpr float edgeKnee{6.f};          // semitones
    static constexpr float seriesGap{12.f};        // semitones kept open between HP and LP

  private:
    // ----- configuration -----------------------------------------------------------------
    double sampleRate{48000}, innerRate{96000};
    double maxCutoff{67};
    int osStages{1}, nInner{16};
    float knobAlpha{0.04f};
    int ctr{0};
    bool needsSnap{true}, recovering{false};

    int curRouting{-1};
    enum FadeState { FadeIdle, FadeOut, FadeIn };
    int fadeState{FadeIdle}, fadeCount{0}, fadeBlocks{36};
    Ramp fade;

    static constexpr int numFilt = 6; // A, Lead, B; then Lead's own copy in each loop (Lead Loop filters every repeat)
    std::array<sfpp::Filter, numFilt> filters;
    int curModel[numFilt]{-1, -1, -1, -1, -1, -1}, curVariant[numFilt]{-1, -1, -1, -1, -1, -1};
    bool curOn[numFilt]{true, true, true, true, true, true};
    bool fActive[numFilt]{};
    float filtSendT[3]{1, 1, 1}, filtMixT[3]{1, 1, 1}, filtSendSm[numFilt], filtMixSm[numFilt]; // v0.8 (smoothed per sample)
    int fPlace[numFilt]{FiltOut, FiltOut, FiltOut, FiltLoop, FiltLoop, FiltLoop};
    PassKind kind[numFilt]{};
    // Z-Plane sections (transposed direct form II), per strip
    struct ZSec
    {
        float b0{1}, b1{0}, b2{0}, a1{0}, a2{0};
        float z1[2]{}, z2[2]{};
    };
    ZSec zsec[numFilt][zStages];
    bool zOn[numFilt]{};
    int zDesign[numFilt]{};
    Ramp zComp[numFilt];
    bool zSnap[numFilt]{true, true, true, true, true, true};
    struct ZBiquad
    {
        double b0, b1, b2, a1, a2; // normalised (a0 = 1)
    };
    // RBJ peaking EQ, or the low-pass section (q > 0, gain ignored)
    ZBiquad zPeak(double f, double gDb, double q) const
    {
        double A = std::pow(10.0, gDb / 40.0), w0 = 2.0 * 3.14159265358979 * f / innerRate, al = std::sin(w0) / (2.0 * q), cs = std::cos(w0);
        double a0 = 1.0 + al / A;
        return {(1.0 + al * A) / a0, -2.0 * cs / a0, (1.0 - al * A) / a0, -2.0 * cs / a0, (1.0 - al / A) / a0};
    }
    ZBiquad zLow(double f, double q) const
    {
        double w0 = 2.0 * 3.14159265358979 * f / innerRate, al = std::sin(w0) / (2.0 * q), cs = std::cos(w0), a0 = 1.0 + al;
        return {(1.0 - cs) / 2.0 / a0, (1.0 - cs) / a0, (1.0 - cs) / 2.0 / a0, -2.0 * cs / a0, (1.0 - al) / a0};
    }
    // a second-order polynomial 1 + c1 z^-1 + c2 z^-2 moved between two frames: when both have a
    // complex pair of roots, their radius and angle are interpolated (the root glides across the
    // z-plane); otherwise the coefficients are (stable for poles: the stability region is convex)
    static void zMove(double c1a, double c2a, double c1b, double c2b, double t, double &c1, double &c2)
    {
        bool ca = c1a * c1a < 4.0 * c2a && c2a > 0, cb = c1b * c1b < 4.0 * c2b && c2b > 0;
        if (ca && cb)
        {
            double ra = std::sqrt(c2a), rb = std::sqrt(c2b);
            double tha = std::acos(std::clamp(-c1a / (2.0 * ra), -1.0, 1.0)), thb = std::acos(std::clamp(-c1b / (2.0 * rb), -1.0, 1.0));
            double r = ra + t * (rb - ra), th = tha + t * (thb - tha);
            c1 = -2.0 * r * std::cos(th), c2 = r * r;
        }
        else
            c1 = c1a + t * (c1b - c1a), c2 = c2a + t * (c2b - c2a);
    }
    void zCoefficients(int b, float cutoffSemi, float reso, float morph)
    {
        int n = 0;
        const ZDesign *d = zDesigns(n);
        const ZDesign &D = d[std::clamp(zDesign[b], 0, n - 1)];
        float t = std::clamp(morph, 0.f, 1.f);
        float shift = std::exp2(3.f * std::tanh((cutoffSemi - hzToSemi(1000.0)) / 36.f)); // Frequency Tracking
        float qs = 0.5f + 2.5f * std::pow(std::clamp(reso, 0.f, 1.f), 1.5f), gs = 0.6f + 0.8f * std::clamp(reso, 0.f, 1.f); // Transform 2
        const double fMax = 0.45 * innerRate;
        auto frameStage = [&](const ZFrame &F, int k, float &peakDb) -> ZBiquad
        {
            if (k == 0)
            {
                if (F.lpF <= 0.f)
                    return {1, 0, 0, 0, 0};
                return zLow(std::clamp((double)F.lpF * shift, 20.0, fMax), std::max(0.5, (double)F.lpQ * (0.6 + 0.4 * qs)));
            }
            int j = k - 1;
            float g = F.g[j] * (F.g[j] > 0 ? gs : 1.f);
            peakDb = std::max(peakDb, g);
            return zPeak(std::clamp((double)F.f[j] * shift, 20.0, fMax), g, (double)F.q[j] * qs);
        };
        float peakA = 0.f, peakB = 0.f;
        for (int k = 0; k < zStages; ++k)
        {
            ZBiquad A = frameStage(D.a, k, peakA), B = frameStage(D.b, k, peakB);
            double a1, a2, n1, n2;
            zMove(A.a1, A.a2, B.a1, B.a2, t, a1, a2);                                             // the poles
            double za = std::fabs(A.b0) > 1e-12 ? A.b0 : 1e-12, zb = std::fabs(B.b0) > 1e-12 ? B.b0 : 1e-12;
            zMove(A.b1 / za, A.b2 / za, B.b1 / zb, B.b2 / zb, t, n1, n2);                         // the zeros
            double g0 = std::exp(std::log(std::fabs(za)) + t * (std::log(std::fabs(zb)) - std::log(std::fabs(za)))); // and the gain
            auto &Z = zsec[b][k];
            Z.b0 = (float)g0, Z.b1 = (float)(g0 * n1), Z.b2 = (float)(g0 * n2), Z.a1 = (float)a1, Z.a2 = (float)a2;
        }
        float maxG = peakA + t * (peakB - peakA);
        float comp = std::pow(10.f, -0.4f * maxG / 20.f); // most of the loudest peak taken back (the Filter Level)
        if (zSnap[b])
            zComp[b].snap(comp), zSnap[b] = false;
        else
            zComp[b].to(comp, nInner);
    }
    inline void zTick(int b, float &l, float &r)
    {
        float x[2] = {l, r}, c = zComp[b].tick();
        for (int k = 0; k < zStages; ++k)
        {
            auto &Z = zsec[b][k];
            for (int ch = 0; ch < 2; ++ch)
            {
                float y = Z.b0 * x[ch] + Z.z1[ch];
                Z.z1[ch] = Z.b1 * x[ch] - Z.a1 * y + Z.z2[ch];
                Z.z2[ch] = Z.b2 * x[ch] - Z.a2 * y;
                if (std::fabs(Z.z1[ch]) < 1e-20f)
                    Z.z1[ch] = 0.f;
                if (std::fabs(Z.z2[ch]) < 1e-20f)
                    Z.z2[ch] = 0.f;
                x[ch] = y;
            }
        }
        l = std::clamp(x[0] * c, -8.f, 8.f);
        r = std::clamp(x[1] * c, -8.f, 8.f);
    }
    float combDelays[numFilt][4][sst::filters::utilities::MAX_FB_COMB +
                                  sst::filters::utilities::SincTable::FIRipol_N];

    HalfBand hrUp[maxOsStages], hrDn[maxOsStages];
    SpectralSplit *spec{new SpectralSplit()}; // large: kept off the stack
    int specQ{1};
    bool spectralOn{false};

    Crossover xover;
    double curF1{250}, curF2{3000};

    struct BandState
    {
        bool active{false};     // filter in use
        bool driveFirst{false}; // order
        bool bypass{false};     // Series: strip bypassed
        int dtype{Warm};
        Ramp dGain, dComp, dWet, dAmt, level;
        float refScale2{1.f};
        float dcX[2]{}, dcY[2]{};
        Guard guard;
        // the bank: TPT state-variable band-passes (two channels x eight)
        float bkA1[numBankBands]{}, bkA2[numBankBands]{}, bkA3[numBankBands]{}, bkK[numBankBands]{};
        float bkS1[2][numBankBands]{}, bkS2[2][numBankBands]{};
        Ramp bkGL[numBankBands], bkGR[numBankBands], bkWet, smix;
        bool bkOn{false};
        float bkNorm{1.f}, bkLo{-1e9f}, bkHi{-1e9f}, bkW{-1.f};
        double ripPh{0};
    } bs[numBands];
    float dispBankGain[numBands][numBankBands]{}, dispBankHz[numBands][numBankBands]{};
    bool needsSnapBank{true};
    // the analyser: the three strips and the output, at the host rate, for the window
    static constexpr int anaSize = 4096;
    std::atomic<float> ana[4][anaSize]{};
    std::atomic<uint32_t> anaWrite{0};
    int anaCount{0};
    inline void anaPush(float a, float b, float c, float o)
    {
        if (++anaCount < nInner / blockSize)
            return;
        anaCount = 0;
        uint32_t w = anaWrite.load(std::memory_order_relaxed);
        uint32_t i = w & (anaSize - 1);
        ana[0][i].store(a, std::memory_order_relaxed);
        ana[1][i].store(b, std::memory_order_relaxed);
        ana[2][i].store(c, std::memory_order_relaxed);
        ana[3][i].store(o, std::memory_order_relaxed);
        anaWrite.store(w + 1, std::memory_order_release);
    }
    float dcR{0.999f};
    float bandBoost{4.f};
    bool guardsOn{true};

    Ramp inGain, outGain, mix;
    Guard finalGuard;
    float finalRefScale2{1.f}, finalNoise2{0.f};

    // pan per strip
    Ramp panL[numBands], panR[numBands];

    // feedback and noise (per routing)
    FbConditioner fbc[2];
    float fbL{0}, fbR{0}, fbLpCoef{0.5f};
    Ramp fbWet, fbLevel, nsGain, nsColor;
    float curFw{0}, curFl{0};
    double fbInSum{0}, fbRetSum{0};
    float fbInMs{0}, fbRetMs{0}, fbCtl{1.f}, fbCtlTarget{1.f};
    uint32_t nsState{0x12345678u};
    float nsLp[2]{}, nsLpCoef{0.05f};

    // transport
    double bpm{120}, beatsPerBar{4}, beats{0};
    bool playing{false};
    double timeSec{0};

    // Sway
    double swBandPh[numBands][2]{{0.0, 0.31}, {0.57, 0.83}, {0.0, 0.83}}, swBandBase[numBands]{};
    long swBandStep[numBands]{-1, -1, -1};
    float swHold[numBands]{}, swOut[numBands]{};
    float inPeak{0}, env{0}, envSlow{0};
    bool envArmed{true};

    // Conductor
    float condCut[numBands]{};
    bool chapStepped{false};
    int chapSteps{0}, chapDir{1};

    // Mutation
    float midPeak{0}, midFast{0}, midSlow{0}, moveSlow{0};
    // phase 3
    float bandPeak[numBands]{};
    float scPeak{0}, scGain{1}, scHp{0}, scHpX{0}, scLp{0}, scHpA{1}, scLpA{1}, scShow{0};
    float efState{0}, efOut{0};
    double lanePos[numLanes]{};
    int laneIdx[numLanes]{-1, -1, -1}, lockIdx[numLanes]{-1, -1, -1};
    float laneOut[numLanes]{}, laneTarget[numLanes]{}, lockOut[numLanes]{}, lockK[numLanes][numBandParams]{};
    bool midLaneStepped{false};
    double lfoPh[numLfos]{};
    double lfoFree[numLfos]{};
    float lfoOut[numLfos]{}, lfoRaw[numLfos]{};
    float shapeTables[numShapes][curveN + 1];
    float customTable[numLfos][2][curveN + 1];
    std::atomic<int> curveActive[numLfos]{};
    bool wasPlaying{false};
    double playStart{0};
    float tgtOff[numModTargetIds]{}, tgtLock[numModTargetIds]{};
    float ripOut[numBands]{};
    float modMaster{1.f};
    float dispBand[numBands][numBandParams]{};
    float dispGlobal[numModTargetIds - TgtSplitLo]{};
    float stripDepthOff[numBands]{}, specBalOff{0}, specSharpOff{0};
    TuneBank tune;
    bool tuneOn{false}, tuneBefore{false}, tuneLayer{false}, tuneOnly{false};
    float tuneAmt{0};
    bool lvlArmed{true}, cutArmed{true}, guardArmed{true};
    long stepPrev{LONG_MIN};
    double lastEvent{-1.0e9};
    unsigned eventCount{0};
    struct Pending
    {
        double at{0};
        float val{0};
        bool live{false};
    } pend[2][8];
    float respKick[2]{}, respVal[2]{};
    int respTgt[2]{-1, -1};
    MutResponse effResp[2], lastParams[2];
    long mutK{-1};
    int lastSeed{-1};
    float lastMutate{-1.f}, mutFrac{0.f};
    bool wasFrozen{false};
    long frozenK{0};
    MutEvent hist[16]{};
    int histPos{0};

    // smoothed knob values
    struct Smoothed
    {
        float co[numBands], re[numBands], mo[numBands], dr[numBands], lv[numBands], pan[numBands];
        float f1, f2, inDb, outDb, mix, fb, ns, nsCol;
    } k{};

    float dispCo[numBands]{}, dispRe[numBands]{}, press[numBands]{};

    // ----- output limiter ----------------------------------------------
    // Transparent peak limiter: a 32-sample (0.7 ms) look-ahead sees a peak coming and eases the gain down over
    // the look-ahead, so the peak is caught without clipping its shape; below -1 dBFS it does nothing at all.
    // The release follows the music: a lone peak lets go in about 60 ms, a long loud stretch more slowly (to
    // 400 ms) so it never pumps. The output is delayed by the look-ahead whether the limiter is on or off.
    static constexpr int limLook = 32;
    float limBufL[limLook]{}, limBufR[limLook]{}, limTgt[limLook]{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    int limPos{0};
    float limGain{1.f}, limOver{0.f};
    void limit(float &l, float &r, bool on)
    {
        const float a = std::max(std::fabs(l), std::fabs(r));
        const float tg = a > limCeiling ? limCeiling / a : 1.f;
        const float dl = limBufL[limPos], dr = limBufR[limPos];
        limBufL[limPos] = l, limBufR[limPos] = r, limTgt[limPos] = tg;
        limPos = limPos + 1 == limLook ? 0 : limPos + 1;
        if (!on)
        {
            l = dl, r = dr;
            return;
        }
        float want = 1.f;
        for (int k = 0; k < limLook; ++k)
            want = std::min(want, limTgt[k]);
        const float fs = (float)sampleRate;
        if (want < limGain)
            limGain += (want - limGain) * (4.f / (float)limLook), limOver = std::min(1.f, limOver + 1.f / (0.5f * fs));
        else
        {
            limOver = std::max(0.f, limOver - 1.f / (2.f * fs));
            const float relMs = 60.f + 340.f * limOver;
            limGain += (want - limGain) * (1.f - std::exp(-1000.f / (relMs * fs)));
        }
        l = std::clamp(dl * limGain, -limCeiling, limCeiling);
        r = std::clamp(dr * limGain, -limCeiling, limCeiling);
    }

    // ----- saturators ----------------------------------------------------------------------
    static inline float warm(float x) // tanh-like, |y| <= 1
    {
        x = std::clamp(x, -3.f, 3.f);
        return x * (27.f + x * x) / (27.f + 9.f * x * x);
    }
    static inline float shape(int type, float x, float amt)
    {
        switch (type)
        {
        default:
        case Warm:
            return warm(x);
        case Push: // tube-like: the two halves of the wave saturate differently
            return (warm(x + 0.4f) - 0.382f) * 1.15f;
        case Bite: // clean until it clips, then a short knee into a flat top
        {
            float a = std::fabs(x);
            if (a <= 0.8f)
                return x;
            float y = a >= 1.2f ? 1.f : a - (a - 0.8f) * (a - 0.8f) * 1.25f;
            return x < 0 ? -y : y;
        }
        case Fuzz: // high gain, lopsided ceilings
        {
            x = std::clamp(x, -30.f, 30.f);
            return x >= 0 ? 1.f - std::exp(-x) : -0.8f * (1.f - std::exp(x * 1.25f));
        }
        case Fold: // wavefolder: past full scale the wave turns back on itself
            return std::sin(std::clamp(x, -64.f, 64.f));
        case Rectify: // blends in the flipped half of the wave: octave-up grit
            return warm((1.f - amt) * x + amt * 1.5f * std::fabs(x));
        case Crush: // bit depth: 16 bits at the bottom of the knob down to 2 at the top (smoothly in between)
        {
            float levels = std::exp2(15.f - 14.f * std::clamp(amt, 0.f, 1.f)); // steps per half wave
            return std::clamp(std::round(std::clamp(x, -1.f, 1.f) * levels) / levels, -1.f, 1.f);
        }
        }
    }
    static inline float driveGainFor(int type, float a)
    {
        float a2 = a * a;
        switch (type)
        {
        default:
        case Warm:
            return 1.f + 15.f * a2; // up to +24 dB into the saturator
        case Crush:
            return 1.f; // the knob sets the bits, not the level
        case Push:
        case Bite:
            return 1.f + 31.f * a2; // +30 dB
        case Fuzz:
            return 1.f + 99.f * a2; // +40 dB
        case Fold:
            return 1.f + 7.f * a2; // 8x: several folds
        case Rectify:
            return 1.f + 3.f * a2;
        }
    }

    // ----- setup ---------------------------------------------------------------------------
    void configureRates()
    {
        int osf = 1 << osStages;
        innerRate = sampleRate * osf;
        nInner = blockSize * osf;
        maxCutoff = 12.0 * std::log2(innerRate * 0.495 / 440.0);
        maxCutoff = std::min(maxCutoff, 12.0 * std::log2(sampleRate * 0.495 / 440.0) + 12.0);
        for (auto &b : bs)
            b.guard.setRate(innerRate);
        dcR = (float)(1.0 - 2.0 * 3.14159265358979 * 8.0 / innerRate); // 8 Hz DC blocker
        spec->setup(innerRate, osf, 1024 << std::clamp(specQ, 0, 3));
    }

    void setupFilter(int f)
    {
        std::memset(combDelays[f], 0, sizeof(combDelays[f]));
        const auto &cat = filterCatalogue();
        auto model = sfpp::FilterModel::None;
        sfpp::ModelConfig cfg{};
        int mi = curModel[f];
        kind[f] = PassKind::Open;
        fActive[f] = false;
        zOn[f] = false;
        zSnap[f] = true;
        for (auto &Z : zsec[f])
            Z.z1[0] = Z.z1[1] = Z.z2[0] = Z.z2[1] = 0.f;
        if (curOn[f] && mi > 0 && mi < (int)cat.size() && cat[(size_t)mi].zplane)
        {
            zOn[f] = true;
            zDesign[f] = std::clamp(curVariant[f], 0, (int)cat[(size_t)mi].configs.size() - 1);
            fActive[f] = true;
            mi = 0; // the sst filter itself stays empty
        }
        if (curOn[f] && mi > 0 && mi < (int)cat.size() && !cat[(size_t)mi].configs.empty())
        {
            model = cat[(size_t)mi].model;
            int vi = std::clamp(curVariant[f], 0, (int)cat[(size_t)mi].configs.size() - 1);
            cfg = cat[(size_t)mi].configs[(size_t)vi];
            kind[f] = cat[(size_t)mi].kinds[(size_t)vi];
            fActive[f] = true;
        }
        int osf = 1 << osStages;
        filters[f].setFilterModel(model);
        filters[f].setModelConfiguration(cfg);
        filters[f].setStereo();
        // In runs per host sample (coefficients each control block); Out and Loop run in the 64-sample
        // FIFO blocks, so their coefficients move once per FIFO block (see filterBlockStart)
        filtBlock[f] = (f == 1 && fPlace[f] == FiltIn) ? blockSize : fifoN;
        filtPrepared[f] = false;
        filters[f].setSampleRateAndBlockSize(osf * sampleRate, osf * filtBlock[f]);
        for (int i = 0; i < 4; ++i)
            filters[f].provideDelayLine(i, combDelays[f][i]);
        (void)filters[f].prepareInstance();
        filters[f].reset();
    }

    void clearAudioState()
    {
        memRate = 44101.f; // the memory rate's filter is set up again (and reset) on the next block
        for (auto &d : memDec)
            d.reset();
        for (auto &d : memIn)
            d.reset();
        for (auto &d : memLoop)
            d.reset();
        for (auto &t : tape)
            t.reset();
        for (auto &h : hrUp)
            h.reset();
        for (auto &h : hrDn)
            h.reset();
        xover.reset();
        for (auto &b : bs)
        {
            b.guard.reset();
            b.dcX[0] = b.dcX[1] = b.dcY[0] = b.dcY[1] = 0;
            for (int c = 0; c < 2; ++c)
                for (int kb = 0; kb < numBankBands; ++kb)
                    b.bkS1[c][kb] = b.bkS2[c][kb] = 0.f;
        }
        finalGuard.reset();
        for (auto &c : fbc)
            c.reset();
        fbL = fbR = 0.f;
        fbInSum = fbRetSum = 0.0;
        fbInMs = fbRetMs = 0.f;
        fbCtl = fbCtlTarget = 1.f;
        nsLp[0] = nsLp[1] = 0.f;
        // Gullay: the delays, reverb, shifters, tilts, crushers and the FIFO
        clearCore();
        tune.reset();
        for (auto &t : tone)
            t.reset();
    }

    void resetModulation()
    {
        nsState = 0x12345678u; // the noise restarts the same way, so bounces match
        timeSec = 0;
        for (int b = 0; b < numBands; ++b)
            bandPeak[b] = 0.f;
        scPeak = scHp = scHpX = scLp = scShow = efState = efOut = 0.f;
        for (int l = 0; l < numLanes; ++l)
        {
            lanePos[l] = 0, laneIdx[l] = lockIdx[l] = -1, laneOut[l] = laneTarget[l] = lockOut[l] = 0.f;
            for (auto &x : lockK[l])
                x = 0.f;
        }
        midLaneStepped = false;
        for (int i = 0; i < numLfos; ++i)
            lfoPh[i] = lfoFree[i] = 0, lfoOut[i] = lfoRaw[i] = 0.f;
        wasPlaying = false;
        playStart = 0;
        for (auto &t : tgtOff)
            t = 0.f;
        for (auto &t : tgtLock)
            t = 0.f;
        {
            static const double start[numBands][2] = {{0.0, 0.31}, {0.57, 0.83}, {0.0, 0.83}};
            for (int b = 0; b < numBands; ++b)
                swBandPh[b][0] = start[b][0], swBandPh[b][1] = start[b][1], swBandBase[b] = 0, swBandStep[b] = -1;
        }
        for (int b = 0; b < numBands; ++b)
            swHold[b] = swOut[b] = condCut[b] = condHeld[b] = condBurst[b] = condSm[b] = 0.f;
        condTick = condBar = condFine = -1, condTw = 0.f, jmTick = LONG_MIN;
        for (int b = 0; b < numBands; ++b)
            jmT[b] = jmS[b] = 0.f;
        inPeak = env = envSlow = 0.f;
        midPeak = midFast = midSlow = moveSlow = 0.f;
        lvlArmed = cutArmed = guardArmed = envArmed = true;
        stepPrev = LONG_MIN;
        lastEvent = -1.0e9;
        eventCount = 0;
        for (auto &row : pend)
            for (auto &p : row)
                p.live = false;
        respKick[0] = respKick[1] = respVal[0] = respVal[1] = 0.f;
        respTgt[0] = respTgt[1] = -1;
        mutK = -1;
        for (auto &h : hist)
            h = MutEvent{};
        histPos = 0;
    }

    // ----- Sway, Conductor and Mutation (control rate) ------------------------------------------
    // One step of Mutate: each answer drifts a little, and may flip or change what it moves.
    // Amount and Chance wander but are pulled back toward what you set, and never fall to nothing,
    // so an answer that drifts never goes silent for good.
    static void mutateStep(MutResponse r[2], const MutResponse base[2], int seed, long i, float m)
    {
        for (int k = 0; k < 2; ++k)
        {
            auto u = [&](int n) { return rand01((uint64_t)(uint32_t)seed + 77, (uint64_t)i, (uint64_t)(k * 16 + n)); };
            r[k].amount += 0.2f * (base[k].amount - r[k].amount);
            r[k].chance += 0.2f * (base[k].chance - r[k].chance);
            r[k].amount = std::clamp(r[k].amount + (u(0) - 0.5f) * 0.6f * m, std::min(0.15f, base[k].amount), 1.f);
            r[k].chance = std::clamp(r[k].chance + (u(1) - 0.5f) * 0.4f * m, std::min(0.25f, base[k].chance), 1.f);
            if (r[k].mode != MutOff && u(2) < 0.25f * m)
                r[k].mode = r[k].mode == MutTandem ? MutContrast : MutTandem;
            if (u(3) < 0.15f * m)
                r[k].target = std::min((int)(u(4) * (float)numMutTargets), numMutTargets - 1);
            if (u(5) < 0.10f * m)
                r[k].delaySteps = std::min((int)(u(6) * 5), 4);
        }
    }

    static bool sameResp(const MutResponse &a, const MutResponse &b)
    {
        return a.mode == b.mode && a.target == b.target && a.amount == b.amount && a.chance == b.chance &&
               a.delaySteps == b.delaySteps;
    }

    static bool midLaneActive(const Settings &)
    {
        return false; // v0.4: no sequencer lanes; Mutation's step trigger follows the 1/16 grid
    }
    static bool midLaneActiveOld(const Settings &s)
    {
        const auto &L = s.lane[Mid];
        if (L.euclid != 0)
            return true;
        for (int i = 0; i < std::clamp(L.length, 1, numSteps); ++i)
        {
            if (std::fabs(L.step[i]) > 1.0e-4f)
                return true;
            for (int k = 0; k < numBandParams; ++k)
                if (std::isfinite(L.plock[k][i]))
                    return true;
        }
        return false;
    }

    // ----- Phase 3: lanes, P-locks, LFOs, follower, matrix (control rate) -----------------------
    float srcDepthNow[10]{1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    double rndPh{0};
    float rndFrom{0}, rndTo{0}, rndOut{0};
    uint32_t rndSeed{12345u};
    void runSources(const Settings &s, double dt)
    {
        for (int k = 0; k < 10; ++k)
            srcDepthNow[k] = std::clamp(s.gy.srcDepth[k], 0.f, 1.f);
        // v0.4: the sequencer lanes and the Follower are gone from Gullay; routes still naming them read as nothing
        // v0.5: their slots now hold Random, Mod Wheel, Velocity and Hymn's envelope
        // band and input peaks for the follower (and Mutation's level trigger)
        float pkBand[numBands];
        for (int b = 0; b < numBands; ++b)
            pkBand[b] = bandPeak[b], bandPeak[b] = 0.f;
        midPeak = pkBand[Mid];
        float pkSc = scPeak;
        scPeak = 0.f;
        scShow = std::max(pkSc, scShow * std::exp(-(float)dt / 0.15f));

        bool startNow = playing && !wasPlaying;
        if (startNow)
            playStart = beats;
        wasPlaying = playing;
        for (auto &t : tgtOff)
            t = 0.f;
        for (auto &t : tgtLock)
            t = 0.f;

        // sidechain filters (one pole each) from the settings
        const auto &E = s.env;
        scGain = std::pow(10.f, std::clamp(E.scGainDb, -24.f, 24.f) / 20.f);
        scHpA = (float)std::exp(-2.0 * 3.14159265358979 * std::clamp((double)E.scHpHz, 10.0, 5000.0) / sampleRate);
        scLpA = (float)(1.0 - std::exp(-2.0 * 3.14159265358979 * std::clamp((double)E.scLpHz, 40.0, 20000.0) / sampleRate));

        // ----- sequencer lanes
        for (int l = 0; l < numLanes; ++l)
        {
            const auto &L = s.lane[l];
            int n = std::clamp(L.length, 1, numSteps);
            double stepBeats = laneRateBeats[std::clamp(L.rateIndex, 0, numLaneRates - 1)];
            double stepSec;
            double pos;
            // Sway pushes this lane ahead of and behind the grid (its own band's Sway), so the
            // steps speed up and slow down but come back to the beat
            double swayPush = std::clamp((double)L.sway, 0.0, 1.0) * 4.0 * swOut[l];
            if (L.freeRun)
            {
                double hz = std::clamp((double)L.speedHz, 0.05, 500.0);
                if (startNow)
                    lanePos[l] = 0;
                lanePos[l] += hz * dt;
                if (lanePos[l] > 1.0e7)
                    lanePos[l] = std::fmod(lanePos[l], 720720.0);
                pos = lanePos[l];
                stepSec = 1.0 / hz;
            }
            else
            {
                double b = beats;
                switch (s.retrigger)
                {
                case RetrigBar: b = std::fmod(std::max(0.0, beats), beatsPerBar); break;
                case Retrig2Bars: b = std::fmod(std::max(0.0, beats), 2 * beatsPerBar); break;
                case Retrig4Bars: b = std::fmod(std::max(0.0, beats), 4 * beatsPerBar); break;
                case RetrigOnStart: b = beats - playStart; break;
                default: break;
                }
                pos = b / stepBeats;
                stepSec = stepBeats * 60.0 / bpm;
            }
            pos += swayPush - std::clamp((double)L.offset, -0.5, 0.5);
            // swing: in each pair of steps the first lasts 2 x swing of the pair
            double sw = std::clamp((double)L.swing, 0.5, 0.75);
            auto stepAt = [sw](double q)
            {
                double pair = std::floor(q / 2.0), ph = q - pair * 2.0;
                double inPair = ph < 2.0 * sw ? ph / (2.0 * sw) : 1.0 + (ph - 2.0 * sw) / (2.0 - 2.0 * sw);
                return (long)(pair * 2.0 + std::floor(inPair));
            };
            long st = stepAt(pos);
            // which step plays: forward, reverse, ping-pong (ends not repeated) or random (by step count, so it repeats)
            const int dir = L.direction;
            auto stepIndex = [dir, l](long q, int len)
            {
                int f = (int)(((q % len) + len) % len);
                switch (dir)
                {
                case 1: return len - 1 - f;
                case 2:
                {
                    if (len < 2)
                        return 0;
                    long per = 2L * len - 2, r = ((q % per) + per) % per;
                    return (int)(r < len ? r : per - r);
                }
                case 3: return std::min((int)(rand01(4242 + (uint64_t)l, (uint64_t)q, 777) * (float)len), len - 1);
                default: return f;
                }
            };
            int idx = stepIndex(st, n);
            int k = std::clamp(L.hits, 0, n), rot = ((L.rotate % n) + n) % n;
            bool hit = ((((idx + rot) % n) * k) % n) < k;
            float v = std::clamp(L.step[idx], -1.f, 1.f);
            if (L.euclid == 1)
                v = hit ? v : 0.f;
            else if (L.euclid == 2)
                v = hit ? 1.f : 0.f;
            if (idx != laneIdx[l])
            {
                if (l == Mid && laneIdx[l] >= 0 && (L.euclid == 0 || hit))
                    midLaneStepped = true;
                laneIdx[l] = idx;
            }
            laneTarget[l] = v;
            double tau = std::max(0.0005, std::clamp((double)L.smooth, 0.0, 1.0) * stepSec);
            laneOut[l] += (float)(1.0 - std::exp(-dt / tau)) * (laneTarget[l] - laneOut[l]);
            float depth = std::clamp(L.depth, 0.f, 1.f);
            if (L.target > 0 && L.target < numModTargetIds)
                tgtOff[L.target] += laneOut[l] * depth * srcDepthNow[l] * targetScale(L.target);
            // P-locks: a locked step pushes its target; unlocked steps glide back
            // the lock lane has its own length and rate (or follows the sequencer's)
            int ln = L.lockLength > 0 ? std::clamp(L.lockLength, 1, numSteps) : n;
            long lst = st;
            if (L.lockRateIndex > 0)
                lst = stepAt(pos * stepBeats / laneRateBeats[std::clamp(L.lockRateIndex - 1, 0, numLaneRates - 1)]);
            int lidx = stepIndex(lst, ln);
            lockIdx[l] = lidx;
            // each knob locked on this step goes to its locked value; the others glide back to the knob
            const auto &KB = s.mode[std::clamp(s.routing, 0, 1)].band[l];
            const float base[numBandParams] = {KB.cutoff, KB.resonance, KB.morph, KB.drive, KB.levelDb, KB.pan};
            float ga = 1.f - std::exp(-(float)dt / (std::max(0.2f, L.lockGlideMs) * 0.001f));
            for (int k = 0; k < numBandParams; ++k)
            {
                // this step's lock, or the latest earlier one still lasting (its length reaches this step)
                // v0.4: P-Locks are gone (the Snapshot sequencer took their place); old locks are ignored
                float lv = std::numeric_limits<float>::quiet_NaN();
                if (false)
                    for (int back = 1; back < ln; ++back)
                    {
                        int j = ((lidx - back) % ln + ln) % ln;
                        float cand = L.plock[k][j];
                        if (std::isfinite(cand))
                        {
                            if (std::clamp(s.gy.plockLen[l][j], 1, numSteps) > back)
                                lv = cand;
                            break;
                        }
                    }
                float want = std::isfinite(lv) ? lv - base[k] : 0.f;
                lockK[l][k] += ga * (want - lockK[l][k]);
                if (std::fabs(lockK[l][k]) < 1.0e-6f)
                    lockK[l][k] = 0.f;
                tgtLock[bandTarget(l, k)] += lockK[l][k];
            }
        }

        // ----- LFOs
        for (int i = 0; i < numLfos; ++i)
        {
            const auto &F = s.lfo[i];
            double cycleSec;
            if (F.mode == LfoFree)
            {
                double hz = std::clamp((double)F.rateHz, 0.005, 500.0);
                lfoFree[i] += hz * dt;
                if (lfoFree[i] > 1.0e6)
                    lfoFree[i] = std::fmod(lfoFree[i], 1.0);
                lfoPh[i] = lfoFree[i];
                cycleSec = 1.0 / hz;
            }
            else
            {
                int si = std::clamp(F.syncIndex, 0, 7);
                double cycBeats = lfoSyncLen[si] * (lfoSyncBars[si] ? beatsPerBar : 1.0);
                double b = F.mode == LfoRetrig ? beats - playStart : beats;
                lfoPh[i] = b / cycBeats;
                cycleSec = cycBeats * 60.0 / bpm;
            }
            double ph = lfoPh[i] + std::clamp((double)F.phase, 0.0, 1.0) + std::clamp((double)F.sway, 0.0, 1.0) * 0.25 * swOut[Mid];
            ph -= std::floor(ph);
            lfoPh[i] = ph;
            float v;
            if (F.shape == ShapeSteps)
                v = stepLfoValue(F, ph);
            else
            {
                const float *tab = F.shape == ShapeCustom ? customTable[i][curveActive[i].load(std::memory_order_acquire)]
                                                          : shapeTables[std::clamp(F.shape, 0, numShapes - 1)];
                double x = ph * curveN;
                int xi = std::min((int)x, curveN - 1);
                float fr = (float)(x - xi);
                v = tab[xi] + fr * (tab[xi + 1] - tab[xi]);
            }
            double tau = std::max(0.0005, std::clamp((double)F.smooth, 0.0, 1.0) * cycleSec * 0.15);
            lfoRaw[i] += (float)(1.0 - std::exp(-dt / tau)) * (v - lfoRaw[i]);
            lfoOut[i] = F.unipolar ? 0.5f * (lfoRaw[i] + 1.f) : lfoRaw[i];
            if (F.target > 0 && F.target < numModTargetIds)
                tgtOff[F.target] += lfoOut[i] * std::clamp(F.amount, -1.f, 1.f) * srcDepthNow[3 + i] * targetScale(F.target);
        }

        // ----- envelope follower (peak, attack / release, sensitivity)
        {
            float pk = 0.f;
            switch (E.source)
            {
            case EnvSidechain: pk = pkSc; break;
            case EnvLow: pk = pkBand[Low]; break;
            case EnvMid: pk = pkBand[Mid]; break;
            case EnvHigh: pk = pkBand[High]; break;
            default: pk = inPeakForEnv; break;
            }
            pk *= std::pow(10.f, std::clamp(E.sensDb, -24.f, 36.f) / 20.f);
            float att = 1.f - std::exp(-(float)dt / (std::max(0.1f, E.attackMs) * 0.001f));
            float rel = 1.f - std::exp(-(float)dt / (std::max(1.f, E.releaseMs) * 0.001f));
            efState += (pk > efState ? att : rel) * (pk - efState);
            if (efState < 1.0e-9f)
                efState = 0.f;
            efOut = std::clamp(efState, 0.f, 1.f);
        }

        // ----- the matrix
        {
            // Random: a smooth drift to a new value about every 1.5 s (no song sync: it is meant to wander)
            rndPh += dt / 1.5;
            if (rndPh >= 1.0)
            {
                rndPh -= std::floor(rndPh), rndFrom = rndTo;
                rndSeed = rndSeed * 1664525u + 1013904223u;
                rndTo = (float)(rndSeed >> 8) / 8388608.f - 1.f;
            }
            float u = (float)rndPh;
            rndOut = rndFrom + (rndTo - rndFrom) * u * u * (3.f - 2.f * u);
        }
        float src[numModSources] = {0.f, rndOut, std::clamp(s.gy.modWheel, 0.f, 1.f), std::clamp(s.gy.velocity, 0.f, 1.f),
                                    lfoOut[0], lfoOut[1], lfoOut[2], std::clamp(hymnEnv, 0.f, 1.f), ripOut[0], ripOut[1], ripOut[2]};
        // which sources only go one way (0..1): Wheel, Velocity, Hymn's envelope, and LFOs set to Unipolar
        const bool uni[numModSources] = {false, false, true, true, s.lfo[0].unipolar, s.lfo[1].unipolar, s.lfo[2].unipolar, true, false, false, false};
        for (const auto &sl : s.slot)
            if (sl.source > 0 && sl.source < numModSources && sl.target > 0 && sl.target < numModTargetIds)
            {
                float v = src[sl.source];
                if (sl.swing == 1 && uni[sl.source])
                    v = 2.f * v - 1.f; // both ways: swings below the knob as far as above
                else if (sl.swing == 2 && !uni[sl.source])
                    v = 0.5f * (v + 1.f); // one way: only ever up (or down, with a negative amount)
                v *= srcDepthNow[sl.source - 1];
                tgtOff[sl.target] += v * std::clamp(sl.amount, -1.f, 1.f) * targetScale(sl.target);
            }
        // the master Mod Depth scales everything that moves; P-locks are set by hand, so they stay as drawn
        modMaster = std::clamp(s.modDepth + tgtOff[TgtModDepth], 0.f, 2.f);
        // these move settings, not sounds: taken before the master depth scales everything
        for (int b = 0; b < numBands; ++b)
            stripDepthOff[b] = tgtOff[TgtStripDepthLow + b];
        specBalOff = tgtOff[TgtSpecBalance], specSharpOff = tgtOff[TgtSpecSharp];
        dispGlobal[TgtModDepth - TgtSplitLo] = modMaster * 0.5f;
        for (int t = 0; t < numModTargetIds; ++t)
            tgtOff[t] = tgtOff[t] * modMaster + tgtLock[t];
    }

    // The Conductor passes Mid's movement to Low and High. With a grid it does so in steps, on the
    // beat: each step may pass (Chance, bar Conditions, clock dividers), Chaos swaps Mid's own
    // movement for random jumps, Bursts and the Third wheel add sudden moves. All of it is a
    // function of the seed and the song position, so the same bar plays the same way.
    double condBeat{0};
    long condTick{-1}, condBar{-1}, condFine{-1};
    float condHeld[numBands]{}, condBurst[numBands]{}, condTw{0}, condSm[numBands]{};
    long jmTick{LONG_MIN};
    float jmT[numBands]{}, jmS[numBands]{}; // Jack's mischief: where each strip is knocked to, and its glide
    bool condBarOn{true}, condBurstBar{false}, condTwBar{false};
    int condBurstBeat{0}, condTwSign{1};
    static float snapMusical(float st, int m)
    {
        if (m == 1)
            return std::round(st);
        if (m == 2)
            return std::round(st / 7.f) * 7.f;
        if (m == 3)
            return std::round(st / 12.f) * 12.f;
        return st;
    }
    // The lock-in: when Lock goes on, the last N steps it played become a loop, so what you liked keeps coming back.
    // (Every step's jump is a function of its step number, so replaying the numbers replays the jumps.)
    bool chapLocked{false};
    long chapLockStart{0};
  public:
    bool chapLockedNow() const { return chapLocked; }
  private:
    long lockedTick(const Settings &s, long t)
    {
        const int n = std::clamp(s.gy.chapLockSteps, 1, 64);
        if (s.gy.chapLock && !chapLocked)
            chapLocked = true, chapLockStart = t - n + 1;
        else if (!s.gy.chapLock)
            chapLocked = false;
        if (!chapLocked)
            return t;
        long d = t - chapLockStart;
        return chapLockStart + ((d % n) + n) % n;
    }
    void runConductor(const Settings &s, float midMove, double dt)
    {
        const auto &C = s.conductor;
        float infl = std::clamp(C.influence, 0.f, 1.f), lean = std::clamp(C.lean, 0.f, 1.f);
        float share[numBands] = {1.f - lean, 0.f, lean};
        float chaos = std::clamp(C.chaos, 0.f, 1.f), range = std::clamp(C.range, 0.f, 48.f);
        uint64_t seed = (uint64_t)(uint32_t)C.seed;
        condBeat = beats;
        double bpb = std::max(1.0, beatsPerBar);
        long bar = (long)std::floor(condBeat / bpb);
        if (bar != condBar)
        {
            condBar = bar;
            int ci = std::clamp(C.condition, 0, numCondConditions - 1);
            int A = condConditions[ci][0], B = condConditions[ci][1];
            condBarOn = B == 0 || ((bar % B + B) % B) + 1 == A;
            condBurstBar = rand01(seed, (uint64_t)bar, 501) < std::clamp(C.bursts, 0.f, 1.f);
            condBurstBeat = (int)(rand01(seed, (uint64_t)bar, 502) * bpb);
        }
        float target[numBands]{};
        if (C.grid <= 0)
        {
            // continuous: Mid's movement (or its random walk) passed straight on
            float r = 0.f;
            if (chaos > 0.f)
            {
                long traw = (long)std::floor(condBeat / 0.25), t = lockedTick(s, traw);
                if (traw != condTick)
                    condTick = traw, condHeld[Mid] = snapMusical((rand01(seed, (uint64_t)t, 510) * 2.f - 1.f) * range, C.musical);
                r = condHeld[Mid];
            }
            float m = (1.f - chaos) * midMove + chaos * r;
            for (int b = 0; b < numBands; ++b)
                target[b] = condBarOn ? infl * share[b] * m : 0.f;
        }
        else
        {
            double gb = condGridBeats[std::clamp(C.grid - 1, 0, numCondGrid - 1)];
            long traw = (long)std::floor(condBeat / gb), t = lockedTick(s, traw);
            if (traw != condTick)
            {
                condTick = traw;
                float r = snapMusical((rand01(seed, (uint64_t)t, 511) * 2.f - 1.f) * range, C.musical);
                float m = (1.f - chaos) * snapMusical(midMove, C.musical) + chaos * r;
                bool pass = condBarOn && rand01(seed, (uint64_t)t, 512) < std::clamp(C.chance, 0.f, 1.f);
                if (pass && infl > 0.f)
                    chapStepped = true, ++chapSteps, chapDir = m >= 0.f ? 1 : -1; // the Chaperone passed something on (it can lead Mutation)
                int div[numBands] = {std::max(1, C.divLow), 1, std::max(1, C.divHigh)};
                for (int b = 0; b < numBands; ++b)
                    if (b != Mid && ((t % div[b]) + div[b]) % div[b] == 0)
                        condHeld[b] = pass ? infl * share[b] * m : (condBarOn ? condHeld[b] : 0.f);
            }
            for (int b = 0; b < numBands; ++b)
                target[b] = condHeld[b];
        }
        // bursts: one beat of 1/32 jumps; the third wheel: Mid grabs both for the first beat of a bar
        double inBar = condBeat - (double)bar * bpb;
        bool burstNow = condBurstBar && (int)std::floor(inBar) == condBurstBeat;
        long fine = (long)std::floor(condBeat / 0.125);
        if (fine != condFine)
        {
            condFine = fine;
            for (int b = 0; b < numBands; ++b)
                condBurst[b] = burstNow && b != Mid ? snapMusical((rand01(seed, (uint64_t)fine, 520 + (uint64_t)b) * 2.f - 1.f) * range, C.musical) : 0.f;
        }
        if (!burstNow)
            condBurst[Low] = condBurst[High] = 0.f;
        condTw = 0.f;
        // a few milliseconds of glide so jumps don't click
        float a = 1.f - std::exp(-(float)dt / 0.006f);
        for (int b = 0; b < numBands; ++b)
        {
            // stepped values glide; the smooth (continuous) pass-on goes straight through, as before
            bool stepped = C.grid > 0 || chaos > 0.f;
            float jumps = b == Mid ? 0.f : (stepped ? target[b] : 0.f) + infl * (condBurst[b] + condTw);
            condSm[b] += a * (jumps - condSm[b]);
            condCut[b] = b == Mid ? 0.f : condSm[b] + (stepped ? 0.f : target[b]);
        }
    }

    // A step value snapped to a scale. Values are -1..1 over +-24 semitones (the cutoff scale).
    static float quantizeStep(float v, int q)
    {
        if (q <= 0)
            return v;
        float st = std::clamp(v, -1.f, 1.f) * 24.f;
        if (q == 5)
            return std::round(st / 12.f) * 12.f / 24.f;
        if (q == 1)
            return std::round(st) / 24.f;
        static const int major[7] = {0, 2, 4, 5, 7, 9, 11}, minor[7] = {0, 2, 3, 5, 7, 8, 10}, penta[5] = {0, 3, 5, 7, 10};
        const int *sc = q == 2 ? major : q == 3 ? minor : penta;
        int n = q == 4 ? 5 : 7;
        float best = 0, bd = 1e9f;
        for (int oct = -3; oct <= 2; ++oct)
            for (int kk = 0; kk < n; ++kk)
            {
                float c = (float)(oct * 12 + sc[kk]);
                if (std::fabs(c - st) < bd)
                    bd = std::fabs(c - st), best = c;
            }
        return std::clamp(best, -24.f, 24.f) / 24.f;
    }
    // The Steps shape at phase ph: the step's value, gliding in from the last one
    static float stepLfoValue(const LfoSettings &F, double ph)
    {
        int n = std::clamp(F.stepCount, 1, 16);
        double pos = ph * n;
        int kk = std::min(n - 1, (int)pos);
        float fr = (float)(pos - kk);
        float v = quantizeStep(F.step[kk], F.quantize);
        float g = std::clamp(F.glide, 0.f, 1.f);
        if (g > 0.f && fr < g)
        {
            float prev = quantizeStep(F.step[(kk + n - 1) % n], F.quantize);
            float t = fr / g;
            t = t * t * (3.f - 2.f * t);
            v = prev + (v - prev) * t;
        }
        return v;
    }

    // Works out this control block's Sway, Conductor and Mutation offsets into im[].
    float inPeakForEnv{0};
    void runModulation(const Settings &s, BandMod im[numBands])
    {
        inPeakForEnv = inPeak;
        inPeak = 0.f;
        const double dt = (double)blockSize / sampleRate;
        timeSec += dt;
        beats += dt * bpm / 60.0; // replaced by the host's song position while playing
        const double stepSec = 15.0 / bpm; // one sixteenth

        // input follower (for Sway's Follow and the Follower trigger)
        {
            float a = inPeakForEnv;
            float att = 1.f - std::exp(-(float)dt / 0.010f), rel = 1.f - std::exp(-(float)dt / 0.300f);
            env += (a > env ? att : rel) * (a - env);
            envSlow += (1.f - std::exp(-(float)dt / 0.600f)) * (a - envSlow);
            if (env < 1.0e-7f)
                env = 0.f;
            if (envSlow < 1.0e-7f)
                envSlow = 0.f;
        }

        // ----- Phase 3 sources: lanes, LFOs, follower; then the matrix
        runSources(s, dt);

        // ----- Tune (after the matrix, so its mod targets are in)
        {
            const auto &T = s.tune;
            tuneOn = T.on, tuneBefore = T.beforeStrips, tuneLayer = T.layer;
            tuneAmt = std::clamp(T.amount + tgtOff[TgtTuneAmount], 0.f, 1.f);
            // only the ringing (the switch, or Amount at 100 %): it is made from your sound before the delays and
            // replaces it everywhere, so whatever Mix is you hear the resonators and their echoes, never the sound itself
            tuneOnly = T.on && (s.gy.tuneOnly || T.amount >= 0.995f); // the knob, not modulation passing 100 %
            if (tuneOnly)
                tuneBefore = true, tuneLayer = false;
            {
                const auto &G = s.gy;
                hymnShapeNow = std::clamp(G.hymnShape, 0, 3);
                const float fsr = (float)sampleRate;
                hymnAttInc = 1.f / std::max(1.f, std::clamp(G.hymnAttackMs, 0.5f, 8000.f) * 0.001f * fsr);
                hymnSus = std::clamp(G.hymnSustain, 0.f, 1.f);
                hymnDecA2 = 1.f - std::exp(-3.f / (std::clamp(G.hymnDecayMs, 5.f, 8000.f) * 0.001f * fsr));
                hymnRelA = 1.f - std::exp(-3.f / (std::clamp(G.hymnReleaseMs, 5.f, 8000.f) * 0.001f * fsr));
                hymnThr = std::pow(10.f, std::clamp(G.hymnThreshDb, -70.f, 0.f) / 20.f);
                hymnHoldN = (int)(std::clamp(G.hymnHoldMs, 0.f, 2000.f) * 0.001f * (float)sampleRate);
                hymnDecA = 1.f - std::exp(-1.f / (std::clamp(G.hymnDecayMs, 5.f, 8000.f) * 0.001f * (float)sampleRate) * 3.f);
            }
            if (tuneOn)
            {
                // ring time moves by up to 3 octaves of time at 100 %; both rounded so the notes are only re-tuned on a real change
                float ring = std::clamp(T.ringMs * std::exp2(3.f * tgtOff[TgtTuneRing]), 20.f, 4000.f);
                ring = std::exp2(std::round(std::log2(ring) * 24.f) / 24.f);
                float tilt = std::round(std::clamp(T.tilt + tgtOff[TgtTuneTilt], -1.f, 1.f) * 50.f) / 50.f;
                tune.configure(T, ring, tilt);
                tune.setTone(std::clamp(T.slope + tgtOff[TgtTuneSlope], -1.f, 1.f), T.pivotHz);
            }
            // Motion runs even with Tune off, so the picture on the Tune page keeps moving with the song
            double div = tuneDivBeats[std::clamp(T.motionDiv, 0, numTuneDivs - 1)];
            double divBeats = div > 0 ? div : -div * beatsPerBar;
            tune.moveControl(T.motion, std::clamp(T.motionHz, 0.01f, 8.f), blockSize, T.motionSync, beats / std::max(1.0e-3, divBeats));
        }

        // ----- Sway: each band has two slow LFOs at rates that never line up, its own speed
        // (against the main rate), Smooth or Stepped, and its own share of the main Sway
        const auto &W = s.sway;
        static constexpr double ratio[4] = {1.0, 1.6180339887, 1.4142135624, 0.7853981634};
        static constexpr double off[4] = {0.0, 0.31, 0.57, 0.83};
        static constexpr int pairA[numBands] = {0, 2, 0}, pairB[numBands] = {1, 3, 3};
        static constexpr double wA[numBands] = {0.6, 0.7, -0.6}, wB[numBands] = {0.4, 0.3, 0.4}; // High leans away from Low
        const double cycleBeats = swayBarsTable[std::clamp(W.syncIndex, 0, 6)] * beatsPerBar;
        const double hz = std::clamp((double)W.rateHz, 0.005, 20.0);
        for (int b = 0; b < numBands; ++b)
        {
            const auto &WB = W.band[b];
            double mul = std::clamp((double)WB.rateMul, 0.25, 4.0), baseTotal, pa, pb;
            if (W.sync)
            {
                baseTotal = beats / cycleBeats * mul;
                pa = baseTotal * ratio[pairA[b]] + off[pairA[b]];
                pb = baseTotal * ratio[pairB[b]] + off[pairB[b]];
                swBandPh[b][0] = pa, swBandPh[b][1] = pb, swBandBase[b] = baseTotal;
            }
            else
            {
                swBandBase[b] += hz * mul * dt;
                swBandPh[b][0] += hz * mul * ratio[pairA[b]] * dt;
                swBandPh[b][1] += hz * mul * ratio[pairB[b]] * dt;
                for (double *x : {&swBandBase[b], &swBandPh[b][0], &swBandPh[b][1]})
                    if (*x > 1.0e6)
                        *x = std::fmod(*x, 1.0);
                baseTotal = swBandBase[b], pa = swBandPh[b][0], pb = swBandPh[b][1];
            }
            float raw = (float)(wA[b] * std::sin(6.283185307 * (pa - std::floor(pa))) + wB[b] * std::sin(6.283185307 * (pb - std::floor(pb))));
            bool stepped = WB.motion == 0 ? W.stepped : WB.motion == 2;
            if (stepped)
            {
                long st = (long)std::floor(baseTotal * 8.0);
                if (st != swBandStep[b])
                    swBandStep[b] = st, swHold[b] = raw;
                double cycleSec = (W.sync ? cycleBeats * 60.0 / bpm : 1.0 / hz) / mul;
                float slew = WB.motion == 0 ? W.slew : WB.slew;
                double tau = std::max(0.001, std::clamp((double)slew, 0.0, 1.0) * cycleSec / 8.0 * 0.5);
                swOut[b] += (float)(1.0 - std::exp(-dt / tau)) * (swHold[b] - swOut[b]);
            }
            else
                swOut[b] = raw;
        }
        float follow = std::clamp(W.follow, 0.f, 1.f);
        float depthScale = (1.f - follow) + follow * std::clamp(env * 4.f, 0.f, 1.5f);
        float sw[numBands], swU[numBands];
        for (int b = 0; b < numBands; ++b)
        {
            swU[b] = std::clamp(W.amount + tgtOff[TgtSwayAmt], 0.f, 1.f) * std::clamp(W.band[b].depth, 0.f, 1.f) * swOut[b] * depthScale;
            sw[b] = swU[b] * std::clamp(W.depthSemis, 0.f, 48.f) * std::clamp(W.toCut, 0.f, 1.f);
        }

        // ----- Conductor: Mid's cutoff movement (its Sway plus host modulation) is passed on
        // Mid's movement: everything moving its cutoff (Sway, lanes, LFOs, matrix, Bitwig)
        float midMove = sw[Mid] + s.mod[Mid].cutoff + tgtOff[bandTarget(Mid, PCut)];
        runConductor(s, midMove, dt);

        // ----- Mutation
        const auto &M = s.mutation;
        // the answers after Mutate: a function of the seed and how many periods have passed,
        // so the same bar always plays the same answers
        {
            long k = 0;
            if (M.mutate > 0.f)
            {
                double every = mutEveryTable[std::clamp(M.everyIndex, 0, 4)];
                double per = std::max(0.0, beats) / beatsPerBar / every;
                k = (long)std::floor(per);
                mutFrac = (float)(per - (double)k);
                k = std::min(k, 4096L);
            }
            else
                mutFrac = 0.f;
            if (M.freeze)
            {
                if (!wasFrozen)
                    frozenK = k;
                k = frozenK;
            }
            wasFrozen = M.freeze;
            if (k != mutK || M.seed != lastSeed || M.mutate != lastMutate || !sameResp(M.resp[0], lastParams[0]) ||
                !sameResp(M.resp[1], lastParams[1]))
            {
                mutK = k;
                lastSeed = M.seed;
                lastMutate = M.mutate;
                lastParams[0] = M.resp[0];
                lastParams[1] = M.resp[1];
                effResp[0] = M.resp[0];
                effResp[1] = M.resp[1];
                float m = std::clamp(M.mutate, 0.f, 1.f);
                if (m > 0.f)
                    for (long i = 0; i < k; ++i)
                        mutateStep(effResp, M.resp, M.seed, i, m);
                for (int r = 0; r < 2; ++r)
                    effResp[r].target = std::clamp(effResp[r].target, 0, numMutTargets - 1);
            }
        }
        // detect an event in Mid
        float sens = std::clamp(M.sensitivity, 0.f, 1.f);
        {
            float pk = midPeak; // copied by runSources this block
            midFast = std::max(pk, midFast * std::exp(-(float)dt / 0.030f));
            midSlow += (1.f - std::exp(-(float)dt / 0.400f)) * (pk - midSlow);
            if (midFast < 1.0e-7f)
                midFast = 0.f;
            if (midSlow < 1.0e-7f)
                midSlow = 0.f;
        }
        float prevSlowMove = moveSlow;
        moveSlow += (1.f - std::exp(-(float)dt / 0.050f)) * (midMove - moveSlow);
        bool fire = false;
        int dir = 1;
        float ratioThr = 1.4f + (1.f - sens) * 2.6f; // +3 to +12 dB over the recent level
        switch (std::clamp(M.trigger, 0, numMutTriggers - 1))
        {
        case TrigLevel:
            if (lvlArmed && midFast > 0.01f && midFast > midSlow * ratioThr)
                fire = true, lvlArmed = false;
            else if (midFast < midSlow * 1.1f)
                lvlArmed = true;
            break;
        case TrigCutoff:
        {
            float jump = midMove - prevSlowMove, thr = 0.5f + (1.f - sens) * 6.f;
            if (cutArmed && std::fabs(jump) > thr)
                fire = true, cutArmed = false, dir = jump >= 0 ? 1 : -1;
            else if (std::fabs(jump) < thr * 0.5f)
                cutArmed = true;
            break;
        }
        case TrigGuard:
            if (guardArmed && rw[Mid].gain < 0.85f) // Gullay: Runaway catching Lead's loop
                fire = true, guardArmed = false, dir = -1;
            else if (rw[Mid].gain > 0.95f)
                guardArmed = true;
            break;
        case TrigChaperone: // the Chaperone leads: each step it passes on sets off a mutation
            if (chapStepped)
                fire = true, dir = chapDir;
            chapStepped = false;
            break;
        case TrigStep:
        {
            // Mid's own sequencer lane when it is doing something, otherwise Bitwig's 1/16 grid
            long st = (long)std::floor(beats * 4.0);
            if (midLaneActive(s))
                fire = midLaneStepped;
            else if (stepPrev != LONG_MIN && st != stepPrev)
                fire = true;
            stepPrev = st;
            float d = midMove - prevSlowMove;
            dir = std::fabs(d) < 0.05f ? ((st & 1) ? -1 : 1) : (d > 0 ? 1 : -1);
            break;
        }
        case TrigFollower:
            if (envArmed && env > 0.01f && env > envSlow * ratioThr)
                fire = true, envArmed = false;
            else if (env < envSlow * 1.1f)
                envArmed = true;
            break;
        }
        if (M.trigger != TrigStep)
            stepPrev = LONG_MIN;
        midLaneStepped = false;
        double hold = std::max(0.015, std::clamp(M.holdSteps, 0, 16) * stepSec);
        if (fire && timeSec - lastEvent < hold - 1.5 * dt) // events right on the limit still count
            fire = false;
        if (fire)
        {
            lastEvent = timeSec;
            ++eventCount;
            MutEvent ev;
            ev.dir = dir;
            for (int r = 0; r < 2; ++r)
            {
                const auto &R = effResp[r];
                if (R.mode == MutOff)
                    continue;
                if (rand01((uint64_t)(uint32_t)M.seed, eventCount, (uint64_t)r + 1000) > R.chance)
                    continue;
                float v = (float)dir * (R.mode == MutTandem ? 1.f : -1.f) * std::clamp(R.amount, 0.f, 1.f);
                (r == 0 ? ev.low : ev.high) = R.mode == MutTandem ? dir : -dir;
                double at = timeSec + std::clamp(R.delaySteps, 0, 4) * stepSec;
                for (auto &p : pend[r])
                    if (!p.live)
                    {
                        p = {at, v, true};
                        break;
                    }
            }
            hist[histPos] = ev;
            histPos = (histPos + 1) % 16;
        }
        // play the answers: each one jumps in quickly and fades back over about a third of a second
        float kDecay = std::exp(-(float)dt / 0.300f), kAtt = 1.f - std::exp(-(float)dt / 0.004f);
        for (int r = 0; r < 2; ++r)
        {
            if (effResp[r].target != respTgt[r])
            {
                respTgt[r] = effResp[r].target;
                respKick[r] = respVal[r] = 0.f;
            }
            for (auto &p : pend[r])
                if (p.live && timeSec >= p.at)
                {
                    respKick[r] = std::clamp(respKick[r] + p.val, -1.f, 1.f);
                    p.live = false;
                }
            respKick[r] *= kDecay;
            respVal[r] += kAtt * (respKick[r] - respVal[r]);
            if (std::fabs(respKick[r]) < 1.0e-6f && std::fabs(respVal[r]) < 1.0e-6f)
                respKick[r] = respVal[r] = 0.f;
        }

        // ----- add it all up
        for (int b = 0; b < numBands; ++b)
            im[b] = BandMod{};
        for (int b = 0; b < numBands; ++b)
            sw[b] *= modMaster, swU[b] *= modMaster;
        for (int b = 0; b < numBands; ++b)
        {
            im[b].cutoff = sw[b] + condCut[b] + tgtOff[bandTarget(b, PCut)];
            im[b].resonance = tgtOff[bandTarget(b, PRes)] + 0.5f * W.toRes * swU[b];
            im[b].morph = tgtOff[bandTarget(b, PMorph)] + 0.5f * W.toMorph * swU[b];
            im[b].drive = tgtOff[bandTarget(b, PDrive)] + 0.5f * W.toDrive * swU[b];
            im[b].levelDb = tgtOff[bandTarget(b, PLevel)] + 12.f * W.toLevel * swU[b];
            im[b].pan = tgtOff[bandTarget(b, PPan)] + W.toPan * swU[b];
        }
        // ----- Jack: one strip moves, another moves the same amount the other way
        {
            const auto &C = s.conductor;
            float jk = std::clamp(C.jack, 0.f, 1.f), mis = std::clamp(C.mischief, 0.f, 1.f);
            // the mischief: on some half beats Jack knocks one strip (any of the three) a little out of
            // line for a beat or so, on the knob Jack moves. Random, but the same with the same seed.
            {
                long t = (long)std::floor(beats / 0.5);
                if (t != jmTick)
                {
                    jmTick = t;
                    uint64_t sd = (uint64_t)(uint32_t)C.seed + 991;
                    if (rand01(sd, (uint64_t)t, 530) < mis * 0.6f)
                    {
                        int b = std::min((int)(rand01(sd, (uint64_t)t, 531) * 3.f), 2);
                        float amt = (rand01(sd, (uint64_t)t, 532) * 2.f - 1.f) * (1.5f + 7.f * mis); // semitones
                        jmT[b] = amt;
                    }
                    else if (rand01(sd, (uint64_t)t, 533) < 0.5f)
                        for (auto &v : jmT)
                            v = 0.f; // and back in line
                }
                if (mis <= 0.f)
                    for (auto &v : jmT)
                        v = 0.f;
                float ga = 1.f - std::exp(-(float)dt / 0.03f);
                for (int b = 0; b < numBands; ++b)
                    jmS[b] += ga * (jmT[b] - jmS[b]);
            }
            if (jk > 0.f || mis > 0.f)
            {
                float mv[numBands];
                for (int b = 0; b < numBands; ++b)
                    mv[b] = im[b].cutoff - tgtLock[bandTarget(b, PCut)]; // how far it is being moved (not its P-locks)
                float push[numBands]{};
                switch (std::clamp(C.jackPair, 0, 5))
                {
                case 0: push[Low] = -mv[High], push[High] = -mv[Low]; break; // Low <-> High
                case 1: push[Low] = push[High] = -mv[Mid]; break;           // Mid against both
                case 2: push[High] = -mv[Low]; break;                       // Low -> High
                case 3: push[Low] = -mv[High]; break;                       // High -> Low
                case 4: push[Low] = -mv[Mid]; break;                        // Mid -> Low
                case 5: push[High] = -mv[Mid]; break;                       // Mid -> High
                }
                for (int b = 0; b < numBands; ++b)
                {
                    float v = jk * push[b] + jmS[b]; // semitones
                    switch (std::clamp(C.jackParam, 0, numBandParams - 1))
                    {
                    case PCut: im[b].cutoff += v; break;
                    case PRes: im[b].resonance += v / 48.f; break;
                    case PMorph: im[b].morph += v / 48.f; break;
                    case PDrive: im[b].drive += v / 48.f; break;
                    case PLevel: im[b].levelDb += v * 0.5f; break;
                    case PPan: im[b].pan += v / 24.f; break;
                    }
                }
            }
        }
        for (int r = 0; r < 2; ++r)
        {
            int b = r == 0 ? Low : High;
            float v = respVal[r];
            switch (respTgt[r])
            {
            case TgtCutoff: im[b].cutoff += 24.f * v * modMaster; break;
            case TgtReso: im[b].resonance += 0.5f * v * modMaster; break;
            case TgtDrive: im[b].drive += 0.5f * v * modMaster; break;
            case TgtLevel: im[b].levelDb += 12.f * v * modMaster; break;
            case TgtPan: im[b].pan += v * modMaster; break;
            // the rest move settings outside the parts (A answers on the low / first side, B on the high / second)
            case TgtMutTone: im[b].morph += 0.4f * v * modMaster; break;
            case TgtMutPattern: tgtOff[TgtPattern] += 8.f * v * modMaster; break;
            case TgtMutFilter: tgtOff[TgtFiltCut] += 24.f * v * modMaster, tgtOff[TgtFiltCutA] += 24.f * v * modMaster, tgtOff[TgtFiltCutB] += 24.f * v * modMaster; break;
            case TgtMutCrush: tgtOff[TgtCrushBits] += 6.f * v * modMaster; break;
            case TgtMutHymn: tgtOff[TgtTuneAmount] += 0.5f * v * modMaster; break;
            case TgtMutShift: tgtOff[r == 0 ? TgtShiftA : TgtShiftB] += 7.f * v * modMaster; break;
            case TgtMutDegrade: tgtOff[TgtDegrade] += 3.f * v * modMaster; break;
            case TgtMutTilt: tgtOff[r == 0 ? TgtTiltIn : TgtTiltOut] += 6.f * v * modMaster; break;
            case TgtMutBand: tgtOff[r == 0 ? TgtSpecLowLvl : TgtSpecHighLvl] += 12.f * v * modMaster; break;
            }
        }
        // each strip's own Mod Depth (its P-locks stay as drawn)
        const auto &MS = s.mode[std::clamp(s.routing, 0, 1)];
        for (int b = 0; b < numBands; ++b)
        {
            float d = std::clamp(MS.band[b].modDepth + stripDepthOff[b], 0.f, 2.f);
            dispGlobal[TgtStripDepthLow + b - TgtSplitLo] = d * 0.5f;
            auto sc = [&](float &x, int p) { float lk = tgtLock[bandTarget(b, p)]; x = lk + d * (x - lk); };
            sc(im[b].cutoff, PCut), sc(im[b].resonance, PRes), sc(im[b].morph, PMorph), sc(im[b].drive, PDrive), sc(im[b].levelDb, PLevel), sc(im[b].pan, PPan);
        }
    }

    // ----- control rate (every 8 host samples) ---------------------------------------------
    void processControl(const Settings &s)
    {
        bool rebuildAll = false;
        if (recovering)
        {
            recovering = false;
            clearAudioState();
            limGain = 1.f;
            rebuildAll = true;
            needsSnap = true;
        }
        int os = 0; // Gullay runs at the host rate
        if (os != osStages)
        {
            osStages = os;
            configureRates();
            clearAudioState();
            rebuildAll = true;
            needsSnap = true;
        }

        // Routing change: fade out (~6 ms), swap to the other routing's own settings with
        // clean state, fade back in.
        int wantRouting = std::clamp(s.routing, 0, 1);
        bool wantSpec = wantRouting == Parallel && s.splitMode == 1;
        int wantQ = std::clamp(s.specQuality, 0, 3);
        if (curRouting < 0)
        {
            curRouting = wantRouting, spectralOn = wantSpec;
            if (wantQ != specQ)
                specQ = wantQ, configureRates();
        }
        if ((wantRouting != curRouting || wantSpec != spectralOn || (wantSpec && wantQ != specQ)) && fadeState != FadeOut)
        {
            fadeState = FadeOut;
            fadeCount = fadeBlocks;
        }
        if (fadeState == FadeOut)
        {
            if (fadeCount > 0)
            {
                fade.to(0.f, fadeCount * nInner);
                --fadeCount;
            }
            else
            {
                fade.snap(0.f);
                curRouting = wantRouting;
                spectralOn = wantSpec;
                if (wantQ != specQ)
                    specQ = wantQ, spec->setup(innerRate, 1 << osStages, 1024 << specQ);
                spec->reset();
                clearAudioState();
                rebuildAll = true;
                needsSnap = true;
                fadeState = FadeIn;
                fadeCount = fadeBlocks;
            }
        }
        if (fadeState == FadeIn)
        {
            if (fadeCount > 0)
            {
                fade.to(1.f, fadeCount * nInner);
                --fadeCount;
            }
            else
            {
                fade.snap(1.f);
                fadeState = FadeIdle;
            }
        }

        const ModeSettings &m = s.mode[curRouting];
        const bool par = curRouting == Parallel;

        // Sway, Conductor and Mutation, added to the host's modulation
        BandMod im[numBands];
        runModulation(s, im);
        BandMod md[numBands];
        for (int i = 0; i < numBands; ++i)
        {
            md[i].cutoff = s.mod[i].cutoff + im[i].cutoff;
            md[i].resonance = s.mod[i].resonance + im[i].resonance;
            md[i].morph = s.mod[i].morph + im[i].morph;
            md[i].drive = s.mod[i].drive + im[i].drive;
            md[i].levelDb = s.mod[i].levelDb + im[i].levelDb;
            md[i].pan = s.mod[i].pan + im[i].pan;
        }

        // ===== Gullay from here: the parts, the engines and everything around them =====
        const auto &G = s.gy;
        freeRun = G.freeRun, freeBpm = std::clamp(G.freeBpm, 30.f, 300.f), freePlay = G.freePlay;
        auto sm = [this](float &v, float t)
        {
            if (needsSnap)
                v = t;
            else
                v += knobAlpha * (t - v);
        };
        for (int i = 0; i < numBands; ++i)
        {
            const auto &B = m.band[i];
            sm(k.co[i], B.cutoff);
            sm(k.re[i], B.resonance);
            sm(k.mo[i], B.morph);
            sm(k.dr[i], B.drive);
            sm(k.lv[i], B.levelDb);
            sm(k.pan[i], B.pan);
        }
        sm(k.inDb, m.inputDb);
        sm(k.outDb, m.outputDb);
        sm(k.mix, m.mix);
        float co[numBands], re[numBands], mo[numBands], dr[numBands], lv[numBands], pn[numBands];
        for (int i = 0; i < numBands; ++i)
        {
            co[i] = std::clamp(k.co[i] + md[i].cutoff, -60.f, 60.f);
            re[i] = std::clamp(k.re[i] + md[i].resonance, 0.f, 1.2f);
            mo[i] = std::clamp(k.mo[i] + md[i].morph, 0.f, 1.f);
            dr[i] = std::clamp(k.dr[i] + md[i].drive, 0.f, 1.f);
            lv[i] = std::clamp(k.lv[i] + md[i].levelDb, -60.f, 12.f);
            pn[i] = std::clamp(k.pan[i] + md[i].pan, -1.f, 1.f);
            dispBand[i][PCut] = co[i], dispBand[i][PRes] = re[i], dispBand[i][PMorph] = mo[i];
            dispBand[i][PDrive] = dr[i], dispBand[i][PLevel] = lv[i], dispBand[i][PPan] = pn[i];
            dispCo[i] = co[i], dispRe[i] = re[i], press[i] = 0.f;
        }
        for (int p = 0; p < numBands; ++p)
            curBandInf[p] = m.band[p].inf, curBandRev[p] = m.band[p].reverse;
        // ----- the engine layout
        int s1 = std::clamp(G.slot1, 0, 1), s2 = G.slot2 < 0 ? -1 : std::clamp(G.slot2, 0, 1);
        int mode2 = s2 < 0 ? std::clamp(G.slot2Mode, 0, 5) : S2Engine;
        if (s1 != curSlot[0] || s2 != curSlot[1] || mode2 != curMode2 || rebuildAll)
        {
            curSlot[0] = s1, curSlot[1] = s2, curMode2 = mode2;
            for (auto &ch : dld)
                for (auto &d : ch)
                    d.clear();
            for (auto &t : tapo)
                t.clear();
            mirror.clear();
            std::fill(crL.begin(), crL.end(), 0.f), std::fill(crR.begin(), crR.end(), 0.f);
            std::fill(fzL.begin(), fzL.end(), 0.f), std::fill(fzR.begin(), fzR.end(), 0.f), fzLen = 0, fzPos = 0, stStart = -1;
            octSh[0].reset(), octSh[1].reset();
            std::fill(ancBuf.begin(), ancBuf.end(), 0.f), std::fill(std::begin(ancLp), std::end(ancLp), 0.f), std::fill(std::begin(ancG), std::end(ancG), 0.f);
            for (auto &r : rw)
                r.reset();
            loaded[0] = loaded[1] = -1;
        }
        wiring = std::clamp(s.routing, 0, 1) == Series ? 1 : (s.splitMode == 1 ? 2 : 0); // 0 parallel, 1 series, 2 spectral
        // ----- time: semitones of time; A and B are beats (0 = one beat), Lead multiplies both
        double beatSec = 60.0 / std::max(1.0, bpm);
        // Lead's Sync snaps its knob to a musical stretch; its modulation then moves smoothly from there
        double leadMul = m.band[Mid].sync
                             ? nearestDiv(leadDivs, std::exp2((double)k.co[Mid] / 12.0)).beats * std::exp2((double)md[Mid].cutoff / 12.0)
                             : std::exp2((double)co[Mid] / 12.0);
        auto snapRatio = [](double r) { return nearestDiv(noteDivs, r).beats; };
        for (int p : {0, 2})
        {
            // Sync snaps the knob to a note value; the movement on top (Sway, LFOs, River ...) stays smooth
            double r = m.band[p].sync ? snapRatio(std::exp2((double)k.co[p] / 12.0)) * std::exp2((double)(co[p] - k.co[p]) / 12.0)
                                      : std::exp2((double)co[p] / 12.0);
            partBeats[p] = r * leadMul;
            partSec[p] = std::clamp(partBeats[p] * beatSec, 0.002, 20.0);
        }
        partSec[1] = 0.5 * (partSec[0] + partSec[2]);
        // ----- feedback (Hold pushes every part to 112 %, then glides back over Return)
        float holdT = G.hold ? 1.f : 0.f;
        float hr = G.hold ? 0.02f : (float)std::clamp((double)blockSize / (G.returnSec * sampleRate), 1e-5, 1.0);
        holdAmt += hr * (holdT - holdAmt);
        float lead = re[Mid];
        {
            // Oscillate: a slow swell into self-oscillation (about 4 s), let go over 2 s (Tails) or killed at once (Cut)
            const float oscT = G.oscillate ? 1.f : 0.f;
            if (oscWas && !G.oscillate && G.oscCut)
                killReq = true, oscAmt = 0.f;
            oscWas = G.oscillate;
            const float up = (float)blockSize / (4.0f * (float)sampleRate), down = (float)blockSize / (2.0f * (float)sampleRate);
            oscAmt += oscT > oscAmt ? std::min(up, oscT - oscAmt) : -std::min(down, oscAmt - oscT);
        }
        for (int p : {0, 2})
        {
            float f = std::clamp(re[p] * lead, 0.f, 1.2f);
            f += oscAmt * (std::max(f, 1.15f) - f);
            partFb[p] = f + holdAmt * (1.12f - f);
        }
        partFb[1] = 0.5f * (partFb[0] + partFb[2]);
        crossAmt = std::clamp(G.chainCross, 0.f, 0.95f), wireTone = std::clamp(G.wireTone, -1.f, 1.f), wireDrive = std::clamp(G.wireDrive, 0.f, 1.f);
        variNow = G.varispeed; // v0.8: in Host sync too (it only worked with Sync on Free)
        variA = 1.0 - std::exp(-1.0 / (std::clamp((double)G.variGlideMs, 10.0, 10000.0) * 0.001 * sampleRate / 3.0));
        // ----- tone, drive, level, pan (Lead adds its tone and drive, and is the master level and pan)
        float tame = std::clamp(G.tame + tgtOff[TgtFeedback], 0.f, 1.f);
        float ceil = std::clamp(G.ceilingDb + tgtOff[TgtCeiling], -36.f, -6.f);
        for (int p = 0; p < numBands; ++p)
        {
            float t = p == Mid ? mo[p] : std::clamp(mo[p] + (mo[Mid] - 0.5f), 0.f, 1.f);
            tone[p].set(t, sampleRate);
            rw[p].drive = p == Mid ? dr[p] : std::clamp(dr[p] + dr[Mid], 0.f, 1.f);
            rw[p].tame = tame, rw[p].ceilingDb = ceil, rw[p].os = std::clamp(s.oversample, 0, 2);
            rw[p].setCuts(G.loopLowHz, G.loopHighHz);
            float gl = dbToLin(lv[p]);
            if (m.band[p].mute || (anySoloOn(m) && !m.band[p].solo && p != Mid))
                gl = p == Mid ? gl : 0.f;
            float th = (pn[p] + 1.f) * 0.78539816f;
            float pl = std::cos(th) * 1.41421356f, pr = std::sin(th) * 1.41421356f;
            if (std::fabs(pn[p]) < 1.0e-6f)
                pl = pr = 1.f;
            if (needsSnap)
                partL[p].snap(gl * pl), partR[p].snap(gl * pr);
            else
                partL[p].to(gl * pl, fifoN), partR[p].to(gl * pr, fifoN);
        }
        // balance between the slots (two engines, Parallel or Spectral)
        float bal = std::clamp(G.balance + tgtOff[TgtSplitHi], -1.f, 1.f);
        balA = (s2 >= 0 && wiring != 1) ? std::min(1.f, 1.f - bal) : 1.f;
        balB = (s2 >= 0 && wiring != 1) ? std::min(1.f, 1.f + bal) : 1.f;
        // ----- the engines' settings
        float degrade = std::clamp(G.degradeBits + tgtOff[TgtDegrade], 11.f, 16.f);
        if (G.tape != tapeOn)
            for (auto &t : tape)
                t.reset();
        tapeOn = G.tape;
        if (tapeOn)
            for (auto &t : tape)
                t.set(G.tapeDrive, G.tapeSat, G.tapeBias);
        {
            float want = std::clamp(G.memRateHz, 5000.f, 44100.f);
            if (want != memRate)
            {
                bool was = memRate < 44000.f;
                memRate = want, memStep = want / (float)sampleRate;
                float fc = std::min(0.42f * want, 0.45f * (float)sampleRate);
                for (auto *arr : {memDec, memIn})
                    for (int j = 0; j < 2; ++j)
                    {
                        arr[j].setLowpass(fc, (float)sampleRate);
                        if (!was)
                            arr[j].reset();
                    }
                for (auto &d : memLoop)
                {
                    d.setLowpass(fc, (float)sampleRate);
                    if (!was)
                        d.reset();
                }
            }
        }
        if (tapNew.load(std::memory_order_acquire))
        {
            patterns[(size_t)(tapoSlots - 1)] = tapPending;
            for (int sl = 0; sl < 2; ++sl)
                if (loaded[sl] == tapoSlots - 1)
                    loaded[sl] = -1; // reload it
            tapNew.store(false, std::memory_order_release);
        }
        // each Tapo slot has its own pattern, morph and taps (slot 2 uses its own only when slot 1 is a Tapo too;
        // a single Tapo in slot 2 takes the main Tapo settings)
        const bool twoTapos = s1 == SlotTapo && s2 == SlotTapo;
        int patS[2];
        float morphSS[2];
        for (int sl = 0; sl < 2; ++sl)
        {
            const bool own2 = sl == 1 && twoTapos;
            const auto &T2 = G.tapo2;
            int pBase = own2 ? T2.pattern : G.pattern;
            bool msync = own2 ? T2.morphSync : G.morphSync;
            int mdiv = own2 ? T2.morphDiv : G.morphDiv;
            float msec = own2 ? T2.morphSec : G.morphSec;
            {
                // v0.8: a pattern number that is moving (a smoothed LFO, a glide) used to load every pattern it passed,
                // several per block, tearing the taps up. Now a new number counts once it has held for 12 ms.
                const int raw = std::clamp((int)std::lround(pBase + tgtOff[TgtPattern]), 0, tapoSlots - 1);
                if (raw != patWant[sl])
                    patWant[sl] = raw, patWantT[sl] = timeSec;
                if (patHeld[sl] < 0 || timeSec - patWantT[sl] >= 0.012)
                    patHeld[sl] = patWant[sl];
                patS[sl] = patHeld[sl];
            }
            float mb = msync ? morphDivBeats[std::clamp(mdiv, 0, 7)] * (float)beatSec : msec;
            morphSS[sl] = std::clamp(mb * std::exp2(tgtOff[TgtMorph]), 0.005f, 16.f);
        }
        const int pat = patS[0];
        // a Tapo pattern keeps its own length (as stored, at 120 bpm), follows the tempo, and is stretched
        // by its part's Time (A for slot 1, B for slot 2) and Lead; Sync rounds the length to the DLD's ratios
        {
            for (int sl = 0; sl < 2; ++sl)
            {
                double natBeats = GyTapo::naturalSeconds(patterns[(size_t)patS[sl]]) / 0.5;
                int p = sl == 0 ? 0 : 2;
                double b = m.band[p].sync ? snapRatio(natBeats * std::exp2((double)k.co[p] / 12.0) * leadMul) * std::exp2((double)(co[p] - k.co[p]) / 12.0)
                                          : natBeats * std::exp2((double)co[p] / 12.0) * leadMul;
                tapoSec[sl] = std::clamp(b * beatSec, 0.01, 20.0);
            }
            if (s1 == SlotTapo)
                partSec[0] = tapoSec[0];
            if (s2 == SlotTapo)
                partSec[2] = tapoSec[1];
        }
        for (int sl = 0; sl < 2; ++sl)
        {
            int ty = sl == 0 ? s1 : s2;
            if (ty < 0)
                continue;
            // the memory (Clean or 16-bit, and Degrade) is the same for both engines
            for (auto &d : dld[sl])
                d.quant16 = G.dld16, d.degradeBits = (int)std::lround(degrade);
            if (ty == SlotTapo)
            {
                auto &T = tapo[sl];
                const bool own2 = sl == 1 && twoTapos;
                const auto &T2 = G.tapo2;
                if (loaded[sl] != patS[sl])
                    T.load(patterns[patS[sl]], patS[sl], morphSS[sl]), loaded[sl] = patS[sl];
                T.modAmount = std::clamp(own2 ? T2.modAmount : G.tapoModAmount, 0.f, 1.f);
                T.modRate = std::clamp(own2 ? T2.modRate : G.tapoModRate, 0.f, 1.f);
                tapoVeloS[sl] = std::clamp(own2 ? T2.velocity : G.tapoVelocity, 0.f, 1.f);
                T.degradeBits = G.dld16 ? (int)std::lround(degrade) : 16;
            }
        }
        dispPattern = pat;
        // the shifts: intervals snapped to the key; Lead's adds to A and B
        {
            const auto &notes = tuneScaleNotes(std::clamp(s.tune.scale, 0, numTuneScales - 1));
            auto snapKey = [&](float st)
            {
                float best = 0, bd = 1e9f;
                for (int o = -3; o <= 3; ++o)
                    for (int nk : notes)
                    {
                        float c = (float)(o * 12 + nk), d = std::fabs(c - st);
                        if (d < bd - 1e-4f || (std::fabs(d - bd) < 1e-4f && c < best))
                            best = c, bd = d;
                    }
                return best;
            };
            const int tg[3] = {TgtShiftA, TgtShiftLead, TgtShiftB};
            float sh[3];
            for (int p = 0; p < 3; ++p)
                sh[p] = (float)G.shift[p] + tgtOff[tg[p]];
            for (int p = 0; p < 3; ++p)
            {
                int mdp = std::clamp(G.shiftMode[p], 0, 2);
                float st = p == Mid ? sh[p] : sh[p] + (G.shiftMode[Mid] != ShiftOff ? sh[Mid] : 0.f);
                st = std::clamp(snapKey(st), -24.f, 24.f);
                if (p != Mid && G.shiftMode[Mid] != ShiftOff && mdp == ShiftOff)
                    mdp = G.shiftMode[Mid];
                shiftModeNow[p] = mdp;
                float once = mdp == ShiftOnce ? st : 0.f, each = mdp == ShiftEach ? st : 0.f;
                // Settle: on repeats shorter than the shifter's window the step is spread over several repeats
                if (G.settle && each != 0.f)
                    each *= (float)std::clamp(partSec[p] / 0.15, 0.25, 1.0);
                for (int c = 0; c < 2; ++c)
                    shOnce[p][c].setSemitones(once), shLoop[p][c].setSemitones(each);
                shiftOnceSt[p] = once, shiftEachSt[p] = each;
            }
        }
        // the kill switch (a press), and auto kill: a runaway held by the ceiling for longer than Kill After
        {
            if (G.kill && !killWas)
                killReq = true;
            killWas = G.kill;
            // running away: a loop still sounding (over -40 dB) with nothing coming in, or with its feedback
            // at or past 100 % (or Hold). Drive can make a loop run on below 100 %, hence the first test.
            bool quiet = env < 0.003f, running = false;
            for (int p = 0; p < 3; ++p)
                running = running || (oscAmt < 0.5f && rw[p].env > 0.01f && (quiet || partFb[p] >= 0.99f || holdAmt > 0.5f));
            // the ceiling lets go between swells, so a short break only winds the timer back (twice as fast as it runs)
            const double dtk = (double)blockSize / sampleRate;
            killTimer = running ? killTimer + dtk : std::max(0.0, killTimer - 2.0 * dtk);
            if (G.autoKill && killTimer > std::clamp((double)G.killAfterSec, 1.0, 60.0))
                killReq = true, killTimer = 0.0, ++autoKills;
        }
        // the spectral split: by frequency or tonal, its two split points and the three bands' levels
        {
            spec->by = std::clamp(s.specBy, 0, 1);
            float lo = std::clamp(G.specLoSemi + tgtOff[TgtSplitLo], -45.f, 33.f), hi = std::clamp(G.specHiSemi + tgtOff[TgtSpecXHi], -27.f, 61.f);
            if (hi < lo + 1.f)
                hi = lo + 1.f;
            spec->f1 = 440.f * std::exp2(lo / 12.f), spec->f2 = 440.f * std::exp2(hi / 12.f);
            spec->balance = std::clamp(s.specBalance + specBalOff, -1.f, 1.f), spec->sharp = std::clamp(s.specSharp + specSharpOff, 0.f, 1.f);
            const int lt[3] = {TgtSpecLowLvl, TgtSpecMidLvl, TgtSpecHighLvl};
            for (int b = 0; b < 3; ++b)
            {
                float want = dbToLin(std::clamp(G.specLvlDb[b] + tgtOff[lt[b]], -60.f, 12.f));
                specG[b] += 0.3f * (want - specG[b]);
            }
            specShown[0] = lo, specShown[1] = hi;
        }
        // the filters (TC's models): one per path; Lead's Loop also runs a copy in every loop (instances 3..5)
        {
            const int ct[3] = {TgtFiltCutA, TgtFiltCut, TgtFiltCutB}, rt[3] = {TgtFiltResA, TgtFiltRes, TgtFiltResB}, mt[3] = {TgtFiltMorphA, TgtFiltMorph, TgtFiltMorphB};
            for (int pth = 0; pth < 3; ++pth)
            {
                float fc = std::clamp(G.filterCut[pth] + tgtOff[ct[pth]], -60.f, (float)maxCutoff);
                float fr = std::clamp(G.filterReso[pth] + tgtOff[rt[pth]], 0.f, 1.f);
                float fm = std::clamp(G.filterMorph[pth] + tgtOff[mt[pth]], 0.f, 1.f);
                dispGlobal[ct[pth] - TgtSplitLo] = fc, dispGlobal[rt[pth] - TgtSplitLo] = fr, dispGlobal[mt[pth] - TgtSplitLo] = fm;
                filtFc[pth] = fc, filtFr[pth] = fr, filtFm[pth] = fm;
            }
            for (int k = 0; k < 3; ++k)
                filtSendT[k] = std::clamp(G.filterSend[k], 0.f, 1.f), filtMixT[k] = std::clamp(G.filterMix[k], 0.f, 1.f);
            int leadPlace = std::clamp(G.filterPlace[1], 0, 2);
            for (int i = 0; i < numFilt; ++i)
            {
                int pth = i < 3 ? i : 1;
                int place = i < 3 ? std::clamp(G.filterPlace[i], 0, 2) : FiltLoop;
                bool on = G.filterModel[pth] > 0 && (i < 3 || leadPlace == FiltLoop);
                int wantBlock = (i == 1 && place == FiltIn) ? blockSize : fifoN;
                if (rebuildAll || G.filterModel[pth] != curModel[i] || G.filterVariant[pth] != curVariant[i] || on != curOn[i] || place != fPlace[i] || wantBlock != filtBlock[i])
                {
                    curModel[i] = G.filterModel[pth], curVariant[i] = G.filterVariant[pth], curOn[i] = on, fPlace[i] = place;
                    setupFilter(i);
                }
                else if (i == 1 && place == FiltIn && filtPrepared[i])
                    filters[i].concludeBlock(), filtPrepared[i] = false;
            }
            if (fPlace[1] == FiltIn)
                filterCoefficients(1);
        }
        // tilts, crusher, reverb
        tiltIn.set(std::clamp(G.tiltIn + tgtOff[TgtTiltIn], -12.f, 12.f), G.pivotIn, sampleRate);
        tiltOut.set(std::clamp(G.tiltOut + tgtOff[TgtTiltOut], -12.f, 12.f), G.pivotOut, sampleRate);
        {
            // v0.8: Degrade (16-bit memory, gone since v0.5) did nothing; its mod target and Mutation move now move Bits
            float bits = std::clamp(G.crushBits + tgtOff[TgtCrushBits] - 2.f * tgtOff[TgtDegrade], 2.f, 16.f);
            float rate = std::clamp(G.crushRateHz * std::exp2(tgtOff[TgtCrushRate] / 12.f), 200.f, (float)sampleRate);
            float cmix = std::clamp(G.crushMix + tgtOff[TgtNoise], 0.f, 1.f);
            dispGlobal[TgtNoise - TgtSplitLo] = cmix;
            crushPlace = std::clamp(G.crushPlace, 0, 3);
            for (int p = 0; p < 3; ++p)
            {
                auto &C = crLoop[p];
                C.sr = sampleRate, C.bits = bits, C.mix = crushPlace == CrushLoop ? cmix : 0.f, C.smooth = G.crushSmooth, C.tilt = G.memTilt, C.noise = crushPlace == CrushLoop ? G.memNoise * 0.35f : 0.f;
                C.rateHz = G.chipMode ? std::max(200.f, rate * (float)std::clamp(0.25 / std::max(0.01, partSec[p]), 0.12, 1.0)) : rate;
            }
            const bool atOut = crushPlace == CrushOut || crushPlace == CrushBoth, atIn = crushPlace == CrushIn || crushPlace == CrushBoth;
            crOut.sr = sampleRate, crOut.bits = bits, crOut.mix = atOut ? cmix : 0.f, crOut.smooth = G.crushSmooth, crOut.tilt = G.memTilt, crOut.noise = (atOut || crushPlace == CrushIn) ? G.memNoise : 0.f;
            crOut.rateHz = rate;
            for (auto &C : crIn) // In: what goes into the delays (your dry sound stays clean)
                C.sr = sampleRate, C.bits = bits, C.mix = atIn ? cmix : 0.f, C.smooth = G.crushSmooth, C.rateHz = rate, C.tilt = G.memTilt;
        }
        {
            GyReverbSettings R = G.rv;
            R.size = std::clamp(R.size + tgtOff[TgtRvSize], 0.f, 1.f);
            R.decay = std::clamp(R.decay + tgtOff[TgtRvDecay], 0.f, 1.f);
            R.modDepth = std::clamp(R.modDepth + tgtOff[TgtRvMod], 0.f, 1.f);
            R.tunedSemis = G.rvFollowKey ? (float)(((s.tune.root - 9) % 12 + 12) % 12) : 0.f;
            rvOn = G.reverbOn;
            if (rvOn && ++rvCtl >= 4) // the reverbs take new settings every 4 control blocks
                rvCtl = 0, reverb.set(R);
            float send = rvOn ? dbToLin(std::clamp(G.rvSendDb + tgtOff[TgtRvSend], -60.f, 0.f)) : 0.f;
            float rl = rvOn ? dbToLin(std::clamp(G.rvLevelDb + tgtOff[TgtRvLevel], -60.f, 6.f)) : 0.f;
            float into = rvOn ? dbToLin(std::clamp(G.rvIntoDelaysDb, -60.f, 0.f)) : 0.f;
            rvSend.to(send, fifoN), rvLevel.to(rl, fifoN), rvInto.to(into, fifoN);
        }
        // Call and Response / Mirror
        crDelay = (int)std::clamp(answerDivBeats[std::clamp(G.answerDiv, 0, numAnswerDivs - 1)] * beatSec * sampleRate, 64.0, (double)crL.size() - 2);
        crChance = std::clamp(G.answerChance, 0.f, 1.f);
        mirrorSec = std::clamp(partSec[0] * std::clamp((double)G.mirrorRatio, 0.25, 4.0), 0.02, 8.0);
        ancestorsControl(G, G.ancFrom == 2 ? partSec[2] : partSec[0], beats); // Ancestors: branches spaced by the part's own time
        reshapeControl(G);
        // ----- the River: three currents read off the river's course (TC's Ripple sources)
        {
            double pos;
            if (G.riverSync)
                pos = beats / (riverPassBars[std::clamp(G.riverPass, 0, numRiverPass - 1)] * beatsPerBar);
            else
                riverFree += std::clamp((double)G.riverHz, 0.001, 20.0) * (double)blockSize / sampleRate, pos = riverFree;
            if (G.tide)
            {
                // a tidal reach: every tideBars the flow turns and runs back up
                double turn = std::max(1, G.tideBars) * beatsPerBar;
                double seg = std::floor(beats / turn);
                if (std::fmod(seg, 2.0) != 0.0)
                    pos = -pos;
            }
            float mea = std::clamp(G.meander, 0.f, 1.f), spr = std::clamp(G.spread, 0.f, 1.f);
            riverPosShown = (float)(pos - std::floor(pos));
            for (int i = 0; i < numBands; ++i)
                ripOut[i] = riverValue(pos + spr * (double)i * 0.25, mea);
        }
        // ----- gains
        float inG = dbToLin(std::clamp(k.inDb, -60.f, 24.f)), outG = dbToLin(std::clamp(k.outDb, -60.f, 24.f));
        float mx = std::clamp(k.mix + tgtOff[TgtMix], 0.f, 1.f);
        dispGlobal[TgtMix - TgtSplitLo] = mx;
        dispGlobal[TgtSwayAmt - TgtSplitLo] = std::clamp(s.sway.amount + tgtOff[TgtSwayAmt], 0.f, 1.f);
        if (needsSnap)
        {
            inGain.snap(inG), outGain.snap(outG), mix.snap(mx);
            needsSnap = false;
        }
        else
            inGain.to(inG, blockSize), outGain.to(outG, fifoN), mix.to(mx, fifoN); // outGain and mix are ticked per FIFO block
    }

    static bool anySoloOn(const ModeSettings &m)
    {
        for (int i = 0; i < numBands; ++i)
            if (m.band[i].solo && i != Mid)
                return true;
        return false;
    }
    // the river's course, as a looping curve: -1..1 across the channel at a point 0..1 along it.
    // Stand-in course until the USGS trace is added: a long Piedmont-to-coast drift with bends that
    // tighten toward the coast; Meander blends from the drift alone to every bend.
    static float riverValue(double pos, float meander)
    {
        double x = pos - std::floor(pos);
        double tw = 2.0 * 3.14159265358979;
        double drift = 0.55 * std::sin(tw * x) + 0.15 * std::sin(tw * 2.0 * x + 0.9);
        double bends = 0.22 * std::sin(tw * 9.0 * x + 0.3) * (0.6 + 0.4 * x) + 0.12 * std::sin(tw * 23.0 * x + 1.7) * x +
                       0.06 * std::sin(tw * 51.0 * x + 0.2) * x * x;
        return (float)std::clamp(drift + meander * bends * 1.6, -1.0, 1.0);
    }

  public:
    // ----- Gullay: read-only info for the window
    int activeWiring() const { return wiring; }
    int patternNow() const { return dispPattern; }
    float partSeconds(int p) const { return (float)partSec[std::clamp(p, 0, 2)]; }
    float runawayGain(int p) const { return rw[std::clamp(p, 0, 2)].gain; }
    float tapeSpeed(int p) const { return tapeSpd[std::clamp(p, 0, 2)]; } // v0.8: Varispeed's tape speed now (1 = steady)
    float tapeSpd[3]{1.f, 1.f, 1.f};
    float runawayEnv(int p) const { return rw[std::clamp(p, 0, 2)].env; }
    float riverNow(int p) const { return ripOut[std::clamp(p, 0, 2)]; }
    bool infNow(int p) const { return infShown[std::clamp(p, 0, 2)]; }
    float riverPos() const { return riverPosShown; }
    float specSplitShown(int i) const { return specShown[std::clamp(i, 0, 1)]; } // semitones re 440 Hz, after modulation
    float specShown[2]{-9.79f, 33.24f}; // 0..1 along the river (the window's flowing water)
    // for the tests: the output stage's gains
    long recoverCount{0}; // times the safety net had to step in (for the tests)
    void debugGains(float *out) const { out[7] = (float)recoverCount, out[0] = fade.v, out[1] = (float)fadeState, out[2] = limGain, out[3] = inGain.v, out[4] = outGain.v, out[5] = mix.v, out[6] = (float)fadeCount; }
    float riverPosShown{0};
    // the tapped pattern (slot 25): main thread hands it over, the audio thread takes it at its next control block
    void setTapped(const TapoPattern &p)
    {
        if (tapNew.load(std::memory_order_acquire))
            return; // the last one has not been taken yet
        tapPending = p;
        tapNew.store(true, std::memory_order_release);
    }
    double bpmNow() const { return bpm; }
    double beatsNow() const { return beats; } // song position (or Free's own), quarter notes
    // the taps of slot sl's pattern as now playing: seconds and levels (for the window)
    int tapsNow(int sl, float *sec, float *vel, int max) const
    {
        sl = std::clamp(sl, 0, 1);
        int pi = std::clamp(loaded[sl] >= 0 ? loaded[sl] : 0, 0, tapoSlots - 1);
        const auto &p = patterns[(size_t)pi];
        double nat = GyTapo::naturalSeconds(p), scale = nat > 0 ? tapoSec[sl] / nat : 1.0;
        int n = std::min(p.size, max);
        for (int t = 0; t < n; ++t)
            sec[t] = (float)(p.time[t] / 48000.0 * scale), vel[t] = p.velo[t];
        return n;
    }
    TapoPattern tapPending{};
    std::atomic<bool> tapNew{false};
    void setPattern(int i, const TapoPattern &p) // main thread, before activate (or with the audio stopped)
    {
        if (i >= 0 && i < tapoSlots)
            patterns[(size_t)i] = p, loaded[0] = loaded[1] = -1;
    }
    const TapoPattern &pattern(int i) const { return patterns[(size_t)std::clamp(i, 0, tapoSlots - 1)]; }

  private:

    // ----- audio rate ------------------------------------------------------------------------
    // Everything runs through a 64-frame FIFO (the Tapographic engine works in blocks of 64), so
    // the plugin's latency is 64 samples plus the spectral split's frame when Spectral is on.
    static constexpr int fifoN = tapoBlock;
    float fIn[2][fifoN]{}, fDry[2][fifoN]{}, fOut[2][fifoN]{};
    float fSpec[2][2][fifoN]{}; // [band 0/1][ch]
    int fPos{0};
    float anaA[fifoN]{}, anaB[fifoN]{}, anaW[fifoN]{};

    GyDld dld[2][2];  // [slot][channel A/B]
    GyTapo tapo[2];
    double tapoSec[2]{0.5, 0.5};
    bool killReq{false}, killWas{false};
    int killPhase{0};
    double killTimer{0};
  public:
    long kills{0}, autoKills{0}; // for the window (it flashes) and the tests
    float runawaySeconds() const { return (float)killTimer; }
  private:
    float specG[3]{1, 1, 1};
    float tapoVeloS[2]{0.3f, 0.3f};
    GyDld mirror;     // one engine, part B = Mirror
    std::vector<float> crL, crR, fzL, fzR; // A's recent echoes; Freeze's held loop
    int fzLen{0}, fzPos{0}, fzNewLen{0}, stStart{-1}, stLen{2400}, stPos{0};
    float fzFade{1.f};
    bool fzWant{false}, stWant{false};
    float stG{1.f};
    CleanShifter octSh[2];
    int crW{0}, crDelay{24000};
    float crChance{1}, crGate{0}, crGateT{1};
    long crStep{-1};
    double mirrorSec{0.5};
    GyReverb reverb;
    std::array<TapoPattern, tapoSlots> patterns{};
    MemDecimator memDec[2];
    float memRate{44100.f}, memStep{1.f};
    int loaded[2]{-1, -1}, curSlot[2]{-2, -2}, curMode2{-1}, wiring{0}, dispPattern{0}, crushPlace{CrushLoop};
    double partBeats[3]{1, 1, 1}, partSec[3]{0.5, 0.5, 0.5}, riverFree{0};
    float partFb[3]{0.45f, 0.45f, 0.45f}, holdAmt{0}, balA{1}, balB{1};
    float oscAmt{0};
    bool oscWas{false};
    // the Chain: what engine 2 sent back last block (Cross), and the Wire's tone filter
    float crossBuf[2][fifoN]{}, wireLo[2]{}, crossAmt{0}, wireTone{0}, wireDrive{0};
    bool variNow{false};
    double variA{0.001};
    float shiftOnceSt[3]{}, shiftEachSt[3]{};
    int shiftModeNow[3]{};
    bool rvOn{false}, freeRun{false}, freePlay{true}, infShown[3]{};
    int rvCtl{0};
    float freeBpm{120};
    Ramp partL[3], partR[3], rvSend, rvLevel, rvInto;
    PartTone tone[3];
    Runaway rw[3];
    Crusher crLoop[3], crOut, crIn[2];
    MemDecimator memIn[2], memLoop[3];
    GyTape tape[7]; // 0-2: in each part's loop (every repeat); v0.8: 3-6, where each engine records (the first echo too)
    bool tapeOn{false};
    Tilt tiltIn, tiltOut;
    CleanShifter shOnce[3][2], shLoop[3][2];
    float rvRet[2][fifoN]{}; // the reverb's last block, fed back "into the delays"

    void prepareCore()
    {
        for (auto &ch : dld)
            for (auto &d : ch)
                d.setSampleRate(sampleRate);
        mirror.setSampleRate(sampleRate);
        for (auto &t : tapo)
            t.setSampleRate(sampleRate);
        reverb.setSampleRate(sampleRate);
        for (auto &p : shOnce)
            for (auto &c : p)
                c.prepare(sampleRate);
        for (auto &p : shLoop)
            for (auto &c : p)
                c.prepare(sampleRate);
        for (auto &r : rw)
            r.setRate(sampleRate);
        crL.assign((size_t)(4.2 * sampleRate * 2.0) + 16, 0.f); // up to 4 beats at 30 bpm... capped to 8.4 s
        crR.assign(crL.size(), 0.f);
        fzL.assign(crL.size(), 0.f), fzR.assign(crL.size(), 0.f);
        ancBuf.assign((size_t)1 << 20, 0.f), ancMask = (1 << 20) - 1, ancW = 0; // Ancestors: 21.8 s at 48 kHz
        octSh[0].prepare(sampleRate), octSh[1].prepare(sampleRate);
        octSh[0].setSemitones(12.f), octSh[1].setSemitones(12.f);
        loaded[0] = loaded[1] = -1;
        curSlot[0] = curSlot[1] = -2;
    }
    void allocateCore() // once, off the audio thread
    {
        for (auto &ch : dld)
            for (auto &d : ch)
                d.allocate(1u << 20); // 21.8 s of stereo at 48 kHz per channel
        mirror.allocate(1u << 19);
        for (auto &t : tapo)
            t.allocate(1u << 21); // the module's 8 MiB: 43.7 s at 48 kHz
        for (int i = 0; i < tapoFactorySlots; ++i)
            patterns[(size_t)i] = tapoFactoryPattern(i);
        patterns[(size_t)(tapoSlots - 1)] = tapoFactoryPattern(0); // until something is tapped
    }
    void clearCore()
    {
        for (auto &ch : dld)
            for (auto &d : ch)
                d.clear();
        mirror.clear();
        for (auto &t : tapo)
            t.clear();
        reverb.clear();
        std::fill(crL.begin(), crL.end(), 0.f), std::fill(crR.begin(), crR.end(), 0.f);
        for (auto &p : shOnce)
            for (auto &c : p)
                c.reset();
        for (auto &p : shLoop)
            for (auto &c : p)
                c.reset();
        for (auto &r : rw)
            r.reset();
        for (auto &c : crLoop)
            c.reset();
        crOut.reset();
        for (auto &C : crIn)
            C.reset();
        tiltIn.reset(), tiltOut.reset();
        std::memset(fIn, 0, sizeof(fIn)), std::memset(fDry, 0, sizeof(fDry)), std::memset(fOut, 0, sizeof(fOut));
        std::memset(fSpec, 0, sizeof(fSpec)), std::memset(rvRet, 0, sizeof(rvRet));
        fPos = 0;
    }

    // Hymn's ringing, shaped: Gate opens it while the sound is over Threshold (then Hold, then it fades over Decay);
    // Decay lets each new hit open it fully and fade over Decay whatever the sound does next.
    int hymnShapeNow{0};
    float hymnThr{0.03f}, hymnDecA{0.001f}, hymnEnv{0}, hymnDet{0};
    int hymnHoldN{0}, hymnHoldLeft{0};
    bool hymnArmed{true};
    // Envelope (shape 3): Attack, Decay to Sustain while the sound stays over the trigger, then Release
    int hymnStage{0}; // 0 idle, 1 attack, 2 decay/sustain, 3 release
    float hymnAttInc{0.004f}, hymnSus{0.6f}, hymnDecA2{0.001f}, hymnRelA{0.001f};
    inline void hymnTick(float &l, float &r)
    {
        if (hymnShapeNow == 0)
        {
            tune.tick(l, r, tuneAmt, tuneLayer);
            return;
        }
        float det = std::max(std::fabs(l), std::fabs(r));
        hymnDet = std::max(det, hymnDet * 0.995f); // lets go in a few ms
        float rl = l, rr = r;
        tune.tick(rl, rr, 1.f, false); // the ringing alone
        bool over = hymnDet > hymnThr;
        if (hymnShapeNow == 3)
        {
            if (over && hymnArmed)
                hymnArmed = false, hymnStage = 1;
            else if (hymnDet < hymnThr * 0.5f)
            {
                hymnArmed = true;
                if (hymnStage == 1 || hymnStage == 2)
                    hymnStage = 3;
            }
            if (hymnStage == 1)
            {
                hymnEnv += hymnAttInc;
                if (hymnEnv >= 1.f)
                    hymnEnv = 1.f, hymnStage = 2;
            }
            else if (hymnStage == 2)
                hymnEnv += hymnDecA2 * (hymnSus - hymnEnv);
            else if (hymnStage == 3)
            {
                hymnEnv -= hymnRelA * hymnEnv;
                if (hymnEnv < 1.0e-5f)
                    hymnEnv = 0.f, hymnStage = 0;
            }
        }
        else if (hymnShapeNow == 1)
        {
            if (over)
                hymnHoldLeft = hymnHoldN;
            else if (hymnHoldLeft > 0)
                --hymnHoldLeft;
            if (over || hymnHoldLeft > 0)
                hymnEnv += 0.02f * (1.f - hymnEnv);
            else
                hymnEnv -= hymnDecA * hymnEnv;
        }
        else
        {
            if (over && hymnArmed)
                hymnArmed = false, hymnHoldLeft = hymnHoldN + 48; // a 1 ms rise, then Hold
            else if (hymnDet < hymnThr * 0.5f)
                hymnArmed = true;
            if (hymnHoldLeft > 0)
                --hymnHoldLeft, hymnEnv += 0.02f * (1.f - hymnEnv);
            else
                hymnEnv -= hymnDecA * hymnEnv;
        }
        float w = tuneAmt * hymnEnv;
        if (tuneLayer)
            l += rl * w, r += rr * w;
        else
            l = l * (1.f - tuneAmt) + rl * w, r = r * (1.f - tuneAmt) + rr * w;
    }
    // the global filter's coefficients for instance i, from this control block's cutoff, reso and morph
    float filtFc[3]{24, 24, 24}, filtFr[3]{0.2f, 0.2f, 0.2f}, filtFm[3]{0, 0, 0};
    int filtBlock[numFilt]{fifoN, fifoN, fifoN, fifoN, fifoN, fifoN};
    bool filtPrepared[numFilt]{};
    void filterCoefficients(int i)
    {
        const int pth = i < 3 ? i : 1;
        const float fFc = filtFc[pth], fFr = filtFr[pth], fFm = filtFm[pth];
        if (zOn[i])
        {
            zCoefficients(i, fFc, fFr, fFm);
            filters[i].makeCoefficients(0, fFc, fFr, 0.f);
            filters[i].copyCoefficientsFromVoiceToVoice(0, 1);
            filters[i].prepareBlock();
            filtPrepared[i] = true;
            return;
        }
        if (!fActive[i])
            return;
        const auto &cat = filterCatalogue();
        float mv = fFm;
        const auto &mi = cat[(size_t)curModel[i]];
        const auto &cfg = mi.configs[(size_t)std::clamp(curVariant[i], 0, (int)mi.configs.size() - 1)];
        if (sfpp::Filter::coefficientsExtraIsBipolar(mi.model, cfg, 0))
            mv = mv * 2 - 1;
        filters[i].makeCoefficients(0, fFc, fFr, mv);
        filters[i].copyCoefficientsFromVoiceToVoice(0, 1);
        filters[i].prepareBlock();
        filtPrepared[i] = true;
    }
    // Out and Loop: one coefficient step per FIFO block (the filters then run exactly fifoN samples)
    void filterBlockStart()
    {
        for (int i = 0; i < numFilt; ++i)
        {
            if (!fActive[i] || (i == 1 && fPlace[i] == FiltIn))
                continue;
            if (filtPrepared[i])
                filters[i].concludeBlock(), filtPrepared[i] = false;
            filterCoefficients(i);
        }
    }
    // the filter on one stereo frame, instance i
    inline void filterTick(int b, float &l, float &r)
    {
        if (!fActive[b])
            return;
        // v0.8: Send: how much goes into the filter (the rest passes by it, unfiltered); Mix: how much of the result you
        // hear against what came in. Both 100 %: the filter as before.
        const int pth = b < 3 ? b : 1;
        float &sd = filtSendSm[b], &mx = filtMixSm[b];
        sd += 0.002f * (filtSendT[pth] - sd), mx += 0.002f * (filtMixT[pth] - mx);
        const float xl = l, xr = r;
        float ol = l * sd, orr = r * sd;
        if (zOn[b])
            zTick(b, ol, orr);
        else
        {
            float fl, fr;
            filters[b].processStereoSample(ol, orr, fl, fr);
            if (!std::isfinite(fl + fr)) // a model that blew up: start it again rather than pass it on
                filters[b].reset(), fl = fr = 0.f;
            ol = std::clamp(fl, -8.f, 8.f), orr = std::clamp(fr, -8.f, 8.f);
        }
        ol += (1.f - sd) * xl, orr += (1.f - sd) * xr;
        l = xl + mx * (ol - xl);
        r = xr + mx * (orr - xr);
    }
    // what comes back round a part's loop: Runaway, the crusher, the filter (Loop), Hymn's Each repeat
#ifdef GY_NAN_TRACE
#define GYNAN(stage, a, b) do { if (gyNanStage == 0 && (!std::isfinite(a) || !std::isfinite(b))) gyNanStage = stage; } while (0)
  public: int gyNanStage{0}; private:
#else
#define GYNAN(stage, a, b) do {} while (0)
#endif
    inline void loopFx(int p, float &l, float &r)
    {
        GYNAN(1, l, r);
        rw[p].tick(l, r);
        GYNAN(2, l, r);
        crLoop[p].tick(l, r);
        if (tapeOn)
            tape[p].tick(l, r);
        if (memRate < 44000.f && crushPlace == CrushLoop)
            memLoop[p].tick(l, r, memStep);
        GYNAN(3, l, r);
        if (p == 1)
        {
            if (fPlace[0] == FiltLoop) filterTick(0, l, r);
            if (fPlace[2] == FiltLoop) filterTick(2, l, r);
        }
        if (fPlace[p] == FiltLoop)
            filterTick(p, l, r);
        if (p != 1 && fPlace[1] == FiltLoop)
            filterTick(3 + p, l, r);
        GYNAN(4, l, r);
        if (shiftModeNow[p] == ShiftEach)
            l = shLoop[p][0].process(l), r = shLoop[p][1].process(r);
        GYNAN(5, l, r);
    }

    // one host frame in, one out (64 frames late)
    inline void coreTick(float inL, float inR, float &oL, float &oR)
    {
        float g = inGain.tick();
        float xl = inL * g, xr = inR * g;
        GYNAN(10, xl, xr);
        float dl = inL, dr = inR;
        if (tuneOn && tuneBefore) // Hymn's Tune into the delays: the engines get the ringing
        {
            hymnTick(xl, xr);
            if (tuneOnly) // and the dry sound is the ringing too
                dl = xl, dr = xr;
        }
        tiltIn.tick(xl, xr);
        GYNAN(11, xl, xr);
        if (fPlace[1] == FiltIn)
            filterTick(1, xl, xr);
        GYNAN(12, xl, xr);
        if (wiring == 2)
        {
            float bl[3], br[3];
            spec->tick(xl, xr, dl, dr, bl, br, dl, dr);
            // Low goes to slot 1 (A), High to slot 2 (B), Mid to both (Lead), each at its band level
            float m0 = specG[1] * 0.70710678f;
            fSpec[0][0][fPos] = bl[0] * specG[0] + bl[1] * m0, fSpec[0][1][fPos] = br[0] * specG[0] + br[1] * m0;
            fSpec[1][0][fPos] = bl[2] * specG[2] + bl[1] * m0, fSpec[1][1][fPos] = br[2] * specG[2] + br[1] * m0;
        }
        fIn[0][fPos] = xl, fIn[1][fPos] = xr;
        fDry[0][fPos] = dl, fDry[1][fPos] = dr;
        oL = fOut[0][fPos], oR = fOut[1][fPos];
        anaPush(anaA[fPos], 0.5f * (inL + inR), anaB[fPos], anaW[fPos]);
        // the parts' levels, for the Follower's Part sources and Mutation's "Lead gets louder"
        bandPeak[0] = std::max(bandPeak[0], std::fabs(anaA[fPos]));
        bandPeak[2] = std::max(bandPeak[2], std::fabs(anaB[fPos]));
        bandPeak[1] = std::max(bandPeak[1], std::fabs(anaW[fPos]));
        if (++fPos == fifoN)
        {
            fPos = 0;
            processBlock();
        }
    }

    // the engines, on one FIFO block
    void processBlock()
    {
        const int N = fifoN;
        filterBlockStart();
        // kill: this block fades the echoes out, the next one starts with every loop empty
        float killFrom = 1.f, killTo = 1.f;
        if (killPhase == 2)
        {
            for (auto &ch : dld)
                for (auto &d : ch)
                    d.clear();
            mirror.clear();
            for (auto &t : tapo)
                t.clear();
            reverb.clear();
            std::fill(crL.begin(), crL.end(), 0.f), std::fill(crR.begin(), crR.end(), 0.f);
            for (auto &r : rw)
                r.reset();
            for (auto &p : shLoop)
                for (auto &c : p)
                    c.reset();
            std::memset(rvRet, 0, sizeof(rvRet));
            killPhase = 0, killFrom = 0.f, ++kills;
        }
        else if (killReq)
            killReq = false, killPhase = 2, killTo = 0.f;
        float pa[2][N], pb[2][N], wet[2][N];
        float in1[2][N], in2[2][N];
        for (int i = 0; i < N; ++i)
        {
            float into = rvInto.tick();
            float l = fIn[0][i] + rvRet[0][i] * into, r = fIn[1][i] + rvRet[1][i] * into;
            in1[0][i] = wiring == 2 ? fSpec[0][0][i] : l, in1[1][i] = wiring == 2 ? fSpec[0][1][i] : r;
            in2[0][i] = wiring == 2 ? fSpec[1][0][i] : l, in2[1][i] = wiring == 2 ? fSpec[1][1][i] : r;
        }
        // Memory's In: the crusher and the rate on what goes into each engine
        if (crushPlace == CrushIn || crushPlace == CrushBoth)
            for (int i = 0; i < N; ++i)
            {
                crIn[0].tick(in1[0][i], in1[1][i]), crIn[1].tick(in2[0][i], in2[1][i]);
                if (memRate < 44000.f)
                    memIn[0].tick(in1[0][i], in1[1][i], memStep), memIn[1].tick(in2[0][i], in2[1][i], memStep);
            }
        if (fPlace[0] == FiltIn || fPlace[2] == FiltIn)
            for (int i = 0; i < N; ++i)
            {
                if (fPlace[0] == FiltIn) filterTick(0, in1[0][i], in1[1][i]);
                if (fPlace[2] == FiltIn) filterTick(2, in2[0][i], in2[1][i]);
            }
        double sr = sampleRate;
        auto runDld = [&](int sl, int ch, int part, const float (*src)[N], float (*dst)[N])
        {
            auto &D = dld[sl][ch];
            // v0.8: the tape speed this block (1 = steady): how far the sliding head moved against the samples played
            struct SpeedNote { float *out; const GyDld *d; double t0; ~SpeedNote() { *out = d->variActive() ? (float)(1.0 - (d->variPos() - t0) / (double)N) : 1.f; } } spNote{&tapeSpd[std::clamp(part, 0, 2)], &D, D.variPos()};
            D.feedback = partFb[part], D.delayFeed = 1.f;
            D.vari = variNow, D.glideA = variA;
            D.setTime(partSec[part] * sr);
            D.setInf(curBandInf[part]), D.setReverse(curBandRev[part]);
            if (tapeOn) // v0.8: Tape records the echoes onto tape (so the first echo is taped too); each repeat goes through again in the loop
            {
                auto &tp = tape[3 + 2 * std::clamp(sl, 0, 1) + std::clamp(ch, 0, 1)];
                for (int i = 0; i < N; ++i)
                {
                    float l = src[0][i], r = src[1][i];
                    tp.tick(l, r);
                    D.tick(l, r, dst[0][i], dst[1][i], [&](float &a, float &b) { loopFx(part, a, b); });
                }
                return;
            }
            for (int i = 0; i < N; ++i)
                D.tick(src[0][i], src[1][i], dst[0][i], dst[1][i], [&](float &l, float &r) { loopFx(part, l, r); });
        };
        auto runTapo = [&](int sl, int fbPart, const float (*src)[N], float (*dA)[N], float (*dB)[N], double lengthSec, float split)
        {
            auto &T = tapo[sl];
            T.feedback = partFb[fbPart], T.gain = 1.f, T.splitFrac = split, T.repeatOn = curBandInf[fbPart];
            T.vari = variNow, T.variA = (float)variA;
            T.velocityParam = tapoVeloS[std::clamp(sl, 0, 1)];
            T.setLength(lengthSec * sr);
            float mono[N];
            for (int i = 0; i < N; ++i)
                mono[i] = 0.5f * (src[0][i] + src[1][i]);
            if (tapeOn) // v0.8: Juba records onto tape too
            {
                auto &tp = tape[3 + 2 * std::clamp(sl, 0, 1)];
                for (int i = 0; i < N; ++i)
                {
                    float l = mono[i], r = mono[i];
                    tp.tick(l, r);
                    mono[i] = l;
                }
            }
            GyTapo::F a[N], b[N];
            T.process(mono, a, b, [&](float &l, float &r) { loopFx(fbPart, l, r); });
            for (int i = 0; i < N; ++i)
            {
                if (dB)
                    dA[0][i] = a[i].l, dA[1][i] = a[i].r, dB[0][i] = b[i].l, dB[1][i] = b[i].r;
                else
                    dA[0][i] = a[i].l + b[i].l, dA[1][i] = a[i].r + b[i].r;
            }
        };
        int s1 = curSlot[0], s2 = curSlot[1];
        if (s2 < 0)
        {
            // one engine: A and B are its two halves (or B answers / mirrors A)
            if (s1 == SlotDld)
            {
                runDld(0, 0, 0, in1, pa);
                if (curMode2 == S2Engine)
                    runDld(0, 1, 2, in2, pb); // in Spectral, channel B hears the highs (and the mids)
            }
            else
            {
                float split = std::clamp(0.5f * std::exp2(dispBand[2][PCut] / 24.f), 0.05f, 0.95f);
                if (wiring == 2) // one Tapo hears every band
                    for (int i = 0; i < N; ++i)
                        in1[0][i] += in2[0][i], in1[1][i] += in2[1][i];
                runTapo(0, 1, in1, pa, curMode2 == S2Engine ? pb : nullptr, tapoSec[0], curMode2 == S2Engine ? split : 1.f);
                if (curMode2 != S2Engine) // all the taps are A
                    ;
            }
            if (curMode2 == S2CallResponse)
                callResponse(pa, pb);
            else if (curMode2 == S2Stutter || curMode2 == S2Freeze || curMode2 == S2Octave)
                shadow(pa, pb);
            else if (curMode2 == S2Mirror)
            {
                mirror.feedback = 0.f, mirror.delayFeed = 1.f;
                mirror.setReverse(true);
                mirror.setTime(mirrorSec * sr);
                for (int i = 0; i < N; ++i)
                    mirror.tick(pa[0][i], pa[1][i], pb[0][i], pb[1][i], [](float &, float &) {});
            }
        }
        else
        {
            // two engines: slot 1 is A, slot 2 is B; Series feeds A's output into B
            const bool chain = wiring == 1;
            if (chain && crossAmt > 0.f) // Cross: engine 2's echoes from the last block come back into engine 1
                for (int i = 0; i < N; ++i)
                    in1[0][i] += crossAmt * std::tanh(crossBuf[0][i]), in1[1][i] += crossAmt * std::tanh(crossBuf[1][i]);
            if (s1 == SlotDld)
                runDld(0, 0, 0, in1, pa);
            else
                runTapo(0, 0, in1, pa, nullptr, tapoSec[0], 1.f);
            float wire[2][N];
            if (chain)
            {
                // the Wire: a tilt around 900 Hz (left darker, right brighter) and a drive, on the way into engine 2
                const float aSplit = (float)std::clamp(2.0 * 3.14159265 * 900.0 / sr, 0.001, 1.0);
                const float gHi = std::exp2(wireTone * 1.5f), gLo = 1.f / gHi;
                const float dg = 1.f + 7.f * wireDrive, dn = 1.f / std::pow(dg, 0.6f); // pushes harder, comes out a little louder
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < N; ++i)
                    {
                        float x = pa[c][i];
                        wireLo[c] += aSplit * (x - wireLo[c]);
                        x = wireLo[c] * gLo + (x - wireLo[c]) * gHi;
                        if (wireDrive > 0.f)
                            x = std::tanh(x * dg) * dn;
                        wire[c][i] = x;
                    }
            }
            const float(*src2)[N] = chain ? wire : in2;
            if (s2 == SlotDld)
                runDld(1, 0, 2, src2, pb);
            else
                runTapo(1, 2, src2, pb, nullptr, tapoSec[1], 1.f);
            if (chain)
                for (int i = 0; i < N; ++i)
                    crossBuf[0][i] = pb[0][i], crossBuf[1][i] = pb[1][i];
        }
        // the memory's sample rate (both engines): the echoes held at the lower rate, then smoothed like a
        // sampler's output filter. Holding on a fixed grid gives the same as holding in memory, every pass alike.
        if (memRate < 44000.f && (crushPlace == CrushOut || crushPlace == CrushBoth))
            for (int i = 0; i < N; ++i)
            {
                memDec[0].tick(pa[0][i], pa[1][i], memStep);
                memDec[1].tick(pb[0][i], pb[1][i], memStep);
            }
        // the parts: tone, Hymn's Once shift, level and pan; Lead is the master on their sum
        for (int i = 0; i < N; ++i)
        {
            float al = pa[0][i], ar = pa[1][i], bl = pb[0][i], br = pb[1][i];
            GYNAN(20, al, ar); GYNAN(21, bl, br);
            tone[0].tick(al, ar), tone[2].tick(bl, br);
            if (fPlace[0] == FiltOut) filterTick(0, al, ar);
            if (fPlace[2] == FiltOut) filterTick(2, bl, br);
            if (shiftModeNow[0] == ShiftOnce)
                al = shOnce[0][0].process(al), ar = shOnce[0][1].process(ar);
            if (shiftModeNow[2] == ShiftOnce)
                bl = shOnce[2][0].process(bl), br = shOnce[2][1].process(br);
            float gal = partL[0].tick() * balA, gar = partR[0].tick() * balA, gbl = partL[2].tick() * balB, gbr = partR[2].tick() * balB;
            al *= gal, ar *= gar, bl *= gbl, br *= gbr;
            anaA[i] = 0.5f * (al + ar), anaB[i] = 0.5f * (bl + br);
            // Series: B carries A's echoes already, so A is only heard through B
            float l = (curSlot[1] >= 0 && wiring == 1) ? bl : al + bl, r = (curSlot[1] >= 0 && wiring == 1) ? br : ar + br;
            tone[1].tick(l, r);
            l *= partL[1].tick(), r *= partR[1].tick();
            if (reActive)
                reshapeTick(l, r);
            if (ancActive)
            {
                const float in = ancFromNow == 1 ? 0.5f * (al + ar) : ancFromNow == 2 ? 0.5f * (bl + br) : 0.5f * (l + r);
                float tl = 0.f, tr = 0.f;
                ancestorsTick(in, tl, tr);
                l += tl, r += tr;
            }
            wet[0][i] = l, wet[1][i] = r;
        }
        // around the echoes: crusher (Out), filter (Out), Hymn's Tune, the reverb, Tilt Out
        float rin[2][N], rout[2][N];
        for (int i = 0; i < N; ++i)
        {
            float l = wet[0][i], r = wet[1][i];
            GYNAN(30, l, r);
            crOut.tick(l, r);
            GYNAN(31, l, r);
            if (fPlace[1] == FiltOut)
                filterTick(1, l, r);
            GYNAN(32, l, r);
            if (tuneOn && !tuneBefore)
                hymnTick(l, r);
            GYNAN(33, l, r);
            float sd = rvSend.tick();
            rin[0][i] = l * sd, rin[1][i] = r * sd;
            wet[0][i] = l, wet[1][i] = r;
        }
        if (rvOn)
            reverb.process(rin[0], rin[1], rout[0], rout[1], N);
        else
            std::memset(rout, 0, sizeof(rout));
        for (int i = 0; i < N; ++i)
        {
            GYNAN(40, rout[0][i], rout[1][i]);
            float rlv = rvLevel.tick();
            float l = wet[0][i] + rout[0][i] * rlv, r = wet[1][i] + rout[1][i] * rlv;
            rvRet[0][i] = rout[0][i], rvRet[1][i] = rout[1][i];
            GYNAN(41, l, r);
            tiltOut.tick(l, r);
            GYNAN(42, l, r);
            anaW[i] = 0.5f * (l + r);
            float kg = killFrom + (killTo - killFrom) * (float)(i + 1) / (float)N;
            l *= kg, r *= kg;
            float og = outGain.tick(), mx = mix.tick(), f = fade.tick();
            fOut[0][i] = (mx * l * og + (1.f - mx) * fDry[0][i]) * f;
            fOut[1][i] = (mx * r * og + (1.f - mx) * fDry[1][i]) * f;
        }
        for (int p = 0; p < 3; ++p)
            infShown[p] = p == 1 ? false : curBandInf[p];
    }

    // Call and Response: B repeats A's echoes after the answer time, on the steps Chance lets through
    void callResponse(const float (*pa)[fifoN], float (*pb)[fifoN])
    {
        const int L = (int)crL.size();
        for (int i = 0; i < fifoN; ++i)
        {
            crL[(size_t)crW] = pa[0][i], crR[(size_t)crW] = pa[1][i];
            int rd = crW - crDelay;
            if (rd < 0)
                rd += L;
            long stepNow = (long)std::floor(beats / std::max(1e-3, (double)crDelay / sampleRate * bpm / 60.0));
            if (stepNow != crStep)
                crStep = stepNow, crGateT = rand01(4242, (uint64_t)(int64_t)stepNow, 9) < crChance ? 1.f : 0.f;
            crGate += 0.003f * (crGateT - crGate);
            pb[0][i] = crL[(size_t)rd] * crGate, pb[1][i] = crR[(size_t)rd] * crGate;
            if (++crW >= L)
                crW = 0;
        }
    }
    // Shadow (one engine): B follows A with a twist, all read from A's recent echoes (crL/crR)
    //   Stutter: on each Answer step (if Chance lets it) B grabs the last Length of A and re-plays it over and over
    //   Freeze:  on each step (if Chance lets it, and A is sounding) B copies the last Length of A and holds it as a pad
    //   Octave:  B plays A's echoes an octave up, after the Answer time (a shimmer)
    void shadow(const float (*pa)[fifoN], float (*pb)[fifoN])
    {
        const int L = (int)crL.size();
        const int len = (int)std::clamp(mirrorSec * sampleRate, 256.0, (double)L / 2 - 4);
        const int fade = std::max(32, (int)(0.004 * sampleRate));
        for (int i = 0; i < fifoN; ++i)
        {
            crL[(size_t)crW] = pa[0][i], crR[(size_t)crW] = pa[1][i];
            long stepNow = (long)std::floor(beats / std::max(1e-3, (double)crDelay / sampleRate * bpm / 60.0));
            bool newStep = stepNow != crStep;
            if (newStep)
                crStep = stepNow, crGateT = rand01(4242, (uint64_t)(int64_t)stepNow, 9) < crChance ? 1.f : 0.f;
            float ol = 0.f, orr = 0.f;
            if (curMode2 == S2Stutter)
            {
                if (newStep && crGateT > 0.f)
                    stWant = true; // v0.8: the slice playing fades out before the new one starts (it used to jump)
                if (stWant && (stStart < 0 || stG <= 0.f))
                {
                    stWant = false, stStart = crW - len, stLen = len, stPos = 0;
                    if (stStart < 0)
                        stStart += L;
                }
                stG = stWant ? std::max(0.f, stG - 1.f / (float)fade) : std::min(1.f, stG + 1.f / (float)fade);
                if (stStart >= 0) // keeps playing on a step Chance skips: crGate (below) fades it out instead of a cut
                {
                    int k = stStart + stPos;
                    if (k >= L)
                        k -= L;
                    float w = std::min({1.f, (float)stPos / (float)fade, (float)(stLen - stPos) / (float)fade}) * stG;
                    ol = crL[(size_t)k] * w, orr = crR[(size_t)k] * w;
                    if (++stPos >= stLen)
                        stPos = 0;
                }
                crGate += 0.003f * ((stStart >= 0 ? crGateT : 0.f) - crGate);
                ol *= crGate, orr *= crGate;
            }
            else if (curMode2 == S2Freeze)
            {
                if (newStep && crGateT > 0.f && (fzLen == 0 || std::fabs(pa[0][i]) + std::fabs(pa[1][i]) > 1.0e-3f))
                    fzWant = true; // v0.8: a new pad is due; the old one fades out first (it used to be cut off at once)
                if (fzWant && (fzLen == 0 || fzFade <= 0.f))
                {
                    fzWant = false;
                    // copy the last Length of A into the held loop (once a step at most; no allocation)
                    int st = crW - len;
                    if (st < 0)
                        st += L;
                    for (int k = 0; k < len; ++k)
                    {
                        int j = st + k;
                        if (j >= L)
                            j -= L;
                        fzL[(size_t)k] = crL[(size_t)j], fzR[(size_t)k] = crR[(size_t)j];
                    }
                    fzLen = len, fzPos = 0, fzFade = 0.f;
                }
                if (fzLen > 0)
                {
                    // equal-power crossfade round the loop's seam, so the pad never clicks
                    const int x = std::min(fade * 8, fzLen / 4);
                    float a = 1.f, b = 0.f;
                    int k2 = -1;
                    if (fzPos >= fzLen - x)
                    {
                        float t = (float)(fzPos - (fzLen - x)) / (float)x;
                        a = std::cos(t * 1.5707963f), b = std::sin(t * 1.5707963f), k2 = fzPos - (fzLen - x);
                    }
                    ol = fzL[(size_t)fzPos] * a + (k2 >= 0 ? fzL[(size_t)k2] * b : 0.f);
                    orr = fzR[(size_t)fzPos] * a + (k2 >= 0 ? fzR[(size_t)k2] * b : 0.f);
                    if (++fzPos >= fzLen)
                        fzPos = x;
                    fzFade = fzWant ? std::max(0.f, fzFade - 1.f / (float)fade) : std::min(1.f, fzFade + 1.f / (float)fade);
                    ol *= fzFade, orr *= fzFade;
                }
            }
            else // Octave
            {
                int rd = crW - crDelay;
                if (rd < 0)
                    rd += L;
                ol = octSh[0].process(crL[(size_t)rd]), orr = octSh[1].process(crR[(size_t)rd]);
            }
            pb[0][i] = ol, pb[1][i] = orr;
            if (++crW >= L)
                crW = 0;
        }
    }
    // ----- Reshape (v0.8): the echoes in three bands, each levelled, panned and tilted
    Crossover reX;
    float reG[3][2]{}, reGT[3][2]{}, reLp[3][2]{}, reA[3]{}, reTl[3]{1, 1, 1}, reTh[3]{1, 1, 1}, reFade{0.f};
    bool reActive{false}, reWant{false};
    double reF1{-1}, reF2{-1};
    void reshapeControl(const GySettings &G)
    {
        reWant = G.reOn;
        if (!reWant && reFade <= 0.f)
        {
            if (reActive)
                reX.reset();
            reActive = false;
            return;
        }
        reActive = true;
        const double f1 = std::clamp((double)G.reLoHz, 40.0, 8000.0), f2 = std::clamp((double)G.reHiHz, f1 * 1.25, 18000.0);
        if (f1 != reF1 || f2 != reF2)
            reX.set(f1, f2, sampleRate), reF1 = f1, reF2 = f2;
        const double mids[3] = {std::sqrt(40.0 * f1), std::sqrt(f1 * f2), std::sqrt(f2 * 16000.0)};
        for (int b = 0; b < 3; ++b)
        {
            const float g = dbToLin(std::clamp(G.reLevelDb[b], -60.f, 12.f)), pan = std::clamp(G.rePan[b], -1.f, 1.f);
            reGT[b][0] = g * std::cos((pan + 1.f) * 0.7853982f) * 1.4142135f, reGT[b][1] = g * std::sin((pan + 1.f) * 0.7853982f) * 1.4142135f;
            reA[b] = 1.f - std::exp(-6.2831853f * (float)mids[b] / (float)sampleRate);
            const float t = std::clamp(G.reTone[b], -12.f, 12.f);
            reTh[b] = dbToLin(0.5f * t), reTl[b] = 1.f / reTh[b]; // a tilt of t dB round the band's middle (highs up t/2, lows down t/2)
        }
    }
    inline void reshapeTick(float &l, float &r)
    {
        reFade = std::clamp(reFade + (reWant ? 1.f : -1.f) / (0.02f * (float)sampleRate), 0.f, 1.f); // 20 ms in and out
        double lo[2], mi[2], hi[2];
        reX.split(0, l, lo[0], mi[0], hi[0]);
        reX.split(1, r, lo[1], mi[1], hi[1]);
        float o[2] = {0.f, 0.f};
        for (int b = 0; b < 3; ++b)
        {
            const double *band = b == 0 ? lo : b == 1 ? mi : hi;
            for (int c = 0; c < 2; ++c)
            {
                reG[b][c] += 0.002f * (reGT[b][c] - reG[b][c]);
                float x = (float)band[c];
                reLp[b][c] += reA[b] * (x - reLp[b][c]);
                x = reLp[b][c] * reTl[b] + (x - reLp[b][c]) * reTh[b];
                o[c] += x * reG[b][c];
            }
        }
        // the fade blends with the plain signal (not a phase-matched copy): ending on it means no jump when Reshape stops
        l += reFade * (o[0] - l);
        r += reFade * (o[1] - r);
    }
    // ----- Ancestors (v0.7): a tree of taps read from one buffer of the echoes
    static constexpr int ancNodes = 15; // node 0 is the echo itself; 1-2 its children, 3-6 grandchildren, 7-14 great-grandchildren
    std::vector<float> ancBuf;
    int ancW{0}, ancMask{0};
    float ancD[ancNodes]{}, ancG[ancNodes]{}, ancGT[ancNodes]{}, ancPL[ancNodes]{}, ancPR[ancNodes]{}, ancLp[ancNodes]{}, ancA[ancNodes]{};
    float ancGate{1.f}, ancOut{1.f}, ancOutT{1.f}, ancFade{0.f};
    // v0.8: each branch reads where it is (ancRd), moving gently toward where it should be (ancD); a jump bigger than a
    // glide crossfades from the old place (ancOld) over ~21 ms instead, so moving echo times never click
    float ancRd[ancNodes]{}, ancOld[ancNodes]{}, ancXf[ancNodes]{};
    bool ancFresh{true};
    float ancDPrevT[ancNodes]{};
    unsigned ancEpoch{~0u}, ancCalls{0};
    double ancGateUntil{0};
    bool ancActive{false}, ancOnCalls{false}, ancWant{false};
    int ancFromNow{0};
    static int ancGen(int n) { return n == 0 ? 0 : n <= 2 ? 1 : n <= 6 ? 2 : 3; }
    // control rate: where each branch sits (time, pan, tone, level) and which branches grow this time
    void ancestorsControl(const GySettings &G, double baseSec, double beatNow)
    {
        ancFromNow = std::clamp(G.ancFrom, 0, 2), ancOnCalls = G.ancGrow == 1;
        const bool want = G.ancOn && !ancBuf.empty();
        ancWant = want;
        if (!want)
        {
            // keep playing while the family fades out (ancestorsTick lowers ancFade); then stop and clear
            if (ancActive && ancFade <= 0.f)
            {
                std::fill(ancBuf.begin(), ancBuf.end(), 0.f), std::fill(std::begin(ancLp), std::end(ancLp), 0.f), std::fill(std::begin(ancG), std::end(ancG), 0.f);
                ancActive = false, ancFresh = true;
            }
            return;
        }
        ancActive = true;
        const int gens = std::clamp(G.ancGens, 1, 3);
        const double base = std::clamp(baseSec, 0.03, 2.0) * sampleRate, maxD = (double)(ancMask - 4);
        const float sp = std::clamp(G.ancSpread, 0.f, 1.f), fan = std::clamp(G.ancFan, 0.f, 1.f);
        const float per = 0.85f - 0.6f * std::clamp(G.ancFade, 0.f, 1.f); // level kept by each generation
        // which tree grows: a new shape each bar (Always) or on each of Lead's calls
        const bool onCalls = G.ancGrow == 1;
        if (onCalls && eventCount != ancCalls)
            ancCalls = eventCount, ancGateUntil = timeSec + std::max(0.05, baseSec);
        const unsigned epoch = onCalls ? eventCount : (unsigned)(uint64_t)std::floor(std::max(0.0, beatNow) / 4.0);
        float pan[ancNodes]{};
        bool alive[ancNodes]{};
        alive[0] = true;
        ancD[0] = 0.f;
        for (int n = 1; n < ancNodes; ++n)
        {
            const int par = (n - 1) / 2, g = ancGen(n);
            const bool later = (n & 1) == 0; // the second child of each pair comes later
            const double step = base * (later ? 1.0 + sp : 1.0 - 0.6 * sp) * std::pow(0.8, g - 1);
            ancD[n] = (float)std::clamp((double)ancD[par] + step, 1.0, maxD);
            pan[n] = std::clamp(pan[par] + (later ? 1.f : -1.f) * fan * std::pow(0.5f, (float)(g - 1)) * 0.7f, -1.f, 1.f);
            alive[n] = g <= gens && alive[par] && rand01(0xA1CE5ull + epoch, (uint64_t)n, 77) < std::clamp(G.ancChance, 0.f, 1.f);
            const float gtNew = alive[n] ? std::pow(per, (float)g) : 0.f;
#ifdef TEST
            if (std::getenv("GY_ANCTRACE") && (gtNew != ancGT[n] || std::fabs(ancD[n] - ancDPrevT[n]) > 64.f))
                std::fprintf(stderr, "t=%.4f node %d gain %.3f->%.3f delay %.0f->%.0f base %.4f\n", timeSec, n, ancGT[n], gtNew, ancDPrevT[n], ancD[n], baseSec);
            ancDPrevT[n] = ancD[n];
#endif
            ancGT[n] = gtNew;
            ancPL[n] = std::cos((pan[n] + 1.f) * 0.7853982f), ancPR[n] = std::sin((pan[n] + 1.f) * 0.7853982f);
            const float hz = 14000.f * std::exp2(-2.6f * std::clamp(G.ancDarken, 0.f, 1.f) * (float)g);
            ancA[n] = 1.f - std::exp(-6.2831853f * std::min(hz, 0.45f * (float)sampleRate) / (float)sampleRate);
        }
        ancEpoch = epoch;
        ancOutT = dbToLin(std::clamp(G.ancLevelDb, -24.f, 6.f));
        if (ancFresh) // just switched on: the buffer is empty, so every branch can start where it belongs
        {
            for (int n = 0; n < ancNodes; ++n)
                ancRd[n] = ancOld[n] = ancD[n], ancXf[n] = 1.f;
            ancOut = ancOutT, ancFresh = false;
        }
    }
    // per sample: write the echoes in, read every living branch out
    void ancestorsTick(float in, float &outL, float &outR)
    {
        const bool onCalls = ancOnCalls;
        const float gateT = !onCalls || timeSec < ancGateUntil ? 1.f : 0.f;
        ancGate += 0.002f * (gateT - ancGate);
        ancBuf[(size_t)ancW] = in * ancGate;
        ancFade = std::clamp(ancFade + (ancWant ? 1.f : -1.f) / (0.03f * (float)sampleRate), 0.f, 1.f); // 30 ms in and out
        ancOut += 0.0015f * (ancOutT - ancOut);
        auto readAt = [&](float d) {
            const int di = (int)d;
            const float fr = d - (float)di;
            const float a = ancBuf[(size_t)((ancW - di) & ancMask)], b = ancBuf[(size_t)((ancW - di - 1) & ancMask)];
            return a + (b - a) * fr;
        };
        const float xfStep = 1.f / (0.021f * (float)sampleRate);
        float l = 0.f, r = 0.f;
        for (int n = 1; n < ancNodes; ++n)
        {
            ancG[n] += 0.0015f * (ancGT[n] - ancG[n]);
            if (ancG[n] < 1e-5f && ancGT[n] == 0.f)
            {
                ancRd[n] = ancOld[n] = ancD[n], ancXf[n] = 1.f; // silent: just follow
                continue;
            }
            const float tgt = ancD[n], diff = tgt - ancRd[n];
            if (ancXf[n] >= 1.f)
            {
                if (std::fabs(diff) > 64.f)
                    ancOld[n] = ancRd[n], ancRd[n] = tgt, ancXf[n] = 0.f; // a jump: crossfade to the new place
                else
                    ancRd[n] += std::clamp(0.002f * diff, -0.02f, 0.02f); // a tiny move: glide, never more than 2 % off speed
            }
            float x = readAt(ancRd[n]);
            if (ancXf[n] < 1.f)
            {
                const float t = ancXf[n];
                x = readAt(ancOld[n]) * (1.f - t) + x * t;
                ancXf[n] = std::min(1.f, t + xfStep);
            }
            ancLp[n] += ancA[n] * (x - ancLp[n]);
            x = ancLp[n] * ancG[n];
            l += x * ancPL[n], r += x * ancPR[n];
        }
#ifdef TEST
        if (std::getenv("GY_ANCTRACE"))
        {
            static float prevL = 0.f, prev2 = 0.f;
            const float o = l * ancOut * ancFade, dd = o - 2 * prevL + prev2;
            if (std::fabs(dd) > 0.006f)
            {
                std::fprintf(stderr, "STEP t=%.5f d2=%.4f out %.4f gate %.3f fade %.3f\n", timeSec, dd, o, ancGate, ancFade);
                for (int n = 1; n < ancNodes; ++n)
                    if (ancG[n] > 1e-4f)
                        std::fprintf(stderr, "   node %d g %.3f rd %.1f tgt %.1f old %.1f xf %.3f lp %.4f\n", n, ancG[n], ancRd[n], ancD[n], ancOld[n], ancXf[n], ancLp[n]);
            }
            prev2 = prevL, prevL = o;
        }
#endif
        ancW = (ancW + 1) & ancMask;
        outL = l * ancOut * ancFade, outR = r * ancOut * ancFade;
    }
    bool curBandInf[3]{}, curBandRev[3]{};
    int patWant[2]{-1, -1}, patHeld[2]{-1, -1}; // v0.8: the Juba pattern number, settled
    double patWantT[2]{};
};
} // namespace otiskanno::gullay

#endif
