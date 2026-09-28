#include "EditOps.h"
#include <thread>
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <numeric>

namespace snag::edit
{

static void clampRange (const AudioData& a, int& s, int& e)
{
    const int n = a.getNumSamples();
    if (e < 0 || e > n) e = n;
    s = juce::jlimit (0, n, s);
    if (e < s) std::swap (s, e);
}

static juce::AudioBuffer<float> copyOf (const AudioData& a)
{
    juce::AudioBuffer<float> b;
    b.makeCopyOf (a.buffer);
    return b;
}

AudioData::Ptr crop (const AudioData& a, int s, int e)
{
    clampRange (a, s, e);
    juce::AudioBuffer<float> b (a.getNumChannels(), juce::jmax (1, e - s));
    b.clear();
    for (int c = 0; c < a.getNumChannels(); ++c)
        b.copyFrom (c, 0, a.buffer, c, s, e - s);
    return AudioData::make (std::move (b), a.sampleRate);
}

AudioData::Ptr removeRange (const AudioData& a, int s, int e)
{
    clampRange (a, s, e);
    const int n = a.getNumSamples();
    const int newLen = juce::jmax (1, n - (e - s));
    juce::AudioBuffer<float> b (a.getNumChannels(), newLen);
    b.clear();
    for (int c = 0; c < a.getNumChannels(); ++c)
    {
        if (s > 0) b.copyFrom (c, 0, a.buffer, c, 0, s);
        if (n - e > 0) b.copyFrom (c, s, a.buffer, c, e, n - e);
    }
    // tiny crossfade at the join to avoid a click
    const int xf = juce::jmin (64, s, n - e);
    for (int c = 0; c < a.getNumChannels() && xf > 0; ++c)
    {
        auto* d = b.getWritePointer (c);
        const auto* src = a.buffer.getReadPointer (c);
        for (int i = 0; i < xf; ++i)
        {
            const float t = (float) (i + 1) / (float) (xf + 1);
            d[s - xf + i] = src[s - xf + i] * (1.0f - t) + src[e - xf + i] * t;
        }
    }
    return AudioData::make (std::move (b), a.sampleRate);
}

AudioData::Ptr silence (const AudioData& a, int s, int e)
{
    clampRange (a, s, e);
    auto b = copyOf (a);
    for (int c = 0; c < b.getNumChannels(); ++c)
        b.clear (c, s, e - s);
    return AudioData::make (std::move (b), a.sampleRate);
}

AudioData::Ptr fade (const AudioData& a, int s, int e, bool fadeIn)
{
    clampRange (a, s, e);
    auto b = copyOf (a);
    const int len = juce::jmax (1, e - s);
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < e - s; ++i)
        {
            const float t = (float) i / (float) len;
            // equal-power curve; only the selected region is affected
            const float g = fadeIn ? std::sin (t * juce::MathConstants<float>::halfPi)
                                   : std::cos (t * juce::MathConstants<float>::halfPi);
            d[s + i] *= g;
        }
    }
    return AudioData::make (std::move (b), a.sampleRate);
}

float peakLevel (const AudioData& a, int s, int e)
{
    clampRange (a, s, e);
    float p = 0.0f;
    for (int c = 0; c < a.getNumChannels(); ++c)
        p = juce::jmax (p, a.buffer.getMagnitude (c, s, e - s));
    return p;
}

AudioData::Ptr normalize (const AudioData& a, int s, int e, float targetDb)
{
    clampRange (a, s, e);
    const float p = peakLevel (a, s, e);
    if (p <= 1.0e-6f)
        return AudioData::make (copyOf (a), a.sampleRate);
    return gain (a, s, e, juce::Decibels::gainToDecibels (juce::Decibels::decibelsToGain (targetDb) / p));
}

AudioData::Ptr reverse (const AudioData& a, int s, int e)
{
    clampRange (a, s, e);
    auto b = copyOf (a);
    b.reverse (s, e - s);
    return AudioData::make (std::move (b), a.sampleRate);
}

