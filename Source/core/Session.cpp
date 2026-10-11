#include "Session.h"
#include "AudioFileIO.h"
#include "Settings.h"
#include <algorithm>
#include <cmath>

namespace snag
{

namespace
{
    struct LiveSessions
    {
        juce::CriticalSection lock;
        juce::Array<const Session*> list;
    };
    // (never destroyed: a session can still be going away while the program is being unloaded)
    LiveSessions& liveSessions()   { static auto* s = new LiveSessions(); return *s; }
}

Session::Session()
{
    sessionId = juce::Uuid().toString();
    dir = paths::sessionsDir().getChildFile (sessionId);

    const juce::ScopedLock sl (liveSessions().lock);
    liveSessions().list.add (this);
}

Session::~Session()
{
    flushWrites();
    removeWhatNoProjectKnows();

    const juce::ScopedLock sl (liveSessions().lock);
    liveSessions().list.removeFirstMatchingValue (this);
}

/** A plug-in that is closed without its project ever being saved leaves samples that nothing
    points at. Whatever in this instance's own folder was never part of a saved state goes with it. */
void Session::removeWhatNoProjectKnows()
{
    if (adoptRestoredFolder || ! dir.isDirectory())
        return;   // (the standalone app saves its one session when it quits: all of it is needed)

    bool anythingKept = false;
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false))
    {
        if (f.getFileName() == ".used")
            continue;
        if (mayDelete (f, dir))
            f.deleteFile();
        else
            anythingKept = true;
    }
    if (! anythingKept)
        dir.deleteRecursively();
}

juce::StringArray Session::getFoldersInUse() const
{
    const juce::ScopedLock sl (savedLock);
    auto folders = foldersUsed;
    folders.addIfNotAlreadyThere (dir.getFullPathName());
    return folders;
}

void Session::noteFolderUsed (const juce::File& folder) const
{
    if (folder == juce::File())
        return;
    const auto path = folder.getFullPathName();
    const auto now = juce::Time::currentTimeMillis();
    {
        const juce::ScopedLock sl (savedLock);
        foldersUsed.addIfNotAlreadyThere (path);
        auto& markedAt = folderMarkedAt[path];
        if (now - markedAt < 3600 * 1000)
            return;   // (a save can come every few seconds: once an hour is plenty for a date)
        markedAt = now;
    }
    sessioncache::markUsed (folder);
}

void Session::flushWrites()
{
    // Losing a queued write means a saved project that has lost its audio, so be patient:
    // a long stereo clip is a few hundred MB and stems come several at a time.
    for (int i = 0; i < 4800 && writer.getNumJobs() > 0; ++i)
        juce::Thread::sleep (25);
}

juce::File Session::freshCacheFile (const Clip& clip, const juce::String& tag) const
{
    if (adoptRestoredFolder)
        return dir.getChildFile (clip.id + "." + tag + "wav");   // the standalone app: one file per clip

    // A new name for every version of the audio: a file that a saved project points at is never
    // written over, so closing a project without saving can't leave it with newer audio than its chops.
    for (;;)
    {
        auto f = dir.getChildFile (clip.id + "." + tag + paths::uniqueTag() + ".wav");
        if (! f.exists())
            return f;
    }
}

bool Session::mayDelete (const juce::File& f, const juce::File& ownFolder) const
{
    if (f == juce::File() || ! f.isAChildOf (ownFolder))
        return false;   // another instance's (or an earlier run's) file: not ours to remove
    const juce::ScopedLock sl (savedLock);
    return ! savedFiles.contains (f.getFullPathName());
}

void Session::retire (const juce::File& f)
{
    if (! mayDelete (f, dir))
        return;

    // Asked again when its turn comes: a save that happened in between keeps the file.
    writer.addJob ([this, f, folder = dir]
    {
        if (mayDelete (f, folder))
            f.deleteFile();
    });
}

void Session::add (Clip::Ptr clip, bool shouldSelect)
{
    if (clip == nullptr || clip->audio == nullptr)
        return;

    // Keep stems right after their parent so the tray reads naturally.
    int insertAt = clips.size();
    if (clip->isStem())
    {
        for (int i = 0; i < clips.size(); ++i)
            if (clips[i]->id == clip->parentId || clips[i]->parentId == clip->parentId)
                insertAt = i + 1;
    }
    clips.insert (insertAt, clip);

    if (shouldSelect)
        selected = clip;

    persist (clip);
    sendChangeMessage();
}

