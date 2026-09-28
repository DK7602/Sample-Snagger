#pragma once

#include "Widgets.h"
#include "../core/AudioData.h"

class SnaggerProcessor;

namespace snag
{

//==============================================================================
/** The big editable waveform: zoom, scroll, select, chop markers, playhead. */
class WaveformView : public juce::Component, private juce::Timer
{
public:
    explicit WaveformView (SnaggerProcessor&);

    void setClip (Clip::Ptr clip);
    Clip::Ptr getClip() const                     { return clip; }
    void audioChanged();                          // clip audio replaced (edit / undo)

    // selection, in samples
    bool hasSelection() const noexcept            { return selEnd - selStart > 16; }
    int getSelectionStart() const noexcept        { return selStart; }
    int getSelectionEnd() const noexcept          { return selEnd; }
    void setSelection (int start, int end);
    void clearSelection();
    int getCursor() const noexcept                { return cursor; }

    // view
    void zoomBy (double factor, double anchorSample);
    void zoomToFit();
    void zoomToSelection();
    double getViewStart() const noexcept          { return viewStart; }
    double getViewLength() const noexcept         { return viewLen; }
    void setViewStart (double s);

    std::function<void()> onSelectionChanged, onSlicesChanged, onViewChanged;
    std::function<void (int fromSample)> onPlayFrom;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    juce::Rectangle<float> getWaveArea() const;
    void paintOverChildren (juce::Graphics&) override;

private:
    void timerCallback() override;
    double xToSample (float x) const;
    float sampleToX (double s) const;
    int markerAt (float x) const;       // index into clip->slices or -1
    void clampView();
    int numSamples() const              { return clip != nullptr && clip->audio != nullptr ? clip->audio->getNumSamples() : 0; }

    SnaggerProcessor& processor;
    Clip::Ptr clip;
    AudioData* lastAudio = nullptr;

    double viewStart = 0, viewLen = 1;
    int selStart = 0, selEnd = 0, cursor = 0;

    enum class Drag { none, select, marker, selStartEdge, selEndEdge, scrub };
    Drag drag = Drag::none;
    int dragMarker = -1, anchor = 0;
    float hoverX = -1.0f;
    int hoverMarker = -1;

    juce::Image waveCache;
    struct CacheKey { const void* audio = nullptr; double start = -1, len = -1; int w = 0, h = 0; } cacheKey;

    double lastPlayhead = -1.0;
};

//==============================================================================
/** Whole-clip overview strip with the visible range; drag to scroll. */
class WaveOverview : public juce::Component
{
public:
    explicit WaveOverview (WaveformView& v) : view (v) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    WaveformView& view;
    WaveThumb thumb;
    double grabOffset = 0;
};

} // namespace snag
