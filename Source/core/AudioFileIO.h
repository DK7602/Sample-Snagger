#pragma once

#include "AudioData.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace snag::audioio
{
    struct LoadResult
    {
        AudioData::Ptr audio;
        juce::String error;
        bool truncated = false;
    };

    /** Longest thing we'll load into memory. */
    constexpr double maxSeconds = 20.0 * 60.0;

    /** Wildcard for file choosers: every audio + video type we understand. */
    juce::String importWildcard();

    /** True for extensions that are handled natively by JUCE (no ffmpeg needed). */
    bool isNativeAudioFile (const juce::File&);

    /** True if this looks like something we can import (audio or video). */
    bool isImportable (const juce::File&);

    /** Loads audio from any audio or video file. Native formats are decoded directly, everything
        else (mp4, mov, mkv, webm, opus, m4a on Linux, ...) is converted via ffmpeg if available. */
    LoadResult loadFile (const juce::File& file, const juce::File& ffmpeg,
                         std::function<void (float)> progress = {},
                         std::function<bool()> shouldCancel = {});

    /** Writes (part of) an AudioData as WAV. bitDepth: 16, 24 or 32 (float). */
    bool writeWav (const AudioData& audio, const juce::File& dest, int bitDepth,
                   int startSample = 0, int numSamples = -1);

    /** Writes a WAV with a unique name into the drag-export folder and returns it. */
    juce::File writeDragFile (const AudioData& audio, const juce::String& name, int bitDepth,
                              int startSample = 0, int numSamples = -1);

    /** Returns a non-existing file "name.wav", "name (2).wav", ... in dir. */
    juce::File uniqueFile (const juce::File& dir, const juce::String& name, const juce::String& ext = ".wav");
}
