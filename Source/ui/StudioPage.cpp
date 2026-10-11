#include "StudioPage.h"
#include "../PluginProcessor.h"
#include "../Actions.h"
#include "../core/EditOps.h"

namespace snag
{
using namespace theme;

//==============================================================================
SlicePads::SlicePads (SnaggerProcessor& p) : proc (p)
{
    startTimerHz (30);
}

int SlicePads::numPads() const
{
    if (clip == nullptr || clip->audio == nullptr) return 0;
    return juce::jmin (StudioPage::maxPads, (int) clip->sliceBoundaries().size() - 1);
}

juce::Rectangle<float> SlicePads::padBounds (int i, int count) const
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const int perRow = juce::jmax (16, count);
    const float gap = 5.0f;
    const float w = (r.getWidth() - gap * (float) (perRow - 1)) / (float) perRow;
    return { r.getX() + (float) i * (w + gap), r.getY(), w, r.getHeight() };
}

int SlicePads::padAt (juce::Point<int> p) const
{
    const int n = numPads();
    for (int i = 0; i < n; ++i)
        if (padBounds (i, n).contains (p.toFloat()))
            return i;
    return -1;
}

void SlicePads::paint (juce::Graphics& g)
{
    const int n = numPads();
    const int root = proc.rootNote.load();
    const bool chopsMode = proc.midiMode.load() == SnaggerProcessor::chops;

    if (n <= 1)
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        glassWindow (g, r, 7.0f, 0.6f);
        g.setColour (col::textDim);
        g.setFont (ui (12.0f));
        g.drawText (chopsMode ? "Chop the sample (AUTO CHOP or EQUAL) to get MIDI pads - pads start at " + noteName (root) + ". Drag any pad into your DAW."
                              : "KEYS mode: play the sample (or selection) chromatically from your MIDI keyboard - " + noteName (root) + " = original pitch.",
                    r.reduced (14.0f, 0.0f), juce::Justification::centredLeft);
        return;
    }

    const auto now = juce::Time::getMillisecondCounter();
    for (int i = 0; i < juce::jmax (16, n); ++i)
    {
        auto r = padBounds (i, n);
        r = r.reduced (1.5f);
        if (i >= n)
        {
            // empty slot: a dark recess in the glass, with a faint gold edge
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff030304), 0.0f, r.getY(), juce::Colour (0xff0b0b0d), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (col::gold.withAlpha (0.22f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            continue;
        }

        const bool isLit = (i == lit && now < litUntil) || i == pressed;
        if (showSelected && i == selected && ! isLit)
        {
            juce::Path ring; ring.addRoundedRectangle (r.expanded (1.5f), 7.0f);
            neonGlow (g, ring, col::gold, 8.0f, 0.6f);
        }
        if (isLit)
        {
            juce::Path p; p.addRoundedRectangle (r, 6.0f);
            neonGlow (g, p, col::red, 12.0f, 0.75f);
        }
        satinSurface (g, r, 6.0f, isLit, i == pressed, i != pressed);
        if (isLit)
        {
            juce::Path rim; rim.addRoundedRectangle (r.reduced (1.2f), 5.0f);
            juce::Path stroked;
            juce::PathStrokeType (1.2f).createStrokedPath (stroked, rim);
            glowPath (g, stroked, col::red, 0.9f);
        }
        else
            goldBorder (g, r.reduced (0.6f), 6.0f, showSelected && i == selected ? 1.8f : 1.0f, showSelected && i == selected ? 1.0f : 0.3f);

        auto numArea = r.withTrimmedBottom (r.getHeight() * 0.38f);
        if (isLit)
            glowText (g, juce::String (i + 1), numArea, display (r.getHeight() * 0.36f, true), juce::Justification::centred, col::red);
        else
        {
            g.setColour (col::goldLight);
            g.setFont (display (r.getHeight() * 0.36f, true));
            g.drawText (juce::String (i + 1), numArea, juce::Justification::centred);
        }
        g.setColour (isLit ? col::redHot : col::textDim);
        g.setFont (ui (juce::jmin (10.0f, r.getWidth() * 0.26f), true));
        g.drawText (noteName (root + i), r.withTrimmedTop (r.getHeight() * 0.55f), juce::Justification::centred);
    }
}

void SlicePads::timerCallback()
{
    const auto c = proc.triggerCounter.load();
    if (c != lastCounter)
    {
        lastCounter = c;
        lit = proc.lastTriggeredSlice.load();
        litUntil = juce::Time::getMillisecondCounter() + 160;
        repaint();
    }
    else if (lit >= 0 && juce::Time::getMillisecondCounter() > litUntil)
    {
        lit = -1;
        repaint();
    }
}

void SlicePads::mouseDown (const juce::MouseEvent& e)
{
    pressed = padAt (e.getPosition());
    dragStarted = false;
    dragPad = -1;
    dragOut.pressed();
    if (pressed >= 0)
    {
        proc.sendUiNote (proc.rootNote.load() + pressed, 0.9f);
        if (onSliceSelected) onSliceSelected (pressed);
    }
    repaint();
}

void SlicePads::mouseDrag (const juce::MouseEvent& e)
{
    if (clip == nullptr || e.getDistanceFromDragStart() < 8)
        return;

    if (! dragStarted)
    {
        if (pressed < 0)
            return;
        dragStarted = true;
        proc.sendUiNote (proc.rootNote.load() + pressed, 0.0f);
        dragPad = pressed;
        pressed = -1;
        repaint();
    }

    // with the pad's sound and the FX (a very long chop is rendered by a background job first)
    if (dragPad >= 0)
        dragOut.drag (proc, *this, [this] { return clip != nullptr ? actions::dragJobForPad (proc, *clip, dragPad) : actions::DragJob(); });
}

void SlicePads::mouseUp (const juce::MouseEvent&)
{
    dragOut.released();
    if (pressed >= 0)
        proc.sendUiNote (proc.rootNote.load() + pressed, 0.0f);
    pressed = -1;
    repaint();
}

