#include "WaveformView.h"
#include "../PluginProcessor.h"
#include "../core/EditOps.h"

namespace snag
{
using namespace theme;

WaveformView::WaveformView (SnaggerProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (false);
    startTimerHz (30);
}

void WaveformView::setClip (Clip::Ptr c)
{
    const bool different = c.get() != clip.get();
    clip = c;
    lastAudio = clip != nullptr ? clip->audio.get() : nullptr;
    if (different)
    {
        selStart = selEnd = cursor = 0;
        zoomToFit();
    }
    else
        clampView();
    waveCache = {};
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void WaveformView::audioChanged()
{
    lastAudio = clip != nullptr ? clip->audio.get() : nullptr;
    const int n = numSamples();
    selStart = juce::jlimit (0, n, selStart);
    selEnd   = juce::jlimit (0, n, selEnd);
    cursor   = juce::jlimit (0, n, cursor);
    if (viewLen > n || viewLen <= 0)
        zoomToFit();
    clampView();
    waveCache = {};
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void WaveformView::setSelection (int s, int e)
{
    const int n = numSamples();
    selStart = juce::jlimit (0, n, juce::jmin (s, e));
    selEnd   = juce::jlimit (0, n, juce::jmax (s, e));
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void WaveformView::clearSelection()
{
    selStart = selEnd = 0;
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void WaveformView::zoomToFit()
{
    viewStart = 0;
    viewLen = juce::jmax (1, numSamples());
    waveCache = {};
    repaint();
    if (onViewChanged) onViewChanged();
}

void WaveformView::zoomToSelection()
{
    if (! hasSelection()) return;
    const double pad = (selEnd - selStart) * 0.05;
    viewStart = selStart - pad;
    viewLen = (selEnd - selStart) + 2 * pad;
    clampView();
    waveCache = {};
    repaint();
    if (onViewChanged) onViewChanged();
}

void WaveformView::zoomBy (double factor, double anchorSample)
{
    const int n = numSamples();
    if (n <= 0) return;
    const double minLen = juce::jmax (64.0, getWaveArea().getWidth() * 0.5);
    const double newLen = juce::jlimit (minLen, (double) n, viewLen * factor);
    const double frac = (anchorSample - viewStart) / viewLen;
    viewStart = anchorSample - frac * newLen;
    viewLen = newLen;
    clampView();
    waveCache = {};
    repaint();
    if (onViewChanged) onViewChanged();
}

void WaveformView::setViewStart (double s)
{
    viewStart = s;
    clampView();
    waveCache = {};
    repaint();
    if (onViewChanged) onViewChanged();
}

void WaveformView::clampView()
{
    const int n = numSamples();
    if (n <= 0) { viewStart = 0; viewLen = 1; return; }
    viewLen = juce::jlimit (1.0, (double) n, viewLen);
    viewStart = juce::jlimit (0.0, (double) n - viewLen, viewStart);
}

juce::Rectangle<float> WaveformView::getWaveArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f).withTrimmedTop (22.0f);
}

double WaveformView::xToSample (float x) const
{
    auto a = getWaveArea();
    return viewStart + (double) (x - a.getX()) / (double) a.getWidth() * viewLen;
}

float WaveformView::sampleToX (double s) const
{
    auto a = getWaveArea();
    return a.getX() + (float) ((s - viewStart) / viewLen) * a.getWidth();
}

int WaveformView::markerAt (float x) const
{
    if (clip == nullptr) return -1;
    int best = -1;
    float bestD = 6.0f;
    for (size_t i = 0; i < clip->slices.size(); ++i)
    {
        const float d = std::abs (sampleToX (clip->slices[i]) - x);
        if (d < bestD) { bestD = d; best = (int) i; }
    }
    return best;
}

void WaveformView::resized()
{
    waveCache = {};
}

//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    glossPanel (g, bounds, 8.0f, col::bg2, col::bg0, false, 0.03f);

    auto area = getWaveArea();
    auto ruler = bounds.reduced (1.0f).withHeight (22.0f);

    if (clip == nullptr || clip->audio == nullptr)
    {
        g.setColour (col::textDim);
        g.setFont (display (18.0f));
        g.drawText ("No sample loaded", area, juce::Justification::centred);
        g.setFont (ui (12.0f));
        g.setColour (col::textFaint);
        g.drawText ("Capture something in BROWSE, drop a file here, or pick a clip from the tray below",
                    area.translated (0, 26.0f), juce::Justification::centred);
        return;
    }

    const auto& audio = *clip->audio;
    const double sr = audio.sampleRate;

    // centre line + grid
    g.setColour (col::lineSoft);
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

    // ruler ticks
    {
        const double secsVisible = viewLen / sr;
        const double targetTicks = juce::jmax (2.0, area.getWidth() / 90.0);
        const double rawStep = secsVisible / targetTicks;
        const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 };
        double step = 300;
        for (auto s : steps) if (s >= rawStep) { step = s; break; }

        g.setFont (ui (9.5f, true));
        const double t0 = std::ceil ((viewStart / sr) / step) * step;
        for (double t = t0; t < (viewStart + viewLen) / sr; t += step)
        {
            const float x = sampleToX (t * sr);
            g.setColour (col::line);
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            g.setColour (col::gold.withAlpha (0.55f));
            g.drawVerticalLine ((int) x, ruler.getBottom() - 6.0f, ruler.getBottom());
            g.setColour (col::textDim);
            g.drawText (step < 1.0 ? formatTime (t, true) : formatTime (t), juce::Rectangle<float> (x + 3.0f, ruler.getY(), 70.0f, ruler.getHeight() - 4.0f),
                        juce::Justification::centredLeft);
        }
        g.setColour (col::line);
        g.drawHorizontalLine ((int) ruler.getBottom(), ruler.getX(), ruler.getRight());
    }

    // waveform (cached)
    {
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int w = (int) (area.getWidth() * scale), h = (int) (area.getHeight() * scale);
        if (waveCache.isNull() || cacheKey.audio != clip->audio.get() || ! juce::exactlyEqual (cacheKey.start, viewStart)
            || ! juce::exactlyEqual (cacheKey.len, viewLen) || cacheKey.w != w || cacheKey.h != h)
        {
            waveCache = juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
            juce::Graphics wg (waveCache);
            drawWaveform (wg, juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0, 6.0f * (float) scale), audio,
                          (int) viewStart, (int) (viewStart + viewLen), col::gold, true);
            cacheKey = { clip->audio.get(), viewStart, viewLen, w, h };
        }
        g.drawImage (waveCache, area, juce::RectanglePlacement::stretchToFit);
    }

