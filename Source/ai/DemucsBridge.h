#pragma once

// Plain C++ interface to the built-in Demucs engine (demucs.cpp).
//
// Everything that touches Eigen lives in DemucsBridge.cpp, which is compiled together with the
// engine (with its own optimisation flags). Only standard types cross this boundary, so the rest
// of the plug-in never needs Eigen's headers or alignment rules.

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace snagai
{
    constexpr int sampleRate = 44100;

    struct Model;   // opaque, immutable once loaded: safe to use from several threads

    /** Loads a demucs.cpp ggml model file. Returns nullptr and fills `error` on failure. */
    std::shared_ptr<Model> loadModel (const std::string& path, std::string& error);

    /** 4 (drums, bass, other, vocals) or 6 (+ guitar, piano). */
    int numSources (const Model&);

    /** Quick check of a model file's header ("dmc4" / "dmc6"). */
    bool looksLikeModelFile (const std::string& path);

    struct SeparateOptions
    {
        int numThreads = 1;
        std::function<void (float)> progress;      // 0..1, called from worker threads
        std::function<bool()> shouldCancel;        // polled from worker threads
    };

    /** Separates stereo audio at 44.1 kHz.
        On success `out` holds numSources * 2 planar channels: out[source * 2 + channel][sample].
        Returns false with `error` set on failure ("Cancelled" if cancelled). */
    bool separate (const Model& model, const float* left, const float* right, int numSamples,
                   std::vector<std::vector<float>>& out, const SeparateOptions& options, std::string& error);
}