//==============================================================================
StudioPage::StudioPage (EditorContext& c)
    : ctx (c), proc (c.getProcessor()), wave (c.getProcessor()), pads (c.getProcessor()), midiDeck (c)
{
    nameLabel.setFont (display (20.0f, true));
    nameLabel.setColour (juce::Label::textColourId, col::goldLight);
    nameLabel.setEditable (false, true, false);
    nameLabel.setTooltip ("Double-click to rename");
    nameLabel.onTextChange = [this] { rename(); };
    nameLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (nameLabel);

    infoLabel.setFont (ui (11.5f, true));
    infoLabel.setColour (juce::Label::textColourId, col::textDim);
    addAndMakeVisible (infoLabel);

    bpmLabel.setFont (ui (12.0f, true));
    bpmLabel.setColour (juce::Label::textColourId, col::goldPale);
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (bpmLabel);

    setStyle (detectBpmBtn, "chip");
    detectBpmBtn.setTooltip ("Find the tempo and the key");
    detectBpmBtn.onClick = [this] { detectBpm(); detectKey (false); };
    addAndMakeVisible (detectBpmBtn);

    setStyle (keyBtn, "chip");
    keyBtn.setTooltip ("The sample's key (Camelot code for DJs). Click to pick another if it's wrong.");
    keyBtn.onClick = [this] { showKeyMenu(); };
    addAndMakeVisible (keyBtn);

    undoBtn.setTooltip ("Undo (Cmd/Ctrl+Z)");
    redoBtn.setTooltip ("Redo (Shift+Cmd/Ctrl+Z)");
    undoBtn.onClick = [this] { undo(); };
    redoBtn.onClick = [this] { redo(); };
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (redoBtn);

    addAndMakeVisible (overview);
    addAndMakeVisible (wave);
    wave.onSelectionChanged = [this]
    {
        refreshInfo(); syncSampler(); overview.repaint();
        if (wave.hasSelection()) midiDeck.setRange (wave.getSelectionStart(), wave.getSelectionEnd());
        else midiDeck.setRange (0, -1);
    };
    wave.onSlicesChanged = [this]
    {
        if (clip != nullptr) proc.session.clipChanged (clip.get(), false);
        syncSampler();
        chopsChanged();
    };
    wave.onViewChanged = [this] { overview.repaint(); };
    wave.onPlayFrom = [this] (int from)
    {
        if (clip != nullptr) proc.previewClip (*clip, from, -1, loopBtn.getToggleState(), true);
    };

    // transport
    playBtn.setTooltip ("Play the selection (or from the cursor) - Space");
    playBtn.onClick = [this] { play(); };
    stopBtn.onClick = [this] { stop(); };
    loopBtn.setClickingTogglesState (true);
    loopBtn.setTooltip ("Loop playback");
    loopBtn.onClick = [this]
    {
        // (it used to be read only when playback started, so a running loop couldn't be switched off)
        if (clip != nullptr && clip->audio != nullptr)
            resumePlayback (clip->audio.get());
    };
    for (auto* b : { static_cast<juce::Component*> (&playBtn), static_cast<juce::Component*> (&stopBtn), static_cast<juce::Component*> (&loopBtn) })
        addAndMakeVisible (b);

    selLabel.setFont (ui (11.5f, true));
    selLabel.setColour (juce::Label::textColourId, col::textDim);
    addAndMakeVisible (selLabel);

    zoomInBtn.onClick  = [this] { wave.zoomBy (0.6, wave.getViewStart() + wave.getViewLength() * 0.5); };
    zoomOutBtn.onClick = [this] { wave.zoomBy (1.6, wave.getViewStart() + wave.getViewLength() * 0.5); };
    setStyle (fitBtn, "ghost");
    fitBtn.onClick = [this] { wave.zoomToFit(); };
    zoomInBtn.setTooltip ("Zoom in (or Cmd/Ctrl + scroll)");
    zoomOutBtn.setTooltip ("Zoom out");
    addAndMakeVisible (zoomInBtn);
    addAndMakeVisible (zoomOutBtn);
    addAndMakeVisible (fitBtn);

    saveBtn.setTooltip ("Save the selection (or whole sample) as a WAV in your Sample Snagger library");
    saveBtn.onClick = [this]
    {
        if (clip == nullptr) return;
        actions::saveToLibrary (proc, *clip, selStartOrZero(), selEndOrAll(), wave.hasSelection() ? " (cut)" : "");
    };
    addAndMakeVisible (saveBtn);

    dragHandle.makeFile = [this]
    {
        if (clip == nullptr) return juce::File();
        return actions::makeDragFile (proc, *clip, selStartOrZero(), selEndOrAll(), wave.hasSelection() ? " (cut)" : "");
    };
    // (the drag itself goes through this: a long sample is rendered by a background job first)
    sampleDrag.attachTo (dragHandle, proc, [this]
    {
        return clip != nullptr ? actions::dragJobFor (proc, *clip, selStartOrZero(), selEndOrAll(), wave.hasSelection() ? " (cut)" : "")
                               : actions::DragJob();
    });
    addAndMakeVisible (dragHandle);

    pads.onSliceSelected = [this] (int idx)
    {
        if (clip == nullptr) return;
        selectedPad = idx;
        padDeck.setClip (clip, idx);
        pads.setSelected (idx, deck == 1);
        auto b = clip->sliceBoundaries();
        if (idx + 1 < (int) b.size())
            wave.setSelection (b[(size_t) idx], b[(size_t) idx + 1]);
    };
    addAndMakeVisible (pads);

    // ---- panels ----
    for (auto* p : { &editPanel, &pitchPanel, &tonePanel, &chopPanel })
        addAndMakeVisible (p);

    struct EditDef { const char* name; const char* tip; };
    const EditDef defs[] = {
        { "TRIM",      "Keep only the selection" },
        { "CUT",       "Delete the selection" },
        { "SILENCE",   "Silence the selection" },
        { "NORMALIZE", "Make it as loud as possible without clipping (-0.3 dB peak)" },
        { "FADE IN",   "Fade in over the selection (or the first 5%)" },
        { "FADE OUT",  "Fade out over the selection (or the last 5%)" },
        { "REVERSE",   "Reverse the selection (or whole sample)" },
        { "MONO",      "Sum to mono" },
    };
    for (auto& d : defs)
    {
        auto* b = editButtons.add (new juce::TextButton (d.name));
        b->setTooltip (d.tip);
        addAndMakeVisible (b);
    }

    auto fadeRange = [this] (bool in, int& s, int& e)
    {
        const int n = clip->audio->getNumSamples();
        if (wave.hasSelection()) { s = wave.getSelectionStart(); e = wave.getSelectionEnd(); return; }
        const int len = juce::jlimit ((int) (0.01 * clip->audio->sampleRate), (int) (2.0 * clip->audio->sampleRate), n / 20);
        if (in) { s = 0; e = len; } else { s = n - len; e = n; }
    };

    editButtons[0]->onClick = [this] { applyEdit ("Trim", true, SliceFix::crop, [] (const AudioData& a, int s, int e) { return edit::crop (a, s, e); }); };
    editButtons[1]->onClick = [this] { applyEdit ("Cut", true, SliceFix::remove, [] (const AudioData& a, int s, int e) { return edit::removeRange (a, s, e); }); };
    editButtons[2]->onClick = [this] { applyEdit ("Silence", true, SliceFix::keep, [] (const AudioData& a, int s, int e) { return edit::silence (a, s, e); }); };
    editButtons[3]->onClick = [this] { applyEdit ("Normalize", false, SliceFix::keep, [] (const AudioData& a, int s, int e) { return edit::normalize (a, s, e); }); };
    editButtons[4]->onClick = [this, fadeRange]
    {
        if (clip == nullptr) return;
        int fs, fe; fadeRange (true, fs, fe);
        applyEditRange ("Fade in", fs, fe, SliceFix::keep, [] (const AudioData& a, int s, int e) { return edit::fade (a, s, e, true); });
    };
    editButtons[5]->onClick = [this, fadeRange]
    {
        if (clip == nullptr) return;
        int fs, fe; fadeRange (false, fs, fe);
        applyEditRange ("Fade out", fs, fe, SliceFix::keep, [] (const AudioData& a, int s, int e) { return edit::fade (a, s, e, false); });
    };
    editButtons[6]->onClick = [this] { applyEdit ("Reverse", false, SliceFix::keep, [] (const AudioData& a, int s, int e) { return edit::reverse (a, s, e); }); };
    editButtons[7]->onClick = [this] { applyEdit ("Mono", false, SliceFix::keep, [] (const AudioData& a, int, int) { return edit::toMono (a); }); };

    // pitch & time
    // (knob steps can leave floating-point dust like 1.3e-15 - never show that)
    pitchKnob.formatter = [this] (double v)
    {
        const bool exact = ! std::isnan (exactSemitones);
        if (exact) v = exactSemitones;
        const bool whole = std::abs (v - std::round (v)) < (exact ? 0.005 : 0.05);
        const auto num = whole ? juce::String ((int) std::round (v)) : juce::String (v, exact ? 2 : 1);
        auto text = (v > 0.05 ? "+" : "") + num + " st";
        if (whole && clip != nullptr && clip->keyTonic >= 0 && ! tapeToggle.getToggleState())
            text << "  " << key::name (key::Key { clip->keyTonic, clip->keyMinor }.transposed ((int) std::round (v)));
        return text;
    };
    toKeyBox.setTooltip ("Shift the sample into another key (sets PITCH)");
    toKeyBox.setTextWhenNothingSelected ("TO KEY");
    toKeyBox.setTextWhenNoChoicesAvailable ("TO KEY");
    toKeyBox.onChange = [this]
    {
        const int id = toKeyBox.getSelectedId();
        if (clip == nullptr || clip->keyTonic < 0 || id < 100) return;
        const int st = key::semitonesTo ({ clip->keyTonic, clip->keyMinor }, id - 100);
        lastAdjustUndoMs = 0;
        tapeToggle.setToggleState (false, juce::dontSendNotification);
        setPitchKnob (st);
        knobsChanged();
    };
    addAndMakeVisible (toKeyBox);
    stretchKnob.formatter = [this] (double v)
    {
        if (exactLength > 0.0)
            return juce::String (exactLength, std::abs (exactLength * 10.0 - std::round (exactLength * 10.0)) < 1.0e-3 ? 1 : 2) + "%";
        return juce::String (juce::roundToInt (v)) + "%";
    };
    bpmKnob.formatter = [this] (double v) { return juce::String (targetFollowsHost && lastHostBpm > 0 ? lastHostBpm : v, 1); };
    bpmKnob.onChange = [this] { targetFollowsHost = false; };   // set by hand: stays until the DAW's tempo changes
    bpmKnob.slider.setTooltip ("The tempo MATCH BPM stretches to. It follows your DAW's tempo until you set it yourself.");
    formantToggle.setToggleState (true, juce::dontSendNotification);
    formantToggle.setTooltip ("Keep formants: voices stay natural when pitching (no chipmunk)");
    tapeToggle.setTooltip ("Old-school varispeed: pitch and speed change together");
    pitchDefaultBtn.setTooltip ("Back to the original pitch and length");
    matchBpmBtn.setTooltip ("Set LENGTH so the sample plays at the target BPM");
    pitchDefaultBtn.onClick = [this]
    {
        setPitchKnob (pitchKnob.getDefault());
        setLengthKnob (stretchKnob.getDefault());
        lastAdjustUndoMs = 0;   // its own undo step
        knobsChanged();
    };
    matchBpmBtn.onClick = [this] { matchBpm(); };
    for (auto* comp : std::initializer_list<juce::Component*> { &pitchKnob, &stretchKnob, &bpmKnob, &formantToggle, &tapeToggle, &pitchDefaultBtn, &matchBpmBtn })
        addAndMakeVisible (comp);

    // tone
    lowCutKnob.slider.setSkewFactorFromMidPoint (150.0);
    highCutKnob.slider.setSkewFactorFromMidPoint (5000.0);
    lowCutKnob.formatter  = [] (double v) { return v < 10.0 ? juce::String ("Off") : juce::String ((int) v) + " Hz"; };
    highCutKnob.formatter = [] (double v) { return v > 19900.0 ? juce::String ("Off") : (v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String ((int) v) + " Hz"); };
    gainKnob.formatter = [] (double v)
    {
        if (std::abs (v) < 0.05) return juce::String ("0.0 dB");
        return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
    };
    toneDefaultBtn.setTooltip ("Back to the original gain and no filters");
    toneDefaultBtn.onClick = [this]
    {
        gainKnob.setValueSilently (gainKnob.getDefault());
        lowCutKnob.setValueSilently (lowCutKnob.getDefault());
        highCutKnob.setValueSilently (highCutKnob.getDefault());
        lastAdjustUndoMs = 0;
        knobsChanged();
    };
    for (auto* comp : std::initializer_list<juce::Component*> { &gainKnob, &lowCutKnob, &highCutKnob, &toneDefaultBtn })
        addAndMakeVisible (comp);

    // every knob / switch in PITCH & TIME and TONE applies as you move it
    for (auto* k : { &pitchKnob, &stretchKnob, &gainKnob, &lowCutKnob, &highCutKnob })
    {
        k->onChange = [this, k]
        {
            // moved by hand: the knob itself says what applies again
            if (k == &pitchKnob)   exactSemitones = std::numeric_limits<double>::quiet_NaN();
            if (k == &stretchKnob) exactLength = 0.0;
            knobsChanged();
        };
        k->onGestureStart = [this] { lastAdjustUndoMs = 0; };   // each new grab is its own undo step
    }
    // what the readouts stand for when it is finer than the dial's steps (the type-in box starts from it)
    stretchKnob.shownValue = [this] { return exactLength > 0.0 ? exactLength : stretchKnob.slider.getValue(); };
    pitchKnob.shownValue   = [this] { return std::isnan (exactSemitones) ? pitchKnob.slider.getValue() : exactSemitones; };
    bpmKnob.shownValue     = [this] { return targetBpm(); };
    // a typed value is taken as typed ("77.5"), not rounded to the knob's steps
    stretchKnob.onTyped = [this] (double v) { lastAdjustUndoMs = 0; setLengthKnob (v); knobsChanged(); return true; };
    pitchKnob.onTyped   = [this] (double v) { lastAdjustUndoMs = 0; setPitchKnob (v);  knobsChanged(); return true; };
    formantToggle.onClick = [this] { lastAdjustUndoMs = 0; knobsChanged(); };
    tapeToggle.onClick    = [this] { lastAdjustUndoMs = 0; knobsChanged(); };

    // chop & play
    setStyle (autoChopBtn, "red");
    autoChopBtn.onClick = [this] { autoChop(); };

    chopCount.addItem ("Every hit", everyHit);
    for (int n : { 2, 3, 4, 6, 8, 12, 16, 24, 32 })
        chopCount.addItem (juce::String (n) + " chops", n);
    chopCount.setSelectedId (everyHit, juce::dontSendNotification);
    chopCount.setTooltip ("How many chops: a number makes AUTO CHOP cut at the strongest hits "
                          "and EQUAL cut evenly. \"Every hit\" lets SENSITIVITY decide.");
    chopCount.onChange = [this] { chopCountChanged(); };
    equalBtn.onClick = [this] { equalChop(); };
    chopCountChanged();

    clearChopsBtn.onClick = [this]
    {
        if (clip == nullptr || clip->slices.empty()) return;
        clip->pushUndo ("Clear chops");
        clip->slices.clear();
        clip->pads.clear();
        selectedPad = 0;
        proc.session.clipChanged (clip.get(), false);
        syncSampler();
        chopsChanged();
        wave.repaint();
    };

    midiModeBox.addItem ("MIDI: Chops", 1);
    midiModeBox.addItem ("MIDI: Keys", 2);
    midiModeBox.setTooltip ("Chops: each chop on its own key from " + noteName (proc.rootNote.load()) + ". Keys: play the sample chromatically.");
    midiModeBox.setSelectedId (proc.midiMode.load() == SnaggerProcessor::keys ? 2 : 1, juce::dontSendNotification);
    midiModeBox.onChange = [this]
    {
        proc.midiMode = midiModeBox.getSelectedId() == 2 ? SnaggerProcessor::keys : SnaggerProcessor::chops;
        pads.repaint();
    };
    oneShotToggle.setToggleState (proc.oneShot.load(), juce::dontSendNotification);
    oneShotToggle.setTooltip ("On: chops play to the end. Off: chops stop when you release the key.");
    oneShotToggle.onClick = [this] { proc.oneShot = oneShotToggle.getToggleState(); };

    for (auto* comp : std::initializer_list<juce::Component*> { &sensKnob, &autoChopBtn, &chopCount, &equalBtn, &clearChopsBtn, &midiModeBox, &oneShotToggle })
        addAndMakeVisible (comp);

    // ---- tool tabs ----
    for (auto* name : { "SAMPLE", "PADS", "FX", "MIDI" })
    {
        auto* t = deckTabs.add (new juce::TextButton (name));
        setStyle (*t, "tab");
        t->setClickingTogglesState (false);
        const int index = deckTabs.size() - 1;
        t->onClick = [this, index] { setDeck (index); };
        addAndMakeVisible (t);
    }
    deckTabs[0]->setTooltip ("Edit, pitch & time, tone, chops");
    deckTabs[1]->setTooltip ("The sound of each pad: gain, pitch, reverse, attack, release, filter");
    deckTabs[2]->setTooltip ("Lo-fi / vinyl, drive, delay and reverb");
    deckTabs[3]->setTooltip ("Turn a melody or chords into MIDI");
    deckHint.setFont (ui (12.5f));
    deckHint.setColour (juce::Label::textColourId, col::textDim);
    deckHint.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (deckHint);

    for (auto* d : { static_cast<DeckCallbacks*> (&padDeck), static_cast<DeckCallbacks*> (&fxDeck) })
    {
        d->beforeChange = [this] (const juce::String& label, bool fresh) { soundUndo (label, fresh); };
        d->afterChange = [this] { soundChanged(); };
    }
    addChildComponent (padDeck);
    addChildComponent (fxDeck);
    addChildComponent (midiDeck);

    proc.session.addChangeListener (this);
    setClip (proc.session.getSelected());
    setDeck (juce::jlimit (0, 3, (int) proc.uiState.getProperty ("studioDeck", 0)));
    startTimerHz (4);
}