    // selection
    if (hasSelection())
    {
        const float x0 = juce::jmax (area.getX(), sampleToX (selStart));
        const float x1 = juce::jmin (area.getRight(), sampleToX (selEnd));
        if (x1 > x0)
        {
            auto sel = juce::Rectangle<float> (x0, area.getY(), x1 - x0, area.getHeight());
            g.setColour (col::red.withAlpha (0.13f));
            g.fillRect (sel);
            neonLine (g, { x0, area.getY(), x0, area.getBottom() }, col::red, 1.2f, 6.0f);
            neonLine (g, { x1, area.getY(), x1, area.getBottom() }, col::red, 1.2f, 6.0f);

            auto label = formatTime ((selEnd - selStart) / sr, true) + "s";
            g.setFont (ui (10.0f, true));
            auto lr = juce::Rectangle<float> (x0 + 4.0f, area.getBottom() - 20.0f, 90.0f, 16.0f);
            g.setColour (col::bg0.withAlpha (0.7f));
            g.fillRoundedRectangle (lr.withWidth (juce::GlyphArrangement::getStringWidth (ui (10.0f, true), label) + 10.0f), 3.0f);
            g.setColour (col::redHot);
            g.drawText (label, lr.withTrimmedLeft (5.0f), juce::Justification::centredLeft);
        }
    }

