#include "StemsPage.h"
#include "../PluginProcessor.h"
#include "../Actions.h"
#include "../core/EditOps.h"

namespace snag
{
using namespace theme;

//==============================================================================
class StemLane : public juce::Component
{
public:
    StemLane (StemsPage& o, Clip::Ptr c, int idx) : owner (o), clip (c), index (idx)
    {
        colour = stemColour (clip->stemName);

        for (auto* b : { &muteBtn, &soloBtn })
        {
            setStyle (*b, "chip");
            b->setClickingTogglesState (true);
            b->onClick = [this] { owner.updateGains(); repaint(); };
            addAndMakeVisible (b);
        }
        muteBtn.setTooltip ("Mute in the mix");
        soloBtn.setTooltip ("Solo in the mix");

        playBtn.setTooltip ("Play this stem on its own");
        playBtn.onClick = [this] { owner.playStem (clip); };
        addAndMakeVisible (playBtn);

        editBtn.setTooltip ("Open this stem in STUDIO to chop / edit it");
        editBtn.onClick = [this]
        {
            owner.getProcessor().session.select (clip.get());
            owner.getContext().showTab (Tab::studio);
        };
        addAndMakeVisible (editBtn);

        saveBtn.setTooltip ("Save this stem to your sample library");
        saveBtn.onClick = [this] { actions::saveToLibrary (owner.getProcessor(), *clip); };
        addAndMakeVisible (saveBtn);

        drag.makeFile = [this] { return actions::makeDragFile (owner.getProcessor(), *clip); };
        addAndMakeVisible (drag);
    }

