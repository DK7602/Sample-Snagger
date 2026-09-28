#include "Jobs.h"

namespace snag
{

JobManager::JobManager() = default;

JobManager::~JobManager()
{
    cancelAll();
    pool.removeAllJobs (true, 15000);
}

Job::Ptr JobManager::start (const juce::String& title, Job::Fn work, Job::Fn done)
{
    auto job = std::make_shared<Job> (title, std::move (work), std::move (done));

    {
        const juce::ScopedLock sl (lock);
        jobs.push_back (job);
    }
    sendChangeMessage();

    juce::WeakReference<JobManager> weakThis (this);

    pool.addJob ([job, weakThis]
    {
        try
        {
            if (! job->isCancelled())
                job->work (*job);
        }
        catch (const std::exception& e)
        {
            job->fail (juce::String ("Unexpected error: ") + e.what());
        }
        catch (...)
        {
            job->fail ("Unexpected error");
        }

        job->finished = true;

        juce::MessageManager::callAsync ([job, weakThis]
        {
            if (auto* self = weakThis.get())
            {
                // Only run completion handlers while the owner (and its session) still exists.
                if (job->done)
                    job->done (*job);

                {
                    const juce::ScopedLock sl (self->lock);
                    self->jobs.erase (std::remove (self->jobs.begin(), self->jobs.end(), job), self->jobs.end());
                }
                if (self->onJobFinished)
                    self->onJobFinished (*job);
                self->sendChangeMessage();
            }
        });
    });

    return job;
}

std::vector<Job::Ptr> JobManager::getActiveJobs() const
{
    const juce::ScopedLock sl (lock);
    return jobs;
}

bool JobManager::isBusy() const
{
    const juce::ScopedLock sl (lock);
    return ! jobs.empty();
}

void JobManager::cancelAll()
{
    const juce::ScopedLock sl (lock);
    for (auto& j : jobs)
        j->cancel();
}

} // namespace snag
