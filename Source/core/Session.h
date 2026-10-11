#pragma once

#include "AudioData.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <map>
#include <utility>
#include <vector>

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

    /** How many clips couldn't be written to the cache since the last call (a full disk, mostly). */
    int takeFailedWriteCount() noexcept  { return failedWrites.exchange (0); }

    /** The session folders this instance works in or was restored from (for the clean-up). */
    juce::StringArray getFoldersInUse() const;

private:
    void persist (Clip::Ptr clip);
    void noteFolderUsed (const juce::File& folder) const;
    void removeWhatNoProjectKnows();
    juce::File freshCacheFile (const Clip&, const juce::String& tag) const;
    bool mayDelete (const juce::File&, const juce::File& ownFolder) const;
    void retire (const juce::File&);
    void queueWrite (AudioData::Ptr, const juce::File& file, const juce::File& replaces);

    juce::ReferenceCountedArray<Clip> clips;
    Clip::Ptr selected;
    juce::String sessionId;
    juce::File dir;
    bool adoptRestoredFolder = false;
    int missingClips = 0;
    std::atomic<int> failedWrites { 0 };

    // Plug-in instances: files that a saved state (DAW project) may point at. They are never
    // deleted or overwritten - the project may be closed without saving again.
    mutable juce::CriticalSection savedLock;
    mutable juce::StringArray savedFiles;

    // Session folders this instance uses, and when each was last marked as used (for the clean-up).
    mutable juce::StringArray foldersUsed;
    mutable std::map<juce::String, juce::int64> folderMarkedAt;

    juce::ThreadPool writer { juce::ThreadPoolOptions{}.withThreadName ("Snagger cache").withNumberOfThreads (1) };
};

//==============================================================================
/** Looking after the Sessions folder, where every tray's samples are kept so that projects reopen
    with them. Nothing in here can know which DAW projects exist, so "unused" goes by time: a
    folder is marked whenever a project that uses it is opened or saved. */
namespace sessioncache
{
    struct Folder
    {
        juce::File dir;
        juce::int64 bytes = 0;
        juce::Time lastUsed;
        bool inUse = false;      // by a Sample Snagger that is open right now, in this program
    };

    /** Every session folder, with its size and when it was last used. Slow on a big folder: call
        it from a background thread. (`root`: the Sessions folder, unless a test says otherwise.) */
    std::vector<Folder> scan (const std::function<bool()>& shouldStop = {}, const juce::File& root = {});

    /** May this folder go, when everything not used since `cutoff` is to be removed? */
    bool removable (const Folder&, juce::Time cutoff);

    /** Removes the folders that aren't in use and weren't used since `cutoff`. Returns the bytes freed. */
    juce::int64 removeUnusedSince (juce::Time cutoff, int* foldersRemoved = nullptr,
                                   const std::function<bool()>& shouldStop = {}, const juce::File& root = {});

    /** Folders from before "last used" was kept count as used from this moment on (the first time
        this version ran): how old they really are says nothing about the projects that need them. */
    juce::Time trackedSince();

    /** Marks a session folder as used now. */
    void markUsed (const juce::File& sessionFolder);

    /** "1.3 GB", "240 MB", "12 KB" */
    juce::String sizeText (juce::int64 bytes);
}

} // namespace snag
