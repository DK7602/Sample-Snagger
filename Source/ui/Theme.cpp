#include "Theme.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "SnaggerBinaryData.h"
#include <deque>
#include <map>

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

const juce::Image& glitterImage()
{
    static const juce::Image img = juce::ImageCache::getFromMemory (SnaggerBinary::glitter_glass_jpg, SnaggerBinary::glitter_glass_jpgSize);
    return img;
}

const juce::Image& goldSmoothImage()
{
    static const juce::Image img = juce::ImageCache::getFromMemory (SnaggerBinary::gold_smooth_jpg, SnaggerBinary::gold_smooth_jpgSize);
    return img;
}

void ScaledTexture::draw (juce::Graphics& g, juce::Rectangle<int> area, const juce::Image& src)
{
    const float scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (cache.isNull() || cachedArea.getWidth() != area.getWidth() || cachedArea.getHeight() != area.getHeight()
        || ! juce::approximatelyEqual (cachedScale, scale) || cachedSource != &src)
    {
        cache = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) area.getWidth() * scale)),
                             juce::jmax (1, juce::roundToInt ((float) area.getHeight() * scale)), false);
        juce::Graphics cg (cache);
        cg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        if (src.isValid())
            cg.drawImage (src, cache.getBounds().toFloat(), juce::RectanglePlacement::fillDestination);
        else
            cg.fillAll (juce::Colours::black);
        cachedArea = area;
        cachedScale = scale;
        cachedSource = &src;
    }
    g.setOpacity (1.0f);   // drawImage uses the current colour's alpha
    g.drawImage (cache, area.toFloat());
}

void glassGlare (juce::Graphics& g, juce::Rectangle<float> r, float corner, float glare)
{
    // plain glass: only a faint reflection along the top
    juce::Path shape;
    shape.addRoundedRectangle (r, corner);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (shape);
    const float h = juce::jmin (r.getHeight() * 0.4f, 60.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.045f * glare), 0.0f, r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + h, false));
    g.fillRect (r.withHeight (h));
}

