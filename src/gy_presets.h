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
};

// filterModel: index of a filter by name, supplied by the plugin (the catalogue is built at run time)
template <class ModelOf>
inline std::vector<TcPreset> gullayPresets(ModelOf modelOf)
{
    const float zplane = (float)modelOf("Z-Plane"), ladder = (float)modelOf("Ladder"), svf = (float)modelOf("Fast SVF");
    using V = std::vector<std::pair<const char *, float>>;
    std::vector<TcPreset> p;
    auto add = [&](const char *n, const char *g, V v) { p.push_back({n, g, std::move(v)}); };
    // the presets written for v0.1 start from v0.1's defaults: B on a dotted beat, A and B panned a little,
    // feedback 45 %, Mix 50 % (v0.3's defaults are a plain quarter-note delay)
    auto addV1 = [&](const char *n, const char *g, V v) {
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
        add(n, g, std::move(base));
    };

    // Init: everything at its default
    add("Init", "Start", {});


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

    // ===== Front Porch: warm, plain, close to the dry sound
    addV1("Screen Door", "Front Porch",
        {{"Par Mix", 0.32f}, {"Par A Feedback", 0.38f}, {"Par B Feedback", 0.3f}, {"Par A Tone", 0.42f}, {"Par B Tone", 0.38f},
         {"Tilt Out", -2.f}, {"Sway", 0.18f}, {"Sway Depth", 0.35f}, {"Sway Rate", -1.5f}});
    addV1("Rocking Chair", "Front Porch",
        {{"Engine 1", 1}, {"Juba Pattern", 2}, {"Par Lead Feedback", 0.55f}, {"Par Mix", 0.35f}, {"Par Lead Tone", 0.45f},
         {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 5}, {"LFO 1 Target", 9}, {"LFO 1 Amount", 0.25f}});
    addV1("Porch Light", "Front Porch",
        {{"Par Mix", 0.38f}, {"Par A Feedback", 0.42f}, {"Par B Feedback", 0.42f}, {"Tune On", 1}, {"Tune Root", 7}, {"Tune Scale", 9},
         {"Tune Amount", 0.2f}, {"B Shift", 7}, {"B Shift Mode", 1}});

    // ===== Ring Shout: rhythm, answers, patterns that move
    addV1("Call and Response", "Ring Shout",
        {{"Shadow", 1}, {"Answer After", 1}, {"Answer Chance", 0.8f}, {"Par Mix", 0.45f}, {"Par A Feedback", 0.35f},
         {"Par B Feedback", 0.5f}, {"Par B Tone", 0.35f}, {"Par A Pan", -0.5f}, {"Par B Pan", 0.5f}});
    addV1("Clap Line", "Ring Shout",
        {{"Engine 1", 1}, {"Juba Pattern", 7}, {"Par Lead Feedback", 0.45f}, {"Par Mix", 0.42f},
         {"Black Herman Influence", 0.5f}});
    addV1("Shout Back", "Ring Shout",
        {{"Engine 2", 2}, {"Juba Pattern", 9}, {"Par Mix", 0.45f}, {"Par A Feedback", 0.4f}, {"Par B Feedback", 0.5f},
         {"Black Herman Influence", 0.55f}, {"Black Herman Grid", 5}, {"Black Herman Chaos", 0.35f}, {"Black Herman Range", 7.f}, {"Black Herman Snap", 2}});
    addV1("Stomp Patterns", "Ring Shout",
        {{"Engine 1", 1}, {"Juba Morph", 7.6f}, {"Par Lead Feedback", 0.5f}, {"Par Mix", 0.4f},
         {"LFO 1 Shape", 9}, {"LFO 1 Sync Rate", 4}, {"LFO 1 Target", 40}, {"LFO 1 Amount", 0.6f}, {"LFO 1 Output", 1}});
    addV1("Mirror Dance", "Ring Shout",
        {{"Shadow", 2}, {"Mirror Time", 0.5f}, {"Par Mix", 0.42f}, {"Par A Feedback", 0.45f}, {"Par B Feedback", 0.4f},
         {"Par A Pan", -0.7f}, {"Par B Pan", 0.7f}, {"LFO 2 Shape", 2}, {"LFO 2 Sync Rate", 6}, {"LFO 2 Target", 6}, {"LFO 2 Amount", 0.5f}});

    // ===== Low Country: dust, bits, runaway loops
    addV1("Ensoniq Dust", "Low Country",
        {{"Crusher Bits", 12}, {"Memory Rate", 14.43f}, {"Crusher Mix", 0.7f}, {"Crusher Steps", 1}, {"Crusher Place", 0}, {"Memory Tilt", 0.3f}, {"Memory Noise", 0.1f}, {"Par Mix", 0.4f},
         {"Par A Feedback", 0.5f}, {"Par B Feedback", 0.5f}, {"Tilt Out", -3.f}});
    addV1("Spanish Moss", "Low Country",
        {{"Engine 1", 1}, {"Juba Pattern", 14}, {"Lead Filter Type", ladder}, {"Lead Filter Place", 2}, {"Lead Filter Cutoff", 30.f}, {"Lead Filter Reso", 0.45f},
         {"Par Lead Feedback", 0.65f}, {"Par Mix", 0.45f}, {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 6}, {"LFO 1 Target", 35}, {"LFO 1 Amount", 0.45f}});
    addV1("Chip Ghost", "Low Country",
        {{"Chip Mode", 1}, {"Crusher Bits", 6}, {"Crusher Mix", 0.8f}, {"Crusher Place", 1}, {"Par Mix", 0.38f},
         {"Par A Time", -12.f}, {"Par A Feedback", 0.55f}, {"Par B Feedback", 0.45f}});

    // ===== Deep Water: long, wide, wet
    addV1("Blackwater", "Deep Water",
        {{"Engine 2", 1}, {"Wiring", 1}, {"Ser Mix", 0.5f}, {"Ser A Feedback", 0.6f}, {"Ser B Feedback", 0.55f}, {"Ser B Time", 12.f}});
    addV1("Spectral Spiral", "Deep Water",
        {{"Engine 2", 2}, {"Parallel Split", 1}, {"Spectral Split By", 1}, {"Juba Pattern", 20}, {"Par Mix", 0.5f},
         {"Par A Feedback", 0.7f}, {"Par B Feedback", 0.6f}, {"Lead Filter Type", zplane}, {"Lead Filter Place", 1}, {"Lead Filter Cutoff", 40.f}});
    addV1("Tidewater Cloud", "Deep Water",
        {{"Engine 1", 1}, {"Juba Pattern", 22}, {"Par Lead Feedback", 0.8f}, {"Juba Mod Amount", 0.25f}, {"Juba Mod Rate", 0.2f}, {"Par Mix", 0.5f}, {"River Tide", 1},
         {"Mod 1 Source", 10}, {"Mod 1 Target", 8}, {"Mod 1 Amount", 0.25f}});

    // ===== Tides (v0.5): spectral splits and loops that keep changing, each its own animal
    auto addT = [&](const char *n, const char *d, V v) { p.push_back({n, "Tides", std::move(v), d}); };
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
    add("Quarter Note", "Bread and Butter", {{"Par Mix", 0.3f}, {"Par A Feedback", 0.35f}, {"Par B Feedback", 0.35f}, {"Par A Tone", 0.45f}, {"Par B Tone", 0.45f}, {"Par A Pan", -0.2f}, {"Par B Pan", 0.2f}});
    add("Dotted Eighth", "Bread and Butter", {{"Par Mix", 0.3f}, {"Par A Time", -4.98f}, {"Par B Time", -4.98f}, {"Par A Feedback", 0.4f}, {"Par B Feedback", 0.4f}, {"Par A Pan", -0.25f}, {"Par B Pan", 0.25f}});
    add("Eighth Note", "Bread and Butter", {{"Par Mix", 0.28f}, {"Par A Time", -12.f}, {"Par B Time", -12.f}, {"Par A Feedback", 0.35f}, {"Par B Feedback", 0.35f}});
    add("Wide Eighths", "Bread and Butter", {{"Par Mix", 0.32f}, {"Par A Time", -12.f}, {"Par B Time", 0.f}, {"Par A Feedback", 0.3f}, {"Par B Feedback", 0.45f}, {"Par A Pan", -1.f}, {"Par B Pan", 1.f}});
    add("Triplet Quarter", "Bread and Butter", {{"Par Mix", 0.3f}, {"Par A Time", -7.02f}, {"Par B Time", -7.02f}, {"Par A Feedback", 0.4f}, {"Par B Feedback", 0.4f}, {"Par A Pan", -0.3f}, {"Par B Pan", 0.3f}});
    add("Slapback", "Bread and Butter", {{"Par Mix", 0.3f}, {"Par A Time Sync", 0}, {"Par B Time Sync", 0}, {"Par A Time", -26.2f}, {"Par B Time", -24.5f}, {"Par A Feedback", 0.08f}, {"Par B Feedback", 0.08f}, {"Par A Pan", -0.15f}, {"Par B Pan", 0.15f}});
    add("Doubler", "Bread and Butter", {{"Par Mix", 0.4f}, {"Par A Time Sync", 0}, {"Par B Time Sync", 0}, {"Par A Time", -48.f}, {"Par B Time", -45.f}, {"Par A Feedback", 0.f}, {"Par B Feedback", 0.f}, {"Par A Pan", -1.f}, {"Par B Pan", 1.f}});
    add("Tape Echo", "Bread and Butter", {{"Par Mix", 0.33f}, {"Par A Time", -4.98f}, {"Par B Time", -4.98f}, {"Par A Feedback", 0.5f}, {"Par B Feedback", 0.5f}, {"DLD Memory", 2}, {"Tape Drive", 0.4f}, {"Tape Saturation", 0.35f}, {"Loop High Cut", 12.f}, {"Sway", 0.25f}, {"Sway Depth", 0.3f}});
    add("Dub Delay", "Bread and Butter", {{"Par Mix", 0.4f}, {"Par A Time", -4.98f}, {"Par B Time", 0.f}, {"Par A Feedback", 0.75f}, {"Par B Feedback", 0.7f}, {"Par A Pan", -0.4f}, {"Par B Pan", 0.4f}, {"Lead Filter Type", ladder}, {"Lead Filter Place", 2}, {"Lead Filter Cutoff", 22.f}, {"Lead Filter Reso", 0.3f}, {"Par Lead Drive", 0.2f}});
    add("Half-Note Wash", "Bread and Butter", {{"Par Mix", 0.4f}, {"Par A Time", 12.f}, {"Par B Time", 19.02f}, {"Par A Feedback", 0.6f}, {"Par B Feedback", 0.55f}, {"Par A Tone", 0.35f}, {"Par B Tone", 0.35f}, {"Par A Pan", -0.5f}, {"Par B Pan", 0.5f}});
    (void)svf;
    return p;
}
