#include "FxRack.h"
#include <cmath>

namespace snag
{

const std::vector<DelayDivision>& delayDivisions()
{
    static const std::vector<DelayDivision> d {
        { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16D", 0.375 }, { "1/8T", 1.0 / 3.0 },
        { "1/8", 0.5 }, { "1/8D", 0.75 }, { "1/4T", 2.0 / 3.0 }, { "1/4", 1.0 }, { "1/4D", 1.5 }, { "1/2", 2.0 }, { "1 bar", 4.0 },
    };
    return d;
}

juce::String FxSettings::toString() const
{
    juce::StringArray t;
    t.add (lofiOn ? "1" : "0");   t.add (juce::String (bits, 2)); t.add (juce::String (rateKHz, 2)); t.add (juce::String (vinyl, 3));
    t.add (driveOn ? "1" : "0");  t.add (juce::String (drive, 3)); t.add (juce::String (tone, 3));
    t.add (delayOn ? "1" : "0");  t.add (juce::String (division)); t.add (juce::String (feedback, 3)); t.add (juce::String (delayMix, 3));
    t.add (reverbOn ? "1" : "0"); t.add (juce::String (size, 3)); t.add (juce::String (reverbMix, 3));
    return t.joinIntoString (" ");
}

FxSettings FxSettings::fromString (const juce::String& s)
{
    FxSettings f;
    auto t = juce::StringArray::fromTokens (s, " ", {});
    if (t.size() < 14)
        return f;
    f.lofiOn = t[0] == "1";   f.bits = juce::jlimit (4.0f, 16.0f, t[1].getFloatValue());
    f.rateKHz = juce::jlimit (2.0f, 44.0f, t[2].getFloatValue()); f.vinyl = juce::jlimit (0.0f, 1.0f, t[3].getFloatValue());
    f.driveOn = t[4] == "1";  f.drive = juce::jlimit (0.0f, 1.0f, t[5].getFloatValue()); f.tone = juce::jlimit (0.0f, 1.0f, t[6].getFloatValue());
    f.delayOn = t[7] == "1";  f.division = juce::jlimit (0, (int) delayDivisions().size() - 1, t[8].getIntValue());
    f.feedback = juce::jlimit (0.0f, 0.9f, t[9].getFloatValue()); f.delayMix = juce::jlimit (0.0f, 1.0f, t[10].getFloatValue());
    f.reverbOn = t[11] == "1"; f.size = juce::jlimit (0.0f, 1.0f, t[12].getFloatValue()); f.reverbMix = juce::jlimit (0.0f, 1.0f, t[13].getFloatValue());
    return f;
}

double FxSettings::tailSeconds (double bpm) const
{
    double tail = 0.0;
    if (delayOn && delayMix > 0.001f)
    {
        const double t = delayDivisions()[(size_t) division].beats * 60.0 / (bpm > 0 ? bpm : 120.0);
        const double repeats = feedback > 0.01f ? std::log (0.001) / std::log ((double) feedback) : 1.0;   // down 60 dB
        tail = juce::jmin (12.0, t * (repeats + 1.0));
    }
    if (reverbOn && reverbMix > 0.001f)
        tail = juce::jmax (tail, 0.8 + 5.0 * size) + (delayOn ? 1.0 : 0.0);
    if (lofiOn)
        tail += 0.01;
    return tail;
}

//==============================================================================
void FxChain::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    for (auto& b : dBuf) b.assign ((size_t) (sr * 4.5) + 8, 0.0f);   // 1 bar at 53 bpm
    for (auto& b : wowBuf) b.assign ((size_t) (sr * 0.02) + 8, 0.0f);
    reverb.setSampleRate (sr);
    reset();
}

void FxChain::reset()
{
    for (auto& b : dBuf) std::fill (b.begin(), b.end(), 0.0f);
    for (auto& b : wowBuf) std::fill (b.begin(), b.end(), 0.0f);
    dWrite = wowWrite = 0;
    dTime = -1.0;
    holdL = holdR = 0; holdPhase = 1.0f;
    clickEnv = hissLp = lpL = lpR = hpL = hpR = hpInL = hpInR = toneL = toneR = dampL = dampR = 0.0f;
    wowPhase = flutterPhase = 0.0;
    rng.setSeed (20260929);
    reverb.reset();
}

void FxChain::process (float* L, float* R, int n, const FxSettings& fx, double bpm) noexcept
{
    if (n <= 0 || dBuf[0].empty())
        return;
    const float fs = (float) sr;

    //---------------------------------------------------------------- LO-FI
    if (fx.lofiOn)
    {
        const float levels = std::pow (2.0f, fx.bits - 1.0f);
        const float holdInc = juce::jlimit (0.001f, 1.0f, fx.rateKHz * 1000.0f / fs);
        const float v = fx.vinyl;
        // worn-out record: band-limited, a little wobbly, crackles and hiss
        const float lpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * (18000.0f - 11000.0f * v) / fs);
        const float hpCoef = std::exp (-juce::MathConstants<float>::twoPi * (20.0f + 60.0f * v) / fs);
        const float clickProb = v * 12.0f / fs;         // ~12 crackles a second at full
        const float clickDecay = std::exp (-1.0f / (0.0007f * fs));
        const float hissCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5000.0f / fs);
        const int wowLen = (int) wowBuf[0].size();
        const double wowDepth = v * 0.0008 * sr, flutterDepth = v * 0.00006 * sr;   // seconds -> samples
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];

            // wow & flutter: a gently modulated short delay
            if (v > 0.001f)
            {
                wowBuf[0][(size_t) wowWrite] = l;
                wowBuf[1][(size_t) wowWrite] = r;
                wowPhase += 0.55 / sr;  if (wowPhase >= 1.0) wowPhase -= 1.0;
                flutterPhase += 6.5 / sr; if (flutterPhase >= 1.0) flutterPhase -= 1.0;
                const double d = 2.0 + wowDepth * (1.0 + std::sin (juce::MathConstants<double>::twoPi * wowPhase))
                                     + flutterDepth * (1.0 + std::sin (juce::MathConstants<double>::twoPi * flutterPhase));
                double rp = wowWrite - d;
                while (rp < 0) rp += wowLen;
                const int i0 = (int) rp; const float fr = (float) (rp - i0);
                const int i1 = (i0 + 1) % wowLen;
                l = wowBuf[0][(size_t) i0] + fr * (wowBuf[0][(size_t) i1] - wowBuf[0][(size_t) i0]);
                r = wowBuf[1][(size_t) i0] + fr * (wowBuf[1][(size_t) i1] - wowBuf[1][(size_t) i0]);
                wowWrite = (wowWrite + 1) % wowLen;
            }

            // sample-rate reduction (sample & hold) and bit crush
            holdPhase += holdInc;
            if (holdPhase >= 1.0f)
            {
                holdPhase -= 1.0f;
                holdL = std::round (l * levels) / levels;
                holdR = std::round (r * levels) / levels;
            }
            l = holdL; r = holdR;

            // vinyl: worn-out tone, crackle and hiss (at VINYL 0 the crush stays raw)
            if (v > 0.001f)
            {
                lpL += lpCoef * (l - lpL); lpR += lpCoef * (r - lpR);
                hpL = hpCoef * (hpL + lpL - hpInL); hpInL = lpL;
                hpR = hpCoef * (hpR + lpR - hpInR); hpInR = lpR;
                l = hpL; r = hpR;

                if (rng.nextFloat() < clickProb)
                {
                    clickEnv = (0.02f + 0.18f * rng.nextFloat() * rng.nextFloat()) * v;
                    clickSign = rng.nextBool() ? 1.0f : -1.0f;
                }
                const float click = clickEnv * clickSign;
                clickSign = -clickSign * 0.92f;    // a tiny ringing tick rather than a DC step
                clickEnv *= clickDecay;
                hissLp += hissCoef * ((rng.nextFloat() * 2.0f - 1.0f) - hissLp);
                const float hiss = hissLp * 0.012f * v;
                l += click + hiss;
                r += click * 0.9f + hiss;
            }
            L[i] = l; R[i] = r;
        }
    }

    //---------------------------------------------------------------- DRIVE
    if (fx.driveOn)
    {
        const float pre = juce::Decibels::decibelsToGain (fx.drive * 30.0f);
        const float bias = 0.12f * fx.drive;
        const float offset = std::tanh (bias);
        const float makeup = 1.0f / std::tanh (juce::jmax (0.35f, pre * 0.35f)) * 0.35f;
        const float toneCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * (1500.0f + 16500.0f * fx.tone * fx.tone) / fs);
        for (int i = 0; i < n; ++i)
        {
            float l = (std::tanh (L[i] * pre + bias) - offset) * makeup;
            float r = (std::tanh (R[i] * pre + bias) - offset) * makeup;
            toneL += toneCoef * (l - toneL); toneR += toneCoef * (r - toneR);
            L[i] = toneL; R[i] = toneR;
        }
    }

    //---------------------------------------------------------------- DELAY (ping-pong, tempo-synced)
    if (fx.delayOn)
    {
        const int len = (int) dBuf[0].size();
        const double beats = delayDivisions()[(size_t) juce::jlimit (0, (int) delayDivisions().size() - 1, fx.division)].beats;
        const double target = juce::jlimit (1.0, (double) len - 4.0, beats * 60.0 / (bpm > 0 ? bpm : 120.0) * sr);
        if (dTime < 0) dTime = target;
        const double glide = 1.0 - std::exp (-1.0 / (0.05 * sr));      // 50 ms glide when the tempo changes
        const float fb = juce::jlimit (0.0f, 0.9f, fx.feedback);
        const float dampCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 6500.0f / fs);
        for (int i = 0; i < n; ++i)
        {
            dTime += (target - dTime) * glide;
            double rp = dWrite - dTime;
            while (rp < 0) rp += len;
            const int i0 = (int) rp; const float fr = (float) (rp - i0);
            const int i1 = (i0 + 1) % len;
            const float yl = dBuf[0][(size_t) i0] + fr * (dBuf[0][(size_t) i1] - dBuf[0][(size_t) i0]);
            const float yr = dBuf[1][(size_t) i0] + fr * (dBuf[1][(size_t) i1] - dBuf[1][(size_t) i0]);
            dampL += dampCoef * (yl - dampL); dampR += dampCoef * (yr - dampR);
            const float in = 0.5f * (L[i] + R[i]);
            dBuf[0][(size_t) dWrite] = in + fb * dampR;     // left <- input + right echo
            dBuf[1][(size_t) dWrite] = fb * dampL;          // right <- left echo (ping-pong)
            dWrite = (dWrite + 1) % len;
            L[i] += fx.delayMix * yl;
            R[i] += fx.delayMix * yr;
        }
    }

    //---------------------------------------------------------------- REVERB
    if (fx.reverbOn)
    {
        juce::Reverb::Parameters p;
        p.roomSize = 0.35f + 0.63f * fx.size;
        p.damping = 0.45f;
        p.wetLevel = fx.reverbMix * 0.8f;
        p.dryLevel = 1.0f - 0.45f * fx.reverbMix;
        p.width = 1.0f;
        reverb.setParameters (p);
        reverb.processStereo (L, R, n);
    }
}

