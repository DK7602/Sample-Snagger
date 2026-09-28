#pragma once

#include "AudioData.h"
#include "Jobs.h"
#include <vector>

/** Built-in AI stem separation (Demucs v4 running natively - no Python).

    Models are downloaded once, on first use, into the Sample Snagger "Models" folder. */
namespace snag::ai
{
    enum class ModelId { htdemucs4, htdemucs6, ftVocals, ftDrums, ftBass, ftOther };

    struct ModelInfo
    {
        ModelId id;
        const char* fileName;
        const char* label;
        int megabytes;
    };

    const ModelInfo& info (ModelId);
    juce::File modelsDir();
    juce::File modelFile (ModelId);
    bool isDownloaded (ModelId);

    /** True when the engine is compiled into this build. */
    bool isAvailable();
    juce::String unavailableReason();

    enum class Mode
    {
        vocalsMusic,    // fine-tuned vocal model -> vocals + everything else (exact remainder)
        fourStems,      // htdemucs: drums, bass, other, vocals
        fourStemsMax,   // four fine-tuned models, one per stem (about 4x slower)
        sixStems        // htdemucs_6s: + guitar, piano
    };

    std::vector<ModelId> modelsFor (Mode);
    int downloadMegabytesFor (Mode);          // 0 if everything is already on disk
    juce::String defaultModelBaseUrl();

    /** Downloads (if needed) the models for a mode. Runs inside a Job. */
    bool ensureModels (Mode, Job&, const juce::String& baseUrl, float p0, float p1);

    struct Stem
    {
        juce::String name;      // "vocals", "music", "drums", "bass", "other", "guitar", "piano"
        AudioData::Ptr audio;
    };

    /** Full pipeline: download models if needed, resample, separate on all cores, resample back.
        Runs inside a Job (reports progress, honours cancel). */
    bool separate (const AudioData& input, Mode mode, Job& job, const juce::String& baseUrl,
                   std::vector<Stem>& stems);

    int defaultThreadCount();
}