void StudioPage::setDeck (int d)
{
    deck = juce::jlimit (0, 3, d);
    proc.uiState.setProperty ("studioDeck", deck, nullptr);
    for (int i = 0; i < deckTabs.size(); ++i)
        deckTabs[i]->setToggleState (i == deck, juce::dontSendNotification);

    const bool sample = deck == 0;
    for (auto* comp : std::initializer_list<juce::Component*> { &editPanel, &pitchPanel, &tonePanel, &pitchKnob, &stretchKnob, &bpmKnob,
                                                                &formantToggle, &tapeToggle, &pitchDefaultBtn, &matchBpmBtn, &toKeyBox,
                                                                &gainKnob, &lowCutKnob, &highCutKnob, &toneDefaultBtn })
        comp->setVisible (sample);
    for (auto* b : editButtons) b->setVisible (sample);
    const bool chops = deck == 0 || deck == 1;
    for (auto* comp : std::initializer_list<juce::Component*> { &chopPanel, &sensKnob, &autoChopBtn, &chopCount, &equalBtn, &clearChopsBtn, &midiModeBox, &oneShotToggle })
        comp->setVisible (chops);
    padDeck.setVisible (deck == 1);
    fxDeck.setVisible (deck == 2);
    midiDeck.setVisible (deck == 3);
    pads.setSelected (selectedPad, deck == 1);

    static const char* hints[] = {
        "",
        "Click a pad to shape its sound - dragging a pad into your DAW includes it",
        "On the pads and STUDIO playback - drags and saves include the effects",
        "Uses the selection if there is one - drag the notes onto a MIDI track",
    };
    deckHint.setText (hints[deck], juce::dontSendNotification);
    resized();
}

void StudioPage::soundUndo (const juce::String& label, bool newGesture)
{
    if (clip == nullptr) return;
    const auto now = juce::Time::getMillisecondCounter();
    if (newGesture || lastSoundUndoMs == 0 || now - lastSoundUndoMs > 1500)
        clip->pushUndo (label);
    lastSoundUndoMs = now;
    lastAdjustUndoMs = 0;
}

void StudioPage::soundChanged()
{
    if (clip == nullptr) return;
    proc.session.clipChanged (clip.get(), false);
    syncSampler();
    pads.repaint();
    refreshInfo();
}

