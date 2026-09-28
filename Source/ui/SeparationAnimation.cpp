#include "SeparationAnimation.h"
#include <cmath>

namespace snag
{
using namespace theme;

namespace
{
    float clamp01 (float x)                     { return juce::jlimit (0.0f, 1.0f, x); }
    float lerp (float a, float b, float t)      { return a + (b - a) * t; }
    float smooth (float a, float b, float x)    { x = clamp01 ((x - a) / (b - a)); return x * x * (3.0f - 2.0f * x); }
    float easeOutCubic (float x)                { x = 1.0f - clamp01 (x); return 1.0f - x * x * x; }
    float easeInCubic (float x)                 { x = clamp01 (x); return x * x * x; }
    float easeInOut (float x)
    {
        x = clamp01 (x);
        return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow (-2.0f * x + 2.0f, 3.0f) * 0.5f;
    }
    float easeOutBack (float x)
    {
        x = clamp01 (x) - 1.0f;
        const float c1 = 1.2f, c3 = c1 + 1.0f;
        return 1.0f + c3 * x * x * x + c1 * x * x;
    }

    // stable pseudo-random numbers, so every loop and every frame agree
    float hash (int a, int b)
    {
        auto h = (juce::uint32) a * 374761393u + (juce::uint32) b * 668265263u + 0x9e3779b9u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) ((h ^ (h >> 16)) & 0xffffffu) / 16777216.0f;
    }

    const juce::Colour yellow     { 0xffffd21a };
    const juce::Colour yellowCore { 0xfffff7c8 };
    const juce::Colour yellowGlow { 0xffffae00 };
    const juce::Colour redBody    { 0xffff2a3a };
    const juce::Colour redCore    { 0xffffe2dc };
    const juce::Colour redGlow    { 0xffff0a28 };

    /** Polished gold across a round part, from a to b (lit a little left of centre, like the icon). */
    juce::ColourGradient metal (juce::Point<float> a, juce::Point<float> b, float bright = 1.0f)
    {
        auto c = [bright] (juce::uint32 argb) { return juce::Colour (argb).withMultipliedBrightness (bright); };
        juce::ColourGradient cg (c (0xff2c1f05), a, c (0xff241903), b, false);
        cg.addColour (0.08, c (0xff6e5012));
        cg.addColour (0.24, c (0xffc79a3c));
        cg.addColour (0.36, c (0xfffbe7b0));
        cg.addColour (0.46, c (0xffeec468));
        cg.addColour (0.66, c (0xffb4862a));
        cg.addColour (0.86, c (0xff5c420e));
        return cg;
    }

    /** An upright gold cylinder seen from the side: shaded left to right, lit rim on top. */
    void cylinder (juce::Graphics& g, juce::Rectangle<float> r, float corner, float bright = 1.0f)
    {
        if (r.getWidth() <= 0.0f || r.getHeight() <= 0.0f)
            return;
        const juce::Point<float> a (r.getX(), r.getY()), b (r.getRight(), r.getY());
        g.setGradientFill (metal (a, b, bright));
        g.fillRoundedRectangle (r, corner);
        const float rim = juce::jmax (1.0f, r.getHeight() * 0.13f);
        g.setGradientFill (metal (a, b, bright * 1.3f));
        g.fillRoundedRectangle (r.withHeight (rim), corner);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (r.withTrimmedTop (rim).withHeight (juce::jmax (0.8f, r.getHeight() * 0.03f)));
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (r.withTrimmedTop (r.getHeight() - juce::jmax (0.8f, r.getHeight() * 0.05f)));
    }

    /** A horizontal gold bar (shaded top to bottom). */
    void bar (juce::Graphics& g, juce::Rectangle<float> r, float bright = 1.0f)
    {
        g.setGradientFill (metal ({ r.getX(), r.getY() }, { r.getX(), r.getBottom() }, bright));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, juce::jmax (0.6f, r.getHeight() * 0.05f));
    }

    void bolt (juce::Graphics& g, juce::Point<float> c, float radius)
    {
        auto r = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
        juce::ColourGradient cg (juce::Colour (0xfff4f4f0), c.x - radius * 0.4f, c.y - radius * 0.5f,
                                 juce::Colour (0xff484848), c.x + radius, c.y + radius, true);
        g.setGradientFill (cg);
        g.fillEllipse (r);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawEllipse (r, juce::jmax (0.6f, radius * 0.16f));
    }

    /** A linkage arm between two pivots, shaded like round metal. */
    void arm (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float width)
    {
        juce::Path p;
        p.startNewSubPath (a);
        p.lineTo (b);
        const juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::rounded;
        g.setColour (juce::Colour (0xff3c2b07));
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, cap));
        g.setColour (juce::Colour (0xffb8892c));
        g.strokePath (p, juce::PathStrokeType (width * 0.72f, juce::PathStrokeType::curved, cap));

        const auto d = b - a;
        const float len = d.getDistanceFromOrigin();
        if (len <= 0.0f)
            return;
        juce::Point<float> perp (-d.y / len, d.x / len);
        if (perp.x + perp.y > 0.0f)
            perp = -perp;   // lit from the upper left
        juce::Path h;
        h.startNewSubPath (a + perp * width * 0.17f);
        h.lineTo (b + perp * width * 0.17f);
        g.setColour (juce::Colour (0xfff8e1a0).withAlpha (0.85f));
        g.strokePath (h, juce::PathStrokeType (width * 0.2f, juce::PathStrokeType::curved, cap));
    }

    // Talon outlines in unit coordinates: knuckle at (0, 0), pointing down, +x = away from the middle
    const juce::Path& sideTalon()
    {
        static const juce::Path p = []
        {
            juce::Path t;
            t.startNewSubPath (0.07f, -0.04f);
            t.cubicTo (0.38f, 0.08f, 0.42f, 0.62f, 0.02f, 1.0f);      // outer edge down to the tip
            t.cubicTo (0.19f, 0.64f, 0.17f, 0.24f, -0.08f, 0.05f);    // inner edge back up
            t.closeSubPath();
            return t;
        }();
        return p;
    }
    const juce::Path& sideTalonEdge()
    {
        static const juce::Path p = []
        {
            juce::Path t;
            t.startNewSubPath (0.08f, -0.01f);
            t.cubicTo (0.35f, 0.10f, 0.38f, 0.60f, 0.05f, 0.94f);
            return t;
        }();
        return p;
    }
    const juce::Path& centreTalon()
    {
        static const juce::Path p = []
        {
            juce::Path t;
            t.startNewSubPath (-0.075f, 0.0f);
            t.cubicTo (-0.11f, 0.36f, -0.06f, 0.76f, 0.01f, 1.0f);
            t.cubicTo (0.07f, 0.72f, 0.10f, 0.30f, 0.075f, 0.0f);
            t.closeSubPath();
            return t;
        }();
        return p;
    }

    /** Where a side talon sits: side = +1 right, -1 left. open = 1 spreads it, 0 clamps it shut. */
    juce::AffineTransform talonTransform (float cx, float C, float L, float hubY, float open, int side)
    {
        const float s = (float) side;
        const float angle = lerp (0.26f, -0.42f, open);
        return juce::AffineTransform::scale (s * L, L)
                 .rotated (s * angle)
                 .translated (cx + s * C * 0.18f, hubY + C * 0.4375f);
    }
}

