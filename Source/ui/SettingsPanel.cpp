#include "SettingsPanel.h"
#include "../PluginProcessor.h"
#include "../Actions.h"

#include "../StandaloneBridge.h"

namespace snag
{
using namespace theme;

//==============================================================================
class ToolRow : public juce::Component
{
public:
    ToolRow (SettingsPanel& o, ToolManager::Tool t) : owner (o), tool (t)
    {
        setStyle (installBtn, "red");
        installBtn.onClick = [this]
        {
            installing = true;
            update();
            juce::Component::SafePointer<ToolRow> safe (this);
            actions::installTool (owner.getProcessor(), tool, [safe] (bool)
            {
                if (safe != nullptr) { safe->installing = false; safe->update(); }
            });
        };
        addAndMakeVisible (installBtn);

        setStyle (locateBtn, "ghost");
        locateBtn.setTooltip ("Use a copy you already have installed");
        locateBtn.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Locate " + ToolManager::displayName (tool));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (! f.existsAsFile()) return;
                auto& p = owner.getProcessor();
                p.getSettings().setToolOverride (tool == ToolManager::Tool::ai ? "Python" : ToolManager::displayName (tool), f.getFullPathName());
                p.getTools().refreshAsync();
            });
        };
        addAndMakeVisible (locateBtn);
        update();
    }

    void update()
    {
        auto st = owner.getProcessor().getTools().getStatus (tool);
        const bool refreshing = owner.getProcessor().getTools().isRefreshing();
        installBtn.setButtonText (installing ? "INSTALLING..." : (st.found ? (tool == ToolManager::Tool::ytdlp ? "UPDATE" : "REINSTALL") : "INSTALL"));
        installBtn.setEnabled (! installing);
        setStyle (installBtn, st.found ? "gold" : "red");
        status = installing ? "Installing..." : (refreshing && ! st.found ? "Checking..." : (st.found ? "Ready" + (st.version.isNotEmpty() ? "  -  " + st.version : juce::String()) : (st.detail.isNotEmpty() ? st.detail : "Not installed")));
        found = st.found;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10);
        auto right = r.removeFromRight (230);
        installBtn.setBounds (right.removeFromRight (130).withSizeKeepingCentre (130, 32));
        right.removeFromRight (8);
        locateBtn.setBounds (right.withSizeKeepingCentre (right.getWidth(), 30));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        glossPanel (g, r, 8.0f, col::bg3, col::bg1, false, 0.04f);

        auto c = r.reduced (16.0f, 10.0f);
        c.removeFromRight (240.0f);
        auto dot = c.removeFromLeft (12.0f).withSizeKeepingCentre (8.0f, 8.0f).translated (0, -12.0f);
        if (found)
        {
            g.setColour (col::gold);
            g.fillEllipse (dot);
        }
        else
        {
            juce::Path p; p.addEllipse (dot);
            neonGlow (g, p, col::red, 6.0f, 0.8f);
            g.setColour (col::red);
            g.fillEllipse (dot);
        }
        c.removeFromLeft (8.0f);

        auto titleRow = c.removeFromTop (22.0f);
        g.setColour (col::goldLight);
        g.setFont (display (16.0f, true));
        g.drawText (ToolManager::displayName (tool), titleRow, juce::Justification::centredLeft);
        const float tw = juce::GlyphArrangement::getStringWidth (display (16.0f, true), ToolManager::displayName (tool));
        g.setColour (found ? col::goldPale : col::redHot);
        g.setFont (ui (11.0f, true));
        g.drawText (status, titleRow.withTrimmedLeft (tw + 14.0f), juce::Justification::centredLeft);

        g.setColour (col::textDim);
        g.setFont (ui (11.5f));
        g.drawFittedText (ToolManager::description (tool), c.toNearestInt(), juce::Justification::topLeft, 2);
    }

    SettingsPanel& owner;
    ToolManager::Tool tool;
    bool installing = false;

