#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "core/AudioData.h"
#include "core/Session.h"
#include "core/Jobs.h"
#include "core/Tools.h"
#include "core/RtShared.h"
#include "core/PadFx.h"
#include "core/FxRack.h"
#include <array>

//==============================================================================
class SnaggerProcessor final : public juce::AudioProcessor,
                               private juce::Timer
{
public:
    SnaggerProcessor();
    ~SnaggerProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return "Sample Snagger"; }
    bool acceptsMidi() const override                      { return true; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 4.0; }   // FX echoes / reverb

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    juce::SharedResourcePointer<snag::SharedServices> services;
    snag::Session session;
    snag::JobManager jobs;

    snag::Settings& getSettings()    { return services->settings; }
    snag::ToolManager& getTools()    { return services->tools; }

    //==============================================================================
    // Sampler (MIDI-playable chops) -- message thread
    void setSamplerClip (snag::Clip::Ptr clip, int selStart = 0, int selEnd = -1);
    snag::Clip::Ptr getSamplerClip() const   { return samplerClip; }

    enum MidiMode { chops = 0, keys = 1 };
    std::atomic<int>   midiMode   { chops };
    std::atomic<bool>  oneShot    { true };
    std::atomic<int>   rootNote   { 60 };      // C3 (Ableton/FL naming) triggers slice 1
    std::atomic<float> masterGain { 1.0f };

    /** Trigger a note from the UI (pads). velocity 0 = note off. */
    void sendUiNote (int note, float velocity);

    std::atomic<int> lastTriggeredSlice { -1 };
    std::atomic<juce::uint32> triggerCounter { 0 };

    //==============================================================================
    // Preview player -- message thread
    /** withFx: run it through the sampler clip's FX rack (STUDIO playback), not for stems / library. */
    void preview (std::vector<snag::AudioData::Ptr> layers, int start, int end, bool loop, bool withFx = false);
    void previewClip (const snag::Clip& clip, int start = 0, int end = -1, bool loop = false, bool withFx = false);
    void stopPreview();
    bool isPreviewing() const noexcept              { return previewPlaying.load(); }
    double getPreviewPosition() const noexcept      { return previewPos.load(); }   // in source samples
    snag::AudioData* getPreviewSource() const;      // first layer of what's playing (for playhead matching)
    std::array<std::atomic<float>, 8> layerGains;

    //==============================================================================
    // Input recording
    bool hasAudioInput() const;
    bool startInputRecording (juce::String& error);
    snag::Clip::Ptr stopInputRecording();
    bool isRecordingInput() const noexcept          { return activeWriter.load() != nullptr; }
    double getInputRecordSeconds() const;
    std::atomic<float> inputLevel { 0.0f }, outputLevel { 0.0f };

    std::atomic<double> hostBpm { 0.0 };
    double getCurrentSampleRate() const noexcept    { return currentSampleRate; }

    /** Set by the editor: toast messages and "open settings" requests from background jobs. */
    std::function<void (const juce::String&, bool)> onNotify;
    std::function<void()> onRequestSettings;

    /** Editor size / tab etc. */
    juce::ValueTree uiState { "UI" };

private:
    static BusesProperties makeBuses();
    void timerCallback() override;

    //==============================================================================
    struct SamplerState final : juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<SamplerState>;
        snag::AudioData::Ptr audio;
        std::vector<int> bounds;   // slice boundaries incl. 0 and end
        int selStart = 0, selEnd = 0;
        std::vector<snag::PadParams> pads;
        snag::FxSettings fx;
        double bpm = 0.0;
    };

    struct PreviewState final : juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<PreviewState>;
        std::vector<snag::AudioData::Ptr> layers;
        int start = 0, end = 0;
        bool loop = false, stop = false, fx = false;
        juce::uint32 serial = 0;
    };

    snag::RtShared<SamplerState> samplerShared;
    snag::RtShared<PreviewState> previewShared;
    snag::Clip::Ptr samplerClip;
    juce::uint32 previewSerial = 0;

    //==============================================================================
    struct Voice
    {
        SamplerState::Ptr state;
        snag::PadVoice pv;
        bool active = false;
        int note = -1;
        juce::uint32 age = 0;
    };
    std::array<Voice, 16> voices;
    juce::uint32 voiceAge = 0;

    void noteOn (int note, float velocity, SamplerState::Ptr state);
    void noteOff (int note);
    void renderVoices (juce::AudioBuffer<float>&, int start, int num);

    // FX rack on the pads + STUDIO playback
    snag::FxChain fxChain;
    juce::AudioBuffer<float> fxBus;
    bool fxWasOn = false;

    // preview (audio thread state)
    PreviewState::Ptr activePreview;
    juce::uint32 activePreviewSerial = 0;
    double pPos = 0;
    float pEnv = 0;
    bool pRunning = false;
    std::atomic<bool> previewPlaying { false };
    std::atomic<double> previewPos { -1.0 };
    void renderPreview (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet, int num);

    // UI notes (single producer / single consumer)
    juce::AbstractFifo uiNoteFifo { 64 };
    std::array<std::pair<int, float>, 64> uiNotes;

    // input recorder
    juce::TimeSliceThread writerThread { "Snagger recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    juce::File recordingFile;
    std::atomic<juce::int64> recordedSamples { 0 };

    double currentSampleRate = 44100.0;
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnaggerProcessor)
};
