#pragma once

#include "PluginProcessor.h"

/** High-level operations that run as background jobs and add results to the session.
    They only touch the processor, so they keep working if the editor window is closed. */
namespace snag::actions
{
    void notify (SnaggerProcessor&, const juce::String& message, bool isError = false);

    /** Import audio / video files (anything FFmpeg can read). */
    void importFiles (SnaggerProcessor&, const juce::StringArray& paths);

    /** Grab high-quality audio for a web page with yt-dlp. inSec/outSec < 0 = whole thing. */
    /** Non-empty (a friendly message) when the link is a site's home / search page, not a video or song. */
    juce::String whyNotAMediaPage (const juce::String& url);

    void downloadUrl (SnaggerProcessor&, const juce::String& url, double inSec = -1.0, double outSec = -1.0,
                      const juce::String& titleHint = {});

    /** quick = built-in DSP; ai / aiMax / ai6 = built-in AI (no Python); python = optional Python engine. */
    enum class Engine { quick, ai, aiMax, ai6, python };
    juce::String engineName (Engine);

    /** Split a clip into stems; adds the stems to the session right after the clip. */
    void separate (SnaggerProcessor&, Clip::Ptr clip, Engine engine, bool fourStems);

    /** Install / update a helper tool. */
    void installTool (SnaggerProcessor&, ToolManager::Tool tool, std::function<void (bool)> onDone = {});

    /** Pitch / stretch in the background (can take a few seconds on long clips). */
    void pitchTime (SnaggerProcessor&, Clip::Ptr clip, float semitones, double lengthRatio,
                    bool keepFormants, bool tape, std::function<void (bool ok)> onDone = {});

    /** Adds a finished capture (live web grab, input recording) to the session. */
    Clip::Ptr addCapture (SnaggerProcessor&, AudioData::Ptr audio, const juce::String& name,
                          const juce::String& kind, const juce::String& origin);

    /** Writes (a range of) a clip into the sample library. Returns the file or {} on failure. */
    juce::File saveToLibrary (SnaggerProcessor&, const Clip& clip, int start = 0, int end = -1,
                              const juce::String& suffix = {});

    /** Makes a temporary WAV for dragging into a DAW. */
    juce::File makeDragFile (SnaggerProcessor&, const Clip& clip, int start = 0, int end = -1,
                             const juce::String& suffix = {});

    /** A short readable name from a URL / page title. */
    juce::String niceTitle (const juce::String& titleOrUrl);
}
