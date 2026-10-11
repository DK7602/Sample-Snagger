#include "Tools.h"
#include "AiStems.h"
#include "SnaggerBinaryData.h"

namespace snag
{

ToolManager::ToolManager (Settings& s) : settings (s) {}

ToolManager::~ToolManager()
{
    shuttingDown = true;
    refreshThread.stopThread (10000);
}

juce::String ToolManager::exeName (const juce::String& base)
{
   #if JUCE_WINDOWS
    return base + ".exe";
   #else
    return base;
   #endif
}

juce::String ToolManager::displayName (Tool t)
{
    switch (t)
    {
        case Tool::ffmpeg: return "FFmpeg";
        case Tool::ytdlp:  return "yt-dlp";
        case Tool::deno:   return "Deno";
        case Tool::aiModels: return "AI Stem Models";
        case Tool::ai:     return "Python AI Engine";
    }
    return {};
}

juce::String ToolManager::description (Tool t)
{
    switch (t)
    {
        case Tool::ffmpeg: return "Reads video files (mp4, mov, mkv, webm...) and any audio format. Required for HQ downloads.";
        case Tool::ytdlp:  return "Grabs full-quality audio from YouTube, SoundCloud, TikTok, Instagram, Vimeo + 1000 more sites.";
        case Tool::deno:   return "Small JavaScript runtime that yt-dlp needs to read YouTube pages reliably.";
        case Tool::aiModels: return "Studio-quality AI stems built right in - no Python needed. Vocal + 4-stem models, 162 MB, downloaded once.";
        case Tool::ai:     return "Optional: the same AI through Python + PyTorch - faster on NVIDIA graphics cards. ~2-3 GB, needs Python 3.9+.";
    }
    return {};
}

ToolManager::Status ToolManager::getStatus (Tool t) const
{
    const juce::ScopedLock sl (lock);
    auto it = statuses.find (t);
    return it != statuses.end() ? it->second : Status{};
}

juce::File ToolManager::getPath (Tool t) const
{
    {
        const juce::ScopedLock sl (lock);
        auto it = statuses.find (t);
        if (it != statuses.end() && it->second.found)
            return it->second.path;
    }
    // Not refreshed yet: do a quick filesystem lookup without running anything.
    auto f = locate (t);
    return f.existsAsFile() ? f : juce::File();
}

juce::File ToolManager::aiVenvDir() const { return paths::toolsDir().getChildFile ("ai-env"); }

juce::File ToolManager::aiVenvPython() const
{
   #if JUCE_WINDOWS
    return aiVenvDir().getChildFile ("Scripts").getChildFile ("python.exe");
   #else
    return aiVenvDir().getChildFile ("bin").getChildFile ("python3");
   #endif
}

juce::File ToolManager::searchPath (const juce::StringArray& names, const juce::StringArray& extraDirs)
{
    juce::StringArray dirs;
    dirs.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}),
                   #if JUCE_WINDOWS
                    ";",
                   #else
                    ":",
                   #endif
                    "\"");
    dirs.addArray (extraDirs);

    for (auto& d : dirs)
    {
        if (d.trim().isEmpty() || ! juce::File::isAbsolutePath (d.trim()))
            continue;
        for (auto& n : names)
        {
            auto f = juce::File (d.trim()).getChildFile (n);
            if (f.existsAsFile())
                return f;
        }
    }
    return {};
}

static juce::StringArray commonBinDirs()
{
    return {
       #if JUCE_MAC
        "/opt/homebrew/bin", "/usr/local/bin", "/opt/local/bin", "/usr/bin",
       #elif JUCE_WINDOWS
        "C:\\ffmpeg\\bin", "C:\\Program Files\\ffmpeg\\bin",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("scoop\\shims").getFullPathName(),
        "C:\\ProgramData\\chocolatey\\bin",
       #else
        "/usr/local/bin", "/usr/bin", "/snap/bin",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/bin").getFullPathName(),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".deno/bin").getFullPathName(),
       #endif
    };
}