void Session::remove (Clip* clip)
{
    if (clip == nullptr)
        return;

    Clip::Ptr keep (clip);
    juce::Array<Clip::Ptr> toRemove { keep };
    toRemove.addArray (getStemsOf (*clip));

    for (auto& c : toRemove)
    {
        clips.removeObject (c.get());
        juce::File audioFile, baseFile;
        {
            const juce::ScopedLock sl (savedLock);   // a host may be saving on its own thread
            audioFile = std::exchange (c->cacheFile, juce::File());
            baseFile = std::exchange (c->baseCacheFile, juce::File());
            c->cachedAudio = nullptr;
            c->cachedBase = nullptr;
        }
        retire (audioFile);
        retire (baseFile);
        if (selected == c)
            selected = nullptr;
    }

    if (selected == nullptr && ! clips.isEmpty())
        selected = clips.getLast();

    sendChangeMessage();
}

void Session::clear()
{
    while (! clips.isEmpty())
        remove (clips.getFirst().get());
}

void Session::select (Clip* clip)
{
    if (clip == selected.get())
        return;
    selected = clip;
    sendChangeMessage();
}

Clip::Ptr Session::findById (const juce::String& id) const
{
    for (auto* c : clips)
        if (c->id == id)
            return c;
    return nullptr;
}

juce::Array<Clip::Ptr> Session::getStemsOf (const Clip& parent) const
{
    juce::Array<Clip::Ptr> r;
    for (auto* c : clips)
        if (c->parentId == parent.id)
            r.add (c);
    return r;
}

Clip::Ptr Session::getParentOf (const Clip& stem) const
{
    return stem.isStem() ? findById (stem.parentId) : nullptr;
}

void Session::clipChanged (Clip* clip, bool audioChanged)
{
    if (clip != nullptr && audioChanged)
        persist (clip);
    trimUndoHistory (clip);
    sendChangeMessage();
}

void Session::trimUndoHistory (Clip* edited)
{
    int dropped = 0;
    if (edited != nullptr)
        dropped += edited->trimUndo (undoBudgetPerClip);

    juce::int64 total = 0;
    for (auto* c : clips)
        total += c->undoBytes();

    if (total > undoBudgetTotal)
    {
        // the samples that were edited longest ago give up their history first
        std::vector<Clip*> others;
        for (auto* c : clips)
            if (c != edited && ! c->undoStack.empty())
                others.push_back (c);
        std::sort (others.begin(), others.end(), [] (const Clip* a, const Clip* b) { return a->undoTouched < b->undoTouched; });

        for (auto* c : others)
        {
            if (total <= undoBudgetTotal)
                break;
            const auto before = c->undoBytes();
            dropped += c->trimUndo (juce::jmax ((juce::int64) 0, before - (total - undoBudgetTotal)), 0);
            total -= before - c->undoBytes();
        }
    }
    undoStepsDropped += dropped;
}

/** Writes to a temporary file first, so an interrupted write never leaves half a clip behind. */
static bool writeCacheFile (const AudioData& audio, const juce::File& file)
{
    auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    if (audioio::writeWav (audio, tmp, 32))
        for (int attempt = 0; attempt < 6; ++attempt)   // a virus scanner may hold the new file for a moment
        {
            if (tmp.replaceFileIn (file))
                return true;
            juce::Thread::sleep (150);
        }

    tmp.deleteFile();
    return false;
}

void Session::queueWrite (AudioData::Ptr audio, const juce::File& file, const juce::File& replaces)
{
    writer.addJob ([this, audio, file, replaces, folder = dir]
    {
        if (audio == nullptr)
            return;

        if (! writeCacheFile (*audio, file))
        {
            ++failedWrites;   // the editor tells the user; the version before this one stays on disk
            return;
        }
        if (replaces != file && mayDelete (replaces, folder))
            replaces.deleteFile();
    });
}

void Session::persist (Clip::Ptr clip)
{
    dir.createDirectory();
    noteFolderUsed (dir);

    // Only audio that isn't on disk yet: undoing a marker or a pad setting comes through here too.
    if (clip->audio != clip->cachedAudio || clip->cacheFile == juce::File())
    {
        juce::File oldFile, file;
        {
            const juce::ScopedLock sl (savedLock);   // a host may be saving on its own thread
            oldFile = clip->cacheFile;
            file = clip->cacheFile = freshCacheFile (*clip, {});
            clip->cachedAudio = clip->audio;
        }
        queueWrite (clip->audio, file, oldFile);
    }

    // the original that the PITCH & TIME / TONE knobs work from, so they still do after a reload
    if (clip->adjustBase != clip->cachedBase || (clip->adjustBase != nullptr && clip->baseCacheFile == juce::File()))
    {
        juce::File oldBase, baseFile;
        {
            const juce::ScopedLock sl (savedLock);
            oldBase = clip->baseCacheFile;
            clip->cachedBase = clip->adjustBase;
            baseFile = clip->baseCacheFile = clip->adjustBase != nullptr ? freshCacheFile (*clip, "base.") : juce::File();
        }
        if (clip->adjustBase != nullptr)
            queueWrite (clip->adjustBase, baseFile, oldBase);
        else
            retire (oldBase);
    }
}

