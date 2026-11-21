/*
 * Gullay - CLAP plugin (forked from Three's Company) (the part a host such as Bitwig Studio talks to).
 * Otis Kanno, 2026. GPL v3, see LICENSE.
 *
 * The window (macOS) is a web view, see gy_gui.h. Elsewhere the host shows its own controls.
 * Every parameter can be automated; the continuous ones can be modulated.
 * Parallel and Series each have their own full set of parameters, so each routing
 * keeps its settings.
 */
#include "gy_engine.h"
#include "gy_presets.h"
#include "gy_midi.h"
#include <map>
#include <ctime>
#include <limits>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include <string>
#include <unordered_map>
#include <vector>

#include <clap/clap.h>

#include "gy_gui.h"
#include "gy_gui_html.h"

#include <mutex>

#if defined(__aarch64__) || defined(__arm64__)
#elif defined(__SSE2__)
#include <xmmintrin.h>
#endif

using namespace otiskanno::gullay;

namespace
{
// Very small numbers slow processors down badly; treat them as zero while processing.
struct ScopedNoDenormals
{
#if defined(__aarch64__) || defined(__arm64__)
    uint64_t old;
    ScopedNoDenormals()
    {
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(old));
        uint64_t v = old | (1ull << 24); // FZ
        __asm__ __volatile__("msr fpcr, %0" : : "r"(v));
    }
    ~ScopedNoDenormals() { __asm__ __volatile__("msr fpcr, %0" : : "r"(old)); }
#elif defined(__SSE2__)
    unsigned old;
    ScopedNoDenormals()
    {
        old = _mm_getcsr();
        _mm_setcsr(old | 0x8040); // FTZ + DAZ
    }
    ~ScopedNoDenormals() { _mm_setcsr(old); }
#endif
};

// ---------------------------------------------------------------------------
// Parameters. Ids never change once a version is out; new ones get new ids.
// ---------------------------------------------------------------------------
inline const char *pluginVersion() { return "0.7.0"; }

// ----- settings kept on this computer (not in projects): defaults for new copies, and hover tips
struct Prefs
{
    int sizePct{100}; // 60..200
    bool dark{false};
    bool tips{true};
    std::string presetDir; // empty: the standard place (beside this file, in Presets)
};
inline std::string prefsPath()
{
#if defined(_WIN32)
    const char *base = std::getenv("APPDATA");
    return base ? std::string(base) + "\\Gullay\\prefs.txt" : std::string();
#elif defined(__APPLE__)
    const char *home = std::getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/Gullay/prefs.txt" : std::string();
#else
    const char *home = std::getenv("HOME");
    return home ? std::string(home) + "/.config/gullay/prefs.txt" : std::string();
#endif
}
inline Prefs loadPrefs()
{
    Prefs p;
    std::string path = prefsPath();
    if (path.empty())
        return p;
    if (FILE *f = std::fopen(path.c_str(), "r"))
    {
        char line[2048];
        while (std::fgets(line, sizeof(line), f))
        {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
                l.pop_back();
            auto num = [&](size_t at) { return std::atoi(l.c_str() + at); };
            if (l.rfind("size ", 0) == 0)
                p.sizePct = std::clamp(num(5), 60, 200);
            else if (l.rfind("dark ", 0) == 0)
                p.dark = num(5) != 0;
            else if (l.rfind("tips ", 0) == 0)
                p.tips = num(5) != 0;
            else if (l.rfind("presets ", 0) == 0)
                p.presetDir = l.substr(8);
        }
        std::fclose(f);
    }
    return p;
}
inline void savePrefs(const Prefs &p)
{
    std::string path = prefsPath();
    if (path.empty())
        return;
    std::string dir = path.substr(0, path.find_last_of("/\\"));
#if defined(_WIN32)
    std::string parent = dir.substr(0, dir.find_last_of('\\'));
    (void)_mkdir(parent.c_str());
    (void)_mkdir(dir.c_str());
#else
    std::string parent = dir.substr(0, dir.find_last_of('/'));
    (void)::mkdir(parent.c_str(), 0755);
    (void)::mkdir(dir.c_str(), 0755);
#endif
    if (FILE *f = std::fopen(path.c_str(), "w"))
    {
        std::fprintf(f, "size %d\ndark %d\ntips %d\n", p.sizePct, p.dark ? 1 : 0, p.tips ? 1 : 0);
        if (!p.presetDir.empty())
            std::fprintf(f, "presets %s\n", p.presetDir.c_str());
        std::fclose(f);
    }
}

enum Kind { kPercent, kDb, kPitch, kToggle, kRouting, kOversample, kModel, kVariant, kDriveType, kOrder,
            kEnum, kInt, kPan, kLean, kLogHz, kSemis, kHzTone, kBipolar, kLock, kOffset, kMs, kRatio, kPLock, kLogMs, kNote,
            kTime, kLeadTime, kBits, kTilt, kReach };

enum GlobalId
{
    idRouting = 1, idOversample = 2, idSmart = 3, idAutoGain = 4, idLimiter = 5, idModDepth = 6,
    // Sway
    idSwayAmount = 20, idSwayRate, idSwaySync, idSwayBars, idSwayMotion, idSwaySlew, idSwayFollow, idSwayDepth,
    // Conductor
    idInfluence = 30, idLean,
    // Mutation
    idMutTrigger = 40, idMutSens, idMutHold,
    idMutLow = 50,  // + rMode, rTarget, rAmount, rChance, rDelay
    idMutHigh = 60,
    idMutate = 70, idMutEvery, idMutFreeze, idMutSeed
};
enum RespOffset { rMode = 0, rTarget, rAmount, rChance, rDelay };
// Phase 3
constexpr clap_id laneBase(int l) { return 3000u + 100u * (uint32_t)l; }
// a P-lock: lane l, knob k (cutoff, reso, morph, drive, level, pan), step i
// steps 1-16 keep their 0.8 ids; steps 17-32 live at 7000
constexpr clap_id plockId(int l, int k, int i) { return (i < 16 ? 6000u : 7000u) + 100u * (uint32_t)l + 16u * (uint32_t)k + (uint32_t)(i % 16); }
// a sequencer step: 1-16 at the lane's base, 17-32 at base + 60
constexpr clap_id seqStepId(int l, int i) { return 3000u + 100u * (uint32_t)l + (i < 16 ? (uint32_t)i : 60u + (uint32_t)(i - 16)); }
static const char *plockName[6] = {"Time", "Feedback", "Tone", "Drive", "Level", "Pan"};
static const float plockMin[6] = {-48, 0, 0, 0, -60, -1}, plockMax[6] = {48, 1.2f, 1, 1, 12, 1}; // the parts' knob ranges
enum LaneOffset { lStep = 0, lLock = 20, lLength = 40, lRate, lEuclid, lHits, lRotate, lSmooth, lDepth, lTarget,
                  lClock, lSpeed, lSwing, lOffset, lLockTarget, lGlide, lSway, lLockLen, lLockRate, lDir };
constexpr clap_id idRetrigger = 80;
constexpr clap_id idSwayTo = 102; // 6: cutoff, reso, morph, drive, level, pan
constexpr clap_id idCondGrid = 110, idCondDivLow = 111, idCondDivHigh = 112, idCondChaos = 113, idCondRange = 114, idCondChance = 115,
                  idCondCondition = 116, idCondBursts = 117, idCondThird = 118, idCondMusical = 119, idCondSeed = 120, idJack = 121,
                  idJackPair = 122, idJackParam = 123;
// Tune (0.18)
enum TuneId : clap_id { idTuneOn = 8000, idTuneRoot, idTuneScale, idTunePlace, idTuneAmount, idTuneRing, idTuneLow, idTuneHigh,
                        idTuneTilt, idTuneDetune, idTuneWidth, idTuneMotion, idTuneMotionRate, idTuneMode,
                        idTuneSlope, idTunePivot, idTuneMotionSync, idTuneMotionDiv };
constexpr clap_id idSplitMode = 124, idSpecBy = 125, idSpecBalance = 126, idSpecSharp = 127, idSpecQuality = 128;
// Sway per band: 90 + 4 x band + {motion, rate, depth, slew}
constexpr clap_id swayBandBase(int b) { return 90u + 4u * (uint32_t)b; }
constexpr clap_id lfoBase(int i) { return 4000u + 20u * (uint32_t)i; }
enum LfoOffset { fShape = 0, fMode, fSync, fRate, fPhase, fSmooth, fPolarity, fSway, fTarget, fAmount, fStepCount, fGlide, fQuant };
constexpr clap_id lfoStepBase(int i) { return 4200u + 20u * (uint32_t)i; }
enum EnvId : clap_id { idEnvSource = 4100, idEnvSens, idEnvAttack, idEnvRelease, idScGain, idScHp, idScLp };
constexpr clap_id slotBase(int k) { return 5000u + 4u * (uint32_t)k; }
enum SlotOffset { sSource = 0, sTarget, sAmount, sSwing };

inline std::vector<std::string> targetLabels()
{
    std::vector<std::string> t{"None"};
    const char *band[3] = {"A", "Lead", "B"}, *par[6] = {"Time", "Feedback", "Tone", "Drive", "Level", "Pan"};
    for (auto *b : band)
        for (auto *q : par)
            t.push_back(std::string(b) + " " + q);
    for (auto *g : {"Spectral Low/Mid", "Balance", "Runaway Tame", "Crusher Mix", "Mix", "Sway", "Mod Depth", "Spectral Balance", "Spectral Sharpness",
                    "A Mod Depth", "Lead Mod Depth", "B Mod Depth", "Tune Amount", "Tune Ring Time", "Tune Tilt", "Tune Slope",
                    "Lead Filter Cutoff", "Lead Filter Reso", "Lead Filter Morph", "Tilt In", "Tilt Out", "Juba Pattern", "Juba Morph", "Grit (Bits)",
                    "Crusher Bits", "Crusher Rate", "Reverb Size", "Reverb Decay", "Reverb Level", "Reverb Mod", "Reverb Send",
                    "A Shift", "Lead Shift", "B Shift", "Runaway Ceiling",
                    "Spectral Mid/High", "Spectral Low Level", "Spectral Mid Level", "Spectral High Level",
                    "A Filter Cutoff", "A Filter Reso", "A Filter Morph", "B Filter Cutoff", "B Filter Reso", "B Filter Morph"})
        t.push_back(g);
    return t;
}
enum ModeOffset { oInput = 3, oOutput = 4, oMix = 5 };
// a part's knobs (TC's strip offsets, re-used): Time, Feedback, Tone, Drive, Level, Pan
enum BandOffset
{
    oCutoff = 3, oReso = 4, oMorph = 5, oDrive = 8, oLevel = 10, oMute = 11, oSolo = 12, oPan = 13,
    oSync = 14, oInf = 15, oReverse = 16, oModDepthB = 27
};
// ----- Gullay's own controls
enum GyId : clap_id
{
    idSlot1 = 9000, idSlot2, idSlot2Mode, idDldMemory, idDegrade, idPattern, idMorph, idTapoModAmt, idTapoModRate, idBalance,
    idAnswer, idAnswerChance, idMirrorRatio,
    idFiltModel = 9020, idFiltVariant, idFiltPlace, idFiltCut, idFiltReso, idFiltMorph, // Lead's filter (the old shared one)
    idFiltA = 9330, idFiltB = 9340, // + 0 Type, 1 Variant, 2 Place, 3 Cutoff, 4 Reso, 5 Morph
    idTiltIn = 9030, idPivotIn, idTiltOut, idPivotOut,
    idTame = 9040, idCeiling, idLoopLow, idLoopHigh, idReturn, idHold,
    idBits = 9050, idCrushRate, idCrushMix, idCrushSmooth, idChip, idCrushPlace,
    idShift0 = 9060, idShiftMode0 = 9063, idSettle = 9066,
    idRvOn = 9070, idRvFlavour, idRvSize, idRvDecay, idRvPre, idRvDiff, idRvModDepth, idRvModRate, idRvModShape, idRvLow, idRvHigh,
    idRvFreeze, idRvTuned, idRvFollowKey, idRvSend, idRvLevel, idRvInto,
    idRiverPass = 9090, idRiverTiming, idRiverRate, idMeander, idSpread, idTide, idTideBars,
    idSyncMode = 9100, idFreeBpm, idFreePlay,
    idMidiKey = 9110, idMidiPatterns,
    // v0.2
    idMorphTiming = 9120, idMorphSync, idTapoVelo, idKill, idAutoKill, idKillAfter, idTuneOnly,
    idSpecLoMid = 9130, idSpecMidHi, idSpecLvl0,
    // v0.3
    idSrcDepth0 = 9140, // 10
    idHymnShape = 9150, idHymnThresh, idHymnHold, idHymnDecay, idChapLock = 9155, idChapLockSteps, idTapCount,
    idMemRate = 9170, idTapeDrive = 9175, idTapeSat, idTapeBias, idMemTilt = 9180, idMemNoise,
    idVarispeed = 9185, idVariGlide, idChainCross = 9188, idChainWireTone, idChainWireDrive, idHymnAttack = 9192, idHymnSustain, idHymnRelease, idOscillate = 9196, idOscTails,
    idSnapOn = 9400, idSnapRate, idSnapLen, idSnapOrder, idSnapGlide, idSnapBlank, // the Snapshot sequencer
    idSnapStep0 = 9410, idSnapChance0 = 9430, // 16 each
    idT2Pattern = 9160, idT2Morph, idT2MorphTiming, idT2MorphSync, idT2Velo, idT2ModAmt, idT2ModRate,
    idAncOn = 9500, idAncGens, idAncSpread, idAncFan, idAncFade, idAncDarken, idAncChance, idAncGrow, idAncFrom, idAncLevel, // v0.7 Ancestors
    idPlockLen0 = 9200 // + 32 x lane + step
};
constexpr clap_id modeBase(int m) { return 1000u * (uint32_t)(m + 1); }
constexpr clap_id bandBase(int m, int b) { return modeBase(m) + 100u * (uint32_t)(b + 1); }

struct ParamDef
{
    clap_id id;
    std::string name, module;
    double min, max, def;
    Kind kind;
    int band{-1}, mode{-1}; // for kVariant: where its Model parameter lives
    std::vector<std::string> labels{}; // for kEnum
};

int modelIndexByName(const char *sub)
{
    const auto &cat = filterCatalogue();
    for (size_t i = 0; i < cat.size(); ++i)
        if (cat[i].name.find(sub) != std::string::npos)
            return (int)i;
    return 0;
}
// knob position (0..1) that selects the first variant of the wanted kind
double variantPosFor(int model, PassKind kind)
{
    const auto &mi = filterCatalogue()[(size_t)model];
    int n = (int)mi.configs.size();
    for (int i = 0; i < n; ++i)
        if (mi.kinds[(size_t)i] == kind)
            return n > 1 ? (double)i / (n - 1) : 0.0;
    return 0.0;
}

