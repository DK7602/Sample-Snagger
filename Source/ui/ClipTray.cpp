#include "ClipTray.h"
#include "../PluginProcessor.h"
#include "../Actions.h"

namespace snag
{
using namespace theme;

//==============================================================================
class ClipCard : public juce::Component, public juce::SettableTooltipClient
{
public:
    ClipCard (ClipTray& o, Clip::Ptr c) : owner (o), clip (c)
    {
        setTooltip (clip->name + "\nClick: select - Double-click: edit - Drag: into your DAW - Right-click: more");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool selected = owner.getProcessor().session.getSelected() == clip;
        const auto accent = clip->isStem() ? stemColour (clip->stemName) : col::gold;

        r = r.reduced (1.5f);
        if (selected)
        {
            juce::Path p; p.addRoundedRectangle (r, 8.0f);
            neonGlow (g, p, col::red, 10.0f, 0.45f);
        }
        satinSurface (g, r, 8.0f, isMouseOver (true) || selected, false, true);
        goldBorder (g, r.reduced (0.6f), 8.0f, 1.0f, selected ? 0.8f : 0.25f);
        if (selected)
        {
            juce::Path bar; bar.addRoundedRectangle (r.withHeight (2.5f).reduced (14.0f, 0.0f).translated (0.0f, r.getHeight() - 3.5f), 1.0f);
            glowPath (g, bar, col::red, 1.0f);
        }

        auto c = r.reduced (10.0f, 7.0f);
        auto top = c.removeFromTop (16.0f);

        // kind badge
        auto kind = clip->isStem() ? prettyStemName (clip->stemName).toUpperCase() : clip->kind.toUpperCase();
        auto badgeFont = ui (8.5f, true).withExtraKerningFactor (0.1f);
        const float bw = juce::GlyphArrangement::getStringWidth (badgeFont, kind) + 10.0f;
        auto badge = top.removeFromLeft (bw).withSizeKeepingCentre (bw, 13.0f);
        g.setColour (accent.withAlpha (0.18f));
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (accent);
        g.setFont (badgeFont);
        g.drawText (kind, badge, juce::Justification::centred);

        g.setColour (col::textDim);
        g.setFont (ui (10.0f, true));
        g.drawText (formatTime (clip->audio->lengthSeconds(), true), top, juce::Justification::centredRight);

        c.removeFromTop (3.0f);
        g.setColour (selected ? col::goldLight : col::text);
        g.setFont (ui (12.0f, true));
        g.drawFittedText (clip->name, c.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);

        c.removeFromTop (3.0f);
        thumb.draw (g, c, clip->audio.get(), accent, 0, -1, true);

        if (dragging)
        {
            g.setColour (col::gold.withAlpha (0.15f));
            g.fillRoundedRectangle (r, 8.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        auto& proc = owner.getProcessor();
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        proc.session.select (clip.get());
        proc.setSamplerClip (clip);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        owner.getContext().showTab (Tab::studio);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || e.mods.isPopupMenu())
            return;
        dragging = true;
        repaint();
        auto f = actions::makeDragFile (owner.getProcessor(), *clip);
        DragHandle::startExternalDrag (*this, f);
        dragging = false;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging && ! e.mods.isPopupMenu() && e.getNumberOfClicks() == 1 && e.getDistanceFromDragStart() < 4)
        {
            auto& proc = owner.getProcessor();
            if (proc.isPreviewing() && proc.getPreviewSource() == clip->audio.get())
                proc.stopPreview();
        }
        dragging = false;
    }

    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Play");
        m.addItem (2, "Open in Studio");
        m.addItem (3, "Separate stems...");
        m.addSeparator();
        m.addItem (4, "Save to library");
        m.addItem (5, "Rename...");
        m.addItem (6, "Duplicate");
        m.addSeparator();
        m.addItem (7, clip->isStem() ? "Remove" : "Remove (and its stems)");

        juce::Component::SafePointer<ClipCard> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr) return;
            auto& tray = safe->owner;
            auto& proc = tray.getProcessor();
            auto c = safe->clip;
            switch (r)
            {
                case 1: for (auto& g : proc.layerGains) g = 1.0f; proc.previewClip (*c); break;
                case 2: proc.session.select (c.get()); tray.getContext().showTab (Tab::studio); break;
                case 3: proc.session.select (c.get()); tray.getContext().showTab (Tab::stems); break;
                case 4: actions::saveToLibrary (proc, *c); break;
                case 5:
                {
                    auto* w = new juce::AlertWindow ("Rename", {}, juce::MessageBoxIconType::NoIcon);
                    w->addTextEditor ("name", c->name);
                    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, c, &proc] (int res)
                    {
                        if (res == 1 && w->getTextEditorContents ("name").trim().isNotEmpty())
                        {
                            c->name = w->getTextEditorContents ("name").trim();
                            proc.session.clipChanged (c.get(), false);
                        }
                    }), true);
                    break;
                }
                case 6:
                {
                    Clip::Ptr d (new Clip());
                    d->name = c->name + " copy";
                    d->kind = c->kind;
                    d->origin = c->origin;
                    d->audio = c->audio;
                    d->slices = c->slices;
                    d->bpm = c->bpm;
                    proc.session.add (d, true);
                    break;
                }
                case 7: proc.session.remove (c.get()); break;
                default: break;
            }
        });
    }

    ClipTray& owner;
    Clip::Ptr clip;

