#include "Session.h"
#include "AudioFileIO.h"
#include "Settings.h"

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
    for (int i = 0; i < 400 && writer.getNumJobs() > 0; ++i)
        juce::Thread::sleep (25);
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
        auto f = c->cacheFile;
        clips.removeObject (c.get());
        if (f != juce::File())
            writer.addJob ([f] { f.deleteFile(); });
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

void Session::persist (Clip::Ptr clip)
{
    dir.createDirectory();
    clip->cacheFile = dir.getChildFile (clip->id + ".wav");
    auto audio = clip->audio;
    auto file = clip->cacheFile;
    writer.addJob ([audio, file]
    {
        if (audio != nullptr)
            audioio::writeWav (*audio, file, 32);
    });
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

    sessionId = t.getProperty ("id", juce::Uuid().toString()).toString();
    dir = paths::sessionsDir().getChildFile (sessionId);

    for (auto ct : t)
    {
        juce::File f (ct.getProperty ("file").toString());
        if (! f.existsAsFile())
            f = dir.getChildFile (ct.getProperty ("id").toString() + ".wav");
        if (! f.existsAsFile())
            continue;

        auto loaded = audioio::loadFile (f, {});
        if (loaded.audio == nullptr)
            continue;

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

        clips.add (c);
    }

    selected = findById (t.getProperty ("selected").toString());
    if (selected == nullptr && ! clips.isEmpty())
        selected = clips.getLast();

    sendChangeMessage();
}

} // namespace snag
