#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

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
    double bpm = 0.0;

    juce::File cacheFile;       // where the session keeps this clip on disk

    bool isStem() const noexcept { return parentId.isNotEmpty(); }

    //==============================================================================
    struct UndoState
    {
        AudioData::Ptr audio;
        std::vector<int> slices;
        juce::String label;
    };

    void pushUndo (const juce::String& label)
    {
        undoStack.push_back ({ audio, slices, label });
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
        redoStack.push_back ({ audio, slices, s.label });
        audio = s.audio;
        slices = s.slices;
        return s.label;
    }

    juce::String redo()
    {
        if (redoStack.empty()) return {};
        auto s = redoStack.back();
        redoStack.pop_back();
        undoStack.push_back ({ audio, slices, s.label });
        audio = s.audio;
        slices = s.slices;
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
    if (s == "piano")   return "Keys";
    if (s == "melodic") return "Melodic";
    return s.substring (0, 1).toUpperCase() + s.substring (1);
}

} // namespace snag