//==============================================================================
SeparationAnimation::SeparationAnimation()
{
    setOpaque (false);
    setInterceptsMouseClicks (true, false);   // nothing underneath is usable while it plays
    setVisible (false);
}

void SeparationAnimation::start()
{
    frozenLoop = frozenBurst = -1.0;
    state = State::looping;
    startMs = juce::Time::getMillisecondCounterHiRes();
    shards.clear();
    burstSparks.clear();
    setVisible (true);
    toFront (false);
    startTimerHz (30);
    repaint();
}

void SeparationAnimation::explode (const juce::Array<juce::Colour>& stemColours)
{
    if (state != State::looping)
    {
        if (onExplodeProgress) onExplodeProgress (1.0f);
        return;
    }
    burstLoopTime = loopTime();
    buildShards (geometry(), stemColours);
    state = State::exploding;
    burstStartMs = juce::Time::getMillisecondCounterHiRes();
    if (! isTimerRunning())
        startTimerHz (30);
    if (onExplodeProgress) onExplodeProgress (0.0f);
    repaint();
}

void SeparationAnimation::stop()
{
    const bool wasActive = state != State::idle;
    state = State::idle;
    frozenLoop = frozenBurst = -1.0;
    stopTimer();
    setVisible (false);
    shards.clear();
    burstSparks.clear();
    yellowTrack = {};
    redTrack = {};
    backdrop = {};
    imageScale = 0.0f;
    if (wasActive && onExplodeProgress)
        onExplodeProgress (1.0f);
}

void SeparationAnimation::setStatus (const juce::String& text, float p)
{
    if (text == status && std::abs (p - progress) < 0.001f)
        return;
    status = text;
    progress = p;
    if (state == State::looping && ! isTimerRunning())
        repaint();
}

void SeparationAnimation::freezeAt (double loopSeconds, double burstSeconds, const juce::Array<juce::Colour>& colours)
{
    stopTimer();
    state = State::looping;
    frozenLoop = juce::jlimit (0.0, loopLength, loopSeconds);
    frozenBurst = -1.0;
    setVisible (true);
    if (burstSeconds >= 0.0)
    {
        burstLoopTime = frozenLoop;
        buildShards (geometry(), colours);
        state = State::exploding;
        frozenBurst = burstSeconds;
    }
    repaint();
}

