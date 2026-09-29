#include "StudioDecks.h"
#include "../PluginProcessor.h"
#include "../Actions.h"
#include "../core/EditOps.h"
#include "../core/FxRack.h"

namespace snag
{
using namespace theme;

namespace
{
    juce::String msText (double v)
    {
        if (v >= 1000.0) return juce::String (v / 1000.0, 2) + " s";
        return juce::String (juce::roundToInt (v)) + " ms";
    }
    juce::String dbText (double v)
    {
        if (std::abs (v) < 0.05) return "0.0 dB";
        return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
    }
    juce::String pctText (double v) { return juce::String (juce::roundToInt (v)) + "%"; }
}

//==============================================================================
PadDeck::PadDeck()
{
    addAndMakeVisible (panel);
    gainKnob.formatter = [] (double v) { return dbText (v); };
    pitchKnob.formatter = [] (double v) { const int s = juce::roundToInt (v); return (s > 0 ? "+" : "") + juce::String (s) + " st"; };
    attackKnob.slider.setSkewFactorFromMidPoint (120.0);
    releaseKnob.slider.setSkewFactorFromMidPoint (250.0);
    attackKnob.formatter = [] (double v) { return msText (v); };
    releaseKnob.formatter = [] (double v) { return msText (v); };
    filterKnob.formatter = [] (double v) { return filterText ((float) v); };

    gainKnob.setTooltip ({});
    pitchKnob.slider.setTooltip ("Pitch this chop up or down (it plays faster / slower, like a classic sampler)");
    attackKnob.slider.setTooltip ("Fade the chop in");
    releaseKnob.slider.setTooltip ("Fade the chop out at its end (and after you let go, with ONE-SHOT off)");
    filterKnob.slider.setTooltip ("Left: low-pass (darker). Right: high-pass (thinner). Middle: off");
    reverseToggle.setTooltip ("Play this chop backwards");
    resetBtn.setTooltip ("This pad back to how the chop sounds on its own");
    allBtn.setTooltip ("Give every pad this pad's settings");

    for (auto* k : { &gainKnob, &pitchKnob, &attackKnob, &releaseKnob, &filterKnob })
    {
        k->onChange = [this] { change ("Pad sound"); };
        k->onGestureStart = [this] { fresh = true; };
        addAndMakeVisible (k);
    }
    reverseToggle.onClick = [this] { fresh = true; change ("Reverse pad"); };
    resetBtn.onClick = [this]
    {
        for (auto* k : { &gainKnob, &pitchKnob, &attackKnob, &releaseKnob, &filterKnob })
            k->setValueSilently (k->getDefault());
        reverseToggle.setToggleState (false, juce::dontSendNotification);
        fresh = true;
        change ("Pad default");
    };
    allBtn.onClick = [this]
    {
        if (clip == nullptr || numPads() < 2) return;
        const auto p = clip->padAt (pad);
        if (beforeChange) beforeChange ("Copy pad to all", true);
        for (int i = 0; i < numPads(); ++i)
            clip->setPad (i, p);
        if (afterChange) afterChange();
    };
    for (auto* c : std::initializer_list<juce::Component*> { &reverseToggle, &resetBtn, &allBtn })
        addAndMakeVisible (c);
    refresh();
}

int PadDeck::numPads() const
{
    return clip != nullptr && clip->audio != nullptr ? (int) clip->sliceBoundaries().size() - 1 : 0;
}

void PadDeck::setClip (Clip::Ptr c, int padIndex)
{
    clip = c;
    pad = juce::jmax (0, padIndex);
    fresh = true;
    refresh();
}

void PadDeck::refresh()
{
    const bool usable = numPads() >= 2;
    const auto p = clip != nullptr ? clip->padAt (pad) : PadParams();
    gainKnob.setValueSilently (p.gainDb);
    pitchKnob.setValueSilently (p.semitones);
    attackKnob.setValueSilently (p.attackMs);
    releaseKnob.setValueSilently (p.releaseMs);
    filterKnob.setValueSilently (p.filter);
    reverseToggle.setToggleState (p.reverse, juce::dontSendNotification);
    panel.setTitle (usable ? "Pad " + juce::String (pad + 1) : "Pad");
    for (auto* c : std::initializer_list<juce::Component*> { &gainKnob, &pitchKnob, &attackKnob, &releaseKnob, &filterKnob, &reverseToggle, &resetBtn, &allBtn })
    {
        c->setEnabled (usable);
        c->setAlpha (usable ? 1.0f : 0.35f);
    }
    repaint();
}

void PadDeck::change (const juce::String& label)
{
    if (clip == nullptr || numPads() < 2)
        return;
    PadParams p;
    p.gainDb = (float) gainKnob.slider.getValue();
    p.semitones = (float) std::round (pitchKnob.slider.getValue());
    p.reverse = reverseToggle.getToggleState();
    p.attackMs = (float) attackKnob.slider.getValue();
    p.releaseMs = (float) releaseKnob.slider.getValue();
    const float f = (float) filterKnob.slider.getValue();
    p.filter = std::abs (f) < 0.02f ? 0.0f : f;
    if (p == clip->padAt (pad))
        return;
    if (beforeChange) beforeChange (label, fresh);
    fresh = false;
    clip->setPad (pad, p);
    if (afterChange) afterChange();
}

void PadDeck::resized()
{
    panel.setBounds (getLocalBounds());
    auto c = panel.getContentBounds().translated (panel.getX(), panel.getY());
    auto right = c.removeFromRight (juce::jmin (190, c.getWidth() / 3));
    right.removeFromLeft (10);
    const int kw = c.getWidth() / 5;
    for (auto* k : { &gainKnob, &pitchKnob, &attackKnob, &releaseKnob })
        k->setBounds (c.removeFromLeft (kw));
    filterKnob.setBounds (c);
    const int rh = juce::jmin (30, (right.getHeight() - 12) / 3);
    reverseToggle.setBounds (right.removeFromTop (rh));
    right.removeFromTop (6);
    resetBtn.setBounds (right.removeFromTop (rh));
    right.removeFromTop (6);
    allBtn.setBounds (right.removeFromTop (rh));
}

void PadDeck::paint (juce::Graphics& g)
{
    if (numPads() >= 2)
        return;
    auto c = panel.getContentBounds().translated (panel.getX(), panel.getY()).toFloat();
    g.setColour (col::goldPale);
    g.setFont (ui (13.0f, true));
    g.drawFittedText ("Chop the sample first (AUTO CHOP or EQUAL on the SAMPLE tab), then click a pad to shape its sound.",
                      c.reduced (20.0f, 0.0f).toNearestInt(), juce::Justification::centred, 2);
}

//==============================================================================
FxDeck::FxDeck()
{
    addAndMakeVisible (panel);
    bitsKnob.formatter = [] (double v) { return juce::String (juce::roundToInt (v)) + " bit"; };
    rateKnob.slider.setSkewFactorFromMidPoint (12.0);
    rateKnob.formatter = [] (double v) { return juce::String (v, v < 10.0 ? 1 : 0) + " kHz"; };
    timeKnob.formatter = [] (double v)
    {
        const auto& d = delayDivisions();
        return juce::String (d[(size_t) juce::jlimit (0, (int) d.size() - 1, juce::roundToInt (v))].name);
    };
    for (auto* k : { &vinylKnob, &driveKnob, &toneKnob, &feedbackKnob, &delayMixKnob, &sizeKnob, &reverbMixKnob })
        k->formatter = [] (double v) { return pctText (v); };

    lofiOn.setTooltip ("Bit crush, lower sample rate and a worn record: crackle, hiss, wow & flutter");
    driveOn.setTooltip ("Warm tube-style saturation");
    delayOn.setTooltip ("Ping-pong echo locked to your DAW's tempo (or the sample's)");
    reverbOn.setTooltip ("Room / hall reverb");
    vinylKnob.slider.setTooltip ("Crackle, hiss, wow & flutter and a worn-out tone");
    timeKnob.slider.setTooltip ("Echo time in beats (D = dotted, T = triplet)");
    defaultBtn.setTooltip ("Switch every effect off (the knobs stay where they are)");

    for (auto* k : { &bitsKnob, &rateKnob, &vinylKnob, &driveKnob, &toneKnob, &timeKnob, &feedbackKnob, &delayMixKnob, &sizeKnob, &reverbMixKnob })
    {
        k->onChange = [this] { change ("FX"); };
        k->onGestureStart = [this] { fresh = true; };
        addAndMakeVisible (k);
    }
    for (auto* t : { &lofiOn, &driveOn, &delayOn, &reverbOn })
    {
        t->onClick = [this] { fresh = true; change ("FX on / off"); };
        addAndMakeVisible (t);
    }
    defaultBtn.onClick = [this]
    {
        for (auto* t : { &lofiOn, &driveOn, &delayOn, &reverbOn })
            t->setToggleState (false, juce::dontSendNotification);
        fresh = true;
        change ("FX off");
    };
    addAndMakeVisible (defaultBtn);
    refresh();
}

void FxDeck::setClip (Clip::Ptr c)
{
    clip = c;
    fresh = true;
    refresh();
}

FxSettings FxDeck::read() const
{
    FxSettings f;
    f.lofiOn = lofiOn.getToggleState();
    f.bits = (float) bitsKnob.slider.getValue();
    f.rateKHz = (float) rateKnob.slider.getValue();
    f.vinyl = (float) vinylKnob.slider.getValue() / 100.0f;
    f.driveOn = driveOn.getToggleState();
    f.drive = (float) driveKnob.slider.getValue() / 100.0f;
    f.tone = (float) toneKnob.slider.getValue() / 100.0f;
    f.delayOn = delayOn.getToggleState();
    f.division = juce::roundToInt (timeKnob.slider.getValue());
    f.feedback = (float) feedbackKnob.slider.getValue() / 100.0f;
    f.delayMix = (float) delayMixKnob.slider.getValue() / 100.0f;
    f.reverbOn = reverbOn.getToggleState();
    f.size = (float) sizeKnob.slider.getValue() / 100.0f;
    f.reverbMix = (float) reverbMixKnob.slider.getValue() / 100.0f;
    return f;
}

void FxDeck::refresh()
{
    const auto f = clip != nullptr ? clip->fx : FxSettings();
    lofiOn.setToggleState (f.lofiOn, juce::dontSendNotification);
    bitsKnob.setValueSilently (f.bits);
    rateKnob.setValueSilently (f.rateKHz);
    vinylKnob.setValueSilently (f.vinyl * 100.0);
    driveOn.setToggleState (f.driveOn, juce::dontSendNotification);
    driveKnob.setValueSilently (f.drive * 100.0);
    toneKnob.setValueSilently (f.tone * 100.0);
    delayOn.setToggleState (f.delayOn, juce::dontSendNotification);
    timeKnob.setValueSilently (f.division);
    feedbackKnob.setValueSilently (f.feedback * 100.0);
    delayMixKnob.setValueSilently (f.delayMix * 100.0);
    reverbOn.setToggleState (f.reverbOn, juce::dontSendNotification);
    sizeKnob.setValueSilently (f.size * 100.0);
    reverbMixKnob.setValueSilently (f.reverbMix * 100.0);

    const bool has = clip != nullptr;
    auto dim = [has] (bool on, std::initializer_list<Knob*> ks) { for (auto* k : ks) { k->setAlpha (has && on ? 1.0f : 0.45f); k->setEnabled (has); } };
    dim (f.lofiOn, { &bitsKnob, &rateKnob, &vinylKnob });
    dim (f.driveOn, { &driveKnob, &toneKnob });
    dim (f.delayOn, { &timeKnob, &feedbackKnob, &delayMixKnob });
    dim (f.reverbOn, { &sizeKnob, &reverbMixKnob });
    for (auto* t : { &lofiOn, &driveOn, &delayOn, &reverbOn }) t->setEnabled (has);
    defaultBtn.setEnabled (has && f.anyOn());
}

void FxDeck::change (const juce::String& label)
{
    if (clip == nullptr)
        return;
    const auto f = read();
    if (f == clip->fx)
        return;
    // turning a knob of a section that's off switches that section on - that's what you meant
    auto f2 = f;
    if (fresh || label == "FX")
    {
        auto touched = [&] (bool& on, bool changed) { if (changed && ! on) on = true; };
        const auto& o = clip->fx;
        if (label == "FX")
        {
            touched (f2.lofiOn, f.bits != o.bits || f.rateKHz != o.rateKHz || f.vinyl != o.vinyl);
            touched (f2.driveOn, f.drive != o.drive || f.tone != o.tone);
            touched (f2.delayOn, f.division != o.division || f.feedback != o.feedback || f.delayMix != o.delayMix);
            touched (f2.reverbOn, f.size != o.size || f.reverbMix != o.reverbMix);
        }
    }
    if (beforeChange) beforeChange (label, fresh);
    fresh = false;
    clip->fx = f2;
    refresh();
    if (afterChange) afterChange();
}

void FxDeck::resized()
{
    panel.setBounds (getLocalBounds());
    defaultBtn.setBounds (getLocalBounds().reduced (12, 5).removeFromTop (20).removeFromRight (90));
    auto c = panel.getContentBounds().translated (panel.getX(), panel.getY());
    const int gap = 14;
    const int total = c.getWidth() - gap * 3;
    const int unit = total / 10;   // 3 + 2 + 3 + 2 knobs
    const int widths[4] = { unit * 3, unit * 2, unit * 3, total - unit * 8 };
    Knob* knobs[4][3] = { { &bitsKnob, &rateKnob, &vinylKnob }, { &driveKnob, &toneKnob, nullptr },
                          { &timeKnob, &feedbackKnob, &delayMixKnob }, { &sizeKnob, &reverbMixKnob, nullptr } };
    juce::ToggleButton* toggles[4] = { &lofiOn, &driveOn, &delayOn, &reverbOn };
    for (int s = 0; s < 4; ++s)
    {
        auto sec = c.removeFromLeft (widths[s]);
        if (s < 3) c.removeFromLeft (gap);
        sections[s] = sec;
        toggles[s]->setBounds (sec.removeFromTop (22).withTrimmedLeft (4));
        const int n = knobs[s][2] != nullptr ? 3 : 2;
        const int kw = sec.getWidth() / n;
        for (int k = 0; k < n; ++k)
            knobs[s][k]->setBounds (k == n - 1 ? sec : sec.removeFromLeft (kw));
    }
}

void FxDeck::paint (juce::Graphics& g)
{
    // hairlines between the sections
    for (int s = 1; s < 4; ++s)
    {
        const float x = (float) sections[s].getX() - 7.0f;
        g.setGradientFill (juce::ColourGradient (col::gold.withAlpha (0.0f), x, (float) sections[s].getY(),
                                                 col::gold.withAlpha (0.0f), x, (float) sections[s].getBottom(), false));
        juce::ColourGradient cg (col::gold.withAlpha (0.0f), x, (float) sections[s].getY(), col::gold.withAlpha (0.0f), x, (float) sections[s].getBottom(), false);
        cg.addColour (0.5, col::gold.withAlpha (0.35f));
        g.setGradientFill (cg);
        g.fillRect (juce::Rectangle<float> (x, (float) sections[s].getY(), 1.0f, (float) sections[s].getHeight()));
    }
}

//==============================================================================
void PianoRoll::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    glassWell (g, r, 6.0f);
    r = r.reduced (6.0f, 5.0f);

