#pragma once

#include "AudioData.h"
#include <functional>
#include <vector>

namespace snag
{

/** Built-in, instant stem splitter (no AI, no downloads).

    It works on the spectrogram:
      - vocals   = centre-panned, harmonic energy in the vocal band (stereo sources only)
      - drums    = percussive energy (harmonic/percussive median filtering)
      - bass     = harmonic energy below ~250 Hz
      - other    = everything that's left
    The stems always sum back to the original exactly. Quality is good for quick
    acapella / instrumental ideas from stereo mixes; use the AI engine for studio quality. */
class QuickSplit
{
public:
    struct Stem
    {
        juce::String name;      // "vocals", "music", "drums", "bass", "other", "melodic"
        AudioData::Ptr audio;
    };

    struct Result
    {
        std::vector<Stem> stems;
        juce::String error;
        juce::String note;      // e.g. "Mono source - vocals can't be isolated in Quick mode"
    };

    /** fourStems = false -> vocals + music. */
    static Result separate (const AudioData& input, bool fourStems,
                            std::function<void (float)> progress = {},
                            std::function<bool()> shouldCancel = {});

    /** True if the channels are (practically) identical. */
    static bool isEffectivelyMono (const AudioData&);
};

} // namespace snag
