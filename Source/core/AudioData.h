#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <vector>
#include "SoundSettings.h"

namespace snag
{

//==============================================================================
/** An immutable block of audio. Edits always create a new AudioData, so the audio
    thread, background jobs and the undo stack can all share these safely. */
struct AudioData final : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<AudioData>;

    AudioData (juce::AudioBuffer<float>&& b, double sr) : buffer (std::move (b)), sampleRate (sr) {}

    static Ptr make (juce::AudioBuffer<float>&& b, double sr)   { return Ptr (new AudioData (std::move (b), sr)); }

    int    getNumSamples() const noexcept    { return buffer.getNumSamples(); }
    int    getNumChannels() const noexcept   { return buffer.getNumChannels(); }
    double lengthSeconds() const noexcept    { return sampleRate > 0 ? (double) getNumSamples() / sampleRate : 0.0; }

    const juce::AudioBuffer<float> buffer;
    const double sampleRate;
};

//==============================================================================
/** A captured / imported / edited sample living in the session tray. */
struct Clip final : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<Clip>;

    juce::String id = juce::Uuid().toString();
    juce::String name;          // display name
    juce::String origin;        // URL or file path it came from
    juce::String kind;          // "Live", "Download", "Import", "Recording", "Stem", "Edit"
    juce::String parentId;      // for stems: the clip they were separated from
    juce::String stemName;      // "vocals", "drums", ...
    juce::Time   created = juce::Time::getCurrentTime();

    AudioData::Ptr audio;
    std::vector<int> slices;    // chop points (sample positions, sorted, excluding 0)
    double bpm = 0.0;           // tempo of the original (before the LENGTH knob)

    /** The live PITCH & TIME / TONE knobs. While any of them is away from its default,
        `adjustBase` holds the audio as it was before, and `audio` is that audio rendered with them. */
    struct Adjust
    {
        float semitones = 0.0f;
        double length = 1.0;            // 0.5 .. 2.0 of the original length
        bool formants = true, tape = false;
        float gainDb = 0.0f;
        float lowCut = 0.0f;            // Hz, 0 = off
        float highCut = 0.0f;           // Hz, 0 = off

        bool pitchNeutral() const noexcept { return std::abs (semitones) < 0.01f && std::abs (length - 1.0) < 0.0005; }
        bool toneNeutral() const noexcept  { return std::abs (gainDb) < 0.01f && lowCut <= 0.0f && highCut <= 0.0f; }
        bool isNeutral() const noexcept    { return pitchNeutral() && toneNeutral(); }
        bool operator== (const Adjust& o) const noexcept
        {
            return std::abs (semitones - o.semitones) < 0.001f && std::abs (length - o.length) < 1.0e-6
                && formants == o.formants && tape == o.tape && std::abs (gainDb - o.gainDb) < 0.001f
                && std::abs (lowCut - o.lowCut) < 0.01f && std::abs (highCut - o.highCut) < 0.01f;
        }
        bool operator!= (const Adjust& o) const noexcept { return ! (*this == o); }

        juce::String toString() const
        {
            return juce::String (semitones, 3) + " " + juce::String (length, 6) + " " + (formants ? "1" : "0") + " " + (tape ? "1" : "0")
                 + " " + juce::String (gainDb, 3) + " " + juce::String (lowCut, 2) + " " + juce::String (highCut, 2);
        }
        static Adjust fromString (const juce::String& s)
        {
            Adjust a;
            const auto t = juce::StringArray::fromTokens (s, " ", {});
            if (t.size() < 7)
                return a;
            a.semitones = juce::jlimit (-24.0f, 24.0f, t[0].getFloatValue());
            a.length    = juce::jlimit (0.5, 2.0, t[1].getDoubleValue());
            a.formants  = t[2] == "1";
            a.tape      = t[3] == "1";
            a.gainDb    = juce::jlimit (-24.0f, 24.0f, t[4].getFloatValue());
            a.lowCut    = juce::jlimit (0.0f, 20000.0f, t[5].getFloatValue());
            a.highCut   = juce::jlimit (0.0f, 20000.0f, t[6].getFloatValue());
            return a;
        }
    };
    Adjust adjust;
    AudioData::Ptr adjustBase;

    /** The knob settings `audio` was actually rendered with. It lags behind `adjust` while a render
        is still running (or was cancelled), which is how STUDIO knows it has to render again. */
    Adjust audioAdjust;
    bool renderIsCurrent() const noexcept   { return adjustBase == nullptr || audioAdjust == adjust; }

    /** The tempo you hear: `bpm` is stored for the original, the LENGTH knob (or TAPE pitch) moves it. */
    double bpmWith (const Adjust& a) const noexcept
    {
        if (bpm <= 0.0 || adjustBase == nullptr)
            return bpm;
        return a.tape ? bpm * std::pow (2.0, a.semitones / 12.0) : bpm / juce::jmax (0.01, a.length);
    }
    double heardBpm() const noexcept        { return bpmWith (audioAdjust); }

    /** Per-chop pad settings (index = chop; missing = default), the FX rack, and the key. */
    std::vector<PadParams> pads;
    FxSettings fx;
    int keyTonic = -1;          // 0..11 (C..B), -1 = not detected yet
    bool keyMinor = false, keyManual = false;

    PadParams padAt (int i) const { return i >= 0 && i < (int) pads.size() ? pads[(size_t) i] : PadParams(); }
    void setPad (int i, const PadParams& p)
    {
        if (i < 0) return;
        if ((int) pads.size() <= i) pads.resize ((size_t) i + 1);
        pads[(size_t) i] = p;
    }

    juce::File cacheFile;       // where the session keeps this clip on disk
    juce::File baseCacheFile;   // ... and `adjustBase` (the original the knobs work from), while there is one
    AudioData::Ptr cachedBase;  // the audio that baseCacheFile holds

    /** Re-homes the per-pad settings after the chop markers changed, so every chop keeps its own
        sound. `oldPositionOf` maps the start of a new chop to where that audio was before the
        change, or returns -1 for a chop that should start clean. */
    template <typename PositionMap>
    void remapPads (const std::vector<int>& oldSlices, PositionMap&& oldPositionOf)
    {
        if (pads.empty())
            return;
        const auto oldPads = pads;
        const auto b = sliceBoundaries();
        std::vector<PadParams> fresh;
        for (size_t i = 0; i + 1 < b.size(); ++i)
        {
            const int oldPos = oldPositionOf (b[i]);
            PadParams p;
            if (oldPos >= 0)
            {
                size_t oldIndex = 0;
                for (auto s : oldSlices)
                    if (s > 0 && s <= oldPos) ++oldIndex;
                if (oldIndex < oldPads.size())
                    p = oldPads[oldIndex];
            }
            fresh.push_back (p);
        }
        while (! fresh.empty() && fresh.back().isDefault())
            fresh.pop_back();
        pads = std::move (fresh);
    }

    bool isStem() const noexcept { return parentId.isNotEmpty(); }

    //==============================================================================
    struct UndoState
    {
        AudioData::Ptr audio;
        std::vector<int> slices;
        juce::String label;
        Adjust adjust;
        AudioData::Ptr adjustBase;
        std::vector<PadParams> pads;
        FxSettings fx;
        Adjust audioAdjust;
    };

    void pushUndo (const juce::String& label)
    {
        undoStack.push_back ({ audio, slices, label, adjust, adjustBase, pads, fx, audioAdjust });
        if (undoStack.size() > 30)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
    }

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    juce::String undo()
    {
        if (undoStack.empty()) return {};
        auto s = undoStack.back();
        undoStack.pop_back();
        redoStack.push_back ({ audio, slices, s.label, adjust, adjustBase, pads, fx, audioAdjust });
        audio = s.audio;
        slices = s.slices;
        adjust = s.adjust;
        adjustBase = s.adjustBase;
        pads = s.pads;
        fx = s.fx;
        audioAdjust = s.audioAdjust;
        return s.label;
    }

    juce::String redo()
    {
        if (redoStack.empty()) return {};
        auto s = redoStack.back();
        redoStack.pop_back();
        undoStack.push_back ({ audio, slices, s.label, adjust, adjustBase, pads, fx, audioAdjust });
        audio = s.audio;
        slices = s.slices;
        adjust = s.adjust;
        adjustBase = s.adjustBase;
        pads = s.pads;
        fx = s.fx;
        audioAdjust = s.audioAdjust;
        return s.label;
    }

    /** Slice boundaries including 0 and the end. */
    std::vector<int> sliceBoundaries() const
    {
        std::vector<int> b;
        const int n = audio != nullptr ? audio->getNumSamples() : 0;
        b.push_back (0);
        for (auto s : slices)
            if (s > 0 && s < n && s > b.back())
                b.push_back (s);
        b.push_back (n);
        return b;
    }

    std::vector<UndoState> undoStack, redoStack;
};

//==============================================================================
/** Stem colour + nice label helpers. */
inline juce::String prettyStemName (const juce::String& s)
{
    if (s == "vocals")  return "Vocals";
    if (s == "instrumental" || s == "music" || s == "no_vocals") return "Music";
    if (s == "drums")   return "Drums";
    if (s == "bass")    return "Bass";
    if (s == "other")   return "Other";
    if (s == "guitar")  return "Guitar";
    if (s == "piano")   return "Piano";
    if (s == "melodic") return "Melodic";
    return s.substring (0, 1).toUpperCase() + s.substring (1);
}

} // namespace snag
