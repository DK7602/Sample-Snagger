#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/Page.h"
#include "ui/BrowsePage.h"
#include "ui/StudioPage.h"
#include "ui/StemsPage.h"
#include "ui/LibraryPage.h"
#include "ui/SettingsPanel.h"
#include "ui/ClipTray.h"

//==============================================================================
class SnaggerEditor final : public juce::AudioProcessorEditor,
                            public snag::EditorContext,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    explicit SnaggerEditor (SnaggerProcessor&);
    ~SnaggerEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // EditorContext
    SnaggerProcessor& getProcessor() override { return processor; }
    void toast (const juce::String& message, bool isError = false) override;
    void showTab (snag::Tab) override;
    void openSettings() override;
    void chooseAndImportFiles() override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    snag::Tab getCurrentTab() const { return currentTab; }

    // for the screenshot / test runner
    snag::StudioPage& getStudioPage()     { return studio; }
    snag::StemsPage& getStemsPage()       { return stems; }
    snag::SettingsPanel& getSettingsPanel() { return settingsPanel; }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void layoutHeader (juce::Rectangle<int>);

    SnaggerProcessor& processor;
    juce::SharedResourcePointer<snag::SnaggerLookAndFeel> lnf;

    // header
    juce::OwnedArray<juce::TextButton> tabButtons;
    snag::IconButton gearBtn   { "settings", snag::theme::icons::gear(), {}, "chip" };
    snag::IconButton cancelBtn { "cancel", snag::theme::icons::close(), {}, "icon" };
    snag::LevelMeter outMeter;
    juce::Rectangle<int> headerArea, hudArea, logoArea, tabsArea, hudGlass;
    snag::theme::GoldPlate goldPlate;

    // pages
    snag::BrowsePage  browse;
    snag::StudioPage  studio;
    snag::StemsPage   stems;
    snag::LibraryPage library;
    snag::ClipTray    tray;
    snag::SettingsPanel settingsPanel;
    snag::ToastOverlay toasts;
    juce::TooltipWindow tooltips { this, 700 };

    snag::Tab currentTab = snag::Tab::browse;
    bool fileDragOver = false;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnaggerEditor)
};
