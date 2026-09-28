#pragma once

#include "Page.h"
#include "../core/WebCapture.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace snag
{

//==============================================================================
/** Built-in web browser + capture bar:
      LIVE REC     - record whatever is playing on the page, like a tape deck
      GRAB LAST    - "hindsight": keep the last 60 s, grab it after you heard it
      HQ SNAG      - full-quality audio of the page (or just IN..OUT) via yt-dlp
      IMPORT FILE  - any audio or video file
      REC INPUT    - record your audio interface / the plug-in's input */
class BrowsePage : public juce::Component, private juce::Timer
{
public:
    explicit BrowsePage (EditorContext&);
    ~BrowsePage() override;

    void pageVisibilityChanged (bool nowVisible);
    void goTo (juce::String url);
    juce::String getCurrentUrl() const { return currentUrl; }

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

    static bool hasEmbeddedBrowser();

private:
    void timerCallback() override;
    void updateCaptureUi();
    void toggleLiveRec();
    void grabLast (double seconds);
    void markPoint (bool isIn);
    void snagHq();
    void toggleInputRec();
    void evaluate (const juce::String& js, WebCapture::ResultCallback cb);
    void createBrowserIfNeeded();
    void withPageInfo (std::function<void (const juce::var&)> cb);

    EditorContext& ctx;
    SnaggerProcessor& proc;

    IconButton backBtn    { "back",    theme::icons::back(),    {}, "icon" };
    IconButton fwdBtn     { "forward", theme::icons::forward(), {}, "icon" };
    IconButton reloadBtn  { "reload",  theme::icons::reload(),  {}, "icon" };
    IconButton homeBtn    { "home",    theme::icons::home(),    {}, "icon" };
    juce::TextEditor urlField;
    juce::TextButton goBtn { "GO" };
    juce::OwnedArray<juce::TextButton> quickLinks;

#if JUCE_WEB_BROWSER
    class Browser;
    std::unique_ptr<Browser> browser;
#endif
    std::unique_ptr<WebCapture> capture;

    // capture bar
    IconButton liveRecBtn  { "liverec", theme::icons::record(), "LIVE REC", "red" };
    LevelMeter meter;
    juce::Label captureInfo;
    juce::OwnedArray<juce::TextButton> grabButtons;
    juce::TextButton markInBtn { "IN" }, markOutBtn { "OUT" }, clearMarksBtn { "x" };
    IconButton snagBtn     { "snag", theme::icons::download(), "HQ SNAG", "red" };
    IconButton importBtn   { "import", theme::icons::folder(), "IMPORT" };
    IconButton recInputBtn { "recinput", theme::icons::mic(), "REC INPUT" };

    juce::String currentUrl, currentTitle, pendingUrl;
    double markIn = -1.0, markOut = -1.0;
    bool pageVisible = false;
    juce::uint32 liveRecStarted = 0;

    juce::Rectangle<int> captureBarArea, browserArea;

    JUCE_DECLARE_WEAK_REFERENCEABLE (BrowsePage)
};

} // namespace snag