//==============================================================================
juce::ValueTree Session::toValueTree() const
{
    juce::ValueTree t ("SESSION");
    t.setProperty ("id", sessionId, nullptr);
    t.setProperty ("selected", selected != nullptr ? selected->id : juce::String(), nullptr);

    for (auto* c : clips)
    {
        juce::ValueTree ct ("CLIP");
        ct.setProperty ("id", c->id, nullptr);
        ct.setProperty ("name", c->name, nullptr);
        ct.setProperty ("origin", c->origin, nullptr);
        ct.setProperty ("kind", c->kind, nullptr);
        ct.setProperty ("parent", c->parentId, nullptr);
        ct.setProperty ("stem", c->stemName, nullptr);
        ct.setProperty ("bpm", c->bpm, nullptr);
        ct.setProperty ("created", c->created.toMilliseconds(), nullptr);

        // The files this state points at are kept from here on. Naming them and marking them is
        // one step, so a change to the clip on the message thread can't delete one in between.
        juce::File audioFile, baseFile;
        {
            const juce::ScopedLock lock (savedLock);
            audioFile = c->cacheFile;
            baseFile = c->adjustBase != nullptr ? c->baseCacheFile : juce::File();
            if (! adoptRestoredFolder)
            {
                savedFiles.addIfNotAlreadyThere (audioFile.getFullPathName());
                if (baseFile != juce::File())
                    savedFiles.addIfNotAlreadyThere (baseFile.getFullPathName());
            }
        }
        ct.setProperty ("file", audioFile.getFullPathName(), nullptr);
        noteFolderUsed (audioFile.getParentDirectory());   // a project is being saved with these: they are in use
        if (baseFile != juce::File())
            noteFolderUsed (baseFile.getParentDirectory());

        // the knobs, the audio they were last rendered with, and the original they work from
        if (baseFile != juce::File())
        {
            ct.setProperty ("adjust", c->adjust.toString(), nullptr);
            ct.setProperty ("rendered", c->audioAdjust.toString(), nullptr);
            ct.setProperty ("base", baseFile.getFullPathName(), nullptr);
        }

        juce::StringArray sl;
        for (auto s : c->slices) sl.add (juce::String (s));
        ct.setProperty ("slices", sl.joinIntoString (","), nullptr);

        juce::StringArray pads;   // only the changed ones, as "chop:settings"
        for (size_t i = 0; i < c->pads.size(); ++i)
            if (! c->pads[i].isDefault())
                pads.add (juce::String ((int) i) + ":" + c->pads[i].toString());
        if (! pads.isEmpty()) ct.setProperty ("pads", pads.joinIntoString (";"), nullptr);
        if (c->fx != FxSettings()) ct.setProperty ("fx", c->fx.toString(), nullptr);
        if (c->keyTonic >= 0)
            ct.setProperty ("key", juce::String (c->keyTonic) + (c->keyMinor ? "m" : "") + (c->keyManual ? "!" : ""), nullptr);
        t.appendChild (ct, nullptr);
    }
    return t;
}

/** A cache file named in a saved state, wherever the app's data folder is today. */
static juce::File findSavedFile (const juce::String& path, const juce::File& savedDir, const juce::String& oldStyleName)
{
    if (juce::File::isAbsolutePath (path))
        if (const juce::File f (path); f.existsAsFile())
            return f;

    // The data folder moved (a new user name, a restored backup, another computer or system):
    // what still holds is the end of the path, <session folder>/<file>, under today's Sessions folder.
    auto parts = juce::StringArray::fromTokens (path, "/\\", {});
    parts.removeEmptyStrings();
    const auto name = parts.isEmpty() ? juce::String() : juce::File::createLegalFileName (parts[parts.size() - 1]);
    if (name.isNotEmpty() && ! name.startsWithChar ('.'))
    {
        if (parts.size() >= 2)
        {
            const auto folder = juce::File::createLegalFileName (parts[parts.size() - 2]);
            if (folder.isNotEmpty() && ! folder.startsWithChar ('.'))
                if (auto f = paths::sessionsDir().getChildFile (folder).getChildFile (name); f.existsAsFile())
                    return f;
        }
        if (savedDir != juce::File())
            if (auto f = savedDir.getChildFile (name); f.existsAsFile())
                return f;
    }

    // a state saved before files carried a version in their name
    if (savedDir != juce::File() && oldStyleName.isNotEmpty())
        if (auto f = savedDir.getChildFile (juce::File::createLegalFileName (oldStyleName)); f.existsAsFile())
            return f;
    return {};
}

