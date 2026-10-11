#pragma once

#include "AudioData.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <utility>

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

    /** How the cache folder is looked after.

        A plug-in instance (the default) treats every file a saved state may point at as read-only:
        each change to a clip's audio goes into a new file in this instance's own folder, and files
        it didn't make, or that were part of a saved state, are never deleted. A DAW project can be
        duplicated, saved under another name or closed without saving, and every copy must still
        find exactly the audio it was saved with.

        The standalone app brings back one session, its own, and saves it when it quits. So it keeps
        working in that session's folder: one file per clip, replaced when the clip changes and
        deleted when the clip is removed. */
    void setAdoptsRestoredFolder (bool shouldAdopt) noexcept   { adoptRestoredFolder = shouldAdopt; }

    /** How many clips of the last restore() had lost their cached audio. Reading it resets it. */
    int takeMissingClipCount() noexcept  { return std::exchange (missingClips, 0); }

private:
    void persist (Clip::Ptr clip);
    juce::File freshCacheFile (const Clip&, const juce::String& tag) const;
    bool mayDelete (const juce::File&) const;
    void retire (const juce::File&);

    juce::ReferenceCountedArray<Clip> clips;
    Clip::Ptr selected;
    juce::String sessionId;
    juce::File dir;
    bool adoptRestoredFolder = false;
    int missingClips = 0;

    // Plug-in instances: files that a saved state (DAW project) may point at. They are never
    // deleted or overwritten - the project may be closed without saving again.
    mutable juce::CriticalSection savedLock;
    mutable juce::StringArray savedFiles;

    juce::ThreadPool writer { juce::ThreadPoolOptions{}.withThreadName ("Snagger cache").withNumberOfThreads (1) };
};

} // namespace snag
