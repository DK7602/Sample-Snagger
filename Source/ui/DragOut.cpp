#include "DragOut.h"

namespace snag
{

DragOut::DragOut() : shared (std::make_shared<Shared>()) {}

void DragOut::pressed()
{
    shared->mouseIsDown = true;
    shared->state = State::idle;
    shared->waitingFor = nullptr;
}

void DragOut::released()
{
    shared->mouseIsDown = false;
    shared->state = State::idle;
    shared->waitingFor = nullptr;   // (a job that is still running finishes; its file is kept for the next drag)
}

bool DragOut::isPreparing() const   { return shared->state == State::preparing; }

void DragOut::drag (SnaggerProcessor& p, juce::Component& source, const std::function<actions::DragJob()>& describe)
{
    auto& s = *shared;
    if (s.state == State::done)
        return;   // one drag per press

    if (s.state == State::preparing)
    {
        if (s.waitingFor != nullptr && s.waitingFor->ready)
        {
            const auto files = s.waitingFor->files;
            s.waitingFor = nullptr;
            s.state = State::done;
            DragHandle::startExternalDrag (source, files);
        }
        return;
    }

    const auto job = describe ? describe() : actions::DragJob();
    if (! job.isValid())
    {
        s.state = State::done;
        return;
    }

    auto allThere = [] (const juce::StringArray& files)
    {
        for (auto& f : files)
            if (! juce::File (f).existsAsFile())
                return false;
        return ! files.isEmpty();
    };

    if (job.key == s.lastKey && allThere (s.lastFiles))
    {
        s.state = State::done;
        DragHandle::startExternalDrag (source, s.lastFiles);
        return;
    }

    if (isQuick (job))
    {
        s.state = State::done;
        DragHandle::startExternalDrag (source, actions::makeDragFiles (job));
        return;
    }

    s.state = State::preparing;
    auto prepared = std::make_shared<Prepared>();
    s.waitingFor = prepared;
    actions::notify (p, "Getting the file ready - keep the mouse button held and the drag starts when it's there...");

    std::weak_ptr<Shared> weak (shared);
    auto* processor = &p;
    p.jobs.start ("Preparing the drag", [prepared, job] (Job& j)
    {
        j.setStatus ("Rendering " + job.name);
        prepared->files = actions::makeDragFiles (job);
    },
    [weak, prepared, job, processor] (Job& j)
    {
        auto state = weak.lock();
        const bool made = ! j.isCancelled() && ! prepared->files.isEmpty();
        if (! made)
        {
            if (state != nullptr && state->waitingFor == prepared)
            {
                state->waitingFor = nullptr;
                state->state = State::done;
            }
            if (! j.isCancelled())
                actions::notify (*processor, "Couldn't write the file for the drag - is the disk full?", true);
            return;
        }
        if (state == nullptr)
            return;
        state->lastKey = job.key;
        state->lastFiles = prepared->files;
        state->keepsAudioAlive = job;
        prepared->ready = true;
        if (state->waitingFor != prepared)   // the button was let go in the meantime
            actions::notify (*processor, "The file is ready - drag again and it starts at once.");
        else
            actions::notify (*processor, "Ready - move the mouse to drag it out.");
    });
}

void DragOut::attachTo (DragHandle& handle, SnaggerProcessor& p, std::function<actions::DragJob()> describe)
{
    handle.onPressChange = [this] (bool down) { if (down) pressed(); else released(); };
    handle.onDragging = [this, &handle, &p, describe = std::move (describe)] { drag (p, handle, describe); };
}

} // namespace snag
