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

//==============================================================================
// Materials
namespace
{
    /** Fine, random grain that gives satin and glass surfaces a real-material feel. Tiled. */
    const juce::Image& grainTile()
    {
        static const juce::Image tile = []
        {
            juce::Image img (juce::Image::ARGB, 128, 128, true);
            juce::Random rng (0x5a7);
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 128; ++x)
                {
                    const float n = rng.nextFloat();
                    const auto c = n > 0.5f ? juce::Colours::white.withAlpha ((n - 0.5f) * 0.075f)
                                            : juce::Colours::black.withAlpha ((0.5f - n) * 0.14f);
                    bd.setPixelColour (x, y, c);
                }
            return img;
        }();
        return tile;
    }

    void fillGrain (juce::Graphics& g, const juce::Path& shape, float alpha)
    {
        g.saveState();
        g.setOpacity (alpha);
        g.setFillType (juce::FillType (grainTile(), juce::AffineTransform()));
        g.fillPath (shape);
        g.restoreState();
    }
}

const juce::Image& goldPlateImage()
{
    static const juce::Image img = juce::ImageCache::getFromMemory (SnaggerBinary::gold_plate_jpg, SnaggerBinary::gold_plate_jpgSize);
    return img;
}

void GoldPlate::draw (juce::Graphics& g, juce::Rectangle<int> area)
{
    const float scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (cache.isNull() || cachedArea != area || ! juce::approximatelyEqual (cachedScale, scale))
    {
        const auto& src = goldPlateImage();
        cache = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) area.getWidth() * scale)),
                             juce::jmax (1, juce::roundToInt ((float) area.getHeight() * scale)), false);
        juce::Graphics cg (cache);
        cg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        if (src.isValid())
            cg.drawImage (src, cache.getBounds().toFloat(), juce::RectanglePlacement::fillDestination);
        else
        {
            cg.setGradientFill (goldGradient (cache.getBounds().toFloat(), false));
            cg.fillAll();
        }
        cachedArea = area;
        cachedScale = scale;
    }
    g.setOpacity (1.0f);
    g.drawImage (cache, area.toFloat());
}

void glassGlare (juce::Graphics& g, juce::Rectangle<float> r, float corner, float glare)
{
    juce::Path shape;
    shape.addRoundedRectangle (r, corner);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (shape);

    // a broad, crisp-edged reflection across the upper left, like a window in polished glass
    const float h = r.getHeight();
    const float edgeTop = r.getX() + juce::jmin (r.getWidth() * 0.46f, h * 2.2f + 180.0f);
    const float slant = h * 0.55f;
    juce::Path wedge;
    wedge.startNewSubPath (r.getX(), r.getY());
    wedge.lineTo (edgeTop, r.getY());
    wedge.lineTo (edgeTop - slant, r.getBottom());
    wedge.lineTo (r.getX(), r.getBottom());
    wedge.closeSubPath();
    juce::ColourGradient gl (juce::Colours::white.withAlpha (0.075f * glare), r.getX(), r.getY(),
                             juce::Colours::white.withAlpha (0.022f * glare), r.getX(), r.getBottom(), false);
    g.setGradientFill (gl);
    g.fillPath (wedge);

    // a thin secondary streak
    juce::Path streak;
    const float s0 = edgeTop + juce::jmin (40.0f, h * 0.35f);
    const float sw = juce::jmin (16.0f, 4.0f + h * 0.08f);
    streak.startNewSubPath (s0, r.getY());
    streak.lineTo (s0 + sw, r.getY());
    streak.lineTo (s0 + sw - slant, r.getBottom());
    streak.lineTo (s0 - slant, r.getBottom());
    streak.closeSubPath();
    g.setColour (juce::Colours::white.withAlpha (0.028f * glare));
    g.fillPath (streak);
}