//==============================================================================
void StudioPage::refreshKey()
{
    if (clip == nullptr || clip->keyTonic < 0)
    {
        keyBtn.setButtonText (clip != nullptr && keyJobs.count (clip->id) ? "KEY  ..." : "KEY  -");
        toKeyBox.clear (juce::dontSendNotification);
        toKeyBox.setEnabled (false);
        pitchKnob.repaint();
        return;
    }
    const key::Key k { clip->keyTonic, clip->keyMinor };
    // what you hear: the PITCH knob moves the key (not with TAPE's odd speeds)
    const double st = clip->adjust.semitones;
    const bool whole = std::abs (st - std::round (st)) < 0.05;
    const auto heard = whole ? k.transposed ((int) std::round (st)) : k;
    keyBtn.setButtonText ("KEY  " + key::name (heard) + "  " + key::camelot (heard) + (whole && std::abs (st) > 0.05 ? "*" : ""));

    toKeyBox.clear (juce::dontSendNotification);
    toKeyBox.setEnabled (true);
    toKeyBox.addSectionHeading ("Shift the sample to...");
    for (int i = 0; i < 12; ++i)
    {
        const int tonic = (k.tonic + i) % 12;
        const int s = key::semitonesTo (k, tonic);
        const key::Key target { tonic, k.minor };
        toKeyBox.addItem (key::name (target) + "   " + key::camelot (target) + (s == 0 ? "   (original)" : "   (" + juce::String (s > 0 ? "+" : "") + juce::String (s) + ")"), 100 + tonic);
    }
    if (whole)
        toKeyBox.setSelectedId (100 + heard.tonic, juce::dontSendNotification);
    pitchKnob.repaint();
}

void StudioPage::detectKey (bool quiet)
{
    if (clip == nullptr || clip->audio == nullptr || keyJobs.count (clip->id)) return;
    auto c = clip;
    auto audio = c->adjustBase != nullptr ? c->adjustBase : c->audio;   // the original sound
    keyJobs.insert (c->id);
    refreshKey();
    juce::Component::SafePointer<StudioPage> safe (this);
    auto& p = proc;
    proc.jobs.start ("Finding the key", [audio] (Job& job)
    {
        job.setStatus ("Listening to the harmony");
        const auto k = key::detect (*audio);
        job.ints = { k.tonic, k.minor ? 1 : 0 };
        for (auto& cand : k.candidates) { job.ints.push_back (cand.first); job.ints.push_back (cand.second ? 1 : 0); }
    },
    [safe, c, quiet, &p] (Job& job)
    {
        if (safe != nullptr) safe->keyJobs.erase (c->id);
        if (job.isCancelled() || job.ints.size() < 2) { if (safe != nullptr) safe->refreshKey(); return; }
        if (job.ints[0] >= 0 && (! c->keyManual || ! quiet))
        {
            c->keyTonic = job.ints[0];
            c->keyMinor = job.ints[1] != 0;
            c->keyManual = false;
            p.session.clipChanged (c.get(), false);
        }
        if (safe != nullptr)
        {
            std::vector<std::pair<int, bool>> cands;
            for (size_t i = 2; i + 1 < job.ints.size(); i += 2) cands.push_back ({ job.ints[i], job.ints[i + 1] != 0 });
            safe->keyCandidates[c->id] = cands;
            if (safe->clip == c)
            {
                safe->refreshKey();
                if (! quiet && c->keyTonic >= 0)
                    safe->ctx.toast ("Key: " + key::longName ({ c->keyTonic, c->keyMinor }) + " (" + key::camelot ({ c->keyTonic, c->keyMinor }) + ")");
            }
        }
    });
}

void StudioPage::showKeyMenu()
{
    if (clip == nullptr) return;
    juce::PopupMenu m;
    const key::Key current { clip->keyTonic, clip->keyMinor };
    auto label = [] (int t, bool mi) { const key::Key k { t, mi }; return key::name (k) + "   " + key::camelot (k) + "   " + key::longName (k); };
    if (auto it = keyCandidates.find (clip->id); it != keyCandidates.end() && ! it->second.empty())
    {
        m.addSectionHeader ("Most likely");
        for (auto& cand : it->second)
            m.addItem (1000 + cand.first * 2 + (cand.second ? 1 : 0), label (cand.first, cand.second), true, current == key::Key { cand.first, cand.second });
    }
    juce::PopupMenu majors, minors;
    for (int t = 0; t < 12; ++t)
    {
        majors.addItem (1000 + t * 2, label (t, false), true, current == key::Key { t, false });
        minors.addItem (1000 + t * 2 + 1, label (t, true), true, current == key::Key { t, true });
    }
    m.addSubMenu ("Major keys", majors);
    m.addSubMenu ("Minor keys", minors);
    m.addSeparator();
    m.addItem (1, "Detect again");
    juce::Component::SafePointer<StudioPage> safe (this);
    const Clip::Ptr menuClip = clip;   // (another sample may be on screen by the time a key is picked)
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyBtn), [safe, menuClip] (int r)
    {
        if (safe == nullptr || safe->clip == nullptr || safe->clip != menuClip || r == 0) return;
        if (r == 1) { safe->detectKey (false); return; }
        const int v = r - 1000;
        safe->clip->keyTonic = v / 2;
        safe->clip->keyMinor = (v % 2) == 1;
        safe->clip->keyManual = true;
        safe->proc.session.clipChanged (safe->clip.get(), false);
        safe->refreshKey();
    });
}

StudioPage::~StudioPage()
{
    proc.session.removeChangeListener (this);
}

void StudioPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto sel = proc.session.getSelected();
    if (sel != clip)
        setClip (sel);
    else
    {
        refreshInfo();
        pads.repaint();
        wave.repaint();
    }
}

void StudioPage::timerCallback()
{
    // TARGET BPM follows the DAW. Set it by hand (half-time, say) and it stays, until the DAW's tempo
    // changes. (It used to snap back within a quarter of a second, so it couldn't be set at all.)
    const double host = proc.hostBpm.load();
    if (host > 0 && std::abs (host - lastHostBpm) > 0.005 && ! bpmKnob.slider.isMouseButtonDown())
    {
        lastHostBpm = host;
        targetFollowsHost = true;
        bpmKnob.setValueSilently (host);
    }

    playBtn.setToggleState (proc.isPreviewing() && clip != nullptr && proc.getPreviewSource() == clip->audio.get(), juce::dontSendNotification);
}

void StudioPage::setClip (Clip::Ptr c)
{
    const bool changed = c != clip;
    // A name that is being typed belongs to the sample it was typed for: keep it there before the
    // box moves on (a download or a capture can finish, and select its sample, at any moment).
    if (changed && nameLabel.isBeingEdited())
        nameLabel.hideEditor (false);
    clip = c;
    setKnobsFrom (clip != nullptr ? clip->adjust : Clip::Adjust());
    lastAdjustUndoMs = 0;
    lastSoundUndoMs = 0;
    wave.setClip (c);
    pads.setClip (c);
    if (changed) selectedPad = 0;
    padDeck.setClip (c, selectedPad);
    fxDeck.setClip (c);
    midiDeck.setClip (c);
    pads.setSelected (selectedPad, deck == 1);
    refreshKey();
    // (once per sample: one with no key to find - a drum hit, silence - was analysed again every
    //  time STUDIO was opened; DETECT still tries again when asked)
    if (clip != nullptr && clip->keyTonic < 0 && clip->audio != nullptr && keyTried.insert (clip->id).second)
        detectKey (true);
    overview.repaint();
    syncSampler();
    refreshInfo();

    // The knobs say one thing and the sound another (a render that never landed: cancelled, or the
    // sample was switched away mid-render)? Render it now.
    // (unless exactly that render is on its way already: STUDIO was just opened again)
    if (clip != nullptr && ! clip->renderIsCurrent()
        && ! (adjustJobRunning && renderingClip == clip.get() && renderingAdjust == clip->adjust))
        startAdjustRender();

    const bool has = clip != nullptr;
    for (auto* b : editButtons) b->setEnabled (has);
    for (auto* comp : std::initializer_list<juce::Component*> { &pitchDefaultBtn, &matchBpmBtn, &toneDefaultBtn, &autoChopBtn,
                                                                &equalBtn, &clearChopsBtn, &playBtn, &stopBtn, &saveBtn, &detectBpmBtn, &dragHandle, &keyBtn })
        comp->setEnabled (has);
    dragHandle.setAlpha (has ? 1.0f : 0.4f);
}

void StudioPage::syncSampler()
{
    if (clip != nullptr)
        proc.setSamplerClip (clip, wave.hasSelection() ? wave.getSelectionStart() : 0, wave.hasSelection() ? wave.getSelectionEnd() : -1);
}

int StudioPage::selStartOrZero() const  { return wave.hasSelection() ? wave.getSelectionStart() : 0; }
int StudioPage::selEndOrAll() const     { return wave.hasSelection() ? wave.getSelectionEnd() : -1; }