namespace
{
    /** A soft star-like highlight where light catches the gold rim. */
    void rimFlare (juce::Graphics& g, juce::Point<float> c, float size, bool vertical)
    {
        auto core = vertical ? juce::Rectangle<float> (size * 0.28f, size * 1.6f).withCentre (c)
                             : juce::Rectangle<float> (size * 1.6f, size * 0.28f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff1c8).withAlpha (0.95f), c.x, c.y,
                                                 col::gold.withAlpha (0.0f), c.x + size, c.y, true));
        g.fillEllipse (juce::Rectangle<float> (size * 2.0f, size * 2.0f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillEllipse (core);
    }

    void renderGlassWindow (juce::Graphics& g, juce::Rectangle<float> r, float corner, float glare, bool rim)
    {
        juce::Path shape;
        shape.addRoundedRectangle (r, corner);

        if (rim)
        {
            // soft shadow onto the glitter plate
            for (int i = 4; i >= 1; --i)
            {
                g.setColour (juce::Colours::black.withAlpha (0.16f));
                g.fillRoundedRectangle (r.expanded ((float) i * 1.1f).translated (0.0f, (float) i * 0.7f), corner + (float) i);
            }
        }

        // the glass: black, a breath lighter at the top
        juce::ColourGradient body (juce::Colour (0xff0d0d10), 0.0f, r.getY(), juce::Colour (0xff020203), 0.0f,
                                   r.getY() + juce::jmin (r.getHeight(), 140.0f), false);
        g.setGradientFill (body);
        g.fillPath (shape);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (shape);

            // the polished bevel inside the rim catches light all the way round...
            const int rings = 8;
            for (int i = 0; i < rings; ++i)
            {
                const float a = 0.085f * std::pow (1.0f - (float) i / (float) rings, 1.6f);
                const float inset = 1.8f + (float) i * 1.35f;
                g.setColour (juce::Colour (0xffece6dc).withAlpha (a));
                g.drawRoundedRectangle (r.reduced (inset), juce::jmax (0.0f, corner - inset), 1.4f);
            }

            // ...most of all in the lower-left and upper-right corners
            const float reach = juce::jmin (r.getWidth(), r.getHeight()) * 0.55f + 10.0f;
            auto cornerGlow = [&] (juce::Point<float> p, float alpha)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (alpha), p.x, p.y,
                                                         juce::Colours::white.withAlpha (0.0f), p.x + reach, p.y, true));
                g.fillRect (r);
            };
            cornerGlow (r.getBottomLeft().translated (2.0f, -2.0f), 0.10f);
            cornerGlow (r.getTopRight().translated (-2.0f, 2.0f), 0.07f);
            cornerGlow (r.getTopLeft().translated (2.0f, 2.0f), 0.04f);

            if (glare > 0.0f)
            {
                const float h = juce::jmin (r.getHeight() * 0.4f, 60.0f);
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.04f * glare), 0.0f, r.getY(),
                                                         juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + h, false));
                g.fillRect (r.withHeight (h));
            }
        }
        fillGrain (g, shape, 0.2f);

        if (! rim)
        {
            g.setColour (juce::Colours::black.withAlpha (0.9f));
            g.drawRoundedRectangle (r.reduced (0.25f), corner, 0.8f);
            return;
        }

        // glowing gold rim
        juce::Path rimPath;
        rimPath.addRoundedRectangle (r.reduced (0.6f), corner);
        juce::Path stroked;
        juce::PathStrokeType (1.7f).createStrokedPath (stroked, rimPath);
        neonGlow (g, stroked, col::gold, 7.0f, 0.55f);
        juce::ColourGradient rg (juce::Colour (0xfffff0c0), r.getX(), r.getY(), col::goldDark, r.getRight(), r.getBottom(), false);
        rg.addColour (0.25, col::goldLight);
        rg.addColour (0.55, col::gold);
        rg.addColour (0.8, col::goldLight.interpolatedWith (col::gold, 0.5f));
        g.setGradientFill (rg);
        g.fillPath (stroked);
        {
            // brighter along the top of the rim, where the light comes from
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (r.expanded (2.0f).withHeight (r.getHeight() * 0.35f + 2.0f).getSmallestIntegerContainer());
            g.setColour (juce::Colour (0xfffff6dc).withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.6f), corner, 0.6f);
        }

        // light catching the rim
        const float fs = juce::jlimit (3.0f, 9.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.06f);
        rimFlare (g, { r.getX() + 0.6f, r.getY() + r.getHeight() * 0.38f }, fs, true);
        rimFlare (g, { r.getRight() - 0.6f, r.getY() + r.getHeight() * 0.62f }, fs, true);
        rimFlare (g, { r.getX() + corner * 0.3f + 1.0f, r.getBottom() - corner * 0.3f - 1.0f }, fs * 0.8f, false);
        rimFlare (g, { r.getRight() - corner * 0.3f - 1.0f, r.getY() + corner * 0.3f + 1.0f }, fs * 0.8f, false);
    }

    /** Glass windows are drawn a lot (the waveform repaints 30x a second), so each size is rendered
        once into an image and reused. */
    struct GlassCache
    {
        std::map<juce::String, juce::Image> images;
        std::deque<juce::String> order;
    };

    GlassCache& glassCache()
    {
        static GlassCache c;
        return c;
    }
}