    // slice markers
    {
        auto bounds2 = clip->sliceBoundaries();
        const int root = processor.rootNote.load();
        g.setFont (ui (9.5f, true));
        for (size_t i = 0; i + 1 < bounds2.size(); ++i)
        {
            const int s = bounds2[i];
            const float x = sampleToX (s);
            if (x < area.getX() - 40 || x > area.getRight())
                continue;

            const bool isMarker = i > 0;
            const bool hot = isMarker && (int) i - 1 == hoverMarker;
            if (isMarker)
            {
                g.setColour ((hot ? col::goldLight : col::gold).withAlpha (hot ? 1.0f : 0.75f));
                g.drawLine (x, area.getY(), x, area.getBottom(), hot ? 1.6f : 1.0f);
            }
            if (bounds2.size() > 2)
            {
                auto tag = juce::Rectangle<float> (x, area.getY() + 2.0f, 34.0f, 15.0f);
                juce::Path tp;
                tp.addRoundedRectangle (tag.getX(), tag.getY(), tag.getWidth(), tag.getHeight(), 3.0f, 3.0f, false, true, false, true);
                g.setGradientFill (goldGradient (tag));
                g.fillPath (tp);
                g.setColour (col::bg0);
                g.drawText (juce::String ((int) i + 1) + " " + noteName (root + (int) i), tag.withTrimmedLeft (3.0f), juce::Justification::centredLeft);
            }
        }
    }

    // hover line
    if (hoverX >= area.getX() && drag == Drag::none)
    {
        g.setColour (col::goldPale.withAlpha (0.25f));
        g.drawVerticalLine ((int) hoverX, area.getY(), area.getBottom());
    }

    // cursor (play start)
    {
        const float x = sampleToX (cursor);
        if (x >= area.getX() && x <= area.getRight())
        {
            g.setColour (col::goldLight.withAlpha (0.8f));
            juce::Path tri;
            tri.addTriangle (x - 5.0f, area.getY(), x + 5.0f, area.getY(), x, area.getY() + 7.0f);
            g.fillPath (tri);
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        }
    }

    // playhead
    if (processor.isPreviewing() && processor.getPreviewSource() == clip->audio.get())
    {
        const float x = sampleToX (processor.getPreviewPosition());
        if (x >= area.getX() && x <= area.getRight())
            neonLine (g, { x, area.getY(), x, area.getBottom() }, col::red, 1.6f, 9.0f);
    }
}

void WaveformView::timerCallback()
{
    if (clip != nullptr && clip->audio.get() != lastAudio)
        audioChanged();

    const bool playing = processor.isPreviewing() && clip != nullptr && processor.getPreviewSource() == clip->audio.get();
    const double ph = playing ? processor.getPreviewPosition() : -1.0;
    if (! juce::exactlyEqual (ph, lastPlayhead))
    {
        lastPlayhead = ph;
        repaint();
    }
}

