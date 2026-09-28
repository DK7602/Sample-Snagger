#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace snag::theme
{
    // ---- palette: gloss black, gold, neon red -----------------------------------------
    namespace col
    {
        const juce::Colour bg0       { 0xff060607 };
        const juce::Colour bg1       { 0xff0c0c0f };
        const juce::Colour bg2       { 0xff131318 };
        const juce::Colour bg3       { 0xff1c1c23 };
        const juce::Colour bg4       { 0xff26262f };
        const juce::Colour line      { 0xff2a2a33 };
        const juce::Colour lineSoft  { 0xff1f1f27 };

        const juce::Colour gold      { 0xffd4af37 };
        const juce::Colour goldLight { 0xfff6dc8a };
        const juce::Colour goldPale  { 0xffe9d8a6 };
        const juce::Colour goldDark  { 0xff8c6b1f };
        const juce::Colour goldDeep  { 0xff4a3a12 };

        const juce::Colour red       { 0xffff1f3d };
        const juce::Colour redHot    { 0xffff5a70 };
        const juce::Colour redDeep   { 0xff7a0c1a };

        const juce::Colour text      { 0xffede6d6 };
        const juce::Colour textDim   { 0xff9a927f };
        const juce::Colour textFaint { 0xff5c5749 };

        const juce::Colour ok        { 0xff7fd18b };
    }

    /** Stem colours (all tuned to sit well next to gold). */
    juce::Colour stemColour (const juce::String& stemName);

    // ---- fonts ---------------------------------------------------------------------------
    juce::Font display (float height, bool extraBold = false);   // Cinzel - logo, headings
    juce::Font ui (float height, bool bold = false);             // Montserrat - everything else
    juce::Font mono (float height);

    // ---- drawing helpers --------------------------------------------------------------
    juce::ColourGradient goldGradient (juce::Rectangle<float> r, bool vertical = true);

    /** Glossy black panel: gradient body, glass highlight on top, fine border. */
    void glossPanel (juce::Graphics&, juce::Rectangle<float> r, float corner,
                     juce::Colour top = col::bg3, juce::Colour bottom = col::bg1,
                     bool goldEdge = false, float highlight = 0.06f);

    // ---- materials -----------------------------------------------------------------------
    /** The brushed 24k gold faceplate texture, scaled to cover `area`. */
    const juce::Image& goldPlateImage();

    /** Keeps a copy of the gold plate pre-scaled to a component's size, so repaints are a plain blit. */
    class GoldPlate
    {
    public:
        void draw (juce::Graphics&, juce::Rectangle<int> area);
    private:
        juce::Image cache;
        juce::Rectangle<int> cachedArea;
        float cachedScale = 0.0f;
    };

    /** A window of polished black glass set into the gold plate (bevelled cut, reflections, glare).
        `rim` draws the cut in the surrounding gold (needs ~3 px of space around r). */
    void glassWindow (juce::Graphics&, juce::Rectangle<float> r, float corner, float glare = 1.0f, bool rim = true);

    /** Just the reflection on the glass - for drawing over a window's content. */
    void glassGlare (juce::Graphics&, juce::Rectangle<float> r, float corner, float strength = 1.0f);

    /** Dark well inside a glass window (for rows and cards that sit inside one). */
    void glassWell (juce::Graphics&, juce::Rectangle<float> r, float corner, bool highlighted = false);

    /** Satin black cap for buttons and pads: soft sheen, fine grain, bevelled edge, soft shadow. */
    void satinSurface (juce::Graphics&, juce::Rectangle<float> r, float corner, bool highlighted, bool down,
                       bool shadow = true);

    /** Text engraved into the gold plate. */
    void engravedText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r,
                       const juce::Font& f, juce::Justification j = juce::Justification::centredLeft);

    /** Neon-lit text or icon (red LEDs behind satin black). */
    void glowText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
                   juce::Justification j, juce::Colour c = col::red, float strength = 1.0f);
    void glowPath (juce::Graphics&, const juce::Path& p, juce::Colour c = col::red, float strength = 1.0f);

    /** Legend (text / icon) colour of a styled button, and whether it is lit neon. */
    struct Legend { juce::Colour colour; bool glow; };
    Legend buttonLegend (const juce::Button&, bool highlighted);

    /** Small indicator LED. */
    void led (juce::Graphics&, juce::Point<float> centre, float radius, bool lit, juce::Colour c = col::red);

    void goldBorder (juce::Graphics&, juce::Rectangle<float> r, float corner, float thickness = 1.0f, float alpha = 1.0f);

    /** Soft neon bloom around a path (drawn *before* the crisp stroke / fill). */
    void neonGlow (juce::Graphics&, const juce::Path& p, juce::Colour c, float radius = 10.0f, float strength = 0.8f);
    void neonLine (juce::Graphics&, juce::Line<float> l, juce::Colour c, float thickness = 1.5f, float glow = 8.0f);

    /** Gold text with a metallic gradient. */
    void goldText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r,
                   const juce::Font& f, juce::Justification j = juce::Justification::centredLeft);

    void sectionLabel (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r);

    /** Brand mark: gold ring + S + neon red dot. */
    void drawLogoMark (juce::Graphics&, juce::Rectangle<float> r);

    juce::String formatTime (double seconds, bool withMillis = false);
    juce::String noteName (int midiNote);   // 60 -> "C3"

    // ---- icons (drawn as paths so they stay crisp at any size) --------------------------
    namespace icons
    {
        juce::Path play();
        juce::Path stop();
        juce::Path record();
        juce::Path loop();
        juce::Path back();
        juce::Path forward();
        juce::Path reload();
        juce::Path home();
        juce::Path gear();
        juce::Path drag();
        juce::Path download();
        juce::Path folder();
        juce::Path undo();
        juce::Path redo();
        juce::Path close();
        juce::Path plus();
        juce::Path minus();
        juce::Path scissors();
        juce::Path save();
        juce::Path wave();
        juce::Path mic();
        juce::Path external();
    }
}

namespace snag
{

//==============================================================================
class SnaggerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SnaggerLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;

    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool isVertical,
                        int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 8; }

    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int w, int h, double progress, const juce::String&) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
};

} // namespace snag