double SeparationAnimation::loopTime() const
{
    if (frozenLoop >= 0.0)
        return frozenLoop;
    if (state == State::exploding)
        return burstLoopTime;
    return std::fmod ((juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0, loopLength);
}

double SeparationAnimation::burstTime() const
{
    if (frozenBurst >= 0.0)
        return frozenBurst;
    return (juce::Time::getMillisecondCounterHiRes() - burstStartMs) / 1000.0;
}

void SeparationAnimation::timerCallback()
{
    if (state == State::exploding)
    {
        const double bt = burstTime();
        if (onExplodeProgress)
            onExplodeProgress ((float) juce::jmin (1.0, bt / burstLength));
        if (bt >= burstLength)
        {
            state = State::exploding;   // (stop() reports 1.0 again - harmless)
            stop();
            return;
        }
    }
    if (isShowing())
        repaint();
}

void SeparationAnimation::resized()
{
    bars.clear();
    buildBars (geometry());
    lastResizeMs = juce::Time::getMillisecondCounterHiRes();   // (the cached images are redrawn once it settles)
}

//==============================================================================
SeparationAnimation::Geo SeparationAnimation::geometry() const
{
    Geo gm;
    gm.window = getLocalBounds().toFloat().reduced (3.0f);
    auto inner = gm.window.reduced (12.0f, 8.0f);
    gm.status = inner.removeFromBottom (juce::jlimit (38.0f, 58.0f, inner.getHeight() * 0.15f));
    gm.scene = inner;

    const auto& s = gm.scene;
    gm.C = juce::jmax (30.0f, juce::jmin (s.getHeight() * 0.80f, s.getWidth() * 0.34f));
    gm.L = gm.C * 0.55f;
    gm.A = juce::jmin (s.getHeight() * 0.16f, gm.C * 0.19f);
    gm.cx = s.getCentreX();
    gm.trackY = s.getY() + s.getHeight() * 0.63f;
    gm.x0 = s.getX() + s.getWidth() * 0.02f;
    gm.x1 = s.getRight() - s.getWidth() * 0.02f;
    gm.secX0 = gm.cx - gm.C * 0.24f;
    gm.secX1 = gm.cx + gm.C * 0.24f;
    gm.grabHubY = gm.trackY - gm.C * 0.68f;
    gm.hiddenHubY = gm.window.getY() - gm.C * 1.02f;
    gm.awayHubY = gm.window.getY() - gm.C * 1.1f - 8.0f;

    if (bars.size() > 2 && grab1 > grab0)
    {
        gm.secX0 = barX (gm, grab0);
        gm.secX1 = barX (gm, grab1);
    }
    return gm;
}

float SeparationAnimation::barX (const Geo& gm, int i) const
{
    return gm.x0 + (gm.x1 - gm.x0) * (float) i / (float) juce::jmax (1, (int) bars.size() - 1);
}

void SeparationAnimation::buildBars (const Geo& gm)
{
    const float w = gm.x1 - gm.x0;
    if (w < 20.0f)
        return;
    const int n = juce::jlimit (48, 460, (int) (w / 4.2f));
    bars.assign ((size_t) n, 0.0f);

    // a few phrases of different loudness - like the waveform in the icon - loudest where the claw grabs
    struct Bump { float pos, width, amp; };
    const Bump bumps[] = { { 0.07f, 0.06f, 0.55f }, { 0.19f, 0.05f, 0.95f }, { 0.31f, 0.07f, 0.6f }, { 0.5f, 0.07f, 1.0f },
                           { 0.64f, 0.05f, 0.75f }, { 0.78f, 0.06f, 0.95f }, { 0.93f, 0.06f, 0.5f } };
    juce::Random r (0x5ec7);
    for (int i = 0; i < n; ++i)
    {
        const float u = (float) i / (float) (n - 1);
        float env = 0.16f;
        for (auto& b : bumps)
            env += b.amp * std::exp (-((u - b.pos) / b.width) * ((u - b.pos) / b.width));
        env = juce::jmin (env, 1.05f);
        float jag = 0.3f + 0.7f * r.nextFloat();
        if (r.nextFloat() < 0.08f)
            jag = 1.0f;   // the odd tall spike
        bars[(size_t) i] = juce::jlimit (0.05f, 1.0f, env * jag);
    }

    auto index = [&] (float x) { return (x - gm.x0) / w * (float) (n - 1); };
    grab0 = juce::jlimit (1, n - 3, (int) std::ceil (index (gm.cx - gm.C * 0.24f)));
    grab1 = juce::jlimit (grab0 + 2, n - 1, (int) std::floor (index (gm.cx + gm.C * 0.24f)));
}

juce::Path SeparationAnimation::wavePath (const Geo& gm, int from, int to) const
{
    juce::Path p;
    from = juce::jmax (0, from);
    to = juce::jmin ((int) bars.size() - 1, to);
    for (int i = from; i <= to; ++i)
    {
        const float y = gm.trackY + (i % 2 == 0 ? -1.0f : 1.0f) * bars[(size_t) i] * gm.A;
        if (i == from) p.startNewSubPath (barX (gm, i), y);
        else           p.lineTo (barX (gm, i), y);
    }
    return p;
}

void SeparationAnimation::ensureImages (const Geo& gm, float scale)
{
    imageMap = backdropMap = {};
    if (yellowTrack.isValid() && std::abs (imageScale - scale) < 0.01f && imageSize == getLocalBounds())
        return;

    // while the window is being dragged bigger or smaller, stretch what we have rather than
    // re-rendering the glow on every step; it's redrawn crisply once the size settles
    if (yellowTrack.isValid() && backdrop.isValid() && isTimerRunning()
         && juce::Time::getMillisecondCounterHiRes() - lastResizeMs < 250.0)
    {
        imageMap = juce::AffineTransform::translation (-imageX0, -imageTrackY)
                     .scaled ((gm.x1 - gm.x0) / juce::jmax (1.0f, imageX1 - imageX0), gm.A / juce::jmax (0.1f, imageA))
                     .translated (gm.x0, gm.trackY);
        backdropMap = juce::AffineTransform::scale ((float) getWidth() / (float) juce::jmax (1, imageSize.getWidth()),
                                                    (float) getHeight() / (float) juce::jmax (1, imageSize.getHeight()));
        return;
    }

    imageScale = scale;
    imageSize = getLocalBounds();

    // only the band around the track is stored (the glow reaches ~20 px past the peaks)
    const float pad = 22.0f;
    const juce::Rectangle<float> band (0.0f, gm.trackY - gm.A - pad, (float) getWidth(), (gm.A + pad) * 2.0f);
    const int w = juce::roundToInt (band.getWidth() * scale), h = juce::roundToInt (band.getHeight() * scale);
    if (w < 4 || h < 4 || bars.empty())
        return;

    auto path = wavePath (gm, 0, (int) bars.size() - 1);
    path.applyTransform (juce::AffineTransform::translation (-band.getX(), -band.getY()).scaled (scale));
    juce::Path stroked;
    juce::PathStrokeType (1.5f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroked, path);

    const float fx0 = gm.x0 * scale, fx1 = gm.x1 * scale, fade = (gm.x1 - gm.x0) * 0.07f * scale;

    auto render = [&] (juce::Colour glow, juce::Colour body, juce::Colour core)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        {
            juce::Graphics g (img);
            juce::DropShadow (glow.withAlpha (0.55f), juce::roundToInt (15.0f * scale), {}).drawForPath (g, stroked);
            juce::DropShadow (glow.withAlpha (0.95f), juce::roundToInt (4.0f * scale), {}).drawForPath (g, stroked);
            g.setColour (body);
            g.fillPath (stroked);
            g.setColour (core);
            g.strokePath (path, juce::PathStrokeType (0.65f * scale));
        }
        // fade the ends of the track into the glass
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
        for (int x = 0; x < w; ++x)
        {
            const float a = smooth (fx0, fx0 + fade, (float) x) * (1.0f - smooth (fx1 - fade, fx1, (float) x));
            if (a >= 0.999f)
                continue;
            for (int y = 0; y < h; ++y)
                reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y))->multiplyAlpha (a);
        }
        return img;
    };

    yellowTrack = render (yellowGlow, yellow, yellowCore);
    redTrack    = render (redGlow, redBody, redCore);
    imageOrigin = band.getPosition();
    imageX0 = gm.x0;
    imageX1 = gm.x1;
    imageTrackY = gm.trackY;
    imageA = gm.A;

    // the window behind it all: black glass, lit softly from above (never changes, so it's one blit a frame)
    backdrop = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) getWidth() * scale),
                            juce::roundToInt ((float) getHeight() * scale), true);
    {
        juce::Graphics g (backdrop);
        g.addTransform (juce::AffineTransform::scale (scale));
        juce::Path shape;
        shape.addRoundedRectangle (gm.window, 10.0f);
        g.setColour (col::bg0);
        g.fillPath (shape);
        glassWindow (g, gm.window, 10.0f, 1.0f);
        g.reduceClipRegion (shape);
        const juce::Point<float> lamp (gm.cx, gm.scene.getY());
        g.setGradientFill (juce::ColourGradient (col::gold.withAlpha (0.08f), lamp,
                                                 col::gold.withAlpha (0.0f), lamp.translated (gm.C * 1.3f, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (gm.C * 2.6f, gm.C * 2.6f).withCentre (lamp));
    }
}

void SeparationAnimation::drawTrackPart (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> clip,
                                         float alpha, juce::AffineTransform move, bool excludeClip) const
{
    if (! img.isValid() || alpha <= 0.002f)
        return;
    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (move);
    if (excludeClip)
        g.excludeClipRegion (clip.getSmallestIntegerContainer());
    else if (! clip.isEmpty())
        g.reduceClipRegion (clip.getSmallestIntegerContainer());
    g.setOpacity (alpha);
    g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / imageScale).translated (imageOrigin).followedBy (imageMap));
}

