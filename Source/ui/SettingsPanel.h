#pragma once

#include "Page.h"
#include "../core/Tools.h"

namespace snag
{

class ToolRow;

//==============================================================================
/** Overlay with helper-tool installers and preferences. */
class SettingsPanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit SettingsPanel (EditorContext&);
    ~SettingsPanel() override;

    void open();
    void close();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    EditorContext& getContext() { return ctx; }
    SnaggerProcessor& getProcessor() { return proc; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateRows();

    EditorContext& ctx;
    SnaggerProcessor& proc;

    juce::Rectangle<int> card;
    IconButton closeBtn { "close", theme::icons::close(), {}, "icon" };
    juce::OwnedArray<ToolRow> rows;
    juce::TextButton installAllBtn { "INSTALL ALL" };
    juce::TextButton recheckBtn { "RE-CHECK" };

    juce::ComboBox cookiesBox, bitsBox;
    juce::Label libLabel;
    juce::TextButton libBtn { "CHANGE" };
    juce::TextButton audioBtn { "AUDIO / MIDI SETTINGS" };
    juce::TextButton licencesBtn { "LICENCES" };
    void showLicences();
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace snag
