#include "PadFx.h"

namespace snag
{

juce::String PadParams::toString() const
{
    return juce::String (gainDb, 2) + " " + juce::String (semitones, 2) + " " + (reverse ? "1" : "0") + " "
         + juce::String (attackMs, 1) + " " + juce::String (releaseMs, 1) + " " + juce::String (filter, 4);
}

PadParams PadParams::fromString (const juce::String& s)
{
    PadParams p;
    auto t = juce::StringArray::fromTokens (s, " ", {});
    if (t.size() >= 6)
    {
        p.gainDb    = juce::jlimit (-24.0f, 12.0f, t[0].getFloatValue());
        p.semitones = juce::jlimit (-24.0f, 24.0f, t[1].getFloatValue());
        p.reverse   = t[2].getIntValue() != 0;
        p.attackMs  = juce::jlimit (0.0f, 2000.0f, t[3].getFloatValue());
        p.releaseMs = juce::jlimit (1.0f, 4000.0f, t[4].getFloatValue());
        p.filter    = juce::jlimit (-1.0f, 1.0f, t[5].getFloatValue());
    }
    return p;
}

float filterCutoffHz (float filter) noexcept
{
    const float a = std::abs (filter);
    return filter < 0 ? 20000.0f * std::pow (0.0015f, a)      // low-pass: 20 kHz down to 30 Hz
                      : 20.0f * std::pow (400.0f, a);          // high-pass: 20 Hz up to 8 kHz
}

juce::String filterText (float filter)
{
    if (std::abs (filter) < 0.005f) return "Off";
    const float hz = filterCutoffHz (filter);
    const auto f = hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + "k" : juce::String (juce::roundToInt (hz));
    return (filter < 0 ? "LP " : "HP ") + f;
}

//==============================================================================
void Svf::set (float cutoffHz, float q, double sampleRate, Mode m) noexcept
{
    mode = m;
    const float fc = juce::jlimit (10.0f, (float) (sampleRate * 0.45), cutoffHz);
    const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);
    k = 1.0f / juce::jmax (0.1f, q);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

//==============================================================================
void PadVoice::begin (const AudioData& s, int st, int en, const PadParams& p, double outputRate,
                      double pitchRatio, float velocityGain) noexcept
{
    src = &s;
    start = juce::jlimit (0, s.getNumSamples(), st);
    end = juce::jlimit (start, s.getNumSamples(), en);
    reverse = p.reverse;
    inc = (s.sampleRate / outputRate) * pitchRatio * std::pow (2.0, p.semitones / 12.0);
    pos = reverse ? (double) end - 1.0 : (double) start;
    gain = velocityGain * juce::Decibels::decibelsToGain (p.gainDb, -100.0f);

    const double sr = outputRate;
    const double lengthOut = (end - start) / juce::jmax (1.0e-6, inc);   // chop length in output samples
    attackStep = p.attackMs <= 0.05f ? 1.0f : (float) (1.0 / juce::jmax (1.0, sr * p.attackMs / 1000.0));
    env = p.attackMs <= 0.05f ? 1.0f : 0.0f;
    releaseStep = (float) (1.0 / juce::jmax (1.0, sr * p.releaseMs / 1000.0));
    // the fade before the chop ends: the release time, but never more than ~half the chop, never a click
    endFade = juce::jmin (sr * p.releaseMs / 1000.0, lengthOut * 0.45);
    endFade = juce::jmax (endFade, juce::jmin (sr * 0.002, lengthOut * 0.45));
    endFade = juce::jmax (1.0, endFade);

    filterOn = std::abs (p.filter) >= 0.005f;
    if (filterOn)
    {
        svf.set (filterCutoffHz (p.filter), 0.9f, sr, p.filter < 0 ? Svf::lowPass : Svf::highPass);
        svf.reset();
    }
    releasing = false;
    active = end - start > 1;
}

bool PadVoice::next (float& left, float& right) noexcept
{
    if (! active)
        return false;
    if (reverse ? pos < (double) start : pos >= (double) end)
    {
        active = false;
        return false;
    }

    if (releasing)
    {
        env -= releaseStep;
        if (env <= 0.0f)
        {
            active = false;
            return false;
        }
    }
    else if (env < 1.0f)
        env = juce::jmin (1.0f, env + attackStep);

    const double remaining = (reverse ? pos - start : end - pos) / inc;
    const float tail = remaining < endFade ? (float) (remaining / endFade) : 1.0f;
    const float amp = gain * env * tail;

    const auto& b = src->buffer;
    const int n = b.getNumSamples();
    float l = hermiteAt (b.getReadPointer (0), n, pos);
    float r = b.getNumChannels() > 1 ? hermiteAt (b.getReadPointer (1), n, pos) : l;
    if (filterOn)
    {
        l = svf.process (l, 0);
        r = svf.process (r, 1);
    }
    left = l * amp;
    right = r * amp;
    pos += reverse ? -inc : inc;
    return true;
}

AudioData::Ptr renderPad (const AudioData& src, int start, int end, const PadParams& p)
{
    PadVoice v;
    v.begin (src, start, end, p, src.sampleRate, 1.0, 1.0f);
    const double inc = std::pow (2.0, p.semitones / 12.0);
    const int len = juce::jmax (1, (int) std::ceil ((end - start) / inc) + 2);
    juce::AudioBuffer<float> out (2, len);
    out.clear();
    for (int i = 0; i < len; ++i)
    {
        float l = 0, r = 0;
        if (! v.next (l, r))
        {
            out.setSize (2, juce::jmax (1, i), true);
            break;
        }
        out.setSample (0, i, l);
        out.setSample (1, i, r);
    }
    return AudioData::make (std::move (out), src.sampleRate);
}

} // namespace snag