//==============================================================================
SeparationAnimation::Pose SeparationAnimation::poseAt (double time, const Geo& gm) const
{
    Pose p;
    const float t = (float) time, C = gm.C;

    if (t < 1.05f)                              // the claw comes down, open
    {
        p.hubY = lerp (gm.hiddenHubY, gm.grabHubY, easeOutBack (t / 1.05f));
        p.open = 1.0f;
    }
    else if (t < 1.95f)                         // clutches the track; the section turns red
    {
        const float tension = smooth (1.55f, 1.95f, t);
        p.hubY = gm.grabHubY - C * 0.035f * tension + std::sin (t * 95.0f) * C * 0.004f * tension;
        p.open = 1.0f - easeInOut ((t - 1.05f) / 0.4f);
        p.red = smooth (1.2f, 1.7f, t);
        p.holding = true;
        p.lift = gm.grabHubY - p.hubY;
    }
    else if (t < 3.0f)                          // pulls it out
    {
        const float k = easeInCubic ((t - 1.95f) / 1.0f);
        p.hubY = lerp (gm.grabHubY - C * 0.035f, gm.awayHubY, k);
        p.open = 0.0f;
        p.red = 1.0f;
        p.holding = true;
        p.gap = true;
        p.lift = gm.grabHubY - p.hubY;
        const float swing = smooth (1.95f, 2.3f, t);
        p.sway = std::sin ((t - 1.95f) * 11.0f) * C * 0.014f * swing;
        p.tilt = std::sin ((t - 1.95f) * 8.0f + 0.6f) * 0.04f * swing;
        p.edgeGlow = 1.0f - smooth (2.0f, 2.95f, t);
    }
    else                                        // the track grows back, ready for the next grab
    {
        p.hubY = gm.awayHubY;
        p.open = 0.3f;
        p.refill = easeInOut ((t - 3.05f) / 0.65f);
        p.gap = p.refill < 1.0f;
    }
    return p;
}