    if (busy >= 0.0f)
    {
        drawSpinner (g, r.withSizeKeepingCentre (22.0f, 22.0f).translated (0.0f, -10.0f), col::red);
        g.setColour (col::goldPale);
        g.setFont (ui (12.0f, true));
        g.drawText ("Listening for notes...  " + juce::String (juce::roundToInt (busy * 100.0f)) + "%",
                    r.withTrimmedTop (r.getHeight() * 0.5f + 8.0f).withHeight (18.0f), juce::Justification::centredTop);
        return;
    }
    if (notes.empty())
    {
        g.setColour (col::textDim);
        g.setFont (ui (12.5f, true));
        g.drawFittedText (message, r.reduced (10.0f, 0.0f).toNearestInt(), juce::Justification::centred, 3);
        return;
    }

    int lo = 127, hi = 0;
    for (auto& n : notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    lo -= 2; hi += 2;
    const float rowH = r.getHeight() / (float) (hi - lo + 1);
    const double len = juce::jmax (0.1, length);
    auto xOf = [&] (double t) { return r.getX() + (float) (t / len) * r.getWidth(); };
    auto yOf = [&] (int p) { return r.getBottom() - (float) (p - lo + 1) * rowH; };

    // black keys shaded, a line on every C
    for (int p = lo; p <= hi; ++p)
    {
        const int pc = p % 12;
        if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10)
        {
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            g.fillRect (juce::Rectangle<float> (r.getX(), yOf (p), r.getWidth(), rowH));
        }
        if (pc == 0)
        {
            g.setColour (col::gold.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (r.getX(), yOf (p) + rowH - 0.5f, r.getWidth(), 0.8f));
            g.setColour (col::textFaint);
            g.setFont (ui (9.0f, true));
            g.drawText (noteName (p), juce::Rectangle<float> (r.getX() + 2.0f, yOf (p) + rowH - 11.0f, 30.0f, 10.0f), juce::Justification::left);
        }
    }

