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
    return juce::jmin (32, (int) clip->sliceBoundaries().size() - 1);
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
            goldBorder (g, r.reduced (0.6f), 6.0f, 1.0f, 0.3f);

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
    if (pressed >= 0)
    {
        proc.sendUiNote (proc.rootNote.load() + pressed, 0.9f);
        if (onSliceSelected) onSliceSelected (pressed);
    }
    repaint();
}

void SlicePads::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0 || dragStarted || e.getDistanceFromDragStart() < 8 || clip == nullptr)
        return;

    dragStarted = true;
    proc.sendUiNote (proc.rootNote.load() + pressed, 0.0f);
    auto b = clip->sliceBoundaries();
    const int idx = pressed;
    pressed = -1;
    repaint();
    if (idx + 1 < (int) b.size())
    {
        auto f = actions::makeDragFile (proc, *clip, b[(size_t) idx], b[(size_t) idx + 1], " - chop " + juce::String (idx + 1));
        DragHandle::startExternalDrag (*this, f);
    }
}

void SlicePads::mouseUp (const juce::MouseEvent&)
{
    if (pressed >= 0)
        proc.sendUiNote (proc.rootNote.load() + pressed, 0.0f);
    pressed = -1;
    repaint();
}

//==============================================================================
StudioPage::StudioPage (EditorContext& c)
    : ctx (c), proc (c.getProcessor()), wave (c.getProcessor()), pads (c.getProcessor())
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
    detectBpmBtn.onClick = [this] { detectBpm(); };
    addAndMakeVisible (detectBpmBtn);

    undoBtn.setTooltip ("Undo (Cmd/Ctrl+Z)");
    redoBtn.setTooltip ("Redo (Shift+Cmd/Ctrl+Z)");
    undoBtn.onClick = [this] { undo(); };
    redoBtn.onClick = [this] { redo(); };
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (redoBtn);

    addAndMakeVisible (overview);
    addAndMakeVisible (wave);
    wave.onSelectionChanged = [this] { refreshInfo(); syncSampler(); overview.repaint(); };
    wave.onSlicesChanged = [this]
    {
        if (clip != nullptr) proc.session.clipChanged (clip.get(), false);
        syncSampler();
        pads.repaint();
    };
    wave.onViewChanged = [this] { overview.repaint(); };
    wave.onPlayFrom = [this] (int from)
    {
        if (clip != nullptr) proc.previewClip (*clip, from, -1, loopBtn.getToggleState());
    };

    // transport
    playBtn.setTooltip ("Play the selection (or from the cursor) - Space");
    playBtn.onClick = [this] { play(); };
    stopBtn.onClick = [this] { stop(); };
    loopBtn.setClickingTogglesState (true);
    loopBtn.setTooltip ("Loop playback");
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
    addAndMakeVisible (dragHandle);

    pads.onSliceSelected = [this] (int idx)
    {
        if (clip == nullptr) return;
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
    pitchKnob.formatter = [] (double v)
    {
        const bool whole = std::abs (v - std::round (v)) < 0.05;
        const auto num = whole ? juce::String ((int) std::round (v)) : juce::String (v, 1);
        return (v > 0.05 ? "+" : "") + num + " st";
    };
    bpmKnob.formatter = [] (double v) { return juce::String (v, 1); };
    formantToggle.setToggleState (true, juce::dontSendNotification);
    formantToggle.setTooltip ("Keep formants: voices stay natural when pitching (no chipmunk)");
    tapeToggle.setTooltip ("Old-school varispeed: pitch and speed change together");
    pitchDefaultBtn.setTooltip ("Back to the original pitch and length");
    matchBpmBtn.setTooltip ("Set LENGTH so the sample plays at the target BPM");
    pitchDefaultBtn.onClick = [this]
    {
        pitchKnob.setValueSilently (pitchKnob.getDefault());
        stretchKnob.setValueSilently (stretchKnob.getDefault());
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
        k->onChange = [this] { knobsChanged(); };
        k->onGestureStart = [this] { lastAdjustUndoMs = 0; };   // each new grab is its own undo step
    }
    formantToggle.onClick = [this] { lastAdjustUndoMs = 0; knobsChanged(); };
    tapeToggle.onClick    = [this] { lastAdjustUndoMs = 0; knobsChanged(); };

    // chop & play
    setStyle (autoChopBtn, "red");
    autoChopBtn.setTooltip ("Find the hits and put a chop on each one");
    autoChopBtn.onClick = [this]
    {
        if (clip == nullptr) return;
        auto found = edit::detectTransients (*clip->audio, (float) sensKnob.slider.getValue() / 100.0f);
        if (wave.hasSelection())
        {
            // only chop inside the selection, keep markers elsewhere
            std::vector<int> keep;
            for (auto s : clip->slices) if (s < wave.getSelectionStart() || s > wave.getSelectionEnd()) keep.push_back (s);
            for (auto s : found) if (s > wave.getSelectionStart() && s < wave.getSelectionEnd()) keep.push_back (s);
            keep.push_back (wave.getSelectionStart());
            keep.push_back (wave.getSelectionEnd());
            std::sort (keep.begin(), keep.end());
            keep.erase (std::unique (keep.begin(), keep.end()), keep.end());
            keep.erase (std::remove_if (keep.begin(), keep.end(), [n = clip->audio->getNumSamples()] (int v) { return v <= 0 || v >= n; }), keep.end());
            found = keep;
        }
        clip->pushUndo ("Auto chop");
        clip->slices = found;
        proc.session.clipChanged (clip.get(), false);
        syncSampler();
        wave.repaint(); pads.repaint();
        ctx.toast (juce::String ((int) clip->sliceBoundaries().size() - 1) + " chops - play them from pads " + noteName (proc.rootNote.load()) + " and up");
    };

    for (int n : { 2, 4, 8, 16, 32 })
        equalCount.addItem (juce::String (n) + " slices", n);
    equalCount.setSelectedId (8, juce::dontSendNotification);
    equalBtn.setTooltip ("Chop into equal slices (great for loops)");
    equalBtn.onClick = [this]
    {
        if (clip == nullptr) return;
        clip->pushUndo ("Equal chop");
        const int n = equalCount.getSelectedId();
        if (wave.hasSelection())
        {
            std::vector<int> s;
            const int a = wave.getSelectionStart(), b = wave.getSelectionEnd();
            s.push_back (a);
            for (int i = 1; i < n; ++i) s.push_back (a + (int) ((juce::int64) (b - a) * i / n));
            s.push_back (b);
            s.erase (std::remove_if (s.begin(), s.end(), [len = clip->audio->getNumSamples()] (int v) { return v <= 0 || v >= len; }), s.end());
            clip->slices = s;
        }
        else
            clip->slices = edit::equalSlices (clip->audio->getNumSamples(), n);
        proc.session.clipChanged (clip.get(), false);
        syncSampler();
        wave.repaint(); pads.repaint();
    };
    clearChopsBtn.onClick = [this]
    {
        if (clip == nullptr || clip->slices.empty()) return;
        clip->pushUndo ("Clear chops");
        clip->slices.clear();
        proc.session.clipChanged (clip.get(), false);
        syncSampler();
        wave.repaint(); pads.repaint();
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

    for (auto* comp : std::initializer_list<juce::Component*> { &sensKnob, &autoChopBtn, &equalCount, &equalBtn, &clearChopsBtn, &midiModeBox, &oneShotToggle })
        addAndMakeVisible (comp);

    proc.session.addChangeListener (this);
    setClip (proc.session.getSelected());
    startTimerHz (4);
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
    const double host = proc.hostBpm.load();
    if (host > 0 && std::abs (host - bpmKnob.slider.getValue()) > 0.01 && ! bpmKnob.slider.isMouseButtonDown()
        && bpmKnob.getProperties().getWithDefault ("followHost", true))
        bpmKnob.slider.setValue (host, juce::dontSendNotification);

    playBtn.setToggleState (proc.isPreviewing() && clip != nullptr && proc.getPreviewSource() == clip->audio.get(), juce::dontSendNotification);
}

void StudioPage::setClip (Clip::Ptr c)
{
    clip = c;
    setKnobsFrom (clip != nullptr ? clip->adjust : Clip::Adjust());
    lastAdjustUndoMs = 0;
    wave.setClip (c);
    pads.setClip (c);
    overview.repaint();
    syncSampler();
    refreshInfo();

    const bool has = clip != nullptr;
    for (auto* b : editButtons) b->setEnabled (has);
    for (auto* comp : std::initializer_list<juce::Component*> { &pitchDefaultBtn, &matchBpmBtn, &toneDefaultBtn, &autoChopBtn,
                                                                &equalBtn, &clearChopsBtn, &playBtn, &stopBtn, &saveBtn, &detectBpmBtn, &dragHandle })
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
    nameLabel.setText (clip->name, juce::dontSendNotification);
    juce::String info;
    info << clip->kind.toUpperCase() << "   " << formatTime (a.lengthSeconds(), true) << "s   "
         << juce::String (a.sampleRate / 1000.0, 1) << " kHz   " << (a.getNumChannels() > 1 ? "STEREO" : "MONO");
    if (clip->slices.size() > 0)
        info << "   " << (int) clip->sliceBoundaries().size() - 1 << " CHOPS";
    infoLabel.setText (info, juce::dontSendNotification);
    double shownBpm = clip->bpm;
    if (shownBpm > 0 && clip->adjustBase != nullptr)
        shownBpm = clip->adjust.tape ? shownBpm * std::pow (2.0, clip->adjust.semitones / 12.0) : shownBpm / clip->adjust.length;
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

    if (fix == SliceFix::crop || fix == SliceFix::remove)
        wave.clearSelection();

    proc.session.clipChanged (clip.get());
    wave.audioChanged();
    syncSampler();
    refreshInfo();
    pads.repaint();
    overview.repaint();

    if (newBase != nullptr)
        startAdjustRender();   // exact version of what's on screen
}

//==============================================================================
Clip::Adjust StudioPage::knobsToAdjust() const
{
    Clip::Adjust a;
    a.semitones = (float) pitchKnob.slider.getValue();
    a.length    = stretchKnob.slider.getValue() / 100.0;
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
    pitchKnob.setValueSilently (a.semitones);
    stretchKnob.setValueSilently (a.length * 100.0);
    formantToggle.setToggleState (a.formants, juce::dontSendNotification);
    tapeToggle.setToggleState (a.tape, juce::dontSendNotification);
    gainKnob.setValueSilently (a.gainDb);
    lowCutKnob.setValueSilently (a.lowCut > 0.0f ? a.lowCut : lowCutKnob.getDefault());
    highCutKnob.setValueSilently (a.highCut > 0.0f ? a.highCut : highCutKnob.getDefault());
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
        if (adj.isNeutral())
            c->adjustBase = nullptr;   // back to the original
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
    proc.jobs.start (adj.pitchNeutral() ? "Tone" : (adj.tape ? "Varispeed" : "Pitch & Time"), [base, adj] (Job& job)
    {
        job.setStatus ("Rendering");
        job.audio = edit::renderAdjust (*base, adj);
    },
    [safe, land] (Job& job)
    {
        if (! job.isCancelled())
            land (job.audio);
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

    const bool wasPlaying = proc.isPreviewing() && proc.getPreviewSource() == oldAudio.get();
    const double pos = wasPlaying ? proc.getPreviewPosition() : -1.0;

    if (wave.hasSelection() && std::abs (ratio - 1.0) > 1.0e-6)
        wave.setSelection (juce::roundToInt (wave.getSelectionStart() * ratio), juce::roundToInt (wave.getSelectionEnd() * ratio));

    wave.audioChanged();
    syncSampler();
    refreshInfo();
    pads.repaint();
    overview.repaint();

    // keep playing from the same moment, now with the new sound
    if (wasPlaying)
    {
        const int from = juce::jlimit (0, juce::jmax (0, newLen - 1), juce::roundToInt (pos * ratio));
        if (wave.hasSelection())
            proc.previewClip (*clip, juce::jlimit (wave.getSelectionStart(), wave.getSelectionEnd(), from), wave.getSelectionEnd(), loopBtn.getToggleState());
        else
            proc.previewClip (*clip, from, -1, loopBtn.getToggleState());
    }
}

void StudioPage::play()
{
    if (clip == nullptr) return;
    if (wave.hasSelection())
        proc.previewClip (*clip, wave.getSelectionStart(), wave.getSelectionEnd(), loopBtn.getToggleState());
    else
        proc.previewClip (*clip, wave.getCursor(), -1, loopBtn.getToggleState());
}

void StudioPage::stop()      { proc.stopPreview(); }

void StudioPage::undo()
{
    if (clip == nullptr || ! clip->canUndo()) return;
    auto what = clip->undo();
    setKnobsFrom (clip->adjust);
    lastAdjustUndoMs = 0;
    proc.session.clipChanged (clip.get());
    wave.audioChanged();
    syncSampler();
    refreshInfo();
    pads.repaint();
    ctx.toast ("Undid " + what);
}

void StudioPage::redo()
{
    if (clip == nullptr || ! clip->canRedo()) return;
    auto what = clip->redo();
    setKnobsFrom (clip->adjust);
    lastAdjustUndoMs = 0;
    proc.session.clipChanged (clip.get());
    wave.audioChanged();
    syncSampler();
    refreshInfo();
    pads.repaint();
    ctx.toast ("Redid " + what);
}

void StudioPage::detectBpm()
{
    if (clip == nullptr) return;
    const int s = selStartOrZero();
    const int e = selEndOrAll() < 0 ? clip->audio->getNumSamples() : selEndOrAll();
    auto part = wave.hasSelection() ? edit::crop (*clip->audio, s, e) : clip->audio;
    const double bpm = edit::estimateBpm (*part);
    if (bpm <= 0)
    {
        ctx.toast ("Couldn't find a steady tempo in this sample.", true);
        return;
    }
    // stored for the original, so LENGTH / MATCH BPM always start from the same place
    clip->bpm = clip->adjustBase != nullptr && ! clip->adjust.tape ? bpm * clip->adjust.length : bpm;
    proc.session.clipChanged (clip.get(), false);
    refreshInfo();
    ctx.toast ("Tempo: " + juce::String (bpm, 1) + " BPM");
}

void StudioPage::matchBpm()
{
    if (clip == nullptr) return;
    if (clip->bpm <= 0)
        detectBpm();
    if (clip->bpm <= 0)
        return;
    const double target = bpmKnob.slider.getValue();
    const double ratio = clip->bpm / target;   // new length, relative to the original
    if (ratio < 0.5 || ratio > 2.0) { ctx.toast ("That's too big a tempo change (the LENGTH knob goes from 50% to 200%).", true); return; }

    lastAdjustUndoMs = 0;
    if (tapeToggle.getToggleState())
        pitchKnob.setValueSilently (12.0 * std::log2 (1.0 / ratio));
    stretchKnob.setValueSilently (ratio * 100.0);
    knobsChanged();
    ctx.toast ("Matched " + juce::String (clip->bpm, 1) + " BPM to " + juce::String (target, 1) + " BPM");
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
    bpmLabel.setBounds (head.removeFromRight (90));
    const int nameW = juce::jmin (460, head.getWidth() / 2);
    nameLabel.setBounds (head.removeFromLeft (nameW));
    infoLabel.setBounds (head.withTrimmedLeft (8));

    r.removeFromTop (6);
    overview.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);

    auto tools = r.removeFromBottom (158);
    r.removeFromBottom (8);
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

    // tool panels
    const int gap = 10;
    const int totalW = tools.getWidth() - 3 * gap;
    editPanel.setBounds (tools.removeFromLeft ((int) (totalW * 0.25)));
    tools.removeFromLeft (gap);
    pitchPanel.setBounds (tools.removeFromLeft ((int) (totalW * 0.29)));
    tools.removeFromLeft (gap);
    tonePanel.setBounds (tools.removeFromLeft ((int) (totalW * 0.21)));
    tools.removeFromLeft (gap);
    chopPanel.setBounds (tools);

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

        equalCount.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 3));
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
