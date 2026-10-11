#include "Session.h"
#include "AudioFileIO.h"
#include "Settings.h"
#include <cmath>

namespace snag
{

Session::Session()
{
    sessionId = juce::Uuid().toString();
    dir = paths::sessionsDir().getChildFile (sessionId);
}

Session::~Session()
{
    flushWrites();
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
        auto f = dir.getChildFile (clip.id + "." + tag + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()) + ".wav");
        if (! f.exists())
            return f;
    }
}

bool Session::mayDelete (const juce::File& f) const
{
    if (f == juce::File() || ! f.isAChildOf (dir))
        return false;   // another instance's (or an earlier run's) file: not ours to remove
    const juce::ScopedLock sl (savedLock);
    return ! savedFiles.contains (f.getFullPathName());
}

void Session::retire (const juce::File& f)
{
    if (mayDelete (f))
        writer.addJob ([f] { f.deleteFile(); });
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
        retire (c->cacheFile);
        retire (c->baseCacheFile);
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
    sendChangeMessage();
}

/** Writes to a temporary file first, so an interrupted write never leaves half a clip behind. */
static void writeCacheFile (const AudioData& audio, const juce::File& file)
{
    auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    if (audioio::writeWav (audio, tmp, 32) && tmp.replaceFileIn (file))
        return;
    tmp.deleteFile();
}

void Session::persist (Clip::Ptr clip)
{
    dir.createDirectory();

    const auto oldFile = clip->cacheFile;
    clip->cacheFile = freshCacheFile (*clip, {});
    auto audio = clip->audio;
    auto file = clip->cacheFile;
    writer.addJob ([audio, file]
    {
        if (audio != nullptr)
            writeCacheFile (*audio, file);
    });
    if (oldFile != clip->cacheFile)
        retire (oldFile);

    // the original that the PITCH & TIME / TONE knobs work from, so they still do after a reload
    if (clip->adjustBase != clip->cachedBase || (clip->adjustBase != nullptr && clip->baseCacheFile == juce::File()))
    {
        const auto oldBase = clip->baseCacheFile;
        clip->cachedBase = clip->adjustBase;
        clip->baseCacheFile = clip->adjustBase != nullptr ? freshCacheFile (*clip, "base.") : juce::File();
        if (auto base = clip->adjustBase)
        {
            auto baseFile = clip->baseCacheFile;
            writer.addJob ([base, baseFile] { writeCacheFile (*base, baseFile); });
        }
        if (oldBase != clip->baseCacheFile)
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
        ct.setProperty ("file", c->cacheFile.getFullPathName(), nullptr);
        ct.setProperty ("created", c->created.toMilliseconds(), nullptr);

        // the knobs, the audio they were last rendered with, and the original they work from
        if (c->adjustBase != nullptr && c->baseCacheFile != juce::File())
        {
            ct.setProperty ("adjust", c->adjust.toString(), nullptr);
            ct.setProperty ("rendered", c->audioAdjust.toString(), nullptr);
            ct.setProperty ("base", c->baseCacheFile.getFullPathName(), nullptr);
        }
        if (! adoptRestoredFolder)
        {
            const juce::ScopedLock lock (savedLock);
            savedFiles.addIfNotAlreadyThere (c->cacheFile.getFullPathName());
            if (c->baseCacheFile != juce::File())
                savedFiles.addIfNotAlreadyThere (c->baseCacheFile.getFullPathName());
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

    for (auto ct : t)
    {
        const auto path = ct.getProperty ("file").toString();
        juce::File f (juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File());
        if (! f.existsAsFile() && savedDir != juce::File())
        {
            // the app's data folder moved (new user name, restored backup): look by file name
            if (f != juce::File() && savedDir.getChildFile (f.getFileName()).existsAsFile())
                f = savedDir.getChildFile (f.getFileName());
            else
                f = savedDir.getChildFile (ct.getProperty ("id").toString() + ".wav");
        }

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
        c->created  = juce::Time ((juce::int64) ct.getProperty ("created", juce::Time::currentTimeMillis()));
        c->audio    = loaded.audio;
        c->cacheFile = f;

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
            const auto basePath = ct.getProperty ("base").toString();
            juce::File bf (juce::File::isAbsolutePath (basePath) ? juce::File (basePath) : juce::File());
            if (! bf.existsAsFile() && bf != juce::File() && savedDir != juce::File())
                bf = savedDir.getChildFile (bf.getFileName());
            auto base = bf.existsAsFile() ? audioio::loadFile (bf, {}) : audioio::LoadResult();

            if (base.audio != nullptr)
            {
                c->adjust = adjust;
                c->audioAdjust = rendered;
                c->adjustBase = base.audio;
                c->cachedBase = base.audio;
                c->baseCacheFile = bf;
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

} // namespace snag
