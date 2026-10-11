#include "Widgets.h"
#include <limits>

namespace snag
{
using namespace theme;

//==============================================================================
IconButton::IconButton (const juce::String& name, juce::Path iconPath, const juce::String& captionIn,
                        const juce::String& styleName)
    : juce::Button (name), icon (std::move (iconPath)), caption (captionIn)
{
    setStyle (*this, styleName);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, {}, highlighted, down);

    const auto style = getProperties().getWithDefault ("style", "gold").toString();
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat();
    if (down && style != "icon" && style != "ghost")
        r.translate (0.0f, 1.0f);

    auto legend = buttonLegend (*this, highlighted);
    if (hasIconColour && ! on && isEnabled())
        legend = { iconColour, false };

    // the record button's dot is always a red light
    if (style == "red" && ! on && getName().containsIgnoreCase ("rec") && isEnabled())
        legend = { col::red, true };

    const float iconSize = juce::jmin (r.getHeight() * 0.42f, 16.0f);
    juce::Rectangle<float> iconArea;

    if (caption.isEmpty())
        iconArea = r.withSizeKeepingCentre (iconSize, iconSize);
    else
    {
        auto font = ui (juce::jmin (12.0f, r.getHeight() * 0.4f), true).withExtraKerningFactor (0.1f);
        const float tw = juce::GlyphArrangement::getStringWidth (font, caption.toUpperCase());
        const float total = iconSize + 8.0f + tw;
        auto content = r.withSizeKeepingCentre (juce::jmin (total, r.getWidth() - 12.0f), r.getHeight());
        iconArea = content.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconSize);
        content.removeFromLeft (8.0f);

        // with a lit record dot the caption stays champagne, otherwise the whole legend lights up
        const bool recDot = style == "red" && ! on && getName().containsIgnoreCase ("rec");
        if (legend.glow && ! recDot)
            glowText (g, caption.toUpperCase(), content, font, juce::Justification::centredLeft, legend.colour);
        else
        {
            g.setFont (font);
            g.setColour (recDot ? (highlighted ? col::goldLight : col::goldPale) : legend.colour);
            g.drawFittedText (caption.toUpperCase(), content.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }
    }

    if (! icon.isEmpty())
    {
        auto p = icon;
        p.applyTransform (p.getTransformToScaleToFit (iconArea, true));
        if (legend.glow)
            glowPath (g, p, legend.colour, 1.0f);
        else
        {
            g.setColour (legend.colour);
            g.fillPath (p);
        }
    }
}

//==============================================================================
Knob::Knob (const juce::String& captionIn, double min, double max, double def, double step, const juce::String& suffixIn)
    : caption (captionIn), suffix (suffixIn)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (min, max, step);
    slider.setValue (def, juce::dontSendNotification);
    slider.setDoubleClickReturnValue (true, def);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    defaultValue = def;
    slider.onValueChange = [this] { repaint(); if (onChange) onChange(); };
    slider.onResetWithoutChange = [this] { repaint(); if (onChange) onChange(); };
    slider.onDragStart = [this]
    {
        // The mouse wheel (or a trackpad swipe) reports every little step as a drag of its own.
        // Taking each for a new grab made one swipe use up the whole 30-step undo history.
        // So: holding the mouse button is a grab, and a run of wheel steps is one grab too.
        const auto now = juce::Time::getMillisecondCounter();
        const bool held = slider.isMouseButtonDown();
        const bool newGesture = held || lastWheelStepMs == 0 || now - lastWheelStepMs > 700;
        lastWheelStepMs = held ? 0 : juce::jmax ((juce::uint32) 1, now);
        if (newGesture && onGestureStart) onGestureStart();
    };
    addAndMakeVisible (slider);
}

Knob::~Knob()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
}

/** A value as people write it: "0", "7", "-3.5" - never "1.33e-15" (step snapping can leave tiny
    floating-point dust, e.g. on Apple Silicon, where -24 + 240 * 0.1 isn't exactly 0). */
static juce::String plainNumber (double v, int decimals)
{
    const double r = std::round (v);
    if (decimals <= 0 || std::abs (v - r) < 1.0e-6)
        return juce::String ((juce::int64) r);
    auto s = juce::String (v, decimals);
    return s.startsWithChar ('-') && s.substring (1).containsOnly ("0.") ? s.substring (1) : s;
}

juce::Rectangle<int> Knob::valueArea() const
{
    return getLocalBounds().removeFromBottom (28).removeFromTop (15).withSizeKeepingCentre (juce::jmin (getWidth(), 90), 17);
}