void glassWindow (juce::Graphics& g, juce::Rectangle<float> r, float corner, float glare, bool rim)
{
    const juce::Colour shadowGold (0xff1f1403), lipGold (0xfffff0bf);

    if (rim)
    {
        // the cut in the gold: the upper wall faces away from the light, the lower lip catches it
        auto cut = r.expanded (2.5f);
        juce::ColourGradient cg (shadowGold.withAlpha (0.85f), 0.0f, cut.getY(), lipGold.withAlpha (0.75f), 0.0f, cut.getBottom(), false);
        cg.addColour (juce::jlimit (0.05, 0.95, 1.0 - 10.0 / juce::jmax (12.0, (double) cut.getHeight())), shadowGold.withAlpha (0.55f));
        g.setGradientFill (cg);
        g.fillRoundedRectangle (cut, corner + 2.5f);
    }

    juce::Path shape;
    shape.addRoundedRectangle (r, corner);

    // the glass itself: almost black, very slightly lifted at the top
    juce::ColourGradient body (juce::Colour (0xff121216), 0.0f, r.getY(), juce::Colour (0xff020203), 0.0f, r.getBottom(), false);
    body.addColour (0.35, juce::Colour (0xff09090b));
    g.setGradientFill (body);
    g.fillPath (shape);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);

        // the plate shades the top of the recessed glass
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.65f), 0.0f, r.getY(),
                                                 juce::Colours::transparentBlack, 0.0f, r.getY() + juce::jmin (9.0f, r.getHeight() * 0.3f), false));
        g.fillRect (r);

        // warm bounce from the gold along the bottom edge
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, r.getBottom() - juce::jmin (14.0f, r.getHeight() * 0.35f),
                                                 col::gold.withAlpha (0.10f), 0.0f, r.getBottom(), false));
        g.fillRect (r);
    }

    fillGrain (g, shape, 0.35f);

    if (glare > 0.0f)
        glassGlare (g, r, corner, glare);

    // polished bevel: bright along the top, gold reflection along the bottom
    juce::ColourGradient edge (juce::Colours::white.withAlpha (0.22f), 0.0f, r.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + juce::jmin (16.0f, r.getHeight() * 0.4f), false);
    g.setGradientFill (edge);
    g.drawRoundedRectangle (r.reduced (1.0f), juce::jmax (0.0f, corner - 1.0f), 1.0f);
    g.setGradientFill (juce::ColourGradient (col::goldLight.withAlpha (0.0f), 0.0f, r.getBottom() - juce::jmin (14.0f, r.getHeight() * 0.4f),
                                             col::goldLight.withAlpha (0.30f), 0.0f, r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (1.0f), juce::jmax (0.0f, corner - 1.0f), 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.drawRoundedRectangle (r.reduced (0.25f), corner, 0.8f);
}

void glassWell (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool highlighted)
{
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.5f), 0.0f, r.getY(),
                                             juce::Colours::transparentBlack, 0.0f, r.getY() + 6.0f, false));
    g.fillRoundedRectangle (r, corner);
    if (highlighted)
    {
        g.setColour (col::gold.withAlpha (0.07f));
        g.fillRoundedRectangle (r, corner);
    }
    g.setColour (highlighted ? col::gold.withAlpha (0.45f) : juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

void satinSurface (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool highlighted, bool down, bool shadow)
{
    if (shadow)
    {
        // soft contact shadow
        const int layers = down ? 2 : 4;
        for (int i = layers; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.11f));
            g.fillRoundedRectangle (r.expanded ((float) i * 0.7f).translated (0.0f, (float) i * 0.55f), corner + (float) i * 0.7f);
        }
    }

    juce::Path shape;
    shape.addRoundedRectangle (r, corner);
    const float w = r.getWidth(), h = r.getHeight();

    // body: satin black, lit from above
    juce::ColourGradient body (down ? juce::Colour (0xff141416) : (highlighted ? juce::Colour (0xff36363b) : juce::Colour (0xff2c2c31)),
                               0.0f, r.getY(),
                               down ? juce::Colour (0xff1d1d21) : juce::Colour (0xff0f0f11), 0.0f, r.getBottom(), false);
    body.addColour (0.5, down ? juce::Colour (0xff18181b) : (highlighted ? juce::Colour (0xff222226) : juce::Colour (0xff1b1b1e)));
    g.setGradientFill (body);
    g.fillPath (shape);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);
        // broad, diffuse sheen - satin scatters the light instead of mirroring it
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (down ? 0.03f : (highlighted ? 0.12f : 0.085f)),
                                    r.getCentreX(), r.getY() - h * 0.15f,
                                    juce::Colours::white.withAlpha (0.0f), r.getCentreX() + juce::jmax (w, h) * 0.55f, r.getY() - h * 0.15f + h * 0.9f, true);
        g.setGradientFill (sheen);
        g.fillRect (r);
    }

    fillGrain (g, shape, 0.9f);

    // bevel: light top lip, dark bottom edge
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (down ? 0.05f : 0.2f), 0.0f, r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + juce::jmin (10.0f, h * 0.5f), false));
    g.drawRoundedRectangle (r.reduced (1.0f), juce::jmax (0.0f, corner - 1.0f), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawRoundedRectangle (r.reduced (0.25f), corner, 0.9f);
}

void engravedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                   const juce::Font& f, juce::Justification j)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (f, text, r.getX(), r.getY(), r.getWidth(), r.getHeight(), j, 1, 0.9f);
    juce::Path p;
    ga.createPath (p);
    // the lower wall of each cut catches the light...
    g.setColour (juce::Colour (0xfffff4cf).withAlpha (0.55f));
    g.fillPath (p, juce::AffineTransform::translation (0.0f, 1.0f));
    // ...and the cut itself is in shadow
    auto b = p.getBounds();
    juce::ColourGradient cut (juce::Colour (0xff120b01), 0.0f, b.getY(), juce::Colour (0xff3b2706), 0.0f, b.getBottom(), false);
    g.setGradientFill (cut);
    g.fillPath (p);
}

void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float strength)
{
    neonGlow (g, p, c, 9.0f, 0.95f * strength);
    neonGlow (g, p, c.brighter (0.3f), 3.0f, 0.8f * strength);
    g.setColour (c.interpolatedWith (juce::Colours::white, 0.28f * strength));
    g.fillPath (p);
}

void glowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
               juce::Justification j, juce::Colour c, float strength)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (f, text, r.getX(), r.getY(), r.getWidth(), r.getHeight(), j, 1, 0.8f);
    juce::Path p;
    ga.createPath (p);
    glowPath (g, p, c, strength);
}

void led (juce::Graphics& g, juce::Point<float> centre, float radius, bool lit, juce::Colour c)
{
    auto r = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (r.expanded (1.0f));
    if (lit)
    {
        juce::Path p; p.addEllipse (r);
        neonGlow (g, p, c, radius * 3.5f, 1.0f);
        g.setGradientFill (juce::ColourGradient (c.interpolatedWith (juce::Colours::white, 0.6f), centre.x, centre.y - radius * 0.3f,
                                                 c, centre.x, r.getBottom(), true));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (c.darker (0.6f).withMultipliedSaturation (0.7f), centre.x, r.getY(),
                                                 juce::Colour (0xff1a0508), centre.x, r.getBottom(), false));
    }
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.6f : 0.18f));
    g.fillEllipse (r.reduced (radius * 0.45f).withHeight (radius * 0.5f).translated (0.0f, -radius * 0.25f));
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
    g.setOpacity (1.0f);   // drawImage uses the current colour's alpha
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
    juce::Path external()
    {
        juce::Path box;
        box.startNewSubPath (0.42f, 0.14f); box.lineTo (0.12f, 0.14f); box.lineTo (0.12f, 0.88f);
        box.lineTo (0.86f, 0.88f); box.lineTo (0.86f, 0.58f);
        auto s = strokedOf (box, 0.1f);
        juce::Path arrow;
        arrow.startNewSubPath (0.46f, 0.54f); arrow.lineTo (0.9f, 0.1f);
        s.addPath (strokedOf (arrow, 0.1f));
        s.addTriangle (0.58f, 0.04f, 0.96f, 0.04f, 0.96f, 0.42f);
        return s;
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
// gold / red / redFill / chip are satin black caps; their legends light up neon red when active.
void SnaggerLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                               bool highlighted, bool down)
{
    const auto style = b.getProperties().getWithDefault ("style", "gold").toString();
    auto r = b.getLocalBounds().toFloat().reduced (style == "tab" || style == "ghost" || style == "icon" ? 0.5f : 2.0f);
    const bool on = b.getToggleState();
    const bool enabled = b.isEnabled();
    const float corner = style == "chip" ? r.getHeight() * 0.5f : juce::jmin (6.0f, r.getHeight() * 0.3f);

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
            glowPath (g, p, col::red, 1.0f);
        }
        return;
    }

    const bool latched = on && style != "redFill";
    const bool redRim  = enabled && (style == "redFill" || (style == "red" && on));

    juce::Path shape;
    shape.addRoundedRectangle (r, corner);
    if (redRim)
        neonGlow (g, shape, col::red, highlighted ? 12.0f : 9.0f, highlighted ? 0.55f : 0.4f);

    satinSurface (g, r, corner, highlighted && enabled, down || latched, ! (down || latched));

    if (redRim)
    {
        juce::Path rim; rim.addRoundedRectangle (r.reduced (1.2f), juce::jmax (0.0f, corner - 1.0f));
        juce::Path stroked;
        juce::PathStrokeType (1.2f).createStrokedPath (stroked, rim);
        glowPath (g, stroked, col::red, 0.8f);
    }
    else if (style == "red")
    {
        g.setColour (col::red.withAlpha (enabled ? (highlighted ? 0.7f : 0.42f) : 0.15f));
        g.drawRoundedRectangle (r.reduced (1.0f), juce::jmax (0.0f, corner - 1.0f), 1.0f);
    }
    else
    {
        // fine gold trim ring
        goldBorder (g, r.reduced (0.6f), corner, 1.0f, enabled ? (highlighted ? 0.75f : (latched ? 0.5f : 0.32f)) : 0.12f);
    }
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

