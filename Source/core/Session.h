#pragma once

#include "AudioData.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace snag
{

//==============================================================================
/** All clips captured in one plug-in instance (the "tray"), plus the selection.
    Clips are cached as WAV files so they survive closing and re-opening a DAW project. */
class Session : public juce::ChangeBroadcaster
{
public:
    Session();
    ~Session() override;

    const juce::ReferenceCountedArray<Clip>& getClips() const noexcept { return clips; }

    void add (Clip::Ptr clip, bool select = true);
    void remove (Clip* clip);            // also removes its stems
    void clear();

    void select (Clip* clip);
    Clip::Ptr getSelected() const        { return selected; }

    Clip::Ptr findById (const juce::String& id) const;
    juce::Array<Clip::Ptr> getStemsOf (const Clip& parent) const;
    Clip::Ptr getParentOf (const Clip& stem) const;

    /** Call after changing a clip's audio / slices / name. */
    void clipChanged (Clip* clip, bool audioChanged = true);

    /** Persistence */
    juce::ValueTree toValueTree() const;
    void restore (const juce::ValueTree&);
    juce::File getSessionDir() const     { return dir; }

    /** Blocks until pending cache writes are done (used on shutdown / tests). */
    void flushWrites();

    std::function<void (const juce::String&)> onMessage;   // toast messages for the UI

private:
    void persist (Clip::Ptr clip);

    juce::ReferenceCountedArray<Clip> clips;
    Clip::Ptr selected;
    juce::String sessionId;
    juce::File dir;
    juce::ThreadPool writer { juce::ThreadPoolOptions{}.withThreadName ("Snagger cache").withNumberOfThreads (1) };
};

} // namespace snag