void StudioPage::refreshInfo()
{
    if (clip == nullptr || clip->audio == nullptr)
    {
        nameLabel.setText ("Studio", juce::dontSendNotification);
        infoLabel.setText ("Nothing loaded yet", juce::dontSendNotification);
        bpmLabel.setText ({}, juce::dontSendNotification);
        selLabel.setText ({}, juce::dontSendNotification);
        undoBtn.setEnabled (false);
        redoBtn.setEnabled (false);
        return;
    }

    const auto& a = *clip->audio;
    if (! nameLabel.isBeingEdited())   // (setText would throw away a name that is being typed)
        nameLabel.setText (clip->name, juce::dontSendNotification);
    juce::String info;
    info << clip->kind.toUpperCase() << "   " << formatTime (a.lengthSeconds(), true) << "s   "
         << juce::String (a.sampleRate / 1000.0, 1) << " kHz   " << (a.getNumChannels() > 1 ? "STEREO" : "MONO");
    if (clip->slices.size() > 0)
        info << "   " << (int) clip->sliceBoundaries().size() - 1 << " CHOPS";
    infoLabel.setText (info, juce::dontSendNotification);
    const double shownBpm = clip->bpmWith (clip->adjust);   // where the knobs are taking it
    bpmLabel.setText (shownBpm > 0 ? juce::String (shownBpm, 1) + " BPM" : "BPM  -", juce::dontSendNotification);

    const double sr = a.sampleRate;
    if (wave.hasSelection())
        selLabel.setText ("SEL  " + formatTime (wave.getSelectionStart() / sr, true) + "  -  " + formatTime (wave.getSelectionEnd() / sr, true)
                          + "   (" + formatTime ((wave.getSelectionEnd() - wave.getSelectionStart()) / sr, true) + "s)", juce::dontSendNotification);
    else
        selLabel.setText ("CURSOR  " + formatTime (wave.getCursor() / sr, true) + "      drag to select - alt+click adds a chop", juce::dontSendNotification);

    undoBtn.setEnabled (clip->canUndo());
    redoBtn.setEnabled (clip->canRedo());
    dragHandle.setCaption (wave.hasSelection() ? "DRAG SELECTION TO DAW" : "DRAG SAMPLE TO DAW");
}

void StudioPage::applyEdit (const juce::String& label, bool needsSelection, SliceFix fix,
                            std::function<AudioData::Ptr (const AudioData&, int, int)> op)
{
    if (clip == nullptr || clip->audio == nullptr)
        return;
    if (needsSelection && ! wave.hasSelection())
    {
        ctx.toast ("Select a region first (drag across the waveform).");
        return;
    }
    const int n = clip->audio->getNumSamples();
    applyEditRange (label, wave.hasSelection() ? wave.getSelectionStart() : 0, wave.hasSelection() ? wave.getSelectionEnd() : n, fix, op);
}

void StudioPage::applyEditRange (const juce::String& label, int s, int e, SliceFix fix,
                                 std::function<AudioData::Ptr (const AudioData&, int, int)> op)
{
    if (clip == nullptr || clip->audio == nullptr)
        return;

    auto result = op (*clip->audio, s, e);
    if (result == nullptr)
        return;

    clip->pushUndo (label);
    lastAdjustUndoMs = 0;
    const AudioData::Ptr before = clip->audio;
    const auto slicesBefore = clip->slices;

    // With the knobs away from default, the edit also goes into the original (same moment in time),
    // so moving a knob later re-renders the edited sound instead of losing the edit.
    AudioData::Ptr newBase;
    if (clip->adjustBase != nullptr)
    {
        const auto& base = *clip->adjustBase;
        const double k = (double) base.getNumSamples() / (double) juce::jmax (1, clip->audio->getNumSamples());
        const int bs = juce::jlimit (0, base.getNumSamples(), juce::roundToInt (s * k));
        const int be = juce::jlimit (bs, base.getNumSamples(), juce::roundToInt (e * k));
        if (label == "Normalize")
        {
            // normalise what you hear: the same gain on the original
            float peak = 0.0f;
            for (int c = 0; c < clip->audio->getNumChannels(); ++c)
                peak = juce::jmax (peak, clip->audio->buffer.getMagnitude (c, s, juce::jmax (0, e - s)));
            if (peak > 1.0e-6f)
                newBase = edit::gain (base, bs, be, -0.3f - juce::Decibels::gainToDecibels (peak));
        }
        else
            newBase = op (base, bs, be);
    }

    clip->audio = result;
    if (newBase != nullptr)
        clip->adjustBase = newBase;

    std::vector<int> fixed;
    for (auto m : clip->slices)
    {
        if (fix == SliceFix::keep) fixed.push_back (m);
        else if (fix == SliceFix::crop) { if (m > s && m < e) fixed.push_back (m - s); }
        else if (fix == SliceFix::remove) { if (m < s) fixed.push_back (m); else if (m >= e) fixed.push_back (m - (e - s)); }
    }
    const int newLen = result->getNumSamples();
    fixed.erase (std::remove_if (fixed.begin(), fixed.end(), [newLen] (int v) { return v <= 0 || v >= newLen; }), fixed.end());
    clip->slices = fixed;

    // the chops that are left keep their own pad sound (they used to inherit a neighbour's)
    if (fix == SliceFix::crop)
        clip->remapPads (slicesBefore, [s] (int pos) { return pos + s; });
    else if (fix == SliceFix::remove)
        clip->remapPads (slicesBefore, [s, e] (int pos) { return pos < s ? pos : pos + (e - s); });

    if (fix == SliceFix::crop || fix == SliceFix::remove)
        wave.clearSelection();

    proc.session.clipChanged (clip.get());
    wave.audioChanged();
    syncSampler();
    chopsChanged();
    overview.repaint();
    resumePlayback (before.get(), 1.0, fix == SliceFix::keep);   // what's playing is the edited sound, not the old one

    if (newBase != nullptr)
        startAdjustRender();   // exact version of what's on screen
}

//==============================================================================
Clip::Adjust StudioPage::knobsToAdjust() const
{
    Clip::Adjust a;
    a.semitones = (float) (std::isnan (exactSemitones) ? pitchKnob.slider.getValue() : exactSemitones);
    a.length    = (exactLength > 0.0 ? exactLength : stretchKnob.slider.getValue()) / 100.0;
    a.formants  = formantToggle.getToggleState();
    a.tape      = tapeToggle.getToggleState();
    a.gainDb    = (float) gainKnob.slider.getValue();
    const double lo = lowCutKnob.slider.getValue(), hi = highCutKnob.slider.getValue();
    a.lowCut  = lo < 10.0 ? 0.0f : (float) lo;
    a.highCut = hi > 19900.0 ? 0.0f : (float) hi;
    return a;
}

void StudioPage::setKnobsFrom (const Clip::Adjust& a)
{
    setPitchKnob (a.semitones);
    setLengthKnob (a.length * 100.0);
    formantToggle.setToggleState (a.formants, juce::dontSendNotification);
    tapeToggle.setToggleState (a.tape, juce::dontSendNotification);
    gainKnob.setValueSilently (a.gainDb);
    lowCutKnob.setValueSilently (a.lowCut > 0.0f ? a.lowCut : lowCutKnob.getDefault());
    highCutKnob.setValueSilently (a.highCut > 0.0f ? a.highCut : highCutKnob.getDefault());
}

void StudioPage::setLengthKnob (double percent)
{
    percent = juce::jlimit (stretchKnob.slider.getMinimum(), stretchKnob.slider.getMaximum(), percent);
    exactLength = std::abs (percent - std::round (percent)) > 1.0e-4 ? percent : 0.0;
    stretchKnob.setValueSilently (percent);
}

void StudioPage::setPitchKnob (double semitones)
{
    semitones = juce::jlimit (pitchKnob.slider.getMinimum(), pitchKnob.slider.getMaximum(), semitones);
    exactSemitones = std::abs (semitones - std::round (semitones * 10.0) / 10.0) > 1.0e-4 ? semitones
                                                                                          : std::numeric_limits<double>::quiet_NaN();
    pitchKnob.setValueSilently (semitones);
}

double StudioPage::targetBpm() const
{
    return targetFollowsHost && lastHostBpm > 0 ? lastHostBpm : bpmKnob.slider.getValue();
}

void StudioPage::chopsChanged()
{
    const int count = clip != nullptr && clip->audio != nullptr ? (int) clip->sliceBoundaries().size() - 1 : 0;
    selectedPad = juce::jlimit (0, juce::jmax (0, count - 1), selectedPad);
    padDeck.setClip (clip, selectedPad);
    pads.setSelected (selectedPad, deck == 1);
    pads.repaint();
    refreshInfo();
}

void StudioPage::resumePlayback (const AudioData* oldAudio, double ratio, bool keepPosition)
{
    if (clip == nullptr || clip->audio == nullptr || oldAudio == nullptr)
        return;
    if (! proc.isPreviewing() || proc.getPreviewSource() != oldAudio)
        return;
    if (! keepPosition)
    {
        proc.stopPreview();
        return;
    }
    const int n = clip->audio->getNumSamples();
    const int from = juce::jlimit (0, juce::jmax (0, n - 1), juce::roundToInt (proc.getPreviewPosition() * ratio));
    const bool loop = loopBtn.getToggleState();
    if (wave.hasSelection() && from >= wave.getSelectionStart() && from < wave.getSelectionEnd())
        proc.previewClip (*clip, wave.getSelectionStart(), wave.getSelectionEnd(), loop, true, from);
    else
        proc.previewClip (*clip, 0, -1, loop, true, from);
}

