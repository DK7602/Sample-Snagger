#pragma once

#include "AudioData.h"
#include "Process.h"
#include <juce_events/juce_events.h>
#include <memory>
#include <vector>

namespace snag
{

//==============================================================================
/** A unit of background work (download, import, stem split, heavy edit, tool install). */
class Job
{
public:
    using Ptr = std::shared_ptr<Job>;
    using Fn  = std::function<void (Job&)>;

    Job (juce::String titleIn, Fn workIn, Fn doneIn)
        : title (std::move (titleIn)), work (std::move (workIn)), done (std::move (doneIn)) {}

    const juce::String title;

    // --- progress (thread-safe) ------------------------------------------------
    void setProgress (float p) noexcept         { progress = p; }
    float getProgress() const noexcept          { return progress; }  // < 0 = indeterminate

    void setStatus (const juce::String& s)      { const juce::ScopedLock sl (lock); status = s; }
    juce::String getStatus() const              { const juce::ScopedLock sl (lock); return status; }

    void fail (const juce::String& message)     { const juce::ScopedLock sl (lock); error = message; failed = true; }
    bool hasFailed() const noexcept             { return failed; }
    juce::String getError() const               { const juce::ScopedLock sl (lock); return error; }

    // --- cancellation ------------------------------------------------------------
    void cancel()                               { cancelled = true; runner.cancel(); }
    bool isCancelled() const noexcept           { return cancelled; }
    std::function<bool()> cancelCheck()         { return [this] { return cancelled.load(); }; }

    bool isFinished() const noexcept            { return finished; }

    // --- results (written by the worker, read on the message thread in done()) -----
    std::vector<Clip::Ptr> clips;
    AudioData::Ptr audio;
    std::vector<int> ints;
    juce::String text;
    juce::var extra;

    ProcessRunner runner;   // for jobs that run command-line tools

private:
    friend class JobManager;
    Fn work, done;
    std::atomic<float> progress { -1.0f };
    std::atomic<bool> cancelled { false }, failed { false }, finished { false };
    juce::CriticalSection lock;
    juce::String status, error;
};

//==============================================================================
/** Runs jobs on a small thread pool and reports progress for the UI. */
class JobManager : public juce::ChangeBroadcaster
{
public:
    JobManager();
    ~JobManager() override;

    Job::Ptr start (const juce::String& title, Job::Fn work, Job::Fn done = {});

    std::vector<Job::Ptr> getActiveJobs() const;
    bool isBusy() const;
    void cancelAll();

    /** Called on the message thread for every finished job (after the job's own done()). */
    std::function<void (Job&)> onJobFinished;

private:
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("Snagger Jobs").withNumberOfThreads (2) };
    mutable juce::CriticalSection lock;
    std::vector<Job::Ptr> jobs;

    JUCE_DECLARE_WEAK_REFERENCEABLE (JobManager)
};

} // namespace snag