juce::File ToolManager::locate (Tool t) const
{
    auto overridePath = settings.getToolOverride (displayName (t));
    if (overridePath.isNotEmpty() && juce::File::isAbsolutePath (overridePath) && juce::File (overridePath).existsAsFile())
        return juce::File (overridePath);

    const auto tools = paths::toolsDir();

    switch (t)
    {
        case Tool::ffmpeg:
        {
            auto own = tools.getChildFile (exeName ("ffmpeg"));
            if (own.existsAsFile()) return own;
            return searchPath ({ exeName ("ffmpeg") }, commonBinDirs());
        }
        case Tool::ytdlp:
        {
            auto own = tools.getChildFile (exeName ("yt-dlp"));
            if (own.existsAsFile()) return own;
            return searchPath ({ exeName ("yt-dlp") }, commonBinDirs());
        }
        case Tool::deno:
        {
            auto own = tools.getChildFile (exeName ("deno"));
            if (own.existsAsFile()) return own;
            return searchPath ({ exeName ("deno") }, commonBinDirs());
        }
        case Tool::aiModels:
            return ai::modelFile (ai::ModelId::ftVocals);
        case Tool::ai:
            return aiVenvPython();
    }
    return {};
}

void ToolManager::refreshAsync()
{
    if (refreshThread.isThreadRunning() || shuttingDown)
        return;
    refreshing = true;
    sendChangeMessage();
    refreshThread.startThread();
}

void ToolManager::refresh()
{
    refreshing = true;
    std::map<Tool, Status> fresh;
    auto stop = [this] { return shuttingDown.load(); };

    for (auto t : { Tool::ffmpeg, Tool::ytdlp, Tool::deno, Tool::aiModels, Tool::ai })
    {
        Status s;
        s.path = locate (t);
        if (t == Tool::aiModels)
        {
            int count = 0, mb = 0;
            for (auto id : { ai::ModelId::ftVocals, ai::ModelId::htdemucs4, ai::ModelId::htdemucs6,
                             ai::ModelId::ftDrums, ai::ModelId::ftBass, ai::ModelId::ftOther })
                if (ai::isDownloaded (id)) { ++count; mb += ai::info (id).megabytes; }
            s.found = ai::isAvailable() && ai::isDownloaded (ai::ModelId::ftVocals) && ai::isDownloaded (ai::ModelId::htdemucs4);
            s.version = count > 0 ? juce::String (count) + (count == 1 ? " model, " : " models, ") + juce::String (mb) + " MB" : juce::String();
            if (! ai::isAvailable())
                s.detail = "Not supported on this computer";
            else if (! s.found)
                s.detail = count > 0 ? "Partly downloaded - click Install to finish" : "Not downloaded yet (or downloads on first use)";
            fresh[t] = s;
            continue;
        }
        if (s.path.existsAsFile())
        {
            juce::String out;
            int code = -1;
            switch (t)
            {
                case Tool::ffmpeg:
                    code = ProcessRunner::runAndCapture ({ s.path.getFullPathName(), "-hide_banner", "-version" }, out, 15000, stop);
                    s.version = out.upToFirstOccurrenceOf ("\n", false, false).fromFirstOccurrenceOf ("version ", false, false).upToFirstOccurrenceOf (" ", false, false);
                    break;
                case Tool::ytdlp:
                    code = ProcessRunner::runAndCapture ({ s.path.getFullPathName(), "--version" }, out, 60000, stop);
                    s.version = out.trim().fromLastOccurrenceOf ("\n", false, false);
                    break;
                case Tool::deno:
                    code = ProcessRunner::runAndCapture ({ s.path.getFullPathName(), "--version" }, out, 15000, stop);
                    s.version = out.upToFirstOccurrenceOf ("\n", false, false).fromFirstOccurrenceOf ("deno ", false, false).upToFirstOccurrenceOf (" ", false, false);
                    break;
                case Tool::aiModels:
                    break;
                case Tool::ai:
                {
                    auto script = writeAiScript();
                    code = ProcessRunner::runAndCapture ({ s.path.getFullPathName(), script.getFullPathName(), "check" }, out, 120000, stop);
                    for (auto& l : juce::StringArray::fromLines (out))
                        if (l.startsWith ("OK "))
                            s.version = l.substring (3).trim();
                    if (code != 0)
                        s.detail = "Installed but not working - click Install to repair";
                    break;
                }
            }
            s.found = (code == 0);
            if (! s.found && s.detail.isEmpty())
                s.detail = "Found but failed to run";

            // A file picked with LOCATE that doesn't run (the wrong one, most likely) is let go, so
            // Sample Snagger's own copy - or the next INSTALL - can take over instead of being stuck behind it.
            const auto located = settings.getToolOverride (displayName (t));
            if (! s.found && ! shuttingDown && t != Tool::ai && located.isNotEmpty() && juce::File (located) == s.path)
            {
                settings.setToolOverride (displayName (t), {});
                Status again;
                again.path = locate (t);
                again.detail = "The file you located didn't run, so it isn't used any more";
                if (again.path.existsAsFile())
                {
                    juce::String said;
                    const bool ff = t == Tool::ffmpeg;
                    const int ok = ProcessRunner::runAndCapture (ff ? juce::StringArray { again.path.getFullPathName(), "-hide_banner", "-version" }
                                                                    : juce::StringArray { again.path.getFullPathName(), "--version" },
                                                                 said, 60000, stop);
                    if (ok == 0)
                    {
                        again.found = true;
                        again.detail = {};
                        const auto first = said.upToFirstOccurrenceOf ("\n", false, false);
                        again.version = ff ? first.fromFirstOccurrenceOf ("version ", false, false).upToFirstOccurrenceOf (" ", false, false)
                                      : t == Tool::deno ? first.fromFirstOccurrenceOf ("deno ", false, false).upToFirstOccurrenceOf (" ", false, false)
                                                        : said.trim().fromLastOccurrenceOf ("\n", false, false);
                    }
                }
                s = again;
            }
        }
        fresh[t] = s;
    }

    {
        const juce::ScopedLock sl (lock);
        statuses = fresh;
    }
    refreshing = false;
    sendChangeMessage();
}