void StudioPage::knobsChanged()
{
    if (clip == nullptr || clip->audio == nullptr)
        return;
    const auto a = knobsToAdjust();
    if (a == clip->adjust)
        return;

    // one undo step per knob grab / button press, however long the drag
    const auto now = juce::Time::getMillisecondCounter();
    if (lastAdjustUndoMs == 0 || now - lastAdjustUndoMs > 1500)
        clip->pushUndo ("Pitch / tone");
    lastAdjustUndoMs = now;

    if (clip->adjustBase == nullptr)
        clip->adjustBase = clip->audio;   // the original, kept until everything is back at default
    clip->adjust = a;
    refreshInfo();
    refreshKey();

    // re-render shortly after the knob stops moving
    const auto serial = ++adjustSerial;
    juce::Component::SafePointer<StudioPage> safe (this);
    juce::Timer::callAfterDelay (140, [safe, serial]
    {
        if (safe != nullptr && safe->adjustSerial == serial)
            safe->startAdjustRender();
    });
}

void StudioPage::startAdjustRender()
{
    if (clip == nullptr || clip->adjustBase == nullptr)
        return;
    if (adjustJobRunning)
    {
        adjustPending = true;   // render again with the latest settings once this one lands
        return;
    }

    auto c = clip;
    auto base = c->adjustBase;
    const auto adj = c->adjust;
    auto& p = proc;
    juce::Component::SafePointer<StudioPage> safe (this);

    // Applies a finished render to the clip - also when the window was closed meanwhile.
    auto land = [&p, c, base, adj, safe] (AudioData::Ptr result)
    {
        if (result == nullptr || c->adjust != adj || c->adjustBase != base)
            return;   // knobs moved on, or undo: a newer render is coming
        auto old = c->audio;
        const double ratio = old != nullptr && old->getNumSamples() > 0 ? (double) result->getNumSamples() / (double) old->getNumSamples() : 1.0;
        for (auto& s : c->slices)
            s = (int) std::round (s * ratio);
        c->audio = result;
        c->audioAdjust = adj;          // the sound and the knobs agree again
        if (adj.isNeutral())
        {
            c->adjustBase = nullptr;   // back to the original
            c->audioAdjust = {};
        }
        p.session.clipChanged (c.get());
        if (safe != nullptr && safe->clip == c)
            safe->adjustRendered (old);
        else if (p.getSamplerClip() == c)
            p.setSamplerClip (c);
    };

    if (adj.isNeutral())
    {
        land (base);
        return;
    }

    adjustJobRunning = true;
    renderingClip = c.get();
    renderingAdjust = adj;
    proc.jobs.start (adj.pitchNeutral() ? "Tone" : (adj.tape ? "Varispeed" : "Pitch & Time"), [base, adj] (Job& job)
    {
        job.setStatus ("Rendering");
        job.audio = edit::renderAdjust (*base, adj);
    },
    [safe, land, &p, c, base, adj] (Job& job)
    {
        if (! job.isCancelled() && job.audio != nullptr)
            land (job.audio);
        else if (c->adjust == adj && c->adjustBase == base)
        {
            // Cancelled (the x in the title bar stops every job) or failed: the knobs go back to what
            // you actually hear, instead of showing a pitch / length that was never applied.
            c->adjust = c->audioAdjust;
            if (c->adjust.isNeutral() && c->adjustBase == c->audio)
                c->adjustBase = nullptr;
            if (job.hasFailed())
                actions::notify (p, "Couldn't apply that: " + job.getError(), true);
            if (safe != nullptr && safe->clip == c)
            {
                safe->setKnobsFrom (c->adjust);
                safe->refreshInfo();
                safe->refreshKey();
            }
        }
        if (safe != nullptr)
        {
            safe->adjustJobRunning = false;
            if (safe->adjustPending)
            {
                safe->adjustPending = false;
                safe->startAdjustRender();
            }
        }
    });
}

void StudioPage::adjustRendered (AudioData::Ptr oldAudio)
{
    const int oldLen = oldAudio != nullptr ? oldAudio->getNumSamples() : 0;
    const int newLen = clip->audio->getNumSamples();
    const double ratio = oldLen > 0 ? (double) newLen / (double) oldLen : 1.0;

    if (wave.hasSelection() && std::abs (ratio - 1.0) > 1.0e-6)
        wave.setSelection (juce::roundToInt (wave.getSelectionStart() * ratio), juce::roundToInt (wave.getSelectionEnd() * ratio));

    wave.audioChanged();
    syncSampler();
    refreshInfo();
    pads.repaint();
    overview.repaint();

    // keep playing from the same moment, now with the new sound
    resumePlayback (oldAudio.get(), ratio);
}

void StudioPage::play()
{
    if (clip == nullptr) return;
    if (wave.hasSelection())
        proc.previewClip (*clip, wave.getSelectionStart(), wave.getSelectionEnd(), loopBtn.getToggleState(), true);
    else
        proc.previewClip (*clip, wave.getCursor(), -1, loopBtn.getToggleState(), true);
}

void StudioPage::stop()      { proc.stopPreview(); }

void StudioPage::undo()
{
    if (clip == nullptr || ! clip->canUndo()) return;
    const AudioData::Ptr before = clip->audio;
    auto what = clip->undo();
    setKnobsFrom (clip->adjust);
    lastAdjustUndoMs = 0;
    lastSoundUndoMs = 0;
    fxDeck.setClip (clip);
    proc.session.clipChanged (clip.get());
    wave.audioChanged();
    syncSampler();
    chopsChanged();
    refreshKey();
    if (before != clip->audio)   // (a marker, pad or FX step leaves what is playing alone)
        resumePlayback (before.get(), 1.0, before != nullptr && before->getNumSamples() == clip->audio->getNumSamples());
    if (! clip->renderIsCurrent())
        startAdjustRender();   // this step was taken while a knob change was still rendering: finish it
    ctx.toast ("Undid " + what);
}

void StudioPage::redo()
{
    if (clip == nullptr || ! clip->canRedo()) return;
    const AudioData::Ptr before = clip->audio;
    auto what = clip->redo();
    setKnobsFrom (clip->adjust);
    lastAdjustUndoMs = 0;
    lastSoundUndoMs = 0;
    fxDeck.setClip (clip);
    proc.session.clipChanged (clip.get());
    wave.audioChanged();
    syncSampler();
    chopsChanged();
    refreshKey();
    if (before != clip->audio)   // (a marker, pad or FX step leaves what is playing alone)
        resumePlayback (before.get(), 1.0, before != nullptr && before->getNumSamples() == clip->audio->getNumSamples());
    if (! clip->renderIsCurrent())
        startAdjustRender();   // this step was taken while a knob change was still rendering: finish it
    ctx.toast ("Redid " + what);
}

void StudioPage::detectBpm (std::function<void()> then)
{
    if (clip == nullptr || clip->audio == nullptr) return;
    if (analysing.count (clip->id))
    {
        ctx.toast ("Still working on this sample - one moment.");
        return;
    }
    const bool sel = wave.hasSelection();
    const int s = selStartOrZero();
    const int e = selEndOrAll() < 0 ? clip->audio->getNumSamples() : selEndOrAll();
    auto c = clip;
    auto audio = clip->audio;
    auto measure = [audio, sel, s, e] { return edit::estimateBpm (sel ? *edit::crop (*audio, s, e) : *audio); };

    auto& p = proc;
    juce::Component::SafePointer<StudioPage> safe (this);
    auto finish = [&p, safe, c, audio, then] (double bpm)
    {
        if (c->audio != audio)
        {
            actions::notify (p, "The sample changed while its tempo was being measured - click DETECT again.", true);
            return;
        }
        if (bpm <= 0)
        {
            actions::notify (p, "Couldn't find a steady tempo in this sample.", true);
            if (then) then();
            return;
        }
        // stored for the original, so LENGTH / MATCH BPM always start from the same place. What was
        // measured is the sound as it is rendered now: stretched by LENGTH, or sped up by TAPE pitch.
        const auto& heard = c->audioAdjust;
        c->bpm = c->adjustBase == nullptr ? bpm
               : heard.tape ? bpm / std::pow (2.0, heard.semitones / 12.0)
                            : bpm * heard.length;
        p.session.clipChanged (c.get(), false);
        if (safe != nullptr && safe->clip == c)
            safe->refreshInfo();
        actions::notify (p, "Tempo: " + juce::String (bpm, 1) + " BPM");
        if (then) then();
    };

    const double seconds = (double) (sel ? e - s : audio->getNumSamples()) / juce::jmax (1.0, audio->sampleRate);
    if (seconds <= 45.0)   // (as quickToAnalyse: what is listened to here can be just the selection)
    {
        finish (measure());
        return;
    }

    analysing.insert (c->id);
    auto result = std::make_shared<double> (0.0);
    proc.jobs.start ("Finding the tempo", [measure, result] (Job& job)
    {
        job.setStatus ("Listening to the rhythm");
        *result = measure();
    },
    [safe, c, result, finish] (Job& job)
    {
        if (safe != nullptr) safe->analysing.erase (c->id);
        if (! job.isCancelled())
            finish (*result);
    });
}

