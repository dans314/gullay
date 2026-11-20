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

    // ===== Front Porch: warm, plain, close to the dry sound
    add("Screen Door", "Front Porch",
        {{"Par Mix", 0.32f}, {"Par A Feedback", 0.38f}, {"Par B Feedback", 0.3f}, {"Par A Tone", 0.42f}, {"Par B Tone", 0.38f},
         {"Tilt Out", -2.f}, {"Sway", 0.18f}, {"Sway Depth", 0.35f}, {"Sway Rate", -1.5f}});
    add("Rocking Chair", "Front Porch",
        {{"Slot 1", 1}, {"Tapo Pattern", 2}, {"Par Lead Feedback", 0.55f}, {"Par Mix", 0.35f}, {"Par Lead Tone", 0.45f},
         {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 5}, {"LFO 1 Target", 9}, {"LFO 1 Amount", 0.25f}});
    add("Lemonade Slap", "Front Porch",
        {{"Par A Time", -24.f}, {"Par B Time", -19.02f}, {"Par A Feedback", 0.12f}, {"Par B Feedback", 0.1f}, {"Par Mix", 0.3f},
         {"Par A Pan", -0.6f}, {"Par B Pan", 0.6f}, {"Tilt Out", 1.5f}});
    add("Porch Light", "Front Porch",
        {{"Par Mix", 0.38f}, {"Par A Feedback", 0.42f}, {"Par B Feedback", 0.42f}, {"Tune On", 1}, {"Tune Root", 7}, {"Tune Scale", 9},
         {"Tune Amount", 0.2f}, {"B Shift", 7}, {"B Shift Mode", 1}, {"Reverb On", 1}, {"Reverb Send", -20.f}, {"Reverb Level", -10.f},
         {"Reverb Size", 0.45f}, {"Reverb Decay", 0.5f}});
    add("Sweet Tea Tape", "Front Porch",
        {{"DLD Degrade", 14}, {"Par Mix", 0.36f}, {"Par Lead Drive", 0.25f}, {"Loop High Cut", 12.5f}, {"Par A Feedback", 0.5f},
         {"Par B Feedback", 0.45f}, {"Sway", 0.3f}, {"Sway Depth", 0.5f}, {"Sway Rate", -1.f}});

    // ===== Ring Shout: rhythm, answers, patterns that move
    add("Call and Response", "Ring Shout",
        {{"One Engine: Part B", 1}, {"Answer After", 1}, {"Answer Chance", 0.8f}, {"Par Mix", 0.45f}, {"Par A Feedback", 0.35f},
         {"Par B Feedback", 0.5f}, {"Par B Tone", 0.35f}, {"Par A Pan", -0.5f}, {"Par B Pan", 0.5f}});
    add("Clap Line", "Ring Shout",
        {{"Slot 1", 1}, {"Tapo Pattern", 7}, {"Par Lead Feedback", 0.45f}, {"Par Mix", 0.42f},
         {"Seq Lead Target", 11}, {"Seq Lead Euclid", 2}, {"Seq Lead Hits", 5}, {"Seq Lead Length", 16}, {"Seq Lead Depth", 1},
         {"Seq Lead Step 1", -0.6f}, {"Seq Lead Step 4", -0.6f}, {"Seq Lead Step 7", -0.6f}, {"Seq Lead Step 11", -0.6f}, {"Seq Lead Step 14", -0.6f}});
    add("Shout Back", "Ring Shout",
        {{"Slot 2", 2}, {"Tapo Pattern", 9}, {"Par Mix", 0.45f}, {"Par A Feedback", 0.4f}, {"Par B Feedback", 0.5f},
         {"Chaperone Influence", 0.55f}, {"Chaperone Grid", 5}, {"Chaperone Chaos", 0.35f}, {"Chaperone Range", 7.f}, {"Chaperone Snap", 2}});
    add("Stomp Patterns", "Ring Shout",
        {{"Slot 1", 1}, {"Tapo Morph", 7.6f}, {"Par Lead Feedback", 0.5f}, {"Par Mix", 0.4f},
         {"LFO 1 Shape", 9}, {"LFO 1 Sync Rate", 4}, {"LFO 1 Target", 40}, {"LFO 1 Amount", 0.6f}, {"LFO 1 Output", 1}});
    add("Mirror Dance", "Ring Shout",
        {{"One Engine: Part B", 2}, {"Mirror Time", 0.5f}, {"Par Mix", 0.42f}, {"Par A Feedback", 0.45f}, {"Par B Feedback", 0.4f},
         {"Par A Pan", -0.7f}, {"Par B Pan", 0.7f}, {"LFO 2 Shape", 2}, {"LFO 2 Sync Rate", 6}, {"LFO 2 Target", 6}, {"LFO 2 Amount", 0.5f}});

    // ===== Low Country: dust, bits, runaway loops
    add("Marsh Tape", "Low Country",
        {{"DLD Degrade", 11}, {"Par Lead Drive", 0.4f}, {"Loop High Cut", 11.8f}, {"Loop Low Cut", 7.3f}, {"Par Mix", 0.42f},
         {"Par A Feedback", 0.6f}, {"Par B Feedback", 0.55f}, {"Sway", 0.35f}, {"Sway Depth", 0.6f}});
    add("Kilobyte Haze", "Low Country",
        {{"Par A Feedback", 1.06f}, {"Par B Feedback", 0.9f}, {"Runaway Tame", 0.72f}, {"Runaway Ceiling", -18.f}, {"Par Lead Drive", 0.5f},
         {"Crusher Bits", 8}, {"Crusher Mix", 0.55f}, {"Crusher Place", 0}, {"Par Mix", 0.4f}});
    add("Ensoniq Dust", "Low Country",
        {{"DLD Degrade", 12}, {"Crusher Rate", 14.f}, {"Crusher Mix", 0.7f}, {"Crusher Steps", 1}, {"Par Mix", 0.4f},
         {"Par A Feedback", 0.5f}, {"Par B Feedback", 0.5f}, {"Tilt Out", -3.f}});
    add("Spanish Moss", "Low Country",
        {{"Slot 1", 1}, {"Tapo Pattern", 14}, {"Filter Type", ladder}, {"Filter Place", 2}, {"Filter Cutoff", 30.f}, {"Filter Reso", 0.45f},
         {"Par Lead Feedback", 0.65f}, {"Par Mix", 0.45f}, {"LFO 1 Shape", 1}, {"LFO 1 Sync Rate", 6}, {"LFO 1 Target", 35}, {"LFO 1 Amount", 0.45f}});
    add("Chip Ghost", "Low Country",
        {{"Chip Mode", 1}, {"Crusher Bits", 6}, {"Crusher Mix", 0.8f}, {"Crusher Place", 1}, {"Par Mix", 0.38f},
         {"Par A Time", -12.f}, {"Par A Feedback", 0.55f}, {"Par B Feedback", 0.45f}});

    // ===== Deep Water: long, wide, wet
    add("Blackwater", "Deep Water",
        {{"Slot 2", 1}, {"Wiring", 1}, {"Ser Mix", 0.5f}, {"Ser A Feedback", 0.6f}, {"Ser B Feedback", 0.55f}, {"Ser B Time", 12.f},
         {"Reverb On", 1}, {"Reverb Flavour", 1}, {"Reverb Size", 0.9f}, {"Reverb Decay", 0.88f}, {"Reverb Send", -6.f}, {"Reverb Level", -4.f}});
    add("Ogeechee Night", "Deep Water",
        {{"Par Mix", 0.5f}, {"Par A Feedback", 0.62f}, {"Par B Feedback", 0.6f}, {"River Meander", 0.85f}, {"River Spread", 0.4f},
         {"Mod 1 Source", 9}, {"Mod 1 Target", 7}, {"Mod 1 Amount", 0.08f}, {"Mod 2 Source", 8}, {"Mod 2 Target", 3}, {"Mod 2 Amount", 0.35f},
         {"Reverb On", 1}, {"Reverb Size", 0.8f}, {"Reverb Decay", 0.8f}, {"Reverb Send", -10.f}});
    add("Hymn Below", "Deep Water",
        {{"Par Mix", 0.5f}, {"Tune On", 1}, {"Tune Root", 2}, {"Tune Scale", 13}, {"Lead Shift", 12}, {"Lead Shift Mode", 2}, {"Shift Settle", 1},
         {"Par A Feedback", 0.55f}, {"Par B Feedback", 0.5f}, {"Reverb On", 1}, {"Reverb Tuned", 1}, {"Reverb Follows Key", 1},
         {"Reverb Send", -8.f}, {"Reverb Decay", 0.85f}});
    add("Spectral Spiral", "Deep Water",
        {{"Slot 2", 2}, {"Parallel Split", 1}, {"Spectral Split By", 1}, {"Tapo Pattern", 20}, {"Par Mix", 0.5f},
         {"Par A Feedback", 0.7f}, {"Par B Feedback", 0.6f}, {"Filter Type", zplane}, {"Filter Place", 1}, {"Filter Cutoff", 40.f},
         {"Reverb On", 1}, {"Reverb Decay", 0.92f}, {"Reverb Send", -9.f}});
    add("Tidewater Cloud", "Deep Water",
        {{"Slot 1", 1}, {"Tapo Pattern", 22}, {"Par Lead Feedback", 0.7f}, {"Par Mix", 0.5f}, {"River Tide", 1},
         {"Mod 1 Source", 10}, {"Mod 1 Target", 8}, {"Mod 1 Amount", 0.25f},
         {"Reverb On", 1}, {"Reverb Flavour", 1}, {"Reverb Size", 1.f}, {"Reverb Decay", 0.93f}, {"Reverb Send", -5.f}, {"Reverb Level", -3.f}});
    (void)svf;
    return p;
}