//==============================================================================
void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (clip == nullptr) return;
    const auto area = getWaveArea();
    const int n = numSamples();
    const int s = juce::jlimit (0, n, (int) xToSample ((float) e.x));

    if (e.mods.isPopupMenu())
    {
        const int m = markerAt ((float) e.x);
        juce::PopupMenu menu;
        menu.addItem (1, "Add chop marker here");
        menu.addItem (2, "Delete this marker", m >= 0);
        menu.addItem (3, "Clear all markers", ! clip->slices.empty());
        menu.addSeparator();
        menu.addItem (4, "Zoom to selection", hasSelection());
        menu.addItem (5, "Zoom to fit");
        juce::Component::SafePointer<WaveformView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [safe, s, m] (int r)
        {
            if (safe == nullptr || safe->clip == nullptr) return;
            auto& c = *safe->clip;
            if (r == 1)      { c.pushUndo ("Add marker"); c.slices.push_back (s); std::sort (c.slices.begin(), c.slices.end()); }
            else if (r == 2) { c.pushUndo ("Delete marker"); c.slices.erase (c.slices.begin() + m); }
            else if (r == 3) { c.pushUndo ("Clear markers"); c.slices.clear(); }
            else if (r == 4) { safe->zoomToSelection(); return; }
            else if (r == 5) { safe->zoomToFit(); return; }
            else return;
            safe->repaint();
            if (safe->onSlicesChanged) safe->onSlicesChanged();
        });
        return;
    }

    if (e.y < area.getY())   // ruler: scrub / set cursor
    {
        drag = Drag::scrub;
        cursor = s;
        repaint();
        return;
    }

    // markers take priority
    const int m = markerAt ((float) e.x);
    if (m >= 0 && ! e.mods.isShiftDown())
    {
        clip->pushUndo ("Move marker");
        drag = Drag::marker;
        dragMarker = m;
        return;
    }

    // selection edges
    if (hasSelection())
    {
        if (std::abs (sampleToX (selStart) - (float) e.x) < 5.0f) { drag = Drag::selStartEdge; return; }
        if (std::abs (sampleToX (selEnd) - (float) e.x) < 5.0f)   { drag = Drag::selEndEdge; return; }
    }

    if (e.mods.isAltDown())
    {
        clip->pushUndo ("Add marker");
        clip->slices.push_back (edit::snapToZeroCrossing (*clip->audio, s, 64));
        std::sort (clip->slices.begin(), clip->slices.end());
        repaint();
        if (onSlicesChanged) onSlicesChanged();
        return;
    }

    if (e.mods.isShiftDown() && hasSelection())
    {
        // extend selection
        if (std::abs (s - selStart) < std::abs (s - selEnd)) selStart = s; else selEnd = s;
        drag = s < selStart + (selEnd - selStart) / 2 ? Drag::selStartEdge : Drag::selEndEdge;
        repaint();
        if (onSelectionChanged) onSelectionChanged();
        return;
    }

    drag = Drag::select;
    anchor = s;
    cursor = s;
    selStart = selEnd = s;
    repaint();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (clip == nullptr) return;
    const int n = numSamples();
    const int s = juce::jlimit (0, n, (int) xToSample ((float) e.x));

    // auto-scroll when dragging outside
    const auto area = getWaveArea();
    if (drag == Drag::select || drag == Drag::selStartEdge || drag == Drag::selEndEdge)
    {
        if (e.x > area.getRight())      setViewStart (viewStart + viewLen * 0.02);
        else if (e.x < area.getX())     setViewStart (viewStart - viewLen * 0.02);
    }

    switch (drag)
    {
        case Drag::select:
            selStart = juce::jmin (anchor, s);
            selEnd   = juce::jmax (anchor, s);
            break;
        case Drag::selStartEdge:
            selStart = juce::jmin (s, selEnd);
            break;
        case Drag::selEndEdge:
            selEnd = juce::jmax (s, selStart);
            break;
        case Drag::marker:
            if (dragMarker >= 0 && dragMarker < (int) clip->slices.size())
                clip->slices[(size_t) dragMarker] = juce::jlimit (1, n - 1, s);
            break;
        case Drag::scrub:
            cursor = s;
            break;
        case Drag::none:
            break;
    }
    repaint();
    if (drag != Drag::marker && drag != Drag::scrub && onSelectionChanged) onSelectionChanged();
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (clip == nullptr) { drag = Drag::none; return; }

    if (drag == Drag::marker)
    {
        std::sort (clip->slices.begin(), clip->slices.end());
        clip->slices.erase (std::unique (clip->slices.begin(), clip->slices.end()), clip->slices.end());
        if (onSlicesChanged) onSlicesChanged();
    }
    else if (drag == Drag::select && ! hasSelection())
    {
        selStart = selEnd = 0;
        if (onSelectionChanged) onSelectionChanged();
        if (e.getNumberOfClicks() == 1 && onPlayFrom && processor.isPreviewing())
            onPlayFrom (cursor);
    }
    else if (drag == Drag::scrub && onPlayFrom)
    {
        onPlayFrom (cursor);
    }

    drag = Drag::none;
    dragMarker = -1;
    repaint();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (clip == nullptr) return;
    const int m = markerAt ((float) e.x);
    if (m >= 0)
    {
        clip->pushUndo ("Delete marker");
        clip->slices.erase (clip->slices.begin() + m);
        repaint();
        if (onSlicesChanged) onSlicesChanged();
        return;
    }

    // double-click selects the slice under the mouse
    const int s = (int) xToSample ((float) e.x);
    auto b = clip->sliceBoundaries();
    for (size_t i = 0; i + 1 < b.size(); ++i)
        if (s >= b[i] && s < b[i + 1])
        {
            setSelection (b[i], b[i + 1]);
            break;
        }
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    hoverX = (float) e.x;
    const int m = markerAt ((float) e.x);
    if (m != hoverMarker) hoverMarker = m;

    bool onEdge = hasSelection() && (std::abs (sampleToX (selStart) - (float) e.x) < 5.0f || std::abs (sampleToX (selEnd) - (float) e.x) < 5.0f);
    setMouseCursor (m >= 0 || onEdge ? juce::MouseCursor::LeftRightResizeCursor
                                     : (e.y < getWaveArea().getY() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::IBeamCursor));
    repaint();
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    hoverX = -1.0f;
    hoverMarker = -1;
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (clip == nullptr) return;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown() || std::abs (w.deltaY) > std::abs (w.deltaX) * 2.0f)
    {
        if (e.mods.isShiftDown())
            setViewStart (viewStart - w.deltaY * viewLen * 0.3);
        else
            zoomBy (w.deltaY > 0 ? 0.8 : 1.25, xToSample ((float) e.x));
    }
    else
        setViewStart (viewStart - w.deltaX * viewLen * 0.3);
}

