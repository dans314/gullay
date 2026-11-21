// Gullay's factory presets: 20 in four groups (Front Porch, Ring Shout, Low Country, Deep Water).
// Each one names only what it changes; every other control starts from its default.
// Values are the parameters' own units (dB, semitones, 0..1, list positions), as the host sees them.
//
// Mod targets (list positions): 1-6 A Time/Feedback/Tone/Drive/Level/Pan, 7-12 Lead, 13-18 B,
// 19 Spectral Split, 20 Balance, 21 Runaway Tame, 22 Crusher Mix, 23 Mix, 35 Filter Cutoff, 36 Filter Reso,
// 37 Filter Morph, 38 Tilt In, 39 Tilt Out, 40 Tapo Pattern, 41 Tapo Morph, 42 DLD Degrade, 43 Crusher Bits,
// 44 Crusher Rate, 45-49 Reverb Size/Decay/Level/Mod/Send, 50-52 A/Lead/B Shift, 53 Runaway Ceiling.
// Mod sources: 1 Random, 2 Mod Wheel, 3 Velocity, 4-6 LFO 1-3, 7 Hymn Env, 8-10 River A/Lead/B.
// Juba patterns: 0-5 A1-A6, 6-11 B, 12-17 C, 18-23 D (bounce, dotted, clave, 3 vs 4, fibonacci, swell).
#pragma once
#include <deque>
#include <string>
#include <utility>
#include <vector>

struct TcPreset
{
    const char *name, *group;
    std::vector<std::pair<const char *, float>> values;
    const char *desc{""}; // one line: what it shows off (shown when it loads)
    // v0.8: snapshots the preset brings with it. Each one is the preset plus what it changes (an empty one is the preset
    // as it is); they fill snapshots 1, 2, ... in order. Presets without any leave the project's snapshots alone.
    std::vector<std::vector<std::pair<const char *, float>>> snaps{};
};

