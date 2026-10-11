#pragma once

#include "Widgets.h"
#include "../Actions.h"
#include <memory>

namespace snag
{

//==============================================================================
/** Starts the drag of a sound out of the window, into a DAW.

    A short sound is rendered and written on the spot, as the mouse starts to move. A long one (a
    whole song with the FX rack on it) took seconds, with the window standing still: that one is
    made by a background job, and the drag starts once the file is there - if the mouse button is
    still held. If it was let go, the file is kept, and dragging the same thing again starts at once. */
class DragOut
{
public:
    DragOut();

    /** Call from mouseDown and mouseUp. */
    void pressed();
    void released();

    /** Call from mouseDrag once the mouse has moved far enough. `describe` is only asked when a
        file has to be made. */
    void drag (SnaggerProcessor&, juce::Component& source, const std::function<actions::DragJob()>& describe);

    bool isPreparing() const;

    /** Wires a DragHandle up to this. */
    void attachTo (DragHandle&, SnaggerProcessor&, std::function<actions::DragJob()> describe);

    /** What starts the drag once the files are there: the system's drag and drop, unless something
        else is put here (the tests do - a real drag needs a mouse, and blocks until it is let go). */
    std::function<void (juce::Component& source, const juce::StringArray& files)> startDrag;

    /** Worth a background job? (More than a minute of audio.) */
    static bool isQuick (const actions::DragJob& j) noexcept   { return j.samples <= 60 * 48000; }

private:
    struct Prepared { bool ready = false; juce::StringArray files; };
    enum class State { idle, preparing, done };
    struct Shared
    {
        State state = State::idle;
        bool mouseIsDown = false;
        std::shared_ptr<Prepared> waitingFor;
        // the last file a job made
        juce::String lastKey;
        juce::StringArray lastFiles;
        actions::DragJob keepsAudioAlive;   // (so the key's addresses can't come to mean other audio)
    };
    std::shared_ptr<Shared> shared;   // (the job's callback can outlive the component this belongs to)
    void begin (juce::Component& source, const juce::StringArray& files) const;
};

} // namespace snag