//==============================================================================
AudioData::Ptr renderFx (const AudioData& in, const FxSettings& fx, double bpm, bool withTail)
{
    if (! fx.anyOn())
        return AudioData::make (juce::AudioBuffer<float> (in.buffer), in.sampleRate);

    const int n = in.getNumSamples();
    const int tail = withTail ? (int) (fx.tailSeconds (bpm) * in.sampleRate) : 0;
    juce::AudioBuffer<float> out (2, n + tail);
    out.clear();
    for (int c = 0; c < 2; ++c)
        out.copyFrom (c, 0, in.buffer, juce::jmin (c, in.getNumChannels() - 1), 0, n);

    FxChain chain;
    chain.prepare (in.sampleRate);
    const int block = 4096;
    for (int s = 0; s < out.getNumSamples(); s += block)
    {
        const int num = juce::jmin (block, out.getNumSamples() - s);
        chain.process (out.getWritePointer (0, s), out.getWritePointer (1, s), num, fx, bpm);
    }

    // trim the ringing once it's inaudible (keep at least the original length)
    if (tail > 0)
    {
        int last = out.getNumSamples();
        const int win = (int) (in.sampleRate * 0.02);
        const float floor = juce::Decibels::decibelsToGain (-66.0f);
        while (last - win > n)
        {
            if (out.getMagnitude (0, last - win, win) > floor || out.getMagnitude (1, last - win, win) > floor)
                break;
            last -= win;
        }
        out.setSize (2, last, true);
        // gentle 10 ms fade at the very end
        const int fade = juce::jmin (last - n, (int) (in.sampleRate * 0.01));
        if (fade > 1)
            for (int c = 0; c < 2; ++c)
                out.applyGainRamp (c, last - fade, fade, 1.0f, 0.0f);
    }
    return AudioData::make (std::move (out), in.sampleRate);
}

} // namespace snag
