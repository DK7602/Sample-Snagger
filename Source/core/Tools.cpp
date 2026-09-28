#include "Tools.h"
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
        case Tool::ai:     return "AI Stem Engine";
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
        case Tool::ai:     return "Demucs neural network (runs locally) for studio-quality vocal / drum / bass / music stems. ~2-3 GB, needs Python 3.9+.";
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

    for (auto t : { Tool::ffmpeg, Tool::ytdlp, Tool::deno, Tool::ai })
    {
        Status s;
        s.path = locate (t);
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
bool ToolManager::download (const juce::String& url, const juce::File& dest, Job& job, float p0, float p1)
{
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

    auto tmp = dest.getSiblingFile (dest.getFileName() + ".part");
    tmp.deleteFile();
    dest.getParentDirectory().createDirectory();

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
            out.write (buf.get(), (size_t) n);
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

        if (total > 0 && got < total)
        {
            out.flush();
            tmp.deleteFile();
            job.fail ("Download was interrupted");
            return false;
        }
    }

    dest.deleteFile();
    if (! tmp.moveFileTo (dest))
    {
        job.fail ("Can't move download into place: " + dest.getFullPathName());
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
    dest.deleteFile();
    tmp.moveFileTo (dest);
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
                std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
                auto dest = paths::toolsDir().getChildFile (exeName ("deno"));
                dest.deleteFile();
                juce::FileOutputStream out (dest);
                ok = in != nullptr && out.openedOk() && out.writeFromInputStream (*in, -1) > 1000000;
                out.flush();
                if (ok) makeRunnable (dest);
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

bool ToolManager::install (Tool t, Job& job)
{
    bool ok = false;
    switch (t)
    {
        case Tool::ffmpeg: ok = installFfmpeg (job); break;
        case Tool::ytdlp:  ok = installYtDlp (job); break;
        case Tool::deno:   ok = installDeno (job); break;
        case Tool::ai:     ok = installAi (job); break;
    }
    if (ok)
    {
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
