#include "QuickSplit.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>

namespace snag
{

bool QuickSplit::isEffectivelyMono (const AudioData& a)
{
    if (a.getNumChannels() < 2)
        return true;

    const auto* l = a.buffer.getReadPointer (0);
    const auto* r = a.buffer.getReadPointer (1);
    double ll = 0, rr = 0, lr = 0;
    const int step = juce::jmax (1, a.getNumSamples() / 400000);
    for (int i = 0; i < a.getNumSamples(); i += step)
    {
        ll += (double) l[i] * l[i];
        rr += (double) r[i] * r[i];
        lr += (double) l[i] * r[i];
    }
    if (ll < 1e-9 || rr < 1e-9)
        return true;

    const double corr = lr / std::sqrt (ll * rr);
    const double balance = std::abs (10.0 * std::log10 (ll / rr));
    return corr > 0.997 && balance < 0.5;
}

namespace
{
    constexpr int fftOrder = 12;
    constexpr int N   = 1 << fftOrder;   // 4096
    constexpr int HOP = N / 4;           // 1024
    constexpr int BINS = N / 2 + 1;

    inline float smoothstep (float e0, float e1, float x)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0));
        return t * t * (3.0f - 2.0f * t);
    }

    template <typename T>
    struct Matrix
    {
        Matrix (int f, int b) : frames (f), bins (b), data ((size_t) f * (size_t) b) {}
        T& at (int f, int b)             { return data[(size_t) f * (size_t) bins + (size_t) b]; }
        T  at (int f, int b) const       { return data[(size_t) f * (size_t) bins + (size_t) b]; }
        int frames, bins;
        std::vector<T> data;
    };

    inline float medianOf (float* v, int n)
    {
        std::nth_element (v, v + n / 2, v + n);
        return v[n / 2];
    }
}

