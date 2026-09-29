#pragma once

#include "AudioData.h"
#include "SoundSettings.h"
#include <cmath>

namespace snag
{

/** Low-pass / high-pass cutoff for a FILTER value, and how to show it ("LP 1.2k", "HP 300", "Off"). */
float filterCutoffHz (float filter) noexcept;
juce::String filterText (float filter);

//==============================================================================
/** Zavalishin / Cytomic TPT state-variable filter (stable under any cutoff), stereo. */
struct Svf
{
    enum Mode { lowPass, highPass };
    void set (float cutoffHz, float q, double sampleRate, Mode m) noexcept;
    void reset() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }
    float process (float x, int ch) noexcept
    {
        const float v3 = x - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch];
        ic2[ch] = 2.0f * v2 - ic2[ch];
        return mode == lowPass ? v2 : x - k * v1 - v2;
    }
    float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1[2] {}, ic2[2] {};
    Mode mode = lowPass;
};

/** 4-point Hermite interpolation. */
inline float hermiteAt (const float* d, int n, double pos) noexcept
{
    const int i = (int) std::floor (pos);
    const float t = (float) (pos - (double) i);
    auto at = [d, n] (int j) { return d[j < 0 ? 0 : (j >= n ? n - 1 : j)]; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

//==============================================================================
/** Plays one chop with its pad settings. The real-time sampler voices and the drag-to-DAW render
    both use this, so what you drag is exactly what you hear. */
struct PadVoice
{
    /** pitchRatio: extra speed factor (e.g. KEYS mode transposition); velocityGain 0..1. */
    void begin (const AudioData& src, int start, int end, const PadParams&, double outputRate,
                double pitchRatio, float velocityGain) noexcept;
    void noteOff() noexcept                        { releasing = true; }
    bool isActive() const noexcept                 { return active; }

    /** Next output sample (adds nothing and returns false once finished). */
    bool next (float& left, float& right) noexcept;

private:
    const AudioData* src = nullptr;
    double pos = 0, inc = 1;
    int start = 0, end = 0;
    bool reverse = false, active = false, releasing = false, filterOn = false;
    float gain = 1.0f, env = 0.0f, attackStep = 1.0f, releaseStep = 1.0f;
    double endFade = 1.0;               // output samples of fade before the chop's end
    Svf svf;
};

/** A chop rendered with its pad settings (stereo, at the source's sample rate). */
AudioData::Ptr renderPad (const AudioData& src, int start, int end, const PadParams&);

} // namespace snag