    for (auto& n : notes)
    {
        auto nr = juce::Rectangle<float> (xOf (n.start), yOf (n.pitch) + 0.5f, juce::jmax (2.0f, xOf (n.end) - xOf (n.start)), juce::jmax (2.0f, rowH - 1.0f));
        const float a = 0.45f + 0.55f * n.velocity;
        g.setGradientFill (juce::ColourGradient (col::goldLight.withAlpha (a), nr.getX(), nr.getY(), col::gold.withAlpha (a * 0.85f), nr.getX(), nr.getBottom(), false));
        g.fillRoundedRectangle (nr, juce::jmin (2.5f, nr.getHeight() * 0.4f));
        g.setColour (col::goldDeep.withAlpha (0.9f));
        g.drawRoundedRectangle (nr, juce::jmin (2.5f, nr.getHeight() * 0.4f), 0.6f);
    }

    if (playhead >= 0.0)
    {
        const float x = xOf (playhead);
        neonLine (g, { x, r.getY(), x, r.getBottom() }, col::red, 1.2f, 6.0f);
    }
}

//==============================================================================
MidiDeck::MidiDeck (EditorContext& c) : ctx (c), proc (c.getProcessor())
{
    addAndMakeVisible (panel);
    modeBox.addItem ("Chords + melody", 1);
    modeBox.addItem ("Melody only", 2);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.setTooltip ("Melody only keeps one note at a time - best for a vocal, a bass line or a lead");
    modeBox.onChange = [this] { renote(); };
    sensKnob.slider.setTooltip ("Higher finds quieter notes too (and more stray ones)");
    minLenKnob.slider.setTooltip ("Notes shorter than this are left out");
    minLenKnob.slider.setSkewFactorFromMidPoint (150.0);
    sensKnob.formatter = [] (double v) { return pctText (v); };
    minLenKnob.formatter = [] (double v) { return msText (v); };
    sensKnob.onChange = [this] { renote(); };
    minLenKnob.onChange = [this] { renote(); };

    setStyle (findBtn, "red");
    findBtn.setTooltip ("Turn the sample (or the selection) into MIDI notes - melodies and chords");
    findBtn.onClick = [this] { findNotes(); };
    listenBtn.setTooltip ("Hear the notes it found");
    listenBtn.onClick = [this] { listen(); };
    saveBtn.setTooltip ("Save the notes as a .mid file in your sample library");
    saveBtn.onClick = [this]
    {
        if (clip == nullptr || notes.empty()) return;
        auto f = actions::makeMidiFile (proc, *clip, notes, proc.getSettings().getLibraryDir());
        ctx.toast (f != juce::File() ? "Saved to library: " + f.getFileName() : juce::String ("Couldn't save the MIDI file"), f == juce::File());
    };
    dragMidi.setTooltip ("Drag the notes onto a MIDI / instrument track in your DAW");
    dragMidi.makeFile = [this]
    {
        if (clip == nullptr || notes.empty()) return juce::File();
        return actions::makeMidiFile (proc, *clip, notes);
    };
    status.setFont (ui (11.5f, true));
    status.setColour (juce::Label::textColourId, col::textDim);
    status.setJustificationType (juce::Justification::centredLeft);

    for (auto* comp : std::initializer_list<juce::Component*> { &modeBox, &sensKnob, &minLenKnob, &findBtn, &listenBtn, &saveBtn, &dragMidi, &roll, &status })
        addAndMakeVisible (comp);
    setClip (nullptr);
    startTimerHz (20);
}