AudioData::Ptr gain (const AudioData& a, int s, int e, float dB)
{
    clampRange (a, s, e);
    auto b = copyOf (a);
    b.applyGain (s, e - s, juce::Decibels::decibelsToGain (dB, -120.0f));
    return AudioData::make (std::move (b), a.sampleRate);
}

AudioData::Ptr toMono (const AudioData& a)
{
    juce::AudioBuffer<float> b (2, a.getNumSamples());
    b.clear();
    const float g = 1.0f / (float) juce::jmax (1, a.getNumChannels());
    for (int c = 0; c < a.getNumChannels(); ++c)
        b.addFrom (0, 0, a.buffer, c, 0, a.getNumSamples(), g);
    b.copyFrom (1, 0, b, 0, 0, a.getNumSamples());
    return AudioData::make (std::move (b), a.sampleRate);
}

AudioData::Ptr filter (const AudioData& a, float lowCutHz, float highCutHz)
{
    auto b = copyOf (a);
    const double sr = a.sampleRate;

    auto runCascade = [&] (const juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>>& stages)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            for (auto* coeffs : stages)
            {
                juce::dsp::IIR::Filter<float> f (coeffs);
                f.reset();
                auto* d = b.getWritePointer (c);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    d[i] = f.processSample (d[i]);
            }
        }
    };

    if (lowCutHz > 10.0f && lowCutHz < sr * 0.45)
        runCascade (juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod (lowCutHz, sr, 4));

    if (highCutHz > 20.0f && highCutHz < sr * 0.49)
        runCascade (juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod (highCutHz, sr, 4));

    return AudioData::make (std::move (b), a.sampleRate);
}

//==============================================================================
static float hermite (const float* d, int n, double pos)
{
    const int i = (int) pos;
    const float t = (float) (pos - i);
    auto at = [&] (int k) { return d[juce::jlimit (0, n - 1, k)]; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3,
                c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

static AudioData::Ptr varispeed (const AudioData& a, double ratio)
{
    const int n = a.getNumSamples();
    const int outLen = juce::jmax (1, (int) std::floor ((double) n / ratio));
    juce::AudioBuffer<float> b (a.getNumChannels(), outLen);
    for (int c = 0; c < a.getNumChannels(); ++c)
    {
        const auto* src = a.buffer.getReadPointer (c);
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < outLen; ++i)
            d[i] = hermite (src, n, i * ratio);
    }
    return AudioData::make (std::move (b), a.sampleRate);
}

namespace
{
    /** Kaiser-windowed sinc, tabulated. Symmetric, so resampling adds no delay at all - which
        matters when stems are subtracted from the original (music = mix - vocals). */
    struct SincTable
    {
        static constexpr int zeroCrossings = 24;   // each side, at full bandwidth
        static constexpr int phases = 512;         // table resolution per input sample

        SincTable (double cutoffIn) : cutoff (cutoffIn)
        {
            halfWidth = (double) zeroCrossings / cutoff;
            const int size = (int) std::ceil (halfWidth * phases) + 2;
            table.resize ((size_t) size);
            const double beta = 8.6;   // ~ -90 dB side lobes
            const double i0b = besselI0 (beta);
            for (int k = 0; k < size; ++k)
            {
                const double x = (double) k / phases;
                double v = 0.0;
                if (x < halfWidth)
                {
                    const double r = x / halfWidth;
                    const double w = besselI0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - r * r))) / i0b;
                    const double t = juce::MathConstants<double>::pi * cutoff * x;
                    v = cutoff * (x < 1.0e-9 ? 1.0 : std::sin (t) / t) * w;
                }
                table[(size_t) k] = (float) v;
            }
        }

        static double besselI0 (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < 1.0e-12 * sum) break;
            }
            return sum;
        }

        float at (double distance) const noexcept   // kernel value at |distance| input samples
        {
            const double p = std::abs (distance) * phases;
            const int i = (int) p;
            if (i + 1 >= (int) table.size()) return 0.0f;
            const float f = (float) (p - i);
            return table[(size_t) i] + f * (table[(size_t) i + 1] - table[(size_t) i]);
        }

        double cutoff, halfWidth = 0.0;
        std::vector<float> table;
    };
}

