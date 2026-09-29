#include "KeyDetect.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace snag::key
{

Key Key::transposed (int semitones) const noexcept
{
    Key k = *this;
    if (isValid())
        k.tonic = ((tonic + semitones) % 12 + 12) % 12;
    return k;
}

namespace
{
    // Temperley (Kostka-Payne) key profiles: how often each scale degree turns up in a key
    constexpr std::array<double, 12> majorProfile { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 };
    constexpr std::array<double, 12> minorProfile { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 };

    double correlation (const std::array<double, 12>& chroma, const std::array<double, 12>& profile, int tonic)
    {
        double mc = 0, mp = 0;
        for (int i = 0; i < 12; ++i) { mc += chroma[(size_t) i]; mp += profile[(size_t) i]; }
        mc /= 12.0; mp /= 12.0;
        double num = 0, dc = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double c = chroma[(size_t) ((i + tonic) % 12)] - mc;
            const double p = profile[(size_t) i] - mp;
            num += c * p; dc += c * c; dp += p * p;
        }
        return dc > 0 && dp > 0 ? num / std::sqrt (dc * dp) : 0.0;
    }
}

Key detect (const AudioData& audio)
{
    Key result;
    const int n = audio.getNumSamples();
    const double sr = audio.sampleRate;
    if (n < (int) (sr * 0.5) || sr <= 0)
        return result;

    // FFT size: ~5 Hz resolution, so neighbouring low notes are told apart
    const int order = sr > 60000.0 ? 14 : 13;
    const int N = 1 << order, hop = N / 4;
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) N), buf ((size_t) N * 2);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);

    const int kLo = juce::jmax (1, (int) (50.0 * N / sr)), kHi = juce::jmin (N / 2 - 1, (int) (2000.0 * N / sr));
    const int bins = kHi - kLo + 1;

    // magnitude spectrogram of the band (mono)
    std::vector<std::vector<float>> spec;
    const int chans = audio.getNumChannels();
    for (int start = 0; start + N / 2 < n; start += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N && start + i < n; ++i)
        {
            float s = 0;
            for (int c = 0; c < chans; ++c) s += audio.buffer.getSample (c, start + i);
            buf[(size_t) i] = s / (float) chans * window[(size_t) i];
        }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        spec.emplace_back (buf.begin() + kLo, buf.begin() + kHi + 1);
    }
    const int T = (int) spec.size();
    if (T == 0)
        return result;

    // keep what's sustained: each bin no louder than its median over ~9 frames (short hits drop out)
    {
        std::vector<std::vector<float>> held ((size_t) T, std::vector<float> ((size_t) bins));
        std::vector<float> tmp;
        for (int k = 0; k < bins; ++k)
            for (int t = 0; t < T; ++t)
            {
                tmp.clear();
                for (int j = juce::jmax (0, t - 4); j <= juce::jmin (T - 1, t + 4); ++j)
                    tmp.push_back (spec[(size_t) j][(size_t) k]);
                std::nth_element (tmp.begin(), tmp.begin() + (long) (tmp.size() / 2), tmp.end());
                held[(size_t) t][(size_t) k] = juce::jmin (spec[(size_t) t][(size_t) k], tmp[tmp.size() / 2]);
            }
        spec = std::move (held);
    }

    // per frame: level, and "whitened" peaks - what stands out from its +-40 Hz neighbourhood
    const int span = juce::jmax (3, (int) (40.0 * N / sr));
    std::vector<float> weight ((size_t) T, 0.0f);
    std::vector<std::vector<float>> peaks ((size_t) T, std::vector<float> ((size_t) bins, 0.0f));
    std::vector<float> prefix ((size_t) bins + 1);
    for (int t = 0; t < T; ++t)
    {
        const auto& m = spec[(size_t) t];
        double e = 0;
        prefix[0] = 0;
        for (int i = 0; i < bins; ++i) { e += (double) m[(size_t) i] * m[(size_t) i]; prefix[(size_t) i + 1] = prefix[(size_t) i] + m[(size_t) i]; }
        weight[(size_t) t] = (float) std::sqrt (std::sqrt (e));
        for (int i = 0; i < bins; ++i)
        {
            const int a = juce::jmax (0, i - span), b = juce::jmin (bins - 1, i + span);
            const float local = (prefix[(size_t) b + 1] - prefix[(size_t) a]) / (float) (b - a + 1);
            peaks[(size_t) t][(size_t) i] = juce::jmax (0.0f, m[(size_t) i] - 1.2f * local);
        }
    }

    // tuning: histogram of peak deviations from the equal-tempered grid (A = 440)
    std::array<double, 20> hist {};   // 5-cent bins, -50..+50
    for (int t = 0; t < T; ++t)
    {
        const auto& f = peaks[(size_t) t];
        for (int i = 1; i + 1 < bins; ++i)
        {
            const float v = f[(size_t) i];
            if (v <= 0 || v < f[(size_t) i - 1] || v < f[(size_t) i + 1]) continue;
            const float a = f[(size_t) i - 1], b = v, c = f[(size_t) i + 1];
            const float den = a - 2 * b + c;
            const double p = std::abs (den) > 1e-12f ? 0.5 * (a - c) / den : 0.0;   // true peak between bins
            const double hz = (kLo + i + p) * sr / N;
            const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const double cents = (midi - std::round (midi)) * 100.0;
            hist[(size_t) juce::jlimit (0, 19, (int) std::floor ((cents + 50.0) / 5.0))] += v * weight[(size_t) t];
        }
    }
    int best = 10;
    for (int i = 0; i < 20; ++i) if (hist[(size_t) i] > hist[(size_t) best]) best = i;
    result.tuningCents = (float) (best * 5 - 50 + 2.5);
    if (std::abs (result.tuningCents) < 5.0f) result.tuningCents = 0.0f;

    // chroma of everything, and of the bass alone (<= 180 Hz); each frame normalised
    std::array<double, 12> chroma {}, bass {};
    std::vector<int> pc ((size_t) bins, -1);
    std::vector<float> w ((size_t) bins, 0.0f);
    const double tune = result.tuningCents / 100.0;
    for (int i = 0; i < bins; ++i)
    {
        const double hz = (kLo + i) * sr / N;
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0) - tune;
        const double nearest = std::round (midi);
        const double dist = std::abs (midi - nearest);
        if (dist > 0.4) continue;                                 // between two notes
        pc[(size_t) i] = ((int) nearest % 12 + 12) % 12;
        w[(size_t) i] = (float) (1.0 - dist / 0.4);
    }
    const int bassTop = (int) (180.0 * N / sr) - kLo;
    for (int t = 0; t < T; ++t)
    {
        std::array<double, 12> c {}, b {};
        for (int i = 0; i < bins; ++i)
            if (pc[(size_t) i] >= 0)
            {
                const double v = (double) peaks[(size_t) t][(size_t) i] * w[(size_t) i];
                c[(size_t) pc[(size_t) i]] += v;
                if (i <= bassTop) b[(size_t) pc[(size_t) i]] += v;
            }
        double cs = 0, bs = 0;
        for (int k = 0; k < 12; ++k) { cs += c[(size_t) k]; bs += b[(size_t) k]; }
        for (int k = 0; k < 12; ++k)
        {
            if (cs > 0) chroma[(size_t) k] += c[(size_t) k] / cs * weight[(size_t) t];
            if (bs > 0) bass[(size_t) k]   += b[(size_t) k] / bs * weight[(size_t) t];
        }
    }
    double bassMax = 0;
    for (auto v : bass) bassMax = juce::jmax (bassMax, v);

    struct Score { double s; int tonic; bool minor; };
    std::vector<Score> scores;
    for (int t = 0; t < 12; ++t)
        for (int minor = 0; minor < 2; ++minor)
        {
            const double bonus = bassMax > 0 ? 0.15 * bass[(size_t) t] / bassMax : 0.0;   // the root is usually the bass's home
            scores.push_back ({ correlation (chroma, minor ? minorProfile : majorProfile, t) + bonus, t, minor == 1 });
        }
    std::sort (scores.begin(), scores.end(), [] (const Score& a, const Score& b) { return a.s > b.s; });
    result.tonic = scores[0].tonic;
    result.minor = scores[0].minor;
    for (size_t i = 0; i < 3; ++i)
        result.candidates.push_back ({ scores[i].tonic, scores[i].minor });
    result.confidence = (float) juce::jlimit (0.0, 1.0, (scores[0].s - scores[1].s) * 4.0 + juce::jmax (0.0, scores[0].s) * 0.5);
    return result;
}

static const char* const majorNames[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char* const minorNames[] = { "Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm" };

juce::String name (const Key& k)
{
    if (! k.isValid()) return "-";
    return k.minor ? minorNames[k.tonic] : majorNames[k.tonic];
}

juce::String longName (const Key& k)
{
    if (! k.isValid()) return "unknown";
    auto n = name (k);
    if (k.minor) n = n.dropLastCharacters (1);
    n = n.replace ("b", " flat").replace ("#", " sharp");
    return n + (k.minor ? " minor" : " major");
}

juce::String camelot (const Key& k)
{
    if (! k.isValid()) return {};
    const int major = k.minor ? (k.tonic + 3) % 12 : k.tonic;   // minor keys share the number of their relative major
    const int number = ((major * 7) % 12 + 7) % 12 + 1;
    return juce::String (number) + (k.minor ? "A" : "B");
}

int semitonesTo (const Key& from, int toTonic)
{
    if (! from.isValid()) return 0;
    int d = ((toTonic - from.tonic) % 12 + 12) % 12;
    return d > 5 ? d - 12 : d;
}

} // namespace snag::key