//==============================================================================
/** Downloads of one and the same file wait for each other (INSTALL ALL next to a first AI split,
    two plug-in windows...): the second one then finds the file already there. */
struct DownloadsInFlight
{
    juce::CriticalSection lock;
    juce::StringArray paths;

    static DownloadsInFlight& get() { static DownloadsInFlight d; return d; }

    struct Claim
    {
        explicit Claim (const juce::String& p) : path (p) {}
        ~Claim() { auto& d = get(); const juce::ScopedLock sl (d.lock); d.paths.removeString (path); }
        juce::String path;
    };
};

bool ToolManager::download (const juce::String& url, const juce::File& dest, Job& job, float p0, float p1)
{
    auto& inFlight = DownloadsInFlight::get();
    const auto key = dest.getFullPathName();
    const auto askedAt = juce::Time::getCurrentTime();
    bool waited = false;
    for (;;)
    {
        {
            const juce::ScopedLock sl (inFlight.lock);
            if (! inFlight.paths.contains (key))
            {
                inFlight.paths.add (key);
                break;
            }
        }
        if (job.isCancelled())
            return false;
        waited = true;
        job.setStatus ("Waiting for the same download to finish");
        juce::Thread::sleep (200);
    }
    const DownloadsInFlight::Claim claim (key);
    if (waited && dest.existsAsFile() && dest.getLastModificationTime() > askedAt - juce::RelativeTime::seconds (5))
        return true;   // the other one fetched it while we waited

    int status = 0;
    juce::StringPairArray headers;
    auto stream = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                        .withConnectionTimeoutMs (20000)
                                                        .withNumRedirectsToFollow (10)
                                                        .withStatusCode (&status)
                                                        .withResponseHeaders (&headers)
                                                        .withExtraHeaders ("User-Agent: SampleSnagger/1.0"));
    if (stream == nullptr || (status != 0 && status >= 400))
    {
        job.fail ("Download failed (" + (status != 0 ? "HTTP " + juce::String (status) : juce::String ("no connection")) + "): " + url);
        return false;
    }

    // (its own name every time: another Sample Snagger - the app next to the plug-in - may be
    //  fetching the same file, and two writers in one ".part" left a broken tool or AI model behind)
    auto tmp = dest.getSiblingFile (dest.getFileName() + "." + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()) + ".part");
    dest.getParentDirectory().createDirectory();
    for (auto& stale : dest.getParentDirectory().findChildFiles (juce::File::findFiles, false, dest.getFileName() + ".*part"))
        if (stale.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::hours (6))
            stale.deleteFile();   // left behind by a download that was cut off

    {
        juce::FileOutputStream out (tmp);
        if (! out.openedOk())
        {
            job.fail ("Can't write to " + tmp.getFullPathName());
            return false;
        }

        const auto total = stream->getTotalLength();
        juce::HeapBlock<char> buf (1 << 16);
        juce::int64 got = 0;

        for (;;)
        {
            if (job.isCancelled())
            {
                out.flush();
                tmp.deleteFile();
                return false;
            }
            const int n = stream->read (buf.get(), 1 << 16);
            if (n <= 0)
                break;
            if (! out.write (buf.get(), (size_t) n))
            {
                tmp.deleteFile();
                job.fail ("Couldn't write the download - is the disk full? (" + dest.getParentDirectory().getFullPathName() + ")");
                return false;
            }
            got += n;

            if (total > 0)
            {
                job.setProgress (p0 + (p1 - p0) * (float) got / (float) total);
                job.setStatus (juce::String (got / 1048576.0, 1) + " / " + juce::String (total / 1048576.0, 1) + " MB");
            }
            else
            {
                job.setStatus (juce::String (got / 1048576.0, 1) + " MB");
            }
        }

        out.flush();
        if ((total > 0 && got < total) || got == 0 || out.getStatus().failed())
        {
            tmp.deleteFile();
            job.fail (out.getStatus().failed() ? "Couldn't write the download - is the disk full?" : "Download was interrupted");
            return false;
        }
    }

    if (! tmp.replaceFileIn (dest))
    {
        tmp.deleteFile();
        job.fail ("Can't put the download in place (is " + dest.getFileName() + " in use?): " + dest.getFullPathName());
        return false;
    }
    return true;
}

