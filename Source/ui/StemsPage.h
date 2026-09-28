#pragma once

#include "Page.h"

namespace snag
{

class StemLane;

//==============================================================================
/** Dropdown with tick boxes: any mix of vocals, music, drums, bass, guitar, piano, other,
    plus a few presets. The menu stays open while you tick parts. */
class PartsPicker : public juce::Button
{
public:
    PartsPicker();
    juce::StringArray parts { "vocals", "music" };
    std::function<void()> onChange;
    juce::String summary() const;
    void setParts (const juce::StringArray&);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void clicked() override;

private:
    void showMenu();
};

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
    void toggleStem (Clip::Ptr);                    // play from the cursor, or stop if it's already playing
    bool isStemPlaying (const Clip&) const;
    void seekTo (double sample);                    // move the cursor; jumps there if something is playing
    double getCursor() const { return cursor; }     // start / seek position, in source samples
    SnaggerProcessor& getProcessor() { return proc; }
    EditorContext& getContext()      { return ctx; }
    const juce::OwnedArray<StemLane>& getLanes() const { return lanes; }
    bool isMixPlaying = false;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuild();
    void separateNow();
    void toggleMix();
    void startMix();
    void updatePlayButtons();
    Clip::Ptr sourceClip() const;
    double cursor = 0.0;

    EditorContext& ctx;
    SnaggerProcessor& proc;

    Panel topPanel;
    juce::Label sourceLabel, sourceInfo, engineNote;
    juce::ComboBox engineBox;
    PartsPicker partsPicker;
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
