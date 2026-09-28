#pragma once

#include "AudioData.h"
#include <vector>

/** Offline, non-realtime edit operations. Every function returns a brand new AudioData. */
namespace snag::edit
{
    AudioData::Ptr crop        (const AudioData&, int start, int end);
    AudioData::Ptr removeRange (const AudioData&, int start, int end);
    AudioData::Ptr silence     (const AudioData&, int start, int end);
    AudioData::Ptr fade        (const AudioData&, int start, int end, bool fadeIn);
    AudioData::Ptr normalize   (const AudioData&, int start, int end, float targetDb = -0.3f);
    AudioData::Ptr reverse     (const AudioData&, int start, int end);
    AudioData::Ptr gain        (const AudioData&, int start, int end, float dB);
    AudioData::Ptr toMono      (const AudioData&);
    AudioData::Ptr filter      (const AudioData&, float lowCutHz, float highCutHz);   // 0 = off

    /** High quality pitch-shift + time-stretch (Signalsmith Stretch).
        lengthRatio > 1 makes it longer. tape = true does old-school varispeed instead
        (pitch and speed change together, lengthRatio ignored). */
    AudioData::Ptr pitchTime   (const AudioData&, float semitones, double lengthRatio,
                                bool keepFormants, bool tape);

    AudioData::Ptr resample    (const AudioData&, double newSampleRate);

    /** The STUDIO knobs applied to the original: pitch / length, then gain, then low / high cut. */
    AudioData::Ptr renderAdjust (const AudioData& original, const Clip::Adjust&);

    /** Sums several (equal-rate) layers with gains. */
    AudioData::Ptr mix         (const std::vector<AudioData::Ptr>& layers, const std::vector<float>& gains);

    //==============================================================================
    /** Onset detection -> slice points. sensitivity 0..1 (1 = more slices). */
    std::vector<int> detectTransients (const AudioData&, float sensitivity, int minGapMs = 70);

    std::vector<int> equalSlices (int numSamples, int numSlices);

    /** Rough tempo estimate from the onset envelope; returns 0 if unsure. */
    double estimateBpm (const AudioData&);

    float peakLevel (const AudioData&, int start = 0, int end = -1);

    /** Moves a position to the nearest zero crossing within +-maxDistance samples. */
    int snapToZeroCrossing (const AudioData&, int pos, int maxDistance);
}
