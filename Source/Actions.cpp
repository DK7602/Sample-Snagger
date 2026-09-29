#include "Actions.h"
#include "core/FxRack.h"
#include "core/PadFx.h"
#include "core/AudioFileIO.h"
#include "core/EditOps.h"
#include "core/QuickSplit.h"
#include "core/AiStems.h"

namespace snag::actions
{

static juce::String fmtTime (double secs)
{
    const int m = (int) (secs / 60.0);
    const double s = secs - m * 60.0;
    return juce::String (m) + ":" + juce::String (s, 1).paddedLeft ('0', 4);
}

void notify (SnaggerProcessor& p, const juce::String& message, bool isError)
{
    if (p.onNotify)
    {
        p.onNotify (message, isError);
    }
    else
    {
        DBG (juce::String (isError ? "ERROR: " : "") + message);
        juce::ignoreUnused (message, isError);
    }
}

juce::String niceTitle (const juce::String& titleOrUrl)
{
    auto t = titleOrUrl.trim();
    for (auto suffix : { " - YouTube", " | TikTok", " on Instagram", " | SoundCloud", " - Vimeo", " - Dailymotion" })
        if (t.endsWithIgnoreCase (suffix))
            t = t.dropLastCharacters ((int) strlen (suffix));

    if (t.startsWithIgnoreCase ("http"))
    {
        juce::URL u (t);
        t = u.getDomain().replace ("www.", "");
    }
    if (t.length() > 60)
        t = t.substring (0, 57).trimEnd() + "...";
    return t.isNotEmpty() ? t : "Sample";
}

//==============================================================================
Clip::Ptr addCapture (SnaggerProcessor& p, AudioData::Ptr audio, const juce::String& name,
                      const juce::String& kind, const juce::String& origin)
{
    if (audio == nullptr || audio->getNumSamples() < 64)
        return nullptr;

    Clip::Ptr c (new Clip());
    c->name = name;
    c->kind = kind;
    c->origin = origin;
    c->audio = audio;
    p.session.add (c, true);
    p.setSamplerClip (c);
    return c;
}

//==============================================================================
void importFiles (SnaggerProcessor& p, const juce::StringArray& files)
{
    auto ffmpeg = p.getTools().getPath (ToolManager::Tool::ffmpeg);

    for (auto& path : files)
    {
        juce::File f (path);
        if (f.isDirectory())
            continue;

        if (! audioio::isNativeAudioFile (f) && ! ffmpeg.existsAsFile())
        {
            notify (p, f.getFileName() + " needs FFmpeg to import. Open Settings (gear, top right) and click Install next to FFmpeg.", true);
            continue;
        }

        p.jobs.start ("Importing " + f.getFileName(), [f, ffmpeg] (Job& job)
        {
            job.setStatus (f.getFileName());
            auto r = audioio::loadFile (f, ffmpeg, [&job] (float pr) { job.setProgress (pr); }, job.cancelCheck());
            if (r.audio == nullptr)
            {
                job.fail (f.getFileName() + ": " + r.error);
                return;
            }
            Clip::Ptr c (new Clip());
            c->name = f.getFileNameWithoutExtension();
            c->kind = audioio::isNativeAudioFile (f) ? "Import" : "Video";
            c->origin = f.getFullPathName();
            c->audio = r.audio;
            job.clips.push_back (c);
            if (r.truncated)
                job.text = "Only the first 20 minutes were imported";
        },
        [&p] (Job& job)
        {
            if (job.isCancelled()) return;
            if (job.hasFailed()) { notify (p, job.getError(), true); return; }
            for (auto& c : job.clips)
            {
                p.session.add (c, true);
                p.setSamplerClip (c);
                notify (p, "Imported " + c->name + (job.text.isNotEmpty() ? " (" + job.text + ")" : juce::String()));
            }
        });
    }
}

//==============================================================================
static juce::String friendlyDownloadError (const juce::StringArray& errors, int code, const juce::String& url)
{
    auto all = errors.joinIntoString (" ");
    const auto host = juce::URL (url).getDomain().toLowerCase();
    const bool youtube = host.endsWith ("youtube.com") || host.endsWith ("youtu.be");

    if (all.containsIgnoreCase ("HTTP Error 404"))
        return "That link wasn't found (404). Check the address, or open the video page itself.";

    // yt-dlp has no extractor for this site and its generic fallback got turned away
    if (! youtube && (all.containsIgnoreCase ("[generic]") || all.containsIgnoreCase ("Unsupported URL")))
        return "HQ Snag can't download from this site. Play the sound here and use LIVE REC or GRAB LAST instead.";
    if (all.containsIgnoreCase ("confirm your age") || all.containsIgnoreCase ("age-restricted"))
        return "This video is age-restricted. In Settings, choose your browser under 'Use cookies from' (you must be signed in there), then try again.";
    if (all.containsIgnoreCase ("not a bot") || all.containsIgnoreCase ("Sign in to confirm"))
        return "YouTube asked for a sign-in check. In Settings, choose your browser under 'Use cookies from', or use LIVE REC.";
    if (all.containsIgnoreCase ("Unsupported URL"))
        return "HQ Snag doesn't recognise this page. Open the video itself, or use LIVE REC to record what's playing.";
    if (all.containsIgnoreCase ("Private video") || all.containsIgnoreCase ("unavailable"))
        return "That video is private or unavailable.";
    if (all.containsIgnoreCase ("HTTP Error 403") || all.containsIgnoreCase ("Requested format is not available")
        || all.containsIgnoreCase ("n challenge") || all.containsIgnoreCase ("JavaScript runtime"))
        return youtube ? "YouTube blocked the download. Update yt-dlp and install Deno in Settings, then try again (or use LIVE REC)."
                       : "This site blocked the download. Play the sound here and use LIVE REC or GRAB LAST instead.";
    if (all.containsIgnoreCase ("ffmpeg") && all.containsIgnoreCase ("not found"))
        return "FFmpeg is missing. Install it in Settings.";
    if (errors.size() > 0)
        return errors[errors.size() - 1].fromFirstOccurrenceOf ("ERROR:", false, false).trim().substring (0, 220);
    return "Download failed (code " + juce::String (code) + ")";
}

/** A site's home / search page rather than a video or song: yt-dlp would either fail with a
    confusing error or grab something random from the feed. */
juce::String whyNotAMediaPage (const juce::String& url)
{
    const juce::URL u (url);
    const auto host = u.getDomain().toLowerCase();
    const auto path = u.getSubPath().toLowerCase().trimCharactersAtEnd ("/");
    const bool youtube = host.endsWith ("youtube.com");

    if (youtube && (path.isEmpty() || path == "results" || path.startsWith ("feed")))
        return "That's the YouTube home / search page, not a video. Open a video (or copy its link in your browser), then hit HQ SNAG.";
    if (path.isEmpty())
        return "That's the site's home page. Open the video or song itself (or copy its link), then hit HQ SNAG.";
    return {};
}

void downloadUrl (SnaggerProcessor& p, const juce::String& urlIn, double inSec, double outSec, const juce::String& titleHint,
                  const MediaHints& hints)
{
    auto url = urlIn.trim();
    if (! url.startsWithIgnoreCase ("http"))
    {
        notify (p, "Open a video page first (or paste a link into the address bar).", true);
        return;
    }
    if (auto why = whyNotAMediaPage (url); why.isNotEmpty() && hints.playing.isEmpty())
    {
        notify (p, why, true);
        return;
    }

    auto& tools = p.getTools();
    const auto ytdlp  = tools.getPath (ToolManager::Tool::ytdlp);
    const auto ffmpeg = tools.getPath (ToolManager::Tool::ffmpeg);
    const auto deno   = tools.getPath (ToolManager::Tool::deno);

    if (! ytdlp.existsAsFile() || ! ffmpeg.existsAsFile())
    {
        notify (p, "HQ Snag needs yt-dlp and FFmpeg. Open Settings (gear, top right) and click Install - it takes a minute. LIVE REC works without them.", true);
        if (p.onRequestSettings) p.onRequestSettings();
        return;
    }

    const auto cookies = p.getSettings().getCookiesBrowser();
    const bool section = inSec >= 0.0 && outSec > inSec;

    // What to try, in order. On sites yt-dlp knows well, the page itself comes first (best quality,
    // proper titles); elsewhere the file that's playing right now wins; then anything else it loaded.
    const auto host = juce::URL (url).getDomain().toLowerCase();
    const bool knownSite = [&host]
    {
        for (auto* d : { "youtube.com", "youtu.be", "soundcloud.com", "tiktok.com", "instagram.com", "vimeo.com",
                         "bandcamp.com", "twitch.tv", "x.com", "twitter.com", "facebook.com", "dailymotion.com",
                         "reddit.com", "mixcloud.com", "audiomack.com", "bilibili.com" })
            if (host == d || host.endsWith (juce::String (".") + d))
                return true;
        return false;
    }();

    struct Attempt { juce::String target; bool direct; };
    std::vector<Attempt> attempts;
    const bool pageOk = whyNotAMediaPage (url).isEmpty();
    if (knownSite && pageOk)
        attempts.push_back ({ url, false });
    for (auto& u : hints.playing)
        attempts.push_back ({ u, true });
    if (! knownSite && pageOk)
        attempts.push_back ({ url, false });
    for (auto& u : hints.others)
        if (! hints.playing.contains (u) && attempts.size() < 6)
            attempts.push_back ({ u, true });

    p.jobs.start (section ? "Snagging HQ section" : "Snagging HQ audio",
                  [=] (Job& job)
    {
        auto dir = paths::tempDir().getChildFile ("dl_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        dir.createDirectory();

        juce::String title = titleHint;
        juce::StringArray pageErrors;
        int code = -1;
        juce::Array<juce::File> wavs;

        for (size_t a = 0; a < attempts.size(); ++a)
        {
            const auto& attempt = attempts[a];
            juce::StringArray args { ytdlp.getFullPathName(),
                                     "--no-playlist", "--playlist-items", "1",   // a playlist / channel link: just its first item
                                     "--newline", "--progress", "--no-simulate", "--no-mtime",
                                     "--color", "never", "--encoding", "utf-8",
                                     "-f", "bestaudio/best", "-x", "--audio-format", "wav",
                                     "--ffmpeg-location", ffmpeg.getFullPathName(),
                                     "-o", dir.getChildFile ("snag.%(ext)s").getFullPathName(),
                                     "--print", "before_dl:SNAGTITLE %(title)s" };
            if (deno.existsAsFile())
                args.addArray ({ "--js-runtimes", "deno:" + deno.getFullPathName() });
            if (cookies.isNotEmpty())
                args.addArray ({ "--cookies-from-browser", cookies });
            if (attempt.direct)
                args.addArray ({ "--referer", url, "--force-generic-extractor" });   // fetch the file as the page did
            if (section)
                args.addArray ({ "--download-sections", "*" + juce::String (inSec, 3) + "-" + juce::String (outSec, 3),
                                 "--force-keyframes-at-cuts" });
            args.add (attempt.target);

            juce::StringArray errors;
            juce::String attemptTitle;
            job.setStatus (attempt.direct ? "Grabbing the file that's playing" : "Connecting");

            code = job.runner.run (args, [&] (const juce::String& line)
            {
                if (line.startsWith ("SNAGTITLE "))
                {
                    attemptTitle = line.substring (10).trim();
                    job.setStatus (niceTitle (attemptTitle));
                }
                else if (line.startsWith ("[download]") && line.contains ("%"))
                {
                    auto pct = line.fromFirstOccurrenceOf ("]", false, false).trim().upToFirstOccurrenceOf ("%", false, false).trim();
                    if (pct.containsOnly ("0123456789."))
                    {
                        job.setProgress (0.85f * pct.getFloatValue() / 100.0f);
                        job.setStatus ("Downloading " + pct + "%");
                    }
                }
                else if (line.startsWith ("[ExtractAudio]") || line.startsWith ("[ffmpeg]"))
                {
                    job.setProgress (0.9f);
                    job.setStatus ("Converting to WAV");
                }
                else if (line.startsWith ("ERROR") || line.containsIgnoreCase ("error:"))
                {
                    errors.add (line);
                }
            }, 45 * 60 * 1000, job.cancelCheck());

            if (code == -2 || job.isCancelled())
            {
                dir.deleteRecursively();
                return;
            }

            wavs = dir.findChildFiles (juce::File::findFiles, false, "*.wav");
            if (code == 0 && ! wavs.isEmpty())
            {
                // a direct file's "title" is its file name - the page title (or a tidied file name) reads better
                if (attempt.direct)
                {
                    auto fileName = juce::URL::removeEscapeChars (juce::URL (attempt.target).getFileName())
                                        .upToLastOccurrenceOf (".", false, false).replaceCharacters ("_-+", "   ").trim();
                    title = fileName.length() >= 4 ? fileName : (titleHint.isNotEmpty() ? titleHint : attemptTitle);
                }
                else if (attemptTitle.isNotEmpty())
                    title = attemptTitle;
                break;
            }

            if (! attempt.direct)
                pageErrors = errors;
            for (auto& f : dir.findChildFiles (juce::File::findFiles, false))
                f.deleteFile();   // leftovers of a failed attempt
            wavs.clear();
        }

        if (wavs.isEmpty())
        {
            dir.deleteRecursively();
            job.fail (friendlyDownloadError (pageErrors, code, url));
            return;
        }

        job.setStatus ("Loading");
        auto r = audioio::loadFile (wavs.getFirst(), ffmpeg, {}, job.cancelCheck());
        dir.deleteRecursively();
        if (r.audio == nullptr)
        {
            job.fail ("Downloaded, but couldn't read the audio: " + r.error);
            return;
        }

        Clip::Ptr c (new Clip());
        c->name = niceTitle (title.isNotEmpty() ? title : url);
        if (section)
            c->name << " [" << fmtTime (inSec) << "-" << fmtTime (outSec) << "]";
        c->kind = "Download";
        c->origin = url;
        c->audio = r.audio;
        job.clips.push_back (c);
        job.setProgress (1.0f);
    },
    [&p] (Job& job)
    {
        if (job.isCancelled()) return;
        if (job.hasFailed()) { notify (p, job.getError(), true); return; }
        for (auto& c : job.clips)
        {
            p.session.add (c, true);
            p.setSamplerClip (c);
            notify (p, "Snagged in HQ: " + c->name);
        }
    });
}

//==============================================================================
juce::String engineName (Engine e)
{
    switch (e)
    {
        case Engine::quick:  return "Quick Split (instant, rough)";
        case Engine::ai:     return "AI Studio (built-in)";
        case Engine::aiMax:  return "AI Studio Max (slower, cleanest)";
        case Engine::ai6:    return "AI 6 Stems (+ guitar, piano)";
        case Engine::python: return "AI via Python (GPU)";
    }
    return {};
}

const juce::StringArray& allStemParts()
{
    static const juce::StringArray parts { "vocals", "music", "drums", "bass", "guitar", "piano", "other" };
    return parts;
}

static bool needsSixParts (const juce::StringArray& parts)  { return parts.contains ("guitar") || parts.contains ("piano"); }
static bool vocalsAndMusicOnly (const juce::StringArray& parts)
{
    for (auto& part : parts)
        if (part != "vocals" && part != "music")
            return false;
    return true;
}

ai::Mode aiModeFor (Engine engine, const juce::StringArray& parts)
{
    if (needsSixParts (parts))       return ai::Mode::sixStems;
    if (engine == Engine::aiMax)     return ai::Mode::fourStemsMax;   // every part from its own specialist
    if (vocalsAndMusicOnly (parts))  return ai::Mode::vocalsMusic;
    return ai::Mode::fourStems;
}

/** Keeps just the parts that were asked for, in menu order. "music" is everything but the vocals:
    built from the AI's other parts (drums + bass + other...) with a spectral mask against the vocal
    stem, so vocal residue the AI missed doesn't stay in the music. */
static std::vector<std::pair<juce::String, AudioData::Ptr>> chooseParts (const std::vector<std::pair<juce::String, AudioData::Ptr>>& raw,
                                                                        const juce::StringArray& parts, const AudioData& input)
{
    auto find = [&raw] (const juce::String& name) -> AudioData::Ptr
    {
        for (auto& r : raw)
            if (r.first == name || (name == "music" && (r.first == "no_vocals" || r.first == "instrumental")))
                return r.second;
        return nullptr;
    };

    std::vector<std::pair<juce::String, AudioData::Ptr>> out;
    for (auto& part : allStemParts())
    {
        if (! parts.contains (part))
            continue;
        auto a = find (part);
        if (a == nullptr && part == "music")
        {
            if (auto v = find ("vocals"))
            {
                std::vector<AudioData::Ptr> rest;
                for (auto& r : raw)
                    if (r.first != "vocals" && r.first != "music")
                        rest.push_back (r.second);
                AudioData::Ptr others;
                if (! rest.empty())
                    others = edit::mix (rest, std::vector<float> (rest.size(), 1.0f));
                a = edit::musicWithoutVocals (input, *v, others.get());
            }
        }
        if (a != nullptr)
            out.push_back ({ part, a });
    }
    return out;
}

void separate (SnaggerProcessor& p, Clip::Ptr clip, Engine engine, const juce::StringArray& partsIn)
{
    if (clip == nullptr || clip->audio == nullptr)
    {
        notify (p, "Pick a sample in the tray first.", true);
        return;
    }
    auto parts = partsIn;
    if (parts.isEmpty())
        parts = { "vocals", "music" };
    if (engine == Engine::ai6)
        engine = Engine::ai;   // the 6-part model is picked automatically now
    const bool fourStems = ! vocalsAndMusicOnly (parts);

    auto audio = clip->audio;
    auto clipId = clip->id;
    auto baseName = clip->name;

    auto finish = [&p, clipId] (Job& job)
    {
        if (job.isCancelled()) return;
        if (job.hasFailed()) { notify (p, job.getError(), true); return; }

        auto parent = p.session.findById (clipId);
        if (parent == nullptr) return;

        // replace any previous stems of this clip
        for (auto& old : p.session.getStemsOf (*parent))
            p.session.remove (old.get());

        for (auto& s : job.clips)
            p.session.add (s, false);
        p.session.select (parent.get());

        notify (p, juce::String ((int) job.clips.size()) + " stems ready" + (job.text.isNotEmpty() ? " - " + job.text : juce::String()));
    };

    auto makeStem = [clipId, baseName] (const juce::String& stemName, AudioData::Ptr a)
    {
        Clip::Ptr c (new Clip());
        c->name = baseName + " - " + prettyStemName (stemName);
        c->kind = "Stem";
        c->parentId = clipId;
        c->stemName = stemName;
        c->audio = a;
        return c;
    };

    if (engine == Engine::quick)
    {
        if (needsSixParts (parts))
        {
            notify (p, "Quick Split can't find guitar or piano - pick AI Studio for those.", true);
            return;
        }
        p.jobs.start ("Quick Split", [audio, fourStems, parts, makeStem] (Job& job)
        {
            job.setStatus ("Analysing the mix");
            auto r = QuickSplit::separate (*audio, fourStems, [&job] (float pr) { job.setProgress (pr); }, job.cancelCheck());
            if (r.error.isNotEmpty())
            {
                if (r.error != "Cancelled") job.fail (r.error);
                return;
            }
            std::vector<std::pair<juce::String, AudioData::Ptr>> raw;
            for (auto& s : r.stems)
                raw.push_back ({ s.name, s.audio });
            for (auto& [name, a] : chooseParts (raw, parts, *audio))
                job.clips.push_back (makeStem (name, a));
            job.text = r.note;
        }, finish, clipId);
        return;
    }

    // ---- built-in AI engine (no Python) ----
    auto& tools = p.getTools();
    auto python = tools.getPath (ToolManager::Tool::ai);

    if (engine != Engine::python && ai::isAvailable())
    {
        const auto mode = aiModeFor (engine, parts);
        const auto baseUrl = p.getSettings().getString ("aiModelBaseUrl");
        const int downloadMB = ai::downloadMegabytesFor (mode);
        if (downloadMB > 0)
            notify (p, "First time: downloading the AI model (" + juce::String (downloadMB) + " MB). After that it works offline.");

        p.jobs.start ("AI Split", [audio, mode, baseUrl, parts, makeStem] (Job& job)
        {
            std::vector<ai::Stem> stems;
            if (! ai::separate (*audio, mode, job, baseUrl, stems))
                return;
            std::vector<std::pair<juce::String, AudioData::Ptr>> raw;
            for (auto& st : stems)
                raw.push_back ({ st.name, st.audio });
            for (auto& [name, a] : chooseParts (raw, parts, *audio))
                job.clips.push_back (makeStem (name, a));
        }, finish, clipId);
        return;
    }

    if (engine != Engine::python && ! python.existsAsFile())
    {
        notify (p, ai::unavailableReason(), true);
        return;
    }

    // ---- optional Python engine (fastest with an NVIDIA GPU) ----
    if (! python.existsAsFile())
    {
        notify (p, "The Python AI engine isn't installed. Open Settings (gear) to install it - or pick AI Studio (built-in), which needs no Python.", true);
        if (p.onRequestSettings) p.onRequestSettings();
        return;
    }

    auto script = tools.writeAiScript();
    const juce::String model = needsSixParts (parts) ? "htdemucs_6s" : (engine == Engine::aiMax ? "htdemucs_ft" : "htdemucs");
    const bool twoStems = ! fourStems;
    auto ffmpeg = tools.getPath (ToolManager::Tool::ffmpeg);

    p.jobs.start ("AI Split", [=] (Job& job)
    {
        auto dir = paths::tempDir().getChildFile ("stems_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        dir.createDirectory();
        auto input = dir.getChildFile ("input.wav");

        job.setStatus ("Preparing audio");
        if (! audioio::writeWav (*audio, input, 32))
        {
            job.fail ("Couldn't write a temporary file for the AI engine");
            return;
        }

        juce::StringArray args { python.getFullPathName(), script.getFullPathName(), "separate",
                                 "--input", input.getFullPathName(),
                                 "--outdir", dir.getChildFile ("out").getFullPathName(),
                                 "--model", model };
        if (twoStems)
            args.addArray ({ "--two-stems", "vocals" });

        std::vector<std::pair<juce::String, juce::File>> stems;
        juce::String lastError;
        juce::StringArray tail;

        const int code = job.runner.run (args, [&] (const juce::String& line)
        {
            tail.add (line);
            if (tail.size() > 12) tail.remove (0);

            if (line.startsWith ("PROGRESS "))      job.setProgress (line.substring (9).getFloatValue() / 100.0f);
            else if (line.startsWith ("STATUS "))   job.setStatus (line.substring (7));
            else if (line.startsWith ("ERROR "))    lastError = line.substring (6);
            else if (line.startsWith ("STEM "))
            {
                auto rest = line.substring (5);
                stems.push_back ({ rest.upToFirstOccurrenceOf ("\t", false, false).trim(),
                                   juce::File (rest.fromFirstOccurrenceOf ("\t", false, false).trim()) });
            }
        }, 2 * 60 * 60 * 1000, job.cancelCheck());

        if (code == -2 || job.isCancelled())
        {
            dir.deleteRecursively();
            return;
        }

        if (code != 0 || stems.empty())
        {
            dir.deleteRecursively();
            if (lastError.isEmpty())
                for (auto& t : tail)
                    if (t.containsIgnoreCase ("error")) lastError = t;
            job.fail ("AI Split failed: " + (lastError.isNotEmpty() ? lastError : juce::String ("exit code ") + juce::String (code)));
            return;
        }

        job.setStatus ("Loading stems");
        std::vector<std::pair<juce::String, AudioData::Ptr>> raw;
        for (auto& [name, file] : stems)
        {
            auto r = audioio::loadFile (file, ffmpeg);
            if (r.audio == nullptr) continue;
            // Demucs works at 44.1 kHz; match the source rate so stems line up with the original.
            auto a = std::abs (r.audio->sampleRate - audio->sampleRate) > 0.5 ? edit::resample (*r.audio, audio->sampleRate) : r.audio;
            raw.push_back ({ name, a });
        }
        for (auto& [name, a] : chooseParts (raw, parts, *audio))
            job.clips.push_back (makeStem (name, a));
        dir.deleteRecursively();
        if (job.clips.empty())
            job.fail ("AI Split produced no readable stems");
    }, finish, clipId);
}

//==============================================================================
void installTool (SnaggerProcessor& p, ToolManager::Tool tool, std::function<void (bool)> onDone)
{
    auto& tools = p.getTools();
    p.jobs.start ("Installing " + ToolManager::displayName (tool), [&tools, tool] (Job& job)
    {
        if (! tools.install (tool, job) && ! job.hasFailed() && ! job.isCancelled())
            job.fail ("Couldn't install " + ToolManager::displayName (tool));
    },
    [&p, tool, onDone] (Job& job)
    {
        const bool ok = ! job.hasFailed() && ! job.isCancelled();
        if (job.hasFailed())
            notify (p, job.getError(), true);
        else if (ok)
            notify (p, ToolManager::displayName (tool) + " is ready");
        if (onDone) onDone (ok);
    });
}

//==============================================================================
void pitchTime (SnaggerProcessor& p, Clip::Ptr clip, float semitones, double lengthRatio,
                bool keepFormants, bool tape, std::function<void (bool ok)> onDone)
{
    if (clip == nullptr || clip->audio == nullptr) return;
    auto audio = clip->audio;

    p.jobs.start (tape ? "Varispeed" : "Pitch & Stretch", [=] (Job& job)
    {
        job.setStatus (juce::String (semitones, 1) + " st, " + juce::String (lengthRatio * 100.0, 0) + "% length");
        job.audio = edit::pitchTime (*audio, semitones, lengthRatio, keepFormants, tape);
    },
    [&p, clip, audio, semitones, lengthRatio, tape, onDone] (Job& job)
    {
        if (job.isCancelled() || job.audio == nullptr) { if (onDone) onDone (false); return; }
        if (clip->audio != audio)
        {
            notify (p, "The sample changed while processing; result discarded.", true);
            if (onDone) onDone (false);
            return;
        }

        clip->pushUndo (tape ? "Varispeed" : "Pitch/Stretch");
        const double ratio = (double) job.audio->getNumSamples() / (double) juce::jmax (1, audio->getNumSamples());
        for (auto& s : clip->slices)
            s = (int) std::round (s * ratio);
        if (clip->bpm > 0)
            clip->bpm = clip->bpm / ratio;
        clip->audio = job.audio;
        p.session.clipChanged (clip.get());
        if (p.getSamplerClip() == clip)
            p.setSamplerClip (clip);
        juce::ignoreUnused (semitones, lengthRatio);
        if (onDone) onDone (true);
    });
}

//==============================================================================
double exportBpm (SnaggerProcessor& p, const Clip& clip)
{
    if (p.hostBpm.load() > 0) return p.hostBpm.load();
    return clip.bpm > 0 ? clip.bpm : 120.0;
}

AudioData::Ptr renderForExport (SnaggerProcessor& p, const Clip& clip, int start, int end)
{
    if (clip.audio == nullptr) return nullptr;
    const int n = clip.audio->getNumSamples();
    if (end < 0 || end > n) end = n;
    start = juce::jlimit (0, end, start);
    auto part = (start == 0 && end == n) ? clip.audio : edit::crop (*clip.audio, start, end);
    if (! clip.fx.anyOn())
        return part;
    return renderFx (*part, clip.fx, exportBpm (p, clip));
}

AudioData::Ptr renderPadForExport (SnaggerProcessor& p, const Clip& clip, int padIndex)
{
    if (clip.audio == nullptr) return nullptr;
    auto b = clip.sliceBoundaries();
    if (padIndex < 0 || padIndex + 1 >= (int) b.size()) return nullptr;
    auto pad = renderPad (*clip.audio, b[(size_t) padIndex], b[(size_t) padIndex + 1], clip.padAt (padIndex));
    if (! clip.fx.anyOn())
        return pad;
    return renderFx (*pad, clip.fx, exportBpm (p, clip));
}

juce::File makePadDragFile (SnaggerProcessor& p, const Clip& clip, int padIndex)
{
    auto a = renderPadForExport (p, clip, padIndex);
    if (a == nullptr) return {};
    return audioio::writeDragFile (*a, clip.name + " - chop " + juce::String (padIndex + 1), p.getSettings().getExportBitDepth(), 0, a->getNumSamples());
}

juce::File makeMidiFile (SnaggerProcessor& p, const Clip& clip, const std::vector<midi::Note>& notes, const juce::File& dirIn)
{
    auto dir = dirIn == juce::File() ? paths::dragExportDir() : dirIn;
    auto f = audioio::uniqueFile (dir, clip.name + " - notes", ".mid");
    if (midi::writeMidiFile (notes, exportBpm (p, clip), clip.name, f))
        return f;
    return {};
}

juce::File saveToLibrary (SnaggerProcessor& p, const Clip& clip, int start, int end, const juce::String& suffix)
{
    auto a = renderForExport (p, clip, start, end);
    if (a == nullptr) return {};
    auto dir = p.getSettings().getLibraryDir();
    auto f = audioio::uniqueFile (dir, clip.name + suffix);
    if (audioio::writeWav (*a, f, p.getSettings().getExportBitDepth(), 0, a->getNumSamples()))
    {
        notify (p, "Saved to library: " + f.getFileName());
        return f;
    }
    notify (p, "Couldn't save to " + dir.getFullPathName(), true);
    return {};
}

juce::File makeDragFile (SnaggerProcessor& p, const Clip& clip, int start, int end, const juce::String& suffix)
{
    auto a = renderForExport (p, clip, start, end);
    if (a == nullptr) return {};
    return audioio::writeDragFile (*a, clip.name + suffix, p.getSettings().getExportBitDepth(), 0, a->getNumSamples());
}

} // namespace snag::actions