//==============================================================================
void SeparationAnimation::drawClawBody (juce::Graphics& g, const Geo& gm, float hubY, float open) const
{
    const float C = gm.C, cx = gm.cx;
    if (hubY + C * 1.0f < gm.window.getY() - 2.0f)
        return;   // entirely above the window

    // braided hose, like the icon
    {
        juce::Path hose;
        hose.startNewSubPath (cx - C * 0.13f, hubY + C * 0.05f);
        hose.cubicTo (cx - C * 0.30f, hubY + C * 0.02f, cx - C * 0.31f, hubY - C * 0.25f, cx - C * 0.23f, hubY - C * 1.0f);
        g.setColour (juce::Colour (0xff161616));
        g.strokePath (hose, juce::PathStrokeType (C * 0.034f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path braid;
        const float dashes[] = { C * 0.012f, C * 0.010f };
        juce::PathStrokeType (C * 0.022f).createDashedStroke (braid, hose, dashes, 2);
        g.setColour (juce::Colour (0xff85847f).withAlpha (0.75f));
        g.fillPath (braid);
    }

    // the rod it hangs from
    const float top = gm.window.getY() - 4.0f;
    if (hubY > top)
    {
        const juce::Rectangle<float> rodR (cx - C * 0.045f, top, C * 0.09f, hubY - top + C * 0.02f);
        g.setGradientFill (metal (rodR.getTopLeft(), rodR.getTopRight()));
        g.fillRect (rodR);
    }

    // hub: collar, body with grooves, lower collar, neck
    cylinder (g, { cx - C * 0.15f, hubY, C * 0.30f, C * 0.09f }, C * 0.012f);
    const juce::Rectangle<float> body (cx - C * 0.118f, hubY + C * 0.09f, C * 0.236f, C * 0.21f);
    cylinder (g, body, C * 0.006f, 0.95f);
    for (float f : { 0.15f, 0.23f })
    {
        const float y = hubY + C * f;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (body.getX(), y, body.getWidth(), juce::jmax (1.0f, C * 0.008f));
        g.setGradientFill (metal (body.getTopLeft(), body.getTopRight(), 1.35f));
        g.fillRect (body.getX(), y + juce::jmax (1.0f, C * 0.008f), body.getWidth(), juce::jmax (0.8f, C * 0.005f));
    }
    cylinder (g, { cx - C * 0.145f, hubY + C * 0.30f, C * 0.29f, C * 0.07f }, C * 0.01f);
    cylinder (g, { cx - C * 0.045f, hubY + C * 0.37f, C * 0.09f, C * 0.05f }, 0.0f, 0.9f);

    // linkage arms from the hub out to each talon
    for (int side : { -1, 1 })
    {
        const auto tr = talonTransform (cx, C, gm.L, hubY, open, side);
        arm (g, { cx + (float) side * C * 0.12f, hubY + C * 0.13f }, juce::Point<float> (0.2f, 0.12f).transformedBy (tr), C * 0.05f);
    }

    // cross bracket carrying the talon pivots
    bar (g, { cx - C * 0.205f, hubY + C * 0.41f, C * 0.41f, C * 0.055f }, 0.95f);
}

void SeparationAnimation::drawTalons (juce::Graphics& g, const Geo& gm, float hubY, float open, float red,
                                      juce::Point<float> glowCentre) const
{
    const float C = gm.C, cx = gm.cx;
    if (hubY + C * 1.0f < gm.window.getY() - 2.0f)
        return;

    juce::Path all;
    for (int side : { -1, 1 })
    {
        const auto tr = talonTransform (cx, C, gm.L, hubY, open, side);
        auto p = sideTalon();
        p.applyTransform (tr);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (p, juce::AffineTransform::translation (C * 0.012f, C * 0.02f));

        // light on the outer bulge, dark on the inside of the curve
        juce::ColourGradient cg (juce::Colour (0xfff7dc8c), juce::Point<float> (0.34f, 0.40f).transformedBy (tr),
                                 juce::Colour (0xff3e2b06), juce::Point<float> (0.02f, 0.46f).transformedBy (tr), false);
        cg.addColour (0.35, juce::Colour (0xffd1a043));
        cg.addColour (0.72, juce::Colour (0xff7a5613));
        g.setGradientFill (cg);
        g.fillPath (p);

        auto edge = sideTalonEdge();
        edge.applyTransform (tr);
        g.setColour (juce::Colour (0xfffff0c0).withAlpha (0.7f));
        g.strokePath (edge, juce::PathStrokeType (juce::jmax (0.8f, C * 0.009f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xff2a1c04).withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (juce::jmax (0.6f, C * 0.004f)));
        all.addPath (p);
    }

    // the front talon swings towards you as the claw opens, so it looks shorter
    {
        const float len = C * 0.5f * lerp (1.0f, 0.74f, open);
        auto p = centreTalon();
        p.applyTransform (juce::AffineTransform::scale (C * 0.55f, len).translated (cx, hubY + C * 0.44f));
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillPath (p, juce::AffineTransform::translation (C * 0.01f, C * 0.015f));
        g.setGradientFill (metal ({ cx - C * 0.055f, 0.0f }, { cx + C * 0.055f, 0.0f }, 0.85f));
        g.fillPath (p);
        g.setColour (juce::Colour (0xff2a1c04).withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (juce::jmax (0.6f, C * 0.004f)));
        all.addPath (p);
    }

    // the red section lights the gold up from inside
    if (red > 0.01f)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (all);
        g.setGradientFill (juce::ColourGradient (redGlow.withAlpha (0.42f * red), glowCentre,
                                                 redGlow.withAlpha (0.0f), glowCentre.translated (C * 0.4f, 0.0f), true));
        g.fillRect (all.getBounds());
    }

    const float boltR = juce::jmax (1.5f, C * 0.017f);
    for (int side : { -1, 1 })
    {
        const auto tr = talonTransform (cx, C, gm.L, hubY, open, side);
        bolt (g, juce::Point<float> (0.2f, 0.12f).transformedBy (tr), boltR);
        bolt (g, { cx + (float) side * C * 0.18f, hubY + C * 0.4375f }, boltR * 1.1f);
        bolt (g, { cx + (float) side * C * 0.12f, hubY + C * 0.13f }, boltR);
    }
}

void SeparationAnimation::drawScene (juce::Graphics& g, const Geo& gm, const Pose& pose, double t) const
{
    const float C = gm.C;
    const auto sec = gm.section();
    const juce::Point<float> secCentre (sec.getCentreX(), gm.trackY);
    const auto move = juce::AffineTransform::rotation (pose.tilt, secCentre.x, secCentre.y).translated (pose.sway, -pose.lift);
    const auto held = secCentre.translated (pose.sway, -pose.lift);
    const float pulse = 0.85f + 0.15f * std::sin ((float) t * 18.0f);

    // red light spilling onto the glass
    if (pose.red > 0.0f && pose.holding)
    {
        const float r = C * 0.6f;
        g.setGradientFill (juce::ColourGradient (redGlow.withAlpha (0.22f * pose.red * pulse), held,
                                                 redGlow.withAlpha (0.0f), held.translated (r, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 1.5f).withCentre (held));
    }

    // the track (minus the section while the claw has it)
    if (! pose.holding && ! pose.gap)
        drawTrackPart (g, yellowTrack, {}, 1.0f);
    else
    {
        drawTrackPart (g, yellowTrack, sec, 1.0f, {}, true);
        if (pose.gap && pose.refill > 0.0f)
        {
            const auto grown = sec.withWidth (sec.getWidth() * pose.refill);
            drawTrackPart (g, yellowTrack, grown, 1.0f);
            const float fx = grown.getRight();
            const float a = 0.9f * (1.0f - pose.refill * pose.refill);
            g.setGradientFill (juce::ColourGradient (yellowCore.withAlpha (a), fx, gm.trackY,
                                                     yellowGlow.withAlpha (0.0f), fx + gm.A * 0.9f, gm.trackY, true));
            g.fillEllipse (juce::Rectangle<float> (gm.A * 1.8f, gm.A * 2.4f).withCentre ({ fx, gm.trackY }));
        }
    }

    // embers where it tore
    if (pose.edgeGlow > 0.0f)
    {
        for (float ex : { sec.getX(), sec.getRight() })
        {
            const float r = gm.A * 0.55f + C * 0.02f;
            const float flicker = 0.75f + 0.25f * std::sin ((float) t * 41.0f + ex);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc0a0).withAlpha (pose.edgeGlow * flicker), ex, gm.trackY,
                                                     redGlow.withAlpha (0.0f), ex + r, gm.trackY, true));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ ex, gm.trackY }));
        }
    }

    drawClawBody (g, gm, pose.hubY, pose.open);

    if (pose.holding)
    {
        drawTrackPart (g, yellowTrack, sec, 1.0f - pose.red, move);
        drawTrackPart (g, redTrack, sec, pose.red, move);
    }

    drawTalons (g, gm, pose.hubY, pose.open, pose.holding ? pose.red : 0.0f, held);

    // bloom over the claw - the red section is the brightest thing in the room
    if (pose.holding && pose.red > 0.0f)
    {
        const float r = C * 0.5f;
        g.setGradientFill (juce::ColourGradient (redGlow.withAlpha (0.2f * pose.red * pulse), held,
                                                 redGlow.withAlpha (0.0f), held.translated (r, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (held));
        const float r2 = C * 0.15f;
        g.setGradientFill (juce::ColourGradient (redCore.withAlpha (0.16f * pose.red * pulse), held,
                                                 redCore.withAlpha (0.0f), held.translated (r2, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (r2 * 2.0f, r2 * 2.0f).withCentre (held));
    }
}

void SeparationAnimation::drawSparks (juce::Graphics& g, const Geo& gm, double t) const
{
    const float st = (float) t - 1.95f;
    if (st < 0.0f || st > 1.0f)
        return;
    const float C = gm.C, gravity = C * 2.4f;
    for (int side = 0; side < 2; ++side)
    {
        const float ex = side == 0 ? gm.secX0 : gm.secX1;
        const float base = side == 0 ? juce::MathConstants<float>::pi + 0.55f : -0.55f;
        for (int k = 0; k < 16; ++k)
        {
            const int seed = side * 101 + k;
            const float life = 0.3f + 0.45f * hash (seed, 1);
            const float age = st - 0.12f * hash (seed, 2);
            if (age < 0.0f || age > life)
                continue;
            const float ang = base + (hash (seed, 3) - 0.5f) * 1.7f;
            const float speed = C * (1.1f + 1.8f * hash (seed, 4));
            const juce::Point<float> v (std::cos (ang) * speed, std::sin (ang) * speed);
            const juce::Point<float> o (ex, gm.trackY + (hash (seed, 5) - 0.5f) * gm.A * 1.2f);
            const auto pos = o + v * age + juce::Point<float> (0.0f, 0.5f * gravity * age * age);
            const auto vel = v + juce::Point<float> (0.0f, gravity * age);
            const float k01 = age / life;
            g.setColour (yellowCore.interpolatedWith (redBody, smooth (0.0f, 0.7f, k01)).withAlpha (1.0f - k01 * k01));
            g.drawLine ({ pos, pos - vel * 0.022f }, 1.0f + 1.2f * hash (seed, 6));
        }
    }
}

//==============================================================================
void SeparationAnimation::buildShards (const Geo& gm, const juce::Array<juce::Colour>& colours)
{
    shards.clear();
    burstSparks.clear();
    const int n = (int) bars.size();
    if (n < 4)
        return;

    // which part of the track is missing at this moment (pulled out, not grown back yet)
    const auto pose = poseAt (burstLoopTime, gm);
    const bool missing = pose.gap;
    const float regrown = grab0 + (grab1 - grab0) * pose.refill;

    const float halfW = juce::jmax (1.0f, (gm.x1 - gm.x0) * 0.5f);
    const float W = (float) getWidth(), H = (float) getHeight();
    int k = 0;
    for (int i0 = 0; i0 < n - 1; ++k)
    {
        const int len = 3 + (int) (hash (k, 11) * 3.0f);
        const int i1 = juce::jmin (n - 1, i0 + len);
        const float mid = 0.5f * (float) (i0 + i1);
        if (! (missing && mid >= regrown && mid <= (float) grab1))
        {
            Shard s;
            s.origin = { 0.5f * (barX (gm, i0) + barX (gm, i1)), gm.trackY };
            s.shape = wavePath (gm, i0, i1);
            s.shape.applyTransform (juce::AffineTransform::translation (-s.origin.x, -s.origin.y));
            const float dir = (s.origin.x - gm.cx) / halfW;                          // -1 .. 1
            s.velocity = { dir * W * (0.45f + 0.5f * hash (k, 12)) + (hash (k, 13) - 0.5f) * gm.C * 0.8f,
                           (hash (k, 14) - 0.6f) * H * 1.3f };
            s.spin = (hash (k, 15) - 0.5f) * 9.0f;
            s.delay = 0.07f * std::abs (dir) * (0.6f + 0.4f * hash (k, 16));         // it bursts from the middle out
            s.colour = colours.isEmpty() ? (k % 2 == 0 ? yellow : redBody)
                                         : colours[(int) (hash (k, 17) * (float) colours.size()) % colours.size()];
            shards.push_back (std::move (s));
        }
        i0 = i1;
    }

    for (int j = 0; j < 90; ++j)
    {
        Spark sp;
        const float u = hash (j, 21) - 0.5f;
        sp.origin = { gm.cx + u * std::abs (u) * 2.0f * halfW, gm.trackY + (hash (j, 22) - 0.5f) * gm.A };
        const float ang = hash (j, 23) * juce::MathConstants<float>::twoPi;
        const float speed = gm.C * (2.0f + 4.0f * hash (j, 24));
        sp.velocity = { std::cos (ang) * speed, std::sin (ang) * speed * 0.8f };
        sp.life = 0.35f + 0.55f * hash (j, 25);
        sp.width = 1.0f + 1.4f * hash (j, 26);
        const int pick = (int) (hash (j, 27) * 4.0f);
        sp.colour = pick == 0 ? redBody : pick == 1 ? yellowCore
                  : (colours.isEmpty() ? col::goldLight : colours[j % colours.size()].brighter (0.4f));
        burstSparks.push_back (sp);
    }
}

void SeparationAnimation::drawBurst (juce::Graphics& g, const Geo& gm, double time) const
{
    const float b = (float) time, C = gm.C;
    const juce::Point<float> centre (gm.cx, gm.trackY);

    // the claw lets go of everything and heads back up
    {
        const auto pose = poseAt (burstLoopTime, gm);
        const float hubY = lerp (pose.hubY, gm.awayHubY, easeInCubic (b / 0.5f));
        drawClawBody (g, gm, hubY, pose.open);
        if (pose.holding && pose.gap)
        {
            const auto sec = gm.section();
            const float lift = gm.grabHubY - hubY;
            drawTrackPart (g, redTrack, sec, 1.0f,
                           juce::AffineTransform::rotation (pose.tilt, sec.getCentreX(), gm.trackY).translated (pose.sway, -lift));
        }
        drawTalons (g, gm, hubY, pose.open, 0.0f, centre);
    }

    // flash
    if (b < 0.4f)
    {
        const float k = easeOutCubic (b / 0.4f);
        const float r = lerp (0.25f, 1.4f, k) * C;
        juce::ColourGradient cg (juce::Colours::white.withAlpha (0.95f * (1.0f - k)), centre,
                                 yellowGlow.withAlpha (0.0f), centre.translated (r, 0.0f), true);
        cg.addColour (0.3, yellow.withAlpha (0.5f * (1.0f - k)));
        g.setGradientFill (cg);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));
    }

    // shock ring
    if (b < 0.7f)
    {
        const float k = b / 0.7f;
        const float r = lerp (0.1f * C, juce::jmax ((float) getWidth(), (float) getHeight()) * 0.6f, easeOutCubic (k));
        const auto ring = juce::Rectangle<float> (r * 2.0f, r * 0.7f).withCentre (centre);
        g.setColour (yellowGlow.withAlpha (0.14f * (1.0f - k)));
        g.drawEllipse (ring, 4.0f + 8.0f * (1.0f - k));
        g.setColour (yellowCore.withAlpha (0.55f * (1.0f - k)));
        g.drawEllipse (ring, 1.0f + 1.5f * (1.0f - k));
    }

    // the pieces of the sound file, turning into the stems' colours as they fly
    const float fade = 1.0f - smooth (0.45f, 1.0f, b / (float) burstLength);
    const float tint = smooth (0.05f, 0.4f, b);
    const float drag = 2.6f;
    for (auto& s : shards)
    {
        const float age = juce::jmax (0.0f, b - s.delay);
        const float travel = (1.0f - std::exp (-drag * age)) / drag;
        const auto pos = s.origin + s.velocity * travel + juce::Point<float> (0.0f, 0.3f * C * age * age);
        auto p = s.shape;
        p.applyTransform (juce::AffineTransform::rotation (s.spin * travel).translated (pos));
        const auto c = yellow.interpolatedWith (s.colour, tint);
        g.setColour (c.withAlpha (0.16f * fade));
        g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.4f * fade));
        g.strokePath (p, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.brighter (0.7f).withAlpha (fade));
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    for (auto& sp : burstSparks)
    {
        if (b > sp.life)
            continue;
        const float travel = (1.0f - std::exp (-3.2f * b)) / 3.2f;
        const auto pos = sp.origin + sp.velocity * travel;
        const auto vel = sp.velocity * std::exp (-3.2f * b);
        const float k = b / sp.life;
        g.setColour (sp.colour.withAlpha (1.0f - k * k));
        g.drawLine ({ pos, pos - vel * 0.03f }, sp.width);
    }
}

