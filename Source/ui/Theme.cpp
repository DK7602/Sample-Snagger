#include "Theme.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "SnaggerBinaryData.h"

namespace snag::theme
{

//==============================================================================
namespace
{
    struct FontSet
    {
        FontSet()
        {
            cinzelSemi   = juce::Typeface::createSystemTypefaceFor (SnaggerBinary::CinzelSemiBold_ttf,   (size_t) SnaggerBinary::CinzelSemiBold_ttfSize);
            cinzelBold   = juce::Typeface::createSystemTypefaceFor (SnaggerBinary::CinzelExtraBold_ttf,  (size_t) SnaggerBinary::CinzelExtraBold_ttfSize);
            montMedium   = juce::Typeface::createSystemTypefaceFor (SnaggerBinary::MontserratMedium_ttf, (size_t) SnaggerBinary::MontserratMedium_ttfSize);
            montBold     = juce::Typeface::createSystemTypefaceFor (SnaggerBinary::MontserratBold_ttf,   (size_t) SnaggerBinary::MontserratBold_ttfSize);
        }
        juce::Typeface::Ptr cinzelSemi, cinzelBold, montMedium, montBold;
    };

    FontSet& fonts()
    {
        static FontSet f;
        return f;
    }
}

juce::Font display (float h, bool extraBold)
{
    auto tf = extraBold ? fonts().cinzelBold : fonts().cinzelSemi;
    return juce::Font (juce::FontOptions (tf).withHeight (h));
}

juce::Font ui (float h, bool bold)
{
    auto tf = bold ? fonts().montBold : fonts().montMedium;
    return juce::Font (juce::FontOptions (tf).withHeight (h));
}

juce::Font mono (float h)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

juce::Colour stemColour (const juce::String& s)
{
    if (s == "vocals")                   return juce::Colour (0xffff2e4d);   // neon red
    if (s == "drums")                    return juce::Colour (0xfff2c14e);   // warm gold
    if (s == "bass")                     return juce::Colour (0xffc9853b);   // bronze
    if (s == "music" || s == "no_vocals")return juce::Colour (0xffe9d8a6);   // champagne
    if (s == "guitar")                   return juce::Colour (0xffd9a05b);
    if (s == "piano")                    return juce::Colour (0xfff7e7b4);
    if (s == "melodic")                  return juce::Colour (0xffe6c46a);
    return juce::Colour (0xffbfa76a);                                          // other: antique gold
}

//==============================================================================
juce::ColourGradient goldGradient (juce::Rectangle<float> r, bool vertical)
{
    juce::ColourGradient g (col::goldLight, r.getX(), r.getY(),
                            col::goldDark, vertical ? r.getX() : r.getRight(), vertical ? r.getBottom() : r.getY(), false);
    g.addColour (0.35, col::gold);
    g.addColour (0.55, col::goldLight.interpolatedWith (col::gold, 0.4f));
    g.addColour (0.80, col::gold.darker (0.15f));
    return g;
}

void glossPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner,
                 juce::Colour top, juce::Colour bottom, bool goldEdge, float highlight)
{
    g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);

    // glass highlight over the top ~45%
    auto glass = r.reduced (1.0f).withHeight (r.getHeight() * 0.46f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (highlight), glass.getX(), glass.getY(),
                                             juce::Colours::white.withAlpha (0.0f), glass.getX(), glass.getBottom(), false));
    g.fillRoundedRectangle (glass, juce::jmax (0.0f, corner - 1.0f));

    // top inner edge
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + corner, r.getRight() - corner);

    if (goldEdge)
        goldBorder (g, r, corner, 1.0f, 0.85f);
    else
    {
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }
}

void goldBorder (juce::Graphics& g, juce::Rectangle<float> r, float corner, float thickness, float alpha)
{
    auto grad = goldGradient (r);
    grad.multiplyOpacity (alpha);
    g.setGradientFill (grad);
    g.drawRoundedRectangle (r.reduced (thickness * 0.5f), corner, thickness);
}

void neonGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float radius, float strength)
{
    juce::DropShadow (c.withAlpha (juce::jlimit (0.0f, 1.0f, strength)), (int) radius, {}).drawForPath (g, p);
}