void glassWindow (juce::Graphics& g, juce::Rectangle<float> r, float corner, float glare, bool rim)
{
    if (r.getWidth() < 2.0f || r.getHeight() < 2.0f)
        return;

    const float scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const float margin = rim ? 12.0f : 1.0f;
    const auto key = juce::String (r.getWidth(), 1) + "x" + juce::String (r.getHeight(), 1) + "c" + juce::String (corner, 1)
                   + "g" + juce::String (glare, 2) + (rim ? "r" : "n") + "s" + juce::String (scale, 2);

    auto& cache = glassCache();
    auto it = cache.images.find (key);
    if (it == cache.images.end())
    {
        const int w = juce::roundToInt ((r.getWidth() + margin * 2.0f) * scale);
        const int h = juce::roundToInt ((r.getHeight() + margin * 2.0f) * scale);
        if ((juce::int64) w * h > 12000000)   // absurdly large: draw directly
        {
            renderGlassWindow (g, r, corner, glare, rim);
            return;
        }
        juce::Image img (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
        {
            juce::Graphics ig (img);
            ig.addTransform (juce::AffineTransform::scale (scale));
            renderGlassWindow (ig, { margin, margin, r.getWidth(), r.getHeight() }, corner, glare, rim);
        }
        it = cache.images.emplace (key, img).first;
        cache.order.push_back (key);
        while (cache.order.size() > 48)
        {
            cache.images.erase (cache.order.front());
            cache.order.pop_front();
        }
    }
    g.setOpacity (1.0f);
    g.drawImage (it->second, r.expanded (margin));
}

void goldPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner, ScaledTexture& texture)
{
    // shadow onto the glitter plate
    for (int i = 5; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.14f));
        g.fillRoundedRectangle (r.expanded ((float) i * 1.2f).translated (0.0f, (float) i * 0.9f), corner + (float) i);
    }

    juce::Path shape;
    shape.addRoundedRectangle (r, corner);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);
        texture.draw (g, r.getSmallestIntegerContainer(), goldSmoothImage());
    }

    // polished bevel: bright top edge, dark bottom edge, fine dark outline
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff8e0).withAlpha (0.85f), 0.0f, r.getY(),
                                             juce::Colour (0xfffff8e0).withAlpha (0.0f), 0.0f, r.getY() + 6.0f, false));
    g.drawRoundedRectangle (r.reduced (1.0f), corner - 1.0f, 1.6f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a2402).withAlpha (0.0f), 0.0f, r.getBottom() - 8.0f,
                                             juce::Colour (0xff3a2402).withAlpha (0.8f), 0.0f, r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (1.0f), corner - 1.0f, 1.6f);
    g.setColour (juce::Colour (0xff1a1000).withAlpha (0.9f));
    g.drawRoundedRectangle (r.reduced (0.25f), corner, 0.8f);
}

