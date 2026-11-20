// Gullay's factory presets: 20 in four groups (Front Porch, Ring Shout, Low Country, Deep Water).
// Each one names only what it changes; every other control starts from its default.
// Values are the parameters' own units (dB, semitones, 0..1, list positions), as the host sees them.
//
// Mod targets (list positions): 1-6 A Time/Feedback/Tone/Drive/Level/Pan, 7-12 Lead, 13-18 B,
// 19 Spectral Split, 20 Balance, 21 Runaway Tame, 22 Crusher Mix, 23 Mix, 35 Filter Cutoff, 36 Filter Reso,
// 37 Filter Morph, 38 Tilt In, 39 Tilt Out, 40 Tapo Pattern, 41 Tapo Morph, 42 DLD Degrade, 43 Crusher Bits,
// 44 Crusher Rate, 45-49 Reverb Size/Decay/Level/Mod/Send, 50-52 A/Lead/B Shift, 53 Runaway Ceiling.
// Mod sources: 1-3 Seq A/Lead/B, 4-6 LFO 1-3, 7 Follower, 8-10 River A/Lead/B.
#pragma once
#include <deque>
#include <string>
#include <utility>
#include <vector>

struct TcPreset
{
    const char *name, *group;
    std::vector<std::pair<const char *, float>> values;
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

    // ===== Front Porch: warm, plain, close to the dry sound
    addV1("Screen Door", "Front Porch",
        {{"Par Mix", 0.32f}, {"Par A Feedback", 0.38f}, {"Par B Feedback", 0.3f}, {"Par A Tone", 0.42f}, {"Par B Tone", 0.38f},
         {"Tilt Out", -2.f}, {"Sway", 0.18f}, {"Sway Depth", 0.35f}, {"Sway Rate", -1.5f}});
    addV1("Rocking Chair", "Front Porch",
        {{"Slot 1", 1}, {"Tapo Pattern", 2}, {"Par Lead Feedback", 0.55f}, {"Par Mix", 0.35f}, {"Par Lead Tone", 0.45f},
         {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 5}, {"LFO 1 Target", 9}, {"LFO 1 Amount", 0.25f}});
    addV1("Lemonade Slap", "Front Porch",
        {{"Par A Time", -24.f}, {"Par B Time", -19.02f}, {"Par A Feedback", 0.12f}, {"Par B Feedback", 0.1f}, {"Par Mix", 0.3f},
         {"Par A Pan", -0.6f}, {"Par B Pan", 0.6f}, {"Tilt Out", 1.5f}});
    addV1("Porch Light", "Front Porch",
        {{"Par Mix", 0.38f}, {"Par A Feedback", 0.42f}, {"Par B Feedback", 0.42f}, {"Tune On", 1}, {"Tune Root", 7}, {"Tune Scale", 9},
         {"Tune Amount", 0.2f}, {"B Shift", 7}, {"B Shift Mode", 1}});
    addV1("Sweet Tea Tape", "Front Porch",
        {{"DLD Degrade", 14}, {"Par Mix", 0.36f}, {"Par Lead Drive", 0.25f}, {"Loop High Cut", 12.5f}, {"Par A Feedback", 0.5f},
         {"Par B Feedback", 0.45f}, {"Sway", 0.3f}, {"Sway Depth", 0.5f}, {"Sway Rate", -1.f}});

    // ===== Ring Shout: rhythm, answers, patterns that move
    addV1("Call and Response", "Ring Shout",
        {{"One Engine: Part B", 1}, {"Answer After", 1}, {"Answer Chance", 0.8f}, {"Par Mix", 0.45f}, {"Par A Feedback", 0.35f},
         {"Par B Feedback", 0.5f}, {"Par B Tone", 0.35f}, {"Par A Pan", -0.5f}, {"Par B Pan", 0.5f}});
    addV1("Clap Line", "Ring Shout",
        {{"Slot 1", 1}, {"Tapo Pattern", 7}, {"Par Lead Feedback", 0.45f}, {"Par Mix", 0.42f},
         {"Seq Lead Target", 11}, {"Seq Lead Euclid", 2}, {"Seq Lead Hits", 5}, {"Seq Lead Length", 16}, {"Seq Lead Depth", 1},
         {"Seq Lead Step 1", -0.6f}, {"Seq Lead Step 4", -0.6f}, {"Seq Lead Step 7", -0.6f}, {"Seq Lead Step 11", -0.6f}, {"Seq Lead Step 14", -0.6f}});
    addV1("Shout Back", "Ring Shout",
        {{"Slot 2", 2}, {"Tapo Pattern", 9}, {"Par Mix", 0.45f}, {"Par A Feedback", 0.4f}, {"Par B Feedback", 0.5f},
         {"Chaperone Influence", 0.55f}, {"Chaperone Grid", 5}, {"Chaperone Chaos", 0.35f}, {"Chaperone Range", 7.f}, {"Chaperone Snap", 2}});
    addV1("Stomp Patterns", "Ring Shout",
        {{"Slot 1", 1}, {"Tapo Morph", 7.6f}, {"Par Lead Feedback", 0.5f}, {"Par Mix", 0.4f},
         {"LFO 1 Shape", 9}, {"LFO 1 Sync Rate", 4}, {"LFO 1 Target", 40}, {"LFO 1 Amount", 0.6f}, {"LFO 1 Output", 1}});
    addV1("Mirror Dance", "Ring Shout",
        {{"One Engine: Part B", 2}, {"Mirror Time", 0.5f}, {"Par Mix", 0.42f}, {"Par A Feedback", 0.45f}, {"Par B Feedback", 0.4f},
         {"Par A Pan", -0.7f}, {"Par B Pan", 0.7f}, {"LFO 2 Shape", 2}, {"LFO 2 Sync Rate", 6}, {"LFO 2 Target", 6}, {"LFO 2 Amount", 0.5f}});

