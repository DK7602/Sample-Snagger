#include "AiStems.h"
#include "EditOps.h"
#include "Settings.h"
#include "Tools.h"

#if SNAGGER_BUILTIN_AI
 #include "DemucsBridge.h"
#endif

namespace snag::ai
{

static const ModelInfo models[] = {
    { ModelId::htdemucs4, "ggml-model-htdemucs-4s-f16.bin",           "4-stem model",           81 },
    { ModelId::htdemucs6, "ggml-model-htdemucs-6s-f16.bin",           "6-stem model",           53 },
    { ModelId::ftVocals,  "ggml-model-htdemucs_ft_vocals-4s-f16.bin", "fine-tuned vocal model", 81 },
    { ModelId::ftDrums,   "ggml-model-htdemucs_ft_drums-4s-f16.bin",  "fine-tuned drum model",  81 },
    { ModelId::ftBass,    "ggml-model-htdemucs_ft_bass-4s-f16.bin",   "fine-tuned bass model",  81 },
    { ModelId::ftOther,   "ggml-model-htdemucs_ft_other-4s-f16.bin",  "fine-tuned other model", 81 },
};

const ModelInfo& info (ModelId id)
{
    for (auto& m : models)
        if (m.id == id)
            return m;
    return models[0];
}

juce::File modelsDir()
{
    auto d = paths::appDataDir().getChildFile ("Models");
    d.createDirectory();
    return d;
}

juce::File modelFile (ModelId id)   { return modelsDir().getChildFile (info (id).fileName); }

bool isDownloaded (ModelId id)
{
    auto f = modelFile (id);
   #if SNAGGER_BUILTIN_AI
    return f.getSize() > 10 * 1024 * 1024 && snagai::looksLikeModelFile (f.getFullPathName().toStdString());
   #else
    return f.getSize() > 10 * 1024 * 1024;
   #endif
}

bool isAvailable()
{
   #if SNAGGER_BUILTIN_AI
    return true;
   #else
    return false;
   #endif
}

juce::String unavailableReason()
{
    if (! isAvailable())
        return "This build doesn't include the built-in AI engine.";
    return {};
}

std::vector<ModelId> modelsFor (Mode mode)
{
    switch (mode)
    {
        case Mode::vocalsMusic:  return { ModelId::ftVocals };
        case Mode::fourStems:    return { ModelId::htdemucs4 };
        case Mode::fourStemsMax: return { ModelId::ftDrums, ModelId::ftBass, ModelId::ftOther, ModelId::ftVocals };
        case Mode::sixStems:     return { ModelId::htdemucs6 };
    }
    return {};
}

int downloadMegabytesFor (Mode mode)
{
    int mb = 0;
    for (auto id : modelsFor (mode))
        if (! isDownloaded (id))
            mb += info (id).megabytes;
    return mb;
}

juce::String defaultModelBaseUrl()
{
    return "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main/";
}

bool ensureModels (Mode mode, Job& job, const juce::String& baseUrlIn, float p0, float p1)
{
    auto baseUrl = baseUrlIn.isNotEmpty() ? baseUrlIn : defaultModelBaseUrl();
    if (! baseUrl.endsWithChar ('/'))
        baseUrl << "/";

    std::vector<ModelId> missing;
    for (auto id : modelsFor (mode))
        if (! isDownloaded (id))
            missing.push_back (id);

    for (size_t i = 0; i < missing.size(); ++i)
    {
        const auto& m = info (missing[i]);
        const float a = p0 + (p1 - p0) * (float) i / (float) missing.size();
        const float b = p0 + (p1 - p0) * (float) (i + 1) / (float) missing.size();
        auto dest = modelFile (m.id);

        job.setStatus ("Downloading the AI " + juce::String (m.label) + " (" + juce::String (m.megabytes) + " MB, one time only)");
        if (! ToolManager::download (baseUrl + m.fileName, dest, job, a, b))
        {
            if (! job.isCancelled() && ! job.hasFailed())
                job.fail ("Couldn't download the AI model. Check your internet connection and try again.");
            return false;
        }
        if (! isDownloaded (m.id))
        {
            dest.deleteFile();
            job.fail ("The downloaded AI model looks damaged. Please try again.");
            return false;
        }
    }
    return true;
}

int defaultThreadCount()
{
    const int cores = juce::jmax (1, juce::SystemStats::getNumPhysicalCpus());
    const int ramMB = juce::SystemStats::getMemorySizeInMegabytes();
    const int byRam = ramMB > 0 ? juce::jmax (1, ramMB / 1500) : 2;   // ~1 GB working memory per worker
    return juce::jlimit (1, 12, juce::jmin (cores, byRam));
}

//==============================================================================
#if SNAGGER_BUILTIN_AI
static AudioData::Ptr toSourceRate (std::vector<float>& l, std::vector<float>& r, double sourceRate, int sourceLength)
{
    const int n = (int) l.size();
    juce::AudioBuffer<float> b (2, n);
    b.copyFrom (0, 0, l.data(), n);
    b.copyFrom (1, 0, r.data(), n);
    auto a = AudioData::make (std::move (b), (double) snagai::sampleRate);
    if (std::abs (sourceRate - snagai::sampleRate) > 0.5)
        a = edit::resample (*a, sourceRate);

    // exact length match with the original, so stems line up sample for sample
    juce::AudioBuffer<float> fixed (2, sourceLength);
    fixed.clear();
    const int copy = juce::jmin (sourceLength, a->getNumSamples());
    for (int c = 0; c < 2; ++c)
        fixed.copyFrom (c, 0, a->buffer, c, 0, copy);
    return AudioData::make (std::move (fixed), sourceRate);
}
#endif

bool separate (const AudioData& input, Mode mode, Job& job, const juce::String& baseUrl, std::vector<Stem>& stems)
{
    stems.clear();
   #if ! SNAGGER_BUILTIN_AI
    juce::ignoreUnused (input, mode, baseUrl);
    job.fail (unavailableReason());
    return false;
   #else
    if (! isAvailable())
    {
        job.fail (unavailableReason());
        return false;
    }

    const int downloadMB = downloadMegabytesFor (mode);
    const float separateStart = downloadMB > 0 ? 0.25f : 0.02f;
    if (! ensureModels (mode, job, baseUrl, 0.0f, separateStart))
        return false;

    // Demucs runs at 44.1 kHz stereo
    job.setStatus ("Preparing audio");
    AudioData::Ptr work;
    {
        juce::AudioBuffer<float> st (2, input.getNumSamples());
        for (int c = 0; c < 2; ++c)
            st.copyFrom (c, 0, input.buffer, juce::jmin (c, input.getNumChannels() - 1), 0, input.getNumSamples());
        work = AudioData::make (std::move (st), input.sampleRate);
        if (std::abs (input.sampleRate - snagai::sampleRate) > 0.5)
            work = edit::resample (*work, (double) snagai::sampleRate);
    }

    const auto ids = modelsFor (mode);
    const int passes = (int) ids.size();
    const int threads = defaultThreadCount();
    std::vector<std::vector<float>> combined;   // [source*2 + ch] at 44.1 kHz

    for (int pass = 0; pass < passes; ++pass)
    {
        const auto& m = info (ids[(size_t) pass]);
        job.setStatus ("Loading the AI " + juce::String (m.label));
        std::string err;
        auto model = snagai::loadModel (modelFile (m.id).getFullPathName().toStdString(), err);
        if (model == nullptr)
        {
            job.fail (juce::String (err));
            return false;
        }

        job.setStatus (passes > 1 ? "Separating (" + juce::String (pass + 1) + " of " + juce::String (passes) + ") on "
                                        + juce::String (threads) + " cores"
                                  : "Separating on " + juce::String (threads) + " CPU cores");

        snagai::SeparateOptions opts;
        opts.numThreads = threads;
        const float a = separateStart + (0.97f - separateStart) * (float) pass / (float) passes;
        const float b = separateStart + (0.97f - separateStart) * (float) (pass + 1) / (float) passes;
        opts.progress = [&job, a, b] (float p) { job.setProgress (a + (b - a) * p); };
        opts.shouldCancel = job.cancelCheck();

        std::vector<std::vector<float>> out;
        if (! snagai::separate (*model, work->buffer.getReadPointer (0), work->buffer.getReadPointer (1),
                                work->getNumSamples(), out, opts, err))
        {
            if (err != "Cancelled")
                job.fail (juce::String (err));
            return false;
        }

        if (mode == Mode::fourStemsMax)
        {
            // each fine-tuned model contributes only its own stem (0 drums, 1 bass, 2 other, 3 vocals)
            const int target = m.id == ModelId::ftDrums ? 0 : m.id == ModelId::ftBass ? 1 : m.id == ModelId::ftOther ? 2 : 3;
            if (combined.empty())
                combined.assign (8, {});
            combined[(size_t) (target * 2)]     = std::move (out[(size_t) (target * 2)]);
            combined[(size_t) (target * 2 + 1)] = std::move (out[(size_t) (target * 2 + 1)]);
        }
        else
        {
            combined = std::move (out);
        }
    }

    job.setStatus ("Finishing stems");
    const int n = input.getNumSamples();
    const double sr = input.sampleRate;

    if (mode == Mode::vocalsMusic)
    {
        auto vocals = toSourceRate (combined[6], combined[7], sr, n);
        // music = original - vocals, so the two stems always add back up to the original exactly
        juce::AudioBuffer<float> music (2, n);
        for (int c = 0; c < 2; ++c)
        {
            music.copyFrom (c, 0, input.buffer, juce::jmin (c, input.getNumChannels() - 1), 0, n);
            music.addFrom (c, 0, vocals->buffer, c, 0, n, -1.0f);
        }
        stems.push_back ({ "vocals", vocals });
        stems.push_back ({ "music", AudioData::make (std::move (music), sr) });
    }
    else
    {
        const char* names[] = { "drums", "bass", "other", "vocals", "guitar", "piano" };
        const int sources = (int) combined.size() / 2;
        for (int s = 0; s < sources; ++s)
            if (! combined[(size_t) (s * 2)].empty())
                stems.push_back ({ names[s], toSourceRate (combined[(size_t) (s * 2)], combined[(size_t) (s * 2 + 1)], sr, n) });
        // present vocals first - it's what most people are after
        std::stable_sort (stems.begin(), stems.end(), [] (const Stem& x, const Stem& y)
                          { return x.name == "vocals" && y.name != "vocals"; });
    }

    job.setProgress (1.0f);
    return ! stems.empty();
   #endif
}

} // namespace snag::ai
