#pragma once

// Randomize and mutate (design §M6). Pure functions of their inputs and an
// explicitly seeded generator, so a session that does the same things gets
// the same settings (design §5.2).

#include "preset.hpp"

#include <cstdint>

namespace studio {

// splitmix64: small, fast, fully specified.
class ExploreRng {
public:
    explicit ExploreRng(uint64_t seed = 0x5EED5EED5EEDull) : state_(seed) {}
    uint64_t next();
    double uniform();                    // [0, 1)
    int range(int lo, int hi);           // inclusive
    bool chance(double p) { return uniform() < p; }
    uint64_t state() const { return state_; }

private:
    uint64_t state_;
};

// New encode settings within ranges that give a usable image (see explore.cpp
// for each parameter's range and why). Keeps the encode seed.
void randomizeEncode(glic::EncodeSettings& s, ExploreRng& rng);

// Randomizes the encode settings and, when decoding is unlinked, the decode
// side as a perturbation of the encode side (so it stays a recognisable
// mismatch). Corruption is left alone: C and N own it.
void randomize(glic::preset::Preset& p, ExploreRng& rng);

// A perturbation of `p`. strength 0..1: 0 changes nothing; small values nudge
// numbers and rarely switch a choice; 1 changes most things. Channels that
// were identical stay identical.
glic::preset::Preset mutate(const glic::preset::Preset& p, float strength, ExploreRng& rng);

} // namespace studio
