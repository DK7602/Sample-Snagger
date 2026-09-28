#include "PluginEditor.h"
#include "Actions.h"
#include "core/AudioFileIO.h"

#include "StandaloneBridge.h"

using namespace snag;
using namespace snag::theme;

static constexpr int headerHeight = 66;
static constexpr int trayHeight = 112;
static int openEditors = 0;   // message thread only

//==============================================================================
SnaggerEditor::SnaggerEditor (SnaggerProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      browse (*this), studio (*this), stems (*this), library (*this), tray (*this), settingsPanel (*this)
{
    setLookAndFeel (&lnf.get());
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf.get());   // popups, alert windows
    ++openEditors;

    const char* names[] = { "Browse", "Studio", "Stems", "Library" };
    for (int i = 0; i < 4; ++i)
    {
        auto* b = tabButtons.add (new juce::TextButton (names[i]));
        setStyle (*b, "tab");
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { showTab ((Tab) i); };
        addAndMakeVisible (b);
    }
    tabButtons[0]->setTooltip ("Find something to sample - YouTube and more (Cmd/Ctrl+1)");
    tabButtons[1]->setTooltip ("Trim, chop, pitch, stretch - and play chops from MIDI (Cmd/Ctrl+2)");
    tabButtons[2]->setTooltip ("Separate vocals, drums, bass and music (Cmd/Ctrl+3)");
    tabButtons[3]->setTooltip ("Samples you've saved (Cmd/Ctrl+4)");

    gearBtn.setTooltip ("Settings & helper tools");
    gearBtn.onClick = [this] { openSettings(); };
    addAndMakeVisible (gearBtn);

    cancelBtn.setTooltip ("Cancel");
    cancelBtn.onClick = [this] { processor.jobs.cancelAll(); };
    addChildComponent (cancelBtn);
    addAndMakeVisible (outMeter);

    addChildComponent (browse);
    addChildComponent (studio);
    addChildComponent (stems);
    addChildComponent (library);
    addAndMakeVisible (tray);
    addChildComponent (settingsPanel);
    addAndMakeVisible (toasts);

    processor.onNotify = [safe = juce::Component::SafePointer<SnaggerEditor> (this)] (const juce::String& m, bool err)
    {
        if (safe != nullptr) safe->toast (m, err);
    };
    processor.onRequestSettings = [safe = juce::Component::SafePointer<SnaggerEditor> (this)]
    {
        if (safe != nullptr) safe->openSettings();
    };
    processor.jobs.addChangeListener (this);

    // Sample Snagger never passes its input through, so there's no feedback risk:
    // un-mute the input in the standalone app so REC INPUT just works.
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        standalone::unmuteInput();

    setResizable (true, true);
    setResizeLimits (1040, 700, 2600, 1700);
    const int w = processor.uiState.getProperty ("w", 1280);
    const int h = processor.uiState.getProperty ("h", 820);
    setSize (w, h);
    setWantsKeyboardFocus (true);

    const int savedTab = processor.uiState.getProperty ("tab", 0);
    showTab (processor.session.getClips().isEmpty() ? Tab::browse : (Tab) juce::jlimit (0, 3, savedTab));

    startTimerHz (20);
}

SnaggerEditor::~SnaggerEditor()
{
    processor.jobs.removeChangeListener (this);
    processor.onNotify = nullptr;
    processor.onRequestSettings = nullptr;
    setLookAndFeel (nullptr);
    if (--openEditors == 0)
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
void SnaggerEditor::showTab (Tab t)
{
    currentTab = t;
    browse.setVisible (t == Tab::browse);
    studio.setVisible (t == Tab::studio);
    stems.setVisible (t == Tab::stems);
    library.setVisible (t == Tab::library);
    browse.pageVisibilityChanged (t == Tab::browse && ! settingsPanel.isVisible());

    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setToggleState (i == (int) t, juce::dontSendNotification);

    processor.uiState.setProperty ("tab", (int) t, nullptr);
    if (t == Tab::studio)
        studio.setClip (processor.session.getSelected());
    grabKeyboardFocus();
    repaint();
}

void SnaggerEditor::openSettings()
{
    settingsPanel.open();
    browse.setVisible (false);   // native web views always draw on top, so hide it under the overlay
    browse.pageVisibilityChanged (false);
    settingsPanel.toFront (false);
    toasts.toFront (false);
}

void SnaggerEditor::toast (const juce::String& message, bool isError)
{
    toasts.show (message, isError, isError ? 7000 : 4000);
    toasts.toFront (false);
}

void SnaggerEditor::chooseAndImportFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Import audio or video", juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   audioio::importWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        juce::StringArray paths;
        for (auto& f : fc.getResults())
            paths.add (f.getFullPathName());
        if (! paths.isEmpty())
            actions::importFiles (processor, paths);
    });
}

