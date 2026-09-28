#pragma once

#include "Page.h"

namespace snag
{

class StemLane;

//==============================================================================
/** Separate the selected sample into vocals / music (or drums, bass, other...),
    audition them solo / muted, then edit, save or drag any stem into your DAW. */
class StemsPage : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit StemsPage (EditorContext&);
    ~StemsPage() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    void updateGains();
    void playStem (Clip::Ptr);
    SnaggerProcessor& getProcessor() { return proc; }
    EditorContext& getContext()      { return ctx; }
    const juce::OwnedArray<StemLane>& getLanes() const { return lanes; }
    bool isMixPlaying = false;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuild();
    void separateNow();
    void playMix();
    Clip::Ptr sourceClip() const;

    EditorContext& ctx;
    SnaggerProcessor& proc;

    Panel topPanel;
    juce::Label sourceLabel, sourceInfo, engineNote;
    juce::ComboBox engineBox, stemsBox;
    IconButton separateBtn { "separate", theme::icons::scissors(), "SEPARATE", "redFill" };

    juce::OwnedArray<StemLane> lanes;
    juce::Viewport laneViewport;
    juce::Component laneHolder;

    IconButton playMixBtn { "playmix", theme::icons::play(), "PLAY MIX", "gold" };
    IconButton stopBtn    { "stop", theme::icons::stop(), {}, "gold" };
    IconButton saveAllBtn { "saveall", theme::icons::save(), "SAVE ALL STEMS", "gold" };
    DragHandle dragMix { "DRAG MIX OF UNMUTED STEMS" };

    Clip::Ptr shownParent;
    juce::StringArray shownStemIds;
    juce::Rectangle<int> emptyArea;
};

} // namespace snag