AudioData::Ptr resample (const AudioData& a, double newRate)
{
    if (newRate <= 0 || std::abs (newRate - a.sampleRate) < 0.5)
        return AudioData::make (copyOf (a), a.sampleRate);

    const double ratio = a.sampleRate / newRate;   // input samples per output sample
    const int n = a.getNumSamples();
    const int outLen = juce::jmax (1, juce::roundToInt ((double) n / ratio));
    const int chans = a.getNumChannels();
    juce::AudioBuffer<float> b (chans, outLen);

    // Low-pass just below the lower of the two Nyquist frequencies (no aliasing when going down).
    const SincTable k (juce::jmin (1.0, 1.0 / ratio) * 0.96);
    const int hw = (int) std::ceil (k.halfWidth);

    auto render = [&] (int c)
    {
        const float* x = a.buffer.getReadPointer (c);
        float* y = b.getWritePointer (c);
        for (int i = 0; i < outLen; ++i)
        {
            const double t = (double) i * ratio;           // exact position in the input
            const int centre = (int) std::floor (t);
            const int k0 = juce::jmax (0, centre - hw + 1), k1 = juce::jmin (n - 1, centre + hw);
            float acc = 0.0f;
            for (int j = k0; j <= k1; ++j)
                acc += x[j] * k.at (t - (double) j);
            y[i] = acc;
        }
    };

    // channels in parallel - long files are resampled before AI separation
    std::vector<std::thread> workers;
    for (int c = 1; c < chans; ++c)
        workers.emplace_back (render, c);
    render (0);
    for (auto& w : workers)
        w.join();

    return AudioData::make (std::move (b), newRate);
}

AudioData::Ptr renderAdjust (const AudioData& original, const Clip::Adjust& adj)
{
    AudioData::Ptr a = AudioData::make (copyOf (original), original.sampleRate);
    if (! adj.pitchNeutral())
        a = pitchTime (*a, adj.semitones, adj.length, adj.formants, adj.tape);
    if (std::abs (adj.gainDb) >= 0.01f)
        a = gain (*a, 0, a->getNumSamples(), adj.gainDb);
    if (adj.lowCut > 0.0f || adj.highCut > 0.0f)
        a = filter (*a, adj.lowCut, adj.highCut);
    return a;
}

AudioData::Ptr pitchTime (const AudioData& a, float semitones, double lengthRatio, bool keepFormants, bool tape)
{
    if (tape)
        return varispeed (a, std::pow (2.0, semitones / 12.0));

    lengthRatio = juce::jlimit (0.25, 4.0, lengthRatio);
    const int ch = a.getNumChannels();
    const int inLen = a.getNumSamples();
    const int outLen = juce::jmax (1, (int) std::round (inLen * lengthRatio));

    signalsmith::stretch::SignalsmithStretch<float> st;
    st.presetDefault (ch, (float) a.sampleRate);
    st.setTransposeSemitones (semitones, 8000.0f / (float) a.sampleRate);
    if (keepFormants && std::abs (semitones) > 0.01f)
        st.setFormantFactor (1.0f, true);

    // Pad short inputs so the stretcher has enough context, then trim afterwards.
    const int minLen = st.outputSeekLength ((float) (1.0 / lengthRatio)) + 16;
    const int padded = juce::jmax (inLen, minLen);

    juce::AudioBuffer<float> in (ch, padded);
    in.clear();
    for (int c = 0; c < ch; ++c)
        in.copyFrom (c, 0, a.buffer, c, 0, inLen);

    const int paddedOut = juce::jmax (1, (int) std::round (padded * lengthRatio));
    juce::AudioBuffer<float> out (ch, paddedOut);
    out.clear();

    st.exact (in.getArrayOfReadPointers(), padded, out.getArrayOfWritePointers(), paddedOut);

    juce::AudioBuffer<float> result (ch, outLen);
    for (int c = 0; c < ch; ++c)
        result.copyFrom (c, 0, out, c, 0, juce::jmin (outLen, paddedOut));

    return AudioData::make (std::move (result), a.sampleRate);
}