    // ===== Low Country: dust, bits, runaway loops
    addV1("Marsh Tape", "Low Country",
        {{"DLD Degrade", 11}, {"Par Lead Drive", 0.4f}, {"Loop High Cut", 11.8f}, {"Loop Low Cut", 7.3f}, {"Par Mix", 0.42f},
         {"Par A Feedback", 0.6f}, {"Par B Feedback", 0.55f}, {"Sway", 0.35f}, {"Sway Depth", 0.6f}});
    addV1("Kilobyte Haze", "Low Country",
        {{"Par A Feedback", 1.06f}, {"Par B Feedback", 0.9f}, {"Runaway Tame", 0.72f}, {"Runaway Ceiling", -18.f}, {"Par Lead Drive", 0.5f},
         {"Crusher Bits", 8}, {"Crusher Mix", 0.55f}, {"Crusher Place", 0}, {"Par Mix", 0.4f}});
    addV1("Ensoniq Dust", "Low Country",
        {{"DLD Degrade", 12}, {"Crusher Rate", 14.f}, {"Crusher Mix", 0.7f}, {"Crusher Steps", 1}, {"Par Mix", 0.4f},
         {"Par A Feedback", 0.5f}, {"Par B Feedback", 0.5f}, {"Tilt Out", -3.f}});
    addV1("Spanish Moss", "Low Country",
        {{"Slot 1", 1}, {"Tapo Pattern", 14}, {"Lead Filter Type", ladder}, {"Lead Filter Place", 2}, {"Lead Filter Cutoff", 30.f}, {"Lead Filter Reso", 0.45f},
         {"Par Lead Feedback", 0.65f}, {"Par Mix", 0.45f}, {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 6}, {"LFO 1 Target", 35}, {"LFO 1 Amount", 0.45f}});
    addV1("Chip Ghost", "Low Country",
        {{"Chip Mode", 1}, {"Crusher Bits", 6}, {"Crusher Mix", 0.8f}, {"Crusher Place", 1}, {"Par Mix", 0.38f},
         {"Par A Time", -12.f}, {"Par A Feedback", 0.55f}, {"Par B Feedback", 0.45f}});

    // ===== Deep Water: long, wide, wet
    addV1("Blackwater", "Deep Water",
        {{"Slot 2", 1}, {"Wiring", 1}, {"Ser Mix", 0.5f}, {"Ser A Feedback", 0.6f}, {"Ser B Feedback", 0.55f}, {"Ser B Time", 12.f}});
    addV1("Ogeechee Night", "Deep Water",
        {{"Par Mix", 0.5f}, {"Par A Feedback", 0.62f}, {"Par B Feedback", 0.6f}, {"River Meander", 0.85f}, {"River Spread", 0.4f},
         {"Mod 1 Source", 9}, {"Mod 1 Target", 7}, {"Mod 1 Amount", 0.08f}, {"Mod 2 Source", 8}, {"Mod 2 Target", 3}, {"Mod 2 Amount", 0.35f}});
    addV1("Hymn Below", "Deep Water",
        {{"Par Mix", 0.5f}, {"Tune On", 1}, {"Tune Root", 2}, {"Tune Scale", 13}, {"Lead Shift", 12}, {"Lead Shift Mode", 2}, {"Shift Settle", 1},
         {"Par A Feedback", 0.55f}, {"Par B Feedback", 0.5f}});
    addV1("Spectral Spiral", "Deep Water",
        {{"Slot 2", 2}, {"Parallel Split", 1}, {"Spectral Split By", 1}, {"Tapo Pattern", 20}, {"Par Mix", 0.5f},
         {"Par A Feedback", 0.7f}, {"Par B Feedback", 0.6f}, {"Lead Filter Type", zplane}, {"Lead Filter Place", 1}, {"Lead Filter Cutoff", 40.f}});
    addV1("Tidewater Cloud", "Deep Water",
        {{"Slot 1", 1}, {"Tapo Pattern", 22}, {"Par Lead Feedback", 0.8f}, {"Tapo Mod Amount", 0.25f}, {"Tapo Mod Rate", 0.2f}, {"Par Mix", 0.5f}, {"River Tide", 1},
         {"Mod 1 Source", 10}, {"Mod 1 Target", 8}, {"Mod 1 Amount", 0.25f}});