MidiDeck::~MidiDeck()
{
    *alive = false;
    if (heard != nullptr && proc.getPreviewSource() == heard.get())
        proc.stopPreview();
}

juce::String MidiDeck::rangeKey() const
{
    if (clip == nullptr || clip->audio == nullptr) return {};
    return clip->id + ":" + juce::String::toHexString ((juce::pointer_sized_int) clip->audio.get()) + ":" + juce::String (rangeStart) + ":" + juce::String (rangeEnd);
}

AudioData::Ptr MidiDeck::source() const
{
    if (clip == nullptr || clip->audio == nullptr) return nullptr;
    const int n = clip->audio->getNumSamples();
    const int e = rangeEnd < 0 ? n : juce::jlimit (0, n, rangeEnd);
    const int s = juce::jlimit (0, e, rangeStart);
    return (s == 0 && e == n) ? clip->audio : edit::crop (*clip->audio, s, e);
}

void MidiDeck::setClip (Clip::Ptr c)
{
    if (c != clip)
    {
        clip = c;
        rangeStart = 0; rangeEnd = -1;
    }
    renote();
}

void MidiDeck::setRange (int s, int e)
{
    if (s == rangeStart && e == rangeEnd) return;
    rangeStart = s; rangeEnd = e;
    renote();
}

