// Gullay: the two reverb flavours behind one set of controls.
//   Plateau: Valley Audio's Dattorro tank (ValleyRackFree 86f02e43, GPL-3.0-or-later,
//            (c) 2020 Valley Audio Soft, Dale Johnson), the core only; the controls are mapped
//            here the way Valley's Plateau module maps its knobs.
//   Cloud Seed: Ghost Note Audio's Cloud Seed core (CloudSeedCore deb21ded, MIT).
// Both are built off the audio thread (activate) and never allocate while processing.
// Copyright 2026 Otis Kanno. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <cmath>
#include <memory>

struct GyReverbSettings
{
    int flavour{0};        // 0 Plateau, 1 Cloud Seed
    float size{0.7f};      // 0..1
    float decay{0.75f};    // 0..1
    float preDelay{0.f};   // 0..1 (0..500 ms)
    float diffusion{0.7f}; // 0..1
    float modDepth{0.3f}, modRate{0.3f}, modShape{0.5f}; // 0..1
    float lowCut{0.1f}, highCut{0.8f};                   // 0..1 (pitch-like: 13.75 Hz .. 14 kHz)
    bool freeze{false};
    bool tuned{false};     // Plateau's tuned mode: small, pitched tanks
    float tunedSemis{0};   // Hymn's key root as an offset from A (tuned mode follows the key)
};

namespace otiskanno::gullay
{
class GyReverb
{
  public:
    GyReverb();
    ~GyReverb();
    void setSampleRate(double sr); // not real time
    void clear();
    void set(const GyReverbSettings &s);
    // a block of n (n <= 64)
    void process(const float *inL, const float *inR, float *outL, float *outR, int n);
    int flavour() const { return cur; }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int cur{0};
};
} // namespace otiskanno::gullay