void Session::restore (const juce::ValueTree& t)
{
    if (! t.hasType ("SESSION"))
        return;

    clips.clear();
    selected = nullptr;
    missingClips = 0;

    // Where the saved clips used to live. Only the standalone app keeps working in that folder:
    // several plug-in instances can restore one and the same state (a duplicated track, a copy of
    // the project), so each writes its own changes to its own folder and never touches these files.
    const auto savedId = t.getProperty ("id").toString();
    const auto savedDir = savedId.isNotEmpty() ? paths::sessionsDir().getChildFile (savedId) : juce::File();
    {
        const juce::ScopedLock sl (savedLock);   // (the clean-up may be asking which folders are in use)
        if (adoptRestoredFolder && savedId.isNotEmpty())
        {
            sessionId = savedId;
            dir = savedDir;
        }
        else
        {
            sessionId = juce::Uuid().toString();
            dir = paths::sessionsDir().getChildFile (sessionId);
        }
    }

    for (auto ct : t)
    {
        const auto f = findSavedFile (ct.getProperty ("file").toString(), savedDir, ct.getProperty ("id").toString() + ".wav");
        auto loaded = f.existsAsFile() ? audioio::loadFile (f, {}) : audioio::LoadResult();
        if (loaded.audio == nullptr)
        {
            ++missingClips;
            continue;
        }

        Clip::Ptr c (new Clip());
        c->id       = ct.getProperty ("id").toString();
        c->name     = ct.getProperty ("name").toString();
        c->origin   = ct.getProperty ("origin").toString();
        c->kind     = ct.getProperty ("kind").toString();
        c->parentId = ct.getProperty ("parent").toString();
        c->stemName = ct.getProperty ("stem").toString();
        c->bpm      = (double) ct.getProperty ("bpm", 0.0);
        if (! (c->bpm > 0.0 && c->bpm < 2000.0))   // also catches a damaged (not-a-number) value
            c->bpm = 0.0;
        c->created  = juce::Time ((juce::int64) ct.getProperty ("created", juce::Time::currentTimeMillis()));
        c->audio    = loaded.audio;
        c->cacheFile = f;
        c->cachedAudio = loaded.audio;
        noteFolderUsed (f.getParentDirectory());   // a project that needs this folder was opened

        for (auto& s : juce::StringArray::fromTokens (ct.getProperty ("slices").toString(), ",", {}))
            if (s.isNotEmpty())
                c->slices.push_back (s.getIntValue());

        if (ct.hasProperty ("pads"))
            for (auto& p : juce::StringArray::fromTokens (ct.getProperty ("pads").toString(), ";", {}))
                if (p.containsChar (':'))
                    c->setPad (juce::jlimit (0, 255, p.upToFirstOccurrenceOf (":", false, false).getIntValue()),
                               PadParams::fromString (p.fromFirstOccurrenceOf (":", false, false)));
        if (ct.hasProperty ("fx"))
            c->fx = FxSettings::fromString (ct.getProperty ("fx").toString());
        if (ct.hasProperty ("key"))
        {
            const auto k = ct.getProperty ("key").toString();
            c->keyTonic = juce::jlimit (-1, 11, k.getIntValue());
            c->keyMinor = k.containsChar ('m');
            c->keyManual = k.containsChar ('!');
        }

        if (ct.hasProperty ("adjust"))
        {
            const auto adjust   = Clip::Adjust::fromString (ct.getProperty ("adjust").toString());
            const auto rendered = Clip::Adjust::fromString (ct.getProperty ("rendered", ct.getProperty ("adjust")).toString());
            const auto bf = findSavedFile (ct.getProperty ("base").toString(), savedDir, {});
            auto base = bf.existsAsFile() ? audioio::loadFile (bf, {}) : audioio::LoadResult();

            if (base.audio != nullptr)
            {
                c->adjust = adjust;
                c->audioAdjust = rendered;
                c->adjustBase = base.audio;
                c->cachedBase = base.audio;
                c->baseCacheFile = bf;
                noteFolderUsed (bf.getParentDirectory());
            }
            else
            {
                // The original is gone, so what's left is the sound as it was rendered: from here on
                // that is the sample, and its tempo and key are the ones you hear.
                if (c->bpm > 0.0)
                    c->bpm = rendered.tape ? c->bpm * std::pow (2.0, rendered.semitones / 12.0)
                                           : c->bpm / juce::jmax (0.01, rendered.length);
                const int shift = juce::roundToInt (rendered.semitones);
                if (c->keyTonic >= 0 && ! rendered.tape && std::abs (rendered.semitones - (float) shift) < 0.05f)
                    c->keyTonic = ((c->keyTonic + shift) % 12 + 12) % 12;
            }
        }

        // these are what the saved state points at: keep them as they are
        if (! adoptRestoredFolder)
        {
            const juce::ScopedLock sl (savedLock);
            savedFiles.addIfNotAlreadyThere (c->cacheFile.getFullPathName());
            if (c->baseCacheFile != juce::File())
                savedFiles.addIfNotAlreadyThere (c->baseCacheFile.getFullPathName());
        }

        clips.add (c);
    }

    selected = findById (t.getProperty ("selected").toString());
    if (selected == nullptr && ! clips.isEmpty())
        selected = clips.getLast();

    sendChangeMessage();
}