void neonLine (juce::Graphics& g, juce::Line<float> l, juce::Colour c, float thickness, float glow)
{
    juce::Path p;
    p.startNewSubPath (l.getStart());
    p.lineTo (l.getEnd());
    juce::Path stroked;
    juce::PathStrokeType (thickness + 1.5f).createStrokedPath (stroked, p);
    neonGlow (g, stroked, c, glow, 0.9f);
    g.setColour (c.brighter (0.25f));
    g.strokePath (p, juce::PathStrokeType (thickness));
}

void goldText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
               const juce::Font& f, juce::Justification j)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (f, text, r.getX(), r.getY(), r.getWidth(), r.getHeight(), j, 1, 1.0f);
    juce::Path p;
    ga.createPath (p);
    auto b = p.getBounds();
    // soft dark drop for depth
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillPath (p, juce::AffineTransform::translation (0.0f, 1.0f));
    g.setGradientFill (goldGradient (b));
    g.fillPath (p);
}

void sectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r)
{
    auto f = ui (10.5f, true).withExtraKerningFactor (0.18f);
    g.setFont (f);
    g.setColour (col::gold.withAlpha (0.9f));
    g.drawText (text.toUpperCase(), r, juce::Justification::centredLeft, false);
}

void drawLogoMark (juce::Graphics& g, juce::Rectangle<float> r)
{
    // The claw icon (gold grabber lifting a neon-red waveform slice out of a gold track)
    const auto logo = juce::ImageCache::getFromMemory (SnaggerBinary::logo_256_png,   // ImageCache keeps it decoded
                                                                      SnaggerBinary::logo_256_pngSize);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (logo, r.withSizeKeepingCentre (size, size), juce::RectanglePlacement::centred);
}

juce::String formatTime (double s, bool withMillis)
{
    if (s < 0) s = 0;
    const int mins = (int) (s / 60.0);
    const double secs = s - mins * 60.0;
    if (withMillis)
        return juce::String (mins) + ":" + juce::String (secs, 2).paddedLeft ('0', 5);
    return juce::String (mins) + ":" + juce::String ((int) secs).paddedLeft ('0', 2);
}

juce::String noteName (int midiNote)
{
    // Ableton / FL / Logic convention: middle C (60) = C3
    return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3);
}

//==============================================================================
namespace icons
{
    juce::Path play()
    {
        juce::Path p;
        p.addTriangle (0.0f, 0.0f, 0.0f, 1.0f, 0.88f, 0.5f);
        return p;
    }
    juce::Path stop()     { juce::Path p; p.addRoundedRectangle (0.05f, 0.05f, 0.9f, 0.9f, 0.08f); return p; }
    juce::Path record()   { juce::Path p; p.addEllipse (0.0f, 0.0f, 1.0f, 1.0f); return p; }

    static juce::Path strokedOf (const juce::Path& src, float w)
    {
        juce::Path out;
        juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (out, src);
        return out;
    }

