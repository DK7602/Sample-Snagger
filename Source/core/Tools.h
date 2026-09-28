#pragma once

#include "Settings.h"
#include "Jobs.h"
#include <juce_events/juce_events.h>

namespace snag
{

//==============================================================================
/** Finds, installs and describes the helper tools:
      FFmpeg  - decodes video / exotic audio, needed by yt-dlp
      yt-dlp  - high-quality audio from YouTube & 1000+ sites
      Deno    - JavaScript runtime yt-dlp uses for YouTube
      AI models - weights for the built-in AI stem engine (no Python)
      AI      - optional private Python environment with Demucs (fastest on NVIDIA GPUs) */
class ToolManager : public juce::ChangeBroadcaster
{
public:
    enum class Tool { ffmpeg, ytdlp, deno, aiModels, ai };   // ai = optional Python engine

    struct Status
    {
        bool found = false;
        juce::File path;
        juce::String version;
        juce::String detail;
    };

    explicit ToolManager (Settings&);
    ~ToolManager() override;

    static juce::String displayName (Tool);
    static juce::String description (Tool);

    Status getStatus (Tool) const;
    juce::File getPath (Tool) const;          // empty File if not available
    bool isAvailable (Tool t) const          { return getPath (t).existsAsFile(); }
    bool isRefreshing() const noexcept       { return refreshing; }

    /** Re-detects everything (runs version commands). Call from a background thread. */
    void refresh();
    void refreshAsync();

    /** Downloads / installs a tool. Runs inside a Job (background thread). */
    bool install (Tool, Job&);

    /** Writes the bundled Python helper script into the tools folder and returns it. */
    juce::File writeAiScript() const;

    /** The Python inside our private AI environment. */
    juce::File aiVenvPython() const;
    juce::File aiVenvDir() const;

    /** Finds a suitable system Python (3.9+) and returns the command to run it. */
    juce::StringArray findSystemPython (juce::String& versionOut) const;

    /** Downloads a URL to a file with progress (0..1 mapped into [p0, p1] of the job). */
    static bool download (const juce::String& url, const juce::File& dest, Job&, float p0, float p1);

    static juce::String exeName (const juce::String& base);

private:
    Settings& settings;
    mutable juce::CriticalSection lock;
    std::map<Tool, Status> statuses;
    std::atomic<bool> refreshing { false }, shuttingDown { false };

    struct RefreshThread : juce::Thread
    {
        explicit RefreshThread (ToolManager& o) : juce::Thread ("Snagger tool check"), owner (o) {}
        void run() override { owner.refresh(); }
        ToolManager& owner;
    };
    RefreshThread refreshThread { *this };

    juce::File locate (Tool) const;
    static juce::File searchPath (const juce::StringArray& names, const juce::StringArray& extraDirs);

    bool installFfmpeg (Job&);
    bool installYtDlp (Job&);
    bool installDeno (Job&);
    bool installAi (Job&);
    bool installAiModels (Job&);
};

//==============================================================================
/** Objects shared by every Sample Snagger instance in the process. */
struct SharedServices
{
    SharedServices() : tools (settings) {}

    Settings settings;
    ToolManager tools;
};

} // namespace snag