void glitterText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                  const juce::Font& f, juce::Justification j)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (f, text, r.getX(), r.getY(), r.getWidth(), r.getHeight(), j, 1, 0.9f);
    juce::Path p;
    ga.createPath (p);
    const auto b = p.getBounds();

    // set into the gold: dark shadow above, light catch below
    g.setColour (juce::Colour (0xfffff4cf).withAlpha (0.6f));
    g.fillPath (p, juce::AffineTransform::translation (0.0f, 1.2f));
    g.setColour (juce::Colour (0xff1a0f00).withAlpha (0.7f));
    g.fillPath (p, juce::AffineTransform::translation (0.0f, -0.8f));

    // black glass full of glitter
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (p);
        g.fillAll (juce::Colours::black);
        static const juce::Image tex = juce::ImageCache::getFromMemory (SnaggerBinary::glitter_text_jpg, SnaggerBinary::glitter_text_jpgSize);
        if (tex.isValid())
        {
            // dense glitter at half size, so each speck is one crisp pixel on a Retina / HiDPI screen
            g.setOpacity (1.0f);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImageTransformed (tex, juce::AffineTransform::scale (0.5f).translated (b.getX() - 4.0f, b.getY() - 4.0f));
        }
        // glass: a glossy highlight over the top half
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.2f), 0.0f, b.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0.0f, b.getCentreY(), false));
        g.fillRect (b.withHeight (b.getHeight() * 0.5f));
    }

    // fine dark edge
    g.setColour (juce::Colour (0xff120a00).withAlpha (0.85f));
    g.strokePath (p, juce::PathStrokeType (0.7f));
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

    if (style == "tab")
    {
        // big, easy-to-spot tabs: the open one is a raised satin cap with a neon underline,
        // the others sit in black glass with a thin gold rim
        auto t = r.reduced (1.0f);
        const float c = juce::jmin (7.0f, t.getHeight() * 0.28f);
        if (on)
        {
            juce::Path shape; shape.addRoundedRectangle (t, c);
            neonGlow (g, shape, col::red, 8.0f, 0.28f);
            satinSurface (g, t, c, highlighted, false, true);
            g.setColour (col::gold.withAlpha (0.55f));
            g.drawRoundedRectangle (t.reduced (0.5f), c, 1.0f);
            auto ul = t.removeFromBottom (3.5f).reduced (t.getWidth() * 0.2f, 0.5f);
            juce::Path p; p.addRoundedRectangle (ul, 1.5f);
            glowPath (g, p, col::red, 1.0f);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (highlighted ? 0.72f : 0.6f));
            g.fillRoundedRectangle (t, c);
            if (highlighted || down)
            {
                g.setColour (col::gold.withAlpha (down ? 0.14f : 0.07f));
                g.fillRoundedRectangle (t, c);
            }
            g.setColour (col::gold.withAlpha (highlighted ? 0.5f : 0.28f));
            g.drawRoundedRectangle (t.reduced (0.5f), c, 1.0f);
        }
        return;
    }

    if (style == "ghost" || style == "icon")
    {
        if ((highlighted || down) && enabled)
        {
            g.setColour (col::gold.withAlpha (down ? 0.16f : 0.08f));
            g.fillRoundedRectangle (r, 6.0f);
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
        return ui (juce::jmin (16.0f, h * 0.47f), true).withExtraKerningFactor (0.14f);
    if (style == "chip")
        return ui (juce::jmin (11.5f, h * 0.48f), true).withExtraKerningFactor (0.06f);
    return ui (juce::jmin (12.0f, h * 0.42f), true).withExtraKerningFactor (0.1f);
}

theme::Legend theme::buttonLegend (const juce::Button& b, bool highlighted)
{
    const auto style = b.getProperties().getWithDefault ("style", "gold").toString();
    const bool on = b.getToggleState();
    Legend l { col::goldPale, false };

    if (style == "tab")          l.colour = on ? col::goldLight : (highlighted ? col::goldLight : col::goldPale.withAlpha (0.78f));
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
namespace
{
    /** The parts of a knob that never move: knurled skirt, red ring, brushed cap. Cached per size. */
    void renderKnobBody (juce::Graphics& g, float size, bool enabled)
    {
        const float R = size * 0.5f;
        const juce::Point<float> c (R, R);
        auto circle = [&] (float radius) { return juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c); };

        // contact shadow
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.15f));
            g.fillEllipse (circle (R * 0.93f + (float) i * 0.6f).translated (0.0f, (float) i * 0.7f));
        }

        // ---- knurled skirt: rows of little pyramids ----
        const float skirtOuter = R * 0.94f, skirtInner = R * 0.79f;
        g.setColour (juce::Colour (0xff0a0a0b));
        g.fillEllipse (circle (skirtOuter));
        const int rows = juce::jmax (2, (int) ((skirtOuter - skirtInner) / 2.6f));
        const float rowH = (skirtOuter - skirtInner) / (float) rows;
        const int around = juce::jlimit (24, 96, (int) (juce::MathConstants<float>::twoPi * skirtOuter / 3.0f));
        const float step = juce::MathConstants<float>::twoPi / (float) around;
        for (int k = 0; k < rows; ++k)
        {
            const float rr = skirtInner + ((float) k + 0.5f) * rowH;
            for (int i = 0; i < around; ++i)
            {
                const float a = ((float) i + ((k % 2) ? 0.5f : 0.0f)) * step;
                const auto top    = c.getPointOnCircumference (rr + rowH * 0.5f, a);
                const auto bottom = c.getPointOnCircumference (rr - rowH * 0.5f, a);
                const auto left   = c.getPointOnCircumference (rr, a - step * 0.5f);
                const auto right  = c.getPointOnCircumference (rr, a + step * 0.5f);
                // light from the upper left: facets facing it are brighter
                const float facing = 0.5f + 0.5f * std::cos (a + juce::MathConstants<float>::pi * 0.25f);
                juce::Path lit, shade;
                lit.startNewSubPath (left);   lit.lineTo (top);    lit.lineTo (right); lit.closeSubPath();
                shade.startNewSubPath (left); shade.lineTo (bottom); shade.lineTo (right); shade.closeSubPath();
                g.setColour (juce::Colour::greyLevel (0.10f + 0.20f * facing));
                g.fillPath (lit);
                g.setColour (juce::Colour::greyLevel (0.03f + 0.05f * facing));
                g.fillPath (shade);
            }
        }
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawEllipse (circle (skirtOuter - 0.4f), 0.8f);

        // ---- glowing red ring in a black groove ----
        g.setColour (juce::Colour (0xff050505));
        g.fillEllipse (circle (skirtInner));
        const float ringR = R * 0.745f;
        {
            juce::Path ring;
            ring.addEllipse (circle (ringR));
            juce::Path stroked;
            juce::PathStrokeType (juce::jmax (1.4f, R * 0.05f)).createStrokedPath (stroked, ring);
            const float s = enabled ? 1.0f : 0.3f;
            neonGlow (g, stroked, col::red, juce::jmax (3.0f, R * 0.18f), 0.9f * s);
            g.setColour (col::red.interpolatedWith (juce::Colours::white, 0.22f).withMultipliedAlpha (s));
            g.fillPath (stroked);
        }

        // ---- cap: black anodised, brushed in circles, catching light in two opposite lobes ----
        const float capR = R * 0.69f;
        g.setColour (juce::Colour (0xff050506));
        g.fillEllipse (circle (capR + juce::jmax (1.0f, R * 0.03f)));   // dark bevel between ring and cap
        juce::Random rng (1234);
        const int wedges = 180;
        const float lightAngle = -juce::MathConstants<float>::pi * 0.25f;   // upper left (0 = up)
        for (int i = 0; i < wedges; ++i)
        {
            const float a0 = (float) i / (float) wedges * juce::MathConstants<float>::twoPi;
            const float a1 = (float) (i + 1) / (float) wedges * juce::MathConstants<float>::twoPi + 0.004f;
            const float lobe = std::pow (std::abs (std::cos (a0 - lightAngle)), 6.0f);
            const float grey = 0.07f + 0.19f * lobe + (rng.nextFloat() - 0.5f) * 0.025f;
            juce::Path w;
            w.startNewSubPath (c);
            w.lineTo (c.getPointOnCircumference (capR, a0));
            w.lineTo (c.getPointOnCircumference (capR, a1));
            w.closeSubPath();
            g.setColour (juce::Colour::greyLevel (juce::jlimit (0.0f, 1.0f, grey)));
            g.fillPath (w);
        }
        // a satin softness over the brushing, darker towards the rim
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, c.x, c.y,
                                                 juce::Colours::black.withAlpha (0.35f), c.x + capR, c.y, true));
        g.fillEllipse (circle (capR));
        // rim of the cap: bright towards the light, dark away from it
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), c.x - capR, c.y - capR,
                                                 juce::Colours::black.withAlpha (0.6f), c.x + capR, c.y + capR, false));
        g.drawEllipse (circle (capR - 0.5f), 1.0f);
    }

    struct KnobCache
    {
        std::map<juce::String, juce::Image> images;
    };
}

void SnaggerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
    if (size < 8.0f)
        return;
    auto r = bounds.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float R = size * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool enabled = s.isEnabled();

    // static body, rendered once per size
    static KnobCache cache;
    const float scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto key = juce::String (size, 1) + (enabled ? "e" : "d") + juce::String (scale, 2);
    auto it = cache.images.find (key);
    if (it == cache.images.end())
    {
        const int px = juce::roundToInt (size * scale);
        juce::Image img (juce::Image::ARGB, px, px, true);
        {
            juce::Graphics ig (img);
            ig.addTransform (juce::AffineTransform::scale (scale));
            renderKnobBody (ig, size, enabled);
        }
        if (cache.images.size() > 32)
            cache.images.clear();
        it = cache.images.emplace (key, img).first;
    }
    g.setOpacity (1.0f);
    g.drawImage (it->second, r);

    // glowing red pointer, from the cap's edge towards the centre
    const float capR = R * 0.69f;
    juce::Path pointer;
    pointer.startNewSubPath (c.getPointOnCircumference (capR * 0.93f, angle));
    pointer.lineTo (c.getPointOnCircumference (capR * 0.42f, angle));
    juce::Path ps;
    juce::PathStrokeType (juce::jmax (1.8f, R * 0.075f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (ps, pointer);
    glowPath (g, ps, col::red, enabled ? 1.0f : 0.3f);
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
