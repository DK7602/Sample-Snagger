#pragma once

#include "AudioData.h"
#include <vector>

namespace snag::key
{

/** A musical key: tonic 0..11 (C..B) and major / minor. tonic < 0 = unknown. */
struct Key
{
    int tonic = -1;
    bool minor = false;
    float confidence = 0.0f;     // 0..1 (how clearly this key beats the others)
    float tuningCents = 0.0f;    // how far the recording sits from A = 440 Hz
    std::vector<std::pair<int, bool>> candidates;   // the best few (tonic, minor), best first

    bool isValid() const noexcept                   { return tonic >= 0 && tonic < 12; }
    bool operator== (const Key& o) const noexcept   { return tonic == o.tonic && minor == o.minor; }

    /** The key after shifting by some semitones. */
    Key transposed (int semitones) const noexcept;
};

/** Finds the key of a recording from its harmony: a tuning-aware chroma of the sustained sound
    (short hits are median-filtered away, so drums don't throw it) between 50 Hz and 2 kHz,
    matched against Temperley's key profiles, with a small bonus when the key's root is the
    note the bass keeps returning to. Related keys (relative minor, a fifth away) can still
    swap places on ambiguous material - `candidates` lists the runners-up. */
Key detect (const AudioData&);

/** "Am", "F#m", "Bb", "C"... (the usual spellings) */
juce::String name (const Key&);
/** "A minor" / "B flat major" */
juce::String longName (const Key&);
/** DJ-style Camelot code: "8A" (A minor), "8B" (C major)... */
juce::String camelot (const Key&);

/** Smallest shift (-6..+5 semitones) that takes `from` to the tonic `toTonic` (same mode). */
int semitonesTo (const Key& from, int toTonic);

} // namespace snag::key
