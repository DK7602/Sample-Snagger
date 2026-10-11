// Compiled together with demucs.cpp (see snagger_demucs in CMakeLists.txt).
#include "DemucsBridge.h"

#include "dsp.hpp"
#include "model.hpp"
#include "tensor.hpp"
#include <Eigen/Dense>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <thread>

#ifdef _WIN32
 #include <filesystem>
 #undef fopen   // (see Utf8Fopen.h: in this file the real one is called by name)

 namespace snagai
 {
     std::FILE* fopenUtf8 (const char* utf8Path, const char* mode)
     {
         if (utf8Path == nullptr || mode == nullptr)
             return nullptr;
         try
         {
             const std::wstring wide = std::filesystem::u8path (utf8Path).wstring();
             std::wstring wideMode;
             for (const char* m = mode; *m != 0; ++m)
                 wideMode.push_back ((wchar_t) *m);
             return _wfopen (wide.c_str(), wideMode.c_str());
         }
         catch (...) {}
         return nullptr;
     }
 }
#endif

namespace snagai
{

struct Model
{
    demucscpp::demucs_model m {};
};

namespace
{
    /** demucs.cpp logs a lot to std::cout (hundreds of lines per model load). Keep the host's
        console quiet while we work, with a refcount so overlapping jobs restore it correctly. */
    /** Discards everything and keeps no state, so worker threads can "print" concurrently. */
    struct NullBuffer : std::streambuf
    {
        int overflow (int c) override                                  { return traits_type::not_eof (c); }
        std::streamsize xsputn (const char*, std::streamsize n) override { return n; }
    };

    struct QuietCout
    {
        QuietCout()
        {
            const std::lock_guard<std::mutex> lock (mutex());
            if (depth()++ == 0)
                saved() = std::cout.rdbuf (&sink());
        }
        ~QuietCout()
        {
            const std::lock_guard<std::mutex> lock (mutex());
            if (--depth() == 0)
                std::cout.rdbuf (saved());
        }
        static std::mutex& mutex()           { static std::mutex m; return m; }
        static int& depth()                  { static int d = 0; return d; }
        static std::streambuf*& saved()      { static std::streambuf* s = nullptr; return s; }
        static NullBuffer& sink()            { static NullBuffer b; return b; }
    };

    struct Cancelled {};
}

bool looksLikeModelFile (const std::string& path)
{
   #ifdef _WIN32
    std::FILE* f = fopenUtf8 (path.c_str(), "rb");
   #else
    std::FILE* f = std::fopen (path.c_str(), "rb");
   #endif
    if (f == nullptr)
        return false;
    uint32_t magic = 0;
    const bool ok = std::fread (&magic, sizeof (magic), 1, f) == 1;
    std::fclose (f);
    return ok && (magic == 0x646d6334 /* dmc4 */ || magic == 0x646d6336 /* dmc6 */);
}

std::shared_ptr<Model> loadModel (const std::string& path, std::string& error)
{
    if (! looksLikeModelFile (path))
    {
        error = "Not a valid AI model file: " + path;
        return nullptr;
    }

    try
    {
        QuietCout quiet;
        auto model = std::make_shared<Model>();
        if (! demucscpp::load_demucs_model (path, &model->m))
        {
            error = "The AI model file could not be loaded (it may be damaged).";
            return nullptr;
        }
        return model;
    }
    catch (const std::exception& e)
    {
        error = std::string ("Loading the AI model failed: ") + e.what();
    }
    catch (...)
    {
        error = "Loading the AI model failed";
    }
    return nullptr;
}

int numSources (const Model& model)
{
    return model.m.is_4sources ? 4 : 6;
}

std::vector<int> planWindows (int numSamples, int segment, int overlap)
{
    if (numSamples <= 0 || segment <= 0)
        return {};
    if (numSamples <= segment)
        return { -((segment - numSamples) / 2) };   // one look, with the audio in the middle of it

    const int hop = std::max (1, segment - std::max (0, overlap));
    std::vector<int> starts;
    for (long long s = 0; s + segment < numSamples; s += hop)
        starts.push_back ((int) s);
    starts.push_back (numSamples - segment);   // the last look ends with the audio: sound to go by, not padding
    return starts;
}

bool separateWith (int sources, int segment, int overlap, const std::function<WindowFn()>& makeWorker,
                   const float* left, const float* right, int numSamples,
                   std::vector<std::vector<float>>& out, const SeparateOptions& options, std::string& error)
{
    out.clear();
    if (numSamples <= 0 || sources <= 0 || segment <= 0)
    {
        error = "No audio";
        return false;
    }

    auto clean = [] (float v) { return std::isfinite (v) ? v : 0.0f; };

    // Demucs works on audio scaled by how much the whole piece varies (around its average), and the
    // parts are scaled back afterwards.
    double sum = 0.0, sumOfSquares = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const double mono = 0.5 * ((double) clean (left[i]) + (double) clean (right[i]));
        sum += mono;
        sumOfSquares += mono * mono;
    }
    const double mean = sum / (double) numSamples;
    const double variance = numSamples > 1 ? (sumOfSquares - (double) numSamples * mean * mean) / (double) (numSamples - 1) : 0.0;
    const double deviation = variance > 0.0 ? std::sqrt (variance) : 0.0;

