#include "AudioToMidi.h"
#include "OnnxLite.h"
#include "EditOps.h"
#include "SnaggerBinaryData.h"
#include <atomic>
#include <map>
#include <mutex>
#include <cmath>
#include <thread>

namespace snag::midi
{

namespace
{
    // Basic Pitch constants (basic_pitch/constants.py)
    constexpr int sampleRate = 22050;
    constexpr int fftHop = 256;
    constexpr int windowSamples = sampleRate * 2 - fftHop;        // 43844
    constexpr int framesPerWindow = 172;                           // ANNOT_N_FRAMES
    constexpr int overlapFrames = 30;
    constexpr int overlapSamples = overlapFrames * fftHop;         // 7680
    constexpr int hopSamples = windowSamples - overlapSamples;     // 36164
    constexpr int nPitches = 88, nContour = 264;
    constexpr int midiOffset = 21;

    const onnx::Model& model()
    {
        static const onnx::Model m (SnaggerBinary::basic_pitch_nmp_onnx, (size_t) SnaggerBinary::basic_pitch_nmp_onnxSize);
        return m;
    }
}

//==============================================================================
bool analyse (const AudioData& audio, Posteriors& out, juce::String& error,
              std::function<void (float)> progress, std::function<bool()> shouldCancel)
{
    const auto& net = model();
    if (! net.isValid() || net.inputNames().empty() || net.outputNames().size() < 3)
    {
        error = "The built-in note detector couldn't load (" + net.getError() + ")";
        return false;
    }

    // mono, 22.05 kHz
    AudioData::Ptr mono;
    {
        juce::AudioBuffer<float> m (1, audio.getNumSamples());
        m.clear();
        const int ch = audio.getNumChannels();
        for (int c = 0; c < ch; ++c)
            m.addFrom (0, 0, audio.buffer, c, 0, audio.getNumSamples(), 1.0f / (float) ch);
        mono = AudioData::make (std::move (m), audio.sampleRate);
    }
    if (std::abs (audio.sampleRate - sampleRate) > 0.5)
        mono = edit::resample (*mono, (double) sampleRate);

    const int originalLength = mono->getNumSamples();
    std::vector<float> x ((size_t) (overlapSamples / 2 + originalLength), 0.0f);
    std::copy (mono->buffer.getReadPointer (0), mono->buffer.getReadPointer (0) + originalLength, x.begin() + overlapSamples / 2);

    const int numWindows = juce::jmax (1, ((int) x.size() + hopSamples - 1) / hopSamples);
    struct WindowOut { std::vector<float> note, onset, contour; bool ok = false; };
    std::vector<WindowOut> results ((size_t) numWindows);

    const auto& outNames = net.outputNames();
    std::atomic<int> next { 0 }, done { 0 };
    std::atomic<bool> failed { false };
    juce::String firstError;
    std::mutex errLock;

    auto worker = [&]
    {
        for (;;)
        {
            const int w = next++;
            if (w >= numWindows || failed || (shouldCancel && shouldCancel()))
                return;
            std::vector<float> win ((size_t) windowSamples, 0.0f);
            const size_t start = (size_t) w * hopSamples;
            for (size_t i = 0; i < (size_t) windowSamples && start + i < x.size(); ++i)
                win[i] = x[start + i];

            std::map<std::string, onnx::Tensor> feeds, res;
            feeds[net.inputNames()[0]] = onnx::Tensor::floats ({ 1, windowSamples, 1 }, std::move (win));
            juce::String err;
            if (! net.run (feeds, res, err))
            {
                const std::lock_guard<std::mutex> sl (errLock);
                if (! failed.exchange (true)) firstError = err;
                return;
            }
            auto& r = results[(size_t) w];
            // Basic Pitch's outputs: StatefulPartitionedCall:0 = contour, :1 = note, :2 = onset
            for (auto& name : outNames)
            {
                auto& t = res[name];
                if (t.shape.size() != 3) continue;
                const auto tag = name.substr (name.find_last_of (':') + 1);
                if (t.shape[2] == nContour) r.contour = t.f;
                else if (t.shape[2] == nPitches && tag == "1") r.note = t.f;
                else if (t.shape[2] == nPitches && tag == "2") r.onset = t.f;
            }
            r.ok = r.contour.size() == (size_t) framesPerWindow * nContour && r.note.size() == (size_t) framesPerWindow * nPitches
                && r.onset.size() == r.note.size();
            if (! r.ok)
            {
                failed = true;
                return;
            }
            const int d = ++done;
            if (progress) progress ((float) d / (float) numWindows);
        }
    };

    const int threads = juce::jlimit (1, 16, juce::jmin (numWindows, (int) std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back (worker);
    worker();
    for (auto& t : pool) t.join();

    if (shouldCancel && shouldCancel())
    {
        error = "Cancelled";
        return false;
    }
    if (failed)
    {
        error = "Note detection failed" + (firstError.isNotEmpty() ? ": " + firstError : juce::String());
        return false;
    }

    // unwrap: drop half the overlap at each end of every window, join, trim to the audio's length
    const int keepPerWindow = framesPerWindow - overlapFrames;
    const int total = (int) ((double) originalLength / hopSamples * keepPerWindow);
    out.frames = juce::jmin (total, numWindows * keepPerWindow);
    out.note.assign ((size_t) out.frames * nPitches, 0.0f);
    out.onset.assign ((size_t) out.frames * nPitches, 0.0f);
    out.contour.assign ((size_t) out.frames * nContour, 0.0f);
    for (int f = 0; f < out.frames; ++f)
    {
        const int w = f / keepPerWindow, local = f % keepPerWindow + overlapFrames / 2;
        const auto& r = results[(size_t) w];
        std::copy_n (r.note.begin() + local * nPitches, nPitches, out.note.begin() + (size_t) f * nPitches);
        std::copy_n (r.onset.begin() + local * nPitches, nPitches, out.onset.begin() + (size_t) f * nPitches);
        std::copy_n (r.contour.begin() + local * nContour, nContour, out.contour.begin() + (size_t) f * nContour);
    }
    return true;
}

//==============================================================================
std::vector<Note> notesFrom (const Posteriors& P, const Options& opt)
{
    std::vector<Note> notes;
    const int T = P.frames;
    if (T < 3)
        return notes;

    const float s = juce::jlimit (0.0f, 1.0f, opt.sensitivity);
    const float onsetThresh = 0.7f - 0.4f * s;     // 0.5 at the default (Basic Pitch's)
    const float frameThresh = 0.45f - 0.3f * s;    // 0.3 at the default
    const int minNoteLen = (int) std::round (opt.minNoteMs / 1000.0 * ((double) sampleRate / fftHop));
    constexpr int energyTol = 11;
    constexpr int maxFreqIdx = nPitches - 1;

    std::vector<float> frames = P.note, onsets = P.onset;
    auto at = [] (std::vector<float>& v, int t, int f) -> float& { return v[(size_t) t * nPitches + (size_t) f]; };

    // constrain the pitch range
    const int lo = juce::jlimit (0, nPitches, opt.lowest - midiOffset), hi = juce::jlimit (0, nPitches, opt.highest - midiOffset + 1);
    for (int t = 0; t < T; ++t)
        for (int f = 0; f < nPitches; ++f)
            if (f < lo || f >= hi) at (frames, t, f) = at (onsets, t, f) = 0.0f;

    // infer onsets from sudden rises in the frame activations
    {
        std::vector<float> diff ((size_t) T * nPitches, 0.0f);
        float maxDiff = 0, maxOnset = 0;
        for (int t = 0; t < T; ++t)
            for (int f = 0; f < nPitches; ++f)
            {
                float d = std::numeric_limits<float>::max();
                for (int k = 1; k <= 2; ++k)
                    d = juce::jmin (d, at (frames, t, f) - (t - k >= 0 ? at (frames, t - k, f) : 0.0f));
                d = t < 2 ? 0.0f : juce::jmax (0.0f, d);
                diff[(size_t) t * nPitches + (size_t) f] = d;
                maxDiff = juce::jmax (maxDiff, d);
                maxOnset = juce::jmax (maxOnset, at (onsets, t, f));
            }
        if (maxDiff > 0)
            for (size_t k = 0; k < diff.size(); ++k)
                onsets[k] = juce::jmax (onsets[k], maxOnset * diff[k] / maxDiff);
    }

    // onset peaks above the threshold, latest first
    struct Peak { int t, f; };
    std::vector<Peak> peaks;
    for (int t = 1; t < T - 1; ++t)
        for (int f = 0; f < nPitches; ++f)
        {
            const float v = at (onsets, t, f);
            if (v >= onsetThresh && v > at (onsets, t - 1, f) && v > at (onsets, t + 1, f))
                peaks.push_back ({ t, f });
        }
    std::reverse (peaks.begin(), peaks.end());

    std::vector<float> remaining = frames;
    struct Raw { int start, end, pitch; float amp; };
    std::vector<Raw> raw;
    auto meanAmp = [&] (int a, int b, int f)
    {
        double sum = 0;
        for (int t = a; t < b; ++t) sum += at (frames, t, f);
        return b > a ? (float) (sum / (b - a)) : 0.0f;
    };

    for (auto& p : peaks)
    {
        if (p.t >= T - 1) continue;
        int i = p.t + 1, k = 0;
        while (i < T - 1 && k < energyTol)
        {
            if (at (remaining, i, p.f) < frameThresh) ++k; else k = 0;
            ++i;
        }
        i -= k;
        if (i - p.t <= minNoteLen) continue;
        for (int t = p.t; t < i; ++t)
        {
            at (remaining, t, p.f) = 0.0f;
            if (p.f < maxFreqIdx) at (remaining, t, p.f + 1) = 0.0f;
            if (p.f > 0) at (remaining, t, p.f - 1) = 0.0f;
        }
        raw.push_back ({ p.t, i, p.f + midiOffset, meanAmp (p.t, i, p.f) });
    }

    // "melodia trick": pick up notes the onsets missed, strongest first
    for (;;)
    {
        size_t best = 0;
        for (size_t q = 1; q < remaining.size(); ++q) if (remaining[q] > remaining[best]) best = q;
        if (remaining[best] <= frameThresh) break;
        const int iMid = (int) (best / nPitches), f = (int) (best % nPitches);
        remaining[best] = 0.0f;
        auto clear = [&] (int t) { at (remaining, t, f) = 0.0f; if (f < maxFreqIdx) at (remaining, t, f + 1) = 0.0f; if (f > 0) at (remaining, t, f - 1) = 0.0f; };
        int i = iMid + 1, k = 0;
        while (i < T - 1 && k < energyTol)
        {
            if (at (remaining, i, f) < frameThresh) ++k; else k = 0;
            clear (i);
            ++i;
        }
        const int iEnd = i - 1 - k;
        i = iMid - 1; k = 0;
        while (i > 0 && k < energyTol)
        {
            if (at (remaining, i, f) < frameThresh) ++k; else k = 0;
            clear (i);
            --i;
        }
        const int iStart = i + 1 + k;
        if (iEnd - iStart <= minNoteLen) continue;
        raw.push_back ({ iStart, iEnd, f + midiOffset, meanAmp (iStart, iEnd, f) });
    }

    // frames -> seconds (model_frames_to_time, incl. its window alignment)
    const double windowOffset = ((double) fftHop / sampleRate) * (framesPerWindow - (double) windowSamples / fftHop) + 0.0018;
    auto timeOf = [&] (int frame) { return frame * (double) fftHop / sampleRate - windowOffset * std::floor (frame / (double) framesPerWindow); };

    for (auto& r : raw)
        notes.push_back ({ juce::jmax (0.0, timeOf (r.start)), timeOf (r.end), r.pitch, juce::jlimit (0.05f, 1.0f, r.amp) });

    if (opt.melodyOnly)
    {
        // one note at a time: keep the strongest, drop what overlaps it
        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                   { return a.velocity * (a.end - a.start) > b.velocity * (b.end - b.start); });
        std::vector<Note> kept;
        for (auto& n : notes)
        {
            bool clash = false;
            for (auto& k : kept)
                if (n.start < k.end - 0.02 && k.start < n.end - 0.02) { clash = true; break; }
            if (! clash) kept.push_back (n);
        }
        notes = kept;
    }
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    return notes;
}

std::vector<Note> transcribe (const AudioData& a, const Options& o, juce::String& error,
                              std::function<void (float)> progress, std::function<bool()> shouldCancel)
{
    Posteriors p;
    if (! analyse (a, p, error, progress, shouldCancel))
        return {};
    return notesFrom (p, o);
}

//==============================================================================
juce::MidiFile toMidiFile (const std::vector<Note>& notes, double bpm, const juce::String& trackName)
{
    constexpr int ppq = 960;
    if (bpm <= 0) bpm = 120.0;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, trackName), 0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    auto ticks = [bpm] (double s) { return std::round (s * bpm / 60.0 * ppq); };
    for (auto& n : notes)
    {
        const double on = ticks (n.start), off = juce::jmax (on + 1.0, ticks (n.end));
        const auto vel = (juce::uint8) juce::jlimit (1, 127, (int) std::round (n.velocity * 127.0f));
        seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, vel), on);
        seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), off);
    }
    seq.updateMatchedPairs();
    seq.addEvent (juce::MidiMessage::endOfTrack(), seq.getEndTime());
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);
    file.addTrack (seq);
    return file;
}

