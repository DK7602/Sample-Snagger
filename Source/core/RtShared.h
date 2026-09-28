#pragma once

#include <juce_core/juce_core.h>

namespace snag
{

/** Publishes reference-counted, immutable objects from the message thread to the audio thread
    without blocking the audio thread and without ever freeing memory on it.

    - publish() (message thread) swaps the current object and remembers it in a release pool.
    - read() (audio thread) try-locks and returns the latest object (or the last one it saw).
    - collectGarbage() (message thread) frees pooled objects nobody else references. */
template <class ObjectType>
class RtShared
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<ObjectType>;

    void publish (Ptr p)
    {
        if (p != nullptr)
            pool.add (p);

        {
            const juce::SpinLock::ScopedLockType sl (lock);
            current = p;
        }
        collectGarbage();
    }

    /** Audio thread. */
    Ptr read() noexcept
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (sl.isLocked())
            lastSeen = current;
        return lastSeen;
    }

    /** Message thread: the object that was last published. */
    Ptr getPublished() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return current;
    }

    void collectGarbage()
    {
        for (int i = pool.size(); --i >= 0;)
            if (pool.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
                pool.remove (i);
    }

private:
    mutable juce::SpinLock lock;
    Ptr current, lastSeen;
    juce::ReferenceCountedArray<ObjectType> pool;
};

} // namespace snag