static void makeRunnable (const juce::File& f)
{
   #if ! JUCE_WINDOWS
    f.setExecutePermission (true);
   #endif
   #if JUCE_MAC
    juce::String ignored;
    ProcessRunner::runAndCapture ({ "/usr/bin/xattr", "-d", "com.apple.quarantine", f.getFullPathName() }, ignored, 5000);
   #endif
}

bool ToolManager::installFfmpeg (Job& job)
{
    juce::String asset;
   #if JUCE_WINDOWS
    asset = "ffmpeg-win32-x64.gz";
   #elif JUCE_MAC
    #if JUCE_ARM
     asset = "ffmpeg-darwin-arm64.gz";
    #else
     asset = "ffmpeg-darwin-x64.gz";
    #endif
   #else
    #if JUCE_ARM
     asset = "ffmpeg-linux-arm64.gz";
    #else
     asset = "ffmpeg-linux-x64.gz";
    #endif
   #endif

    const auto url = "https://github.com/eugeneware/ffmpeg-static/releases/download/b6.1.1/" + asset;
    auto gz = paths::tempDir().getChildFile (asset);
    job.setStatus ("Downloading FFmpeg");
    if (! download (url, gz, job, 0.0f, 0.85f))
        return false;

    job.setStatus ("Unpacking FFmpeg");
    job.setProgress (0.9f);
    auto dest = paths::toolsDir().getChildFile (exeName ("ffmpeg"));
    auto tmp  = dest.getSiblingFile (dest.getFileName() + ".new");
    {
        juce::FileInputStream in (gz);
        juce::GZIPDecompressorInputStream unzip (&in, false, juce::GZIPDecompressorInputStream::gzipFormat);
        tmp.deleteFile();
        juce::FileOutputStream out (tmp);
        if (! out.openedOk() || out.writeFromInputStream (unzip, -1) < 1000000)
        {
            job.fail ("FFmpeg download looks corrupt");
            gz.deleteFile();
            return false;
        }
    }
    gz.deleteFile();
    if (! tmp.replaceFileIn (dest))
    {
        // (an FFmpeg that is running right now can't be replaced: say so, instead of "FFmpeg is ready")
        tmp.deleteFile();
        job.fail ("Couldn't replace FFmpeg - it may be in use. Wait for imports and downloads to finish, then try again.");
        return false;
    }
    makeRunnable (dest);
    return dest.existsAsFile();
}