bool writeMidiFile (const std::vector<Note>& notes, double bpm, const juce::String& trackName, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (! out.openedOk())
        return false;
    return toMidiFile (notes, bpm, trackName).writeTo (out);
}

AudioData::Ptr renderNotes (const std::vector<Note>& notes, double sr, double lengthSeconds)
{
    double len = lengthSeconds;
    for (auto& n : notes) len = juce::jmax (len, n.end + 0.5);
    juce::AudioBuffer<float> b (2, juce::jmax (1, (int) (len * sr)));
    b.clear();
    for (auto& n : notes)
    {
        const double hz = 440.0 * std::pow (2.0, (n.pitch - 69) / 12.0);
        const int s0 = (int) (n.start * sr), s1 = juce::jmin (b.getNumSamples(), (int) ((n.end + 0.25) * sr));
        const double dur = n.end - n.start;
        const float amp = 0.12f * (0.3f + 0.7f * n.velocity);
        for (int i = s0; i < s1; ++i)
        {
            const double t = (i - s0) / sr;
            const double w = juce::MathConstants<double>::twoPi * hz * t;
            const double tone = std::sin (w) + 0.35 * std::sin (2.0 * w) * std::exp (-t * 6.0) + 0.12 * std::sin (3.0 * w) * std::exp (-t * 9.0);
            double env = std::exp (-t * 1.2) * juce::jmin (1.0, t * 400.0);
            if (t > dur) env *= std::exp (-(t - dur) * 25.0);   // key up
            const float v = (float) (tone * env) * amp;
            b.addSample (0, i, v);
            b.addSample (1, i, v);
        }
    }
    const float peak = b.getMagnitude (0, 0, b.getNumSamples());
    if (peak > 0.9f) b.applyGain (0.9f / peak);
    return AudioData::make (std::move (b), sr);
}

juce::String noteRangeText (const std::vector<Note>& notes)
{
    if (notes.empty()) return "no notes";
    int lo = 127, hi = 0;
    for (auto& n : notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    auto nm = [] (int p) { return juce::MidiMessage::getMidiNoteName (p, true, true, 3); };
    return juce::String ((int) notes.size()) + (notes.size() == 1 ? " note" : " notes") + ", " + nm (lo) + (hi != lo ? " - " + nm (hi) : juce::String());
}

} // namespace snag::midi