AudioData::Ptr mix (const std::vector<AudioData::Ptr>& layers, const std::vector<float>& gains)
{
    int len = 0, ch = 1;
    double sr = 44100.0;
    for (auto& l : layers)
        if (l != nullptr) { len = juce::jmax (len, l->getNumSamples()); ch = juce::jmax (ch, l->getNumChannels()); sr = l->sampleRate; }

    juce::AudioBuffer<float> b (ch, juce::jmax (1, len));
    b.clear();
    for (size_t i = 0; i < layers.size(); ++i)
    {
        auto& l = layers[i];
        if (l == nullptr) continue;
        const float g = i < gains.size() ? gains[i] : 1.0f;
        if (g <= 0.0f) continue;
        for (int c = 0; c < ch; ++c)
            b.addFrom (c, 0, l->buffer, juce::jmin (c, l->getNumChannels() - 1), 0, l->getNumSamples(), g);
    }
    return AudioData::make (std::move (b), sr);
}

//==============================================================================
struct OnsetEnvelope
{
    std::vector<float> env;
    int hop = 256;
};

static OnsetEnvelope computeOnsetEnvelope (const AudioData& a)
{
    OnsetEnvelope o;
    constexpr int order = 10, size = 1 << order;
    o.hop = 256;

    const int n = a.getNumSamples();
    if (n < size)
        return o;

    juce::dsp::FFT fft (order);
    std::vector<float> window (size), frame (size * 2), prev (size / 2 + 1, 0.0f);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (size - 1));

    const float chanGain = 1.0f / (float) a.getNumChannels();
    const int frames = (n - size) / o.hop + 1;
    o.env.resize ((size_t) frames, 0.0f);

    for (int f = 0; f < frames; ++f)
    {
        const int start = f * o.hop;
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int c = 0; c < a.getNumChannels(); ++c)
        {
            const auto* d = a.buffer.getReadPointer (c, start);
            for (int i = 0; i < size; ++i)
                frame[(size_t) i] += d[i] * window[(size_t) i] * chanGain;
        }
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);

        float flux = 0.0f;
        for (int k = 1; k <= size / 2; ++k)
        {
            const float mag = std::log1p (100.0f * frame[(size_t) k]);
            const float diff = mag - prev[(size_t) k];
            if (diff > 0) flux += diff;
            prev[(size_t) k] = mag;
        }
        o.env[(size_t) f] = flux;
    }

    // normalise
    const float mx = *std::max_element (o.env.begin(), o.env.end());
    if (mx > 0)
        for (auto& v : o.env) v /= mx;
    return o;
}

int snapToZeroCrossing (const AudioData& a, int pos, int maxDistance)
{
    const int n = a.getNumSamples();
    if (n < 3) return pos;
    auto mono = [&] (int i)
    {
        float s = 0;
        for (int c = 0; c < a.getNumChannels(); ++c) s += a.buffer.getSample (c, i);
        return s;
    };
    int best = pos;
    float bestV = std::numeric_limits<float>::max();
    for (int d = -maxDistance; d <= maxDistance; ++d)
    {
        const int i = pos + d;
        if (i <= 0 || i >= n - 1) continue;
        const float v0 = mono (i - 1), v1 = mono (i);
        if ((v0 <= 0 && v1 >= 0) || (v0 >= 0 && v1 <= 0))
        {
            const float score = std::abs (v1) + std::abs ((float) d) * 1.0e-4f;
            if (score < bestV) { bestV = score; best = i; }
        }
    }
    return best;
}