    juce::Path loop()
    {
        juce::Path p;
        p.addCentredArc (0.5f, 0.5f, 0.38f, 0.38f, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.5f, true);
        auto s = strokedOf (p, 0.1f);
        s.addTriangle (0.62f, 0.0f, 0.62f, 0.36f, 0.9f, 0.18f);
        return s;
    }
    juce::Path back()
    {
        juce::Path p;
        p.startNewSubPath (0.65f, 0.1f); p.lineTo (0.25f, 0.5f); p.lineTo (0.65f, 0.9f);
        return strokedOf (p, 0.14f);
    }
    juce::Path forward()
    {
        juce::Path p;
        p.startNewSubPath (0.35f, 0.1f); p.lineTo (0.75f, 0.5f); p.lineTo (0.35f, 0.9f);
        return strokedOf (p, 0.14f);
    }
    juce::Path reload()
    {
        juce::Path p;
        p.addCentredArc (0.5f, 0.52f, 0.36f, 0.36f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.2f, true);
        auto s = strokedOf (p, 0.11f);
        s.addTriangle (0.44f, 0.0f, 0.44f, 0.34f, 0.72f, 0.17f);
        return s;
    }
    juce::Path home()
    {
        juce::Path p;
        p.startNewSubPath (0.08f, 0.5f); p.lineTo (0.5f, 0.1f); p.lineTo (0.92f, 0.5f);
        auto s = strokedOf (p, 0.11f);
        s.addRectangle (0.22f, 0.5f, 0.56f, 0.42f);
        return s;
    }
    juce::Path gear()
    {
        juce::Path p;
        const int teeth = 8;
        for (int i = 0; i < teeth * 2; ++i)
        {
            const float a = (float) i / (teeth * 2) * juce::MathConstants<float>::twoPi;
            const float r = (i % 2 == 0) ? 0.5f : 0.38f;
            const float a2 = a + juce::MathConstants<float>::twoPi / (teeth * 2);
            auto pt  = juce::Point<float> (0.5f + r * std::sin (a),  0.5f - r * std::cos (a));
            auto pt2 = juce::Point<float> (0.5f + r * std::sin (a2), 0.5f - r * std::cos (a2));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            p.lineTo (pt2);
        }
        p.closeSubPath();
        p.addEllipse (0.33f, 0.33f, 0.34f, 0.34f);
        p.setUsingNonZeroWinding (false);
        return p;
    }
    juce::Path drag()
    {
        juce::Path p;
        // 6-dot grip + arrow out
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 2; ++c)
                p.addEllipse (0.05f + c * 0.22f, 0.12f + r * 0.3f, 0.14f, 0.14f);
        juce::Path a;
        a.startNewSubPath (0.55f, 0.5f); a.lineTo (0.95f, 0.5f);
        auto s = strokedOf (a, 0.1f);
        s.addTriangle (0.8f, 0.3f, 1.0f, 0.5f, 0.8f, 0.7f);
        p.addPath (s);
        return p;
    }
    juce::Path download()
    {
        juce::Path a;
        a.startNewSubPath (0.5f, 0.05f); a.lineTo (0.5f, 0.6f);
        auto s = strokedOf (a, 0.12f);
        s.addTriangle (0.25f, 0.45f, 0.75f, 0.45f, 0.5f, 0.75f);
        juce::Path tray;
        tray.startNewSubPath (0.08f, 0.7f); tray.lineTo (0.08f, 0.95f); tray.lineTo (0.92f, 0.95f); tray.lineTo (0.92f, 0.7f);
        s.addPath (strokedOf (tray, 0.1f));
        return s;
    }
    juce::Path folder()
    {
        juce::Path p;
        p.startNewSubPath (0.05f, 0.2f); p.lineTo (0.4f, 0.2f); p.lineTo (0.5f, 0.32f); p.lineTo (0.95f, 0.32f);
        p.lineTo (0.95f, 0.88f); p.lineTo (0.05f, 0.88f); p.closeSubPath();
        return strokedOf (p, 0.09f);
    }
    juce::Path undo()
    {
        juce::Path p;
        p.addCentredArc (0.55f, 0.55f, 0.35f, 0.3f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        auto s = strokedOf (p, 0.11f);
        s.addTriangle (0.02f, 0.25f, 0.3f, 0.02f, 0.3f, 0.48f);
        return s;
    }
    juce::Path redo()
    {
        auto p = undo();
        p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 0.5f, 0.5f));
        return p;
    }
    juce::Path close()
    {
        juce::Path p;
        p.startNewSubPath (0.15f, 0.15f); p.lineTo (0.85f, 0.85f);
        p.startNewSubPath (0.85f, 0.15f); p.lineTo (0.15f, 0.85f);
        return strokedOf (p, 0.13f);
    }
    juce::Path plus()
    {
        juce::Path p;
        p.startNewSubPath (0.5f, 0.1f); p.lineTo (0.5f, 0.9f);
        p.startNewSubPath (0.1f, 0.5f); p.lineTo (0.9f, 0.5f);
        return strokedOf (p, 0.13f);
    }
    juce::Path minus()
    {
        juce::Path p;
        p.startNewSubPath (0.1f, 0.5f); p.lineTo (0.9f, 0.5f);
        return strokedOf (p, 0.13f);
    }
    juce::Path scissors()
    {
        juce::Path p;
        p.addEllipse (0.05f, 0.62f, 0.3f, 0.3f);
        p.addEllipse (0.65f, 0.62f, 0.3f, 0.3f);
        auto s = strokedOf (p, 0.08f);
        juce::Path b;
        b.startNewSubPath (0.3f, 0.66f); b.lineTo (0.75f, 0.05f);
        b.startNewSubPath (0.7f, 0.66f); b.lineTo (0.25f, 0.05f);
        s.addPath (strokedOf (b, 0.08f));
        return s;
    }
    juce::Path save()
    {
        juce::Path p;
        p.startNewSubPath (0.08f, 0.08f); p.lineTo (0.75f, 0.08f); p.lineTo (0.92f, 0.25f);
        p.lineTo (0.92f, 0.92f); p.lineTo (0.08f, 0.92f); p.closeSubPath();
        auto s = strokedOf (p, 0.09f);
        s.addRectangle (0.28f, 0.08f, 0.4f, 0.24f);
        s.addRectangle (0.25f, 0.58f, 0.5f, 0.1f);
        return s;
    }
    juce::Path wave()
    {
        juce::Path p;
        const float hs[] = { 0.2f, 0.5f, 0.9f, 0.6f, 0.35f, 0.75f, 0.45f, 0.25f };
        for (int i = 0; i < 8; ++i)
        {
            const float x = 0.06f + i * 0.125f;
            p.addRoundedRectangle (x, 0.5f - hs[i] * 0.5f, 0.07f, hs[i], 0.03f);
        }
        return p;
    }
    juce::Path mic()
    {
        juce::Path p;
        p.addRoundedRectangle (0.35f, 0.02f, 0.3f, 0.55f, 0.15f);
        juce::Path arc;
        arc.addCentredArc (0.5f, 0.4f, 0.28f, 0.28f, 0.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 1.5f, true);
        arc.startNewSubPath (0.5f, 0.68f); arc.lineTo (0.5f, 0.92f);
        arc.startNewSubPath (0.3f, 0.95f); arc.lineTo (0.7f, 0.95f);
        p.addPath (strokedOf (arc, 0.08f));
        return p;
    }
}

} // namespace snag::theme