void StudioPage::matchBpm()
{
    if (clip == nullptr) return;
    if (clip->bpm <= 0)
    {
        // (the tempo isn't known yet: find it first - which is a background job for a long sample)
        juce::Component::SafePointer<StudioPage> safe (this);
        detectBpm ([safe, c = clip]
        {
            if (safe != nullptr && safe->clip == c && c->bpm > 0)
                safe->matchBpm();
        });
        return;
    }
    const double wanted = targetBpm();
    double target = wanted;
    // too far for the LENGTH knob (50 - 200%)? half or double time sits on the same grid
    if (clip->bpm / target < 0.5)      target *= 0.5;
    else if (clip->bpm / target > 2.0) target *= 2.0;
    const double ratio = clip->bpm / target;   // new length, relative to the original
    if (ratio < 0.5 || ratio > 2.0) { ctx.toast ("That's too big a tempo change (the LENGTH knob goes from 50% to 200%).", true); return; }

    // The exact length, not the nearest whole percent: 87 BPM to 120 is 72.5%, and at 73% a 4-bar
    // loop ended a twentieth of a second late while the message said "matched".
    lastAdjustUndoMs = 0;
    if (tapeToggle.getToggleState())
        setPitchKnob (12.0 * std::log2 (1.0 / ratio));
    setLengthKnob (ratio * 100.0);
    knobsChanged();
    ctx.toast ("Matched " + juce::String (clip->bpm, 1) + " BPM to " + juce::String (target, 1) + " BPM"
               + (std::abs (target - wanted) > 0.01 ? (target < wanted ? " (half time of " : " (double time of ") + juce::String (wanted, 1) + ")" : juce::String()));
}

void StudioPage::rename()
{
    if (clip == nullptr) return;
    auto t = nameLabel.getText().trim();
    if (t.isEmpty()) { nameLabel.setText (clip->name, juce::dontSendNotification); return; }
    clip->name = t;
    proc.session.clipChanged (clip.get(), false);
}

bool StudioPage::handleKey (const juce::KeyPress& k)
{
    // Typing somewhere (a knob value, the sample's name): the keys belong to that box. A box with
    // nothing left to undo lets Cmd/Ctrl+Z through, and that used to undo the last edit of the sample.
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;

    // Covered by Settings (or another overlay): Delete must not cut a selection nobody can see.
    for (juce::Component* c = this; c->getParentComponent() != nullptr; c = c->getParentComponent())
    {
        auto* parent = c->getParentComponent();
        const auto middle = parent->getLocalPoint (this, getLocalBounds().getCentre());
        for (int i = parent->getNumChildComponents(); --i > parent->getIndexOfChildComponent (c);)
        {
            auto* above = parent->getChildComponent (i);
            if (above->isVisible() && above->getBounds().contains (middle) && dynamic_cast<juce::TooltipWindow*> (above) == nullptr
                && above->hitTest (middle.x - above->getX(), middle.y - above->getY()))
                return false;
        }
    }

    if (k == juce::KeyPress::spaceKey)
    {
        if (proc.isPreviewing()) stop(); else play();
        return true;
    }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))                                  { undo(); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { redo(); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        if (wave.hasSelection()) editButtons[1]->triggerClick();
        return true;
    }
    return false;
}

//==============================================================================
void StudioPage::chopCountChanged()
{
    const bool every = chopCount.getSelectedId() == everyHit;
    sensKnob.setEnabled (every);
    sensKnob.setAlpha (every ? 1.0f : 0.4f);
    sensKnob.setTooltip (every ? "How picky AUTO CHOP is: higher finds quieter hits (more chops)"
                               : "Only used with \"Every hit\" - with a number, AUTO CHOP picks the strongest hits");
    const auto n = juce::String (chopCount.getSelectedId());
    autoChopBtn.setTooltip (every ? "Put a chop on every hit (SENSITIVITY sets how many)"
                                  : "Cut into " + n + " chops at the strongest hits");
    equalBtn.setTooltip (every ? "Cut into equal chops - pick how many in the list"
                               : "Cut into " + n + " equal chops (great for loops)");
}

StudioPage::ChopFind StudioPage::findChops (const AudioData& audio, const ChopAsk& ask, const std::vector<int>& slicesNow)
{
    const int len = audio.getNumSamples();
    ChopFind r;
    r.found = ask.every ? edit::detectTransients (audio, ask.sensitivity)
                        : edit::chopAtStrongestHits (audio, ask.count, ask.a, ask.b);

    // There are 32 pads. "Every hit" on a long sample can find far more: keep the 32 strongest
    // rather than leave chops that no pad shows (and that can't be dragged or shaped).
    if (ask.every && ! ask.sel && (int) r.found.size() > maxPads - 1)
    {
        r.hitsFound = (int) r.found.size() + 1;
        r.found = edit::chopAtStrongestHits (audio, maxPads, 0, len);
    }
    else if (ask.every && ask.sel)
    {
        // (the same inside a selection: the markers that stay elsewhere use up pads too)
        int others = (ask.a > 0 ? 1 : 0) + (ask.b < len ? 1 : 0), inside = 0;
        for (auto s : slicesNow) if ((s < ask.a || s > ask.b) && s > 0 && s < len) ++others;
        for (auto s : r.found)   if (s > ask.a && s < ask.b) ++inside;
        const int room = juce::jmax (0, maxPads - 1 - others);
        if (inside > room)
        {
            r.hitsFound = inside + 1;
            r.found = edit::chopAtStrongestHits (audio, room + 1, ask.a, ask.b);
        }
    }
    return r;
}

void StudioPage::autoChop()
{
    if (clip == nullptr || clip->audio == nullptr) return;
    if (analysing.count (clip->id))
    {
        ctx.toast ("Still working on this sample - one moment.");
        return;
    }
    const int len = clip->audio->getNumSamples();
    ChopAsk ask;
    ask.count = chopCount.getSelectedId();
    ask.every = ask.count == everyHit;
    ask.sel = wave.hasSelection();
    ask.a = ask.sel ? wave.getSelectionStart() : 0;
    ask.b = ask.sel ? wave.getSelectionEnd() : len;
    ask.sensitivity = (float) sensKnob.slider.getValue() / 100.0f;

    auto c = clip;
    auto audio = clip->audio;
    if (quickToAnalyse (*audio))
    {
        applyChops (c, findChops (*audio, ask, c->slices), ask);
        return;
    }

    // a long sample: look for the hits in the background
    analysing.insert (c->id);
    auto result = std::make_shared<ChopFind>();
    const auto slicesThen = c->slices;
    juce::Component::SafePointer<StudioPage> safe (this);
    proc.jobs.start ("Auto chop", [audio, ask, slicesThen, result] (Job& job)
    {
        job.setStatus ("Finding the hits");
        *result = findChops (*audio, ask, slicesThen);
    },
    [safe, c, audio, slicesThen, ask, result] (Job& job)
    {
        if (safe == nullptr) return;
        safe->analysing.erase (c->id);
        if (job.isCancelled()) return;
        if (c->audio != audio || c->slices != slicesThen)
        {
            safe->ctx.toast ("The sample changed while AUTO CHOP was looking at it - click it again.", true);
            return;
        }
        safe->applyChops (c, *result, ask);
    });
}

void StudioPage::applyChops (Clip::Ptr c, const ChopFind& find, const ChopAsk& ask)
{
    if (c == nullptr || c->audio == nullptr) return;
    const int len = c->audio->getNumSamples();
    const bool every = ask.every, sel = ask.sel;
    const int a = ask.a, b = ask.b, count = ask.count, hitsFound = find.hitsFound;
    auto found = find.found;

    const auto slicesBefore = c->slices;
    if (sel)
    {
        // only chop inside the selection, keep markers elsewhere
        std::vector<int> keep;
        for (auto s : c->slices) if (s < a || s > b) keep.push_back (s);
        for (auto s : found) if (s > a && s < b) keep.push_back (s);
        keep.push_back (a);
        keep.push_back (b);
        std::sort (keep.begin(), keep.end());
        keep.erase (std::unique (keep.begin(), keep.end()), keep.end());
        keep.erase (std::remove_if (keep.begin(), keep.end(), [len] (int v) { return v <= 0 || v >= len; }), keep.end());
        found = keep;
    }
    c->pushUndo ("Auto chop");
    c->slices = found;
    // new chops start with a clean sound (UNDO brings the old ones back); with a selection,
    // the chops outside it are left alone - their pad sounds too
    if (sel)
        c->remapPads (slicesBefore, [a, b] (int pos) { return pos >= a && pos < b ? -1 : pos; });
    else
        c->pads.clear();
    proc.session.clipChanged (c.get(), false);
    if (c == clip)   // (a long sample's chops can arrive after another sample was opened)
    {
        selectedPad = 0;
        syncSampler();
        chopsChanged();
        wave.repaint();
    }

    const int total = (int) c->sliceBoundaries().size() - 1;
    const auto where = "play them from pads " + noteName (proc.rootNote.load()) + " and up";
    if (hitsFound > 0 && sel)
        ctx.toast ("Found " + juce::String (hitsFound) + " hits in the selection - kept the strongest, so every chop has a pad ("
                   + juce::String (total) + " in all) - " + where);
    else if (hitsFound > 0)
        ctx.toast ("Found " + juce::String (hitsFound) + " hits - kept the " + juce::String (total) + " strongest (one per pad) - " + where);
    else if (every)
        ctx.toast (juce::String (total) + " chops, one per hit - " + where);
    else if (sel)
        ctx.toast (juce::String (count) + " chops in the selection (" + juce::String (total) + " in all) - " + where);
    else
        ctx.toast (juce::String (total) + " chops at the strongest hits - " + where);
}