// (While the type-in box is open this knob also hears the mouse everywhere else - see startTyping.
//  Those events carry positions in other components, so only the knob's own are looked at here.)
void Knob::mouseMove (const juce::MouseEvent& e)
{
    if (e.eventComponent != this)
        return;
    const bool overValue = valueArea().contains (e.getPosition());
    setMouseCursor (overValue ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
    setTooltip (overValue ? "Double-click to type a value" : juce::String());
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && valueArea().expanded (0, 4).contains (e.getPosition()))
        startTyping();
}

void Knob::startTyping()
{
    if (! isEnabled() || (typing != nullptr && typing->isVisible()))
        return;
    typing = std::make_unique<juce::TextEditor>();
    auto& t = *typing;
    t.setFont (theme::ui (12.0f, true));
    t.setJustification (juce::Justification::centred);
    t.setIndents (2, 1);
    t.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black);
    t.setColour (juce::TextEditor::textColourId, theme::col::goldLight);
    t.setColour (juce::TextEditor::outlineColourId, theme::col::gold);
    t.setColour (juce::TextEditor::focusedOutlineColourId, theme::col::red);

    // The box starts with what the readout stands for, with as many decimals as that takes: a
    // value that is only looked at must not come back rounded (and isn't applied again at all).
    const double step = slider.getInterval();
    const double v = shownValue ? shownValue() : slider.getValue();
    const double onStep = step > 0.0 ? slider.getMinimum() + step * std::round ((v - slider.getMinimum()) / step) : v;
    const bool offTheSteps = std::abs (v - onStep) > 1.0e-6;
    auto start = plainNumber (v, offTheSteps ? 2 : (step >= 1.0 ? 0 : (step >= 0.1 ? 1 : 2)));
    if (start.containsChar ('.'))
    {
        while (start.endsWithChar ('0')) start = start.dropLastCharacters (1);
        if (start.endsWithChar ('.'))    start = start.dropLastCharacters (1);
    }
    typingStartedWith = start;
    t.setText (start, false);

    t.onReturnKey = [this] { finishTyping (true); };
    t.onEscapeKey = [this] { finishTyping (false); };
    t.onFocusLost = [this] { finishTyping (true); };
    addAndMakeVisible (t);
    t.setBounds (valueArea());
    t.grabKeyboardFocus();
    t.selectAll();
    // A click elsewhere finishes the box. (The waveform, pads and knobs never take the keyboard, so
    // the box used to stay open and swallow Space, Delete and the like until a button was pressed.)
    juce::Desktop::getInstance().addGlobalMouseListener (this);
    repaint();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (typing == nullptr || ! typing->isVisible() || e.eventComponent == nullptr)
        return;
    if (e.eventComponent == typing.get() || typing->isParentOf (e.eventComponent))
        return;
    if (e.eventComponent->getTopLevelComponent() != getTopLevelComponent())
        return;   // the box's own right-click menu (Paste...) lives in a window of its own
    finishTyping (true);
}

void Knob::finishTyping (bool apply)
{
    if (typing == nullptr || ! typing->isVisible())
        return;
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
    const auto text = typing->getText().trim();
    // remove it after this callback has returned
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this), box = typing.get()]
    {
        if (safe != nullptr && safe->typing.get() == box) { safe->typing.reset(); safe->repaint(); }
    });
    typing->onFocusLost = nullptr;
    typing->setVisible (false);
    if (apply && text.isNotEmpty() && text != typingStartedWith)
    {
        const double v = parseTyped (text);
        if (std::isfinite (v))
        {
            const double inRange = juce::jlimit (slider.getMinimum(), slider.getMaximum(), v);
            if (! (onTyped && onTyped (inRange)))
                slider.setValue (inRange, juce::sendNotificationSync);
        }
    }
}