//==============================================================================
namespace snag
{
using namespace theme;

SnaggerLookAndFeel::SnaggerLookAndFeel()
{
    auto scheme = LookAndFeel_V4::getDarkColourScheme();
    scheme.setUIColour (ColourScheme::windowBackground, col::bg1);
    scheme.setUIColour (ColourScheme::widgetBackground, col::bg2);
    scheme.setUIColour (ColourScheme::menuBackground, col::bg2);
    scheme.setUIColour (ColourScheme::outline, col::line);
    scheme.setUIColour (ColourScheme::defaultText, col::text);
    scheme.setUIColour (ColourScheme::defaultFill, col::gold);
    scheme.setUIColour (ColourScheme::highlightedText, col::bg0);
    scheme.setUIColour (ColourScheme::highlightedFill, col::gold);
    scheme.setUIColour (ColourScheme::menuText, col::text);
    setColourScheme (scheme);

    setColour (juce::TextButton::buttonColourId, col::bg3);
    setColour (juce::TextButton::textColourOffId, col::goldPale);
    setColour (juce::TextButton::textColourOnId, col::bg0);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextEditor::backgroundColourId, col::bg0);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, col::gold.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, col::text);
    setColour (juce::CaretComponent::caretColourId, col::gold);
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::TextEditor::focusedOutlineColourId, col::gold);
    setColour (juce::ComboBox::backgroundColourId, col::bg2);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::gold);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::PopupMenu::backgroundColourId, col::bg2);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::gold.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::goldLight);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, col::gold.withAlpha (0.5f));
    setColour (juce::Slider::textBoxTextColourId, col::goldPale);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ResizableWindow::backgroundColourId, col::bg1);
    setColour (juce::AlertWindow::backgroundColourId, col::bg2);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::gold);
    setColour (juce::TooltipWindow::backgroundColourId, col::bg2);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::gold.withAlpha (0.5f));
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::ToggleButton::tickColourId, col::gold);
    setColour (juce::ProgressBar::foregroundColourId, col::gold);
    setColour (juce::ProgressBar::backgroundColourId, col::bg0);
    setColour (juce::DocumentWindow::textColourId, col::gold);
}

juce::Typeface::Ptr SnaggerLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return ui (f.getHeight(), f.isBold()).getTypefacePtr();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