void MidiDeck::findNotes()
{
    auto src = source();
    if (src == nullptr || analysing) return;
    if (src->lengthSeconds() > 15.0 * 60.0) { ctx.toast ("That's over 15 minutes - select a part to turn into MIDI.", true); return; }
    analysing = true;
    progress->store (0.0f);
    renote();
    auto key = rangeKey();
    auto result = std::make_shared<midi::Posteriors>();
    auto prog = progress;
    auto alv = alive;
    juce::Component::SafePointer<MidiDeck> safe (this);
    proc.jobs.start ("Finding notes", [src, result, prog] (Job& job)
    {
        job.setStatus ("Listening for notes");
        juce::String err;
        if (! midi::analyse (*src, *result, err, [&job, prog] (float p) { job.setProgress (p); prog->store (p); }, job.cancelCheck()))
            if (err != "Cancelled") job.fail (err);
    },
    [safe, alv, result, key] (Job& job)
    {
        if (! *alv || safe == nullptr) return;
        safe->analysing = false;
        if (job.hasFailed()) { safe->ctx.toast (job.getError(), true); safe->renote(); return; }
        if (job.isCancelled()) { safe->renote(); return; }
        safe->post = std::move (*result);
        safe->postKey = key;
        safe->renote();
    });
}

void MidiDeck::renote()
{
    const bool has = clip != nullptr && clip->audio != nullptr;
    const bool current = has && post.frames > 0 && postKey == rangeKey();
    if (current)
    {
        midi::Options o;
        o.sensitivity = (float) sensKnob.slider.getValue() / 100.0f;
        o.minNoteMs = (float) minLenKnob.slider.getValue();
        o.melodyOnly = modeBox.getSelectedId() == 2;
        notes = midi::notesFrom (post, o);
    }
    else
        notes.clear();
    heard = nullptr;

    const int n = has ? clip->audio->getNumSamples() : 0;
    const int e = rangeEnd < 0 ? n : juce::jlimit (0, n, rangeEnd);
    const int s = juce::jlimit (0, e, rangeStart);
    const double len = has ? (e - s) / clip->audio->sampleRate : 1.0;
    roll.setBusy (analysing ? progress->load() : -1.0f);
    roll.setNotes (notes, len);
    const juce::String what = has && rangeEnd >= 0 ? "the selection" : "the sample";
    if (! has)
        roll.setMessage ("Load a sample to turn it into MIDI.");
    else if (current && notes.empty())
        roll.setMessage ("No clear notes found in " + what + " - try a higher SENSITIVITY.");
    else
        roll.setMessage ("Press FIND NOTES to turn " + what + " into MIDI - a melody, a bass line or chords.\n"
                         "Tip: separate the part in STEMS first for the cleanest notes.");

    if (current && ! notes.empty())
        status.setText (midi::noteRangeText (notes) + "   at " + juce::String (actions::exportBpm (proc, *clip), 1) + " BPM", juce::dontSendNotification);
    else
        status.setText (has ? (rangeEnd >= 0 ? "Selection: " + formatTime (s / clip->audio->sampleRate, true) + " - " + formatTime (e / clip->audio->sampleRate, true)
                                             : juce::String ("Whole sample")) : juce::String(), juce::dontSendNotification);

    const bool ready = ! notes.empty();
    findBtn.setEnabled (has && ! analysing);
    for (auto* comp : std::initializer_list<juce::Component*> { &listenBtn, &saveBtn, &dragMidi })
    {
        comp->setEnabled (ready);
        comp->setAlpha (ready ? 1.0f : 0.4f);
    }
}