private:
    juce::TextButton installBtn { "INSTALL" }, locateBtn { "LOCATE..." };
    juce::String status;
    bool found = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
SettingsPanel::SettingsPanel (EditorContext& c) : ctx (c), proc (c.getProcessor())
{
    closeBtn.onClick = [this] { close(); };
    addAndMakeVisible (closeBtn);

    for (auto t : { ToolManager::Tool::ffmpeg, ToolManager::Tool::ytdlp, ToolManager::Tool::deno, ToolManager::Tool::ai })
        addAndMakeVisible (rows.add (new ToolRow (*this, t)));

    setStyle (installAllBtn, "redFill");
    installAllBtn.setTooltip ("Install everything that's missing (FFmpeg, yt-dlp, Deno)");
    installAllBtn.onClick = [this]
    {
        int n = 0;
        for (auto* r : rows)
            if (r->tool != ToolManager::Tool::ai && ! proc.getTools().getStatus (r->tool).found)
            {
                r->installing = true;
                r->update();
                juce::Component::SafePointer<ToolRow> safe (r);
                actions::installTool (proc, r->tool, [safe] (bool) { if (safe != nullptr) { safe->installing = false; safe->update(); } });
                ++n;
            }
        if (n == 0) ctx.toast ("FFmpeg, yt-dlp and Deno are already installed.");
    };
    addAndMakeVisible (installAllBtn);
    setStyle (recheckBtn, "ghost");
    recheckBtn.onClick = [this] { proc.getTools().refreshAsync(); updateRows(); };
    addAndMakeVisible (recheckBtn);

    cookiesBox.addItem ("Don't use browser cookies", 1);
    const char* browsers[] = { "chrome", "firefox", "safari", "edge", "brave", "opera", "vivaldi" };
    for (int i = 0; i < 7; ++i)
        cookiesBox.addItem (juce::String ("Use cookies from ") + juce::String (browsers[i]).substring (0, 1).toUpperCase() + juce::String (browsers[i]).substring (1), i + 2);
    {
        auto cur = proc.getSettings().getCookiesBrowser();
        int id = 1;
        for (int i = 0; i < 7; ++i) if (cur == browsers[i]) id = i + 2;
        cookiesBox.setSelectedId (id, juce::dontSendNotification);
    }
    cookiesBox.setTooltip ("Lets HQ SNAG reach age-restricted or sign-in-only videos using your normal browser's login");
    cookiesBox.onChange = [this, browsers]
    {
        const int id = cookiesBox.getSelectedId();
        proc.getSettings().setCookiesBrowser (id <= 1 ? juce::String() : juce::String (browsers[id - 2]));
    };
    addAndMakeVisible (cookiesBox);

    bitsBox.addItem ("16-bit WAV", 16);
    bitsBox.addItem ("24-bit WAV", 24);
    bitsBox.addItem ("32-bit float WAV", 32);
    bitsBox.setSelectedId (proc.getSettings().getExportBitDepth(), juce::dontSendNotification);
    bitsBox.onChange = [this] { proc.getSettings().setExportBitDepth (bitsBox.getSelectedId()); };
    addAndMakeVisible (bitsBox);

    libLabel.setFont (ui (11.5f, true));
    libLabel.setColour (juce::Label::textColourId, col::goldPale);
    libLabel.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (libLabel);
    libBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose your sample library folder", proc.getSettings().getLibraryDir());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().isDirectory())
            {
                proc.getSettings().setLibraryDir (fc.getResult());
                updateRows();
            }
        });
    };
    addAndMakeVisible (libBtn);

    audioBtn.onClick = []
    {
        standalone::showAudioSettings();
    };
    addChildComponent (audioBtn);
    audioBtn.setVisible (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone);

    proc.getTools().addChangeListener (this);
    setVisible (false);
}