//==============================================================================
// Buttons. Style is chosen with button.getProperties().set ("style", "...")
//   "gold" (default) | "red" | "redFill" | "ghost" | "tab" | "icon" | "chip"
void SnaggerLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                               bool highlighted, bool down)
{
    const auto style = b.getProperties().getWithDefault ("style", "gold").toString();
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const bool enabled = b.isEnabled();
    const float corner = style == "chip" ? r.getHeight() * 0.5f : 6.0f;

    if (style == "tab" || style == "ghost" || style == "icon")
    {
        if (style != "tab" && (highlighted || down) && enabled)
        {
            g.setColour (col::gold.withAlpha (down ? 0.16f : 0.08f));
            g.fillRoundedRectangle (r, 6.0f);
        }
        if (style == "tab" && on)
        {
            auto ul = r.removeFromBottom (3.0f).reduced (r.getWidth() * 0.22f, 0.0f);
            juce::Path p; p.addRoundedRectangle (ul, 1.5f);
            neonGlow (g, p, col::red, 10.0f, 0.95f);
            g.setColour (col::red);
            g.fillPath (p);
        }
        return;
    }

    if (style == "red" || style == "redFill")
    {
        const bool lit = on || style == "redFill";
        juce::Path shape; shape.addRoundedRectangle (r, corner);
        if (enabled && (lit || highlighted))
            neonGlow (g, shape, col::red, lit ? 14.0f : 9.0f, lit ? 0.75f : 0.45f);

        if (lit)
        {
            g.setGradientFill (juce::ColourGradient (col::red.brighter (0.1f), r.getX(), r.getY(),
                                                     col::redDeep, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, corner);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), r.getX(), r.getY(),
                                                     juce::Colours::white.withAlpha (0.0f), r.getX(), r.getCentreY(), false));
            g.fillRoundedRectangle (r.reduced (1.0f).withHeight (r.getHeight() * 0.5f), corner);
        }
        else
        {
            glossPanel (g, r, corner, col::bg3, col::bg1, false, 0.05f);
        }

        g.setColour ((enabled ? col::red : col::red.withAlpha (0.35f)).withAlpha (down ? 1.0f : (lit ? 0.9f : 0.75f)));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, lit ? 1.2f : 1.0f);
        return;
    }

    // gold (default) and chip
    if (on)
    {
        g.setGradientFill (goldGradient (r));
        g.fillRoundedRectangle (r, corner);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.25f), r.getX(), r.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), r.getX(), r.getCentreY(), false));
        g.fillRoundedRectangle (r.reduced (1.0f).withHeight (r.getHeight() * 0.5f), corner);
        return;
    }

    glossPanel (g, r, corner,
                down ? col::bg1 : (highlighted ? col::bg4 : col::bg3),
                down ? col::bg2 : col::bg1, false, down ? 0.02f : 0.06f);
    goldBorder (g, r, corner, 1.0f, enabled ? (highlighted ? 0.95f : 0.55f) : 0.2f);
}

juce::Font SnaggerLookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    const auto style = b.getProperties().getWithDefault ("style", "gold").toString();
    if (style == "tab")
        return ui (juce::jmin (13.5f, h * 0.36f), true).withExtraKerningFactor (0.16f);
    if (style == "chip")
        return ui (juce::jmin (11.5f, h * 0.48f), true).withExtraKerningFactor (0.06f);
    return ui (juce::jmin (12.0f, h * 0.42f), true).withExtraKerningFactor (0.1f);
}

void SnaggerLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
{
    const auto style = b.getProperties().getWithDefault ("style", "gold").toString();
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat();

    juce::Colour c = col::goldPale;
    if (style == "tab")
        c = on ? col::goldLight : (highlighted ? col::goldPale : col::textDim);
    else if (style == "red")
        c = on ? juce::Colours::white : col::redHot;
    else if (style == "redFill")
        c = juce::Colours::white;
    else if (style == "ghost")
        c = highlighted ? col::goldLight : col::textDim;
    else if (on)
        c = col::bg0;
    else if (highlighted)
        c = col::goldLight;

    if (! b.isEnabled())
        c = c.withAlpha (0.35f);

    auto font = getTextButtonFont (b, b.getHeight());
    auto text = b.getButtonText();
    if (style != "chip" && style != "ghost")
        text = text.toUpperCase();

    // optional icon (stored as a Path in the "icon" property via IconButton helper)
    if (auto* iconObj = dynamic_cast<juce::DynamicObject*> (b.getProperties()["iconPath"].getDynamicObject()))
        juce::ignoreUnused (iconObj);

    g.setFont (font);
    g.setColour (c);

    if (style == "tab" && on)
        goldText (g, text, r.withTrimmedBottom (3.0f), font, juce::Justification::centred);
    else
        g.drawFittedText (text, r.reduced (6.0f, 2.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

void SnaggerLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.22f);
    const bool on = b.getToggleState();

    // pill switch
    auto pill = box.withWidth (box.getWidth() * 1.7f).withCentre ({ box.getCentreX() + box.getWidth() * 0.35f, box.getCentreY() });
    r.removeFromLeft (pill.getWidth() - box.getWidth() + 6.0f);

    g.setColour (on ? col::redDeep : col::bg0);
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (on ? col::red : col::line.brighter (0.2f));
    g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 1.0f);

    auto knob = pill.withWidth (pill.getHeight()).reduced (2.0f);
    if (on) knob.setX (pill.getRight() - pill.getHeight() + 2.0f);
    if (on)
    {
        juce::Path kp; kp.addEllipse (knob);
        neonGlow (g, kp, col::red, 8.0f, 0.9f);
    }
    g.setGradientFill (goldGradient (knob));
    g.fillEllipse (knob);

    g.setColour (highlighted ? col::goldLight : col::text.withAlpha (b.isEnabled() ? 0.9f : 0.4f));
    g.setFont (ui (juce::jmin (12.0f, r.getHeight() * 0.5f), true).withExtraKerningFactor (0.06f));
    g.drawFittedText (b.getButtonText().toUpperCase(), r.toNearestInt(), juce::Justification::centredLeft, 1);
}