const std::vector<ParamDef> &paramDefs()
{
    static const std::vector<ParamDef> &defs = *new std::vector<ParamDef>([]()
    {
        std::vector<ParamDef> d;
        const int nModels = (int)filterCatalogue().size();
        const int svf = modelIndexByName("Fast SVF");
        d.push_back({idRouting, "Wiring", "", 0, 1, 0, kRouting});
        d.push_back({idLimiter, "Limiter", "", 0, 1, 1, kToggle});
        d.push_back({idModDepth, "Mod Depth", "", 0, 2, 1, kPercent});
        const char *modeName[2] = {"Parallel", "Series"}, *modeShort[2] = {"Par", "Ser"};
        const char *bandName[3] = {"A", "Lead", "B"};
        for (int m = 0; m < 2; ++m)
        {
            std::string mod = modeName[m], pre = std::string(modeShort[m]) + " ";
            d.push_back({modeBase(m) + oInput, pre + "Input", mod, -24, 24, 0, kDb});
            d.push_back({modeBase(m) + oOutput, pre + "Output", mod, -24, 24, 0, kDb});
            d.push_back({modeBase(m) + oMix, pre + "Mix", mod, 0, 1, 0.35, kPercent});
            for (int b = 0; b < 3; ++b)
            {
                std::string bm = mod + "/" + bandName[b], bp = pre + bandName[b] + " ";
                clap_id base = bandBase(m, b);
                // A: one beat; B: a dotted beat (1.5); Lead: x1. Feedback: A and B 45 %, Lead x1 (100 %).
                double tdef = 0.0; // Init: a plain quarter-note delay on A and B
                d.push_back({base + oCutoff, bp + "Time", bm, -48, 48, tdef, b == 1 ? kLeadTime : kTime});
                d.push_back({base + oReso, bp + "Feedback", bm, 0, 1.2, b == 1 ? 1.0 : 0.4, kPercent});
                d.push_back({base + oMorph, bp + "Tone", bm, 0, 1, 0.5, kPercent});
                d.push_back({base + oDrive, bp + "Drive", bm, 0, 1, 0, kPercent});
                d.push_back({base + oLevel, bp + "Level", bm, -60, 12, 0, kDb});
                d.push_back({base + oMute, bp + "Mute", bm, 0, 1, 0, kToggle});
                d.push_back({base + oSolo, bp + "Solo", bm, 0, 1, 0, kToggle});
                d.push_back({base + oPan, bp + "Pan", bm, -1, 1, 0.0, kPan});
                d.push_back({base + oSync, bp + "Time Sync", bm, 0, 1, 1, kToggle});
                d.push_back({base + oInf, bp + (b == 1 ? "Hold All" : "INF"), bm, 0, 1, 0, kToggle});
                d.push_back({base + oReverse, bp + "Reverse", bm, 0, 1, 0, kToggle});
                d.push_back({base + oModDepthB, bp + "Mod Depth", bm, 0, 2, 1, kPercent});
            }
        }
        // Shared by every routing: Sway, the Conductor, Mutation
        auto en = [&d](clap_id id, const char *name, const char *mod, const std::vector<std::string> &labels, int def)
        {
            ParamDef p{id, name, mod, 0, (double)labels.size() - 1, (double)def, kEnum};
            p.labels = labels;
            d.push_back(std::move(p));
        };
        d.push_back({idSwayAmount, "Sway", "Sway", 0, 1, 0, kPercent});
        d.push_back({idSwayRate, "Sway Rate", "Sway", std::log2(0.01), std::log2(10.0), std::log2(0.12), kLogHz});
        d.push_back({idSwaySync, "Sway Sync", "Sway", 0, 1, 0, kToggle});
        en(idSwayBars, "Sway Sync Length", "Sway", {"1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"}, 4);
        en(idSwayMotion, "Sway Motion", "Sway", {"Smooth", "Stepped"}, 0);
        d.push_back({idSwaySlew, "Sway Slew", "Sway", 0, 1, 0.4, kPercent});
        d.push_back({idSwayFollow, "Sway Follow", "Sway", 0, 1, 0, kPercent});
        d.push_back({idSwayDepth, "Sway Depth", "Sway", 0, 48, 12, kReach});
        for (int b = 0; b < 3; ++b)
        {
            std::string pre = std::string("Sway ") + bandName[b] + " ";
            en(swayBandBase(b), (pre + "Motion").c_str(), "Sway", {"Main", "Smooth", "Stepped"}, 0);
            d.push_back({swayBandBase(b) + 1, pre + "Rate", "Sway", 0.25, 4, 1, kRatio});
            d.push_back({swayBandBase(b) + 2, pre + "Depth", "Sway", 0, 1, 1, kPercent});
            d.push_back({swayBandBase(b) + 3, pre + "Slew", "Sway", 0, 1, 0.4, kPercent});
        }
        {
            static const char *dst[6] = {"Time", "Feedback", "Tone", "Drive", "Level", "Pan"};
            for (int k = 0; k < 6; ++k)
                d.push_back({idSwayTo + (clap_id)k, std::string("Sway to ") + dst[k], "Sway", 0, 1, k == 0 ? 1.0 : 0.0, kPercent});
        }
        en(idCondGrid, "Black Herman Grid", "Black Herman", {"Smooth", "1/64", "1/32T", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1 bar", "2 bars", "4 bars", "8 bars"}, 0);
        d.push_back({idCondDivLow, "Black Herman A Every", "Black Herman", 1, 16, 1, kInt});
        d.push_back({idCondDivHigh, "Black Herman B Every", "Black Herman", 1, 16, 1, kInt});
        d.push_back({idCondChaos, "Black Herman Chaos", "Black Herman", 0, 1, 0, kPercent});
        d.push_back({idCondRange, "Black Herman Range", "Black Herman", 0, 48, 12, kReach});
        d.push_back({idCondChance, "Black Herman Chance", "Black Herman", 0, 1, 1, kPercent});
        en(idCondCondition, "Black Herman Bars", "Black Herman", {"Every bar", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "1:8", "8:8"}, 0);
        d.push_back({idCondBursts, "Black Herman Bursts", "Black Herman", 0, 1, 0, kPercent});
        d.push_back({idCondThird, "Legerdemain Mischief", "Black Herman", 0, 1, 0, kPercent});
        en(idCondMusical, "Black Herman Snap", "Black Herman", {"Free", "Semitones", "Fifths", "Octaves"}, 0);
        d.push_back({idCondSeed, "Black Herman Seed", "Black Herman", 0, 999, 1, kInt});
        d.push_back({idJack, "Legerdemain", "Black Herman", 0, 1, 0, kPercent});
        en(idJackPair, "Legerdemain Pair", "Black Herman", {"A <> B", "Lead vs both", "A > B", "B > A", "Lead > A", "Lead > B"}, 0);
        en(idJackParam, "Legerdemain Moves", "Black Herman", {"Time", "Feedback", "Tone", "Drive", "Level", "Pan"}, 1);
        // Parallel's split: crossover filters or the spectral (FFT) split
        en(idSplitMode, "Parallel Split", "Spectral", {"Parallel", "Spectral"}, 0);
        en(idSpecBy, "Spectral Split By", "Spectral", {"Frequency", "Tonal"}, 0);
        d.push_back({idSpecBalance, "Spectral Balance", "Spectral", -1, 1, 0, kBipolar});
        d.push_back({idSpecSharp, "Spectral Sharpness", "Spectral", 0, 1, 0.6, kPercent});
        en(idSpecQuality, "Spectral Quality", "Spectral", {"Fast", "Normal", "High", "Ultra"}, 1);
        en(idOversample, "Oversample", "", {"Off", "2x", "4x"}, 0);
        d.push_back({idInfluence, "Black Herman Influence", "Black Herman", 0, 1, 0, kPercent});
        d.push_back({idLean, "Black Herman Lean", "Black Herman", 0, 1, 0.5, kLean});
        en(idMutTrigger, "Mutation Trigger", "Mutation", {"Lead gets louder", "Lead time jumps", "Runaway catches", "Lead's sequencer", "Follower hit", "Black Herman moves"}, 0);
        d.push_back({idMutSens, "Mutation Sensitivity", "Mutation", 0, 1, 0.5, kPercent});
        en(idMutHold, "Mutation Hold-off", "Mutation", {"none", "1 step", "2 steps", "3 steps", "4 steps", "6 steps", "8 steps"}, 1);
        for (int r = 0; r < 2; ++r)
        {
            clap_id b = r == 0 ? idMutLow : idMutHigh;
            std::string pre = r == 0 ? "Mutation A " : "Mutation B ", mod = r == 0 ? "Mutation/A" : "Mutation/B";
            en(b + rMode, (pre + "Answer").c_str(), mod.c_str(), {"Off", "Same way", "Opposite way"}, r == 0 ? 1 : 2);
            en(b + rTarget, (pre + "Moves").c_str(), mod.c_str(), {"Time", "Feedback", "Drive", "Level", "Pan", "Tone", "Juba Pattern", "Filter", "Crusher Bits", "Hymn",
                                                                   "Shift", "Grit", "Tilt", "Spectral Band"}, r == 0 ? 1 : 3);
            d.push_back({b + rAmount, pre + "Amount", mod, 0, 1, 0.4, kPercent});
            d.push_back({b + rChance, pre + "Chance", mod, 0, 1, 1, kPercent});
            en(b + rDelay, (pre + "Delay").c_str(), mod.c_str(), {"none", "1 step", "2 steps", "3 steps", "4 steps"}, r == 0 ? 0 : 1);
        }
        d.push_back({idMutate, "Mutate", "Mutation", 0, 1, 0, kPercent});
        en(idMutEvery, "Mutate Every", "Mutation", {"1 bar", "2 bars", "4 bars", "8 bars", "16 bars"}, 2);
        d.push_back({idMutFreeze, "Mutate Freeze", "Mutation", 0, 1, 0, kToggle});
        d.push_back({idMutSeed, "Mutate Seed", "Mutation", 0, 999, 0, kInt});
        // ----- Phase 3: sequencer lanes with their lock lanes
        const auto tl = targetLabels();
        const char *laneName[3] = {"A", "Lead", "B"};
        const int laneDefTarget[3] = {bandTarget(Low, PRes), bandTarget(Mid, PMorph), bandTarget(High, PLevel)};
        for (int l = 0; l < 3; ++l)
        {
            clap_id b = laneBase(l);
            std::string pre = std::string("Seq ") + laneName[l] + " ", mod = std::string("Sequencer/") + laneName[l];
            for (int i = 0; i < numSteps; ++i)
                d.push_back({seqStepId(l, i), pre + "Step " + std::to_string(i + 1), mod, -1, 1, 0, kBipolar});
            // Elektron-style locks: each step can lock each of the band's six knobs.
            // -1 = not locked, 0..1 = where the knob is locked (across its range)
            for (int k = 0; k < (int)numBandParams; ++k)
                for (int i = 0; i < numSteps; ++i)
                    d.push_back({plockId(l, k, i), std::string("Lock ") + laneName[l] + " " + plockName[k] + " " + std::to_string(i + 1),
                                 std::string("P-Locks/") + laneName[l], -1, 1, -1, kPLock});
            d.push_back({b + lLength, pre + "Length", mod, 1, 32, 16, kInt});
            en(b + lRate, (pre + "Rate").c_str(), mod.c_str(), {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 bar", "1/64", "1/32T", "1/16D", "1/8D", "1/4D", "1/2D", "1/1T", "1/32D", "2 bars", "4 bars", "8 bars"}, 2);
            en(b + lEuclid, (pre + "Euclid").c_str(), mod.c_str(), {"Off", "Gate", "Trig"}, 0);
            d.push_back({b + lHits, pre + "Hits", mod, 0, 32, 4, kInt});
            d.push_back({b + lRotate, pre + "Rotate", mod, 0, 31, 0, kInt});
            d.push_back({b + lSmooth, pre + "Smooth", mod, 0, 1, 0, kPercent});
            d.push_back({b + lDepth, pre + "Depth", mod, 0, 1, 1, kPercent});
            en(b + lTarget, (pre + "Target").c_str(), mod.c_str(), tl, laneDefTarget[l]);
            en(b + lClock, (pre + "Timing").c_str(), mod.c_str(), {"Clock", "Free"}, 0);
            d.push_back({b + lSpeed, pre + "Speed", mod, std::log2(0.1), std::log2(500.0), std::log2(4.0), kLogHz});
            d.push_back({b + lSwing, pre + "Swing", mod, 0.5, 0.75, 0.5, kPercent});
            d.push_back({b + lOffset, pre + "Offset", mod, -0.5, 0.5, 0, kOffset});
            d.push_back({b + lGlide, std::string("Lock ") + laneName[l] + " Glide", std::string("P-Locks/") + laneName[l], 0.2, 500, 2, kMs});
            d.push_back({b + lSway, pre + "Sway", mod, 0, 1, 0, kPercent});
            en(b + lDir, (pre + "Direction").c_str(), mod.c_str(), {"Forward", "Reverse", "Ping-pong", "Random"}, 0);
            {
                std::vector<std::string> ll{"Seq"};
                for (int i = 1; i <= numSteps; ++i)
                    ll.push_back(std::to_string(i));
                en(b + lLockLen, (std::string("Lock ") + laneName[l] + " Length").c_str(), (std::string("P-Locks/") + laneName[l]).c_str(), ll, 0);
                en(b + lLockRate, (std::string("Lock ") + laneName[l] + " Rate").c_str(), (std::string("P-Locks/") + laneName[l]).c_str(),
                   {"Seq", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 bar", "1/64", "1/32T", "1/16D", "1/8D", "1/4D", "1/2D", "1/1T", "1/32D", "2 bars", "4 bars", "8 bars"}, 0);
            }
        }
        en(idRetrigger, "Retrigger", "Sequencer", {"Song position", "Every bar", "Every 2 bars", "Every 4 bars", "On start"}, 0);
        // ----- LFOs
        for (int i = 0; i < numLfos; ++i)
        {
            clap_id b = lfoBase(i);
            std::string pre = "LFO " + std::to_string(i + 1) + " ", mod = "LFO " + std::to_string(i + 1);
            en(b + fShape, (pre + "Shape").c_str(), mod.c_str(), {"Drawn", "Sine", "Triangle", "Ramp", "Square", "Pump", "Swell", "Bounce", "Stairs", "S&H", "Steps"}, i == 0 ? 0 : (i == 1 ? 5 : 1));
            en(b + fMode, (pre + "Timing").c_str(), mod.c_str(), {"Sync", "Free", "Retrig"}, 0);
            en(b + fSync, (pre + "Sync Rate").c_str(), mod.c_str(), {"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars"}, 4);
            d.push_back({b + fRate, pre + "Rate", mod, std::log2(0.01), std::log2(500.0), 0.0, kLogHz});
            d.push_back({b + fPhase, pre + "Phase", mod, 0, 1, 0, kPercent});
            d.push_back({b + fSmooth, pre + "Smooth", mod, 0, 1, 0, kPercent});
            en(b + fPolarity, (pre + "Output").c_str(), mod.c_str(), {"Bipolar", "Unipolar"}, 0);
            d.push_back({b + fSway, pre + "Sway", mod, 0, 1, 0, kPercent});
            en(b + fTarget, (pre + "Target").c_str(), mod.c_str(), tl, 0);
            d.push_back({b + fAmount, pre + "Amount", mod, -1, 1, 0.5, kBipolar});
            d.push_back({b + fStepCount, pre + "Steps", mod, 1, 16, 8, kInt});
            d.push_back({b + fGlide, pre + "Glide", mod, 0, 1, 0, kPercent});
            en(b + fQuant, (pre + "Scale").c_str(), mod.c_str(), {"Free", "Semitones", "Major", "Minor", "Pentatonic", "Octaves"}, 0);
            for (int k = 0; k < 16; ++k)
                d.push_back({lfoStepBase(i) + (clap_id)k, pre + "Step " + std::to_string(k + 1), mod, -1, 1, 0, kBipolar});
        }
        // ----- envelope follower
        en(idEnvSource, "Follower Listens To", "Follower", {"Input", "Sidechain", "Part A", "Lead (all echoes)", "Part B"}, 0);
        d.push_back({idEnvSens, "Follower Sens", "Follower", -24, 36, 6, kDb});
        d.push_back({idEnvAttack, "Follower Attack", "Follower", 0.1, 100, 5, kMs});
        d.push_back({idEnvRelease, "Follower Release", "Follower", 1, 2000, 150, kMs});
        d.push_back({idScGain, "Sidechain Gain", "Follower", -24, 24, 0, kDb});
        d.push_back({idScHp, "Sidechain High-pass", "Follower", hzToSemi(10), hzToSemi(5000), hzToSemi(20), kHzTone});
        d.push_back({idScLp, "Sidechain Low-pass", "Follower", hzToSemi(40), hzToSemi(20000), hzToSemi(20000), kHzTone});
        // ----- mod matrix
        for (int k = 0; k < numSlots; ++k)
        {
            clap_id b = slotBase(k);
            std::string pre = "Mod " + std::to_string(k + 1) + " ";
            en(b + sSource, (pre + "Source").c_str(), "Mod matrix", {"None", "Random", "Mod Wheel", "Velocity", "LFO 1", "LFO 2", "LFO 3", "Hymn Env", "River A", "River Lead", "River B"}, 0);
            en(b + sTarget, (pre + "Target").c_str(), "Mod matrix", tl, 0);
            d.push_back({b + sAmount, pre + "Amount", "Mod matrix", -1, 1, 0, kBipolar});
            // new slots go one way (0.18); older projects and presets keep "As source" (see loadStateText)
            en(b + sSwing, (pre + "Swing").c_str(), "Mod matrix", {"As source", "Both ways", "One way"}, 2);
        }
        // ----- Tune
        d.push_back({idTuneOn, "Tune On", "Tune", 0, 1, 0, kToggle});
        en(idTuneRoot, "Tune Root", "Tune", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 9);
        en(idTuneScale, "Tune Scale", "Tune", {"Octaves", "Fifths up", "Fifths down", "Major 3rds up", "Major 3rds down", "Minor 3rds up", "Minor 3rds down",
                                               "Major chord", "Minor chord", "Major", "Minor", "Dorian", "Mixolydian", "Pentatonic", "Blues", "Whole tone", "Chromatic"}, 10);
        en(idTuneMode, "Tune Mode", "Tune", {"Layer", "Blend"}, 0);
        en(idTunePlace, "Tune Place", "Tune", {"Into the delays", "On the echoes"}, 1);
        d.push_back({idTuneAmount, "Tune Amount", "Tune", 0, 1, 0.4, kPercent});
        d.push_back({idTuneRing, "Tune Ring Time", "Tune", std::log2(20.0), std::log2(4000.0), std::log2(400.0), kLogMs});
        d.push_back({idTuneLow, "Tune Lowest", "Tune", 24, 108, 36, kNote});
        d.push_back({idTuneHigh, "Tune Highest", "Tune", 24, 108, 96, kNote});
        d.push_back({idTuneTilt, "Tune Tilt", "Tune", -1, 1, 0, kBipolar});
        d.push_back({idTuneDetune, "Tune Detune", "Tune", 0, 1, 0, kPercent});
        d.push_back({idTuneWidth, "Tune Width", "Tune", 0, 1, 0.5, kPercent});
        en(idTuneMotion, "Tune Wave", "Tune", {"Still", "Swell", "Drift", "Pulse", "Arp", "Fall", "Sparkle"}, 0);
        d.push_back({idTuneMotionRate, "Tune Wave Rate", "Tune", std::log2(0.02), std::log2(4.0), std::log2(0.25), kLogHz});
        en(idTuneMotionSync, "Tune Wave Timing", "Tune", {"Free", "Sync"}, 0);
        en(idTuneMotionDiv, "Tune Wave Sync", "Tune", {"1/16", "1/8T", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"}, 9);
        d.push_back({idTuneSlope, "Tune Slope", "Tune", -1, 1, 0, kBipolar});
        d.push_back({idTunePivot, "Tune Slope Pivot", "Tune", std::log2(100.0), std::log2(8000.0), std::log2(1000.0), kLogHz});
        // ----- Gullay: engines
        en(idSlot1, "Engine 1", "Engines", {"Echo", "Juba"}, 0);
        en(idSlot2, "Engine 2", "Engines", {"Off", "Echo", "Juba"}, 0);
        en(idSlot2Mode, "Shadow", "Engines", {"Off", "Answer", "Reverse", "Stutter", "Freeze", "Octave"}, 0);
        en(idDldMemory, "DLD Memory", "Engines", {"Clean", "16-bit", "Tape"}, 1);
        d.push_back({idTapeDrive, "Tape Drive", "Engines", 0, 1, 0.5, kPercent});
        d.push_back({idTapeSat, "Tape Saturation", "Engines", 0, 1, 0.5, kPercent});
        d.push_back({idTapeBias, "Tape Bias", "Engines", 0, 1, 0.5, kPercent});
        d.push_back({idDegrade, "DLD Degrade", "Engines", 11, 16, 16, kBits});
        d.push_back({idMemRate, "Memory Rate", "Engines", std::log2(2000.0), std::log2(44100.0), std::log2(44100.0), kLogHz});
        d.push_back({idMemTilt, "Memory Tilt", "Engines", -1, 1, 0, kBipolar});
        d.push_back({idChainCross, "Chain Cross", "Engines", 0, 0.95, 0, kPercent});
        d.push_back({idChainWireTone, "Chain Wire Tone", "Engines", -1, 1, 0, kBipolar});
        d.push_back({idChainWireDrive, "Chain Wire Drive", "Engines", 0, 1, 0, kPercent});
        d.push_back({idMemNoise, "Memory Noise", "Engines", 0, 1, 0, kPercent});
        {
            std::vector<std::string> pl;
            for (int bk = 0; bk < 4; ++bk)
                for (int k = 0; k < 6; ++k)
                    pl.push_back(std::string(1, (char)('A' + bk)) + std::to_string(k + 1));
            pl.push_back("Tapped");
            en(idPattern, "Juba Pattern", "Engines", pl, 0);
        }
        d.push_back({idMorph, "Juba Morph", "Engines", std::log2(5.0), std::log2(8000.0), std::log2(350.0), kLogMs});
        d.push_back({idTapoModAmt, "Juba Mod Amount", "Engines", 0, 1, 0, kPercent});
        d.push_back({idTapoModRate, "Juba Mod Rate", "Engines", 0, 1, 0.3, kPercent});
        d.push_back({idBalance, "Engine Balance", "Engines", -1, 1, 0, kBipolar});
        en(idAnswer, "Answer After", "Engines", {"1/16", "1/8", "1/8D", "1/4", "1/2", "1 bar"}, 1);
        d.push_back({idAnswerChance, "Answer Chance", "Engines", 0, 1, 1, kPercent});
        d.push_back({idMirrorRatio, "Mirror Time", "Engines", 0.25, 4, 1, kRatio});
        // ----- tone: the filter (TC's models), the tilts
        // each path's filter: A, Lead (ids kept from the old shared filter, which it still is), B
        {
            const int nModels = (int)filterCatalogue().size();
            const clap_id base[3] = {idFiltA, idFiltModel, idFiltB};
            const char *nm[3] = {"A", "Lead", "B"};
            for (int k = 0; k < 3; ++k)
            {
                std::string pre = std::string(nm[k]) + " Filter ";
                d.push_back({base[k], pre + "Type", "Tone", 0, (double)(nModels - 1), 0, kModel});
                d.push_back({base[k] + 1, pre + "Variant", "Tone", 0, 1, 0, kVariant, -2, -2});
                en(base[k] + 2, (pre + "Place").c_str(), "Tone", {"In", "Out", "Loop"}, 1);
                d.push_back({base[k] + 3, pre + "Cutoff", "Tone", -60, 82, 24, kPitch});
                d.push_back({base[k] + 4, pre + "Reso", "Tone", 0, 1, 0.2, kPercent});
                d.push_back({base[k] + 5, pre + "Morph", "Tone", 0, 1, 0, kPercent});
            }
        }
        d.push_back({idTiltIn, "Tilt In", "Tone", -12, 12, 0, kTilt});
        d.push_back({idPivotIn, "Tilt In Pivot", "Tone", std::log2(200.0), std::log2(5000.0), std::log2(800.0), kLogHz});
        d.push_back({idTiltOut, "Tilt Out", "Tone", -12, 12, 0, kTilt});
        d.push_back({idPivotOut, "Tilt Out Pivot", "Tone", std::log2(200.0), std::log2(5000.0), std::log2(1600.0), kLogHz});
        // ----- Runaway, crusher
        d.push_back({idTame, "Runaway Tame", "Runaway", 0, 1, 0.6, kPercent});
        d.push_back({idCeiling, "Runaway Ceiling", "Runaway", -36, -6, -12, kDb});
        d.push_back({idLoopLow, "Loop Low Cut", "Runaway", std::log2(10.0), std::log2(2000.0), std::log2(80.0), kLogHz});
        d.push_back({idLoopHigh, "Loop High Cut", "Runaway", std::log2(500.0), std::log2(20000.0), std::log2(9000.0), kLogHz});
        d.push_back({idReturn, "Runaway Return", "Runaway", 0.1, 4, 1.2, kRatio});
        d.push_back({idHold, "Runaway Hold", "Runaway", 0, 1, 0, kToggle});
        d.push_back({idBits, "Crusher Bits", "Crusher", 2, 16, 16, kBits});
        d.push_back({idCrushRate, "Crusher Rate", "Crusher", std::log2(1000.0), std::log2(48000.0), std::log2(48000.0), kLogHz});
        d.push_back({idCrushMix, "Crusher Mix", "Crusher", 0, 1, 0, kPercent});
        en(idCrushSmooth, "Crusher Steps", "Crusher", {"Raw", "Smooth"}, 0);
        d.push_back({idChip, "Chip Mode", "Crusher", 0, 1, 0, kToggle});
        en(idCrushPlace, "Crusher Place", "Crusher", {"Loop", "Out", "In", "Both"}, 1); // Memory's Where: Out by default
        // ----- Hymn's shifts (the key and Tune are TC's Tune page)
        for (int b = 0; b < 3; ++b)
        {
            d.push_back({idShift0 + (clap_id)b, std::string(bandName[b]) + " Shift", "Hymn", -12, 12, 0, kSemis});
            en(idShiftMode0 + (clap_id)b, (std::string(bandName[b]) + " Shift Mode").c_str(), "Hymn", {"Off", "Once", "Each repeat"}, 0);
        }
        d.push_back({idSettle, "Shift Settle", "Hymn", 0, 1, 1, kToggle});
        // ----- the River
        en(idRiverPass, "River Pass", "River", {"1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"}, 4);
        en(idRiverTiming, "River Timing", "River", {"Synced", "Free"}, 0);
        d.push_back({idRiverRate, "River Rate", "River", std::log2(0.01), std::log2(10.0), std::log2(0.1), kLogHz});
        d.push_back({idMeander, "River Meander", "River", 0, 1, 0.7, kPercent});
        d.push_back({idSpread, "River Spread", "River", 0, 1, 0.25, kPercent});
        d.push_back({idTide, "River Tide", "River", 0, 1, 0, kToggle});
        en(idTideBars, "Tide Turns Every", "River", {"2 bars", "4 bars", "8 bars", "16 bars"}, 2);
        // ----- v0.2: Tapo morph timing and tap dynamics, the kill switch, Hymn's only-ringing, the spectral bands
        en(idMorphTiming, "Juba Morph Timing", "Engines", {"Free", "Sync"}, 0);
        en(idMorphSync, "Juba Morph Sync", "Engines", {"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars"}, 2);
        d.push_back({idTapoVelo, "Juba Velocity", "Engines", 0, 1, 0.3, kPercent});
        d.push_back({idKill, "Kill", "Runaway", 0, 1, 0, kToggle});
        d.push_back({idAutoKill, "Auto Kill", "Runaway", 0, 1, 1, kToggle});
        d.push_back({idKillAfter, "Auto Kill After", "Runaway", 5, 60, 30, kInt});
        d.push_back({idTuneOnly, "Tune Only Ringing", "Tune", 0, 1, 0, kToggle});
        d.push_back({idSpecLoMid, "Spectral Low/Mid", "Spectral", hzToSemi(40), hzToSemi(2000), hzToSemi(250), kPitch});
        d.push_back({idSpecMidHi, "Spectral Mid/High", "Spectral", hzToSemi(400), hzToSemi(16000), hzToSemi(3000), kPitch});
        for (int b = 0; b < 3; ++b)
            d.push_back({idSpecLvl0 + (clap_id)b, std::string("Spectral ") + (b == 0 ? "Low" : b == 1 ? "Mid" : "High") + " Level", "Spectral", -60, 12, 0, kDb});
        // ----- v0.3: source depths, Hymn's shape, the Chaperone's lock-in, tap count, P-lock lengths
        {
            const char *sn[10] = {"Random", "Mod Wheel", "Velocity", "LFO 1", "LFO 2", "LFO 3", "Hymn Env", "River A", "River Lead", "River B"};
            for (int k = 0; k < 10; ++k)
                d.push_back({idSrcDepth0 + (clap_id)k, std::string("Depth ") + sn[k], "Mod/Depths", 0, 1, 1, kPercent});
        }
        en(idHymnShape, "Hymn Shape", "Tune", {"Off", "Gate", "Decay", "Envelope"}, 0);
        d.push_back({idHymnThresh, "Hymn Threshold", "Tune", -60, 0, -30, kDb});
        d.push_back({idHymnHold, "Hymn Hold", "Tune", 0, 1000, 30, kMs});
        d.push_back({idHymnDecay, "Hymn Decay", "Tune", std::log2(5.0), std::log2(8000.0), std::log2(250.0), kLogMs});
        d.push_back({idHymnAttack, "Hymn Attack", "Tune", std::log2(1.0), std::log2(4000.0), std::log2(5.0), kLogMs});
        d.push_back({idHymnSustain, "Hymn Sustain", "Tune", 0, 1, 0.6, kPercent});
        d.push_back({idHymnRelease, "Hymn Release", "Tune", std::log2(5.0), std::log2(8000.0), std::log2(400.0), kLogMs});
        d.push_back({idChapLock, "Black Herman Lock", "Black Herman", 0, 1, 0, kToggle});
        en(idChapLockSteps, "Black Herman Lock Steps", "Black Herman", {"4", "8", "16", "32"}, 2);
        // the Snapshot sequencer: 16 steps, each a stored snapshot (S1-S8) or "-" (keep what's playing)
        d.push_back({idSnapOn, "Snap Seq On", "Snapshots", 0, 1, 0, kToggle});
        en(idSnapRate, "Snap Seq Rate", "Snapshots", {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 bar", "1/64", "1/32T", "1/16D", "1/8D", "1/4D", "1/2D", "1/1T", "1/32D", "2 bars", "4 bars", "8 bars"}, 8);
        d.push_back({idSnapLen, "Snap Seq Length", "Snapshots", 1, 16, 8, kInt});
        en(idSnapOrder, "Snap Seq Order", "Snapshots", {"Forward", "Backward", "Ping-pong", "Random", "Drunk"}, 0);
        d.push_back({idSnapGlide, "Snap Seq Glide", "Snapshots", 0, 1, 0, kPercent});
        en(idSnapBlank, "Snap Seq Blank Steps", "Snapshots", {"Hold last snapshot", "Revert to preset"}, 0);
        {
            static std::deque<std::string> nm;
            for (int i = 0; i < 16; ++i)
            {
                nm.push_back("Snap Step " + std::to_string(i + 1));
                en(idSnapStep0 + (clap_id)i, nm.back().c_str(), "Snapshots", {"-", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8"}, 0);
                d.push_back({idSnapChance0 + (clap_id)i, "Snap Step " + std::to_string(i + 1) + " Chance", "Snapshots", 0.01, 1, 1, kPercent});
            }
        }
        en(idTapCount, "Juba Tap Count", "Engines", {"4", "8", "16"}, 0);
        {
            std::vector<std::string> pl;
            for (int bk = 0; bk < 4; ++bk)
                for (int k = 0; k < 6; ++k)
                    pl.push_back(std::string(1, (char)('A' + bk)) + std::to_string(k + 1));
            pl.push_back("Tapped");
            en(idT2Pattern, "Juba 2 Pattern", "Engines/Juba 2", pl, 6);
        }
        d.push_back({idT2Morph, "Juba 2 Morph", "Engines/Juba 2", std::log2(5.0), std::log2(8000.0), std::log2(350.0), kLogMs});
        en(idT2MorphTiming, "Juba 2 Morph Timing", "Engines/Juba 2", {"Free", "Sync"}, 0);
        en(idT2MorphSync, "Juba 2 Morph Sync", "Engines/Juba 2", {"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars"}, 2);
        d.push_back({idT2Velo, "Juba 2 Velocity", "Engines/Juba 2", 0, 1, 0.3, kPercent});
        d.push_back({idT2ModAmt, "Juba 2 Mod Amount", "Engines/Juba 2", 0, 1, 0, kPercent});
        d.push_back({idT2ModRate, "Juba 2 Mod Rate", "Engines/Juba 2", 0, 1, 0.3, kPercent});
        for (int l = 0; l < 3; ++l)
            for (int i = 0; i < 32; ++i)
                d.push_back({idPlockLen0 + (clap_id)(32 * l + i), std::string("Lock ") + bandName[l] + " Steps " + std::to_string(i + 1), std::string("P-Locks/") + bandName[l], 1, 16, 1, kInt});
        // ----- transport and MIDI
        en(idSyncMode, "Sync", "Transport", {"Host", "Free"}, 0);
        d.push_back({idVarispeed, "Varispeed", "Transport", 0, 1, 0, kToggle});
        d.push_back({idOscillate, "Oscillate", "", 0, 1, 0, kToggle});
        en(idOscTails, "Oscillate Kill", "", {"Tails ring on", "Cut at once"}, 0);
        // v0.7 Ancestors: the family tree of echoes (Mutation page)
        d.push_back({idAncOn, "Ancestors", "Ancestors", 0, 1, 0, kToggle});
        en(idAncGens, "Ancestors Generations", "Ancestors", {"1", "2", "3"}, 1);
        d.push_back({idAncSpread, "Ancestors Spread", "Ancestors", 0, 1, 0.5, kPercent});
        d.push_back({idAncFan, "Ancestors Fan", "Ancestors", 0, 1, 0.6, kPercent});
        d.push_back({idAncFade, "Ancestors Fade", "Ancestors", 0, 1, 0.5, kPercent});
        d.push_back({idAncDarken, "Ancestors Darken", "Ancestors", 0, 1, 0.4, kPercent});
        d.push_back({idAncChance, "Ancestors Chance", "Ancestors", 0, 1, 1, kPercent});
        en(idAncGrow, "Ancestors Grow", "Ancestors", {"Always", "On Lead's calls"}, 0);
        en(idAncFrom, "Ancestors From", "Ancestors", {"All echoes", "A", "B"}, 0);
        d.push_back({idAncLevel, "Ancestors Level", "Ancestors", -24, 6, 0, kDb});
        d.push_back({idVariGlide, "Varispeed Bend Time", "Transport", std::log2(20.0), std::log2(10000.0), std::log2(300.0), kLogMs}); // v0.8: was "Varispeed Glide", 20 ms-4 s
        d.push_back({idFreeBpm, "Free Tempo", "Transport", 30, 300, 120, kInt});
        d.push_back({idFreePlay, "Free Play", "Transport", 0, 1, 1, kToggle});
        d.push_back({idMidiKey, "MIDI Sets Key", "MIDI", 0, 1, 0, kToggle});
        d.push_back({idMidiPatterns, "MIDI Picks Patterns", "MIDI", 0, 1, 0, kToggle});
        return d;
    }());
    return defs;
}

const std::unordered_map<clap_id, int> &paramIndex()
{
    static const std::unordered_map<clap_id, int> &ix = *new std::unordered_map<clap_id, int>([]()
    {
        std::unordered_map<clap_id, int> r;
        const auto &d = paramDefs();
        for (size_t i = 0; i < d.size(); ++i)
            r[d[i].id] = (int)i;
        return r;
    }());
    return ix;
}

bool isStepped(Kind k)
{
    return k != kPercent && k != kDb && k != kPitch && k != kVariant && k != kPan && k != kLean && k != kLogHz &&
           k != kSemis && k != kHzTone && k != kBipolar && k != kLock && k != kOffset && k != kMs && k != kRatio && k != kPLock && k != kLogMs &&
           k != kTime && k != kLeadTime && k != kBits && k != kTilt && k != kReach;
}

// ---------------------------------------------------------------------------
struct Plugin
{
    clap_plugin_t clap{};
    const clap_host_t *host{nullptr};
    std::atomic<int> latencyReported{0};
    std::atomic<bool> restartAsked{false};
    Engine engine;
    Settings settings;
    std::vector<std::atomic<float>> value; // what the knobs say
    std::vector<float> mod;                // host modulation on top (audio thread only)
    std::atomic<bool> dirty{true};
    std::vector<float> bufL, bufR, scBufL, scBufR;
    // MIDI (audio thread): held notes set Hymn's key, C1-B2 and Program Change pick Tapo patterns
    int midiRoot{-1}, midiPattern{-1};
    // the tapped Tapo pattern: tap times in the module's units (samples at 48 kHz, at 120 bpm); main thread
    std::vector<float> tapped;
    // ----- the Snapshot sequencer: 8 stored settings, stepped through in time (audio thread plays them)
    static constexpr int numSnaps = 8, numSnapSteps = 16;
    static const std::vector<clap_id> &snapIds()
    {
        static std::vector<clap_id> ids = [] {
            std::vector<clap_id> v;
            std::vector<std::string> n;
            for (const char *m : {"Par", "Ser"})
                for (const char *b : {"A", "Lead", "B"})
                    for (const char *k : {"Time", "Feedback", "Tone", "Drive", "Level", "Pan"})
                        n.push_back(std::string(m) + " " + b + " " + k);
            for (const char *m : {"Par", "Ser"})
                n.push_back(std::string(m) + " Mix");
            for (const char *b : {"A", "Lead", "B"})
                for (const char *k : {"Cutoff", "Reso", "Morph"})
                    n.push_back(std::string(b) + " Filter " + k);
            for (const char *k : {"Tune Amount", "Crusher Mix", "Memory Rate", "Crusher Bits"})
                n.push_back(k);
            for (auto &x : n)
                for (auto &d : paramDefs())
                    if (d.name == x)
                    {
                        v.push_back(d.id);
                        break;
                    }
            return v;
        }();
        return ids;
    }
    // slots 0-7 are the snapshots; slot 8 is the selected preset, kept for "Revert to preset" on blank steps
    static constexpr int presetSlot = numSnaps;
    std::unique_ptr<std::atomic<float>[]> snapVal{new std::atomic<float>[(numSnaps + 1) * 64]};
    std::atomic<bool> snapValid[numSnaps + 1]{};
    std::atomic<int> snapLoaded{-1}; // the snapshot last loaded onto the knobs (Recall / Edit), -1 none
    std::vector<float> snapOv; // per parameter: the value the sequencer plays, or NaN (audio thread writes)
    std::vector<int> snapIdx;  // parameter index of each snapshot entry
    float snapFrom[64]{}, snapTo[64]{};
    int snapCur{-1}, snapPos{-1}; // snapCur: the snapshot playing, presetSlot = the preset (blank: Revert), -1 the knobs
    long snapLast{LONG_MIN};
    double snapStart{0};
    bool snapOvOn{false};
    static float snapHash(long k, int salt)
    {
        uint32_t x = (uint32_t)(k * 2654435761u) ^ (uint32_t)(salt * 40503u + 0x9e3779b9u);
        x ^= x >> 15, x *= 0x2c1b3c6du, x ^= x >> 12, x *= 0x297a2d39u, x ^= x >> 15;
        return (x & 0xffffff) / 16777216.f;
    }
    float baseOf(int pi) const { return value[(size_t)pi].load(std::memory_order_relaxed); }
    void snapStore(int k)
    {
        const auto &ids = snapIds();
        for (size_t i = 0; i < ids.size() && i < 64; ++i)
            snapVal[(size_t)k * 64 + i].store(baseOf(snapIdx[i]), std::memory_order_relaxed);
        snapValid[k].store(true);
    }
    // the preset just loaded becomes the "Revert to preset" target (main thread)
    void snapTakePreset()
    {
        const auto &ids = snapIds();
        for (size_t i = 0; i < ids.size() && i < 64; ++i)
            snapVal[(size_t)presetSlot * 64 + i].store(baseOf(snapIdx[i]), std::memory_order_relaxed);
        snapValid[presetSlot].store(true);
        snapLoaded.store(-1);
    }
    // audio thread, before the settings are read for a stretch of samples.
    // Each step: a snapshot that passes its chance plays (gliding from what's playing). A blank step, a step whose
    // chance fails, or one naming an empty slot is a blank: "Hold last" keeps what's playing (before any snapshot
    // has played, that is the knobs); "Revert to preset" goes back to the selected preset (or the knobs if none).
    void runSnapSeq()
    {
        const auto &ids = snapIds();
        bool any = false;
        for (int k = 0; k < numSnaps; ++k)
            any = any || snapValid[k].load(std::memory_order_relaxed);
        if (snapOv.empty() || !any || step(idSnapOn) == 0)
        {
            if (snapOvOn)
            {
                std::fill(snapOv.begin(), snapOv.end(), std::numeric_limits<float>::quiet_NaN());
                snapOvOn = false, dirty.store(true, std::memory_order_relaxed);
            }
            snapCur = -1, snapPos = -1, snapLast = LONG_MIN;
            return;
        }
        static constexpr float rateBeats[20] = {0.125f, 1.f / 6.f, 0.25f, 1.f / 3.f, 0.5f, 2.f / 3.f, 1.f, 2.f, 4.f, 0.0625f, 1.f / 12.f, 0.375f, 0.75f, 1.5f, 3.f, 8.f / 3.f, 0.1875f, 8.f, 16.f, 32.f};
        double rb = rateBeats[std::clamp(step(idSnapRate), 0, 19)];
        double beats = engine.beatsNow();
        long k = (long)std::floor(beats / rb);
        int L = std::clamp(step(idSnapLen), 1, numSnapSteps);
        if (k != snapLast)
        {
            auto pmod = [](long a, long m) { return (int)(((a % m) + m) % m); };
            int pos = 0;
            switch (step(idSnapOrder))
            {
            default: pos = pmod(k, L); break;
            case 1: pos = L - 1 - pmod(k, L); break;
            case 2: { long P = std::max(1, 2 * L - 2); int m = pmod(k, P); pos = m < L ? m : (int)P - m; break; }
            case 3: pos = std::min(L - 1, (int)(snapHash(k, 7) * L)); break;
            case 4: pos = (k == snapLast + 1 && snapPos >= 0) ? pmod(snapPos + (snapHash(k, 11) < 0.5f ? -1 : 1), L) : pmod(k, L); break;
            }
            snapPos = pos;
            int want = step(idSnapStep0 + (clap_id)pos) - 1;
            float ch = val(idSnapChance0 + (clap_id)pos);
            bool pass = ch >= 0.999f || snapHash(k, 3 + pos) < ch;
            int target = snapCur;                                   // blank, Hold last: keep what's playing
            if (want >= 0 && want < numSnaps && snapValid[want].load(std::memory_order_relaxed) && pass)
                target = want;
            else if (step(idSnapBlank) == 1)                        // blank, Revert to preset
                target = snapValid[presetSlot].load(std::memory_order_relaxed) ? presetSlot : -1;
            if (target != snapCur || snapLast == LONG_MIN)
            {
                for (size_t i = 0; i < ids.size() && i < 64; ++i)
                {
                    float o = snapOv[(size_t)snapIdx[i]];
                    snapFrom[i] = std::isfinite(o) ? o : baseOf(snapIdx[i]);
                    snapTo[i] = target >= 0 ? snapVal[(size_t)target * 64 + i].load(std::memory_order_relaxed) : baseOf(snapIdx[i]);
                }
                snapCur = target, snapStart = (double)k * rb;
            }
            snapLast = k;
        }
        if (snapCur < 0 && !snapOvOn)
            return;
        float g = val(idSnapGlide);
        double prog = g <= 0.001f ? 1.0 : std::clamp((beats - snapStart) / (g * rb), 0.0, 1.0);
        bool changed = false;
        for (size_t i = 0; i < ids.size() && i < 64; ++i)
        {
            float to = snapCur >= 0 ? snapTo[i] : baseOf(snapIdx[i]);
            float v = snapFrom[i] + (to - snapFrom[i]) * (float)prog;
            const auto &d = paramDefs()[(size_t)snapIdx[i]];
            if (isStepped(d.kind))
                v = std::round(v);
            if (snapOv[(size_t)snapIdx[i]] != v)
                snapOv[(size_t)snapIdx[i]] = v, changed = true;
        }
        snapOvOn = true;
        if (snapCur < 0 && prog >= 1.0) // back on the knobs: let go
        {
            std::fill(snapOv.begin(), snapOv.end(), std::numeric_limits<float>::quiet_NaN());
            snapOvOn = false, changed = true;
        }
        if (changed)
            dirty.store(true, std::memory_order_relaxed);
    }
    void applyTapped()
    {
        TapoPattern p{};
        p.size = std::min((int)tapped.size(), 32);
        for (int t = 0; t < p.size; ++t)
        {
            p.time[t] = tapped[(size_t)t];
            p.velo[t] = 1.f;
            p.type[t] = 0;
            p.pan[t] = (t % 2) ? 0.7f : 0.3f;
        }
        if (p.size > 0)
            engine.setTapped(p);
    }
    uint8_t held[128]{};
    GyMidiLearn learn;
    const clap_output_events_t *curOut{nullptr};
    double sampleRate{48000};

    // what the window shows besides the knobs, copied from the engine after every block
    enum Meter
    {
        mBand = 0,                          // 3 x 6: modulated cutoff, reso, morph, drive, level, pan
        mGlobal = mBand + 18,               // 6: split lo, split hi, feedback, noise, mix, sway
        mLaneStep = mGlobal + 6,            // 3
        mLaneVal = mLaneStep + 3,           // 3
        mLfoVal = mLaneVal + 3,             // 3
        mLfoPhase = mLfoVal + 3,            // 3
        mEnv = mLfoPhase + 3, mSc, mLimiter, mOutGuard,
        mGuard,                             // 3
        mEdge = mGuard + 3,                 // 3
        mSway = mEdge + 3,                  // 3
        mMutEvents = mSway + 3,
        mHist,                              // 16 x 3
        mLockStep = mHist + 48,             // 3
        mBank = mLockStep + 3,              // 3 x 8 gains now
        mBankHz = mBank + 24,               // 3 x 8 centres
        mChap = mBankHz + 24,               // Low, High offsets from the Chaperone (st), its step count
        mTunePos = mChap + 3,               // Tune Motion's position (cycles)
        mMutNow,                            // 2 x 5: Low and High answers after Mutate: mode, moves, amount, chance, delay
        mMutK = mMutNow + 10,               // how many times Mutate has changed them, and how far into the period
        mMutFrac,
        // Gullay
        mWiring, mPattern, mPartSec,        // 3 seconds per part
        mRunaway = mPartSec + 3,            // 3 Runaway gains
        mRiver = mRunaway + 3,              // 3 currents
        mInf = mRiver + 3,                  // 3
        mLatency = mInf + 3, mRiverPos, mKills, mSpecSplit, // 2: the split points after modulation
        mRunT = mSpecSplit + 2, mChapLocked, mSnapPos, mSnapCur,
        mVari, // v0.8: 3, the tape speed of A, Lead and B under Varispeed (1 = steady)
        numMeters = mVari + 3
    };
    std::atomic<float> meter[numMeters];

    // the drawn curves (main thread), saved with the project
    std::vector<CurvePoint> curve[numLfos];

    // changes made in the window, on their way to the host (window -> audio thread)
    struct GuiEvent
    {
        uint16_t type; // 0 value, 1 gesture begin, 2 gesture end
        clap_id id;
        double value;
    };
    static constexpr size_t qSize = 1024;
    GuiEvent q[qSize];
    std::atomic<size_t> qHead{0}, qTail{0};
    void push(uint16_t type, clap_id id, double v)
    {
        size_t h = qHead.load(std::memory_order_relaxed), n = (h + 1) % qSize;
        if (n == qTail.load(std::memory_order_acquire))
            return; // full: the value itself is already stored, only the host's note of it is lost
        q[h] = {type, id, v};
        qHead.store(n, std::memory_order_release);
    }
    void sendGuiEvents(const clap_output_events_t *out)
    {
        size_t t = qTail.load(std::memory_order_relaxed);
        while (t != qHead.load(std::memory_order_acquire))
        {
            const auto &e = q[t];
            if (out)
            {
                if (e.type == 0)
                {
                    clap_event_param_value_t ev{};
                    ev.header = {sizeof(ev), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
                    ev.param_id = e.id;
                    ev.note_id = -1, ev.port_index = -1, ev.channel = -1, ev.key = -1;
                    ev.value = e.value;
                    out->try_push(out, &ev.header);
                }
                else
                {
                    clap_event_param_gesture_t ev{};
                    ev.header = {sizeof(ev), 0, CLAP_CORE_EVENT_SPACE_ID,
                                 (uint16_t)(e.type == 1 ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END), 0};
                    ev.param_id = e.id;
                    out->try_push(out, &ev.header);
                }
            }
            t = (t + 1) % qSize;
        }
        qTail.store(t, std::memory_order_release);
    }

    struct GuiBridge;
    GuiBridge *bridge{nullptr};
    const clap_host_params_t *hostParams{nullptr};
    std::atomic<bool> darkTheme{false}; // the window's look, saved with the project
    std::string themeName{"porch"};     // Gullay: the named theme (main thread)
    std::string presetLabel, uiState;   // v0.6: the preset name on the pill and the window's own state (page, open cards), kept with the project
    const clap_host_state_t *hostState{nullptr};
    void guiNeedsInit();
    void requestFlush();
    void markDirty();
    GuiBridge *ensureBridge();
    void deleteBridge();
    void *gui{nullptr};
    uint32_t guiW{1280}, guiH{940};
    uint32_t maxBlock{0};
    double guiScale{1.0}; // Windows: the screen scale the host reported (sizes to the host are in pixels)

    Plugin() : value(paramDefs().size()), mod(paramDefs().size(), 0.f)
    {
        const auto &d = paramDefs();
        for (size_t i = 0; i < d.size(); ++i)
            value[i].store((float)d[i].def, std::memory_order_relaxed);
        snapOv.assign(d.size(), std::numeric_limits<float>::quiet_NaN());
        for (clap_id id : snapIds())
            snapIdx.push_back(paramIndex().at(id));
        for (int k = 0; k < (numSnaps + 1) * 64; ++k)
            snapVal[(size_t)k].store(0.f, std::memory_order_relaxed);
        for (auto &m : meter)
            m.store(0.f, std::memory_order_relaxed);
        for (int i = 0; i < numLfos; ++i)
            curve[i] = defaultCurve();
        // a new copy opens with this computer's preferred theme and size (a project's own state replaces them)
        Prefs pr = loadPrefs();
        darkTheme.store(pr.dark);
        themeName = pr.dark ? "porchdark" : "porch"; // Porch Ceiling is the default look
        guiW = (uint32_t)std::lround(1280 * pr.sizePct / 100.0), guiH = (uint32_t)std::lround(940 * pr.sizePct / 100.0);
    }
    static std::vector<CurvePoint> defaultCurve()
    {
        return {{0, 0, 1}, {0.12f, 0.9f, 0.5f}, {0.3f, 0.2f, 2}, {0.42f, 0.6f, 1}, {0.62f, -0.8f, 0.45f}, {0.8f, -0.2f, 2.2f}, {1, 0, 1}};
    }
    void applyCurve(int i)
    {
        engine.setCustomCurve(i, curve[i].data(), (int)curve[i].size());
    }

    void readMeters()
    {
        auto st = [this](int k, float v) { meter[k].store(v, std::memory_order_relaxed); };
        for (int b = 0; b < 3; ++b)
            for (int q2 = 0; q2 < 6; ++q2)
                st(mBand + b * 6 + q2, engine.displayBand(b, q2));
        for (int g = 0; g < 6; ++g)
            st(mGlobal + g, engine.displayGlobal(TgtSplitLo + g));
        for (int l = 0; l < 3; ++l)
            st(mLaneStep + l, (float)engine.laneStep(l)), st(mLaneVal + l, engine.laneValue(l)), st(mLockStep + l, (float)engine.lockStep(l));
        for (int i = 0; i < 3; ++i)
            st(mLfoVal + i, engine.lfoValue(i)), st(mLfoPhase + i, engine.lfoPhase(i));
        st(mEnv, engine.envValue());
        st(mWiring, (float)engine.activeWiring()), st(mPattern, (float)engine.patternNow());
        for (int p = 0; p < 3; ++p)
            st(mPartSec + p, engine.partSeconds(p)), st(mRunaway + p, engine.runawayGain(p)), st(mRiver + p, engine.riverNow(p)),
                st(mInf + p, engine.infNow(p) ? 1.f : 0.f);
        st(mLatency, (float)engine.latencySamples());
        st(mRiverPos, engine.riverPos());
        st(mKills, (float)engine.kills);
        st(mRunT, engine.runawaySeconds()), st(mChapLocked, engine.chapLockedNow() ? 1.f : 0.f);
        st(mSnapPos, (float)snapPos), st(mSnapCur, (float)snapCur);
        for (int k = 0; k < 3; ++k)
            st(mVari + k, engine.tapeSpeed(k));
        st(mSpecSplit, engine.specSplitShown(0)), st(mSpecSplit + 1, engine.specSplitShown(1));
        st(mSc, engine.sidechainLevel());
        st(mLimiter, engine.limiterGain());
        st(mOutGuard, engine.outputGuardGain());
        for (int b = 0; b < 3; ++b)
            st(mGuard + b, engine.bandGuardGain(b)), st(mEdge + b, engine.edgePress(b)), st(mSway + b, engine.swayValue(b));
        st(mMutEvents, (float)engine.mutationEvents());
        st(mTunePos, (float)std::fmod(engine.tuneMotionPos(), 4096.0));
        for (int r = 0; r < 2; ++r)
        {
            const auto &A = engine.mutationAnswer(r);
            st(mMutNow + r * 5, (float)A.mode), st(mMutNow + r * 5 + 1, (float)A.target), st(mMutNow + r * 5 + 2, A.amount);
            st(mMutNow + r * 5 + 3, A.chance), st(mMutNow + r * 5 + 4, (float)A.delaySteps);
        }
        st(mMutK, (float)std::max(0L, engine.mutationPeriod())), st(mMutFrac, engine.mutationPeriodFrac());
        st(mChap, engine.conductorPassed(0)), st(mChap + 1, engine.conductorPassed(2)), st(mChap + 2, (float)engine.chaperoneSteps());
        for (int b = 0; b < 3; ++b)
            for (int k = 0; k < numBankBands; ++k)
                st(mBank + b * 8 + k, engine.bankGainNow(b, k)), st(mBankHz + b * 8 + k, engine.bankHz(b, k));
        for (int k = 0; k < 16; ++k)
        {
            auto ev = engine.mutationHistory(k);
            st(mHist + k * 3, (float)ev.dir), st(mHist + k * 3 + 1, (float)ev.low), st(mHist + k * 3 + 2, (float)ev.high);
        }
    }

    float val(clap_id id, bool withMod = true) const
    {
        auto it = paramIndex().find(id);
        if (it == paramIndex().end())
            return 0.f;
        const auto &d = paramDefs()[(size_t)it->second];
        float v = value[(size_t)it->second].load(std::memory_order_relaxed);
        if (!snapOv.empty())
        {
            float o = snapOv[(size_t)it->second];
            if (std::isfinite(o))
                v = o; // the Snapshot sequencer is playing this one
        }
        if (withMod)
            v += mod[(size_t)it->second];
        return std::clamp(v, (float)d.min, (float)d.max);
    }
    bool on(clap_id id) const { return val(id) >= 0.5f; }
    int step(clap_id id) const { return (int)std::lround(val(id)); }

    void buildSettings()
    {
        Settings &s = settings;
        s.routing = step(idRouting);
        s.splitMode = step(idSplitMode);
        s.specBy = step(idSpecBy);
        s.specBalance = val(idSpecBalance);
        s.specSharp = val(idSpecSharp);
        s.specQuality = step(idSpecQuality);
        s.oversample = std::clamp(step(idOversample), 0, 2);
        s.modDepth = val(idModDepth);
        s.smartEdges = false;
        s.autoGain = false;
        s.limiter = on(idLimiter);
        for (int m = 0; m < 2; ++m)
        {
            auto &M = s.mode[m];
            M.inputDb = val(modeBase(m) + oInput);
            M.outputDb = val(modeBase(m) + oOutput);
            M.mix = val(modeBase(m) + oMix);
            for (int b = 0; b < 3; ++b)
            {
                auto &B = M.band[b];
                clap_id base = bandBase(m, b);
                B.cutoff = val(base + oCutoff);
                B.resonance = val(base + oReso);
                B.morph = val(base + oMorph);
                B.drive = val(base + oDrive);
                float lv = val(base + oLevel);
                B.levelDb = lv <= -59.9f ? -60.f : lv;
                B.mute = on(base + oMute);
                B.solo = on(base + oSolo);
                B.pan = val(base + oPan);
                B.sync = on(base + oSync);
                B.inf = on(base + oInf);
                B.reverse = on(base + oReverse);
                B.modDepth = val(base + oModDepthB);
            }
        }
        {
            auto &G = s.gy;
            G.slot1 = step(idSlot1);
            G.slot2 = step(idSlot2) - 1;
            G.slot2Mode = step(idSlot2Mode);
            G.dld16 = step(idDldMemory) == 1;
            G.tape = step(idDldMemory) == 2;
            G.tapeDrive = val(idTapeDrive), G.tapeSat = val(idTapeSat), G.tapeBias = val(idTapeBias);
            G.degradeBits = val(idDegrade);
            G.memRateHz = (float)std::exp2(val(idMemRate));
            G.pattern = midiPattern >= 0 && on(idMidiPatterns) ? midiPattern : step(idPattern);
            G.morphSec = std::exp2(val(idMorph)) * 0.001f;
            G.morphSync = step(idMorphTiming) == 1, G.morphDiv = step(idMorphSync), G.tapoVelocity = val(idTapoVelo);
            G.kill = on(idKill), G.autoKill = on(idAutoKill), G.killAfterSec = val(idKillAfter);
            G.tuneOnly = on(idTuneOnly);
            for (int k = 0; k < 10; ++k)
                G.srcDepth[k] = val(idSrcDepth0 + (clap_id)k);
            G.hymnShape = step(idHymnShape), G.hymnThreshDb = val(idHymnThresh), G.hymnHoldMs = val(idHymnHold);
            G.hymnDecayMs = (float)std::exp2(val(idHymnDecay));
            G.chapLock = on(idChapLock);
            G.tapo2.pattern = step(idT2Pattern), G.tapo2.morphSec = std::exp2(val(idT2Morph)) * 0.001f;
            G.tapo2.morphSync = step(idT2MorphTiming) == 1, G.tapo2.morphDiv = step(idT2MorphSync);
            G.tapo2.velocity = val(idT2Velo), G.tapo2.modAmount = val(idT2ModAmt), G.tapo2.modRate = val(idT2ModRate);
            static constexpr int lockSteps[4] = {4, 8, 16, 32};
            G.chapLockSteps = lockSteps[std::clamp(step(idChapLockSteps), 0, 3)];
            for (int l = 0; l < 3; ++l)
                for (int i = 0; i < 32; ++i)
                    G.plockLen[l][i] = step(idPlockLen0 + (clap_id)(32 * l + i));
            G.specLoSemi = val(idSpecLoMid), G.specHiSemi = val(idSpecMidHi);
            for (int b = 0; b < 3; ++b)
                G.specLvlDb[b] = val(idSpecLvl0 + (clap_id)b);
            G.tapoModAmount = val(idTapoModAmt), G.tapoModRate = val(idTapoModRate);
            G.balance = val(idBalance);
            G.answerDiv = step(idAnswer), G.answerChance = val(idAnswerChance), G.mirrorRatio = val(idMirrorRatio);
            {
                const clap_id base[3] = {idFiltA, idFiltModel, idFiltB};
                for (int k = 0; k < 3; ++k)
                {
                    G.filterModel[k] = step(base[k]);
                    G.filterVariant[k] = variantIndexFor(G.filterModel[k], val(base[k] + 1));
                    G.filterPlace[k] = step(base[k] + 2);
                    G.filterCut[k] = val(base[k] + 3), G.filterReso[k] = val(base[k] + 4), G.filterMorph[k] = val(base[k] + 5);
                }
            }
            G.tiltIn = val(idTiltIn), G.pivotIn = std::exp2(val(idPivotIn));
            G.tiltOut = val(idTiltOut), G.pivotOut = std::exp2(val(idPivotOut));
            G.tame = val(idTame), G.ceilingDb = val(idCeiling);
            G.loopLowHz = std::exp2(val(idLoopLow)), G.loopHighHz = std::exp2(val(idLoopHigh));
            G.returnSec = val(idReturn), G.hold = on(idHold);
            G.crushBits = val(idBits), G.crushRateHz = std::exp2(val(idCrushRate)), G.crushMix = val(idCrushMix);
            G.crushSmooth = step(idCrushSmooth) == 1, G.chipMode = on(idChip), G.crushPlace = step(idCrushPlace);
            G.memTilt = val(idMemTilt), G.memNoise = val(idMemNoise);
            G.hymnAttackMs = (float)std::exp2(val(idHymnAttack)), G.hymnSustain = val(idHymnSustain), G.hymnReleaseMs = (float)std::exp2(val(idHymnRelease));
            G.varispeed = on(idVarispeed), G.variGlideMs = (float)std::exp2(val(idVariGlide));
            G.chainCross = val(idChainCross), G.wireTone = val(idChainWireTone), G.wireDrive = val(idChainWireDrive);
            G.oscillate = on(idOscillate), G.oscCut = step(idOscTails) == 1;
            G.ancOn = on(idAncOn), G.ancGens = step(idAncGens) + 1, G.ancSpread = val(idAncSpread), G.ancFan = val(idAncFan), G.ancFade = val(idAncFade);
            G.ancDarken = val(idAncDarken), G.ancChance = val(idAncChance), G.ancGrow = step(idAncGrow), G.ancFrom = step(idAncFrom), G.ancLevelDb = val(idAncLevel);
            G.modWheel = midiWheel.load(std::memory_order_relaxed), G.velocity = midiVelo.load(std::memory_order_relaxed);
            for (int b = 0; b < 3; ++b)
                G.shift[b] = step(idShift0 + (clap_id)b), G.shiftMode[b] = step(idShiftMode0 + (clap_id)b);
            G.settle = on(idSettle);
            G.reverbOn = false; // v0.3: the reverb is out
            G.riverPass = step(idRiverPass), G.riverSync = step(idRiverTiming) == 0, G.riverHz = std::exp2(val(idRiverRate));
            G.meander = val(idMeander), G.spread = val(idSpread), G.tide = on(idTide);
            static constexpr int tideBars[4] = {2, 4, 8, 16};
            G.tideBars = tideBars[std::clamp(step(idTideBars), 0, 3)];
            G.freeRun = step(idSyncMode) == 1, G.freeBpm = val(idFreeBpm), G.freePlay = on(idFreePlay);
        }
        auto &W = s.sway;
        W.amount = val(idSwayAmount);
        W.rateHz = std::exp2(val(idSwayRate));
        W.sync = on(idSwaySync);
        W.syncIndex = step(idSwayBars);
        W.stepped = step(idSwayMotion) == 1;
        W.slew = val(idSwaySlew);
        W.follow = val(idSwayFollow);
        W.depthSemis = val(idSwayDepth);
        W.toCut = val(idSwayTo), W.toRes = val(idSwayTo + 1), W.toMorph = val(idSwayTo + 2), W.toDrive = val(idSwayTo + 3), W.toLevel = val(idSwayTo + 4),
        W.toPan = val(idSwayTo + 5);
        for (int b = 0; b < 3; ++b)
        {
            W.band[b].motion = step(swayBandBase(b));
            W.band[b].rateMul = val(swayBandBase(b) + 1);
            W.band[b].depth = val(swayBandBase(b) + 2);
            W.band[b].slew = val(swayBandBase(b) + 3);
        }
        s.conductor.influence = val(idInfluence);
        {
            auto &C = s.conductor;
            C.grid = step(idCondGrid);
            C.divLow = step(idCondDivLow), C.divHigh = step(idCondDivHigh);
            C.chaos = val(idCondChaos), C.range = val(idCondRange), C.chance = val(idCondChance);
            C.condition = step(idCondCondition);
            C.bursts = val(idCondBursts), C.mischief = val(idCondThird);
            C.musical = step(idCondMusical), C.seed = step(idCondSeed);
            C.jack = val(idJack), C.jackPair = step(idJackPair), C.jackParam = step(idJackParam);
        }
        s.conductor.lean = val(idLean);
        auto &Mu = s.mutation;
        Mu.trigger = step(idMutTrigger);
        Mu.sensitivity = val(idMutSens);
        static constexpr int holdSteps[7] = {0, 1, 2, 3, 4, 6, 8};
        Mu.holdSteps = holdSteps[std::clamp(step(idMutHold), 0, 6)];
        for (int r = 0; r < 2; ++r)
        {
            clap_id b = r == 0 ? idMutLow : idMutHigh;
            Mu.resp[r] = {step(b + rMode), step(b + rTarget), val(b + rAmount), val(b + rChance), step(b + rDelay)};
        }
        Mu.mutate = val(idMutate);
        Mu.everyIndex = step(idMutEvery);
        Mu.freeze = on(idMutFreeze);
        Mu.seed = step(idMutSeed);
        for (int l = 0; l < numLanes; ++l)
        {
            auto &L = s.lane[l];
            clap_id b = laneBase(l);
            for (int i = 0; i < numSteps; ++i)
                L.step[i] = val(seqStepId(l, i));
            for (int k = 0; k < (int)numBandParams; ++k)
                for (int i = 0; i < numSteps; ++i)
                {
                    float v = val(plockId(l, k, i));
                    L.plock[k][i] = v < 0.f ? std::numeric_limits<float>::quiet_NaN() : plockMin[k] + std::clamp(v, 0.f, 1.f) * (plockMax[k] - plockMin[k]);
                }
            L.length = step(b + lLength);
            L.rateIndex = step(b + lRate);
            L.euclid = step(b + lEuclid);
            L.hits = step(b + lHits);
            L.rotate = step(b + lRotate);
            L.smooth = val(b + lSmooth);
            L.depth = val(b + lDepth);
            L.target = step(b + lTarget);
            L.freeRun = step(b + lClock) == 1;
            L.speedHz = std::exp2(val(b + lSpeed));
            L.swing = val(b + lSwing);
            L.offset = val(b + lOffset);
            L.lockGlideMs = val(b + lGlide);
            L.sway = val(b + lSway);
            L.lockLength = step(b + lLockLen);
            L.direction = step(b + lDir);
            L.lockRateIndex = step(b + lLockRate);
        }
        s.retrigger = step(idRetrigger);
        for (int i = 0; i < numLfos; ++i)
        {
            auto &F = s.lfo[i];
            clap_id b = lfoBase(i);
            F.shape = step(b + fShape);
            F.mode = step(b + fMode);
            F.syncIndex = step(b + fSync);
            F.rateHz = std::exp2(val(b + fRate));
            F.phase = val(b + fPhase);
            F.smooth = val(b + fSmooth);
            F.unipolar = step(b + fPolarity) == 1;
            F.sway = val(b + fSway);
            F.target = step(b + fTarget);
            F.amount = val(b + fAmount);
            F.stepCount = step(b + fStepCount);
            F.glide = val(b + fGlide);
            F.quantize = step(b + fQuant);
            for (int k = 0; k < 16; ++k)
                F.step[k] = val(lfoStepBase(i) + (clap_id)k);
        }
        auto &E = s.env;
        E.source = step(idEnvSource);
        E.sensDb = val(idEnvSens);
        E.attackMs = val(idEnvAttack);
        E.releaseMs = val(idEnvRelease);
        E.scGainDb = val(idScGain);
        E.scHpHz = (float)semiToHz(val(idScHp));
        E.scLpHz = (float)semiToHz(val(idScLp));
        for (int k = 0; k < numSlots; ++k)
            s.slot[k] = {step(slotBase(k) + sSource), step(slotBase(k) + sTarget), val(slotBase(k) + sAmount), step(slotBase(k) + sSwing)};
        auto &T = s.tune;
        T.on = step(idTuneOn) != 0, T.root = step(idTuneRoot), T.scale = step(idTuneScale), T.beforeStrips = step(idTunePlace) == 0;
        T.amount = val(idTuneAmount), T.ringMs = std::exp2(val(idTuneRing)), T.lowest = step(idTuneLow), T.highest = step(idTuneHigh);
        T.tilt = val(idTuneTilt), T.detuneCents = 50.f * val(idTuneDetune), T.width = val(idTuneWidth), T.motion = step(idTuneMotion);
        T.motionHz = std::exp2(val(idTuneMotionRate));
        T.layer = step(idTuneMode) == 0;
        T.motionSync = step(idTuneMotionSync) == 1, T.motionDiv = step(idTuneMotionDiv);
        T.slope = val(idTuneSlope), T.pivotHz = (float)std::exp2(val(idTunePivot));
        if (on(idMidiKey) && midiRoot >= 0)
            T.root = midiRoot;
    }

    // a parameter's position across its range (for MIDI Learn)
    float pos01(int i) const
    {
        const auto &d = paramDefs()[(size_t)i];
        return d.max > d.min ? (float)((value[(size_t)i].load(std::memory_order_relaxed) - d.min) / (d.max - d.min)) : 0.f;
    }
    // MIDI Learn moved a parameter: store it and tell the host (so Bitwig and the window follow)
    void setFromMidi(int i, float p01)
    {
        const auto &d = paramDefs()[(size_t)i];
        double v = d.min + p01 * (d.max - d.min);
        if (isStepped(d.kind))
            v = std::round(v);
        value[(size_t)i].store((float)v, std::memory_order_relaxed);
        dirty.store(true, std::memory_order_relaxed);
        if (curOut)
        {
            clap_event_param_value_t ev{};
            ev.header = {sizeof(ev), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
            ev.param_id = d.id;
            ev.note_id = -1, ev.port_index = -1, ev.channel = -1, ev.key = -1;
            ev.value = v;
            curOut->try_push(curOut, &ev.header);
        }
    }
    std::atomic<float> midiWheel{0.f}, midiVelo{0.f};
    void handleMidi(const uint8_t *m)
    {
        const int status = m[0] & 0xf0;
        if (status == 0xb0 && m[1] == 1) // the mod wheel is a mod source (and can still be learnt)
            midiWheel.store((float)(m[2] & 0x7f) / 127.f, std::memory_order_relaxed), dirty.store(true, std::memory_order_relaxed);
        if (status == 0x90 && m[2] > 0) // the last note's velocity is a mod source
            midiVelo.store((float)(m[2] & 0x7f) / 127.f, std::memory_order_relaxed), dirty.store(true, std::memory_order_relaxed);
        if (status == 0xb0)
        {
            learn.feed(m, [this](int i) { return pos01(i); }, [this](int i, float p) { setFromMidi(i, p); });
            return;
        }
        const bool patterns = on(idMidiPatterns), key = on(idMidiKey);
        if (status == 0xc0)
        {
            if (patterns && m[1] < 24)
                midiPattern = m[1], dirty.store(true, std::memory_order_relaxed);
            return;
        }
        if (status != 0x90 && status != 0x80)
            return;
        const int note = m[1] & 0x7f;
        const bool down = status == 0x90 && m[2] > 0;
        // C1..B2 (36..59, Bitwig's C1 = MIDI 36) pick patterns when that is on; every other note sets the key
        if (patterns && note >= 36 && note < 60)
        {
            if (down)
                midiPattern = note - 36, dirty.store(true, std::memory_order_relaxed);
            return;
        }
        held[note] = down ? 1 : 0;
        if (!key)
            return;
        // the lowest held note is the key's root; letting go of everything keeps the last key
        for (int n = 0; n < 128; ++n)
            if (held[n])
            {
                if (midiRoot != n % 12)
                    midiRoot = n % 12, dirty.store(true, std::memory_order_relaxed);
                break;
            }
    }

    void handleEvent(const clap_event_header_t *h)
    {
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;
        if (h->type == CLAP_EVENT_PARAM_VALUE)
        {
            auto *e = (const clap_event_param_value_t *)h;
            auto it = paramIndex().find(e->param_id);
            if (it == paramIndex().end())
                return;
            const auto &d = paramDefs()[(size_t)it->second];
            value[(size_t)it->second].store((float)std::clamp(e->value, d.min, d.max), std::memory_order_relaxed);
            dirty.store(true, std::memory_order_relaxed);
            learn.unpick(it->second);
        }
        else if (h->type == CLAP_EVENT_MIDI)
            handleMidi(((const clap_event_midi_t *)h)->data);
        else if (h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF)
        {
            auto *e = (const clap_event_note_t *)h;
            uint8_t m[3] = {(uint8_t)((h->type == CLAP_EVENT_NOTE_ON ? 0x90 : 0x80) | (std::max<int16_t>(e->channel, 0) & 15)),
                            (uint8_t)std::clamp<int>(e->key, 0, 127), (uint8_t)std::clamp((int)std::lround(e->velocity * 127), 1, 127)};
            handleMidi(m);
        }
        else if (h->type == CLAP_EVENT_PARAM_MOD)
        {
            auto *e = (const clap_event_param_mod_t *)h;
            auto it = paramIndex().find(e->param_id);
            if (it == paramIndex().end() || e->note_id >= 0)
                return;
            mod[(size_t)it->second] = (float)e->amount;
            dirty.store(true, std::memory_order_relaxed);
        }
    }
};

Plugin *self(const clap_plugin_t *p) { return (Plugin *)p->plugin_data; }

// ----- text for values -------------------------------------------------------
std::string pitchText(double st)
{
    double hz = semiToHz(st);
    char b[64];
    if (hz >= 10000)
        snprintf(b, sizeof(b), "%.1f kHz", hz / 1000);
    else if (hz >= 1000)
        snprintf(b, sizeof(b), "%.2f kHz", hz / 1000);
    else if (hz >= 100)
        snprintf(b, sizeof(b), "%.0f Hz", hz);
    else
        snprintf(b, sizeof(b), "%.1f Hz", hz);
    return b;
}

// the Type parameter a filter Variant belongs to
static clap_id modelIdOf(clap_id variant)
{
    return variant == idFiltA + 1 ? idFiltA : variant == idFiltB + 1 ? idFiltB : idFiltModel;
}

std::string valueText(const Plugin *pl, const ParamDef &d, double v)
{
    char b[96];
    switch (d.kind)
    {
    case kPercent:
        snprintf(b, sizeof(b), d.max - d.min < 0.5 ? "%.1f %%" : "%.0f %%", v * 100); // Swing (50-75%) gets a decimal
        return b;
    case kReach: // how far, as a share of the most it can (Sway Depth, Chaperone Range)
        snprintf(b, sizeof(b), "%.0f %%", (v - d.min) / std::max(1e-9, d.max - d.min) * 100);
        return b;
    case kDb:
        if (d.min <= -60 && v <= -59.9)
            return "Off";
        snprintf(b, sizeof(b), "%+.1f dB", v);
        return b;
    case kPitch:
        return pitchText(v);
    case kToggle:
        return v >= 0.5 ? "On" : "Off";
    case kRouting:
        return v >= 0.5 ? "Series" : "Parallel";
    case kOversample:
    {
        int i = (int)std::lround(v);
        return i <= 0 ? "Off" : i == 1 ? "2x" : "4x";
    }
    case kOrder:
        return v >= 0.5 ? "Drive > Filter" : "Filter > Drive";
    case kDriveType:
        return driveName((int)std::lround(v));
    case kModel:
    {
        const auto &cat = filterCatalogue();
        return cat[(size_t)std::clamp((int)std::lround(v), 0, (int)cat.size() - 1)].name;
    }
    case kEnum:
        return d.labels[(size_t)std::clamp((int)std::lround(v), 0, (int)d.labels.size() - 1)];
    case kInt:
        snprintf(b, sizeof(b), "%d", (int)std::lround(v));
        return b;
    case kPan:
    {
        int pc = (int)std::lround(v * 100);
        if (pc == 0)
            return "C";
        snprintf(b, sizeof(b), "%s%d", pc < 0 ? "L" : "R", std::abs(pc));
        return b;
    }
    case kLean:
    {
        int pc = (int)std::lround(v * 100);
        if (pc == 50)
            return "Even";
        snprintf(b, sizeof(b), "%d%% to %s", pc < 50 ? 100 - pc : pc, pc < 50 ? "A" : "B");
        return b;
    }
    case kLogHz:
    {
        double hz = std::exp2(v);
        snprintf(b, sizeof(b), hz < 1 ? "%.3f Hz" : hz < 100 ? "%.2f Hz" : "%.0f Hz", hz);
        return b;
    }
    case kSemis:
        snprintf(b, sizeof(b), "%.1f st", v);
        return b;
    case kBipolar:
        snprintf(b, sizeof(b), "%+.0f%%", v * 100);
        return b;
    case kLock:
        if (std::fabs(v) < 0.01)
            return "off";
        snprintf(b, sizeof(b), "%+.0f%%", v * 100);
        return b;
    case kOffset:
        snprintf(b, sizeof(b), "%+.0f%% of a step", v * 100);
        return b;
    case kMs:
        snprintf(b, sizeof(b), v < 10 ? "%.1f ms" : "%.0f ms", v);
        return b;
    case kLogMs:
    {
        double ms = std::exp2(v);
        if (ms >= 1000)
            snprintf(b, sizeof(b), "%.2f s", ms / 1000);
        else
            snprintf(b, sizeof(b), "%.0f ms", ms);
        return b;
    }
    case kNote:
    {
        static const char *nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int m = (int)std::lround(v);
        snprintf(b, sizeof(b), "%s%d", nn[((m % 12) + 12) % 12], m / 12 - 1);
        return b;
    }
    case kRatio:
        snprintf(b, sizeof(b), "%.2fx", v);
        return b;
    case kPLock:
        if (v < 0)
            return "--";
        snprintf(b, sizeof(b), "%.0f%%", v * 100.0);
        return b;
    case kHzTone:
        return pitchText(v);
    case kTime:
    case kLeadTime:
    {
        // semitones of time: +12 doubles it. A and B count beats; Lead scales both.
        double r = std::exp2(v / 12.0);
        // with the part's Sync on, the note value it snaps to (1/4, 1/8D, 1/4T ...) or Lead's stretch
        if (pl && pl->step(d.id - oCutoff + oSync) == 1)
            return d.kind == kLeadTime ? nearestDiv(leadDivs, r).name : nearestDiv(noteDivs, r).name;
        // with Sync off, A and B show their time in ms or s at the tempo now
        if (pl && d.kind == kTime)
        {
            double sec = r * 60.0 / std::clamp(pl->engine.bpmNow(), 20.0, 400.0);
            if (sec < 1.0)
                snprintf(b, sizeof(b), sec < 0.1 ? "%.1f ms" : "%.0f ms", sec * 1000.0);
            else
                snprintf(b, sizeof(b), "%.2f s", sec);
            return b;
        }
        static const struct { double r; const char *t; } named[] = {{1. / 16, "1/16"}, {1. / 12, "1/12"}, {1. / 8, "1/8"}, {1. / 6, "1/6"},
            {1. / 4, "1/4"}, {1. / 3, "1/3"}, {3. / 8, "3/8"}, {1. / 2, "1/2"}, {2. / 3, "2/3"}, {3. / 4, "3/4"}, {1, "1"}, {1.5, "1.5"},
            {2, "2"}, {3, "3"}, {4, "4"}, {6, "6"}, {8, "8"}, {12, "12"}, {16, "16"}};
        for (auto &n : named)
            if (std::fabs(r / n.r - 1.0) < 0.004)
            {
                snprintf(b, sizeof(b), d.kind == kLeadTime ? "x%s" : "%s beat%s", n.t, n.r == 1 ? "" : "s");
                return b;
            }
        snprintf(b, sizeof(b), d.kind == kLeadTime ? "x%.3f" : "%.3f beats", r);
        return b;
    }
    case kBits:
        snprintf(b, sizeof(b), "%.1f bit", v);
        return b;
    case kTilt:
        if (std::fabs(v) < 0.05)
            return "Flat";
        snprintf(b, sizeof(b), "%+.1f dB %s", v, v > 0 ? "bright" : "dark");
        return b;
    case kVariant:
    {
        const auto &cat = filterCatalogue();
        int model = pl ? pl->step(modelIdOf(d.id)) : 0;
        model = std::clamp(model, 0, (int)cat.size() - 1);
        const auto &mi = cat[(size_t)model];
        if (mi.labels.empty())
            return "-";
        return mi.labels[(size_t)variantIndexFor(model, (float)v)];
    }
    }
    return "";
}

bool sameText(const std::string &a, const char *b)
{
    size_t n = std::strlen(b);
    if (a.size() != n)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

bool textValue(const Plugin *pl, const ParamDef &d, const char *text, double &out)
{
    // a named value first: try every step (or, for variants, every position)
    if (d.kind == kVariant)
    {
        const auto &cat = filterCatalogue();
        int model = std::clamp(pl ? pl->step(modelIdOf(d.id)) : 0, 0, (int)cat.size() - 1);
        const auto &mi = cat[(size_t)model];
        for (size_t i = 0; i < mi.labels.size(); ++i)
            if (sameText(mi.labels[i], text))
            {
                out = mi.labels.size() > 1 ? (double)i / (double)(mi.labels.size() - 1) : 0.0;
                return true;
            }
    }
    else if (isStepped(d.kind))
    {
        for (int i = (int)d.min; i <= (int)d.max; ++i)
            if (sameText(valueText(pl, d, i), text))
            {
                out = i;
                return true;
            }
    }
    else if (d.kind == kDb && sameText("Off", text) && d.min <= -60)
    {
        out = d.min;
        return true;
    }
    if (d.kind == kLock && sameText("off", text))
    {
        out = 0.0;
        return true;
    }
    if (d.kind == kPan || d.kind == kLean)
    {
        // "C", "L30", "R30", "Even", "70% to High"
        std::string t = text;
        for (auto &c : t)
            c = (char)std::tolower((unsigned char)c);
        if (t == "c" || t == "even")
        {
            out = d.kind == kPan ? 0.0 : 0.5;
            return true;
        }
        if (d.kind == kPan && (t[0] == 'l' || t[0] == 'r'))
        {
            double x = std::atof(t.c_str() + 1) / 100.0;
            out = std::clamp(t[0] == 'l' ? -x : x, -1.0, 1.0);
            return true;
        }
        if (d.kind == kLean)
        {
            double x = std::atof(t.c_str()) / 100.0;
            out = std::clamp(t.find("to a") != std::string::npos ? 1.0 - x : x, 0.0, 1.0);
            return true;
        }
    }
    if (d.kind == kPLock && std::string(text).find("--") != std::string::npos)
    {
        out = -1.0; // not locked
        return true;
    }
    if (d.kind == kTime || d.kind == kLeadTime)
    {
        // note values first ("1/4", "1/8D", "1/4T", "2 bars"), then "1.5 beats", "x2", "3/8 beats", "0.75"
        if (text[0] != 'x' && text[0] != 'X' && !std::strstr(text, "beat"))
            for (auto &n : noteDivs)
                if (sameText(n.name, text))
                {
                    out = std::clamp(12.0 * std::log2(n.beats), d.min, d.max);
                    return true;
                }
        const char *t = text;
        while (*t == ' ' || *t == 'x' || *t == 'X')
            ++t;
        char *e = nullptr;
        double r = std::strtod(t, &e);
        if (e == t || r <= 0)
            return false;
        // "500 ms" or "1.2 s": a time at the tempo now
        {
            const char *u = e;
            while (*u == ' ')
                ++u;
            bool ms = (u[0] == 'm' || u[0] == 'M') && (u[1] == 's' || u[1] == 'S'), sec = (u[0] == 's' || u[0] == 'S') && !std::isalpha((unsigned char)u[1]);
            if (ms || sec)
            {
                double bpm = pl ? std::clamp(pl->engine.bpmNow(), 20.0, 400.0) : 120.0;
                double beats = (ms ? r / 1000.0 : r) * bpm / 60.0;
                out = std::clamp(12.0 * std::log2(beats), d.min, d.max);
                return true;
            }
        }
        if (*e == '/')
        {
            double q = std::strtod(e + 1, nullptr);
            if (q <= 0)
                return false;
            r /= q;
        }
        out = std::clamp(12.0 * std::log2(r), d.min, d.max);
        return true;
    }
    if (d.kind == kVariant && std::string(text) == "-")
    {
        out = 0.0; // a filter with no variants
        return true;
    }
    if (d.kind == kTilt && sameText("Flat", text))
    {
        out = 0.0;
        return true;
    }
    if (d.kind == kLean && std::string(text).find("to A") != std::string::npos)
    {
        out = std::clamp(1.0 - std::atof(text) / 100.0, 0.0, 1.0);
        return true;
    }
    char *end = nullptr;
    double v = std::strtod(text, &end);
    if (end == text)
        return false;
    while (*end == ' ')
        ++end;
    switch (d.kind)
    {
    case kPercent:
        v /= 100.0;
        break;
    case kReach:
        v = d.min + v / 100.0 * (d.max - d.min);
        break;
    case kPitch:
    case kHzTone:
        if (*end == 'k' || *end == 'K')
            v *= 1000.0;
        if (v <= 0)
            return false;
        v = hzToSemi(v);
        break;
    case kLogHz:
        if (v <= 0)
            return false;
        v = std::log2(v);
        break;
    case kLogMs:
        if (*end == 's' || *end == 'S')
            v *= 1000.0;
        if (v <= 0)
            return false;
        v = std::log2(v);
        break;
    case kPan:
    case kBipolar:
    case kLock:
    case kOffset:
    case kPLock:
        v /= 100.0;
        break;
    default:
        break;
    }
    out = std::clamp(v, d.min, d.max);
    return true;
}

// ----- params extension --------------------------------------------------------
uint32_t paramsCount(const clap_plugin_t *) { return (uint32_t)paramDefs().size(); }

bool paramsInfo(const clap_plugin_t *, uint32_t index, clap_param_info_t *info)
{
    const auto &defs = paramDefs();
    if (index >= defs.size())
        return false;
    const auto &d = defs[index];
    std::memset(info, 0, sizeof(*info));
    info->id = d.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (isStepped(d.kind))
    {
        info->flags |= CLAP_PARAM_IS_STEPPED;
        if (d.kind != kToggle && d.kind != kInt)
            info->flags |= CLAP_PARAM_IS_ENUM;
    }
    else if (d.kind != kVariant)
        info->flags |= CLAP_PARAM_IS_MODULATABLE;
    if (d.id == idDegrade) // v0.8: retired (it only worked in the 16-bit memory v0.5 removed); kept so old projects load
        info->flags = CLAP_PARAM_IS_HIDDEN;
    info->cookie = nullptr;
    snprintf(info->name, sizeof(info->name), "%s", d.name.c_str());
    snprintf(info->module, sizeof(info->module), "%s", d.module.c_str());
    info->min_value = d.min;
    info->max_value = d.max;
    info->default_value = d.def;
    return true;
}

bool paramsValue(const clap_plugin_t *p, clap_id id, double *out)
{
    auto it = paramIndex().find(id);
    if (it == paramIndex().end())
        return false;
    *out = self(p)->value[(size_t)it->second].load(std::memory_order_relaxed);
    return true;
}

bool paramsValueToText(const clap_plugin_t *p, clap_id id, double v, char *out, uint32_t cap)
{
    auto it = paramIndex().find(id);
    if (it == paramIndex().end() || cap == 0)
        return false;
    snprintf(out, cap, "%s", valueText(self(p), paramDefs()[(size_t)it->second], v).c_str());
    return true;
}

bool paramsTextToValue(const clap_plugin_t *p, clap_id id, const char *text, double *out)
{
    auto it = paramIndex().find(id);
    if (it == paramIndex().end() || !text)
        return false;
    return textValue(self(p), paramDefs()[(size_t)it->second], text, *out);
}

void paramsFlush(const clap_plugin_t *p, const clap_input_events_t *in, const clap_output_events_t *out)
{
    auto *pl = self(p);
    pl->sendGuiEvents(out);
    uint32_t n = in ? in->size(in) : 0;
    for (uint32_t i = 0; i < n; ++i)
        pl->handleEvent(in->get(in, i));
}

const clap_plugin_params_t extParams = {paramsCount, paramsInfo, paramsValue, paramsValueToText, paramsTextToValue, paramsFlush};

// ----- state extension: plain text, "id value" per line, so old projects keep loading --------
// "<lfo> <n> x y bend x y bend ..." -> that LFO's drawn curve. Points are kept in order, start at
// x = 0, end at x = 1, and number 2 to 32.
bool parseCurveLine(Plugin *pl, const char *t)
{
    char *e = nullptr;
    long lfo = std::strtol(t, &e, 10);
    if (e == t || lfo < 0 || lfo >= numLfos)
        return false;
    t = e;
    long n = std::strtol(t, &e, 10);
    if (e == t || n < 2 || n > maxCurvePoints)
        return false;
    t = e;
    std::vector<CurvePoint> pts;
    for (long i = 0; i < n; ++i)
    {
        float v[3];
        for (auto &x : v)
        {
            x = std::strtof(t, &e);
            if (e == t || !std::isfinite(x))
                return false;
            t = e;
        }
        pts.push_back({std::clamp(v[0], 0.f, 1.f), std::clamp(v[1], -1.f, 1.f), std::clamp(v[2], 0.05f, 20.f)});
    }
    std::stable_sort(pts.begin(), pts.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
    pts.front().x = 0.f;
    pts.back().x = 1.f;
    pl->curve[lfo] = pts;
    return true;
}

static std::string stateText(Plugin *pl, bool withView);
bool stateSave(const clap_plugin_t *p, const clap_ostream_t *stream)
{
    std::string s = stateText(self(p), true);
    size_t done = 0;
    while (done < s.size())
    {
        int64_t w = stream->write(stream, s.data() + done, s.size() - done);
        if (w <= 0)
            return false;
        done += (size_t)w;
    }
    return true;
}
// a theme name from the window or a project: letters only, short
static std::string oneLine(const std::string &in, size_t cap) // no line breaks or control characters, at most cap bytes
{
    std::string o;
    for (char c : in)
        if ((unsigned char)c >= 32 && o.size() < cap)
            o += c;
    return o;
}
static std::string cleanThemeName(const std::string &in)
{
    std::string o;
    for (char c : in)
        if (std::isalnum((unsigned char)c) && o.size() < 24) // v0.6: digits too (EVA-01 was saved as "eva")
            o += c;
    return o.empty() ? std::string("porch") : o;
}
static bool themeIsDark(const std::string &n)
{
    // each theme has a light and a dark side: "<name>flip" is the other side of a theme's own look
    bool flip = n.size() > 4 && n.compare(n.size() - 4, 4, "flip") == 0;
    std::string base = flip ? n.substr(0, n.size() - 4) : n;
    static const char *darkOnes[] = {"dark", "porchdark", "deep", "session", "blackwater", "jax", "prince", "persona", "panther", "storm", "static",
                                     "miles", "eva01", "eva02", "thanos", "spawn"};
    bool d = false;
    for (const char *x : darkOnes)
        d = d || base == x;
    return d != flip;
}
static std::string midiMapText(Plugin *pl)
{
    return pl->learn.save([](int i) { return paramDefs()[(size_t)i].id; });
}
static bool midiMapLine(Plugin *pl, const char *line)
{
    return pl->learn.loadLine(line, [](unsigned id) { auto it = paramIndex().find((clap_id)id); return it == paramIndex().end() ? -1 : it->second; });
}
// the default set of mappings (Settings > MIDI > Save as default): a text file beside the preferences
static std::string midiDefaultPath()
{
    std::string p = prefsPath();
    if (p.empty())
        return p;
    return (std::filesystem::u8path(p).parent_path() / "MIDI Default.txt").string();
}
static void loadMidiDefault(Plugin *pl)
{
    std::string p = midiDefaultPath();
    if (p.empty())
        return;
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("M ", 0) == 0)
            midiMapLine(pl, line.c_str() + 2);
}
static bool saveMidiDefault(Plugin *pl)
{
    std::string p = midiDefaultPath();
    if (p.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(p).parent_path(), ec);
    std::ofstream f(p);
    f << midiMapText(pl);
    return (bool)f;
}
// every setting as text; withView adds the window's theme and size (projects yes, presets no)
static std::string stateText(Plugin *pl, bool withView)
{
    std::string s = "GULLAY 3\n";
    const auto &d = paramDefs();
    char b[64];
    for (size_t i = 0; i < d.size(); ++i)
    {
        snprintf(b, sizeof(b), "%u %.9g\n", (unsigned)d[i].id, (double)pl->value[i].load(std::memory_order_relaxed));
        s += b;
    }
    if (withView)
    {
        s += "T " + pl->themeName + "\n";
        if (!pl->presetLabel.empty())
            s += "N " + pl->presetLabel + "\n";
        if (!pl->uiState.empty())
            s += "U " + pl->uiState + "\n";
        s += "W " + std::to_string(pl->guiW) + " " + std::to_string(pl->guiH) + "\n";
        // MIDI Learn: saved with the project (a preset leaves your mappings alone)
        s += "MM\n" + midiMapText(pl);
    }
    // the drawn LFO curves: "C <lfo> <points> x y bend ..." (older versions skip these lines)
    for (int i = 0; i < numLfos; ++i)
    {
        snprintf(b, sizeof(b), "C %d %d", i, (int)pl->curve[i].size());
        s += b;
        for (auto &c : pl->curve[i])
        {
            snprintf(b, sizeof(b), " %.6g %.6g %.6g", c.x, c.y, c.t);
            s += b;
        }
        s += "\n";
    }
    // the snapshots: "SN <k> <id>:<value> ..."
    for (int k = 0; k < Plugin::numSnaps; ++k)
        if (pl->snapValid[k].load())
        {
            s += "SN " + std::to_string(k + 1);
            const auto &ids = Plugin::snapIds();
            for (size_t i = 0; i < ids.size() && i < 64; ++i)
            {
                snprintf(b, sizeof(b), " %u:%.9g", (unsigned)ids[i], (double)pl->snapVal[(size_t)k * 64 + i].load());
                s += b;
            }
            s += "\n";
        }
    // the tapped Tapo pattern (presets keep it too): "TP <n> <time> ..."
    if (!pl->tapped.empty())
    {
        s += "TP " + std::to_string(pl->tapped.size());
        for (float t : pl->tapped)
        {
            snprintf(b, sizeof(b), " %.1f", (double)t);
            s += b;
        }
        s += "\n";
    }
    return s;
}

// ----- your own presets: one text file each, in the settings folder's Presets folder (not in projects)
namespace fs = std::filesystem;
// a path as UTF-8 text (C++20's u8string is a different string type)
static std::string pathText(const fs::path &p)
{
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
static fs::path defaultPresetDir()
{
    std::string p = prefsPath();
    if (p.empty())
        return fs::path();
    return fs::u8path(p).parent_path() / "Presets";
}
// where your presets live: the folder you chose in Settings, or the standard place
static fs::path userPresetDir()
{
    Prefs pr = loadPrefs();
    if (!pr.presetDir.empty())
        return fs::u8path(pr.presetDir);
    return defaultPresetDir();
}
std::string jsonEsc(const std::string &in);
// MIDI profiles: a whole set of mappings under a name, kept in the presets folder's "MIDI Profiles"
static fs::path midiProfileDir() { return userPresetDir() / "MIDI Profiles"; }
static std::string cleanName(std::string n)
{
    std::string o;
    for (char c : n)
        if (c != '/' && c != '\\' && c != ':' && (unsigned char)c >= 32)
            o += c;
    while (!o.empty() && (o.back() == ' ' || o.back() == '.'))
        o.pop_back();
    return o.substr(0, 64);
}
static std::string midiProfilesJson()
{
    std::vector<std::string> names;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(midiProfileDir(), ec))
        if (e.path().extension() == ".gymidi")
            { auto u = e.path().stem().u8string(); names.push_back(std::string(u.begin(), u.end())); }
    std::sort(names.begin(), names.end());
    std::string o = "[";
    for (size_t i = 0; i < names.size(); ++i)
        o += (i ? ",\"" : "\"") + jsonEsc(names[i]) + "\"";
    return o + "]";
}
// a name that is safe as a file name on Mac and Windows
static std::string cleanPresetName(std::string n)
{
    std::string o;
    for (char c : n)
        if (!std::strchr("/\\:*?\"<>|", c) && (unsigned char)c >= 32)
            o += c;
    while (!o.empty() && (o.back() == ' ' || o.back() == '.'))
        o.pop_back();
    while (!o.empty() && o.front() == ' ')
        o.erase(o.begin());
    if (o.size() > 60)
        o.resize(60);
    return o;
}
static std::vector<std::string> userPresetNames()
{
    std::vector<std::string> r;
    std::error_code ec;
    fs::path d = userPresetDir();
    if (d.empty() || !fs::is_directory(d, ec))
        return r;
    for (auto &e : fs::directory_iterator(d, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".gypreset")
            r.push_back(pathText(e.path().stem()));
    std::sort(r.begin(), r.end());
    return r;
}
static bool saveUserPreset(Plugin *pl, const std::string &name)
{
    std::error_code ec;
    fs::path d = userPresetDir();
    if (d.empty() || name.empty())
        return false;
    fs::create_directories(d, ec);
    std::ofstream f(d / fs::u8path(name + ".gypreset"), std::ios::binary);
    f << stateText(pl, false);
    return (bool)f;
}
static bool loadStateText(Plugin *pl, const std::string &s);
static bool loadUserPreset(Plugin *pl, const std::string &name)
{
    std::ifstream f(userPresetDir() / fs::u8path(name + ".gypreset"), std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    if (!loadStateText(pl, ss.str()))
        return false;
    pl->snapTakePreset();
    if (pl->hostParams && pl->hostParams->rescan)
        pl->hostParams->rescan(pl->host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    pl->markDirty();
    return true;
}
static void deleteUserPreset(const std::string &name)
{
    std::error_code ec;
    fs::remove(userPresetDir() / fs::u8path(name + ".gypreset"), ec);
}

static bool loadStateText(Plugin *pl, const std::string &s);
bool stateLoad(const clap_plugin_t *p, const clap_istream_t *stream)
{
    auto *pl = self(p);
    std::string s;
    char buf[4096];
    for (;;)
    {
        int64_t r = stream->read(stream, buf, sizeof(buf));
        if (r < 0)
            return false;
        if (r == 0)
            break;
        s.append(buf, (size_t)r);
        if (s.size() > (1u << 20))
            return false;
    }
    for (auto &v : pl->snapValid) // a project brings its own snapshots (or none)
        v.store(false);
    return loadStateText(pl, s);
}
// v0.5: settings that changed meaning. 16-bit memory is gone (Crush in the Loop does it, better); Hymn's Gate and
// Decay shapes become the Envelope; and in states from before v0.5 the mod sources that were the old sequencer
// lanes and the Follower (they read as nothing in v0.4) are cleared, as those slots now hold new sources.
static void migrateState(Plugin *pl, bool older)
{
    auto get = [&](clap_id id) -> float { auto it = paramIndex().find(id); return it == paramIndex().end() ? 0.f : pl->value[(size_t)it->second].load(std::memory_order_relaxed); };
    auto put = [&](clap_id id, float v) { auto it = paramIndex().find(id); if (it != paramIndex().end()) { const auto &pd = paramDefs()[(size_t)it->second]; pl->value[(size_t)it->second].store((float)std::clamp((double)v, pd.min, pd.max), std::memory_order_relaxed); } };
    if (std::lround(get(idDldMemory)) == 1)
    {
        put(idDldMemory, 0);
        if (get(idCrushMix) < 0.05f)
            put(idBits, get(idDegrade)), put(idCrushPlace, 0 /* Loop */), put(idCrushMix, 1.f);
    }
    const int shape = (int)std::lround(get(idHymnShape));
    if (shape == 1 || shape == 2)
    {
        const float dec = get(idHymnDecay);
        put(idHymnShape, 3);
        put(idHymnAttack, std::log2(shape == 1 ? 2.f : 1.f));
        put(idHymnSustain, shape == 1 ? 1.f : 0.f);
        put(idHymnRelease, dec);
    }
    if (older)
        for (int k = 0; k < numSlots; ++k)
        {
            const int src = (int)std::lround(get(slotBase(k) + sSource));
            if (src == 1 || src == 2 || src == 3 || src == 7)
                put(slotBase(k) + sSource, 0);
        }
}
static bool loadStateText(Plugin *pl, const std::string &s)
{
    if (s.rfind("GULLAY ", 0) != 0)
        return false;
    const auto &d = paramDefs();
    // anything the saved state does not mention goes back to its starting value
    for (size_t i = 0; i < d.size(); ++i)
        pl->value[i].store((float)d[i].def, std::memory_order_relaxed);
    for (int i = 0; i < numLfos; ++i)
        pl->curve[i] = Plugin::defaultCurve();
    size_t pos = s.find('\n');
    while (pos != std::string::npos && pos + 1 < s.size())
    {
        size_t next = s.find('\n', pos + 1);
        std::string line = s.substr(pos + 1, next == std::string::npos ? std::string::npos : next - pos - 1);
        unsigned id = 0;
        double v = 0;
        if (line == "MM")
            pl->learn.clear();
        else if (line.rfind("M ", 0) == 0)
            midiMapLine(pl, line.c_str() + 2);
        else if (!line.empty() && line[0] == 'C')
            parseCurveLine(pl, line.c_str() + 1);
        else if (line.rfind("SN ", 0) == 0)
        {
            std::istringstream is(line.substr(3));
            int k = 0;
            is >> k;
            if (k >= 1 && k <= Plugin::numSnaps)
            {
                const auto &ids = Plugin::snapIds();
                for (size_t i = 0; i < ids.size() && i < 64; ++i) // missing entries keep the knob's value now
                    pl->snapVal[(size_t)(k - 1) * 64 + i].store(pl->baseOf(pl->snapIdx[i]));
                std::string tok;
                while (is >> tok)
                {
                    unsigned id = 0;
                    double v = 0;
                    if (sscanf(tok.c_str(), "%u:%lf", &id, &v) == 2)
                        for (size_t i = 0; i < ids.size() && i < 64; ++i)
                            if (ids[i] == id)
                                pl->snapVal[(size_t)(k - 1) * 64 + i].store((float)v);
                }
                pl->snapValid[k - 1].store(true);
            }
        }
        else if (line.rfind("TP ", 0) == 0)
        {
            std::istringstream is(line.substr(3));
            int n = 0;
            is >> n;
            std::vector<float> t;
            float x;
            while ((int)t.size() < std::min(n, 32) && is >> x)
                t.push_back(x);
            if (!t.empty())
                pl->tapped = t, pl->applyTapped();
        }
        else if (line.rfind("T ", 0) == 0)
            pl->themeName = cleanThemeName(line.substr(2)), pl->darkTheme.store(themeIsDark(pl->themeName));
        else if (line.rfind("N ", 0) == 0)
            pl->presetLabel = oneLine(line.substr(2), 80);
        else if (line.rfind("U ", 0) == 0)
            pl->uiState = oneLine(line.substr(2), 2000);
        else if (line.rfind("W ", 0) == 0)
        {
            unsigned w = 0, h = 0;
            if (std::sscanf(line.c_str() + 2, "%u %u", &w, &h) == 2 && w >= 640 && w <= 3840 && h >= 470 && h <= 2820)
                pl->guiW = w, pl->guiH = h;
        }
        else if (sscanf(line.c_str(), "%u %lf", &id, &v) == 2 && std::isfinite(v))
        {
            auto it = paramIndex().find((clap_id)id);
            if (it != paramIndex().end())
            {
                const auto &pd = d[(size_t)it->second];
                pl->value[(size_t)it->second].store((float)std::clamp(v, pd.min, pd.max), std::memory_order_relaxed);
            }
        }
        pos = next;
    }
    migrateState(pl, s.rfind("GULLAY 2", 0) == 0 || s.rfind("GULLAY 1", 0) == 0);
    for (int i = 0; i < numLfos; ++i)
        pl->applyCurve(i);
    pl->dirty.store(true, std::memory_order_relaxed);
    if (pl->bridge)
        pl->guiNeedsInit();
    return true;
}

const clap_plugin_state_t extState = {stateSave, stateLoad};

// Gullay's factory presets (gy_presets.h)
static const std::vector<TcPreset> &presetList()
{
    static const std::vector<TcPreset> &all = *new std::vector<TcPreset>(gullayPresets([](const char *n) { return modelIndexByName(n); }));
    return all;
}

// A factory preset as state text: every control back to its start, then the preset's values.
// "<name>#idx" gives the filter variant by number (turned into the knob's 0..1 for its model).
static std::string presetText(size_t index)
{
    const auto &ps = presetList();
    std::string t = "GULLAY 3\n";
    if (index >= ps.size())
        return t;
    const auto &d = paramDefs();
    auto idOf = [&](const std::string &n) -> int {
        for (size_t i = 0; i < d.size(); ++i)
            if (d[i].name == n)
                return (int)i;
        return -1;
    };
    std::map<std::string, float> got;
    for (auto &kv : ps[index].values)
        got[kv.first] = kv.second;
    char b[64];
    for (auto &kv : ps[index].values)
    {
        std::string n = kv.first;
        float v = kv.second;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, "#idx") == 0)
        {
            n.resize(n.size() - 4);
            std::string tn = n.substr(0, n.size() - 7) + "Type"; // "<path> Filter Variant" -> "<path> Filter Type"
            int model = got.count(tn) ? (int)got[tn] : 0;
            const auto &cat = filterCatalogue();
            int nv = model > 0 && model < (int)cat.size() ? (int)cat[(size_t)model].labels.size() : 1;
            v = nv > 1 ? std::clamp(v, 0.f, (float)(nv - 1)) / (float)(nv - 1) : 0.f;
        }
        int i = idOf(n);
        if (i < 0)
        {
            if (std::getenv("GY_PRESET_CHECK"))
                std::fprintf(stderr, "preset %s: no parameter named %s\n", ps[index].name, n.c_str());
            continue;
        }
        snprintf(b, sizeof(b), "%u %.9g\n", (unsigned)d[(size_t)i].id, (double)v);
        t += b;
    }
    return t;
}
// Loads a factory preset (from the window): values, curves, then tells Bitwig to re-read everything
static bool loadPreset(Plugin *pl, size_t index)
{
    if (!loadStateText(pl, presetText(index)))
        return false;
    pl->presetLabel = presetList()[index].name;
    pl->snapTakePreset();
    if (pl->hostParams && pl->hostParams->rescan)
        pl->hostParams->rescan(pl->host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    pl->markDirty();
    return true;
}

// ----- audio ports: stereo in, stereo out ---------------------------------------
// stereo in, a stereo sidechain in (for the follower), stereo out
uint32_t portsCount(const clap_plugin_t *, bool isInput) { return isInput ? 2 : 1; }
bool portsGet(const clap_plugin_t *, uint32_t index, bool isInput, clap_audio_port_info_t *info)
{
    if (index >= (isInput ? 2u : 1u))
        return false;
    std::memset(info, 0, sizeof(*info));
    if (isInput && index == 1)
    {
        info->id = 2;
        snprintf(info->name, sizeof(info->name), "%s", "Sidechain");
        info->flags = 0;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }
    info->id = isInput ? 0 : 1;
    snprintf(info->name, sizeof(info->name), "%s", isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t extPorts = {portsCount, portsGet};
// one MIDI / note input: the key for Hymn, pattern picks, and MIDI Learn controllers
uint32_t notePortsCount(const clap_plugin_t *, bool isInput) { return isInput ? 1 : 0; }
bool notePortsGet(const clap_plugin_t *, uint32_t index, bool isInput, clap_note_port_info_t *info)
{
    if (!isInput || index != 0)
        return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "%s", "MIDI in");
    return true;
}
const clap_plugin_note_ports_t extNotePorts = {notePortsCount, notePortsGet};

// ----- the plugin itself ------------------------------------------------------------
bool plInit(const clap_plugin_t *p)
{
    auto *pl = self(p);
    if (pl->host && pl->host->get_extension)
    {
        pl->hostParams = (const clap_host_params_t *)pl->host->get_extension(pl->host, CLAP_EXT_PARAMS);
        pl->hostState = (const clap_host_state_t *)pl->host->get_extension(pl->host, CLAP_EXT_STATE);
    }
    for (int i = 0; i < numLfos; ++i)
        pl->applyCurve(i);
    loadMidiDefault(pl); // a project's own mappings replace these when it loads
    return true;
}
void plDestroy(const clap_plugin_t *p)
{
    auto *pl = self(p);
    if (pl->gui)
        tcgui::destroy(pl->gui);
    pl->deleteBridge();
    delete pl;
}
// the spectral split delays the sound by one FFT frame (2048 samples); the host is told so it can line it up
static int wantedLatency(Plugin *pl)
{
    auto v = [&](clap_id id) { auto it = paramIndex().find(id); return it == paramIndex().end() ? 0.0 : pl->value[(size_t)it->second].load(std::memory_order_relaxed); };
    // Gullay: every path runs through the 64-sample Tapo block, the limiter looks 32 samples ahead, plus the spectral frame when that split is on
    return 64 + 32 + ((std::lround(v(idRouting)) == 0 && std::lround(v(idSplitMode)) == 1) ? (1024 << std::clamp((int)std::lround(v(idSpecQuality)), 0, 3)) : 0);
}
uint32_t latencyGet(const clap_plugin_t *p) { return (uint32_t)self(p)->latencyReported.load(); }
const clap_plugin_latency_t extLatency = {latencyGet};
bool plActivate(const clap_plugin_t *p, double sr, uint32_t, uint32_t maxFrames)
{
    auto *pl = self(p);
    pl->latencyReported.store(wantedLatency(pl));
    pl->restartAsked.store(false);
    pl->sampleRate = sr;
    pl->maxBlock = maxFrames;
    pl->engine.setSampleRate(sr);
    size_t n = std::max<uint32_t>(maxFrames, 64);
    pl->bufL.assign(n, 0.f);
    pl->bufR.assign(n, 0.f);
    pl->scBufL.assign(n, 0.f);
    pl->scBufR.assign(n, 0.f);
    pl->dirty.store(true, std::memory_order_relaxed);
    return true;
}
void plDeactivate(const clap_plugin_t *) {}
bool plStart(const clap_plugin_t *) { return true; }
void plStop(const clap_plugin_t *) {}
void plReset(const clap_plugin_t *p)
{
    auto *pl = self(p);
    pl->engine.reset();
    std::fill(pl->mod.begin(), pl->mod.end(), 0.f);
    pl->dirty.store(true, std::memory_order_relaxed);
}

clap_process_status plProcess(const clap_plugin_t *p, const clap_process_t *proc)
{
    auto *pl = self(p);
    ScopedNoDenormals noDenormals;
    // the spectral split was switched on or off (or its quality changed): ask the host to restart us so it hears the new delay
    if (wantedLatency(pl) != pl->latencyReported.load() && !pl->restartAsked.exchange(true) && pl->host && pl->host->request_restart)
        pl->host->request_restart(pl->host);
    const uint32_t frames = proc->frames_count;
    const float *inL = nullptr, *inR = nullptr;
    if (proc->audio_inputs_count > 0 && proc->audio_inputs[0].data32 && proc->audio_inputs[0].channel_count > 0)
    {
        inL = proc->audio_inputs[0].data32[0];
        inR = proc->audio_inputs[0].channel_count > 1 ? proc->audio_inputs[0].data32[1] : inL;
    }
    float *outL = nullptr, *outR = nullptr;
    if (proc->audio_outputs_count > 0 && proc->audio_outputs[0].data32 && proc->audio_outputs[0].channel_count > 0)
    {
        outL = proc->audio_outputs[0].data32[0];
        outR = proc->audio_outputs[0].channel_count > 1 ? proc->audio_outputs[0].data32[1] : nullptr;
    }

    // Bitwig's tempo and song position: Sway sync, the sixteenth-step trigger and Mutate follow them
    if (const auto *t = proc->transport)
    {
        HostTime ht;
        ht.playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        if (t->flags & CLAP_TRANSPORT_HAS_TEMPO)
            ht.bpm = t->tempo;
        if (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)
        {
            ht.beatsValid = true;
            ht.beats = (double)t->song_pos_beats / (double)CLAP_BEATTIME_FACTOR;
        }
        if (t->flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE)
        {
            ht.num = t->tsig_num;
            ht.den = t->tsig_denom;
        }
        pl->engine.setHostTime(ht);
    }
    pl->sendGuiEvents(proc->out_events);
    pl->curOut = proc->out_events;
    const float *scL = nullptr, *scR = nullptr;
    if (proc->audio_inputs_count > 1 && proc->audio_inputs[1].data32 && proc->audio_inputs[1].channel_count > 0)
    {
        scL = proc->audio_inputs[1].data32[0];
        scR = proc->audio_inputs[1].channel_count > 1 ? proc->audio_inputs[1].data32[1] : scL;
    }
    const uint32_t nEvents = proc->in_events ? proc->in_events->size(proc->in_events) : 0;
    uint32_t ev = 0, pos = 0;
    const uint32_t cap = (uint32_t)pl->bufL.size();
    while (pos < frames)
    {
        // take in every change due at or before this point, then run up to the next one
        while (ev < nEvents)
        {
            const clap_event_header_t *h = proc->in_events->get(proc->in_events, ev);
            if (h->time > pos)
                break;
            pl->handleEvent(h);
            ++ev;
        }
        uint32_t until = frames;
        if (ev < nEvents)
            until = std::min(until, proc->in_events->get(proc->in_events, ev)->time);
        uint32_t n = std::min(std::max(until, pos + 1) - pos, cap);
        n = std::min(n, frames - pos);
        pl->runSnapSeq();
        if (pl->dirty.exchange(false, std::memory_order_relaxed))
            pl->buildSettings();
        for (uint32_t i = 0; i < n; ++i)
        {
            pl->bufL[i] = inL ? inL[pos + i] : 0.f;
            pl->bufR[i] = inR ? inR[pos + i] : 0.f;
        }
        if (scL)
            for (uint32_t i = 0; i < n; ++i)
                pl->scBufL[i] = scL[pos + i], pl->scBufR[i] = scR[pos + i];
        pl->engine.process(pl->bufL.data(), pl->bufR.data(), pl->bufL.data(), pl->bufR.data(), (int)n, pl->settings,
                           scL ? pl->scBufL.data() : nullptr, scL ? pl->scBufR.data() : nullptr);
        if (outL && outR)
        {
            std::memcpy(outL + pos, pl->bufL.data(), n * sizeof(float));
            std::memcpy(outR + pos, pl->bufR.data(), n * sizeof(float));
        }
        else if (outL)
            for (uint32_t i = 0; i < n; ++i)
                outL[pos + i] = 0.5f * (pl->bufL[i] + pl->bufR[i]);
        pos += n;
    }
    while (ev < nEvents)
        pl->handleEvent(proc->in_events->get(proc->in_events, ev++));
    pl->curOut = nullptr;
    pl->readMeters();
    return CLAP_PROCESS_CONTINUE;
}

// ----- the window's bridge ---------------------------------------------------------------
std::string jsonEsc(const std::string &in);
static std::string presetDirJson()
{
    return std::string("{\"path\":\"") + jsonEsc(pathText(userPresetDir())) + "\",\"custom\":" + (loadPrefs().presetDir.empty() ? "false" : "true") + "}";
}
static std::string userPresetJson()
{
    std::string o = "[";
    auto names = userPresetNames();
    for (size_t i = 0; i < names.size(); ++i)
        o += (i ? ",\"" : "\"") + jsonEsc(names[i]) + "\"";
    return o + "]";
}
std::string jsonEsc(const std::string &in)
{
    std::string o;
    o.reserve(in.size() + 2);
    for (unsigned char c : in)
    {
        if (c == '"' || c == '\\')
            o += '\\', o += (char)c;
        else if (c < 0x20)
        {
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", c);
            o += b;
        }
        else if (c == '<')
            o += "\\u003c"; // never let a name close a script tag
        else
            o += (char)c;
    }
    return o;
}
std::string num(double v)
{
    if (!std::isfinite(v))
        return "0";
    char b[32];
    snprintf(b, sizeof(b), "%.6g", v);
    return b;
}

// A Snapshot: everything about the sound right now, as plain text you can send with your notes.
// Written to the settings folder's Snapshots folder; returns the file's path ("" if it could not be written).
static std::string writeSnapshot(Plugin *pl, const std::string &note)
{
    std::string base = prefsPath();
    if (base.empty())
        return "";
    std::error_code ec;
    auto dir = std::filesystem::u8path(base).parent_path() / "Snapshots";
    std::filesystem::create_directories(dir, ec);
    std::time_t t = std::time(nullptr);
    char stamp[64];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H%M%S", std::localtime(&t));
    auto path = dir / (std::string("Gullay Snapshot ") + stamp + ".txt");
    std::ofstream f(path);
    if (!f)
        return "";
    f << "Gullay " << pluginVersion() << " snapshot, " << stamp << "\n";
    f << "Note: " << (note.empty() ? "(none)" : note) << "\n";
    f << "Sample rate: " << pl->sampleRate << " Hz, latency " << pl->engine.latencySamples() << " samples, wiring "
      << (pl->engine.activeWiring() == 0 ? "Parallel" : pl->engine.activeWiring() == 1 ? "Series" : "Spectral")
      << ", Juba pattern " << pl->engine.patternNow() + 1 << ", MIDI key root " << pl->midiRoot << "\n";
    f << "Part seconds (A, Lead, B): " << pl->engine.partSeconds(0) << ", " << pl->engine.partSeconds(1) << ", " << pl->engine.partSeconds(2) << "\n\n";
    f << "--- Changed from the defaults\n";
    const auto &d = paramDefs();
    for (size_t i = 0; i < d.size(); ++i)
    {
        double v = pl->value[i].load(std::memory_order_relaxed);
        if (std::fabs(v - d[i].def) > 1e-6)
            f << d[i].name << " = " << valueText(pl, d[i], v) << "  (" << v << ")\n";
    }
    f << "\n--- MIDI mappings\n" << midiMapText(pl);
    f << "\n--- Full state (paste back into a project via a preset file)\n" << stateText(pl, false);
    return pathText(path);
}

struct Plugin::GuiBridge : tcgui::Bridge
{
    Plugin *pl;
    bool needInit{true};
    std::vector<double> lastV;
    std::vector<std::string> lastT;
    std::string out; // kept for the test hook
    int view{1};     // what the top display shows: 0 curves only, 1 spectrum, 2 scope
    float specHold[4][96]{};

    explicit GuiBridge(Plugin *p) : pl(p)
    {
        for (auto &r : specHold)
            for (auto &v : r)
                v = -120.f;
    }

    // radix-2 FFT in place (n a power of two)
    static void fft(std::vector<float> &re, std::vector<float> &im)
    {
        const size_t n = re.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(re[i], re[j]), std::swap(im[i], im[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            double a = -2.0 * 3.14159265358979 / (double)len;
            float wr = (float)std::cos(a), wi = (float)std::sin(a);
            for (size_t i = 0; i < n; i += len)
            {
                float cr = 1.f, ci = 0.f;
                for (size_t k = 0; k < len / 2; ++k)
                {
                    size_t u = i + k, v = u + len / 2;
                    float tr = re[v] * cr - im[v] * ci, ti = re[v] * ci + im[v] * cr;
                    re[v] = re[u] - tr, im[v] = im[u] - ti;
                    re[u] += tr, im[u] += ti;
                    float ncr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr, cr = ncr;
                }
            }
        }
    }
    // spectrum of the three strips and the output: 96 points from 20 Hz to 20 kHz, in dB
    std::string spectrum()
    {
        const int N = 2048;
        std::vector<float> buf(N), re(N), im(N);
        double sr = pl->sampleRate > 0 ? pl->sampleRate : 48000.0;
        std::string o = "[";
        for (int w = 0; w < 4; ++w)
        {
            pl->engine.readAnalyser(w, buf.data(), N);
            for (int i = 0; i < N; ++i)
            {
                float hann = 0.5f - 0.5f * (float)std::cos(2.0 * 3.14159265358979 * i / (N - 1));
                re[i] = buf[i] * hann, im[i] = 0.f;
            }
            fft(re, im);
            o += w ? ",[" : "[";
            for (int k = 0; k < 96; ++k)
            {
                double f0 = 20.0 * std::pow(1000.0, k / 96.0), f1 = 20.0 * std::pow(1000.0, (k + 1) / 96.0);
                int b0 = std::max(1, (int)std::floor(f0 * N / sr)), b1 = std::max(b0, (int)std::ceil(f1 * N / sr));
                float pk = 0.f;
                for (int bi = b0; bi <= b1 && bi < N / 2; ++bi)
                    pk = std::max(pk, re[bi] * re[bi] + im[bi] * im[bi]);
                // a full-scale sine reads about 0 dB (Hann window: sum = N / 2)
                float db = 10.f * std::log10(pk + 1e-20f) - 20.f * std::log10((float)N / 4.f);
                float &h = specHold[w][k];
                h = db > h ? db : std::max(db, h - 2.5f); // falls back slowly
                o += (k ? "," : "") + std::to_string((int)std::lround(std::clamp(h, -120.f, 12.f)));
            }
            o += "]";
        }
        return o + "]";
    }
    // the three strips and the output as waveforms, steadied on a rising zero crossing of the output
    std::string scope()
    {
        const int N = 4096, W = 1920, P = 240;
        std::vector<float> b[4];
        for (int w = 0; w < 4; ++w)
            b[w].resize(N), pl->engine.readAnalyser(w, b[w].data(), N);
        int start = N - W;
        for (int i = N - W - 1; i > N - W - 1800 && i > 1; --i)
            if (b[3][i - 1] < 0.f && b[3][i] >= 0.f)
            {
                start = i;
                break;
            }
        std::string o = "[";
        for (int w = 0; w < 4; ++w)
        {
            o += w ? ",[" : "[";
            for (int k = 0; k < P; ++k)
            {
                // the largest swing in each slice, with its sign, so peaks are not lost
                int i0 = start + k * W / P, i1 = start + (k + 1) * W / P;
                float best = 0.f;
                for (int i = i0; i < i1 && i < N; ++i)
                    if (std::fabs(b[w][i]) > std::fabs(best))
                        best = b[w][i];
                char t[16];
                snprintf(t, sizeof(t), "%.3f", std::clamp(best, -2.f, 2.f));
                o += (k ? "," : "") + std::string(t);
            }
            o += "]";
        }
        return o + "]";
    }
    std::string pendingAfterInit; // messages queued before an init: sent right after it
    // the Tapo taps now playing, per slot: [[sec, level], ...] (empty when the slot is not Tapo)
    // a preset's values into a snapshot: its own value where it sets one, the default where it doesn't
    void snapFromText(int k, const std::string &text)
    {
        const auto &ids = Plugin::snapIds();
        for (size_t i = 0; i < ids.size() && i < 64; ++i)
            pl->snapVal[(size_t)k * 64 + i].store((float)paramDefs()[(size_t)pl->snapIdx[i]].def);
        std::istringstream is(text);
        std::string line;
        while (std::getline(is, line))
        {
            unsigned id = 0;
            double v = 0;
            char extra = 0;
            if (sscanf(line.c_str(), "%u %lf%c", &id, &v, &extra) >= 2)
                for (size_t i = 0; i < ids.size() && i < 64; ++i)
                    if (ids[i] == id)
                        pl->snapVal[(size_t)k * 64 + i].store((float)v);
        }
        pl->snapValid[k].store(true);
    }
    std::string snapValidJson()
    {
        std::string o = "[";
        for (int k = 0; k < Plugin::numSnaps; ++k)
            o += (k ? "," : "") + std::string(pl->snapValid[k].load() ? "1" : "0");
        return o + "]";
    }
    std::string tapsJson()
    {
        std::string o = "[";
        for (int sl = 0; sl < 2; ++sl)
        {
            int ty = sl == 0 ? pl->step(idSlot1) : pl->step(idSlot2) - 1;
            o += sl ? ",[" : "[";
            if (ty == 1)
            {
                float sec[32], vel[32];
                int n = pl->engine.tapsNow(sl, sec, vel, 32);
                for (int t = 0; t < n; ++t)
                {
                    char b[48];
                    std::snprintf(b, sizeof(b), "%s[%.4f,%.3f]", t ? "," : "", sec[t], vel[t]);
                    o += b;
                }
            }
            o += "]";
        }
        return o + "]";
    }
    // the water line: peak levels of the last ~20 ms of input, output, A and B
    std::vector<float> lvlBuf = std::vector<float>(1024);
    std::string levels()
    {
        std::string o = "[";
        const int which[4] = {1, 3, 0, 2};
        for (int k = 0; k < 4; ++k)
        {
            pl->engine.readAnalyser(which[k], lvlBuf.data(), (int)lvlBuf.size());
            float pk = 0.f;
            for (float v : lvlBuf)
                pk = std::max(pk, std::fabs(v));
            char t[24];
            std::snprintf(t, sizeof(t), "%s%.4f", k ? "," : "", std::min(pk, 4.f));
            o += t;
        }
        return o + "]";
    }
    const char *html() override { return gyGuiHtml; }
    std::string mapsJson()
    {
        std::string o = "[";
        bool first = true;
        for (int i = 0; i < GyMidiLearn::kMax; ++i)
        {
            MidiMap mm = pl->learn.get(i);
            if (!mm.valid)
                continue;
            const auto &d = paramDefs()[(size_t)mm.param];
            char b[256];
            std::snprintf(b, sizeof(b), "%s{\"slot\":%d,\"ch\":%d,\"num\":%d,\"kind\":%d,\"beh\":%d,\"pickup\":%d,\"id\":%u,\"lo\":%.4f,\"hi\":%.4f,\"name\":\"",
                          first ? "" : ",", i, mm.ch, mm.num, mm.kind, mm.beh, mm.pickup ? 1 : 0, (unsigned)d.id, mm.lo, mm.hi);
            o += b + jsonEsc(d.name) + "\"}";
            first = false;
        }
        return o + "]";
    }

    void setValue(int i, double v)
    {
        const auto &d = paramDefs()[(size_t)i];
        if (!std::isfinite(v))
            return;
        v = std::clamp(v, d.min, d.max);
        if (isStepped(d.kind))
            v = std::round(v);
        pl->value[(size_t)i].store((float)v, std::memory_order_relaxed);
        pl->dirty.store(true, std::memory_order_relaxed);
        pl->push(0, d.id, v);
        pl->requestFlush();
    }

    void onMessage(const char *msg) override
    {
        if (!msg)
            return;
        std::string m = msg;
        char cmd[16] = {0};
        unsigned id = 0;
        double v = 0;
        if (m == "ready")
        {
            needInit = true;
            return;
        }
        if (m.rfind("zoom ", 0) == 0)
        {
            // a size step from the window's - / + buttons: ask the host for that size (same shape as always)
            double z = std::clamp(std::atof(m.c_str() + 5), 0.5, 2.0);
            uint32_t w = (uint32_t)std::lround(1280 * z * pl->guiScale), h = (uint32_t)std::lround(940 * z * pl->guiScale);
            auto *hg = pl->host && pl->host->get_extension ? (const clap_host_gui_t *)pl->host->get_extension(pl->host, CLAP_EXT_GUI) : nullptr;
            if (hg && hg->request_resize && hg->request_resize(pl->host, w, h))
            {
                pl->guiW = (uint32_t)std::lround(1280 * z), pl->guiH = (uint32_t)std::lround(940 * z);
                if (pl->gui)
                    tcgui::setSize(pl->gui, (int)w, (int)h);
                pl->markDirty();
            }
            return;
        }
        if (m.rfind("prefs ", 0) == 0) // "prefs <size %> <dark 0/1> <tips 0/1>": this computer's defaults
        {
            Prefs p;
            int sz = 100, dk = 0, tp = 1;
            if (std::sscanf(m.c_str() + 6, "%d %d %d", &sz, &dk, &tp) == 3)
            {
                p = loadPrefs(); // keep the preset folder
                p.sizePct = std::clamp(sz, 60, 200), p.dark = dk != 0, p.tips = tp != 0;
                savePrefs(p);
            }
            return;
        }
        if (m.rfind("pname ", 0) == 0 || m.rfind("uistate ", 0) == 0)
        {
            bool nm = m[0] == 'p';
            std::string v = oneLine(m.substr(nm ? 6 : 8), nm ? 80 : 2000);
            std::string &dst = nm ? pl->presetLabel : pl->uiState;
            if (v != dst)
                dst = v, pl->markDirty();
            return;
        }
        if (m.rfind("theme ", 0) == 0)
        {
            pl->themeName = cleanThemeName(m.substr(6));
            pl->darkTheme.store(themeIsDark(pl->themeName));
            pl->markDirty();
            return;
        }
        if (m.rfind("ctx ", 0) == 0) // the window asks for Bitwig's menu items for a knob
        {
            clap_id id = (clap_id)std::strtoul(m.c_str() + 4, nullptr, 10);
            MenuCollect c;
            if (auto *hm = hostMenu(); hm && hm->populate)
            {
                clap_context_menu_target_t t{CLAP_CONTEXT_MENU_TARGET_KIND_PARAM, id};
                clap_context_menu_builder_t b{&c, menuAdd, menuSupports};
                hm->populate(pl->host, &t, &b);
            }
            pendingJs += "tc.ctx(" + std::to_string(id) + ",[" + c.json + "]);";
            return;
        }
        if (m.rfind("ctxdo ", 0) == 0) // ... and runs the one picked
        {
            unsigned long id = 0, action = 0;
            if (std::sscanf(m.c_str() + 6, "%lu %lu", &id, &action) == 2)
                if (auto *hm = hostMenu(); hm && hm->perform)
                {
                    clap_context_menu_target_t t{CLAP_CONTEXT_MENU_TARGET_KIND_PARAM, (clap_id)id};
                    hm->perform(pl->host, &t, (clap_id)action);
                }
            return;
        }
        if (m == "ufolder pick")
        {
            pickFolder = true; // done from poll(): a system dialog should not open inside the web view's message callback
            return;
        }
        if (m == "ufolder default" || m == "ufolder show")
        {
            if (m == "ufolder default")
            {
                Prefs p = loadPrefs();
                p.presetDir.clear();
                savePrefs(p);
                pendingJs += "tc.user(" + userPresetJson() + "," + presetDirJson() + ");";
            }
            else
            {
                std::error_code ec;
                fs::create_directories(userPresetDir(), ec);
                tcgui::reveal(pathText(userPresetDir()));
            }
            return;
        }
        if (m.rfind("usave ", 0) == 0 || m.rfind("uload ", 0) == 0 || m.rfind("udel ", 0) == 0) // your own presets
        {
            const bool save = m[1] == 's', load = m[1] == 'l';
            std::string name = cleanPresetName(m.substr(m.find(' ') + 1));
            if (name.empty())
                return;
            if (save)
                saveUserPreset(pl, name);
            else if (load)
                loadUserPreset(pl, name);
            else
                deleteUserPreset(name);
            if (!load)
                pendingJs += "tc.user(" + userPresetJson() + "," + presetDirJson() + ");";
            return;
        }
        // ----- snapshots: "snap store|clear|recall <1-8>", "snap preset <1-8> <index>", "snap user <1-8> <name>"
        if (m.rfind("snap ", 0) == 0)
        {
            char what[16] = {};
            int k = 0, used = 0;
            if (sscanf(m.c_str() + 5, "%15s %d %n", what, &k, &used) >= 2 && k >= 1 && k <= Plugin::numSnaps)
            {
                std::string w = what, rest = used > 0 ? m.substr(5 + (size_t)used) : std::string();
                if (w == "store")
                    pl->snapStore(k - 1), pl->snapLoaded.store(k - 1);
                else if (w == "clear")
                {
                    pl->snapValid[k - 1].store(false);
                    if (pl->snapLoaded.load() == k - 1)
                        pl->snapLoaded.store(-1);
                }
                else if (w == "recall" && pl->snapValid[k - 1].load())
                {
                    const auto &ids = Plugin::snapIds();
                    for (size_t i = 0; i < ids.size() && i < 64; ++i)
                        setValue(pl->snapIdx[i], pl->snapVal[(size_t)(k - 1) * 64 + i].load());
                    pl->snapLoaded.store(k - 1);
                }
                else if (w == "preset" || w == "user") // a preset becomes a snapshot (the knobs stay as they are)
                {
                    std::string text;
                    if (w == "preset")
                    {
                        long idx = std::strtol(rest.c_str(), nullptr, 10);
                        if (idx >= 0 && (size_t)idx < presetList().size())
                            text = presetText((size_t)idx);
                    }
                    else
                    {
                        std::ifstream f(userPresetDir() / fs::u8path(rest + ".gypreset"), std::ios::binary);
                        std::stringstream ss;
                        ss << f.rdbuf();
                        text = ss.str();
                    }
                    if (!text.empty())
                        snapFromText(k - 1, text);
                }
                pl->markDirty();
            }
            return;
        }
        // ----- every Juba pattern's taps (time as a share of its length, loudness), for the pattern cards
        if (m == "pats")
        {
            std::string o = "[";
            for (int i = 0; i < 25; ++i)
            {
                const auto &pt = pl->engine.pattern(i);
                double len = 0;
                for (int t = 0; t < pt.size; ++t)
                    len = std::max(len, (double)pt.time[t]);
                o += i ? ",[" : "[";
                for (int t = 0; t < pt.size && t < 32; ++t)
                {
                    char b[48];
                    std::snprintf(b, sizeof(b), "%s[%.3f,%.2f]", t ? "," : "", len > 0 ? pt.time[t] / len : 0.0, (double)pt.velo[t]);
                    o += b;
                }
                o += "]";
            }
            pendingJs += "S.pats=" + o + "];if(S.drawPats)S.drawPats();";
            return;
        }
        // ----- a pattern tapped on the Tapo pad: "taps <ms> <ms> ..." (each tap after the first click)
        if (m.rfind("taps ", 0) == 0)
        {
            std::vector<float> ms;
            const char *c = m.c_str() + 5;
            char *e = nullptr;
            for (double v = std::strtod(c, &e); e != c && ms.size() < 32; c = e, v = std::strtod(c, &e))
                if (v > 1.0 && v < 20000.0)
                    ms.push_back((float)v);
            if (!ms.empty())
            {
                // at the tempo now, so it plays back as tapped; it follows the tempo from here
                double bpm = std::clamp(pl->engine.bpmNow(), 20.0, 400.0);
                pl->tapped.clear();
                for (float x : ms)
                    pl->tapped.push_back((float)(x * 0.4 * bpm));
                pl->applyTapped();
                pl->markDirty();
            }
            return;
        }
        // ----- MIDI Learn
        if (m.rfind("learn ", 0) == 0) // "learn <param id> <kind>" arms it; "learn off"
        {
            unsigned pid = 0;
            int kind = 0;
            if (m == "learn off")
                pl->learn.disarm();
            else if (std::sscanf(m.c_str() + 6, "%u %d", &pid, &kind) >= 1)
            {
                auto it = paramIndex().find((clap_id)pid);
                if (it != paramIndex().end())
                    pl->learn.lastLearned.store(-1), pl->learn.arm(it->second, std::clamp(kind, 0, numMidiKinds - 1));
            }
            return;
        }
        if (m == "maps")
        {
            pendingJs += "tc.maps(" + mapsJson() + ");";
            return;
        }
        if (m.rfind("mapset ", 0) == 0) // "mapset <slot> <ch> <num> <kind> <behaviour> <pickup> <lo> <hi>"
        {
            int slot, ch, num, kind, beh, pk;
            float lo, hi;
            if (std::sscanf(m.c_str() + 7, "%d %d %d %d %d %d %f %f", &slot, &ch, &num, &kind, &beh, &pk, &lo, &hi) == 8 && slot >= 0 &&
                slot < GyMidiLearn::kMax)
            {
                MidiMap mm = pl->learn.get(slot);
                if (mm.valid)
                {
                    mm.ch = std::clamp(ch, 0, 15), mm.num = std::clamp(num, 0, 16383), mm.kind = std::clamp(kind, 0, numMidiKinds - 1);
                    mm.beh = std::clamp(beh, 0, 2), mm.pickup = pk != 0, mm.lo = std::clamp(lo, 0.f, 1.f), mm.hi = std::clamp(hi, 0.f, 1.f);
                    pl->learn.set(slot, mm);
                    pl->markDirty();
                }
            }
            pendingJs += "tc.maps(" + mapsJson() + ");";
            return;
        }
        if (m.rfind("mapdel ", 0) == 0)
        {
            int slot = std::atoi(m.c_str() + 7);
            if (slot < 0)
                pl->learn.clear();
            else
                pl->learn.remove(slot);
            pl->markDirty();
            pendingJs += "tc.maps(" + mapsJson() + ");";
            return;
        }
        if (m.rfind("mprofile ", 0) == 0) // MIDI profiles: list / save <name> / load <name> / delete <name>
        {
            std::string rest = m.substr(9), verb = rest.substr(0, rest.find(' ')), name = cleanName(rest.size() > verb.size() ? rest.substr(verb.size() + 1) : std::string());
            std::error_code ec;
            const fs::path f = midiProfileDir() / fs::u8path(name + ".gymidi");
            std::string note;
            if (verb == "save" && !name.empty())
            {
                fs::create_directories(midiProfileDir(), ec);
                std::ofstream o(f);
                o << midiMapText(pl);
                note = o ? "Saved MIDI profile " + name : "Could not save " + name;
            }
            else if (verb == "load" && !name.empty())
            {
                std::ifstream in(f);
                if (in)
                {
                    pl->learn.clear();
                    std::string line;
                    while (std::getline(in, line))
                        if (line.rfind("M ", 0) == 0)
                            midiMapLine(pl, line.c_str() + 2);
                    pl->markDirty();
                    note = "Loaded MIDI profile " + name;
                }
                else
                    note = "Could not find " + name;
            }
            else if (verb == "delete" && !name.empty())
                fs::remove(f, ec), note = "Deleted " + name;
            pendingJs += "S.midiProfiles=" + midiProfilesJson() + ";tc.maps(" + mapsJson() + ");" + (note.empty() ? std::string() : "tc.note(\"" + jsonEsc(note) + "\");");
            return;
        }
        if (m.rfind("open ", 0) == 0) // the About page's link: only Otis Kanno's own page opens from here
        {
            const std::string url = m.substr(5);
            if (url == "https://beacons.ai/otiskanno")
                tcgui::openWeb(url);
            return;
        }
        if (m == "mapdefault save" || m == "mapdefault load")
        {
            bool ok = true;
            if (m == "mapdefault save")
                ok = saveMidiDefault(pl);
            else
                pl->learn.clear(), loadMidiDefault(pl), pl->markDirty();
            pendingJs += "tc.maps(" + mapsJson() + ");tc.note(" + std::string(ok ? "\"Saved\"" : "\"Could not save\"") + ");";
            return;
        }
        if (m.rfind("snapshot", 0) == 0) // "snapshot <your note>": a text file of everything, for feedback
        {
            std::string path = writeSnapshot(pl, m.size() > 9 ? m.substr(9) : std::string());
            pendingJs += "tc.snapshot(\"" + jsonEsc(path) + "\");";
            if (!path.empty())
                tcgui::reveal(path);
            return;
        }
        if (m.rfind("preset ", 0) == 0)
        {
            int k = std::atoi(m.c_str() + 7);
            if (k >= 0)
                loadPreset(pl, (size_t)k);
            return;
        }
        if (m.rfind("view ", 0) == 0)
        {
            view = std::clamp(std::atoi(m.c_str() + 5), 0, 2);
            return;
        }
        if (m.rfind("curve ", 0) == 0)
        {
            int lfo = std::atoi(m.c_str() + 6);
            if (parseCurveLine(pl, m.c_str() + 6) && lfo >= 0 && lfo < numLfos)
            {
                pl->applyCurve(lfo);
                pl->markDirty();
            }
            return;
        }
        int got = sscanf(m.c_str(), "%15s %u %lf", cmd, &id, &v);
        if (got < 2)
            return;
        auto it = paramIndex().find((clap_id)id);
        if (it == paramIndex().end())
            return;
        std::string c = cmd;
        if (c == "set" && got == 3)
            setValue(it->second, v);
        else if (c == "reset")
            setValue(it->second, paramDefs()[(size_t)it->second].def);
        else if (c == "begin")
            pl->push(1, (clap_id)id, 0), pl->requestFlush();
        else if (c == "end")
            pl->push(2, (clap_id)id, 0), pl->requestFlush();
    }

    // Bitwig's own menu for a knob (automation, modulators ...), fetched through CLAP's context-menu
    // extension and shown inside our own right-click menu
    std::string pendingJs;
    bool pickFolder{false}, inPicker{false};
    const clap_host_context_menu_t *hostMenu()
    {
        if (!pl->host || !pl->host->get_extension)
            return nullptr;
        auto *m = (const clap_host_context_menu_t *)pl->host->get_extension(pl->host, CLAP_EXT_CONTEXT_MENU);
        if (!m)
            m = (const clap_host_context_menu_t *)pl->host->get_extension(pl->host, CLAP_EXT_CONTEXT_MENU_COMPAT);
        return m;
    }
    struct MenuCollect
    {
        std::string json;
        int n{0};
        std::string prefix;
    };
    static bool menuAdd(const clap_context_menu_builder_t *b, clap_context_menu_item_kind_t kind, const void *data)
    {
        auto *c = (MenuCollect *)b->ctx;
        auto item = [&](const char *label, bool enabled, long action, int type) {
            c->json += (c->n++ ? "," : "") + std::string("[\"") + jsonEsc(c->prefix + (label ? label : "")) + "\"," + std::to_string(action) + "," +
                       (enabled ? "1" : "0") + "," + std::to_string(type) + "]";
        };
        switch (kind)
        {
        case CLAP_CONTEXT_MENU_ITEM_ENTRY:
        {
            auto *e = (const clap_context_menu_entry_t *)data;
            item(e->label, e->is_enabled, (long)e->action_id, 0);
            break;
        }
        case CLAP_CONTEXT_MENU_ITEM_CHECK_ENTRY:
        {
            auto *e = (const clap_context_menu_check_entry_t *)data;
            item(e->label, e->is_enabled, (long)e->action_id, e->is_checked ? 2 : 1);
            break;
        }
        case CLAP_CONTEXT_MENU_ITEM_SEPARATOR: item("", false, -1, 3); break;
        case CLAP_CONTEXT_MENU_ITEM_TITLE:
        {
            auto *t = (const clap_context_menu_item_title_t *)data;
            item(t->title, false, -1, 4);
            break;
        }
        case CLAP_CONTEXT_MENU_ITEM_BEGIN_SUBMENU:
        {
            auto *m = (const clap_context_menu_submenu_t *)data;
            c->prefix = std::string(m->label ? m->label : "") + " › ";
            break;
        }
        case CLAP_CONTEXT_MENU_ITEM_END_SUBMENU: c->prefix.clear(); break;
        default: break;
        }
        return true;
    }
    static bool menuSupports(const clap_context_menu_builder_t *, clap_context_menu_item_kind_t) { return true; }

    std::string poll() override
    {
        // the folder chooser for your presets (asked for by the page; opened here, from the window's timer)
        if (pickFolder && !inPicker)
        {
            pickFolder = false, inPicker = true;
            std::string chosen = tcgui::chooseFolder(pl->gui, pathText(userPresetDir()));
            inPicker = false;
            if (!chosen.empty())
            {
                Prefs p = loadPrefs();
                p.presetDir = chosen;
                savePrefs(p);
                pendingJs += "tc.user(" + userPresetJson() + "," + presetDirJson() + ");";
            }
        }
        if (inPicker)
            return std::string();
        const auto &d = paramDefs();
        if (lastV.size() != d.size())
            lastV.assign(d.size(), 1.0e300), lastT.assign(d.size(), std::string());
        std::string o;
        if (!pendingJs.empty())
            o += pendingJs, pendingJs.clear();
        if (needInit)
        {
            needInit = false;
            pendingAfterInit = o, o.clear();
            o = "tc.init({\"params\":[";
            for (size_t i = 0; i < d.size(); ++i)
            {
                double v = pl->value[i].load(std::memory_order_relaxed);
                std::string t = valueText(pl, d[i], v);
                lastV[i] = v, lastT[i] = t;
                if (i)
                    o += ",";
                o += "{\"id\":" + std::to_string(d[i].id) + ",\"n\":\"" + jsonEsc(d[i].name) + "\",\"min\":" + num(d[i].min) +
                     ",\"max\":" + num(d[i].max) + ",\"def\":" + num(d[i].def) + ",\"v\":" + num(v) + ",\"t\":\"" + jsonEsc(t) +
                     "\",\"st\":" + (isStepped(d[i].kind) ? "true" : "false");
                std::vector<std::string> labels = d[i].labels;
                if (labels.empty() && isStepped(d[i].kind) && d[i].kind != kInt && d[i].max - d[i].min <= 64)
                    for (int k = (int)d[i].min; k <= (int)d[i].max; ++k)
                        labels.push_back(valueText(pl, d[i], k)); // on/off, routing, models, drive types...
                if (!labels.empty())
                {
                    o += ",\"lab\":[";
                    for (size_t k = 0; k < labels.size(); ++k)
                        o += (k ? ",\"" : "\"") + jsonEsc(labels[k]) + "\"";
                    o += "]";
                }
                o += "}";
            }
            o += "],\"models\":[";
            const auto &cat = filterCatalogue();
            for (size_t m = 0; m < cat.size(); ++m)
            {
                o += (m ? ",{\"name\":\"" : "{\"name\":\"") + jsonEsc(cat[m].name) + "\",\"variants\":[";
                for (size_t k = 0; k < cat[m].labels.size(); ++k)
                    o += (k ? ",\"" : "\"") + jsonEsc(cat[m].labels[k]) + "\"";
                o += "]}";
            }
            o += "],\"zplane\":[";
            {
                int nz = 0;
                const ZDesign *zd = zDesigns(nz);
                auto fr = [&](const ZFrame &F) {
                    std::string r = "{\"f\":[";
                    for (int k = 0; k < zSections; ++k)
                        r += (k ? "," : "") + num(F.f[k]);
                    r += "],\"g\":[";
                    for (int k = 0; k < zSections; ++k)
                        r += (k ? "," : "") + num(F.g[k]);
                    r += "],\"q\":[";
                    for (int k = 0; k < zSections; ++k)
                        r += (k ? "," : "") + num(F.q[k]);
                    return r + "],\"lpF\":" + num(F.lpF) + ",\"lpQ\":" + num(F.lpQ) + "}";
                };
                for (int i = 0; i < nz; ++i)
                    o += (i ? "," : "") + std::string("{\"a\":") + fr(zd[i].a) + ",\"b\":" + fr(zd[i].b) + "}";
            }
            {
                Prefs pr = loadPrefs();
                const char *os =
#if defined(_WIN32)
                    "Windows";
#elif defined(__APPLE__)
                    "macOS";
#else
                    "Linux";
#endif
                o += std::string("],\"prefs\":{\"size\":") + std::to_string(pr.sizePct) + ",\"dark\":" + (pr.dark ? "1" : "0") + ",\"tips\":" + (pr.tips ? "1" : "0") + "}";
                o += std::string(",\"info\":{\"version\":\"") + jsonEsc(pluginVersion()) + "\",\"os\":\"" + os + "\",\"host\":\"" +
                     jsonEsc(pl->host && pl->host->name ? pl->host->name : "?") + "\",\"hostVersion\":\"" + jsonEsc(pl->host && pl->host->version ? pl->host->version : "") +
                     "\",\"block\":" + std::to_string(pl->maxBlock) + "}";
            }
            o += ",\"user\":" + userPresetJson() + ",\"presetDir\":" + presetDirJson();
            o += std::string(",\"sr\":") + num((float)pl->sampleRate) + std::string(",\"theme\":\"") + jsonEsc(pl->themeName) + "\",\"pname\":\"" + jsonEsc(pl->presetLabel) + "\",\"ui\":\"" + jsonEsc(pl->uiState) + "\",\"presets\":[";
            for (size_t i = 0; i < presetList().size(); ++i)
                o += (i ? ",[\"" : "[\"") + jsonEsc(presetList()[i].name) + "\",\"" + jsonEsc(presetList()[i].group) + "\",\"" + jsonEsc(presetList()[i].desc ? presetList()[i].desc : "") + "\"]";
            o += "],\"curves\":[";
            for (int i = 0; i < numLfos; ++i)
            {
                o += i ? ",[" : "[";
                for (size_t k = 0; k < pl->curve[i].size(); ++k)
                {
                    const auto &c = pl->curve[i][k];
                    o += (k ? "," : "") + num(c.x) + "," + num(c.y) + "," + num(c.t);
                }
                o += "]";
            }
            o += "]});";
            o += pendingAfterInit;
            pendingAfterInit.clear();
            return o;
        }
        // values that changed (from automation, Bitwig's own knobs, presets), with their text
        std::string vals;
        for (size_t i = 0; i < d.size(); ++i)
        {
            double v = pl->value[i].load(std::memory_order_relaxed);
            std::string t = valueText(pl, d[i], v);
            if (v != lastV[i] || t != lastT[i])
            {
                lastV[i] = v, lastT[i] = t;
                vals += (vals.empty() ? "\"" : ",\"") + std::to_string(d[i].id) + "\":[" + num(v) + ",\"" + jsonEsc(t) + "\"]";
            }
        }
        // the modulated values of the knobs that can move, for the rings
        int m = std::clamp(pl->step(idRouting), 0, 1);
        std::string mods;
        auto addMod = [&](clap_id id, float v) { mods += (mods.empty() ? "\"" : ",\"") + std::to_string(id) + "\":" + num(v); };
        const int bandOff[6] = {oCutoff, oReso, oMorph, oDrive, oLevel, oPan};
        for (int b = 0; b < 3; ++b)
            for (int q2 = 0; q2 < 6; ++q2)
                addMod(bandBase(m, b) + bandOff[q2], pl->meter[mBand + b * 6 + q2].load(std::memory_order_relaxed));
        addMod(idBalance, pl->meter[mGlobal + 1].load());
        addMod(idTame, pl->meter[mGlobal + 2].load());
        addMod(idCrushMix, pl->meter[mGlobal + 3].load());
        addMod(modeBase(m) + oMix, pl->meter[mGlobal + 4].load());
        addMod(idSwayAmount, pl->meter[mGlobal + 5].load());
        auto arr = [&](int from, int n)
        {
            std::string a = "[";
            for (int k = 0; k < n; ++k)
                a += (k ? "," : "") + num(pl->meter[from + k].load(std::memory_order_relaxed));
            return a + "]";
        };
        o += "tc.upd({\"v\":{" + vals + "},\"m\":{" + mods + "},\"lane\":" + arr(mLaneStep, 3) + ",\"lockLane\":" + arr(mLockStep, 3) + ",\"laneV\":" + arr(mLaneVal, 3) +
            ",\"lfo\":" + arr(mLfoVal, 3) + ",\"lfoPh\":" + arr(mLfoPhase, 3) + ",\"env\":" + num(pl->meter[mEnv].load()) +
            ",\"sc\":" + num(pl->meter[mSc].load()) + ",\"lim\":" + num(pl->meter[mLimiter].load()) + ",\"guard\":" + arr(mGuard, 3) +
            ",\"edge\":" + arr(mEdge, 3) + ",\"sway\":" + arr(mSway, 3) + ",\"ev\":" + num(pl->meter[mMutEvents].load()) +
            ",\"chap\":" + arr(mChap, 3) + ",\"hist\":" + arr(mHist, 48) + ",\"bank\":" + arr(mBank, 24) + ",\"bankHz\":" + arr(mBankHz, 24) +
            ",\"tunePos\":" + num(pl->meter[mTunePos].load()) + ",\"mutNow\":" + arr(mMutNow, 10) + ",\"mutK\":" + num(pl->meter[mMutK].load()) +
            ",\"mutFrac\":" + num(pl->meter[mMutFrac].load()) +
            ",\"gy\":{\"wiring\":" + num(pl->meter[mWiring].load()) + ",\"pattern\":" + num(pl->meter[mPattern].load()) +
            ",\"sec\":" + arr(mPartSec, 3) + ",\"runaway\":" + arr(mRunaway, 3) + ",\"river\":" + arr(mRiver, 3) + ",\"inf\":" + arr(mInf, 3) +
            ",\"lat\":" + num(pl->meter[mLatency].load()) + ",\"riverPos\":" + num(pl->meter[mRiverPos].load()) + ",\"kills\":" + num(pl->meter[mKills].load()) + ",\"runT\":" + num(pl->meter[mRunT].load()) +
            ",\"locked\":" + num(pl->meter[mChapLocked].load()) + ",\"snap\":{\"pos\":" + num(pl->meter[mSnapPos].load()) + ",\"cur\":" + num(pl->meter[mSnapCur].load()) + ",\"loaded\":" + std::to_string(pl->snapLoaded.load()) + ",\"valid\":" + snapValidJson() + "}" + ",\"taps\":" + tapsJson() + ",\"bpm\":" + num((float)pl->engine.bpmNow()) + ",\"split\":" + arr(mSpecSplit, 2) + ",\"vari\":" + arr(mVari, 3) +
            ",\"learn\":" + std::to_string(pl->learn.armed()) + ",\"learned\":" + std::to_string(pl->learn.lastLearned.load()) +
            ",\"key\":" + std::to_string(pl->midiRoot) + ",\"lvl\":" + levels() + "}" +
            (view == 1 ? ",\"spec\":" + spectrum() : view == 2 ? ",\"scope\":" + scope() : std::string()) + "});";
        out = o;
        return o;
    }
};

void Plugin::guiNeedsInit()
{
    if (bridge)
        bridge->needInit = true;
}
void Plugin::requestFlush()
{
    if (hostParams && hostParams->request_flush)
        hostParams->request_flush(host);
}
void Plugin::markDirty()
{
    if (hostState && hostState->mark_dirty)
        hostState->mark_dirty(host);
}
void Plugin::deleteBridge()
{
    delete bridge;
    bridge = nullptr;
}
Plugin::GuiBridge *Plugin::ensureBridge()
{
    if (!bridge)
        bridge = new GuiBridge(this);
    return bridge;
}

// ----- window (CLAP gui extension) ---------------------------------------------------------
static constexpr uint32_t baseW = 1280, baseH = 940;
bool guiIsApiSupported(const clap_plugin_t *, const char *api, bool floating)
{
#if defined(__APPLE__)
    return !floating && api && !std::strcmp(api, CLAP_WINDOW_API_COCOA);
#elif defined(_WIN32)
    return !floating && api && !std::strcmp(api, CLAP_WINDOW_API_WIN32);
#else
    (void)api, (void)floating;
    return false;
#endif
}
bool guiPreferredApi(const clap_plugin_t *, const char **api, bool *floating)
{
#if defined(__APPLE__)
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
#elif defined(_WIN32)
    *api = CLAP_WINDOW_API_WIN32;
    *floating = false;
    return true;
#else
    (void)api, (void)floating;
    return false;
#endif
}
bool guiCreate(const clap_plugin_t *p, const char *api, bool floating)
{
    if (!guiIsApiSupported(p, api, floating))
        return false;
    auto *pl = self(p);
    if (pl->gui)
        return true;
    auto *b = pl->ensureBridge();
    b->needInit = true;
    pl->gui = tcgui::create(b, (int)pl->guiW, (int)pl->guiH);
    return pl->gui != nullptr;
}
void guiDestroy(const clap_plugin_t *p)
{
    auto *pl = self(p);
    if (pl->gui)
        tcgui::destroy(pl->gui);
    pl->gui = nullptr;
}
#if defined(_WIN32)
// Windows hosts size plugin windows in pixels and tell us the screen's scale; the page itself fits whatever size it gets
bool guiSetScale(const clap_plugin_t *p, double scale)
{
    self(p)->guiScale = std::clamp(scale, 0.5, 4.0);
    return true;
}
#else
bool guiSetScale(const clap_plugin_t *, double) { return false; } // macOS works in points
#endif
bool guiGetSize(const clap_plugin_t *p, uint32_t *w, uint32_t *h)
{
    auto *pl = self(p);
    *w = (uint32_t)std::lround(pl->guiW * pl->guiScale), *h = (uint32_t)std::lround(pl->guiH * pl->guiScale);
    return true;
}
bool guiCanResize(const clap_plugin_t *) { return true; }
bool guiResizeHints(const clap_plugin_t *, clap_gui_resize_hints_t *h)
{
    h->can_resize_horizontally = h->can_resize_vertically = true;
    h->preserve_aspect_ratio = true; // the layout is drawn for one shape; keeping it means nothing is ever cut off
    h->aspect_ratio_width = baseW, h->aspect_ratio_height = baseH;
    return true;
}
bool guiAdjustSize(const clap_plugin_t *p, uint32_t *w, uint32_t *h)
{
    // free dragging, but always the designed shape (1280 x 940), from 50% to 200%: the window
    // scales as a whole, so nothing runs off the edge or gets squeezed at odd sizes
    double s = self(p)->guiScale, z = std::min((double)*w / (baseW * s), (double)*h / (baseH * s));
    z = std::clamp(z, 0.5, 2.0);
    *w = (uint32_t)std::lround(baseW * z * s), *h = (uint32_t)std::lround(baseH * z * s);
    return true;
}
bool guiSetSize(const clap_plugin_t *p, uint32_t w, uint32_t h)
{
    auto *pl = self(p);
    pl->guiW = (uint32_t)std::lround(w / pl->guiScale), pl->guiH = (uint32_t)std::lround(h / pl->guiScale);
    if (pl->gui)
        tcgui::setSize(pl->gui, (int)w, (int)h);
    return true;
}
bool guiSetParent(const clap_plugin_t *p, const clap_window_t *win)
{
    auto *pl = self(p);
#if defined(_WIN32)
    return pl->gui && win && tcgui::setParent(pl->gui, win->win32);
#else
    return pl->gui && win && tcgui::setParent(pl->gui, win->cocoa);
#endif
}
bool guiSetTransient(const clap_plugin_t *, const clap_window_t *) { return false; }
void guiSuggestTitle(const clap_plugin_t *, const char *) {}
bool guiShow(const clap_plugin_t *p)
{
    if (self(p)->gui)
        tcgui::show(self(p)->gui, true);
    return self(p)->gui != nullptr;
}
bool guiHide(const clap_plugin_t *p)
{
    if (self(p)->gui)
        tcgui::show(self(p)->gui, false);
    return self(p)->gui != nullptr;
}
const clap_plugin_gui_t extGui = {guiIsApiSupported, guiPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize,
                                  guiCanResize, guiResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
                                  guiSuggestTitle, guiShow, guiHide};

// ----- test hook: drives the window's bridge without a window (used by tests/clap_host_test) -----
struct TestBridge
{
    void (*send)(const clap_plugin_t *, const char *);
    const char *(*poll)(const clap_plugin_t *);
};
void tbSend(const clap_plugin_t *p, const char *m) { self(p)->ensureBridge()->onMessage(m); }
const char *tbPoll(const clap_plugin_t *p)
{
    auto *b = self(p)->ensureBridge();
    b->out = b->poll();
    return b->out.c_str();
}
const TestBridge extTestBridge = {tbSend, tbPoll};

const void *plExtension(const clap_plugin_t *, const char *id)
{
    if (!std::strcmp(id, CLAP_EXT_PARAMS))
        return &extParams;
    if (!std::strcmp(id, CLAP_EXT_STATE))
        return &extState;
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
        return &extPorts;
    if (!std::strcmp(id, CLAP_EXT_LATENCY))
        return &extLatency;
    if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
        return &extNotePorts;
    if (!std::strcmp(id, CLAP_EXT_GUI) && tcgui::available())
        return &extGui;
    if (!std::strcmp(id, "com.otiskanno.gullay/test-bridge"))
        return &extTestBridge;
    return nullptr;
}
void plMainThread(const clap_plugin_t *) {}

const char *const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY,
                                CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION_INIT,
    "com.otiskanno.gullay",
    "Gullay",
    "Otis Kanno",
    "",
    "",
    "",
    "0.7.0",
    "Two 4ms delay engines (Dual Looping Delay, Juba) moved by Three's Company's modulation, tuned by Hymn, with Plateau and Cloud Seed reverbs.",
    features,
};

// ----- factory and entry --------------------------------------------------------------
uint32_t factoryCount(const clap_plugin_factory_t *) { return 1; }
const clap_plugin_descriptor_t *factoryDescriptor(const clap_plugin_factory_t *, uint32_t i)
{
    return i == 0 ? &descriptor : nullptr;
}
const clap_plugin_t *factoryCreate(const clap_plugin_factory_t *, const clap_host_t *host, const char *id)
{
    if (!host || !clap_version_is_compatible(host->clap_version) || !id || std::strcmp(id, descriptor.id))
        return nullptr;
    auto *pl = new Plugin();
    pl->host = host;
    pl->clap.desc = &descriptor;
    pl->clap.plugin_data = pl;
    pl->clap.init = plInit;
    pl->clap.destroy = plDestroy;
    pl->clap.activate = plActivate;
    pl->clap.deactivate = plDeactivate;
    pl->clap.start_processing = plStart;
    pl->clap.stop_processing = plStop;
    pl->clap.reset = plReset;
    pl->clap.process = plProcess;
    pl->clap.get_extension = plExtension;
    pl->clap.on_main_thread = plMainThread;
    return &pl->clap;
}
const clap_plugin_factory_t factory = {factoryCount, factoryDescriptor, factoryCreate};

bool entryInit(const char *)
{
    (void)filterCatalogue();
    (void)paramDefs();
    (void)paramIndex();
    return true;
}
void entryDeinit() {}
const void *entryFactory(const char *id) { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &factory; }
} // namespace

// The CLAP entry. The CMake build (clap-wrapper: CLAP, VST3, AU) defines it in tc_entry.cpp instead, from these.
bool tcEntryInit(const char *path) { return entryInit(path); }
void tcEntryDeinit() { entryDeinit(); }
const void *tcEntryFactory(const char *id) { return entryFactory(id); }
#ifndef GY_SEPARATE_ENTRY
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entryInit, entryDeinit, entryFactory};
#endif
