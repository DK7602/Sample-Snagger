#pragma once

#include "Page.h"
#include "../core/AudioToMidi.h"
#include <atomic>
#include <memory>

class SnaggerProcessor;

namespace snag
{

/** Shared by the decks: called before a change (so STUDIO can make one undo step per knob grab),
    and after it (to update the sampler / session). */
struct DeckCallbacks
{
    std::function<void (const juce::String& label, bool newGesture)> beforeChange;
    std::function<void()> afterChange;
};

//==============================================================================
/** PADS: the sound of the selected chop - gain, pitch, reverse, attack, release, filter. */
class PadDeck : public juce::Component, public DeckCallbacks
{
public:
    PadDeck();
    void setClip (Clip::Ptr, int padIndex);
    int getPad() const noexcept { return pad; }

    void resized() override;
    void paint (juce::Graphics&) override;

    Panel panel { "Pad" };
    Knob gainKnob    { "Gain", -24.0, 12.0, 0.0, 0.5, " dB" };
    Knob pitchKnob   { "Pitch", -24.0, 24.0, 0.0, 1.0, " st" };
    Knob attackKnob  { "Attack", 0.0, 2000.0, 1.0, 1.0, " ms" };
    Knob releaseKnob { "Release", 1.0, 4000.0, 10.0, 1.0, " ms" };
    Knob filterKnob  { "Filter", -1.0, 1.0, 0.0, 0.01 };
    juce::ToggleButton reverseToggle { "Reverse" };
    juce::TextButton resetBtn { "DEFAULT" }, allBtn { "COPY TO ALL PADS" };

private:
    void change (const juce::String& label);
    void refresh();
    int numPads() const;

    Clip::Ptr clip;
    int pad = 0;
    bool fresh = true;
};

//==============================================================================
/** FX: lo-fi / vinyl, drive, tempo-synced delay and reverb for the pads and STUDIO playback. */
class FxDeck : public juce::Component, public DeckCallbacks
{
public:
    FxDeck();
    void setClip (Clip::Ptr);

    void resized() override;
    void paint (juce::Graphics&) override;

    Panel panel { "FX Rack" };
    juce::ToggleButton lofiOn { "LO-FI" }, driveOn { "DRIVE" }, delayOn { "DELAY" }, reverbOn { "REVERB" };
    Knob bitsKnob     { "Bits", 4.0, 16.0, 10.0, 1.0 };
    Knob rateKnob     { "Rate", 2.0, 44.0, 18.0, 0.1, " kHz" };
    Knob vinylKnob    { "Vinyl", 0.0, 100.0, 50.0, 1.0, "%" };
    Knob driveKnob    { "Drive", 0.0, 100.0, 40.0, 1.0, "%" };
    Knob toneKnob     { "Tone", 0.0, 100.0, 60.0, 1.0, "%" };
    Knob timeKnob     { "Time", 0.0, 11.0, 6.0, 1.0 };
    Knob feedbackKnob { "Feedback", 0.0, 90.0, 35.0, 1.0, "%" };
    Knob delayMixKnob { "Mix", 0.0, 100.0, 30.0, 1.0, "%" };
    Knob sizeKnob     { "Size", 0.0, 100.0, 55.0, 1.0, "%" };
    Knob reverbMixKnob { "Mix", 0.0, 100.0, 25.0, 1.0, "%" };
    juce::TextButton defaultBtn { "ALL OFF" };

private:
    void change (const juce::String& label);
    void refresh();
    FxSettings read() const;

    Clip::Ptr clip;
    bool fresh = true;
    juce::Rectangle<int> sections[4];
};

//==============================================================================
/** Notes as bars on a little keyboard grid. */
class PianoRoll : public juce::Component
{
public:
    void setNotes (const std::vector<midi::Note>& n, double lengthSeconds) { notes = n; length = lengthSeconds; repaint(); }
    void setMessage (const juce::String& m) { message = m; repaint(); }
    void setPlayhead (double seconds) { if (std::abs (seconds - playhead) > 0.004) { playhead = seconds; repaint(); } }
    void setBusy (float progress) { busy = progress; repaint(); }
    void paint (juce::Graphics&) override;

private:
    std::vector<midi::Note> notes;
    double length = 1.0, playhead = -1.0;
    float busy = -1.0f;
    juce::String message;
};

/** MIDI: turn a melody or chords into MIDI (Spotify's Basic Pitch, built in). */
class MidiDeck : public juce::Component, private juce::Timer
{
public:
    explicit MidiDeck (EditorContext&);
    ~MidiDeck() override;

    void setClip (Clip::Ptr);
    /** The part to transcribe (selection), in samples; end < 0 = the whole sample. */
    void setRange (int start, int end);

    void findNotes();
    const std::vector<midi::Note>& getNotes() const noexcept { return notes; }
    bool isAnalysing() const noexcept { return analysing; }

    void resized() override;
    void paint (juce::Graphics&) override;

    Panel panel { "Audio to MIDI" };
    juce::ComboBox modeBox;
    Knob sensKnob   { "Sensitivity", 0.0, 100.0, 50.0, 1.0, "%" };
    Knob minLenKnob { "Min note", 30.0, 500.0, 128.0, 1.0, " ms" };
    juce::TextButton findBtn { "FIND NOTES" };
    IconButton listenBtn { "listen", theme::icons::play(), "LISTEN", "gold" };
    juce::TextButton saveBtn { "SAVE .MID" };
    DragHandle dragMidi { "DRAG MIDI TO DAW" };
    PianoRoll roll;
    juce::Label status;

private:
    void timerCallback() override;
    void renote();
    void listen();
    juce::String rangeKey() const;
    AudioData::Ptr source() const;

    EditorContext& ctx;
    SnaggerProcessor& proc;
    Clip::Ptr clip;
    int rangeStart = 0, rangeEnd = -1;

    midi::Posteriors post;
    juce::String postKey;           // which clip / range / edit the analysis belongs to
    std::vector<midi::Note> notes;
    bool analysing = false;
    std::shared_ptr<std::atomic<float>> progress = std::make_shared<std::atomic<float>> (0.0f);
    AudioData::Ptr heard;           // the LISTEN rendering
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace snag