void StudioPage::equalChop()
{
    if (clip == nullptr || clip->audio == nullptr) return;
    const int n = chopCount.getSelectedId();
    if (n == everyHit)
    {
        ctx.toast ("Pick how many equal chops first");
        chopCount.showPopup();
        return;
    }
    clip->pushUndo ("Equal chop");
    selectedPad = 0;
    if (wave.hasSelection())
    {
        std::vector<int> s;
        const int a = wave.getSelectionStart(), b = wave.getSelectionEnd();
        const auto slicesBefore = clip->slices;
        for (auto v : clip->slices) if (v < a || v > b) s.push_back (v);
        s.push_back (a);
        for (int i = 1; i < n; ++i) s.push_back (a + (int) ((juce::int64) (b - a) * i / n));
        s.push_back (b);
        std::sort (s.begin(), s.end());
        s.erase (std::unique (s.begin(), s.end()), s.end());
        s.erase (std::remove_if (s.begin(), s.end(), [len = clip->audio->getNumSamples()] (int v) { return v <= 0 || v >= len; }), s.end());
        clip->slices = s;
        clip->remapPads (slicesBefore, [a, b] (int pos) { return pos >= a && pos < b ? -1 : pos; });   // chops outside keep their sound
    }
    else
    {
        clip->slices = edit::equalSlices (clip->audio->getNumSamples(), n);
        clip->pads.clear();
    }
    proc.session.clipChanged (clip.get(), false);
    syncSampler();
    chopsChanged();
    wave.repaint();
    ctx.toast (juce::String ((int) clip->sliceBoundaries().size() - 1) + " equal chops - play them from pads "
               + noteName (proc.rootNote.load()) + " and up");
}

void StudioPage::resized()
{
    auto r = getLocalBounds().reduced (14, 10);

    auto head = r.removeFromTop (40);
    headGlass = head;
    head = head.reduced (14, 3);
    redoBtn.setBounds (head.removeFromRight (30).reduced (2));
    undoBtn.setBounds (head.removeFromRight (30).reduced (2));
    head.removeFromRight (10);
    detectBpmBtn.setBounds (head.removeFromRight (70).reduced (0, 6));
    head.removeFromRight (6);
    bpmLabel.setBounds (head.removeFromRight (84));
    head.removeFromRight (8);
    keyBtn.setBounds (head.removeFromRight (122).reduced (0, 6));
    head.removeFromRight (6);
    const int nameW = juce::jmin (420, (int) (head.getWidth() * 0.45));
    nameLabel.setBounds (head.removeFromLeft (nameW));
    infoLabel.setBounds (head.withTrimmedLeft (8));

    r.removeFromTop (6);
    overview.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);

    auto tools = r.removeFromBottom (158);
    auto tabRow = r.removeFromBottom (34);
    r.removeFromBottom (6);
    pads.setBounds (r.removeFromBottom (50));
    r.removeFromBottom (8);
    auto transport = r.removeFromBottom (38);
    r.removeFromBottom (8);
    wave.setBounds (r);

    // transport row
    playBtn.setBounds (transport.removeFromLeft (92));
    transport.removeFromLeft (6);
    stopBtn.setBounds (transport.removeFromLeft (40));
    transport.removeFromLeft (6);
    loopBtn.setBounds (transport.removeFromLeft (40));
    transport.removeFromLeft (14);
    dragHandle.setBounds (transport.removeFromRight (220));
    transport.removeFromRight (8);
    saveBtn.setBounds (transport.removeFromRight (92));
    transport.removeFromRight (14);
    readoutGlass = transport;
    transport = transport.reduced (12, 0);
    fitBtn.setBounds (transport.removeFromRight (44).reduced (0, 6));
    zoomInBtn.setBounds (transport.removeFromRight (32).reduced (2, 7));
    zoomOutBtn.setBounds (transport.removeFromRight (32).reduced (2, 7));
    transport.removeFromRight (8);
    selLabel.setBounds (transport);

    // tool tabs
    {
        auto t = tabRow;
        for (auto* b : deckTabs)
        {
            b->setBounds (t.removeFromLeft (120));
            t.removeFromLeft (6);
        }
        deckHint.setBounds (t.withTrimmedRight (6));
    }

    // tool panels
    const int gap = 10;
    const int totalW = tools.getWidth() - 3 * gap;
    const auto allTools = tools;
    editPanel.setBounds (tools.removeFromLeft ((int) (totalW * 0.25)));
    tools.removeFromLeft (gap);
    pitchPanel.setBounds (tools.removeFromLeft ((int) (totalW * 0.29)));
    tools.removeFromLeft (gap);
    tonePanel.setBounds (tools.removeFromLeft ((int) (totalW * 0.21)));
    tools.removeFromLeft (gap);
    chopPanel.setBounds (tools);
    toKeyBox.setBounds (pitchPanel.getBounds().reduced (12, 5).removeFromTop (20).removeFromRight (juce::jmin (150, pitchPanel.getWidth() / 2)));
    if (deck == 1)
    {
        auto t = allTools;
        chopPanel.setBounds (t.removeFromRight (tools.getWidth()));
        t.removeFromRight (gap);
        padDeck.setBounds (t);
    }
    fxDeck.setBounds (allTools);
    midiDeck.setBounds (allTools);

    // edit grid 2 x 4
    {
        auto c = editPanel.getContentBounds().translated (editPanel.getX(), editPanel.getY());
        const int rows = 4, cols = 2;
        const int bw = (c.getWidth() - 6) / cols, bh = (c.getHeight() - 3 * 6) / rows;
        for (int i = 0; i < editButtons.size(); ++i)
        {
            const int row = i / cols, col = i % cols;
            editButtons[i]->setBounds (c.getX() + col * (bw + 6), c.getY() + row * (bh + 6), bw, bh);
        }
    }

    // pitch & time
    {
        auto c = pitchPanel.getContentBounds().translated (pitchPanel.getX(), pitchPanel.getY());
        auto right = c.removeFromRight (juce::jmin (130, c.getWidth() / 3));
        const int kw = c.getWidth() / 3;
        pitchKnob.setBounds (c.removeFromLeft (kw));
        stretchKnob.setBounds (c.removeFromLeft (kw));
        bpmKnob.setBounds (c);
        right.removeFromLeft (6);
        formantToggle.setBounds (right.removeFromTop (26));
        tapeToggle.setBounds (right.removeFromTop (26));
        right.removeFromTop (4);
        pitchDefaultBtn.setBounds (right.removeFromTop (juce::jmin (30, right.getHeight() / 2 - 3)));
        right.removeFromTop (6);
        matchBpmBtn.setBounds (right.removeFromTop (juce::jmin (30, right.getHeight())));
    }

    // tone
    {
        auto c = tonePanel.getContentBounds().translated (tonePanel.getX(), tonePanel.getY());
        auto buttons = c.removeFromBottom (28);
        const int kw = c.getWidth() / 3;
        gainKnob.setBounds (c.removeFromLeft (kw));
        lowCutKnob.setBounds (c.removeFromLeft (kw));
        highCutKnob.setBounds (c);
        toneDefaultBtn.setBounds (buttons.withSizeKeepingCentre (juce::jmin (buttons.getWidth(), 160), buttons.getHeight()));
    }

    // chop & play
    {
        auto c = chopPanel.getContentBounds().translated (chopPanel.getX(), chopPanel.getY());
        sensKnob.setBounds (c.removeFromLeft (juce::jmin (84, c.getWidth() / 4)));
        c.removeFromLeft (8);
        const int rowH = (c.getHeight() - 12) / 3;
        auto row1 = c.removeFromTop (rowH);
        c.removeFromTop (6);
        auto row2 = c.removeFromTop (rowH);
        c.removeFromTop (6);
        auto row3 = c;

        autoChopBtn.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 3));
        row1.removeFromLeft (6);
        clearChopsBtn.setBounds (row1);

        chopCount.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 3));
        row2.removeFromLeft (6);
        equalBtn.setBounds (row2);

        midiModeBox.setBounds (row3.removeFromLeft (row3.getWidth() - 116));
        row3.removeFromLeft (6);
        oneShotToggle.setBounds (row3);
    }
}

void StudioPage::paint (juce::Graphics& g)
{
    // black glass displays set into the gold: clip info on top, selection read-out next to the transport
    glassWindow (g, headGlass.toFloat().reduced (3.0f, 1.0f), 8.0f, 0.8f);
    glassWindow (g, readoutGlass.toFloat().reduced (0.0f, 1.0f), 8.0f, 0.6f);
}

} // namespace snag