//==============================================================================
void SeparationAnimation::drawStatus (juce::Graphics& g, const Geo& gm, float alpha) const
{
    if (alpha <= 0.01f)
        return;
    auto r = gm.status;
    auto textR = r.removeFromTop (r.getHeight() * 0.58f);

    const auto text = status.isNotEmpty() ? status : juce::String ("Separating");
    const int pct = progress >= 0.0f ? juce::roundToInt (juce::jlimit (0.0f, 1.0f, progress) * 100.0f) : -1;
    const auto font = ui (13.0f, true);
    const auto pctText = pct >= 0 ? "   " + juce::String (pct) + "%" : juce::String();
    const float tw = juce::GlyphArrangement::getStringWidth (font, text);
    const float pw = juce::GlyphArrangement::getStringWidth (font, pctText);
    const float x = juce::jmax (textR.getX(), textR.getCentreX() - (tw + pw) * 0.5f);

    g.setFont (font);
    g.setColour (col::goldPale.withAlpha (alpha));
    g.drawText (text, juce::Rectangle<float> (x, textR.getY(), juce::jmin (tw + 4.0f, textR.getWidth()), textR.getHeight()),
                juce::Justification::centredLeft, true);
    if (pct >= 0)
    {
        g.setColour (col::redHot.withAlpha (alpha));
        g.drawText (pctText, juce::Rectangle<float> (x + tw, textR.getY(), pw + 4.0f, textR.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    // slim progress bar
    const float bw = juce::jmin (420.0f, r.getWidth() * 0.5f);
    const auto barR = juce::Rectangle<float> (bw, 4.0f).withCentre ({ r.getCentreX(), r.getY() + 7.0f });
    g.setColour (juce::Colours::black.withAlpha (0.75f * alpha));
    g.fillRoundedRectangle (barR, 2.0f);
    g.setColour (col::gold.withAlpha (0.3f * alpha));
    g.drawRoundedRectangle (barR.expanded (0.5f), 2.5f, 0.8f);

    juce::Rectangle<float> fill;
    if (pct >= 0)
        fill = barR.withWidth (bw * juce::jlimit (0.0f, 1.0f, progress));
    else
    {
        const float pos = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() / 1400.0, 1.0);
        fill = barR.withWidth (bw * 0.22f).withX (barR.getX() - bw * 0.22f + bw * 1.22f * pos).getIntersection (barR);
    }
    if (fill.getWidth() > 0.5f)
    {
        juce::ColourGradient cg (col::goldDark.withAlpha (alpha), fill.getX(), 0.0f, col::goldLight.withAlpha (alpha), fill.getRight(), 0.0f, false);
        g.setGradientFill (cg);
        g.fillRoundedRectangle (fill, 2.0f);
        const juce::Point<float> head (fill.getRight(), fill.getCentreY());
        g.setGradientFill (juce::ColourGradient (col::redHot.withAlpha (0.9f * alpha), head,
                                                 col::red.withAlpha (0.0f), head.translated (9.0f, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (head));
    }
}

void SeparationAnimation::paint (juce::Graphics& g)
{
    if (state == State::idle || getWidth() < 60 || getHeight() < 60 || bars.empty())
        return;

    const auto gm = geometry();
    ensureImages (gm, juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor()));
    if (! yellowTrack.isValid())
        return;

    const bool bursting = state == State::exploding;
    const double bt = bursting ? burstTime() : 0.0;
    const double t = loopTime();

    // the glass window (fades away during the burst, revealing the stem lanes behind it)
    const float bgAlpha = bursting ? 1.0f - smooth (0.18f, 0.8f, (float) bt) : 1.0f;
    if (bgAlpha > 0.0f && backdrop.isValid())
    {
        g.setOpacity (bgAlpha);
        g.drawImageTransformed (backdrop, juce::AffineTransform::scale (1.0f / imageScale).followedBy (backdropMap));
    }

    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (gm.window.getSmallestIntegerContainer());   // the claw comes in over the window's top edge

        if (! bursting)
        {
            drawScene (g, gm, poseAt (t, gm), t);
            drawSparks (g, gm, t);
        }
        else
            drawBurst (g, gm, bt);

        drawStatus (g, gm, bursting ? 1.0f - smooth (0.0f, 0.25f, (float) bt) : 1.0f);
    }

    if (bgAlpha > 0.0f)
        glassGlare (g, gm.window, 10.0f, 0.6f * bgAlpha);
}

} // namespace snag
