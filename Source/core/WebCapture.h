#pragma once

#include "AudioData.h"
#include <juce_events/juce_events.h>
#include <functional>

namespace snag
{

//==============================================================================
/** Receives audio from the page in the built-in browser (via the injected webtap.js).

    Keeps a rolling "hindsight" buffer of the last minute that played, so you can grab
    a moment *after* you heard it, and can also record forward like a tape deck. */
class WebCapture : private juce::Timer
{
public:
    using ResultCallback = std::function<void (const juce::var&)>;
    using Evaluator      = std::function<void (const juce::String& script, ResultCallback)>;

    explicit WebCapture (Evaluator evaluator);
    ~WebCapture() override;

    static constexpr double hindsightSeconds = 60.0;
    static constexpr double maxRecordSeconds = 15.0 * 60.0;

    void setArmed (bool shouldListen);
    bool isArmed() const noexcept               { return armed; }

    void startRecording();
    AudioData::Ptr stopRecording();
    bool isRecording() const noexcept           { return recording; }
    double getRecordedSeconds() const;

    /** Copies the last N seconds of audio that played in the browser. */
    AudioData::Ptr grabLast (double seconds) const;
    double getAvailableHindsight() const;

    float getLevel() const noexcept             { return level; }
    bool isMediaPlaying() const noexcept        { return mediaPlaying; }
    int getNumTaps() const noexcept             { return numTaps; }
    juce::String getContextState() const        { return contextState; }
    double getSampleRate() const noexcept       { return sampleRate; }

    /** Must be called when the browser navigates to a new document. */
    void pageChanged()                          { injected = false; }

    /** Decodes one drain() result. Public so it can be unit-tested. */
    void ingest (const juce::var& drainResult);

    /** Adds interleaved stereo frames directly (used by ingest + tests). */
    void pushFrames (const float* interleaved, int numFrames, double sr);

    static juce::String getTapScript();

    std::function<void()> onStateChanged;

private:
    void timerCallback() override;
    void poll();

    Evaluator evaluate;
    bool armed = false, recording = false, injected = false, waiting = false;
    int pollsWithoutAnswer = 0;

    double sampleRate = 0.0;
    juce::AudioBuffer<float> ring;
    int ringWrite = 0, ringFilled = 0;

    std::vector<float> recL, recR;

    float level = 0.0f;
    bool mediaPlaying = false;
    int numTaps = 0;
    juce::String contextState;

    JUCE_DECLARE_WEAK_REFERENCEABLE (WebCapture)
};

} // namespace snag
