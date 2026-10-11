#pragma once

#include "PluginProcessor.h"
#include "core/AiStems.h"
#include "core/AudioToMidi.h"

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

    /** Direct links to the audio / video files a page is playing (from the built-in browser).
        HQ SNAG tries what's playing first, then the page itself, then anything else the page loaded -
        so it also works on sites its downloader doesn't know. */
    struct MediaHints
    {
        juce::StringArray playing, others;
    };

    void downloadUrl (SnaggerProcessor&, const juce::String& url, double inSec = -1.0, double outSec = -1.0,
                      const juce::String& titleHint = {}, const MediaHints& hints = {});

    /** quick = built-in DSP; ai / aiMax / ai6 = built-in AI (no Python); python = optional Python engine. */
    enum class Engine { quick, ai, aiMax, ai6, python };
    juce::String engineName (Engine);

    /** Every part the stems menu offers, in display order: vocals, music (everything but the
        vocals), drums, bass, guitar, piano, other. */
    const juce::StringArray& allStemParts();

    /** Which AI model a choice of parts needs (guitar / piano -> the 6-part model, etc.). */
    ai::Mode aiModeFor (Engine, const juce::StringArray& parts);

    /** Split a clip into the chosen parts; adds them to the session right after the clip. */
    void separate (SnaggerProcessor&, Clip::Ptr clip, Engine engine, const juce::StringArray& parts);

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

    /** What a drag file is made from, put together on the message thread - so that the making
        itself (rendering the FX, writing the WAV) can be done on any thread, away from the window. */
    struct DragJob
    {
        std::function<AudioData::Ptr()> render;   // works only on what it holds: safe on any thread
        juce::String name;
        int bitDepth = 24;
        juce::int64 samples = 0;                  // how much there is to render and write
        juce::String key;                         // the same key gives the same file
        std::vector<DragJob> parts;               // instead of `render`: several files in one drag (all the chops)
        bool isValid() const   { return render != nullptr || ! parts.empty(); }
    };
    DragJob dragJobFor (SnaggerProcessor&, const Clip& clip, int start = 0, int end = -1, const juce::String& suffix = {});
    DragJob dragJobForPad (SnaggerProcessor&, const Clip& clip, int padIndex);
    DragJob dragJobForMix (SnaggerProcessor&, const juce::String& name, const std::vector<AudioData::Ptr>& layers,
                           const std::vector<float>& gains);
    juce::File makeDragFile (const DragJob&);
    juce::StringArray makeDragFiles (const DragJob&);   // (one, or one per part)

    /** Every chop as a file of its own, with its pad sound and the FX rack: for one drag, or for the library. */
    DragJob dragJobForAllChops (SnaggerProcessor&, const Clip& clip);
    void saveChopsToLibrary (SnaggerProcessor&, const Clip& clip);

    /** The audio you'd hear for (a range of) a clip: with its FX rack, echoes / reverb ringing out. */
    AudioData::Ptr renderForExport (SnaggerProcessor&, const Clip& clip, int start, int end);

    /** One chop with its pad settings and the FX rack, for dragging into a DAW. */
    AudioData::Ptr renderPadForExport (SnaggerProcessor&, const Clip& clip, int padIndex);
    juce::File makePadDragFile (SnaggerProcessor&, const Clip& clip, int padIndex);

    /** The tempo the FX and MIDI lock to: the DAW's, else the sample's, else 120. */
    double exportBpm (SnaggerProcessor&, const Clip& clip);

    /** Writes notes as a .mid file in the drag folder (or `dir`). */
    juce::File makeMidiFile (SnaggerProcessor&, const Clip& clip, const std::vector<midi::Note>& notes,
                             const juce::File& dir = {});

    /** A short readable name from a URL / page title. */
    juce::String niceTitle (const juce::String& titleOrUrl);
}
