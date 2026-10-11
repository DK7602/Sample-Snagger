#pragma once

#include "Page.h"
#include "../core/Session.h"
#include "../core/Tools.h"
#include <atomic>
#include <memory>

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

    // the tray samples kept on disk for projects ("Sessions" folder): how much, and the clean-up
    juce::Label cacheLabel;
    juce::TextButton cacheBtn { "CLEAN UP" };
    std::vector<sessioncache::Folder> cacheFolders;
    bool cacheScanned = false;
    int cacheScanSerial = 0;
    std::shared_ptr<std::atomic<bool>> cacheStop = std::make_shared<std::atomic<bool>> (false);
    void scanCache();
    void showCleanUpMenu();
    void cleanUp (int days);
    juce::int64 removableAfter (int days) const;
    juce::ThreadPool cacheWorker { juce::ThreadPoolOptions{}.withThreadName ("Snagger sessions").withNumberOfThreads (1) };
};

} // namespace snag