    out.assign ((size_t) sources * 2, std::vector<float> ((size_t) numSamples, 0.0f));
    if (! (deviation > 1.0e-8))
    {
        // Nothing but digital silence (or a constant): there is nothing to separate, and scaling
        // by "no variation" would be a division by zero.
        if (options.progress)
            options.progress (1.0f);
        return true;
    }
    const float offset = (float) mean, scale = (float) (1.0 / deviation);
    const float silence = -offset * scale;   // what a sample of 0 is, scaled

    // The network takes a fixed stretch of audio at a time. Each look shares `overlap` samples with
    // the next and the two are cross-faded there. (demucs.cpp's own splitting ran the network a
    // second time, on a mostly empty window, for the tail of every stretch we gave it: about twice
    // the work, and it blended that poorer second guess into the result.)
    const auto windows = planWindows (numSamples, segment, overlap);
    std::vector<float> weightSum ((size_t) numSamples, 0.0f);

    std::mutex accumulate;
    std::atomic<int> nextWindow { 0 };
    std::atomic<bool> failed { false }, cancelled { false };
    std::string firstError;
    std::mutex errorLock;

    std::vector<std::atomic<float>> windowProgress (windows.size());
    for (auto& p : windowProgress) p = 0.0f;

    auto report = [&]
    {
        if (! options.progress) return;
        float total = 0.0f;
        for (auto& p : windowProgress) total += p.load();
        options.progress (total / (float) windows.size());
    };

    auto fail = [&] (const std::string& message)
    {
        const std::lock_guard<std::mutex> lock (errorLock);
        if (firstError.empty()) firstError = message;
        failed = true;
    };

    auto worker = [&]
    {
        // (everything in the try: an exception that escapes a worker thread would end the whole DAW)
        try
        {
            WindowFn network;   // made on first use: a worker that finds no work left needs no buffers
            std::vector<float> inL ((size_t) segment), inR ((size_t) segment);
            std::vector<std::vector<float>> parts ((size_t) sources * 2, std::vector<float> ((size_t) segment));

            for (;;)
            {
                const int index = nextWindow++;
                if (index >= (int) windows.size() || failed || cancelled)
                    return;

                if (options.shouldCancel && options.shouldCancel())
                {
                    cancelled = true;
                    return;
                }

                const int start = windows[(size_t) index];
                float lowest = std::numeric_limits<float>::max(), highest = std::numeric_limits<float>::lowest();
                for (int k = 0; k < segment; ++k)
                {
                    const int src = start + k;
                    if (src >= 0 && src < numSamples)
                    {
                        const float l = clean (left[src]), r = clean (right[src]);
                        inL[(size_t) k] = (l - offset) * scale;
                        inR[(size_t) k] = (r - offset) * scale;
                        lowest = std::min (lowest, l + r);
                        highest = std::max (highest, l + r);
                    }
                    else
                    {
                        inL[(size_t) k] = inR[(size_t) k] = 0.0f;
                    }
                }

                // A stretch of digital silence (a track's tail, the gap before a grab starts) has
                // nothing in it to separate: it stays silence, and costs no time.
                const bool flat = ! (highest - lowest > 1.0e-7f);
                if (! flat)
                {
                    if (! network)
                        network = makeWorker();
                    network (inL, inR, parts, [&, index] (float p)
                    {
                        if ((options.shouldCancel && options.shouldCancel()) || cancelled || failed)
                            throw Cancelled();
                        windowProgress[(size_t) index] = std::clamp (p, 0.0f, 1.0f);
                        report();
                    });
                }

                {
                    const std::lock_guard<std::mutex> lock (accumulate);
                    const int from = std::max (0, -start), to = std::min (segment, numSamples - start);
                    for (int k = from; k < to; ++k)
                        weightSum[(size_t) (start + k)] += (float) std::min (k + 1, segment - k);   // triangular cross-fade
                    for (size_t c = 0; c < parts.size(); ++c)
                    {
                        auto& channel = out[c];
                        const float* part = parts[c].data();
                        for (int k = from; k < to; ++k)
                        {
                            const float v = flat ? silence : part[k];
                            if (std::isfinite (v))
                                channel[(size_t) (start + k)] += (float) std::min (k + 1, segment - k) * v;
                        }
                    }
                }
                windowProgress[(size_t) index] = 1.0f;
                report();
            }
        }
        catch (const Cancelled&)
        {
            cancelled = true;
        }
        catch (const std::bad_alloc&)
        {
            fail ("AI separation failed: not enough free memory. Close other programs and try again.");
        }
        catch (const std::exception& e)
        {
            fail (std::string ("AI separation failed: ") + e.what());
        }
        catch (...)
        {
            fail ("AI separation failed");
        }
    };

