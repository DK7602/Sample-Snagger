#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

// Plain settings a Clip carries (no DSP here, so AudioData.h can include this).
namespace snag
{

//==============================================================================
/** Per-pad (per-chop) sound settings. */
struct PadParams
{
    float gainDb = 0.0f;        // -24 .. +12 dB
    float semitones = 0.0f;     // -24 .. +24, sampler style (speed changes with pitch, like an MPC)
    bool reverse = false;
    float attackMs = 1.0f;      // 0 .. 2000 ms fade-in
    float releaseMs = 10.0f;    // 1 .. 4000 ms: fade at the chop's end / after note-off (gated)
    float filter = 0.0f;        // -1 .. +1: below 0 low-pass (more = darker), above 0 high-pass, 0 = off

    bool isDefault() const noexcept { return *this == PadParams(); }
    bool operator== (const PadParams& o) const noexcept
    {
        return std::abs (gainDb - o.gainDb) < 0.001f && std::abs (semitones - o.semitones) < 0.001f && reverse == o.reverse
            && std::abs (attackMs - o.attackMs) < 0.01f && std::abs (releaseMs - o.releaseMs) < 0.01f && std::abs (filter - o.filter) < 0.0001f;
    }
    bool operator!= (const PadParams& o) const noexcept { return ! (*this == o); }

    juce::String toString() const;
    static PadParams fromString (const juce::String&);
};

//==============================================================================
/** The FX rack: LO-FI -> DRIVE -> DELAY -> REVERB. Each section has a power switch; the knob
    values are kept while a section is off. */
struct FxSettings
{
    bool lofiOn = false;
    float bits = 10.0f;         // 4 .. 16
    float rateKHz = 18.0f;      // 2 .. 44 (sample-rate reduction)
    float vinyl = 0.5f;         // 0 .. 1: crackle, hiss, wow & flutter, worn-out tone

    bool driveOn = false;
    float drive = 0.4f;         // 0 .. 1 (0 .. 30 dB into a warm tube-ish curve)
    float tone = 0.6f;          // 0 dark .. 1 bright

    bool delayOn = false;
    int division = 6;           // index into delayDivisions() (1/8 dotted); synced to the tempo
    float feedback = 0.35f;     // 0 .. 0.9
    float delayMix = 0.3f;      // 0 .. 1

    bool reverbOn = false;
    float size = 0.55f;         // 0 .. 1
    float reverbMix = 0.25f;    // 0 .. 1

    bool anyOn() const noexcept { return lofiOn || driveOn || delayOn || reverbOn; }
    bool operator== (const FxSettings& o) const noexcept { return toString() == o.toString(); }
    bool operator!= (const FxSettings& o) const noexcept { return ! (*this == o); }

    juce::String toString() const;
    static FxSettings fromString (const juce::String&);

    /** How long the echoes / reverb keep ringing after the sound stops (seconds). */
    double tailSeconds (double bpm) const;
};

struct DelayDivision { const char* name; double beats; };
const std::vector<DelayDivision>& delayDivisions();

} // namespace snag
