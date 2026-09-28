#pragma once

#include "Page.h"
#include "WaveformView.h"

namespace snag
{

//==============================================================================
/** Row of MIDI pads, one per chop. Click to play, drag a pad into your DAW. */
class SlicePads : public juce::Component, private juce::Timer
{
public:
    explicit SlicePads (SnaggerProcessor&);
    void setClip (Clip::Ptr c)  { clip = c; repaint(); }
    std::function<void (int sliceIndex)> onSliceSelected;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    int padAt (juce::Point<int>) const;
    juce::Rectangle<float> padBounds (int index, int count) const;
    int numPads() const;

    SnaggerProcessor& proc;
    Clip::Ptr clip;
    int pressed = -1, lit = -1;
    juce::uint32 litUntil = 0, lastCounter = 0;
    bool dragStarted = false;
};

//==============================================================================
class StudioPage : public juce::Component, private juce::ChangeListener, private juce::Timer
{
    friend struct StudioPageTester;
public:
    explicit StudioPage (EditorContext&);
    ~StudioPage() override;

    void setClip (Clip::Ptr);
    Clip::Ptr getClip() const { return clip; }

    bool handleKey (const juce::KeyPress&);

    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Rectangle<int> headGlass, readoutGlass;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshInfo();
    void syncSampler();

    enum class SliceFix { keep, crop, remove };
    void applyEdit (const juce::String& label, bool needsSelection, SliceFix fix,
                    std::function<AudioData::Ptr (const AudioData&, int start, int end)> op);
    void applyEditRange (const juce::String& label, int start, int end, SliceFix fix,
                         std::function<AudioData::Ptr (const AudioData&, int start, int end)> op);

    // PITCH & TIME / TONE knobs are live: every move re-renders the original with all of them
    Clip::Adjust knobsToAdjust() const;
    void setKnobsFrom (const Clip::Adjust&);
    void knobsChanged();
    void startAdjustRender();
    void adjustRendered (AudioData::Ptr oldAudio);   // UI follow-up once a render landed
    bool adjustJobRunning = false, adjustPending = false;
    juce::uint32 adjustSerial = 0, lastAdjustUndoMs = 0;

    void play();
    void stop();
    void undo();
    void redo();
    void detectBpm();
    void matchBpm();
    void rename();

    int selStartOrZero() const;
    int selEndOrAll() const;

    EditorContext& ctx;
    SnaggerProcessor& proc;
    Clip::Ptr clip;

    // header row
    juce::Label nameLabel, infoLabel, bpmLabel;
    juce::TextButton detectBpmBtn { "DETECT" };
    IconButton undoBtn { "undo", theme::icons::undo(), {}, "icon" };
    IconButton redoBtn { "redo", theme::icons::redo(), {}, "icon" };

    WaveformView wave;
    WaveOverview overview { wave };

    // transport row
    IconButton playBtn { "play", theme::icons::play(), "PLAY", "gold" };
    IconButton stopBtn { "stop", theme::icons::stop(), {}, "gold" };
    IconButton loopBtn { "loop", theme::icons::loop(), {}, "gold" };
    juce::Label selLabel;
    IconButton zoomInBtn  { "zoomin",  theme::icons::plus(),  {}, "icon" };
    IconButton zoomOutBtn { "zoomout", theme::icons::minus(), {}, "icon" };
    juce::TextButton fitBtn { "FIT" };
    IconButton saveBtn { "save", theme::icons::save(), "SAVE", "gold" };
    DragHandle dragHandle;

    SlicePads pads;

    // tool panels
    Panel editPanel { "Edit" }, pitchPanel { "Pitch & Time" }, tonePanel { "Tone" }, chopPanel { "Chop & Play" };
    juce::OwnedArray<juce::TextButton> editButtons;

    Knob pitchKnob   { "Pitch",  -24.0, 24.0, 0.0, 0.1, " st" };
    Knob stretchKnob { "Length", 50.0, 200.0, 100.0, 1.0, "%" };
    Knob bpmKnob     { "Target BPM", 60.0, 200.0, 120.0, 0.5 };
    juce::ToggleButton formantToggle { "Formants" }, tapeToggle { "Tape" };
    juce::TextButton pitchDefaultBtn { "DEFAULT" }, matchBpmBtn { "MATCH BPM" };

    Knob gainKnob    { "Gain",     -24.0, 24.0, 0.0, 0.5, " dB" };
    Knob lowCutKnob  { "Low cut",  0.0, 1000.0, 0.0, 1.0, " Hz" };
    Knob highCutKnob { "High cut", 1000.0, 20000.0, 20000.0, 10.0, " Hz" };
    juce::TextButton toneDefaultBtn { "DEFAULT" };

    Knob sensKnob    { "Sensitivity", 0.0, 100.0, 55.0, 1.0, "%" };
    juce::TextButton autoChopBtn { "AUTO CHOP" }, equalBtn { "EQUAL" }, clearChopsBtn { "CLEAR" };
    juce::ComboBox equalCount, midiModeBox;
    juce::ToggleButton oneShotToggle { "One-shot" };

};

} // namespace snag