    bool isMuted() const  { return muteBtn.getToggleState(); }
    bool isSoloed() const { return soloBtn.getToggleState(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        r.removeFromLeft (8);                    // colour bar
        auto left = r.removeFromLeft (180);
        left.removeFromTop (26);
        auto btnRow = left.removeFromTop (28);
        muteBtn.setBounds (btnRow.removeFromLeft (30).reduced (0, 2));
        btnRow.removeFromLeft (5);
        soloBtn.setBounds (btnRow.removeFromLeft (30).reduced (0, 2));
        btnRow.removeFromLeft (5);
        playBtn.setBounds (btnRow.removeFromLeft (34));

        auto right = r.removeFromRight (250);
        drag.setBounds (right.removeFromRight (128).reduced (0, juce::jmax (0, (right.getHeight() - 40) / 2)));
        right.removeFromRight (6);
        saveBtn.setBounds (right.removeFromRight (40).reduced (0, juce::jmax (0, (right.getHeight() - 36) / 2)));
        right.removeFromRight (6);
        editBtn.setBounds (right.removeFromRight (66).reduced (0, juce::jmax (0, (right.getHeight() - 36) / 2)));

        waveArea = r.reduced (10, 0).toFloat();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        glossPanel (g, r, 8.0f, col::bg3, col::bg1, false, 0.04f);

        auto bar = r.withWidth (5.0f).reduced (0.0f, 10.0f).translated (8.0f, 0.0f);
        juce::Path bp; bp.addRoundedRectangle (bar, 2.5f);
        neonGlow (g, bp, colour, 8.0f, 0.6f);
        g.setColour (colour);
        g.fillPath (bp);

        const bool audible = isAudible();
        auto nameArea = getLocalBounds().reduced (12, 10).withTrimmedLeft (8).removeFromLeft (180).removeFromTop (24).toFloat();
        g.setColour (audible ? col::goldLight : col::textFaint);
        g.setFont (display (17.0f, true));
        g.drawText (prettyStemName (clip->stemName), nameArea, juce::Justification::centredLeft);

        g.setColour (col::textFaint);
        g.setFont (ui (10.0f, true));
        g.drawText (formatTime (clip->audio->lengthSeconds(), true) + "s", nameArea.withTrimmedLeft (110.0f), juce::Justification::centredLeft);

        auto wr = waveArea;
        g.setColour (col::bg0.withAlpha (0.6f));
        g.fillRoundedRectangle (wr, 6.0f);
        thumb.draw (g, wr.reduced (4.0f, 4.0f), clip->audio.get(), audible ? colour : colour.withSaturation (0.1f).darker (0.6f), 0, -1, true);

        // playhead when this stem (or the mix) is playing
        auto& proc = owner.getProcessor();
        if (proc.isPreviewing())
        {
            auto* src = proc.getPreviewSource();
            if (src != nullptr && src->getNumSamples() > 0 && (src == clip->audio.get() || owner.isMixPlaying))
            {
                const float x = wr.getX() + 4.0f + (float) (proc.getPreviewPosition() / src->getNumSamples()) * (wr.getWidth() - 8.0f);
                neonLine (g, { x, wr.getY(), x, wr.getBottom() }, col::red, 1.2f, 6.0f);
            }
        }
    }

    bool isAudible() const
    {
        bool anySolo = false;
        for (auto* l : owner.getLanes()) anySolo |= l->isSoloed();
        return anySolo ? isSoloed() : ! isMuted();
    }

    StemsPage& owner;
    Clip::Ptr clip;
    int index;

private:
    juce::Colour colour;
    juce::TextButton muteBtn { "M" }, soloBtn { "S" };
    IconButton playBtn { "play", icons::play(), {}, "gold" };
    juce::TextButton editBtn { "EDIT" };
    IconButton saveBtn { "save", icons::save(), {}, "gold" };
    DragHandle drag { "DRAG" };
    WaveThumb thumb;
    juce::Rectangle<float> waveArea;
};

//==============================================================================
StemsPage::StemsPage (EditorContext& c) : ctx (c), proc (c.getProcessor())
{
    addAndMakeVisible (topPanel);

    sourceLabel.setFont (display (19.0f, true));
    sourceLabel.setColour (juce::Label::textColourId, col::goldLight);
    sourceLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (sourceLabel);
    sourceInfo.setFont (ui (11.5f, true));
    sourceInfo.setColour (juce::Label::textColourId, col::textDim);
    addAndMakeVisible (sourceInfo);

    engineBox.addItem (actions::engineName (actions::Engine::quick), 1);
    engineBox.addItem (actions::engineName (actions::Engine::aiFast), 2);
    engineBox.addItem (actions::engineName (actions::Engine::aiBest), 3);
    engineBox.addItem (actions::engineName (actions::Engine::ai6), 4);
    engineBox.setSelectedId (proc.getTools().isAvailable (ToolManager::Tool::ai) ? 2 : 1, juce::dontSendNotification);
    engineBox.onChange = [this] { timerCallback(); };
    addAndMakeVisible (engineBox);

    stemsBox.addItem ("Vocals + Music", 1);
    stemsBox.addItem ("Vocals, Drums, Bass, Other", 2);
    stemsBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (stemsBox);

    engineNote.setFont (ui (11.0f));
    engineNote.setColour (juce::Label::textColourId, col::textDim);
    addAndMakeVisible (engineNote);

    separateBtn.setTooltip ("Split the sample into stems");
    separateBtn.onClick = [this] { separateNow(); };
    addAndMakeVisible (separateBtn);

    laneViewport.setViewedComponent (&laneHolder, false);
    laneViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (laneViewport);

    playMixBtn.onClick = [this] { playMix(); };
    stopBtn.onClick = [this] { proc.stopPreview(); isMixPlaying = false; };
    saveAllBtn.onClick = [this]
    {
        int n = 0;
        for (auto* l : lanes)
            if (actions::saveToLibrary (proc, *l->clip) != juce::File()) ++n;
        if (n > 0) ctx.toast ("Saved " + juce::String (n) + " stems to your library");
    };
    dragMix.makeFile = [this]
    {
        std::vector<AudioData::Ptr> layers;
        std::vector<float> gains;
        for (auto* l : lanes) { layers.push_back (l->clip->audio); gains.push_back (l->isAudible() ? 1.0f : 0.0f); }
        if (layers.empty() || shownParent == nullptr) return juce::File();
        Clip tmp;
        tmp.name = shownParent->name + " - custom mix";
        tmp.audio = edit::mix (layers, gains);
        return actions::makeDragFile (proc, tmp);
    };
    for (auto* comp : std::initializer_list<juce::Component*> { &playMixBtn, &stopBtn, &saveAllBtn, &dragMix })
        addAndMakeVisible (comp);

    proc.session.addChangeListener (this);
    proc.getTools().addChangeListener (this);
    rebuild();
    startTimerHz (15);
}

StemsPage::~StemsPage()
{
    proc.session.removeChangeListener (this);
    proc.getTools().removeChangeListener (this);
}

Clip::Ptr StemsPage::sourceClip() const
{
    auto sel = proc.session.getSelected();
    if (sel != nullptr && sel->isStem())
        if (auto parent = proc.session.getParentOf (*sel))
            return parent;
    return sel;
}

void StemsPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuild();
}

void StemsPage::rebuild()
{
    auto src = sourceClip();
    juce::StringArray ids;
    if (src != nullptr)
        for (auto& s : proc.session.getStemsOf (*src))
            ids.add (s->id);

    if (src != shownParent || ids != shownStemIds)
    {
        shownParent = src;
        shownStemIds = ids;
        lanes.clear();
        if (src != nullptr)
        {
            int i = 0;
            for (auto& s : proc.session.getStemsOf (*src))
            {
                auto* lane = lanes.add (new StemLane (*this, s, i++));
                laneHolder.addAndMakeVisible (lane);
            }
        }
        resized();
    }

    if (src != nullptr && src->audio != nullptr)
    {
        sourceLabel.setText (src->name, juce::dontSendNotification);
        sourceInfo.setText (src->kind.toUpperCase() + "   " + formatTime (src->audio->lengthSeconds(), true) + "s   "
                            + (src->audio->getNumChannels() > 1 ? "STEREO" : "MONO"), juce::dontSendNotification);
    }
    else
    {
        sourceLabel.setText ("No sample selected", juce::dontSendNotification);
        sourceInfo.setText ("Capture or import something, then pick it in the tray", juce::dontSendNotification);
    }

    const bool hasStems = ! lanes.isEmpty();
    separateBtn.setEnabled (src != nullptr);
    for (auto* comp : std::initializer_list<juce::Component*> { &playMixBtn, &stopBtn, &saveAllBtn, &dragMix })
        comp->setEnabled (hasStems);
    dragMix.setAlpha (hasStems ? 1.0f : 0.4f);
    timerCallback();
    repaint();
}

void StemsPage::timerCallback()
{
    const int id = engineBox.getSelectedId();
    const bool aiReady = proc.getTools().isAvailable (ToolManager::Tool::ai);
    juce::String note;
    if (id == 1)
        note = "Instant, runs inside the plug-in. Best on stereo mixes. For studio quality use an AI engine.";
    else if (! aiReady)
        note = "AI engine not installed yet: open Settings (gear, top right) > Install AI Stem Engine.";
    else if (id == 3)
        note = "Highest quality, about 4x slower. Runs locally on your computer.";
    else if (id == 4)
        note = "Six stems: vocals, drums, bass, guitar, piano, other.";
    else
        note = "Neural separation (Demucs), runs locally. A 3-minute song takes about 1-3 minutes on CPU.";
    engineNote.setText (note, juce::dontSendNotification);
    engineNote.setColour (juce::Label::textColourId, (id != 1 && ! aiReady) ? col::redHot : col::textDim);
    stemsBox.setEnabled (id != 4);

    if (proc.isPreviewing())
        for (auto* l : lanes) l->repaint();
    else if (isMixPlaying)
    {
        isMixPlaying = false;
        for (auto* l : lanes) l->repaint();
    }
}

void StemsPage::separateNow()
{
    auto src = sourceClip();
    if (src == nullptr) { ctx.toast ("Pick a sample in the tray first.", true); return; }

    const int id = engineBox.getSelectedId();
    auto engine = id == 1 ? actions::Engine::quick : id == 2 ? actions::Engine::aiFast : id == 3 ? actions::Engine::aiBest : actions::Engine::ai6;
    actions::separate (proc, src, engine, stemsBox.getSelectedId() == 2);
}

void StemsPage::updateGains()
{
    for (int i = 0; i < (int) proc.layerGains.size(); ++i)
    {
        float g = 0.0f;
        if (i < lanes.size())
            g = isMixPlaying ? (lanes[i]->isAudible() ? 1.0f : 0.0f) : (i == 0 ? 1.0f : 0.0f);
        proc.layerGains[(size_t) i] = g;
    }
    for (auto* l : lanes) l->repaint();
}

void StemsPage::playMix()
{
    if (lanes.isEmpty()) return;
    std::vector<AudioData::Ptr> layers;
    for (auto* l : lanes) layers.push_back (l->clip->audio);
    isMixPlaying = true;
    updateGains();
    proc.preview (layers, 0, -1, false);
}

void StemsPage::playStem (Clip::Ptr c)
{
    isMixPlaying = false;
    for (auto& g : proc.layerGains) g = 1.0f;
    proc.previewClip (*c);
}

//==============================================================================
void StemsPage::resized()
{
    auto r = getLocalBounds().reduced (14, 10);

    auto top = r.removeFromTop (96);
    topPanel.setBounds (top);
    auto t = top.reduced (16, 12);
    t.removeFromTop (16);
    auto right = t.removeFromRight (juce::jmin (640, t.getWidth() * 2 / 3));
    sourceLabel.setBounds (t.removeFromTop (30));
    sourceInfo.setBounds (t.removeFromTop (20));

    auto row = right.removeFromTop (40);
    separateBtn.setBounds (row.removeFromRight (170));
    row.removeFromRight (10);
    stemsBox.setBounds (row.removeFromRight (210).reduced (0, 5));
    row.removeFromRight (8);
    engineBox.setBounds (row.reduced (0, 5));
    engineNote.setBounds (right.removeFromTop (22));

    r.removeFromTop (10);
    auto bottom = r.removeFromBottom (40);
    r.removeFromBottom (10);

    playMixBtn.setBounds (bottom.removeFromLeft (120));
    bottom.removeFromLeft (6);
    stopBtn.setBounds (bottom.removeFromLeft (40));
    dragMix.setBounds (bottom.removeFromRight (250));
    bottom.removeFromRight (8);
    saveAllBtn.setBounds (bottom.removeFromRight (170));

    laneViewport.setBounds (r);
    emptyArea = r;
    const int laneH = lanes.isEmpty() ? 0 : juce::jlimit (84, 130, (r.getHeight() - 8 * (lanes.size() - 1)) / juce::jmax (1, lanes.size()));
    const int totalH = lanes.size() * laneH + juce::jmax (0, lanes.size() - 1) * 8;
    laneHolder.setSize (r.getWidth() - (totalH > r.getHeight() ? 10 : 0), juce::jmax (1, totalH));
    for (int i = 0; i < lanes.size(); ++i)
        lanes[i]->setBounds (0, i * (laneH + 8), laneHolder.getWidth(), laneH);
}

void StemsPage::paint (juce::Graphics& g)
{
    if (! lanes.isEmpty())
        return;

    auto r = emptyArea.toFloat();
    glossPanel (g, r, 10.0f, col::bg2, col::bg0, false, 0.02f);

    auto c = r.withSizeKeepingCentre (juce::jmin (640.0f, r.getWidth() - 40.0f), 160.0f);
    auto icon = icons::scissors();
    icon.applyTransform (icon.getTransformToScaleToFit (c.removeFromTop (46.0f).withSizeKeepingCentre (40.0f, 40.0f), true));
    neonGlow (g, icon, col::red, 10.0f, 0.7f);
    g.setColour (col::redHot);
    g.fillPath (icon);
    c.removeFromTop (12.0f);
    goldText (g, "Separate vocals from the music", c.removeFromTop (30.0f), display (22.0f, true), juce::Justification::centred);
    g.setColour (col::textDim);
    g.setFont (ui (13.0f));
    g.drawFittedText ("Choose an engine and hit SEPARATE. Every stem can be soloed, chopped in STUDIO, saved to your library "
                      "or dragged straight onto a DAW track.", c.toNearestInt(), juce::Justification::centredTop, 3);
}

} // namespace snag