//==============================================================================
bool SnaggerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (audioio::isImportable (juce::File (f)))
            return true;
    return false;
}

void SnaggerEditor::fileDragEnter (const juce::StringArray&, int, int)  { fileDragOver = true; repaint(); }
void SnaggerEditor::fileDragExit (const juce::StringArray&)             { fileDragOver = false; repaint(); }

void SnaggerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    fileDragOver = false;
    repaint();

    // Ignore our own drag-exports being dropped back onto us
    juce::StringArray wanted;
    for (auto& f : files)
        if (audioio::isImportable (juce::File (f)) && ! juce::File (f).isAChildOf (paths::dragExportDir()))
            wanted.add (f);
    if (! wanted.isEmpty())
        actions::importFiles (processor, wanted);
}

bool SnaggerEditor::keyPressed (const juce::KeyPress& k)
{
    for (int i = 0; i < 4; ++i)
        if (k == juce::KeyPress ('1' + i, juce::ModifierKeys::commandModifier, 0))
        {
            showTab ((Tab) i);
            return true;
        }

    if (k == juce::KeyPress::escapeKey && settingsPanel.isVisible())
    {
        settingsPanel.close();
        return true;
    }

    if (currentTab == Tab::studio && studio.handleKey (k))
        return true;

    if (k == juce::KeyPress::spaceKey && processor.isPreviewing())
    {
        processor.stopPreview();
        return true;
    }
    return false;
}

//==============================================================================
void SnaggerEditor::timerCallback()
{
    outMeter.setLevel (processor.outputLevel.load());
    processor.outputLevel = processor.outputLevel.load() * 0.8f;

    // restore the browser when the settings overlay closes
    if (! settingsPanel.isVisible() && currentTab == Tab::browse && ! browse.isVisible())
    {
        browse.setVisible (true);
        browse.pageVisibilityChanged (true);
    }

    if (processor.jobs.isBusy())
        repaint (hudArea);
}

void SnaggerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    cancelBtn.setVisible (processor.jobs.isBusy());
    repaint (hudArea);
}

//==============================================================================
void SnaggerEditor::resized()
{
    auto r = getLocalBounds();
    processor.uiState.setProperty ("w", getWidth(), nullptr);
    processor.uiState.setProperty ("h", getHeight(), nullptr);

    headerArea = r.removeFromTop (headerHeight);
    layoutHeader (headerArea);

    auto trayArea = r.removeFromBottom (trayHeight);
    tray.setBounds (trayArea);

    for (auto* page : std::initializer_list<juce::Component*> { &browse, &studio, &stems, &library })
        page->setBounds (r);

    settingsPanel.setBounds (getLocalBounds());
    toasts.setBounds (getLocalBounds().removeFromBottom (trayHeight + 130).removeFromRight (520));
}

void SnaggerEditor::layoutHeader (juce::Rectangle<int> r)
{
    r = r.reduced (18, 0);
    logoArea = r.removeFromLeft (290);

    gearBtn.setBounds (r.removeFromRight (40).withSizeKeepingCentre (36, 36));
    r.removeFromRight (10);
    outMeter.setBounds (r.removeFromRight (70).withSizeKeepingCentre (70, 5));
    r.removeFromRight (14);
    hudArea = r.removeFromRight (juce::jmin (330, r.getWidth() / 3));
    cancelBtn.setBounds (hudArea.withLeft (hudArea.getRight() - 26).withSizeKeepingCentre (22, 22));

    const int tabW = juce::jmin (130, r.getWidth() / 4);
    auto tabs = r.withSizeKeepingCentre (tabW * 4, headerHeight);
    for (auto* b : tabButtons)
        b->setBounds (tabs.removeFromLeft (tabW).reduced (4, 12));
}

void SnaggerEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // body: deep gloss black with a faint warm vignette
    g.setGradientFill (juce::ColourGradient (col::bg1, 0, 0, col::bg0, 0, r.getBottom(), false));
    g.fillAll();
    {
        juce::ColourGradient vignette (col::goldDeep.withAlpha (0.10f), r.getCentreX(), headerHeight + 40.0f,
                                       juce::Colours::transparentBlack, r.getCentreX(), r.getHeight() * 0.9f, true);
        g.setGradientFill (vignette);
        g.fillRect (r);
    }

    // header
    auto h = headerArea.toFloat();
    g.setGradientFill (juce::ColourGradient (col::bg3, 0, h.getY(), col::bg1, 0, h.getBottom(), false));
    g.fillRect (h);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), 0, h.getY(),
                                             juce::Colours::transparentWhite, 0, h.getCentreY(), false));
    g.fillRect (h.withHeight (h.getHeight() * 0.5f));
    auto hair = goldGradient (h.withHeight (1.0f).withY (h.getBottom() - 1.0f), false);
    g.setGradientFill (hair);
    g.fillRect (h.withHeight (1.0f).withY (h.getBottom() - 1.0f));

    // logo
    auto logo = logoArea.toFloat();
    drawLogoMark (g, logo.removeFromLeft (44.0f).withSizeKeepingCentre (42.0f, 42.0f));
    logo.removeFromLeft (12.0f);
    goldText (g, "SAMPLE SNAGGER", logo.withTrimmedBottom (20.0f).withTrimmedTop (12.0f), display (22.0f, true));
    g.setColour (col::textDim);
    g.setFont (ui (9.0f, true).withExtraKerningFactor (0.42f));
    g.drawText ("CAPTURE  -  CHOP  -  SEPARATE", logo.withTrimmedTop (38.0f).withHeight (14.0f), juce::Justification::centredLeft);

    // job HUD
    auto hud = hudArea.toFloat();
    auto active = processor.jobs.getActiveJobs();
    if (! active.empty())
    {
        auto& job = *active.front();
        auto box = hud.withSizeKeepingCentre (hud.getWidth(), 42.0f);
        glossPanel (g, box, 8.0f, col::bg2, col::bg0, true, 0.04f);
        auto inner = box.reduced (10.0f, 6.0f).withTrimmedRight (26.0f);
        drawSpinner (g, inner.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f), col::red);
        inner.removeFromLeft (8.0f);
        auto title = job.title + (active.size() > 1 ? "  (+" + juce::String ((int) active.size() - 1) + ")" : juce::String());
        g.setColour (col::goldLight);
        g.setFont (ui (11.0f, true));
        g.drawText (title.toUpperCase(), inner.removeFromTop (14.0f), juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.setFont (ui (10.0f));
        g.drawText (job.getStatus(), inner.removeFromTop (12.0f), juce::Justification::centredLeft);
        auto bar = inner.withHeight (3.0f).withY (box.getBottom() - 8.0f);
        g.setColour (col::bg0);
        g.fillRoundedRectangle (bar, 1.5f);
        const float p = job.getProgress();
        if (p >= 0.0f)
        {
            g.setGradientFill (goldGradient (bar, false));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, p)), 1.5f);
        }
        else
        {
            const float t = (float) (juce::Time::getMillisecondCounter() % 1400) / 1400.0f;
            auto seg = bar.withWidth (bar.getWidth() * 0.25f).withX (bar.getX() + bar.getWidth() * (t * 1.25f - 0.25f));
            g.setGradientFill (goldGradient (seg, false));
            g.fillRoundedRectangle (seg.getIntersection (bar), 1.5f);
        }
    }
    else
    {
        auto clip = processor.session.getSelected();
        g.setColour (col::textFaint);
        g.setFont (ui (10.5f, true));
        g.drawText (clip != nullptr ? "SELECTED:  " + clip->name.toUpperCase() : "READY", hud, juce::Justification::centredRight, true);
    }
}

void SnaggerEditor::paintOverChildren (juce::Graphics& g)
{
    if (fileDragOver)
    {
        auto r = getLocalBounds().toFloat().reduced (8.0f);
        g.setColour (col::bg0.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 14.0f);
        juce::Path p; p.addRoundedRectangle (r, 14.0f);
        juce::Path s; juce::PathStrokeType (2.0f).createStrokedPath (s, p);
        neonGlow (g, s, col::gold, 18.0f, 0.8f);
        goldBorder (g, r, 14.0f, 2.0f);
        goldText (g, "Drop to import", r.withSizeKeepingCentre (r.getWidth(), 40.0f), display (32.0f, true), juce::Justification::centred);
        g.setColour (col::textDim);
        g.setFont (ui (14.0f));
        g.drawText ("Audio or video - mp3, wav, flac, mp4, mov, mkv, webm...", r.withSizeKeepingCentre (r.getWidth(), 30.0f).translated (0, 40.0f), juce::Justification::centred);
    }
}