//==============================================================================
void SnaggerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 6.0f;
    auto r = bounds.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float radius = size * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
    g.setColour (col::bg0);
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc (bipolar sliders fill from the centre)
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? startAngle + (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * (endAngle - startAngle)
                               : startAngle;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setGradientFill (goldGradient (r));
    g.strokePath (arc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body: glossy black
    auto body = r.reduced (size * 0.16f);
    g.setGradientFill (juce::ColourGradient (col::bg4, body.getCentreX(), body.getY(), col::bg0, body.getCentreX(), body.getBottom(), false));
    g.fillEllipse (body);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), body.getCentreX(), body.getY(),
                                             juce::Colours::white.withAlpha (0.0f), body.getCentreX(), body.getCentreY(), false));
    g.fillEllipse (body.reduced (2.0f).withHeight (body.getHeight() * 0.5f));
    g.setColour (col::goldDark.withAlpha (0.7f));
    g.drawEllipse (body, 1.0f);

    // neon red pointer
    const float pr = body.getWidth() * 0.5f - 5.0f;
    auto tip = c.getPointOnCircumference (pr, angle);
    auto dot = juce::Rectangle<float> (4.5f, 4.5f).withCentre (tip);
    juce::Path dp; dp.addEllipse (dot);
    neonGlow (g, dp, col::red, 6.0f, 1.0f);
    g.setColour (col::redHot);
    g.fillEllipse (dot);
}

void SnaggerLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                           juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const bool horiz = style == juce::Slider::LinearHorizontal;
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    auto track = horiz ? r.withSizeKeepingCentre (r.getWidth(), 4.0f) : r.withSizeKeepingCentre (4.0f, r.getHeight());
    g.setColour (col::bg0);
    g.fillRoundedRectangle (track, 2.0f);
    auto filled = horiz ? track.withRight (pos) : track.withTop (pos);
    g.setGradientFill (goldGradient (filled, ! horiz));
    g.fillRoundedRectangle (filled, 2.0f);
    auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre (horiz ? juce::Point<float> (pos, r.getCentreY())
                                                                          : juce::Point<float> (r.getCentreX(), pos));
    g.setGradientFill (goldGradient (thumb));
    g.fillEllipse (thumb);
}

juce::Label* SnaggerLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (ui (11.0f, true));
    l->setColour (juce::Label::textColourId, col::goldPale);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setJustificationType (juce::Justification::centred);
    return l;
}

//==============================================================================
void SnaggerLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    glossPanel (g, r, 6.0f, col::bg3, col::bg1, false, 0.05f);
    goldBorder (g, r, 6.0f, 1.0f, box.isMouseOver (true) ? 0.9f : 0.45f);

    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (col::gold);
    g.fillPath (arrow);
}

juce::Font SnaggerLookAndFeel::getComboBoxFont (juce::ComboBox&)  { return ui (12.0f, true); }

void SnaggerLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void SnaggerLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::bg2);
    g.setColour (col::gold.withAlpha (0.4f));
    g.drawRect (0, 0, w, h, 1);
}

void SnaggerLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                            bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                            const juce::String& text, const juce::String& shortcut,
                                            const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (col::line);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (2, 0);
    if (isHighlighted && isActive)
    {
        g.setColour (col::gold.withAlpha (0.14f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }
    if (isTicked)
    {
        auto dot = r.removeFromLeft (18).toFloat().withSizeKeepingCentre (6.0f, 6.0f);
        g.setColour (col::red);
        g.fillEllipse (dot);
    }
    else
        r.removeFromLeft (18);

    g.setColour (textColour != nullptr ? *textColour
                                       : (isActive ? (isHighlighted ? col::goldLight : col::text) : col::textFaint));
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, r.reduced (4, 0), juce::Justification::centredLeft, 1);
    if (shortcut.isNotEmpty())
    {
        g.setColour (col::textDim);
        g.drawText (shortcut, r.reduced (8, 0), juce::Justification::centredRight);
    }
    juce::ignoreUnused (hasSubMenu, icon);
}

juce::Font SnaggerLookAndFeel::getPopupMenuFont()   { return ui (13.0f); }

void SnaggerLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (r, 6.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.5f), 0, 0,
                                             juce::Colours::transparentBlack, 0, 6.0f, false));
    g.fillRoundedRectangle (r, 6.0f);
}

void SnaggerLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    if (ed.hasKeyboardFocus (true) && ! ed.isReadOnly())
        goldBorder (g, r, 6.0f, 1.2f, 1.0f);
    else
    {
        g.setColour (col::line.brighter (0.15f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }
}

void SnaggerLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                        int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float> ((float) x + 2, (float) thumbStart, (float) w - 4, (float) thumbSize)
                                            : juce::Rectangle<float> ((float) thumbStart, (float) y + 2, (float) thumbSize, (float) h - 4);
    g.setColour (col::gold.withAlpha (isMouseDown ? 0.8f : (isMouseOver ? 0.6f : 0.32f)));
    g.fillRoundedRectangle (thumb, 3.0f);
}

void SnaggerLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int w, int h, double progress, const juce::String& text)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (col::bg0);
    g.fillRoundedRectangle (r, h * 0.5f);
    if (progress >= 0.0 && progress <= 1.0)
    {
        auto f = r.withWidth (r.getWidth() * (float) progress);
        g.setGradientFill (goldGradient (f));
        g.fillRoundedRectangle (f, h * 0.5f);
    }
    else
    {
        const float t = (float) (juce::Time::getMillisecondCounter() % 1200) / 1200.0f;
        auto f = r.withWidth (r.getWidth() * 0.3f).withX (r.getWidth() * (t * 1.3f - 0.3f));
        g.setGradientFill (goldGradient (f));
        g.fillRoundedRectangle (f.getIntersection (r), h * 0.5f);
    }
    if (text.isNotEmpty())
    {
        g.setColour (col::text);
        g.setFont (ui (10.0f, true));
        g.drawText (text, r, juce::Justification::centred);
    }
}

void SnaggerLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (col::bg2);
    g.fillRoundedRectangle (r, 5.0f);
    goldBorder (g, r, 5.0f, 1.0f, 0.6f);
    g.setColour (col::text);
    g.setFont (ui (12.5f));
    g.drawFittedText (text, r.reduced (10, 6).toNearestInt(), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> SnaggerLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    const int w = juce::jmin (340, 24 + (int) juce::GlyphArrangement::getStringWidth (ui (12.5f), tipText));
    const int lines = (int) std::ceil (juce::GlyphArrangement::getStringWidth (ui (12.5f), tipText) / 316.0f);
    const int h = 14 + 17 * juce::jlimit (1, 4, lines);
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 12,
                                 w, h).constrainedWithin (parentArea);
}

void SnaggerLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool)
{
    g.setColour (col::gold.withAlpha (isMouseOver ? 0.8f : 0.35f));
    for (int i = 1; i <= 3; ++i)
    {
        const float o = (float) i * 4.0f;
        g.drawLine ((float) w - o, (float) h - 1.0f, (float) w - 1.0f, (float) h - o, 1.0f);
    }
}

juce::Font SnaggerLookAndFeel::getLabelFont (juce::Label& l)
{
    auto f = l.getFont();
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return ui (f.getHeight(), f.isBold());
    return f;
}

void SnaggerLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    auto r = alert.getLocalBounds().toFloat();
    glossPanel (g, r, 8.0f, col::bg3, col::bg1, true, 0.05f);
    g.setColour (col::text);
    layout.draw (g, textArea.toFloat());
}

} // namespace snag