    // ===== Bread and Butter: the everyday delays
    add("Quarter Note", "Bread and Butter", {{"Par Mix", 0.3f}, {"Par A Feedback", 0.35f}, {"Par B Feedback", 0.35f}, {"Par A Tone", 0.45f}, {"Par B Tone", 0.45f}, {"Par A Pan", -0.2f}, {"Par B Pan", 0.2f}});
    add("Dotted Eighth", "Bread and Butter", {{"Par Mix", 0.3f}, {"Par A Time", -4.98f}, {"Par B Time", -4.98f}, {"Par A Feedback", 0.4f}, {"Par B Feedback", 0.4f}, {"Par A Pan", -0.25f}, {"Par B Pan", 0.25f}});
    add("Eighth Note", "Bread and Butter", {{"Par Mix", 0.28f}, {"Par A Time", -12.f}, {"Par B Time", -12.f}, {"Par A Feedback", 0.35f}, {"Par B Feedback", 0.35f}});
    add("Wide Eighths", "Bread and Butter", {{"Par Mix", 0.32f}, {"Par A Time", -12.f}, {"Par B Time", 0.f}, {"Par A Feedback", 0.3f}, {"Par B Feedback", 0.45f}, {"Par A Pan", -1.f}, {"Par B Pan", 1.f}});
    add("Triplet Quarter", "Bread and Butter", {{"Par Mix", 0.3f}, {"Par A Time", -7.02f}, {"Par B Time", -7.02f}, {"Par A Feedback", 0.4f}, {"Par B Feedback", 0.4f}, {"Par A Pan", -0.3f}, {"Par B Pan", 0.3f}});
    add("Slapback", "Bread and Butter", {{"Par Mix", 0.3f}, {"Par A Time Sync", 0}, {"Par B Time Sync", 0}, {"Par A Time", -26.2f}, {"Par B Time", -24.5f}, {"Par A Feedback", 0.08f}, {"Par B Feedback", 0.08f}, {"Par A Pan", -0.15f}, {"Par B Pan", 0.15f}});
    add("Doubler", "Bread and Butter", {{"Par Mix", 0.4f}, {"Par A Time Sync", 0}, {"Par B Time Sync", 0}, {"Par A Time", -48.f}, {"Par B Time", -45.f}, {"Par A Feedback", 0.f}, {"Par B Feedback", 0.f}, {"Par A Pan", -1.f}, {"Par B Pan", 1.f}});
    add("Tape Echo", "Bread and Butter", {{"Par Mix", 0.33f}, {"Par A Time", -4.98f}, {"Par B Time", -4.98f}, {"Par A Feedback", 0.5f}, {"Par B Feedback", 0.5f}, {"DLD Degrade", 13}, {"Loop High Cut", 12.f}, {"Par Lead Drive", 0.2f}, {"Sway", 0.25f}, {"Sway Depth", 0.3f}});
    add("Dub Delay", "Bread and Butter", {{"Par Mix", 0.4f}, {"Par A Time", -4.98f}, {"Par B Time", 0.f}, {"Par A Feedback", 0.75f}, {"Par B Feedback", 0.7f}, {"Par A Pan", -0.4f}, {"Par B Pan", 0.4f}, {"Lead Filter Type", ladder}, {"Lead Filter Place", 2}, {"Lead Filter Cutoff", 22.f}, {"Lead Filter Reso", 0.3f}, {"Par Lead Drive", 0.2f}});
    add("Half-Note Wash", "Bread and Butter", {{"Par Mix", 0.4f}, {"Par A Time", 12.f}, {"Par B Time", 19.02f}, {"Par A Feedback", 0.6f}, {"Par B Feedback", 0.55f}, {"Par A Tone", 0.35f}, {"Par B Tone", 0.35f}, {"Par A Pan", -0.5f}, {"Par B Pan", 0.5f}});
    (void)svf;
    return p;
}
