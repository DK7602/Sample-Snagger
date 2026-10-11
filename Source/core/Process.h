#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <algorithm>
#include <functional>
#include <thread>
#include <vector>

namespace snag
{

//==============================================================================
/** The programs a program has started in turn. yt-dlp runs ffmpeg, and on Windows yt-dlp.exe is
    a small starter that runs the real yt-dlp as a child: ending only the program that was
    launched left those running - a cancelled download carried on in the background. */
namespace proctree
{
    int currentProcess();
    std::vector<int> childrenOfCurrentProcess();

    /** When a process started, in the system's own units (0 where that isn't needed or known). */
    juce::uint64 startTimeOf (int pid);

    /** Everything below `root`: its children, theirs, and so on. */
    std::vector<int> descendantsOf (int root, juce::uint64 rootStarted = 0);

    /** Ends everything below `root` (not `root` itself). Returns how many processes that was. */
    int killDescendants (int root, juce::uint64 rootStarted = 0);

    /** Stops a process from running on, where the system can do that, until it is ended. */
    void freeze (int pid);
}

//==============================================================================
/** Runs a command-line tool, streams its output line-by-line and can be cancelled
    from another thread. Used for ffmpeg, yt-dlp, deno and the AI stem engine. */
class ProcessRunner
{
public:
    using LineCallback = std::function<void (const juce::String& line)>;

    /** Blocks until the process exits (or is cancelled). Returns the exit code, or -1 if
        the process could not be started, or -2 if it was cancelled. */
    int run (const juce::StringArray& args, LineCallback onLine = {}, int timeoutMs = -1,
             std::function<bool()> shouldCancel = {})
    {
        cancelled = false;

        {
            // Which process is ours? The one that is new among our children once it has started -
            // one program is started at a time, so that there is only one new one to find.
            static juce::CriticalSection oneStartAtATime;
            const juce::ScopedLock starting (oneStartAtATime);
            const auto before = proctree::childrenOfCurrentProcess();

            const juce::ScopedLock sl (lock);
            childPid = 0;
            childStarted = 0;
            process = std::make_unique<juce::ChildProcess>();
            if (! process->start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            {
                process.reset();
                return -1;
            }

            std::vector<int> fresh;
            for (auto pid : proctree::childrenOfCurrentProcess())
                if (std::find (before.begin(), before.end(), pid) == before.end())
                    fresh.push_back (pid);
            if (fresh.size() == 1)   // (anything else: something beside us started a program just then - don't guess)
            {
                childPid = fresh[0];
                childStarted = proctree::startTimeOf (childPid);
            }
        }

        const auto startTime = juce::Time::getMillisecondCounter();
        juce::MemoryBlock pending;
        // JUCE's POSIX implementation uses fread(), which blocks until the buffer is full,
        // so keep reads small to get timely progress lines.
        char buf[64];

        // Reading the output blocks for as long as the program stays quiet. So the time limit and
        // the cancel check are watched from the side: a program that prints nothing and never ends
        // (the wrong file picked with LOCATE, a stuck download) is still stopped on time.
        std::atomic<bool> finished { false };
        std::thread watchdog;
        if (timeoutMs > 0 || shouldCancel)
            watchdog = std::thread ([&]
            {
                while (! finished.load())
                {
                    if ((shouldCancel && shouldCancel())
                        || (timeoutMs > 0 && (int) (juce::Time::getMillisecondCounter() - startTime) > timeoutMs))
                    {
                        cancel();
                        return;
                    }
                    juce::Thread::sleep (25);
                }
            });
        struct JoinWatchdog
        {
            std::atomic<bool>& done; std::thread& thread;
            ~JoinWatchdog() { done = true; if (thread.joinable()) thread.join(); }
        } joinWatchdog { finished, watchdog };

        auto flushLines = [&] (bool all)
        {
            auto* bytes = static_cast<const char*> (pending.getData());
            const int size = (int) pending.getSize();

            // Split on the last line-break *byte* so multi-byte UTF-8 characters are never cut.
            int lastBreak = -1;
            for (int i = size; --i >= 0;)
                if (bytes[i] == '\n' || bytes[i] == '\r') { lastBreak = i; break; }

            const int completeBytes = all ? size : lastBreak + 1;
            if (completeBytes <= 0)
                return;

            juce::StringArray lines;
            lines.addTokens (juce::String::fromUTF8 (bytes, completeBytes), "\r\n", {});
            for (auto& l : lines)
                if (l.trim().isNotEmpty() && onLine)
                    onLine (l.trimEnd());

            juce::MemoryBlock rest (bytes + completeBytes, (size_t) (size - completeBytes));
            pending = std::move (rest);
        };

        for (;;)
        {
            int n = 0;
            {
                juce::ChildProcess* p = nullptr;
                { const juce::ScopedLock sl (lock); p = process.get(); }
                if (p == nullptr) break;
                n = p->readProcessOutput (buf, (int) sizeof (buf));
            }

            if (n > 0)
            {
                pending.append (buf, (size_t) n);
                flushLines (false);
            }
            else
            {
                const juce::ScopedLock sl (lock);
                if (process == nullptr || ! process->isRunning())
                    break;
                juce::Thread::sleep (5);
            }

            if (shouldCancel && shouldCancel())
                cancel();

            if (cancelled)
                break;

            if (timeoutMs > 0 && (int) (juce::Time::getMillisecondCounter() - startTime) > timeoutMs)
            {
                cancel();
                break;
            }
        }

        finished = true;   // the program is done (or stopped): from here on nothing may cancel it any more
        flushLines (true);

        const juce::ScopedLock sl (lock);
        int code = -1;
        if (process != nullptr)
        {
            if (cancelled)
                endProcessAndItsChildren();
            else
                process->waitForProcessToFinish (5000);

            code = cancelled ? -2 : (int) process->getExitCode();
            process.reset();
        }
        return cancelled ? -2 : code;
    }

    void cancel()
    {
        cancelled = true;
        const juce::ScopedLock sl (lock);
        if (process != nullptr)
            endProcessAndItsChildren();
    }

    bool wasCancelled() const noexcept { return cancelled; }

    /** Convenience: run and collect all output. */
    static int runAndCapture (const juce::StringArray& args, juce::String& output, int timeoutMs = 20000,
                              std::function<bool()> shouldCancel = {})
    {
        ProcessRunner r;
        juce::StringArray lines;
        auto code = r.run (args, [&] (const juce::String& l) { lines.add (l); }, timeoutMs, std::move (shouldCancel));
        output = lines.joinIntoString ("\n");
        return code;
    }

private:
    /** (call with the lock held) */
    void endProcessAndItsChildren()
    {
        // (only while it is still there: once it has ended, its number can come to mean another program)
        const bool tree = childPid > 0 && process->isRunning();
        if (tree)
        {
            proctree::freeze (childPid);
            proctree::killDescendants (childPid, childStarted);
        }
        process->kill();
        if (tree)
            proctree::killDescendants (childPid, childStarted);   // (anything it still got started in between)
        childPid = 0;
    }

    juce::CriticalSection lock;
    std::unique_ptr<juce::ChildProcess> process;
    int childPid = 0;                 // 0: not known, so only the process itself can be ended
    juce::uint64 childStarted = 0;
    std::atomic<bool> cancelled { false };
};

} // namespace snag
