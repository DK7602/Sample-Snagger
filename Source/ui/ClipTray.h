#pragma once

#include "Page.h"

namespace snag
{

class ClipCard;

//==============================================================================
/** The session tray along the bottom: every capture, import, recording and stem.
    Click to select, double-click to edit, drag a card straight into your DAW. */
class ClipTray : public juce::Component, private juce::ChangeListener
{
public:
    explicit ClipTray (EditorContext&);
    ~ClipTray() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    EditorContext& getContext()      { return ctx; }
    SnaggerProcessor& getProcessor() { return proc; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();

    EditorContext& ctx;
    SnaggerProcessor& proc;

    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<ClipCard> cards;
    IconButton importBtn { "import", theme::icons::plus(), "IMPORT", "gold" };
    juce::TextButton clearBtn { "CLEAR" };
    juce::StringArray shownIds;
};

} // namespace snag
