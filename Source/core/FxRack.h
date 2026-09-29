#pragma once

#include "AudioData.h"
#include "PadFx.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace snag
{

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