// filterModel: index of a filter by name, supplied by the plugin (the catalogue is built at run time)
template <class ModelOf>
inline std::vector<TcPreset> gullayPresets(ModelOf modelOf)
{
    const float zplane = (float)modelOf("Z-Plane"), ladder = (float)modelOf("Ladder"), svf = (float)modelOf("Fast SVF");
    using V = std::vector<std::pair<const char *, float>>;
    std::vector<TcPreset> p;
    auto add = [&](const char *n, const char *g, const char *d, V v) { p.push_back({n, g, std::move(v), d}); };
    // the presets written for v0.1 start from v0.1's defaults: B on a dotted beat, A and B panned a little,
    // feedback 45 %, Mix 50 % (v0.3's defaults are a plain quarter-note delay)
    auto addV1 = [&](const char *n, const char *g, const char *d, V v) {
        V base;
        for (const char *m : {"Par ", "Ser "})
        {
            auto has = [&](const std::string &k) { for (auto &x : v) if (k == x.first) return true; return false; };
            static std::deque<std::string> keep; // names must outlive the preset list (a deque never moves them)
            auto put = [&](const std::string &k, float val) { if (!has(k)) { keep.push_back(k); base.push_back({keep.back().c_str(), val}); } };
            put(std::string(m) + "B Time", 7.02f), put(std::string(m) + "A Pan", -0.35f), put(std::string(m) + "B Pan", 0.35f);
            put(std::string(m) + "A Feedback", 0.45f), put(std::string(m) + "B Feedback", 0.45f), put(std::string(m) + "Mix", 0.5f);
        }
        for (auto &x : v)
            base.push_back(x);
        add(n, g, d, std::move(base));
    };

    // Init: everything at its default
    add("Init", "Start", "Everything at its default: a plain quarter-note echo on A and B, 35 % mix. Start here to build your own.", {});


    // ===== Showcase (v0.5): each one plays at least two of the new features where you can hear them
    auto addS = [&](const char *n, const char *d, V v) { p.push_back({n, "Showcase", std::move(v), d}); };
    addS("Hymn Bloom", "Hymn's Envelope blooms on every hit (slow attack, long release) while the Arp wave walks up the ringing notes.",
        {{"Tune On", 1}, {"Tune Root", 9}, {"Tune Amount", 0.6f}, {"Hymn Shape", 3}, {"Hymn Attack", 8.814f}, {"Hymn Sustain", 0.7f}, {"Hymn Release", 10.551f},
         {"Tune Wave", 4}, {"Tune Wave Timing", 1}, {"Par Mix", 0.45f}, {"Par A Feedback", 0.45f}, {"Par B Feedback", 0.45f}});
    addS("Sampler Grit", "An old sampler in the loop: 8 bits at 11 kHz, the grit tilted into the highs and a hiss floor, so each repeat comes back dustier.",
        {{"Crusher Bits", 8}, {"Memory Rate", 13.428f}, {"Memory Tilt", 0.6f}, {"Memory Noise", 0.25f}, {"Crusher Place", 0}, {"Crusher Mix", 0.8f},
         {"Par A Feedback", 0.6f}, {"Par B Feedback", 0.55f}, {"Par Mix", 0.4f}});
    addS("River Wander", "The new Random source wanders Lead's filter while the River's Lead current breathes the feedback.",
        {{"Lead Filter Type", ladder}, {"Lead Filter Place", 1}, {"Lead Filter Cutoff", 34.f}, {"Lead Filter Reso", 0.35f}, {"Mod 1 Source", 1}, {"Mod 1 Target", 35}, {"Mod 1 Amount", 0.5f},
         {"Mod 2 Source", 9}, {"Mod 2 Target", 8}, {"Mod 2 Amount", 0.2f}, {"River Meander", 0.8f}, {"Par Mix", 0.42f}, {"Par A Feedback", 0.5f}, {"Par B Feedback", 0.5f}});
    addS("Pocket Shuffle", "Juba's clave with Black Herman passing moves on the 16ths and Legerdemain pulling A and B against each other.",
        {{"Engine 1", 1}, {"Juba Pattern", 20}, {"Black Herman Influence", 0.6f}, {"Black Herman Grid", 5}, {"Black Herman Chaos", 0.3f}, {"Legerdemain", 0.4f},
         {"Legerdemain Mischief", 0.3f}, {"Par Lead Feedback", 0.45f}, {"Par Mix", 0.42f}});
    addS("Choir Answers", "B repeats A on the beat while Call & Response turns their levels against each other, over a soft Hymn swell.",
        {{"Shadow", 1}, {"Answer After", 2}, {"Answer Chance", 0.85f}, {"Mutation A Answer", 1}, {"Mutation B Answer", 2}, {"Mutation B Moves", 3}, {"Mutate", 0.3f},
         {"Tune On", 1}, {"Tune Amount", 0.3f}, {"Tune Wave", 1}, {"Par Mix", 0.45f}, {"Par A Pan", -0.5f}, {"Par B Pan", 0.5f}});
    addS("Split Clave", "Split by Tonal: the held tones ring through Echo while the hits play Juba's clave.",
        {{"Engine 1", 0}, {"Engine 2", 2}, {"Parallel Split", 1}, {"Spectral Split By", 1}, {"Juba Pattern", 20}, {"Par Mix", 0.45f},
         {"Par A Feedback", 0.5f}, {"Par B Feedback", 0.4f}});
    addS("Bounce Pulse", "Juba's new bouncing-ball taps, with Hymn pulsing every ringing note together.",
        {{"Engine 1", 1}, {"Juba Pattern", 18}, {"Tune On", 1}, {"Tune Amount", 0.35f}, {"Tune Wave", 3}, {"Tune Wave Timing", 1},
         {"Par Lead Feedback", 0.4f}, {"Par Mix", 0.4f}});

    addS("Stutter Ghost", "Shadow's Stutter grabs a sixteenth of A's echoes and stammers it back in rhythm, while the Memory's grit is tilted into the highs.",
        {{"Shadow", 3}, {"Answer After", 0}, {"Answer Chance", 0.7f}, {"Mirror Time", 0.5f}, {"Par A Feedback", 0.45f}, {"Par A Time", -12.f},
         {"Crusher Bits", 10}, {"Crusher Mix", 0.5f}, {"Crusher Place", 1}, {"Memory Tilt", 0.7f}, {"Par Mix", 0.42f}, {"Par A Pan", -0.4f}, {"Par B Pan", 0.4f}});
    addS("Frozen Choir", "Shadow's Freeze holds a bar of A's echoes as a pad while Hymn pulses the notes of the key through it.",
        {{"Shadow", 4}, {"Answer After", 5}, {"Answer Chance", 0.6f}, {"Mirror Time", 2.f}, {"Par A Feedback", 0.5f}, {"Tune On", 1}, {"Tune Root", 2},
         {"Tune Amount", 0.45f}, {"Tune Wave", 3}, {"Tune Wave Timing", 1}, {"Par Mix", 0.5f}, {"Par B Pan", 0.3f}});
    addS("Octave Bloom", "Shadow's Octave answers A an octave up (a shimmer) while the Random source wanders Lead's filter.",
        {{"Shadow", 5}, {"Answer After", 2}, {"Par A Feedback", 0.55f}, {"Par B Level", -4.f}, {"Lead Filter Type", ladder}, {"Lead Filter Place", 1},
         {"Lead Filter Cutoff", 36.f}, {"Mod 1 Source", 1}, {"Mod 1 Target", 35}, {"Mod 1 Amount", 0.45f}, {"Par Mix", 0.45f}, {"Par A Pan", -0.35f}, {"Par B Pan", 0.45f}});

    // ===== Performance (v0.8): the chain Juba -> Memory -> Reshape, played by the snapshot sequencer. Each one brings
    // its own snapshots and steps; the description says what to listen for.
    // Snap Seq Rate: 6 = 1/4, 7 = 1/2, 8 = 1 bar, 17 = 2 bars. Order: 0 Forward, 2 Ping-pong, 3 Random, 4 Drunk.
    // Blank Steps: 0 hold the last snapshot, 1 back to the preset. Snap Step N: 0 blank, 1-8 snapshot 1-8.
    using Snaps = std::vector<V>;
    auto addP = [&](const char *n, const char *d, V v, Snaps sn) { p.push_back({n, "Performance", std::move(v), d, std::move(sn)}); };
    addP("Tape Relay", "Every bar Juba changes rhythm (clave, bounce, dotted, then a swell) and the tape is pushed harder each time; listen to the repeats thicken and the highs swing from side to side as Reshape moves them.",
        {{"Engine 1", 1}, {"Juba Pattern", 20}, {"DLD Memory", 2}, {"Tape Drive", 0.45f}, {"Tape Saturation", 0.5f},
         {"Reshape", 1}, {"Reshape High Pan", -0.6f}, {"Reshape High Tone", 3.f}, {"Reshape Low Level", -4.f},
         {"Par Lead Feedback", 0.45f}, {"Par Mix", 0.42f},
         {"Snap Seq On", 1}, {"Snap Seq Rate", 8}, {"Snap Seq Length", 4}, {"Snap Seq Glide", 0.15f},
         {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}, {"Snap Step 4", 4}},
        {{},
         {{"Juba Pattern", 18}, {"Tape Drive", 0.65f}, {"Reshape High Pan", 0.6f}},
         {{"Juba Pattern", 19}, {"Tape Drive", 0.8f}, {"Tape Saturation", 0.7f}, {"Reshape High Pan", -0.3f}, {"Reshape Mid Level", 3.f}},
         {{"Juba Pattern", 23}, {"Tape Drive", 0.95f}, {"Reshape High Pan", 0.7f}, {"Reshape High Tone", -4.f}, {"Par Lead Feedback", 0.6f}}});
    addP("Loose Clave", "Juba's clave on quarter-note steps with chances below 100 %: some steps keep the last snapshot, so the band levels and the Memory's grit (tape, then 7 bits, 4 on the third snapshot) come and go unevenly. Turn Variation down and every step plays.",
        {{"Engine 1", 1}, {"Juba Pattern", 20}, {"DLD Memory", 2}, {"Tape Drive", 0.6f}, {"Crusher Bits", 7}, {"Crusher Mix", 0.5f}, {"Crusher Place", 1},
         {"Reshape", 1}, {"Reshape Mid Level", 4.f}, {"Reshape Mid Tone", 3.f},
         {"Par Lead Feedback", 0.4f}, {"Par Mix", 0.4f},
         {"Snap Seq On", 1}, {"Snap Seq Rate", 6}, {"Snap Seq Length", 8}, {"Snap Seq Variation", 1.f},
         {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 1}, {"Snap Step 4", 3}, {"Snap Step 5", 2}, {"Snap Step 6", 1}, {"Snap Step 7", 3}, {"Snap Step 8", 2},
         {"Snap Step 2 Chance", 0.6f}, {"Snap Step 4 Chance", 0.4f}, {"Snap Step 5 Chance", 0.7f}, {"Snap Step 7 Chance", 0.35f}, {"Snap Step 8 Chance", 0.5f}},
        {{},
         {{"Reshape Mid Level", -12.f}, {"Reshape High Level", 5.f}, {"Juba Pattern", 21}},
         {{"Reshape Low Level", -14.f}, {"Reshape Mid Level", 6.f}, {"Crusher Bits", 4}, {"Crusher Mix", 0.8f}}});
    addP("Tide Turns", "Two-bar steps: Juba's fibonacci taps, then the plain preset returns on the blank steps. Listen for the tape slowing into each new snapshot (Varispeed) and Reshape darkening the highs as the tide goes out.",
        {{"Engine 1", 1}, {"Juba Pattern", 22}, {"DLD Memory", 2}, {"Tape Drive", 0.55f}, {"Varispeed", 1}, {"Varispeed Bend Time", 9.f},
         {"Reshape", 1}, {"Reshape High Tone", 2.f},
         {"Par Lead Feedback", 0.5f}, {"Par Mix", 0.42f},
         {"Snap Seq On", 1}, {"Snap Seq Rate", 17}, {"Snap Seq Length", 4}, {"Snap Seq Blank Steps", 1},
         {"Snap Step 1", 1}, {"Snap Step 2", 0}, {"Snap Step 3", 2}, {"Snap Step 4", 0}},
        {{{"Juba Pattern", 22}, {"Par Lead Time", 7.f}, {"Reshape High Tone", -6.f}, {"Reshape High Level", -6.f}},
         {{"Juba Pattern", 23}, {"Par Lead Time", -5.f}, {"Tape Drive", 0.85f}, {"Reshape Low Level", 4.f}, {"Reshape High Tone", -9.f}}});

    // ===== v0.8.2: ten more Performance presets: Black Herman and the spectral split, played by the snapshot sequencer
    // (three grow Ancestors). Black Herman Grid: 5 1/16, 8 1/8, 11 1/4, 13 1/2, 14 1 bar. Snap: 1 semitones, 2 fifths, 3 octaves.
    const V pF = {{"Engine 2", 1}, {"Parallel Split", 1}, {"Spectral Split By", 0}}, pT = {{"Engine 2", 1}, {"Parallel Split", 1}, {"Spectral Split By", 1}};
    auto mix = [](V base, V more) { for (auto &x : more) base.push_back(x); return base; };
    addP("Herman's Prism", "Black Herman nudges A and B in fifths on the eighths while each bar a new snapshot moves the frequency split; on the last step he locks his moves so the bar repeats. Listen for the split sliding and the rhythm settling.",
        mix(pF, {{"Par A Time", 7.02f}, {"Par A Feedback", 0.62f}, {"Par B Time", 12.f}, {"Par B Feedback", 0.58f}, {"Black Herman Influence", 0.6f}, {"Black Herman Grid", 8}, {"Black Herman Chaos", 0.3f}, {"Black Herman Snap", 2},
                 {"Par Mix", 0.42f}, {"Snap Seq On", 1}, {"Snap Seq Rate", 8}, {"Snap Seq Length", 4}, {"Snap Seq Glide", 0.3f}, {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}, {"Snap Step 4", 4}}),
        {{}, {{"Spectral Low/Mid", 2.f}, {"Black Herman Influence", 0.9f}}, {{"Spectral Split By", 1}, {"Legerdemain", 0.6f}}, {{"Black Herman Lock", 1}, {"Spectral Low/Mid", -20.f}}});
    addP("Sleight of Bands", "Split by tonal content: Legerdemain pushes A one way as B goes the other, and Black Herman twitches both on the sixteenths. Snapshots trade the trick on and off and swing the bands across the speakers.",
        mix(pT, {{"Par A Time", 12.f}, {"Par A Feedback", 0.6f}, {"Par B Time", 7.02f}, {"Par B Feedback", 0.62f}, {"Legerdemain", 0.7f}, {"Legerdemain Mischief", 0.5f}, {"Black Herman Influence", 0.5f}, {"Black Herman Grid", 5},
                 {"Reshape", 1}, {"Par Mix", 0.42f}, {"Snap Seq On", 1}, {"Snap Seq Rate", 8}, {"Snap Seq Length", 4}, {"Snap Seq Glide", 0.6f}, {"Snap Seq Order", 2}, {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}, {"Snap Step 4", 2}}),
        {{}, {{"Legerdemain", 0.f}, {"Reshape Mid Pan", -0.8f}, {"Reshape High Pan", 0.8f}}, {{"Legerdemain", 1.f}, {"Reshape Mid Pan", 0.8f}, {"Reshape High Pan", -0.8f}, {"Black Herman Influence", 0.8f}}});
    addP("Family Trick", "Ancestors grows a family from every echo of a frequency split while Black Herman moves A and B in octaves on the quarters. Each bar a snapshot changes the family: wider, more generations, darker.",
        mix(pF, {{"Par A Time", 12.f}, {"Par A Feedback", 0.6f}, {"Par B Time", 19.02f}, {"Par B Feedback", 0.55f}, {"Ancestors", 1}, {"Ancestors Generations", 1}, {"Ancestors Level", -3.f}, {"Ancestors Fade", 0.6f},
                 {"Black Herman Influence", 0.55f}, {"Black Herman Grid", 11}, {"Black Herman Snap", 3}, {"Par Mix", 0.45f},
                 {"Snap Seq On", 1}, {"Snap Seq Rate", 8}, {"Snap Seq Length", 4}, {"Snap Seq Glide", 0.5f}, {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}, {"Snap Step 4", 2}}),
        {{}, {{"Ancestors Spread", 0.9f}, {"Ancestors Fan", 0.8f}}, {{"Ancestors Generations", 2}, {"Ancestors Darken", 0.8f}, {"Spectral Low/Mid", 0.f}}});
    addP("Pocket Spectrum", "Juba plays the lows and Echo the highs; Black Herman's pocket locks into a 16-step loop of moves on the eighths. Every bar a snapshot changes Juba's pattern and where the split sits.",
        {{"Engine 1", 1}, {"Engine 2", 1}, {"Parallel Split", 1}, {"Spectral Split By", 0}, {"Juba Pattern", 18}, {"Par Lead Feedback", 0.55f}, {"Par B Time", 7.02f}, {"Par B Feedback", 0.6f},
         {"Black Herman Influence", 0.6f}, {"Black Herman Grid", 8}, {"Black Herman Lock", 1}, {"Black Herman Lock Steps", 2}, {"Par Mix", 0.42f},
         {"Snap Seq On", 1}, {"Snap Seq Rate", 8}, {"Snap Seq Length", 4}, {"Snap Seq Glide", 0.2f}, {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}, {"Snap Step 4", 4}},
        {{}, {{"Juba Pattern", 20}, {"Spectral Low/Mid", 0.f}}, {{"Juba Pattern", 21}, {"Spectral Low/Mid", -20.f}}, {{"Juba Pattern", 22}, {"Spectral Mid/High", 40.f}}});
    addP("Card Shark", "Black Herman deals bursts of chaos into a tonal split while the snapshots shuffle in random order on half notes, each step with its own chance. No two bars play the same hand.",
        mix(pT, {{"Par A Time", 12.f}, {"Par A Feedback", 0.62f}, {"Par B Time", 4.02f}, {"Par B Feedback", 0.55f}, {"Black Herman Influence", 0.6f}, {"Black Herman Chaos", 0.6f}, {"Black Herman Bursts", 0.5f}, {"Black Herman Chance", 0.7f},
                 {"Reshape", 1}, {"Par Mix", 0.42f}, {"Snap Seq On", 1}, {"Snap Seq Rate", 7}, {"Snap Seq Length", 8}, {"Snap Seq Order", 3}, {"Snap Seq Variation", 1.f},
                 {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}, {"Snap Step 4", 1}, {"Snap Step 5", 3}, {"Snap Step 6", 2}, {"Snap Step 7", 1}, {"Snap Step 8", 3},
                 {"Snap Step 3 Chance", 0.6f}, {"Snap Step 5 Chance", 0.5f}, {"Snap Step 8 Chance", 0.4f}}),
        {{}, {{"Reshape High Level", 4.f}, {"Reshape Low Level", -10.f}, {"Black Herman Chaos", 0.9f}}, {{"Reshape High Tone", -6.f}, {"Reshape Mid Level", 4.f}, {"Par A Time", 7.02f}}});
    addP("Lineage", "A tonal split whose echoes grow Ancestors only when Lead calls: Mutation calls every bar, Black Herman moves the parts in between. Snapshots switch which part the family grows from: everything, then A, then B.",
        mix(pT, {{"Par A Time", 12.f}, {"Par A Feedback", 0.6f}, {"Par B Time", 19.02f}, {"Par B Feedback", 0.58f}, {"Ancestors", 1}, {"Ancestors Grow", 1}, {"Ancestors Generations", 2}, {"Ancestors Level", -3.f},
                 {"Mutation Trigger", 6}, {"Mutation Every Step", 4}, {"Black Herman Influence", 0.4f}, {"Black Herman Grid", 11}, {"Par Mix", 0.45f},
                 {"Snap Seq On", 1}, {"Snap Seq Rate", 17}, {"Snap Seq Length", 3}, {"Snap Seq Glide", 0.4f}, {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}}),
        {{}, {{"Ancestors From", 1}, {"Ancestors Spread", 0.8f}}, {{"Ancestors From", 2}, {"Ancestors Darken", 0.7f}}});
    addP("Vanishing Act", "Black Herman bends the times in semitone steps on half notes while the snapshots make the split's bands disappear in turn: first the highs, then the lows, then all three come back panned wide.",
        mix(pF, {{"Par A Time", 12.f}, {"Par A Feedback", 0.64f}, {"Par B Time", 7.02f}, {"Par B Feedback", 0.6f}, {"Black Herman Influence", 0.7f}, {"Black Herman Grid", 13}, {"Black Herman Snap", 1},
                 {"Reshape", 1}, {"Par Mix", 0.45f}, {"Snap Seq On", 1}, {"Snap Seq Rate", 8}, {"Snap Seq Length", 4}, {"Snap Seq Glide", 0.8f}, {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}, {"Snap Step 4", 4}}),
        {{}, {{"Spectral High Level", -24.f}}, {{"Spectral Low Level", -24.f}, {"Spectral High Level", 0.f}}, {{"Reshape Low Pan", -0.8f}, {"Reshape High Pan", 0.8f}}});
    addP("Levitation", "Black Herman lifts the times bar by bar, leaning upward, while the snapshots raise Lead an octave on the third bar and open the split's high band on the fourth. The echoes seem to float up.",
        mix(pF, {{"Par A Time", 12.f}, {"Par A Feedback", 0.62f}, {"Par B Time", 19.02f}, {"Par B Feedback", 0.6f}, {"Black Herman Influence", 0.6f}, {"Black Herman Grid", 14}, {"Black Herman Lean", 0.7f},
                 {"Par Mix", 0.42f}, {"Snap Seq On", 1}, {"Snap Seq Rate", 8}, {"Snap Seq Length", 4}, {"Snap Seq Glide", 0.5f}, {"Snap Step 1", 1}, {"Snap Step 2", 1}, {"Snap Step 3", 2}, {"Snap Step 4", 3}}),
        {{}, {{"Lead Shift", 12.f}, {"Lead Shift Mode", 1}}, {{"Lead Shift", 12.f}, {"Lead Shift Mode", 1}, {"Spectral High Level", 4.f}, {"Spectral Mid/High", 20.f}}});
    addP("Generations", "Three generations of Ancestors on a frequency split with Juba's fibonacci taps on the highs; Black Herman stirs the eighths. Two-bar snapshots, ping-pong, widen and narrow the family tree.",
        {{"Engine 1", 0}, {"Engine 2", 2}, {"Parallel Split", 1}, {"Spectral Split By", 0}, {"Juba Pattern", 22}, {"Par A Time", 12.f}, {"Par A Feedback", 0.58f}, {"Par B Feedback", 0.55f},
         {"Ancestors", 1}, {"Ancestors Generations", 2}, {"Ancestors Level", -4.f}, {"Ancestors Fade", 0.65f}, {"Black Herman Influence", 0.5f}, {"Black Herman Grid", 8}, {"Black Herman Chaos", 0.4f}, {"Par Mix", 0.45f},
         {"Snap Seq On", 1}, {"Snap Seq Rate", 17}, {"Snap Seq Length", 3}, {"Snap Seq Glide", 0.7f}, {"Snap Seq Order", 2}, {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}},
        {{{"Ancestors Spread", 0.3f}, {"Ancestors Fan", 0.3f}}, {{"Ancestors Spread", 0.7f}, {"Ancestors Fan", 0.6f}}, {{"Ancestors Spread", 1.f}, {"Ancestors Fan", 1.f}, {"Ancestors Darken", 0.7f}}});
    addP("Escape Artist", "Black Herman throws wide bursts into a tonal split while Legerdemain keeps A and B pulling apart; the snapshots play only when their chance comes up and otherwise fall back to the preset, so the trick keeps slipping away.",
        mix(pT, {{"Par A Time", 7.02f}, {"Par A Feedback", 0.6f}, {"Par B Time", 12.f}, {"Par B Feedback", 0.6f}, {"Black Herman Influence", 0.6f}, {"Black Herman Bursts", 0.8f}, {"Black Herman Chance", 0.5f}, {"Black Herman Range", 0.8f},
                 {"Legerdemain", 0.4f}, {"Par Mix", 0.42f}, {"Snap Seq On", 1}, {"Snap Seq Rate", 7}, {"Snap Seq Length", 8}, {"Snap Seq Blank Steps", 1}, {"Snap Seq Variation", 1.f},
                 {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 0}, {"Snap Step 4", 1}, {"Snap Step 5", 2}, {"Snap Step 6", 0}, {"Snap Step 7", 2}, {"Snap Step 8", 1},
                 {"Snap Step 1 Chance", 0.7f}, {"Snap Step 2 Chance", 0.5f}, {"Snap Step 4 Chance", 0.6f}, {"Snap Step 5 Chance", 0.5f}, {"Snap Step 7 Chance", 0.4f}, {"Snap Step 8 Chance", 0.6f}}),
        {{{"Spectral Split By", 0}, {"Legerdemain", 0.9f}}, {{"Black Herman Range", 1.f}, {"Spectral Mid Level", 5.f}, {"Par B Time", 19.02f}}});

    // ===== Front Porch: warm, plain, close to the dry sound
    addV1("Screen Door", "Warm & Subtle", "Soft quarter and dotted echoes a little darker than the dry sound, with a slow Sway so they drift slightly. Sits behind vocals and keys.",
        {{"Par Mix", 0.32f}, {"Par A Feedback", 0.38f}, {"Par B Feedback", 0.3f}, {"Par A Tone", 0.42f}, {"Par B Tone", 0.38f},
         {"Tilt Out", -2.f}, {"Sway", 0.18f}, {"Sway Depth", 0.35f}, {"Sway Rate", -1.5f}});
    addV1("Rocking Chair", "Warm & Subtle", "Juba's swing pattern at a low mix; an LFO rocks Lead's tone back and forth every bar, so the repeats lean bright then dark.",
        {{"Engine 1", 1}, {"Juba Pattern", 2}, {"Par Lead Feedback", 0.55f}, {"Par Mix", 0.35f}, {"Par Lead Tone", 0.45f},
         {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 5}, {"LFO 1 Target", 9}, {"LFO 1 Amount", 0.25f}});
    addV1("Porch Light", "Warm & Subtle", "A clean delay with Hymn ringing G-scale notes out of the echoes and B shifted up a fifth on its first repeat: gentle harmony.",
        {{"Par Mix", 0.38f}, {"Par A Feedback", 0.42f}, {"Par B Feedback", 0.42f}, {"Tune On", 1}, {"Tune Root", 7}, {"Tune Scale", 9},
         {"Tune Amount", 0.2f}, {"B Shift", 7}, {"B Shift Mode", 1}});

    // ===== Ring Shout: rhythm, answers, patterns that move
    addV1("Call and Response", "Rhythmic", "A plays the echo, then B answers it a step later (Shadow: Answer, 80 % of the time), panned to the other side and darker.",
        {{"Shadow", 1}, {"Answer After", 1}, {"Answer Chance", 0.8f}, {"Par Mix", 0.45f}, {"Par A Feedback", 0.35f},
         {"Par B Feedback", 0.5f}, {"Par B Tone", 0.35f}, {"Par A Pan", -0.5f}, {"Par B Pan", 0.5f}});
    addV1("Clap Line", "Rhythmic", "Juba's swung B2 pattern as a handclap rhythm; Black Herman pushes A and B slightly off the grid so it feels played, not quantized.",
        {{"Engine 1", 1}, {"Juba Pattern", 7}, {"Par Lead Feedback", 0.45f}, {"Par Mix", 0.42f},
         {"Black Herman Influence", 0.5f}});
    addV1("Shout Back", "Rhythmic", "Juba on Engine 2 against Echo on Engine 1, with Black Herman snapping their timing to a grid and throwing in chaos: busy, call-and-shout rhythms.",
        {{"Engine 2", 2}, {"Juba Pattern", 9}, {"Par Mix", 0.45f}, {"Par A Feedback", 0.4f}, {"Par B Feedback", 0.5f},
         {"Black Herman Influence", 0.55f}, {"Black Herman Grid", 5}, {"Black Herman Chaos", 0.35f}, {"Black Herman Range", 7.f}, {"Black Herman Snap", 2}});
    addV1("Stomp Patterns", "Rhythmic", "An LFO steps through Juba's patterns every 1/8, so the rhythm of the echoes changes constantly. Juba's Morph smooths each change.",
        {{"Engine 1", 1}, {"Juba Morph", 7.6f}, {"Par Lead Feedback", 0.5f}, {"Par Mix", 0.4f},
         {"LFO 1 Shape", 9}, {"LFO 1 Sync Rate", 4}, {"LFO 1 Target", 40}, {"LFO 1 Amount", 0.6f}, {"LFO 1 Output", 1}});
    addV1("Mirror Dance", "Rhythmic", "B plays A's echoes backwards (Shadow: Reverse), and an LFO swings A's pan from side to side: reversed answers dancing around the stereo field.",
        {{"Shadow", 2}, {"Mirror Time", 0.5f}, {"Par Mix", 0.42f}, {"Par A Feedback", 0.45f}, {"Par B Feedback", 0.4f},
         {"Par A Pan", -0.7f}, {"Par B Pan", 0.7f}, {"LFO 2 Shape", 2}, {"LFO 2 Sync Rate", 6}, {"LFO 2 Target", 6}, {"LFO 2 Amount", 0.5f}});

    // ===== Low Country: dust, bits, runaway loops
    addV1("Ensoniq Dust", "Lo-fi & Character", "An old 12-bit sampler in the loop: about 22 kHz, a little hiss and a darker top. Each repeat gets a bit dustier.",
        {{"Crusher Bits", 12}, {"Memory Rate", 14.43f}, {"Crusher Mix", 0.7f}, {"Crusher Steps", 1}, {"Crusher Place", 0}, {"Memory Tilt", 0.3f}, {"Memory Noise", 0.1f}, {"Par Mix", 0.4f},
         {"Par A Feedback", 0.5f}, {"Par B Feedback", 0.5f}, {"Tilt Out", -3.f}});
    addV1("Spanish Moss", "Lo-fi & Character", "Juba's cluster pattern through a resonant ladder filter in the loop, with an LFO slowly opening and closing it: murky, swaying repeats.",
        {{"Engine 1", 1}, {"Juba Pattern", 14}, {"Lead Filter Type", ladder}, {"Lead Filter Place", 2}, {"Lead Filter Cutoff", 30.f}, {"Lead Filter Reso", 0.45f},
         {"Par Lead Feedback", 0.65f}, {"Par Mix", 0.45f}, {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 6}, {"LFO 1 Target", 35}, {"LFO 1 Amount", 0.45f}});
    addV1("Chip Ghost", "Lo-fi & Character", "6-bit chip-style crush on the output with A on eighths: bright, buzzy video-game echoes.",
        {{"Chip Mode", 1}, {"Crusher Bits", 6}, {"Crusher Mix", 0.8f}, {"Crusher Place", 1}, {"Par Mix", 0.38f},
         {"Par A Time", -12.f}, {"Par A Feedback", 0.55f}, {"Par B Feedback", 0.45f}});

    // ===== Deep Water: long, wide, wet
    addV1("Blackwater", "Deep & Spectral", "Two Echo engines in a chain: the second takes the first's repeats and stretches them to double length. Long, dark and deep.",
        {{"Engine 2", 1}, {"Wiring", 1}, {"Ser Mix", 0.5f}, {"Ser A Feedback", 0.6f}, {"Ser B Feedback", 0.55f}, {"Ser B Time", 12.f}});
    addV1("Spectral Spiral", "Deep & Spectral", "Split by tonal content: held notes go to Echo, hits and air to Juba's clave, with a Z-Plane filter on all the echoes. Long, wide feedback.",
        {{"Engine 2", 2}, {"Parallel Split", 1}, {"Spectral Split By", 1}, {"Juba Pattern", 20}, {"Par Mix", 0.5f},
         {"Par A Feedback", 0.7f}, {"Par B Feedback", 0.6f}, {"Lead Filter Type", zplane}, {"Lead Filter Place", 1}, {"Lead Filter Cutoff", 40.f}});
    addV1("Tidewater Cloud", "Deep & Spectral", "Juba's fibonacci taps at high feedback with a slow wobble; the River's Lead current moves Lead's feedback, so the cloud swells and recedes.",
        {{"Engine 1", 1}, {"Juba Pattern", 22}, {"Par Lead Feedback", 0.8f}, {"Juba Mod Amount", 0.25f}, {"Juba Mod Rate", 0.2f}, {"Par Mix", 0.5f}, {"River Tide", 1},
         {"Mod 1 Source", 10}, {"Mod 1 Target", 8}, {"Mod 1 Amount", 0.25f}});

    // ===== v0.8.1: 15 more for Deep & Spectral: long, evolving, split by frequency or by tonal content. Each keeps moving
    // on its own (LFOs over 4-8 bars, the River, Sway, Mutation, Ancestors, or its own snapshots).
    // Targets: 19 Spectral Low/Mid, 26 Spectral Balance, 27 Spectral Sharpness, 54 Spectral Mid/High, 55-57 Spectral
    // Low/Mid/High Level, 40 Juba Pattern, 64-66 Reshape levels, 67-69 Reshape pans, 70-72 Reshape tones.
    // LFO Sync Rate: 4 1 bar, 5 2 bars, 6 4 bars, 7 8 bars. Snap Seq Rate: 17 2 bars, 18 4 bars, 19 8 bars.
    auto addD = [&](const char *n, const char *d, V v, std::vector<V> sn = {}) { p.push_back({n, "Deep & Spectral", std::move(v), d, std::move(sn)}); };
    const V splitF = {{"Engine 2", 1}, {"Parallel Split", 1}, {"Spectral Split By", 0}}, splitT = {{"Engine 2", 1}, {"Parallel Split", 1}, {"Spectral Split By", 1}};
    auto with = [](V base, V more) { for (auto &x : more) base.push_back(x); return base; };
    addD("Marsh Fog", "Split by tonal content: held notes ring on in whole-note repeats, the breathy air answers on half notes; Ancestors branches every echo into a dim family; a 4-bar LFO tips the balance between the two and an 8-bar one darkens and lifts the lows.",
        with(splitT, {{"Par A Time", 24.f}, {"Par A Feedback", 0.82f}, {"Par A Tone", 0.3f}, {"Par B Time", 12.f}, {"Par B Feedback", 0.6f}, {"Par B Tone", 0.75f},
                      {"Reshape", 1}, {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 7}, {"LFO 1 Target", 70}, {"LFO 1 Amount", 0.8f}, {"Ancestors", 1}, {"Ancestors Generations", 1}, {"Ancestors Fade", 0.7f}, {"Ancestors Darken", 0.5f}, {"Ancestors Level", -4.f},
                      {"LFO 2 Shape", 2}, {"LFO 2 Sync Rate", 6}, {"LFO 2 Target", 26}, {"LFO 2 Amount", 0.5f}, {"Par Mix", 0.5f}}));
    addD("Tidal Bands", "Split by frequency with the River on a 16-bar pass and the tide turning every 8 bars: River A moves the low crossover, River B the high one and River Lead the middle band's level, so the three bands swell and ebb.",
        with(splitF, {{"Par A Time", 12.f}, {"Par A Feedback", 0.8f}, {"Par A Tone", 0.25f}, {"Par B Time", 7.02f}, {"Par B Feedback", 0.6f}, {"Par B Tone", 0.75f}, {"River Pass", 6},
                      {"Mod 3 Source", 9}, {"Mod 3 Target", 56}, {"Mod 3 Amount", 0.6f}, {"River Tide", 1}, {"Tide Turns Every", 2},
                      {"Mod 1 Source", 8}, {"Mod 1 Target", 19}, {"Mod 1 Amount", 0.6f}, {"Mod 2 Source", 10}, {"Mod 2 Target", 54}, {"Mod 2 Amount", 0.5f}, {"Par Mix", 0.48f}}));
    addD("Spectral Bloom", "Three snapshots, 4 bars each, gliding all the way: the split opens from a low, dark crossover to a high, bright one and back, with the band levels trading places as it goes.",
        with(splitF, {{"Par A Time", 12.f}, {"Par A Feedback", 0.72f}, {"Par B Time", 19.02f}, {"Par B Feedback", 0.68f}, {"Par Mix", 0.5f},
                      {"Snap Seq On", 1}, {"Snap Seq Rate", 18}, {"Snap Seq Length", 4}, {"Snap Seq Glide", 1.f}, {"Snap Seq Order", 2},
                      {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}, {"Snap Step 4", 2}}),
        {{{"Spectral Low/Mid", -26.f}, {"Spectral Mid/High", 12.f}, {"Spectral High Level", -8.f}},
         {{"Spectral Low/Mid", -10.f}, {"Spectral Mid/High", 30.f}},
         {{"Spectral Low/Mid", 6.f}, {"Spectral Mid/High", 45.f}, {"Spectral Low Level", -8.f}, {"Spectral High Level", 3.f}}});
    addD("Undertow", "The lows pull long and heavy (85 % feedback, dark), the highs sit back; an 8-bar ramp drags the high band's level down and lets it snap back up, like a wave drawing out.",
        with(splitF, {{"Par A Time", 19.02f}, {"Par A Feedback", 0.85f}, {"Par A Tone", 0.25f}, {"Par B Time", 12.f}, {"Par B Feedback", 0.6f}, {"Spectral Low Level", 3.f}, {"Spectral High Level", -6.f},
                      {"LFO 1 Shape", 3}, {"LFO 1 Sync Rate", 7}, {"LFO 1 Target", 57}, {"LFO 1 Amount", -0.6f}, {"Par Mix", 0.5f}}));
    addD("Night Current", "A steep frequency split whose low crossover jumps to a new place every 2 bars (sample-and-hold, smoothed), so a different slice of your sound falls into the long, dark repeats each time; a 4-bar LFO breathes the low band in and out.",
        with(splitF, {{"Spectral Sharpness", 0.9f}, {"Par A Time", 24.f}, {"Par A Feedback", 0.8f}, {"Par A Tone", 0.25f}, {"Par B Time", 17.02f}, {"Par B Feedback", 0.62f}, {"Par B Tone", 0.75f},
                      {"LFO 2 Shape", 2}, {"LFO 2 Sync Rate", 6}, {"LFO 2 Target", 55}, {"LFO 2 Amount", 0.5f},
                      {"LFO 1 Shape", 9}, {"LFO 1 Sync Rate", 5}, {"LFO 1 Target", 19}, {"LFO 1 Amount", 0.45f}, {"LFO 1 Smooth", 0.6f}, {"Par Mix", 0.5f}}));
    addD("Drift Shelf", "Split by frequency with Mutation on: every bar Lead's movements set A and B answering, one following, one opposing, so the two bands keep renegotiating their repeats, while a 4-bar LFO slides the split between the dark low band and the bright high one.",
        with(splitF, {{"Par A Time", 12.f}, {"Par A Feedback", 0.78f}, {"Par A Tone", 0.28f}, {"Par B Time", 19.02f}, {"Par B Feedback", 0.62f}, {"Par B Tone", 0.72f},
                      {"LFO 1 Shape", 2}, {"LFO 1 Sync Rate", 6}, {"LFO 1 Target", 19}, {"LFO 1 Amount", 0.55f}, {"Mutate", 0.5f}, {"Mutate Every", 1}, {"Mutation A Answer", 1}, {"Mutation B Answer", 2}, {"Sway", 0.3f}, {"Par Mix", 0.48f}}));
    addD("Salt Haze", "Split by tonal content through tape: long, saturated repeats, an 8-bar triangle LFO slowly swapping which of the two (notes or air) leads, and a 4-bar sine raising and lowering the air.",
        with(splitT, {{"Par A Time", 19.02f}, {"Par A Feedback", 0.78f}, {"Par B Time", 12.f}, {"Par B Feedback", 0.65f}, {"DLD Memory", 2}, {"Tape Drive", 0.45f},
                      {"LFO 1 Shape", 2}, {"LFO 1 Sync Rate", 7}, {"LFO 1 Target", 26}, {"LFO 1 Amount", 0.6f}, {"Par A Tone", 0.3f}, {"Par B Tone", 0.75f},
                      {"LFO 2 Shape", 1}, {"LFO 2 Sync Rate", 6}, {"LFO 2 Target", 57}, {"LFO 2 Amount", 0.5f}, {"Par Mix", 0.48f}}));
    addD("Deep Sounding", "A very low split (120 Hz and 900 Hz): the sub and low-mids repeat on whole notes at high feedback through a ladder filter that opens and closes over 8 bars, and Ancestors grows three generations of branching echoes beneath.",
        with(splitF, {{"Spectral Low/Mid", -22.49f}, {"Spectral Mid/High", 12.39f}, {"Par A Time", 24.f}, {"Par A Feedback", 0.84f}, {"Par B Time", 12.f}, {"Par B Feedback", 0.6f},
                      {"Lead Filter Type", ladder}, {"Lead Filter Place", 1}, {"Lead Filter Cutoff", 26.f}, {"Lead Filter Reso", 0.25f},
                      {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 7}, {"LFO 1 Target", 35}, {"LFO 1 Amount", 0.45f},
                      {"Ancestors", 1}, {"Ancestors Generations", 2}, {"Ancestors Fade", 0.6f}, {"Ancestors Darken", 0.6f}, {"Ancestors Level", -6.f}, {"Par Mix", 0.5f}}));
    addD("Oxbow", "The River on an 8-bar pass with the tide turning every 4 bars: River A moves the low crossover between a dark low band and a bright high one, River Lead raises and lowers the middle, and River B swings the highs across the speakers, back and forth like a bend in the river.",
        with(splitF, {{"Par A Time", 12.f}, {"Par A Feedback", 0.78f}, {"Par A Tone", 0.25f}, {"Par B Time", 7.02f}, {"Par B Feedback", 0.62f}, {"Par B Tone", 0.75f}, {"Reshape", 1}, {"River Pass", 5},
                      {"Mod 3 Source", 9}, {"Mod 3 Target", 56}, {"Mod 3 Amount", 0.7f},
                      {"River Tide", 1}, {"Tide Turns Every", 1},
                      {"Mod 1 Source", 8}, {"Mod 1 Target", 19}, {"Mod 1 Amount", 0.6f}, {"Mod 2 Source", 10}, {"Mod 2 Target", 69}, {"Mod 2 Amount", 0.8f}, {"Par Mix", 0.48f}}));
    addD("Slow Prism", "Snapshots on 4-bar steps: a frequency split, then a tonal split, then a steep one with the bands panned apart. Knobs glide over half of each step; the switch from frequency to tonal lands on the step.",
        with(splitF, {{"Par A Time", 19.02f}, {"Par A Feedback", 0.74f}, {"Par B Time", 24.f}, {"Par B Feedback", 0.7f}, {"Reshape", 1}, {"Par Mix", 0.5f},
                      {"Snap Seq On", 1}, {"Snap Seq Rate", 18}, {"Snap Seq Length", 3}, {"Snap Seq Glide", 0.5f},
                      {"Snap Step 1", 1}, {"Snap Step 2", 2}, {"Snap Step 3", 3}}),
        {{},
         {{"Spectral Split By", 1}, {"Reshape High Tone", -4.f}},
         {{"Spectral Sharpness", 1.f}, {"Reshape Low Pan", -0.7f}, {"Reshape High Pan", 0.7f}, {"Reshape High Tone", 4.f}}});
    addD("Blue Hour", "Split by tonal content with Hymn ringing D-minor notes; an 8-bar sine darkens and brightens the high band through Reshape while a 4-bar triangle lifts the middle band.",
        with(splitT, {{"Par A Time", 24.f}, {"Par A Feedback", 0.76f}, {"Par B Time", 12.f}, {"Par B Feedback", 0.62f}, {"Tune On", 1}, {"Tune Root", 2}, {"Tune Amount", 0.3f}, {"Reshape", 1},
                      {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 7}, {"LFO 1 Target", 72}, {"LFO 1 Amount", 0.7f},
                      {"LFO 2 Shape", 2}, {"LFO 2 Sync Rate", 6}, {"LFO 2 Target", 56}, {"LFO 2 Amount", 0.5f}, {"Par Mix", 0.48f}}));
    addD("Tide Pool", "Juba's fibonacci taps on the notes and a long Echo on the air (a tonal split); an 8-bar stepped LFO moves Juba to a new pattern now and then, so the rhythm in the pool keeps changing, and the high band rises and falls over 8 bars.",
        {{"Engine 1", 1}, {"Engine 2", 1}, {"Parallel Split", 1}, {"Spectral Split By", 1}, {"Juba Pattern", 22}, {"Par Lead Feedback", 0.7f}, {"Par B Time", 19.02f}, {"Par B Feedback", 0.72f},
         {"LFO 1 Shape", 10}, {"LFO 1 Sync Rate", 7}, {"LFO 1 Target", 40}, {"LFO 1 Amount", 0.3f}, {"Par B Tone", 0.3f},
         {"Reshape", 1}, {"LFO 2 Shape", 1}, {"LFO 2 Sync Rate", 7}, {"LFO 2 Target", 66}, {"LFO 2 Amount", 0.6f}, {"Par Mix", 0.48f}});

    // ===== v0.8.2: 1 Note and Go. One note in (a piano key, a pluck) and four bars of sound out: arps built from Shift on
    // each repeat (intervals, so they follow whatever key you play in) or a lush field from Shadow, Ancestors and Sway.
    // No Varispeed, no Memory hiss.
    auto addN = [&](const char *n, const char *d, V v, std::vector<V> sn = {}) { p.push_back({n, "1 Note and Go", std::move(v), d, std::move(sn)}); };
    addN("Rising Fifths", "An arp from one note: every half note A climbs a fifth, B answers an octave higher every dotted quarter, and the climb fades out over about four bars.",
        {{"Par A Time", 12.f}, {"Par A Feedback", 0.82f}, {"A Shift", 7.f}, {"A Shift Mode", 2}, {"Par B Time", 7.02f}, {"Par B Feedback", 0.7f}, {"B Shift", 12.f}, {"B Shift Mode", 2},
         {"Par A Pan", -0.45f}, {"Par B Pan", 0.45f}, {"Par Lead Tone", 0.42f}, {"Par Mix", 0.5f}});
    addN("Crossing Thirds", "Two arps from one note: every half note A falls a minor third while every dotted quarter B rises a major third, crossing left and right as they pass.",
        {{"Par A Time", 12.f}, {"Par A Feedback", 0.82f}, {"A Shift", -3.f}, {"A Shift Mode", 2}, {"Par B Time", 7.02f}, {"Par B Feedback", 0.74f}, {"B Shift", 4.f}, {"B Shift Mode", 2},
         {"Par A Pan", 0.5f}, {"Par B Pan", -0.5f}, {"Par Mix", 0.5f}});
    addN("Octave Ladder", "Juba's bouncing taps with every repeat an octave up: one note becomes a quickening ladder of octaves that thins out into the air.",
        {{"Engine 1", 1}, {"Juba Pattern", 18}, {"Par Lead Feedback", 0.4f}, {"Lead Shift", 12.f}, {"Lead Shift Mode", 2}, {"Par Lead Tone", 0.38f}, {"Par Mix", 0.5f}});
    addN("Halo Pad", "Shadow freezes the last half bar of A into a pad on every half note, Ancestors branches the echoes into three generations and Sway keeps it breathing: a held cloud from a single note.",
        {{"Shadow", 4}, {"Answer Chance", 1.f}, {"Answer After", 4}, {"Mirror Time", 2.f}, {"Par A Time", 0.f}, {"Par A Feedback", 0.7f}, {"Ancestors", 1}, {"Ancestors Generations", 2}, {"Ancestors Level", -3.f},
         {"Sway", 0.4f}, {"Sway Depth", 12.f}, {"Par Mix", 0.55f}});
    addN("Swell Choir", "Shadow plays A backwards, rising into each next echo, while A rings an octave up once: one note turns into swelling, choir-like waves.",
        {{"Shadow", 6}, {"Answer Chance", 1.f}, {"Mirror Time", 1.5f}, {"A Shift", 12.f}, {"A Shift Mode", 1}, {"Par A Time", 0.f}, {"Par A Feedback", 0.72f}, {"Ancestors", 1}, {"Ancestors Generations", 1},
         {"Sway", 0.3f}, {"Par Mix", 0.55f}});
    addN("Smear Garden", "A long dotted-quarter echo blurred into grains by Shadow's Smear, scattered across the speakers; an LFO slowly swings the highs from side to side through Reshape.",
        {{"Shadow", 7}, {"Answer Chance", 1.f}, {"Mirror Time", 2.f}, {"Par A Time", 7.02f}, {"Par A Feedback", 0.78f}, {"Reshape", 1},
         {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 5}, {"LFO 1 Target", 69}, {"LFO 1 Amount", 0.8f}, {"Par Mix", 0.55f}});
    addN("Ghost Bells", "A climbs a fifth every half note while Shadow's Ghost answers each echo an eighth later, an octave down and dark: bright bells over low shadows.",
        {{"Shadow", 8}, {"Answer After", 1}, {"Par A Time", 12.f}, {"Par A Feedback", 0.8f}, {"A Shift", 7.f}, {"A Shift Mode", 2}, {"Par A Tone", 0.6f}, {"Par Mix", 0.5f}});
    addN("Fourths Rain", "Juba's fibonacci taps with each repeat a fourth higher: one note falls as a scattered rain of rising fourths, spread wide.",
        {{"Engine 1", 1}, {"Juba Pattern", 22}, {"Par Lead Feedback", 0.62f}, {"Lead Shift", 5.f}, {"Lead Shift Mode", 2}, {"Par Lead Tone", 0.5f}, {"Juba Velocity", 0.6f}, {"Par Mix", 0.5f}});
    addN("Wide Bloom", "Split by tonal content: the note itself rings a whole note long and an octave up, its attack scatters on eighths; Ancestors grows three generations and Sway keeps the bloom moving.",
        {{"Engine 2", 1}, {"Parallel Split", 1}, {"Spectral Split By", 1}, {"Par A Time", 24.f}, {"Par A Feedback", 0.76f}, {"A Shift", 12.f}, {"A Shift Mode", 1}, {"Par B Time", -12.f}, {"Par B Feedback", 0.55f},
         {"Ancestors", 1}, {"Ancestors Generations", 2}, {"Ancestors Level", -4.f}, {"Sway", 0.35f}, {"Par Mix", 0.55f}});
    addN("Clockwork Arp", "Black Herman moves the times on the quarters in fifths and locks into a 16-step loop, while A climbs a fourth and B falls a whole tone on each repeat: a mechanical arp that repeats its moves and slowly fades.",
        {{"Par A Time", 12.f}, {"Par A Feedback", 0.8f}, {"A Shift", 5.f}, {"A Shift Mode", 2}, {"Par B Time", 7.02f}, {"Par B Feedback", 0.72f}, {"B Shift", -2.f}, {"B Shift Mode", 2},
         {"Black Herman Influence", 0.6f}, {"Black Herman Grid", 11}, {"Black Herman Snap", 2}, {"Black Herman Lock", 1}, {"Black Herman Lock Steps", 2}, {"Par Mix", 0.5f}});

    // ===== Tides (v0.5): spectral splits and loops that keep changing, each its own animal
    auto addT = [&](const char *n, const char *d, V v) { p.push_back({n, "Moving", std::move(v), d}); };
    addT("Tonal Drift", "Split by Tonal: held tones ring through Echo, hits play Juba, and Mutation keeps nudging how the two answer each other.",
        {{"Engine 1", 0}, {"Engine 2", 2}, {"Parallel Split", 1}, {"Spectral Split By", 1}, {"Juba Pattern", 7}, {"Par A Feedback", 0.62f}, {"Par B Feedback", 0.45f},
         {"Mutate", 0.55f}, {"Mutate Every", 1}, {"Mutation A Answer", 1}, {"Mutation B Answer", 2}, {"Sway", 0.3f}, {"Par Mix", 0.48f}});
    addT("Low Tide", "Split by frequency: the lows sink into long dark repeats, the highs ping-pong short, and a slow LFO walks the crossover up and down.",
        {{"Engine 2", 1}, {"Parallel Split", 1}, {"Spectral Split By", 0}, {"Spectral Low/Mid", -15.47f}, {"Spectral Mid/High", 30.08f},
         {"Par A Time", 12.f}, {"Par A Feedback", 0.78f}, {"Par A Tone", 0.3f}, {"Par B Time", -12.f}, {"Par B Feedback", 0.4f}, {"Par A Pan", -0.2f}, {"Par B Pan", 0.7f},
         {"LFO 1 Shape", 2}, {"LFO 1 Sync Rate", 9}, {"LFO 1 Target", 19}, {"LFO 1 Amount", 0.6f}, {"Par Mix", 0.5f}});
    addT("Rising Water", "Two Echoes in a Chain on tape: Cross feeds the second back into the first so the loop swells and darkens while the River breathes the feedback.",
        {{"Engine 2", 1}, {"Wiring", 1}, {"Chain Cross", 0.55f}, {"Chain Wire Tone", -0.4f}, {"Chain Wire Drive", 0.3f}, {"DLD Memory", 2}, {"Tape Drive", 0.45f},
         {"Ser A Time", 7.02f}, {"Ser B Time", 12.f}, {"Ser A Feedback", 0.45f}, {"Ser B Feedback", 0.5f}, {"Mod 1 Source", 9}, {"Mod 1 Target", 2}, {"Mod 1 Amount", 0.25f}, {"Ser Mix", 0.45f}});
    addT("Air Lines", "Split by Tonal with Juba on the air: a stepped LFO keeps changing Juba's pattern, so the breath and sparkle of your sound scatter in new rhythms.",
        {{"Engine 1", 0}, {"Engine 2", 2}, {"Parallel Split", 1}, {"Spectral Split By", 1}, {"Spectral High Level", 3.f}, {"Juba Pattern", 21},
         {"LFO 1 Shape", 9}, {"LFO 1 Sync Rate", 6}, {"LFO 1 Target", 40}, {"LFO 1 Amount", 0.5f}, {"Par A Feedback", 0.35f}, {"Par B Feedback", 0.55f}, {"Par Mix", 0.45f}});
    addT("Shoreline", "A loop that never settles: Black Herman passes moves on the eighths, Legerdemain pushes A and B apart, Mutation drifts it all, on warm tape.",
        {{"Par A Feedback", 0.72f}, {"Par B Feedback", 0.68f}, {"DLD Memory", 2}, {"Tape Drive", 0.35f}, {"Black Herman Influence", 0.65f}, {"Black Herman Grid", 8},
         {"Black Herman Chaos", 0.35f}, {"Legerdemain", 0.5f}, {"Legerdemain Mischief", 0.4f}, {"Mutate", 0.6f}, {"Par Mix", 0.45f}, {"Par A Pan", -0.5f}, {"Par B Pan", 0.5f}});
    addT("Spectral Field", "Two Jubas split by frequency: a swell pattern on the lows, three-against-four on the highs, with Hymn sparkling the key over the top.",
        {{"Engine 1", 1}, {"Engine 2", 2}, {"Parallel Split", 1}, {"Spectral Split By", 0}, {"Juba Pattern", 23}, {"Juba 2 Pattern", 21}, {"Par A Feedback", 0.55f},
         {"Par B Feedback", 0.5f}, {"Tune On", 1}, {"Tune Root", 7}, {"Tune Amount", 0.35f}, {"Tune Wave", 6}, {"Tune Wave Timing", 1}, {"Par Mix", 0.5f}});

    // ===== Bread and Butter: the everyday delays
    add("Quarter Note", "Basic Delays", "A clean quarter-note delay, 35 % feedback, slightly darker repeats, A and B a little apart. The everyday delay.", {{"Par Mix", 0.3f}, {"Par A Feedback", 0.35f}, {"Par B Feedback", 0.35f}, {"Par A Tone", 0.45f}, {"Par B Tone", 0.45f}, {"Par A Pan", -0.2f}, {"Par B Pan", 0.2f}});
    add("Dotted Eighth", "Basic Delays", "Dotted-eighth echoes on both sides, 40 % feedback: the classic rhythmic guitar delay.", {{"Par Mix", 0.3f}, {"Par A Time", -4.98f}, {"Par B Time", -4.98f}, {"Par A Feedback", 0.4f}, {"Par B Feedback", 0.4f}, {"Par A Pan", -0.25f}, {"Par B Pan", 0.25f}});
    add("Eighth Note", "Basic Delays", "Straight eighth-note echoes in the centre, short tail. Tight and rhythmic.", {{"Par Mix", 0.28f}, {"Par A Time", -12.f}, {"Par B Time", -12.f}, {"Par A Feedback", 0.35f}, {"Par B Feedback", 0.35f}});
    add("Wide Eighths", "Basic Delays", "A on eighths hard left, B on quarters hard right: a wide ping-pong without a ping-pong mode.", {{"Par Mix", 0.32f}, {"Par A Time", -12.f}, {"Par B Time", 0.f}, {"Par A Feedback", 0.3f}, {"Par B Feedback", 0.45f}, {"Par A Pan", -1.f}, {"Par B Pan", 1.f}});
    add("Triplet Quarter", "Basic Delays", "Quarter-note triplets, slightly spread. Rolling echoes that sit across the beat.", {{"Par Mix", 0.3f}, {"Par A Time", -7.02f}, {"Par B Time", -7.02f}, {"Par A Feedback", 0.4f}, {"Par B Feedback", 0.4f}, {"Par A Pan", -0.3f}, {"Par B Pan", 0.3f}});
    add("Slapback", "Basic Delays", "One short echo per side (about 110 and 120 ms at 120 BPM), almost no feedback: the 1950s vocal and guitar slap.", {{"Par Mix", 0.3f}, {"Par A Time Sync", 0}, {"Par B Time Sync", 0}, {"Par A Time", -26.2f}, {"Par B Time", -24.5f}, {"Par A Feedback", 0.08f}, {"Par B Feedback", 0.08f}, {"Par A Pan", -0.15f}, {"Par B Pan", 0.15f}});
    add("Doubler", "Basic Delays", "Two very short echoes (about 31 and 37 ms at 120 BPM) hard left and right, no feedback: makes a mono source sound double-tracked and wide.", {{"Par Mix", 0.4f}, {"Par A Time Sync", 0}, {"Par B Time Sync", 0}, {"Par A Time", -48.f}, {"Par B Time", -45.f}, {"Par A Feedback", 0.f}, {"Par B Feedback", 0.f}, {"Par A Pan", -1.f}, {"Par B Pan", 1.f}});
    add("Tape Echo", "Basic Delays", "Dotted eighths through the tape model: saturated, darker every repeat and gently wobbling.", {{"Par Mix", 0.33f}, {"Par A Time", -4.98f}, {"Par B Time", -4.98f}, {"Par A Feedback", 0.5f}, {"Par B Feedback", 0.5f}, {"DLD Memory", 2}, {"Tape Drive", 0.4f}, {"Tape Saturation", 0.35f}, {"Loop High Cut", 12.f}, {"Sway", 0.25f}, {"Sway Depth", 0.3f}});
    add("Dub Delay", "Basic Delays", "High feedback (70-75 %) through a resonant ladder filter in the loop and a little drive: long dub tails that build and darken.", {{"Par Mix", 0.4f}, {"Par A Time", -4.98f}, {"Par B Time", 0.f}, {"Par A Feedback", 0.75f}, {"Par B Feedback", 0.7f}, {"Par A Pan", -0.4f}, {"Par B Pan", 0.4f}, {"Lead Filter Type", ladder}, {"Lead Filter Place", 2}, {"Lead Filter Cutoff", 22.f}, {"Lead Filter Reso", 0.3f}, {"Par Lead Drive", 0.2f}});
    add("Half-Note Wash", "Basic Delays", "Long half-note and dotted echoes, darker and spread wide, 55-60 % feedback: a wash under pads and slow parts.", {{"Par Mix", 0.4f}, {"Par A Time", 12.f}, {"Par B Time", 19.02f}, {"Par A Feedback", 0.6f}, {"Par B Feedback", 0.55f}, {"Par A Tone", 0.35f}, {"Par B Tone", 0.35f}, {"Par A Pan", -0.5f}, {"Par B Pan", 0.5f}});
    (void)svf;
    return p;
}
