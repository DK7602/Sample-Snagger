#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

namespace snag
{

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
            const juce::ScopedLock sl (lock);
            process = std::make_unique<juce::ChildProcess>();
            if (! process->start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            {
                process.reset();
                return -1;
            }
        }

        const auto startTime = juce::Time::getMillisecondCounter();
        juce::MemoryBlock pending;
        // JUCE's POSIX implementation uses fread(), which blocks until the buffer is full,
        // so keep reads small to get timely progress lines.
        char buf[64];

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

        flushLines (true);

        const juce::ScopedLock sl (lock);
        int code = -1;
        if (process != nullptr)
        {
            if (cancelled)
                process->kill();
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
            process->kill();
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
    juce::CriticalSection lock;
    std::unique_ptr<juce::ChildProcess> process;
    std::atomic<bool> cancelled { false };
};

} // namespace snag