void MidiDeck::listen()
{
    if (notes.empty()) return;
    if (heard != nullptr && proc.isPreviewing() && proc.getPreviewSource() == heard.get())
    {
        proc.stopPreview();
        return;
    }
    heard = midi::renderNotes (notes, 44100.0, 0.0);
    proc.preview ({ heard }, 0, -1, false);
}

void MidiDeck::timerCallback()
{
    const bool playing = heard != nullptr && proc.isPreviewing() && proc.getPreviewSource() == heard.get();
    listenBtn.setIcon (playing ? icons::stop() : icons::play());
    listenBtn.setCaption (playing ? "STOP" : "LISTEN");
    roll.setPlayhead (playing ? proc.getPreviewPosition() / heard->sampleRate : -1.0);
    if (analysing)
        roll.setBusy (progress->load());
}

void MidiDeck::resized()
{
    panel.setBounds (getLocalBounds());
    auto c = panel.getContentBounds().translated (panel.getX(), panel.getY());

    auto left = c.removeFromLeft (juce::jmin (190, c.getWidth() / 6));
    modeBox.setBounds (left.removeFromTop (28).reduced (0, 1));
    findBtn.setBounds (left.removeFromBottom (juce::jmin (40, left.getHeight() / 2)));
    c.removeFromLeft (8);
    auto knobs = c.removeFromLeft (juce::jmin (190, c.getWidth() / 5));
    sensKnob.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
    minLenKnob.setBounds (knobs);

    c.removeFromLeft (12);
    auto right = c.removeFromRight (juce::jmin (230, c.getWidth() / 3));
    right.removeFromLeft (12);
    const int rh = juce::jmin (32, (right.getHeight() - 12) / 3);
    listenBtn.setBounds (right.removeFromTop (rh));
    right.removeFromTop (6);
    saveBtn.setBounds (right.removeFromTop (rh));
    right.removeFromTop (6);
    dragMidi.setBounds (right.removeFromTop (juce::jmax (rh, right.getHeight())));

    status.setBounds (c.removeFromBottom (18));
    c.removeFromBottom (2);
    roll.setBounds (c);
}

void MidiDeck::paint (juce::Graphics&) {}

} // namespace snag