    {
        const int threads = std::max (1, std::min (options.numThreads, (int) windows.size()));
        std::vector<std::thread> pool;
        try
        {
            for (int t = 1; t < threads; ++t)
                pool.emplace_back (worker);
        }
        catch (...) {}   // the system won't give us more threads: carry on with the ones we have
        worker();   // the calling thread works too
        for (auto& t : pool)
            t.join();
    }

    if (cancelled)
    {
        out.clear();
        error = "Cancelled";
        return false;
    }
    if (failed)
    {
        out.clear();
        error = firstError;
        return false;
    }

    const float back = (float) deviation;
    for (auto& channel : out)
        for (size_t i = 0; i < weightSum.size(); ++i)
            channel[i] = weightSum[i] > 0.0f ? channel[i] / weightSum[i] * back + offset : 0.0f;

    if (options.progress)
        options.progress (1.0f);
    return true;
}

bool separate (const Model& model, const float* left, const float* right, int numSamples,
               std::vector<std::vector<float>>& out, const SeparateOptions& options, std::string& error)
{
    const int sources = numSources (model);
    const int segment = (int) (demucscpp::SEGMENT_LEN_SECS * demucscpp::SUPPORTED_SAMPLE_RATE);   // 7.8 s
    const int overlap = (int) (1.2f * (float) sampleRate);

    // One set of working buffers per thread, used for every look that thread takes.
    struct Buffers
    {
        Buffers (int segmentSamples, int numSourcesIn)
            : segment (2, segmentSamples, numSourcesIn), stft (segment.padded_segment_samples) {}
        demucscpp::demucs_segment_buffers segment;
        demucscpp::stft_buffers stft;
    };

    auto makeWorker = [&model, sources, segment]() -> WindowFn
    {
        auto buffers = std::make_shared<Buffers> (segment, sources);
        return [&model, sources, segment, buffers] (const std::vector<float>& l, const std::vector<float>& r,
                                                    std::vector<std::vector<float>>& parts, const std::function<void (float)>& progress)
        {
            auto& b = *buffers;
            for (int k = 0; k < segment; ++k)
            {
                b.segment.mix (0, k) = l[(size_t) k];
                b.segment.mix (1, k) = r[(size_t) k];
            }
            demucscpp::model_inference (model.m, b.segment, b.stft,
                                        [&progress] (float p, const std::string&) { progress (p); }, 0.0f, 1.0f);
            for (int s = 0; s < sources; ++s)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* dst = parts[(size_t) (s * 2 + ch)].data();
                    for (int k = 0; k < segment; ++k)
                        dst[k] = b.segment.targets_out (s, ch, k);
                }
        };
    };

    QuietCout quiet;
    return separateWith (sources, segment, overlap, makeWorker, left, right, numSamples, out, options, error);
}

} // namespace snagai
