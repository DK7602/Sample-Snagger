#pragma once

#include "Theme.h"
#include "../core/AudioData.h"
#include <functional>

namespace snag
{

inline void setStyle (juce::Component& c, const juce::String& style) { c.getProperties().set ("style", style); }

//==============================================================================
/** Button with a vector icon and optional caption. */
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, juce::Path iconPath, const juce::String& caption = {},
                const juce::String& styleName = "gold");

    void setIcon (juce::Path p)                 { icon = std::move (p); repaint(); }
    void setCaption (const juce::String& c)     { caption = c; repaint(); }
    void setIconColour (juce::Colour c)         { iconColour = c; hasIconColour = true; repaint(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Path icon;
    juce::String caption;
    juce::Colour iconColour;
    bool hasIconColour = false;
};

//==============================================================================
/** Rotary knob with a caption underneath and a value readout. */
class Knob : public juce::Component
{
public:
    Knob (const juce::String& caption, double min, double max, double def, double step = 0.0,
          const juce::String& suffix = {});

    juce::Slider slider;
    std::function<juce::String (double)> formatter;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String caption, suffix;
};

//==============================================================================
/** Titled glossy panel used to group tools. */
class Panel : public juce::Component
{
public:
    explicit Panel (const juce::String& t = {}) : title (t) { setInterceptsMouseClicks (false, true); }
    void setTitle (const juce::String& t) { title = t; repaint(); }
    juce::Rectangle<int> getContentBounds() const;
    void paint (juce::Graphics&) override;
    bool goldEdge = false;

private:
    juce::String title;
};

//==============================================================================
class LevelMeter : public juce::Component
{
public:
    void setLevel (float l) { if (std::abs (l - level) > 0.002f) { level = l; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    float level = 0.0f;
};

//==============================================================================
/** Anything you can drag out of the plug-in into your DAW (or Finder / Explorer).
    makeFile is called on mouse-down-drag and must return a WAV file on disk. */
class DragHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DragHandle (const juce::String& caption = "DRAG TO DAW");

    std::function<juce::File()> makeFile;
    void setCaption (const juce::String& c) { caption = c; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Starts an OS drag of a file; returns false if not possible. */
    static bool startExternalDrag (juce::Component& source, const juce::File& file);

private:
    juce::String caption;
    bool dragging = false, pressed = false;
};

//==============================================================================
/** Cached waveform thumbnail image for small views. */
class WaveThumb
{
public:
    /** normalise = scale the drawing so quiet material (stems, room tone) is still readable. */
    void draw (juce::Graphics&, juce::Rectangle<float> r, const AudioData* audio,
               juce::Colour colour, int start = 0, int end = -1, bool normalise = false);
private:
    juce::Image cache;
    const AudioData* cachedFor = nullptr;
    juce::Colour cachedColour;
    int cachedStart = -1, cachedEnd = -1;
    juce::Rectangle<int> cachedSize;
};

void drawWaveform (juce::Graphics&, juce::Rectangle<float> r, const AudioData& audio, int start, int end,
                   juce::Colour colour, bool glossy = true, float displayGain = 1.0f);

//==============================================================================
/** Brief notifications in the bottom-right corner. */
class ToastOverlay : public juce::Component, private juce::Timer
{
public:
    ToastOverlay() { setInterceptsMouseClicks (false, false); }
    void show (const juce::String& text, bool isError = false, int ms = 4000);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    struct Toast { juce::String text; bool error; juce::uint32 until; };
    std::vector<Toast> toasts;
};

//==============================================================================
/** Small spinning ring for "busy". */
void drawSpinner (juce::Graphics&, juce::Rectangle<float> r, juce::Colour c);

} // namespace snag