double Knob::parseTyped (const juce::String& textIn) const
{
    auto text = textIn.trim().toLowerCase().replaceCharacter ((juce::juce_wchar) 0x2212, '-');   // (a typographic minus)
    if (text.contains ("off"))
        return defaultValue;

    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const bool hasNumber = text.containsAnyOf ("0123456789");
    const double number = hasNumber ? text.retainCharacters ("0123456789.-+,").replaceCharacter (',', '.').getDoubleValue() : nan;

    // just a number: that's the value
    if (hasNumber && text.removeCharacters ("0123456789.-+, ").isEmpty())
        return number;

    // Typed the way this knob writes it - "1/8", "1/4d", "12 bit", "5.0 kHz" - picks that position.
    // (Read as a plain number, "1/8" was 18: the DELAY time jumped to a whole bar.)
    const double step = slider.getInterval();
    if (formatter && step > 0.0 && (slider.getMaximum() - slider.getMinimum()) / step <= 4000.0)
    {
        auto squash = [] (const juce::String& t) { return t.toLowerCase().removeCharacters (" "); };
        const auto wanted = squash (text);
        double first = nan, last = nan;
        const int steps = juce::roundToInt ((slider.getMaximum() - slider.getMinimum()) / step);
        for (int i = 0; i <= steps; ++i)
        {
            const double v = juce::jmin (slider.getMaximum(), slider.getMinimum() + i * step);
            if (squash (formatter (v)) == wanted)
            {
                if (std::isnan (first)) first = v;
                last = v;
            }
        }
        if (! std::isnan (first))
        {
            // Several positions can read the same ("5.0 kHz" covers 4.95 to 5.04): the number that
            // was typed says which one, in the knob's own unit or a thousand of them.
            if (hasNumber && last > first)
                for (double scale : { 1.0, 1000.0, 0.001 })
                    if (number * scale >= first - step * 0.5 && number * scale <= last + step * 0.5)
                        return number * scale;
            return first;
        }
    }

    if (! hasNumber)
        return nan;
    double v = number;

    // "5k", "2.5 kHz" - but not on a knob that counts in kHz already ("22k" on RATE is 22, not 22000)
    if (text.containsChar ('k') && ! suffix.containsIgnoreCase ("k"))
        v *= 1000.0;
    // "1.5 s" on a knob that counts milliseconds
    if (suffix.trim().equalsIgnoreCase ("ms") && text.endsWithChar ('s') && ! text.endsWith ("ms"))
        v *= 1000.0;
    return v;
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (28);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto bottom = r.removeFromBottom (28.0f);

    auto valueText = formatter ? formatter (slider.getValue())
                               : (slider.getInterval() >= 1.0 ? plainNumber (slider.getValue(), 0)
                                                              : juce::String (std::abs (slider.getValue()) < 0.05 ? 0.0 : slider.getValue(), 1)) + suffix;
    g.setColour (col::goldPale);
    g.setFont (ui (11.0f, true));
    if (typing == nullptr)
        g.drawText (valueText, bottom.removeFromTop (14.0f), juce::Justification::centred);
    else
        bottom.removeFromTop (14.0f);
    g.setColour (col::textDim);
    g.setFont (ui (9.5f, true).withExtraKerningFactor (0.14f));
    g.drawText (caption.toUpperCase(), bottom, juce::Justification::centred);
}

//==============================================================================
juce::Rectangle<int> Panel::getContentBounds() const
{
    auto r = getLocalBounds().reduced (12, 10);
    if (title.isNotEmpty())
        r.removeFromTop (18);
    return r;
}

void Panel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    glassWindow (g, r, 8.0f, 0.8f);
    if (goldEdge)
        goldBorder (g, r, 8.0f, 1.0f, 0.5f);
    if (title.isNotEmpty())
    {
        auto t = r.reduced (11.0f, 6.0f).removeFromTop (14.0f);
        sectionLabel (g, title, t);
        // small neon tick before the next section
        g.setColour (col::red.withAlpha (0.9f));
        const float tw = juce::GlyphArrangement::getStringWidth (ui (10.5f, true).withExtraKerningFactor (0.18f), title.toUpperCase());
        g.fillRect (juce::Rectangle<float> (t.getX() + tw + 8.0f, t.getCentreY() - 0.5f, 18.0f, 1.0f));
    }
}

//==============================================================================
void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.fillRoundedRectangle (r, 2.0f);
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    auto f = r.withWidth (r.getWidth() * frac);
    juce::ColourGradient cg (col::goldDark, r.getX(), 0, col::red, r.getRight(), 0, false);
    cg.addColour (0.7, col::gold);
    cg.addColour (0.9, col::goldLight);
    g.setGradientFill (cg);
    g.fillRoundedRectangle (f, 2.0f);
}

//==============================================================================
DragHandle::DragHandle (const juce::String& c) : caption (c)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag onto a track in your DAW, or onto your desktop / sample folder");
}

void DragHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool hover = isMouseOver (true);
    const float corner = juce::jmin (7.0f, r.getHeight() * 0.3f);

    // the main call to action: satin black, backlit in neon red
    juce::Path shape; shape.addRoundedRectangle (r, corner);
    neonGlow (g, shape, col::red, hover || dragging ? 14.0f : 9.0f, hover || dragging ? 0.6f : 0.35f);
    satinSurface (g, r, corner, hover, dragging, ! dragging);
    {
        juce::Path rim; rim.addRoundedRectangle (r.reduced (1.2f), corner - 1.0f);
        juce::Path stroked;
        juce::PathStrokeType (1.2f).createStrokedPath (stroked, rim);
        glowPath (g, stroked, col::red, hover || dragging ? 1.0f : 0.7f);
    }

    auto content = r.reduced (12.0f, 4.0f);
    auto iconArea = content.removeFromLeft (juce::jmin (18.0f, content.getHeight())).withSizeKeepingCentre (16.0f, 16.0f);
    auto icon = icons::drag();
    icon.applyTransform (icon.getTransformToScaleToFit (iconArea, true));
    glowPath (g, icon, col::red, 1.0f);

    content.removeFromLeft (9.0f);
    auto font = ui (juce::jmin (12.0f, r.getHeight() * 0.36f), true).withExtraKerningFactor (0.12f);
    if (juce::GlyphArrangement::getStringWidth (font, caption) <= content.getWidth())
        glowText (g, caption, content, font, juce::Justification::centredLeft, col::red, 1.0f);
    else
    {
        g.setFont (font);
        g.setColour (col::redHot);
        g.drawFittedText (caption, content.toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
    }
}

void DragHandle::mouseDown (const juce::MouseEvent&) { pressed = true; }
void DragHandle::mouseUp (const juce::MouseEvent&)   { pressed = false; dragging = false; repaint(); }

bool DragHandle::startExternalDrag (juce::Component& source, const juce::File& file)
{
    if (! file.existsAsFile())
        return false;
    return juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, &source);
}

void DragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! pressed || e.getDistanceFromDragStart() < 4 || ! makeFile)
        return;

    dragging = true;
    repaint();
    auto f = makeFile();
    if (f.existsAsFile())
        startExternalDrag (*this, f);
    dragging = false;
    pressed = false;
    repaint();
}

//==============================================================================
void drawWaveform (juce::Graphics& g, juce::Rectangle<float> r, const AudioData& audio, int start, int end,
                   juce::Colour colour, bool glossy, float displayGain)
{
    const int n = audio.getNumSamples();
    if (end < 0 || end > n) end = n;
    start = juce::jlimit (0, n, start);
    if (end <= start || r.getWidth() < 1.0f)
        return;

    const int w = (int) r.getWidth();
    const float mid = r.getCentreY();
    const float half = r.getHeight() * 0.5f * displayGain;
    const double spp = (double) (end - start) / (double) w;
    const int chans = audio.getNumChannels();

    juce::Path peak, rms;
    std::vector<float> tops ((size_t) w), bots ((size_t) w), rmsv ((size_t) w);

    for (int x = 0; x < w; ++x)
    {
        const int s0 = start + (int) (x * spp);
        const int s1 = juce::jmin (end, start + (int) ((x + 1) * spp) + 1);
        float mn = 0, mx = 0;
        double sq = 0;
        const int stride = juce::jmax (1, (s1 - s0) / 256);
        int count = 0;
        for (int c = 0; c < chans; ++c)
        {
            const float* d = audio.buffer.getReadPointer (c);
            for (int i = s0; i < s1; i += stride)
            {
                mn = juce::jmin (mn, d[i]);
                mx = juce::jmax (mx, d[i]);
                sq += (double) d[i] * d[i];
                ++count;
            }
        }
        tops[(size_t) x] = mx;
        bots[(size_t) x] = mn;
        rmsv[(size_t) x] = count > 0 ? (float) std::sqrt (sq / count) : 0.0f;
    }

    peak.startNewSubPath (r.getX(), mid);
    for (int x = 0; x < w; ++x)
        peak.lineTo (r.getX() + (float) x, mid - juce::jlimit (0.0f, 1.0f / displayGain, tops[(size_t) x]) * half);
    for (int x = w; --x >= 0;)
        peak.lineTo (r.getX() + (float) x, mid - juce::jlimit (-1.0f / displayGain, 0.0f, bots[(size_t) x]) * half);
    peak.closeSubPath();

    rms.startNewSubPath (r.getX(), mid);
    for (int x = 0; x < w; ++x)
        rms.lineTo (r.getX() + (float) x, mid - juce::jmin (1.0f / displayGain, rmsv[(size_t) x] * 1.4f) * half);
    for (int x = w; --x >= 0;)
        rms.lineTo (r.getX() + (float) x, mid + juce::jmin (1.0f / displayGain, rmsv[(size_t) x] * 1.4f) * half);
    rms.closeSubPath();

    if (glossy)
    {
        juce::ColourGradient full (colour.brighter (0.35f), 0, r.getY(), colour.brighter (0.35f), 0, r.getBottom(), false);
        full.addColour (0.5, colour.darker (0.5f));
        g.setGradientFill (full);
        g.setOpacity (0.55f);
        g.fillPath (peak);
        g.setOpacity (1.0f);
        g.setGradientFill (full);
        g.fillPath (rms);
    }
    else
    {
        g.setColour (colour.withAlpha (0.6f));
        g.fillPath (peak);
        g.setColour (colour);
        g.fillPath (rms);
    }
}