theme::Legend theme::buttonLegend (const juce::Button& b, bool highlighted)
{
    const auto style = b.getProperties().getWithDefault ("style", "gold").toString();
    const bool on = b.getToggleState();
    Legend l { col::goldPale, false };

    if (style == "tab")          l.colour = on ? col::goldLight : (highlighted ? col::goldPale : col::textDim);
    else if (style == "ghost")   l.colour = highlighted ? col::goldLight : col::textDim;
    else if (style == "icon")    l.colour = highlighted ? col::goldLight : col::gold.withAlpha (0.85f);
    else if (style == "redFill") l = { col::red, true };
    else if (style == "red")     l = { on ? col::red : col::redHot, on };
    else if (on)                 l = { col::red, true };
    else if (highlighted)        l.colour = col::goldLight;

    if (! b.isEnabled())
        l = { l.colour.withAlpha (0.35f), false };
    return l;
}

void SnaggerLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool down)
{
    const auto style = b.getProperties().getWithDefault ("style", "gold").toString();
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat();
    if (down && style != "tab" && style != "ghost")
        r.translate (0.0f, 1.0f);

    const auto legend = buttonLegend (b, highlighted);
    auto font = getTextButtonFont (b, b.getHeight());
    auto text = b.getButtonText();
    if (style != "chip" && style != "ghost")
        text = text.toUpperCase();

    if (style == "tab" && on)
        goldText (g, text, r.withTrimmedBottom (3.0f), font, juce::Justification::centred);
    else if (legend.glow)
        glowText (g, text, r.reduced (8.0f, 2.0f), font, juce::Justification::centred, legend.colour);
    else
    {
        g.setFont (font);
        g.setColour (legend.colour);
        g.drawFittedText (text, r.reduced (8.0f, 2.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
}

void SnaggerLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.2f);
    const bool on = b.getToggleState();

    // recessed slot with a satin black slider in it
    auto pill = box.withWidth (box.getWidth() * 1.8f).withCentre ({ box.getCentreX() + box.getWidth() * 0.4f, box.getCentreY() });
    r.removeFromLeft (pill.getWidth() - box.getWidth() + 8.0f);
    const float pr = pill.getHeight() * 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.fillRoundedRectangle (pill, pr);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black, 0.0f, pill.getY(), juce::Colour (0xff17171a), 0.0f, pill.getBottom(), false));
    g.fillRoundedRectangle (pill.reduced (1.0f), pr - 1.0f);
    if (on)
    {
        // red light spilling out of the slot
        juce::Path slot; slot.addRoundedRectangle (pill.reduced (2.0f), pr - 2.0f);
        neonGlow (g, slot, col::red, 6.0f, 0.55f);
        g.setGradientFill (juce::ColourGradient (col::red.withAlpha (0.75f), pill.getX(), 0.0f, col::redDeep.withAlpha (0.4f), pill.getRight(), 0.0f, false));
        g.fillPath (slot);
    }
    g.setColour (col::goldLight.withAlpha (0.22f));
    g.drawRoundedRectangle (pill.withTrimmedTop (1.0f), pr, 0.8f);

    auto knob = pill.withWidth (pill.getHeight()).reduced (1.5f);
    if (on) knob.setX (pill.getRight() - pill.getHeight() + 1.5f);
    satinSurface (g, knob, knob.getHeight() * 0.5f, highlighted, false, true);
    led (g, knob.getCentre(), juce::jmax (1.6f, knob.getWidth() * 0.13f), on);

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
    const float arcR = radius - 2.5f;

    // LED ring: a dark groove with lit red segments for the value
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.strokePath (track, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff2a0a0f));
    g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? startAngle + (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * (endAngle - startAngle)
                               : startAngle;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        juce::Path stroked;
        juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroked, arc);
        glowPath (g, stroked, col::red, s.isEnabled() ? 0.9f : 0.3f);
    }

    // knob: satin black skirt + cap
    auto skirt = r.reduced (size * 0.15f);
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.13f));
        g.fillEllipse (skirt.expanded ((float) i * 0.8f).translated (0.0f, (float) i * 0.9f));
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a40), skirt.getX(), skirt.getY(),
                                             juce::Colour (0xff060607), skirt.getRight(), skirt.getBottom(), false));
    g.fillEllipse (skirt);

    // fine knurling on the skirt
    {
        const int ridges = juce::jlimit (24, 60, (int) (skirt.getWidth() * 1.2f));
        const float ro = skirt.getWidth() * 0.5f, ri = ro - juce::jmax (2.0f, skirt.getWidth() * 0.06f);
        for (int i = 0; i < ridges; ++i)
        {
            const float a = (float) i / (float) ridges * juce::MathConstants<float>::twoPi;
            const float lightness = 0.5f + 0.5f * std::cos (a + juce::MathConstants<float>::pi * 0.25f);   // lit from the top left
            g.setColour ((i % 2 == 0 ? juce::Colours::white : juce::Colours::black).withAlpha (i % 2 == 0 ? 0.03f + 0.07f * lightness : 0.25f));
            g.drawLine ({ c.getPointOnCircumference (ri, a), c.getPointOnCircumference (ro - 0.5f, a) }, 1.0f);
        }
    }

    auto cap = skirt.reduced (skirt.getWidth() * 0.09f);
    juce::Path capPath; capPath.addEllipse (cap);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c31), cap.getCentreX(), cap.getY(),
                                             juce::Colour (0xff0e0e10), cap.getCentreX(), cap.getBottom(), false));
    g.fillPath (capPath);
    // satin sheen: broad and soft
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.13f), cap.getX() + cap.getWidth() * 0.38f, cap.getY() + cap.getHeight() * 0.22f,
                                             juce::Colours::white.withAlpha (0.0f), cap.getX() + cap.getWidth() * 0.38f + cap.getWidth() * 0.62f, cap.getY() + cap.getHeight() * 0.22f, true));
    g.fillPath (capPath);
    fillGrain (g, capPath, 1.0f);
    // rim highlight top-left, shadow bottom-right
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), cap.getX(), cap.getY(),
                                             juce::Colours::black.withAlpha (0.5f), cap.getRight(), cap.getBottom(), false));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);

    // glowing red pointer
    const float capR = cap.getWidth() * 0.5f;
    juce::Path pointer;
    pointer.startNewSubPath (c.getPointOnCircumference (capR * 0.42f, angle));
    pointer.lineTo (c.getPointOnCircumference (capR * 0.86f, angle));
    juce::Path ps;
    juce::PathStrokeType (juce::jmax (2.0f, capR * 0.11f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (ps, pointer);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillPath (ps, juce::AffineTransform::translation (0.0f, 0.8f));
    glowPath (g, ps, col::red, s.isEnabled() ? 1.0f : 0.3f);
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
    auto track = horiz ? r.withSizeKeepingCentre (r.getWidth(), 5.0f) : r.withSizeKeepingCentre (5.0f, r.getHeight());
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.fillRoundedRectangle (track, 2.5f);
    auto filled = (horiz ? track.withRight (pos) : track.withTop (pos)).reduced (1.0f);
    if (! filled.isEmpty())
    {
        juce::Path fp; fp.addRoundedRectangle (filled, 1.5f);
        glowPath (g, fp, col::red, 0.8f);
    }
    auto thumb = juce::Rectangle<float> (14.0f, 14.0f).withCentre (horiz ? juce::Point<float> (pos, r.getCentreY())
                                                                          : juce::Point<float> (r.getCentreX(), pos));
    satinSurface (g, thumb, 7.0f, s.isMouseOverOrDragging(), false, true);
    led (g, thumb.getCentre(), 1.8f, true);
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
void SnaggerLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (2.0f);
    const bool over = box.isMouseOver (true);
    const float corner = juce::jmin (6.0f, r.getHeight() * 0.3f);
    satinSurface (g, r, corner, over, down, true);
    goldBorder (g, r.reduced (0.6f), corner, 1.0f, over ? 0.75f : 0.32f);

    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    glowPath (g, arrow, col::red, box.isEnabled() ? 0.8f : 0.2f);
}

juce::Font SnaggerLookAndFeel::getComboBoxFont (juce::ComboBox&)  { return ui (12.0f, true); }

void SnaggerLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void SnaggerLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141418), 0.0f, 0.0f, juce::Colour (0xff060607), 0.0f, (float) h, false));
    g.fillRect (r);
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRect (r.withHeight (juce::jmin (40.0f, r.getHeight() * 0.3f)));
    goldBorder (g, r, 0.0f, 1.0f, 0.6f);
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
    glassWindow (g, r, 8.0f, 0.6f, false);
    goldBorder (g, r, 8.0f, 1.0f, 0.8f);
    g.setColour (col::text);
    layout.draw (g, textArea.toFloat());
}

} // namespace snag