void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (clip != nullptr && scaleFactor > 0)
        zoomBy (1.0 / scaleFactor, xToSample ((float) e.x));
}

//==============================================================================
void WaveOverview::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    glossPanel (g, r, 5.0f, col::bg2, col::bg0, false, 0.03f);
    auto clip = view.getClip();
    if (clip == nullptr || clip->audio == nullptr)
        return;

    auto inner = r.reduced (2.0f);
    thumb.draw (g, inner, clip->audio.get(), col::goldDark.brighter (0.3f));

    const double n = clip->audio->getNumSamples();
    if (view.hasSelection())
    {
        const float x0 = inner.getX() + (float) (view.getSelectionStart() / n) * inner.getWidth();
        const float x1 = inner.getX() + (float) (view.getSelectionEnd() / n) * inner.getWidth();
        g.setColour (col::red.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (x0, inner.getY(), juce::jmax (1.0f, x1 - x0), inner.getHeight()));
    }

    const float vx = inner.getX() + (float) (view.getViewStart() / n) * inner.getWidth();
    const float vw = juce::jmax (4.0f, (float) (view.getViewLength() / n) * inner.getWidth());
    auto vr = juce::Rectangle<float> (vx, r.getY() + 1.0f, vw, r.getHeight() - 2.0f);
    g.setColour (col::gold.withAlpha (0.08f));
    g.fillRoundedRectangle (vr, 3.0f);
    goldBorder (g, vr, 3.0f, 1.0f, 0.9f);
}

void WaveOverview::mouseDown (const juce::MouseEvent& e)
{
    auto clip = view.getClip();
    if (clip == nullptr || clip->audio == nullptr) return;
    const double n = clip->audio->getNumSamples();
    const double s = (double) e.x / getWidth() * n;
    if (s >= view.getViewStart() && s <= view.getViewStart() + view.getViewLength())
        grabOffset = s - view.getViewStart();
    else
    {
        grabOffset = view.getViewLength() * 0.5;
        view.setViewStart (s - grabOffset);
    }
}

void WaveOverview::mouseDrag (const juce::MouseEvent& e)
{
    auto clip = view.getClip();
    if (clip == nullptr || clip->audio == nullptr) return;
    const double n = clip->audio->getNumSamples();
    view.setViewStart ((double) e.x / getWidth() * n - grabOffset);
    repaint();
}

} // namespace snag