void WaveThumb::draw (juce::Graphics& g, juce::Rectangle<float> r, const AudioData* audio,
                      juce::Colour colour, int start, int end, bool normalise)
{
    if (audio == nullptr || r.isEmpty())
        return;

    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    auto size = (r * scale).toNearestInt().withZeroOrigin();

    if (cache.isNull() || cachedFor != audio || cachedColour != colour || cachedStart != start || cachedEnd != end || cachedSize != size)
    {
        cache = juce::Image (juce::Image::ARGB, juce::jmax (1, size.getWidth()), juce::jmax (1, size.getHeight()), true);
        juce::Graphics ig (cache);
        float gain = 1.0f;
        if (normalise)
        {
            float pk = 0.0f;
            const int e = end < 0 ? audio->getNumSamples() : end;
            for (int c = 0; c < audio->getNumChannels(); ++c)
                pk = juce::jmax (pk, audio->buffer.getMagnitude (c, start, juce::jmax (0, e - start)));
            gain = pk > 1.0e-5f ? juce::jlimit (1.0f, 10.0f, 0.92f / pk) : 1.0f;
        }
        drawWaveform (ig, size.toFloat(), *audio, start, end, colour, true, gain);
        cachedFor = audio; cachedColour = colour; cachedStart = start; cachedEnd = end; cachedSize = size;
    }
    g.setOpacity (1.0f);   // drawImage uses the current colour's alpha
    g.drawImage (cache, r, juce::RectanglePlacement::stretchToFit);
}

//==============================================================================
void ToastOverlay::show (const juce::String& text, bool isError, int ms)
{
    toasts.push_back ({ text, isError, juce::Time::getMillisecondCounter() + (juce::uint32) ms });
    if (toasts.size() > 4)
        toasts.erase (toasts.begin());
    startTimerHz (20);
    repaint();
}

void ToastOverlay::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    toasts.erase (std::remove_if (toasts.begin(), toasts.end(), [now] (const Toast& t) { return now > t.until; }), toasts.end());
    if (toasts.empty())
        stopTimer();
    repaint();
}

void ToastOverlay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (18.0f);
    float y = area.getBottom();
    const auto now = juce::Time::getMillisecondCounter();
    auto font = ui (13.0f, true);

    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it)
    {
        const float tw = juce::jmin (460.0f, juce::GlyphArrangement::getStringWidth (font, it->text) + 44.0f);
        const int lines = juce::GlyphArrangement::getStringWidth (font, it->text) + 44.0f > 460.0f ? 2 : 1;
        const float h = lines == 1 ? 40.0f : 58.0f;
        auto r = juce::Rectangle<float> (area.getRight() - tw, y - h, tw, h);
        y -= h + 8.0f;

        const float remaining = (float) ((juce::int64) it->until - (juce::int64) now);
        const float alpha = juce::jlimit (0.0f, 1.0f, remaining / 400.0f);
        g.setOpacity (alpha);

        juce::Path shape; shape.addRoundedRectangle (r, 8.0f);
        neonGlow (g, shape, it->error ? col::red : col::gold, 14.0f, 0.35f * alpha);
        glassWindow (g, r, 8.0f, 0.7f, false);
        if (! it->error)
            goldBorder (g, r, 8.0f, 1.0f, 0.7f);
        if (it->error)
        {
            g.setColour (col::red);
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.2f);
        }
        auto dot = r.withWidth (26.0f).withSizeKeepingCentre (7.0f, 7.0f).translated (6.0f, 0.0f);
        g.setColour (it->error ? col::red : col::gold);
        g.fillEllipse (dot);
        g.setColour (col::text.withAlpha (alpha));
        g.setFont (font);
        g.drawFittedText (it->text, r.withTrimmedLeft (30.0f).reduced (6.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 2);
        g.setOpacity (1.0f);
    }
}

void drawSpinner (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const float t = (float) (juce::Time::getMillisecondCounter() % 1000) / 1000.0f;
    const float start = t * juce::MathConstants<float>::twoPi;
    juce::Path p;
    p.addCentredArc (r.getCentreX(), r.getCentreY(), r.getWidth() * 0.4f, r.getHeight() * 0.4f, 0.0f, start, start + 4.2f, true);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace snag
