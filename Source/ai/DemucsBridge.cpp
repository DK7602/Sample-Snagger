// Compiled together with demucs.cpp (see snagger_demucs in CMakeLists.txt).
#include "DemucsBridge.h"

#include "model.hpp"
#include "tensor.hpp"
#include <Eigen/Dense>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

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
    FILE* f = std::fopen (path.c_str(), "rb");
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
            error = "The AI model file could not be loaded (it may be damaged - delete it and try again)";
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

bool separate (const Model& model, const float* left, const float* right, int numSamples,
               std::vector<std::vector<float>>& out, const SeparateOptions& options, std::string& error)
{
    out.clear();
    if (numSamples <= 0)
    {
        error = "No audio";
        return false;
    }

    const int sources = numSources (model);

    // Chunks of at most 7.2 s (6 s core + 0.6 s context each side). Demucs adds a random shift of
    // up to 0.5 s, so every chunk fits in exactly one of its 7.8 s windows: no wasted compute, and
    // the chunks spread evenly over the CPU cores. Neighbouring chunks are cross-faded.
    const int core = 6 * sampleRate;
    const int pad  = (int) (0.6f * (float) sampleRate);

    struct Chunk { int start, end; };
    std::vector<Chunk> chunks;
    for (int c = 0; (long long) c * core < numSamples; ++c)
        chunks.push_back ({ c * core - pad, std::min (numSamples, (c + 1) * core) + pad });

    out.assign ((size_t) sources * 2, std::vector<float> ((size_t) numSamples, 0.0f));
    std::vector<float> weightSum ((size_t) numSamples, 0.0f);

    std::mutex accumulate;
    std::atomic<int> nextChunk { 0 };
    std::atomic<bool> failed { false }, cancelled { false };
    std::string firstError;
    std::mutex errorLock;

    std::vector<std::atomic<float>> chunkProgress (chunks.size());
    for (auto& p : chunkProgress) p = 0.0f;

    auto report = [&]
    {
        if (! options.progress) return;
        float sum = 0.0f;
        for (auto& p : chunkProgress) sum += p.load();
        options.progress (sum / (float) chunks.size());
    };

    auto worker = [&]
    {
        for (;;)
        {
            const int index = nextChunk++;
            if (index >= (int) chunks.size() || failed || cancelled)
                return;

            if (options.shouldCancel && options.shouldCancel())
            {
                cancelled = true;
                return;
            }

            const auto chunk = chunks[(size_t) index];
            const int len = chunk.end - chunk.start;

            Eigen::MatrixXf audio = Eigen::MatrixXf::Zero (2, len);
            for (int i = 0; i < len; ++i)
            {
                const int src = chunk.start + i;
                if (src >= 0 && src < numSamples)
                {
                    audio (0, i) = left[src];
                    audio (1, i) = right[src];
                }
            }

            demucscpp::ProgressCallback cb = [&, index] (float p, const std::string&)
            {
                if ((options.shouldCancel && options.shouldCancel()) || cancelled || failed)
                    throw Cancelled();
                chunkProgress[(size_t) index] = std::clamp (p, 0.0f, 1.0f);
                report();
            };

            try
            {
                Eigen::Tensor3dXf result = demucscpp::demucs_inference (model.m, audio, cb);

                const int outLen = std::min (len, (int) result.dimension (2));
                const std::lock_guard<std::mutex> lock (accumulate);
                for (int i = 0; i < outLen; ++i)
                {
                    const int dst = chunk.start + i;
                    if (dst < 0 || dst >= numSamples)
                        continue;
                    const float w = (float) std::min (i + 1, len - i);   // triangular cross-fade
                    weightSum[(size_t) dst] += w;
                    for (int s = 0; s < sources; ++s)
                        for (int ch = 0; ch < 2; ++ch)
                            out[(size_t) (s * 2 + ch)][(size_t) dst] += w * result (s, ch, i);
                }
                chunkProgress[(size_t) index] = 1.0f;
                report();
            }
            catch (const Cancelled&)
            {
                cancelled = true;
                return;
            }
            catch (const std::exception& e)
            {
                const std::lock_guard<std::mutex> lock (errorLock);
                if (firstError.empty()) firstError = std::string ("AI separation failed: ") + e.what();
                failed = true;
                return;
            }
            catch (...)
            {
                const std::lock_guard<std::mutex> lock (errorLock);
                if (firstError.empty()) firstError = "AI separation failed";
                failed = true;
                return;
            }
        }
    };

    {
        QuietCout quiet;
        const int threads = std::max (1, std::min (options.numThreads, (int) chunks.size()));
        std::vector<std::thread> pool;
        for (int t = 1; t < threads; ++t)
            pool.emplace_back (worker);
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

    for (size_t i = 0; i < weightSum.size(); ++i)
    {
        const float w = weightSum[i];
        if (w > 0.0f)
            for (auto& channel : out)
                channel[i] /= w;
    }
    if (options.progress)
        options.progress (1.0f);
    return true;
}

} // namespace snagai