bool ToolManager::installYtDlp (Job& job)
{
    juce::String asset;
   #if JUCE_WINDOWS
    asset = "yt-dlp.exe";
   #elif JUCE_MAC
    asset = "yt-dlp_macos";
   #else
    #if JUCE_ARM
     asset = "yt-dlp_linux_aarch64";
    #else
     asset = "yt-dlp_linux";
    #endif
   #endif

    job.setStatus ("Downloading yt-dlp");
    auto dest = paths::toolsDir().getChildFile (exeName ("yt-dlp"));
    if (! download ("https://github.com/yt-dlp/yt-dlp/releases/latest/download/" + asset, dest, job, 0.0f, 1.0f))
        return false;
    makeRunnable (dest);
    return true;
}

bool ToolManager::installDeno (Job& job)
{
    juce::String asset;
   #if JUCE_WINDOWS
    asset = "deno-x86_64-pc-windows-msvc.zip";
   #elif JUCE_MAC
    #if JUCE_ARM
     asset = "deno-aarch64-apple-darwin.zip";
    #else
     asset = "deno-x86_64-apple-darwin.zip";
    #endif
   #else
    #if JUCE_ARM
     asset = "deno-aarch64-unknown-linux-gnu.zip";
    #else
     asset = "deno-x86_64-unknown-linux-gnu.zip";
    #endif
   #endif

    job.setStatus ("Downloading Deno");
    auto zipFile = paths::tempDir().getChildFile (asset);
    if (! download ("https://github.com/denoland/deno/releases/latest/download/" + asset, zipFile, job, 0.0f, 0.9f))
        return false;

    job.setStatus ("Unpacking Deno");
    bool ok = false;
    {
        juce::ZipFile zip (zipFile);
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* e = zip.getEntry (i);
            if (e != nullptr && juce::File::createFileWithoutCheckingPath (e->filename).getFileName() == exeName ("deno"))
            {
                // unpack next to it first: a cut-off archive must not replace a Deno that works
                std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
                auto dest = paths::toolsDir().getChildFile (exeName ("deno"));
                auto fresh = dest.getSiblingFile (dest.getFileName() + ".new");
                fresh.deleteFile();
                {
                    juce::FileOutputStream out (fresh);
                    ok = in != nullptr && out.openedOk() && out.writeFromInputStream (*in, -1) > 1000000;
                    out.flush();
                    ok = ok && ! out.getStatus().failed();
                }
                ok = ok && fresh.replaceFileIn (dest);
                if (ok) makeRunnable (dest);
                else fresh.deleteFile();
                break;
            }
        }
    }
    zipFile.deleteFile();
    if (! ok)
        job.fail ("Couldn't unpack Deno");
    return ok;
}

juce::File ToolManager::writeAiScript() const
{
    auto f = paths::toolsDir().getChildFile ("snagger_ai.py");
    juce::MemoryBlock mb (SnaggerBinary::snagger_ai_py, (size_t) SnaggerBinary::snagger_ai_pySize);
    juce::MemoryBlock existing;
    if (! f.existsAsFile() || ! f.loadFileAsData (existing) || existing != mb)
        f.replaceWithData (mb.getData(), mb.getSize());
    return f;
}

juce::StringArray ToolManager::findSystemPython (juce::String& versionOut) const
{
    std::vector<juce::StringArray> candidates;

    auto overridePy = settings.getToolOverride ("Python");
    if (overridePy.isNotEmpty())
        candidates.push_back ({ overridePy });

   #if JUCE_WINDOWS
    for (auto v : { "3.12", "3.11", "3.13", "3.10", "3.9" })
        candidates.push_back ({ "py", juce::String ("-") + v });
    auto local = juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs\\Python");
    for (auto v : { "312", "311", "313", "310", "39" })
        candidates.push_back ({ local.getChildFile ("Python" + juce::String (v)).getChildFile ("python.exe").getFullPathName() });
    candidates.push_back ({ "py", "-3" });
    candidates.push_back ({ "python" });
   #elif JUCE_MAC
    for (auto v : { "3.12", "3.11", "3.13", "3.10" })
    {
        candidates.push_back ({ "/opt/homebrew/bin/python" + juce::String (v) });
        candidates.push_back ({ "/usr/local/bin/python" + juce::String (v) });
        candidates.push_back ({ "/Library/Frameworks/Python.framework/Versions/" + juce::String (v) + "/bin/python3" });
    }
    candidates.push_back ({ "/opt/homebrew/bin/python3" });
    candidates.push_back ({ "/usr/local/bin/python3" });
    candidates.push_back ({ "/usr/bin/python3" });
   #else
    for (auto v : { "3.12", "3.11", "3.13", "3.10", "3.9" })
        candidates.push_back ({ "python" + juce::String (v) });
    candidates.push_back ({ "python3" });
   #endif

    for (auto& cmd : candidates)
    {
        if (juce::File::isAbsolutePath (cmd[0]) && ! juce::File (cmd[0]).existsAsFile())
            continue;

        auto args = cmd;
        args.addArray ({ "-c", "import sys;print('PYVER %d.%d' % sys.version_info[:2])" });
        juce::String out;
        if (ProcessRunner::runAndCapture (args, out, 20000) != 0)
            continue;

        auto ver = out.fromFirstOccurrenceOf ("PYVER ", false, false).trim();
        const int major = ver.upToFirstOccurrenceOf (".", false, false).getIntValue();
        const int minor = ver.fromFirstOccurrenceOf (".", false, false).getIntValue();
        if (major == 3 && minor >= 9)
        {
            versionOut = ver;
            return cmd;
        }
    }
    return {};
}