std::vector<int> detectTransients (const AudioData& a, float sensitivity, int minGapMs)
{
    std::vector<int> result;
    auto o = computeOnsetEnvelope (a);
    const int frames = (int) o.env.size();
    if (frames < 4)
        return result;

    sensitivity = juce::jlimit (0.0f, 1.0f, sensitivity);
    const float delta = juce::jmap (sensitivity, 0.30f, 0.025f);
    const int medRadius = 12;
    const int minGapFrames = juce::jmax (1, (int) (minGapMs * 0.001 * a.sampleRate / o.hop));

    std::vector<float> tmp;
    int lastPeak = -minGapFrames;
    for (int f = 1; f < frames - 1; ++f)
    {
        const float v = o.env[(size_t) f];
        if (v < o.env[(size_t) f - 1] || v < o.env[(size_t) f + 1])
            continue;

        tmp.clear();
        for (int k = juce::jmax (0, f - medRadius); k <= juce::jmin (frames - 1, f + medRadius); ++k)
            tmp.push_back (o.env[(size_t) k]);
        std::nth_element (tmp.begin(), tmp.begin() + (long) tmp.size() / 2, tmp.end());
        const float threshold = tmp[tmp.size() / 2] * 1.1f + delta;

        if (v > threshold && f - lastPeak >= minGapFrames)
        {
            // FFT frame f covers [f*hop, f*hop+size); the onset sits roughly in the middle.
            int pos = f * o.hop + 512 - (int) (0.004 * a.sampleRate);   // a few ms of pre-roll
            pos = snapToZeroCrossing (a, juce::jmax (0, pos), (int) (0.0015 * a.sampleRate));
            if (pos > (int) (0.02 * a.sampleRate) && pos < a.getNumSamples() - (int) (0.02 * a.sampleRate))
                result.push_back (pos);
            lastPeak = f;
        }
    }

    std::sort (result.begin(), result.end());
    result.erase (std::unique (result.begin(), result.end()), result.end());
    return result;
}

std::vector<int> equalSlices (int numSamples, int numSlices)
{
    std::vector<int> r;
    numSlices = juce::jlimit (1, 128, numSlices);
    for (int i = 1; i < numSlices; ++i)
        r.push_back ((int) ((juce::int64) numSamples * i / numSlices));
    return r;
}

double estimateBpm (const AudioData& a)
{
    if (a.lengthSeconds() < 2.5)
        return 0.0;

    auto o = computeOnsetEnvelope (a);
    const int frames = (int) o.env.size();
    const double fps = a.sampleRate / o.hop;

    // remove the local mean so autocorrelation isn't dominated by loudness
    std::vector<float> e (o.env);
    const float mean = std::accumulate (e.begin(), e.end(), 0.0f) / (float) juce::jmax (1, frames);
    for (auto& v : e) v = juce::jmax (0.0f, v - mean);

    double bestScore = 0, bestBpm = 0, total = 0;
    for (double bpm = 60.0; bpm <= 190.0; bpm += 0.5)
    {
        const double lag = fps * 60.0 / bpm;
        const int l0 = (int) lag;
        const double fr = lag - l0;
        if (l0 + 1 >= frames) continue;

        double acc = 0;
        for (int i = 0; i + l0 + 1 < frames; ++i)
            acc += e[(size_t) i] * (e[(size_t) (i + l0)] * (1.0 - fr) + e[(size_t) (i + l0 + 1)] * fr);
        acc /= (frames - l0);

        // gentle preference for the 85-160 range
        const double w = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.6, 2.0));
        const double score = acc * (0.6 + 0.4 * w);
        total += score;
        if (score > bestScore) { bestScore = score; bestBpm = bpm; }
    }

    if (bestBpm <= 0 || total <= 0)
        return 0.0;

    const double avg = total / 261.0;
    if (bestScore < avg * 1.35)   // no clear periodicity
        return 0.0;

    while (bestBpm < 75.0)  bestBpm *= 2.0;
    while (bestBpm > 175.0) bestBpm *= 0.5;
    return std::round (bestBpm * 2.0) / 2.0;
}

} // namespace snag::edit