SettingsPanel::~SettingsPanel()
{
    proc.getTools().removeChangeListener (this);
}

void SettingsPanel::open()
{
    setVisible (true);
    toFront (true);
    proc.getTools().refreshAsync();
    updateRows();
    startTimerHz (4);
}

void SettingsPanel::close()
{
    stopTimer();
    setVisible (false);
}

void SettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*) { updateRows(); }
void SettingsPanel::timerCallback() { updateRows(); }

void SettingsPanel::updateRows()
{
    for (auto* r : rows) r->update();
    libLabel.setText (proc.getSettings().getLibraryDir().getFullPathName(), juce::dontSendNotification);
}

void SettingsPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card.contains (e.getPosition()))
        close();
}

void SettingsPanel::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (juce::jmin (820, getWidth() - 40), juce::jmin (596, getHeight() - 40));
    auto r = card.reduced (24, 18);
    closeBtn.setBounds (r.getRight() - 30, r.getY(), 30, 30);
    r.removeFromTop (44);

    auto toolsHead = r.removeFromTop (26);
    recheckBtn.setBounds (toolsHead.removeFromRight (100).reduced (0, 1));
    toolsHead.removeFromRight (8);
    installAllBtn.setBounds (toolsHead.removeFromRight (130).reduced (0, 0));
    r.removeFromTop (8);

    for (auto* row : rows)
    {
        row->setBounds (r.removeFromTop (74));
        r.removeFromTop (6);
    }

    r.removeFromTop (12);
    r.removeFromTop (20);   // "Preferences" heading
    auto prefs = r.removeFromTop (34);
    cookiesBox.setBounds (prefs.removeFromLeft (260).reduced (0, 2));
    prefs.removeFromLeft (12);
    bitsBox.setBounds (prefs.removeFromLeft (170).reduced (0, 2));
    prefs.removeFromLeft (12);
    audioBtn.setBounds (prefs.removeFromRight (200).reduced (0, 1));

    r.removeFromTop (10);
    auto lib = r.removeFromTop (30);
    libBtn.setBounds (lib.removeFromRight (100).reduced (0, 1));
    lib.removeFromRight (10);
    lib.removeFromLeft (110);
    libLabel.setBounds (lib);
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.72f));

    auto c = card.toFloat();
    juce::Path shape; shape.addRoundedRectangle (c, 12.0f);
    neonGlow (g, shape, col::gold, 24.0f, 0.25f);
    glossPanel (g, c, 12.0f, col::bg3, col::bg0, true, 0.05f);

    auto r = card.reduced (24, 18).toFloat();
    auto titleRow = r.removeFromTop (34.0f);
    goldText (g, "Settings", titleRow, display (26.0f, true));
    g.setColour (col::red);
    g.fillRect (juce::Rectangle<float> (titleRow.getX(), titleRow.getBottom() + 2.0f, 42.0f, 2.0f));

    r.removeFromTop (10.0f);
    sectionLabel (g, "Helper tools", r.removeFromTop (26.0f));

    auto prefsHead = juce::Rectangle<float> (r.getX(), (float) cookiesBox.getY() - 24.0f, 300.0f, 18.0f);
    sectionLabel (g, "Preferences", prefsHead);

    g.setColour (col::textDim);
    g.setFont (ui (11.5f, true));
    g.drawText ("LIBRARY FOLDER", juce::Rectangle<float> (r.getX(), (float) libLabel.getY(), 110.0f, (float) libLabel.getHeight()), juce::Justification::centredLeft);

    g.setColour (col::textFaint);
    g.setFont (ui (10.5f));
    g.drawFittedText ("Everything runs on your computer. Helper tools are downloaded from their official GitHub releases into Sample Snagger's own folder. "
                      "Only sample material you have the rights to use - clear samples before releasing music.",
                      card.reduced (24, 16).removeFromBottom (32), juce::Justification::centredLeft, 2);
}

} // namespace snag