bool ToolManager::installAi (Job& job)
{
    job.setStatus ("Looking for Python");
    juce::String ver;
    auto python = findSystemPython (ver);
    if (python.isEmpty())
    {
        job.fail ("Python 3.9 or newer is needed for AI stems. Install it from python.org (3.12 recommended), then click Install again.");
        return false;
    }

    auto script = writeAiScript();
    auto args = python;
    args.addArray ({ script.getFullPathName(), "install", "--venv", aiVenvDir().getFullPathName() });

    juce::String lastError;
    job.setStatus ("Using Python " + ver);
    const int code = job.runner.run (args, [&] (const juce::String& line)
    {
        if (line.startsWith ("PROGRESS "))      job.setProgress (line.substring (9).getFloatValue() / 100.0f);
        else if (line.startsWith ("STATUS "))   job.setStatus (line.substring (7));
        else if (line.startsWith ("ERROR "))    lastError = line.substring (6);
        else if (line.containsIgnoreCase ("Downloading ") || line.containsIgnoreCase ("Installing collected"))
            job.setStatus (line.trim().substring (0, 90));
    }, 3 * 60 * 60 * 1000, job.cancelCheck());

    if (code == -2)
        return false;
    if (code != 0)
    {
        job.fail (lastError.isNotEmpty() ? lastError : "AI engine install failed (exit code " + juce::String (code) + ")");
        return false;
    }
    return true;
}

bool ToolManager::installAiModels (Job& job)
{
    if (! ai::isAvailable())
    {
        job.fail (ai::unavailableReason());
        return false;
    }
    const auto base = settings.getString ("aiModelBaseUrl");
    return ai::ensureModels (ai::Mode::vocalsMusic, job, base, 0.0f, 0.5f)
        && ai::ensureModels (ai::Mode::fourStems, job, base, 0.5f, 1.0f);
}

bool ToolManager::install (Tool t, Job& job)
{
    {
        const juce::ScopedLock sl (lock);
        if (! installing.insert (t).second)
        {
            job.fail (displayName (t) + " is already being installed.");
            return false;
        }
    }
    struct Done
    {
        ToolManager& owner; Tool tool;
        ~Done() { const juce::ScopedLock sl (owner.lock); owner.installing.erase (tool); }
    } done { *this, t };

    bool ok = false;
    switch (t)
    {
        case Tool::ffmpeg: ok = installFfmpeg (job); break;
        case Tool::ytdlp:  ok = installYtDlp (job); break;
        case Tool::deno:   ok = installDeno (job); break;
        case Tool::aiModels: ok = installAiModels (job); break;
        case Tool::ai:     ok = installAi (job); break;
    }
    if (ok)
    {
        // INSTALL means "use Sample Snagger's own copy": a copy picked earlier with LOCATE (maybe the
        // wrong file, which is why they are installing) would otherwise stay in charge for good.
        if (t == Tool::ffmpeg || t == Tool::ytdlp || t == Tool::deno)
            settings.setToolOverride (displayName (t), {});

        job.setStatus ("Verifying");
        refresh();
        if (! getStatus (t).found)
        {
            job.fail (displayName (t) + " was installed but won't run: " + getStatus (t).detail);
            return false;
        }
    }
    return ok;
}

} // namespace snag