private:
    WaveThumb thumb;
    bool dragging = false;
};

//==============================================================================
ClipTray::ClipTray (EditorContext& c) : ctx (c), proc (c.getProcessor())
{
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);

    importBtn.setTooltip ("Import audio or video files");
    importBtn.onClick = [this] { ctx.chooseAndImportFiles(); };
    addAndMakeVisible (importBtn);

    setStyle (clearBtn, "ghost");
    clearBtn.setTooltip ("Remove every clip from this session (your library is not touched)");
    clearBtn.onClick = [this]
    {
        if (proc.session.getClips().isEmpty()) return;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Clear the session?",
                                            "Removes all captured clips and stems from this session. Samples you saved to the library stay.",
                                            "Clear", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) { proc.stopPreview(); proc.session.clear(); } }));
    };
    addAndMakeVisible (clearBtn);

    proc.session.addChangeListener (this);
    rebuild();
}

ClipTray::~ClipTray()
{
    proc.session.removeChangeListener (this);
}

void ClipTray::changeListenerCallback (juce::ChangeBroadcaster*) { rebuild(); }

void ClipTray::rebuild()
{
    juce::StringArray ids;
    for (auto* c : proc.session.getClips()) ids.add (c->id + c->name + juce::String::toHexString ((juce::pointer_sized_int) c->audio.get()));

    if (ids != shownIds)
    {
        shownIds = ids;
        cards.clear();
        for (auto* c : proc.session.getClips())
            holder.addAndMakeVisible (cards.add (new ClipCard (*this, c)));
        resized();

        // keep the selected card in view
        if (auto sel = proc.session.getSelected())
            for (auto* card : cards)
                if (card->clip == sel)
                {
                    auto b = card->getBounds();
                    auto vis = viewport.getViewArea();
                    if (vis.getWidth() <= 0) break;
                    if (b.getRight() > vis.getRight()) viewport.setViewPosition (b.getRight() - vis.getWidth() + 8, 0);
                    else if (b.getX() < vis.getX())    viewport.setViewPosition (juce::jmax (0, b.getX() - 8), 0);
                }
    }
    for (auto* card : cards) card->repaint();
    repaint();
}

void ClipTray::resized()
{
    auto r = getLocalBounds().reduced (14, 6).reduced (12, 8);
    auto left = r.removeFromLeft (110);
    left.removeFromTop (20);
    importBtn.setBounds (left.removeFromTop (32));
    left.removeFromTop (6);
    clearBtn.setBounds (left.removeFromTop (24));
    r.removeFromLeft (12);

    viewport.setBounds (r);
    const int cardW = 176, gap = 8;
    const int h = r.getHeight() - 8;
    holder.setSize (juce::jmax (r.getWidth(), cards.size() * (cardW + gap)), h);
    for (int i = 0; i < cards.size(); ++i)
    {
        auto* card = cards[i];
        const bool stem = card->clip->isStem();
        card->setBounds (i * (cardW + gap) + (stem ? 0 : 0), stem ? 6 : 0, cardW, stem ? h - 6 : h);
    }
}

void ClipTray::paint (juce::Graphics& g)
{
    // the session dock: one long black glass window along the bottom of the plate
    glassWindow (g, getLocalBounds().toFloat().reduced (17.0f, 7.0f), 10.0f, 0.9f);

    auto head = getLocalBounds().reduced (14, 6).reduced (12, 8).removeFromLeft (110).removeFromTop (18).toFloat();
    sectionLabel (g, "Session", head);
    g.setColour (col::textFaint);
    g.setFont (ui (10.0f, true));
    g.drawText (juce::String (proc.session.getClips().size()), head, juce::Justification::centredRight);

    if (cards.isEmpty())
    {
        auto area = viewport.getBounds().toFloat();
        g.setColour (col::lineSoft);
        const float dashes[] = { 6.0f, 5.0f };
        juce::Path p; p.addRoundedRectangle (area.reduced (2.0f), 8.0f);
        juce::Path d;
        juce::PathStrokeType (1.0f).createDashedStroke (d, p, dashes, 2);
        g.setColour (col::gold.withAlpha (0.3f));
        g.fillPath (d);
        g.setColour (col::textDim);
        g.setFont (ui (12.5f));
        g.drawText ("Your captures land here. Drop audio or video files anywhere on Sample Snagger to import them.",
                    area, juce::Justification::centred);
    }
}

void ClipTray::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    juce::ignoreUnused (e);
    viewport.setViewPosition (viewport.getViewPositionX() - (int) ((w.deltaY + w.deltaX) * 240.0f), 0);
}

} // namespace snag