QuickSplit::Result QuickSplit::separate (const AudioData& input, bool fourStems,
                                         std::function<void (float)> progress,
                                         std::function<bool()> shouldCancel)
{
    Result result;
    const int numSamples = input.getNumSamples();
    if (numSamples < N)
    {
        result.error = "The sample is too short to separate";
        return result;
    }

    const bool mono = isEffectivelyMono (input);
    if (mono && ! fourStems)
    {
        result.error = "This source is mono, so Quick Split can't find the vocal. Use AI Split (Settings -> Install AI Stems) or choose 4 stems for drums / bass / melodic.";
        return result;
    }
    if (mono)
        result.note = "Mono source: vocals can't be isolated in Quick mode (try AI Split).";

    const double sr = input.sampleRate;
    const int chans = 2;
    auto channelPtr = [&] (int c) { return input.buffer.getReadPointer (juce::jmin (c, input.getNumChannels() - 1)); };

    // Pad so frames cover the whole signal.
    const int pad = N;
    const int paddedLen = numSamples + 2 * pad;
    const int frames = (paddedLen - N) / HOP + 1;

    auto sampleAt = [&] (int c, int idx) -> float
    {
        const int i = idx - pad;
        return (i >= 0 && i < numSamples) ? channelPtr (c)[i] : 0.0f;
    };

    std::vector<float> window (N);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> bufL (2 * N), bufR (2 * N);

    auto analyse = [&] (int f)
    {
        const int start = f * HOP;
        for (int i = 0; i < N; ++i)
        {
            bufL[(size_t) i] = sampleAt (0, start + i) * window[(size_t) i];
            bufR[(size_t) i] = sampleAt (1, start + i) * window[(size_t) i];
        }
        std::fill (bufL.begin() + N, bufL.end(), 0.0f);
        std::fill (bufR.begin() + N, bufR.end(), 0.0f);
        fft.performRealOnlyForwardTransform (bufL.data(), true);
        fft.performRealOnlyForwardTransform (bufR.data(), true);
    };

    auto report = [&] (float p) { if (progress) progress (juce::jlimit (0.0f, 1.0f, p)); };
    auto cancelled = [&] { return shouldCancel && shouldCancel(); };

    //==============================================================================
    // Pass A: mid magnitude + centre similarity
    Matrix<float> mag (frames, BINS);
    Matrix<std::uint8_t> centre (frames, BINS);

    for (int f = 0; f < frames; ++f)
    {
        if ((f & 63) == 0)
        {
            if (cancelled()) { result.error = "Cancelled"; return result; }
            report (0.30f * (float) f / (float) frames);
        }

        analyse (f);
        for (int k = 0; k < BINS; ++k)
        {
            const std::complex<float> L (bufL[(size_t) (2 * k)], bufL[(size_t) (2 * k + 1)]);
            const std::complex<float> R (bufR[(size_t) (2 * k)], bufR[(size_t) (2 * k + 1)]);
            mag.at (f, k) = std::abs (L + R) * 0.5f;

            const float denom = std::norm (L) + std::norm (R) + 1.0e-12f;
            const float sim = juce::jmax (0.0f, 2.0f * (L * std::conj (R)).real() / denom);
            centre.at (f, k) = (std::uint8_t) juce::jlimit (0, 255, (int) std::lround (sim * 255.0f));
        }
    }

    //==============================================================================
    // Pass B: harmonic / percussive soft masks via median filtering
    constexpr int timeRadius = 8, freqRadius = 8;
    Matrix<std::uint8_t> percMask (frames, BINS);
    {
        std::vector<float> harm ((size_t) frames * BINS);
        std::vector<float> tmp (64);

        for (int k = 0; k < BINS; ++k)
        {
            if ((k & 127) == 0)
            {
                if (cancelled()) { result.error = "Cancelled"; return result; }
                report (0.30f + 0.25f * (float) k / (float) BINS);
            }
            for (int f = 0; f < frames; ++f)
            {
                int n = 0;
                for (int t = juce::jmax (0, f - timeRadius); t <= juce::jmin (frames - 1, f + timeRadius); ++t)
                    tmp[(size_t) n++] = mag.at (t, k);
                harm[(size_t) f * BINS + (size_t) k] = medianOf (tmp.data(), n);
            }
        }

        for (int f = 0; f < frames; ++f)
        {
            if ((f & 63) == 0)
            {
                if (cancelled()) { result.error = "Cancelled"; return result; }
                report (0.55f + 0.15f * (float) f / (float) frames);
            }
            for (int k = 0; k < BINS; ++k)
            {
                int n = 0;
                for (int b = juce::jmax (0, k - freqRadius); b <= juce::jmin (BINS - 1, k + freqRadius); ++b)
                    tmp[(size_t) n++] = mag.at (f, b);
                const float P = medianOf (tmp.data(), n);
                const float H = harm[(size_t) f * BINS + (size_t) k];
                const float mp = (P * P) / (P * P + H * H + 1.0e-12f);
                percMask.at (f, k) = (std::uint8_t) juce::jlimit (0, 255, (int) std::lround (mp * 255.0f));
            }
        }
    }

    // Per-bin frequency weights
    std::vector<float> vocalBand (BINS), bassBand (BINS);
    for (int k = 0; k < BINS; ++k)
    {
        const float hz = (float) (k * sr / N);
        const float hiTaper = hz < 9000.0f ? 1.0f : juce::jmap (juce::jlimit (0.0f, 1.0f, (hz - 9000.0f) / 7000.0f), 1.0f, 0.35f);
        vocalBand[(size_t) k] = smoothstep (90.0f, 220.0f, hz) * hiTaper;
        bassBand[(size_t) k]  = 1.0f - smoothstep (160.0f, 300.0f, hz);
    }

    //==============================================================================
    // Pass C: masks -> resynthesis via weighted overlap-add
    const int numOut = fourStems ? 4 : 2;
    std::vector<juce::AudioBuffer<float>> outs;
    for (int i = 0; i < numOut; ++i)
    {
        outs.emplace_back (chans, paddedLen);
        outs.back().clear();
    }
    std::vector<float> norm ((size_t) paddedLen, 0.0f);

    std::vector<std::vector<float>> spec ((size_t) numOut * chans, std::vector<float> (2 * N));
    std::vector<float> vm (BINS), dm (BINS), bm (BINS);

    for (int f = 0; f < frames; ++f)
    {
        if ((f & 63) == 0)
        {
            if (cancelled()) { result.error = "Cancelled"; return result; }
            report (0.70f + 0.30f * (float) f / (float) frames);
        }

        analyse (f);

        for (int k = 0; k < BINS; ++k)
        {
            const float c  = mono ? 0.0f : (float) centre.at (f, k) / 255.0f;
            const float mp = (float) percMask.at (f, k) / 255.0f;
            const float c4 = c * c * c * c;
            vm[(size_t) k] = juce::jlimit (0.0f, 1.0f, c4 * vocalBand[(size_t) k] * std::pow (1.0f - mp, 0.7f));
            dm[(size_t) k] = mp;
            bm[(size_t) k] = bassBand[(size_t) k] * (1.0f - mp);
        }

        for (int ch = 0; ch < chans; ++ch)
        {
            const auto& X = ch == 0 ? bufL : bufR;
            auto& V = spec[(size_t) (0 * chans + ch)];
            auto& M = spec[(size_t) (1 * chans + ch)];   // music (2-stem) or drums (4-stem)

            for (int k = 0; k < BINS; ++k)
            {
                const float re = X[(size_t) (2 * k)], im = X[(size_t) (2 * k + 1)];
                const float v = vm[(size_t) k];
                const float rRe = re * (1.0f - v), rIm = im * (1.0f - v);   // residual (instrumental)

                V[(size_t) (2 * k)] = re * v;
                V[(size_t) (2 * k + 1)] = im * v;

                if (! fourStems)
                {
                    M[(size_t) (2 * k)] = rRe;
                    M[(size_t) (2 * k + 1)] = rIm;
                }
                else
                {
                    auto& B = spec[(size_t) (2 * chans + ch)];
                    auto& O = spec[(size_t) (3 * chans + ch)];
                    const float d = dm[(size_t) k], b = bm[(size_t) k];
                    const float o = juce::jmax (0.0f, 1.0f - d - b);
                    M[(size_t) (2 * k)] = rRe * d;  M[(size_t) (2 * k + 1)] = rIm * d;
                    B[(size_t) (2 * k)] = rRe * b;  B[(size_t) (2 * k + 1)] = rIm * b;
                    O[(size_t) (2 * k)] = rRe * o;  O[(size_t) (2 * k + 1)] = rIm * o;
                }
            }
        }

        const int start = f * HOP;
        for (int s = 0; s < numOut; ++s)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                auto& buf = spec[(size_t) (s * chans + ch)];
                std::fill (buf.begin() + 2 * BINS, buf.end(), 0.0f);
                fft.performRealOnlyInverseTransform (buf.data());
                auto* out = outs[(size_t) s].getWritePointer (ch);
                for (int i = 0; i < N; ++i)
                    out[start + i] += buf[(size_t) i] * window[(size_t) i];
            }
        }
        for (int i = 0; i < N; ++i)
            norm[(size_t) (start + i)] += window[(size_t) i] * window[(size_t) i];
    }

    // Normalise the overlap-add and trim the padding
    const char* names4[] = { "vocals", "drums", "bass", "other" };
    const char* names2[] = { "vocals", "music" };

    for (int s = 0; s < numOut; ++s)
    {
        juce::AudioBuffer<float> trimmed (input.getNumChannels() >= 2 ? 2 : 1, numSamples);
        for (int ch = 0; ch < trimmed.getNumChannels(); ++ch)
        {
            const auto* src = outs[(size_t) s].getReadPointer (ch);
            auto* dst = trimmed.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const float w = norm[(size_t) (i + pad)];
                dst[i] = w > 1.0e-6f ? src[i + pad] / w : 0.0f;
            }
        }

        outs[(size_t) s].setSize (0, 0);   // free memory as we go

        juce::String name = fourStems ? names4[s] : names2[s];
        if (mono && fourStems && s == 0)
            continue;                                   // no vocal stem for mono sources
        if (mono && fourStems && name == "other")
            name = "melodic";

        result.stems.push_back ({ name, AudioData::make (std::move (trimmed), sr) });
    }

    report (1.0f);
    return result;
}

} // namespace snag