//==============================================================================
namespace sessioncache
{

juce::Time trackedSince()
{
    auto marker = paths::sessionsDir().getChildFile (".tracked-since");
    if (! marker.existsAsFile())
        marker.replaceWithText (juce::String (juce::Time::currentTimeMillis()));
    const auto ms = marker.loadFileAsString().trim().getLargeIntValue();
    return ms > 0 ? juce::Time (ms) : juce::Time::getCurrentTime();
}

void markUsed (const juce::File& folder)
{
    if (! folder.isDirectory() || ! folder.isAChildOf (paths::sessionsDir()))
        return;
    trackedSince();   // (makes sure the starting date exists)
    auto marker = folder.getChildFile (".used");
    if (marker.existsAsFile())
        marker.setLastModificationTime (juce::Time::getCurrentTime());
    else
        marker.create();
}

juce::String sizeText (juce::int64 bytes)
{
    const double mb = (double) bytes / 1048576.0;
    if (mb >= 1000.0) return juce::String (mb / 1024.0, 1) + " GB";
    if (mb >= 1.0)    return juce::String (juce::roundToInt (mb)) + " MB";
    return juce::String (juce::roundToInt ((double) bytes / 1024.0)) + " KB";
}

std::vector<Folder> scan (const std::function<bool()>& shouldStop, const juce::File& rootIn)
{
    const auto root = rootIn != juce::File() ? rootIn : paths::sessionsDir();
    juce::StringArray inUse;
    {
        const juce::ScopedLock sl (liveSessions().lock);
        for (auto* session : liveSessions().list)
            inUse.addArray (session->getFoldersInUse());
    }

    const auto since = trackedSince();
    std::vector<Folder> folders;
    for (auto& dir : root.findChildFiles (juce::File::findDirectories, false))
    {
        if (shouldStop && shouldStop())
            break;
        Folder f;
        f.dir = dir;
        f.inUse = inUse.contains (dir.getFullPathName());
        bool marked = false;
        for (auto& file : dir.findChildFiles (juce::File::findFiles, true))
        {
            if (file.getFileName() == ".used")
            {
                marked = true;
                f.lastUsed = file.getLastModificationTime();
            }
            else
            {
                f.bytes += file.getSize();
            }
        }
        if (! marked)
            f.lastUsed = since;
        folders.push_back (f);
    }
    return folders;
}

bool removable (const Folder& f, juce::Time cutoff)
{
    return ! f.inUse && f.lastUsed < cutoff;
}

juce::int64 removeUnusedSince (juce::Time cutoff, int* foldersRemoved, const std::function<bool()>& shouldStop, const juce::File& root)
{
    juce::int64 freed = 0;
    int removed = 0;
    for (auto& f : scan (shouldStop, root))
    {
        if (shouldStop && shouldStop())
            break;
        if (! removable (f, cutoff))
            continue;
        if (f.dir.deleteRecursively())
        {
            freed += f.bytes;
            ++removed;
        }
    }
    if (foldersRemoved != nullptr)
        *foldersRemoved = removed;
    return freed;
}

} // namespace sessioncache

} // namespace snag
