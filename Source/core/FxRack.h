#pragma once

#include "AudioData.h"
#include "PadFx.h"
#include "SoundSettings.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace snag
{

//==============================================================================
/** Runs the FX rack on a stereo buffer. Real-time safe after prepare(). */
class FxChain
{
public:
    void prepare (double sampleRate);
    void reset();
    /** bpm: the tempo the delay locks to (host tempo, else the sample's, else 120). */
    void process (float* left, float* right, int numSamples, const FxSettings&, double bpm) noexcept;

private:
    double sr = 44100.0;

    // lo-fi
    float holdL = 0, holdR = 0, holdPhase = 1.0f;
    float clickEnv = 0, clickSign = 1, hissLp = 0, lpL = 0, lpR = 0, hpL = 0, hpR = 0, hpInL = 0, hpInR = 0;
    double wowPhase = 0, flutterPhase = 0;
    std::vector<float> wowBuf[2];
    int wowWrite = 0;
    juce::Random rng { 20260929 };

    // drive
    float toneL = 0, toneR = 0;

    // delay
    std::vector<float> dBuf[2];
    int dWrite = 0;
    double dTime = -1.0;        // smoothed delay time in samples
    float dampL = 0, dampR = 0;

    // reverb
    juce::Reverb reverb;
};

/** Renders a sound through the FX rack; with `withTail` the echoes / reverb ring out after it. */
AudioData::Ptr renderFx (const AudioData& in, const FxSettings&, double bpm, bool withTail = true);

} // namespace snag
