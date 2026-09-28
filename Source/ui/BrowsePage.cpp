#include "BrowsePage.h"
#include "../PluginProcessor.h"
#include "../Actions.h"
#include "../core/EditOps.h"

#include "../StandaloneBridge.h"

namespace snag
{
using namespace theme;

static const char* homeUrl = "https://www.youtube.com";

/** Site chip that knows whether it was right-clicked. */
struct LinkChip : juce::TextButton
{
    using juce::TextButton::TextButton;
    using juce::TextButton::clicked;
    std::function<void (bool rightClick)> onChipClick;
    void clicked (const juce::ModifierKeys& mods) override
    {
        if (onChipClick) onChipClick (mods.isPopupMenu());
    }
};

//==============================================================================
#if JUCE_WEB_BROWSER
class BrowsePage::Browser : public juce::WebBrowserComponent
{
public:
    Browser (BrowsePage& o, const Options& opts) : juce::WebBrowserComponent (opts), owner (o) {}

    void pageFinishedLoading (const juce::String& url) override
    {
        if (url.startsWith ("http"))
        {
            owner.currentUrl = url;
            if (! owner.urlField.hasKeyboardFocus (true))
            {
                owner.urlField.setText (url, false);
                owner.urlEditedByUser = false;
            }
        }
        if (owner.capture != nullptr)
            owner.capture->pageChanged();
    }

    void newWindowAttemptingToLoad (const juce::String& url) override
    {
        goToURL (url);   // keep pop-ups inside the plug-in
    }

    bool pageLoadHadNetworkError (const juce::String& info) override
    {
        owner.ctx.toast ("Page couldn't load (" + info.substring (0, 80) + ")", true);
        return true;
    }

    BrowsePage& owner;
};
#endif

bool BrowsePage::hasEmbeddedBrowser()
{
   #if JUCE_WEB_BROWSER
    return true;
   #else
    return false;
   #endif
}

//==============================================================================
BrowsePage::BrowsePage (EditorContext& c) : ctx (c), proc (c.getProcessor())
{
    for (auto* b : { &backBtn, &fwdBtn, &reloadBtn, &homeBtn })
        addAndMakeVisible (b);
    backBtn.setTooltip ("Back");
    fwdBtn.setTooltip ("Forward");
    reloadBtn.setTooltip ("Reload");
    homeBtn.setTooltip ("YouTube home");

    urlField.setTextToShowWhenEmpty ("Search YouTube or paste any video / audio link", col::textFaint);
    urlField.setFont (ui (13.0f));
    urlField.setIndents (12, 0);
    urlField.setJustification (juce::Justification::centredLeft);
    urlField.onReturnKey = [this] { goTo (urlField.getText()); };
    urlField.onTextChange = [this] { urlEditedByUser = true; };   // only user edits: setText (..., false) is silent
    addAndMakeVisible (urlField);

    setStyle (goBtn, "gold");
    goBtn.onClick = [this] { goTo (urlField.getText()); };
    addAndMakeVisible (goBtn);

    const std::pair<const char*, const char*> links[] = {
        { "YouTube",     "https://www.youtube.com" },
        { "SoundCloud",  "https://soundcloud.com/discover" },
        { "TikTok",      "https://www.tiktok.com/explore" },
        { "Instagram",   "https://www.instagram.com/reels/" },
        { "Vimeo",       "https://vimeo.com/watch" },
        { "Bandcamp",    "https://bandcamp.com" },
        { "Internet Archive", "https://archive.org/details/audio" },
        { "Freesound",   "https://freesound.org" },
    };
    for (auto& [name, url] : links)
    {
        auto* b = new LinkChip (name);
        quickLinks.add (b);
        setStyle (*b, "chip");
        juce::String u (url);
        b->onChipClick = [this, u] (bool rightClick)
        {
            if (rightClick) openInSystemBrowser (u);
            else            goTo (u);
        };
        b->setTooltip ("Open " + juce::String (name) + " (right-click: open in your own browser)");
        addAndMakeVisible (b);
    }

    setStyle (openModeBtn, "chip");
    openModeBtn.setTooltip ("Choose where sites open: inside Sample Snagger (LIVE REC + hindsight work) "
                            "or in your own browser - Chrome, Safari, Edge... (copy a video link back here for HQ SNAG)");
    openModeBtn.onClick = [this]
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Open sites in...");
        m.addItem (1, "Sample Snagger's built-in browser", true, ! opensExternally());
        m.addItem (2, "My browser (Chrome, Safari, Edge...)", true, opensExternally());
        m.addSeparator();
        m.addItem (3, "Open this page in my browser now");
        juce::Component::SafePointer<BrowsePage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&openModeBtn), [safe] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) safe->setOpensExternally (false);
            else if (r == 2) safe->setOpensExternally (true);
            else if (r == 3) safe->openExternalBtn.triggerClick();
        });
    };
    addAndMakeVisible (openModeBtn);
    updateOpenModeButton();

    openExternalBtn.setTooltip ("Open this page in your own browser (Chrome, Safari, Edge...)");
    openExternalBtn.onClick = [this]
    {
        withPageInfo ([this] (const juce::var& info)
        {
            auto u = info.getProperty ("url", "").toString();
            if (! u.startsWithIgnoreCase ("http")) u = normaliseUrl (urlField.getText());
            if (u.isEmpty()) u = homeUrl;
            openInSystemBrowser (u);
        });
    };
    addAndMakeVisible (openExternalBtn);

#if JUCE_WEB_BROWSER
    // The web view itself is created lazily, once this page is really on screen
    // (see createBrowserIfNeeded). Hosts and build tools sometimes construct the editor
    // without ever showing it, and a native web view must not be started then.
    backBtn.onClick   = [this] { if (browser != nullptr) browser->goBack(); };
    fwdBtn.onClick    = [this] { if (browser != nullptr) browser->goForward(); };
    reloadBtn.onClick = [this] { if (browser != nullptr) browser->refresh(); };
#endif
    homeBtn.onClick = [this] { goTo (homeUrl); };

    capture = std::make_unique<WebCapture> ([this] (const juce::String& js, WebCapture::ResultCallback cb) { evaluate (js, std::move (cb)); });

    // ---- capture bar ----
    liveRecBtn.setClickingTogglesState (false);
    liveRecBtn.setTooltip ("Record whatever is playing in the browser, like a tape deck. Click again to stop and keep it.");
    liveRecBtn.onClick = [this] { toggleLiveRec(); };
    addAndMakeVisible (liveRecBtn);
    addAndMakeVisible (meter);

    captureInfo.setFont (ui (11.0f, true));
    captureInfo.setColour (juce::Label::textColourId, col::textDim);
    captureInfo.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (captureInfo);

    for (int secs : { 10, 20, 30, 60 })
    {
        auto* b = grabButtons.add (new juce::TextButton (juce::String (secs) + "s"));
        setStyle (*b, "chip");
        b->setTooltip ("Missed it? Grab the last " + juce::String (secs) + " seconds that played");
        b->onClick = [this, secs] { grabLast (secs); };
        addAndMakeVisible (b);
    }

    for (auto* b : { &markInBtn, &markOutBtn, &clearMarksBtn })
    {
        setStyle (*b, "chip");
        addAndMakeVisible (b);
    }
    markInBtn.setTooltip ("Set the start of an HQ section at the video's current time");
    markOutBtn.setTooltip ("Set the end of an HQ section at the video's current time");
    clearMarksBtn.setTooltip ("Clear IN / OUT (snag the whole thing)");
    markInBtn.onClick  = [this] { markPoint (true); };
    markOutBtn.onClick = [this] { markPoint (false); };
    clearMarksBtn.onClick = [this] { markIn = markOut = -1.0; updateCaptureUi(); };

    snagBtn.setTooltip ("Download full-quality audio of this page (or just IN..OUT) with yt-dlp");
    snagBtn.onClick = [this] { snagHq(); };
    addAndMakeVisible (snagBtn);

    importBtn.setTooltip ("Import any audio or video file (or just drop files anywhere on the plug-in)");
    importBtn.onClick = [this] { ctx.chooseAndImportFiles(); };
    addAndMakeVisible (importBtn);

    recInputBtn.setTooltip ("Record your audio input (standalone) or the audio routed into the plug-in");
    recInputBtn.onClick = [this] { toggleInputRec(); };
    addAndMakeVisible (recInputBtn);

    updateCaptureUi();
    startTimerHz (10);

    pendingUrl = homeUrl;
    urlField.setText (homeUrl, false);
}

BrowsePage::~BrowsePage()
{
    stopTimer();
    capture.reset();
}

void BrowsePage::createBrowserIfNeeded()
{
#if JUCE_WEB_BROWSER
    if (browser != nullptr || ! isShowing())
        return;

    using Opts = juce::WebBrowserComponent::Options;
    auto opts = Opts{}
                  .withBackend (Opts::Backend::webview2)
                  .withKeepPageLoadedWhenBrowserIsHidden()
                  // installed before each page's own scripts, so Web Audio players are heard too
                  .withUserScript (WebCapture::getTapScript())
                  .withWinWebView2Options (Opts::WinWebView2{}
                                              .withUserDataFolder (paths::webDataDir())
                                              .withBackgroundColour (col::bg0)
                                              .withStatusBarDisabled());
   #if JUCE_MAC || JUCE_LINUX
    // A regular Safari user agent so sites serve their full desktop players.
    opts = opts.withUserAgent ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Safari/605.1.15");
   #endif

    browser = std::make_unique<Browser> (*this, opts);
    addAndMakeVisible (*browser);
    browser->setBounds (browserArea.reduced (1));

    auto url = pendingUrl.isNotEmpty() ? pendingUrl : juce::String (homeUrl);
    pendingUrl = {};
    browser->goToURL (url);
    if (capture != nullptr) capture->pageChanged();
    repaint();
#endif
}

void BrowsePage::evaluate (const juce::String& js, WebCapture::ResultCallback cb)
{
#if JUCE_WEB_BROWSER
    if (browser == nullptr)
    {
        if (cb) cb (juce::var());
        return;
    }
    juce::WeakReference<BrowsePage> weak (this);
    browser->evaluateJavascript (js, [weak, cb] (juce::WebBrowserComponent::EvaluationResult r)
    {
        if (weak.get() == nullptr || ! cb)
            return;
        if (auto* v = r.getResult())
            cb (*v);
        else
            cb (juce::var());
    });
#else
    juce::ignoreUnused (js);
    if (cb) cb (juce::var());
#endif
}

void BrowsePage::withPageInfo (std::function<void (const juce::var&)> cb)
{
#if JUCE_WEB_BROWSER
    evaluate ("window.__snag ? window.__snag.info() : JSON.stringify({url: location.href, title: document.title, time: (document.querySelector('video')||{}).currentTime ?? -1})",
              [this, cb] (const juce::var& v)
    {
        auto obj = v.isString() ? juce::JSON::parse (v.toString()) : v;
        if (! obj.isObject())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("url", currentUrl.isNotEmpty() ? currentUrl : urlField.getText());
            o->setProperty ("title", "");
            o->setProperty ("time", -1.0);
            obj = juce::var (o);
        }
        cb (obj);
    });
#else
    auto* o = new juce::DynamicObject();
    o->setProperty ("url", urlField.getText().trim());
    o->setProperty ("title", "");
    o->setProperty ("time", -1.0);
    cb (juce::var (o));
#endif
}

juce::String BrowsePage::normaliseUrl (juce::String url)
{
    url = url.trim();
    if (url.isEmpty())
        return {};
    const bool looksLikeUrl = url.startsWithIgnoreCase ("http://") || url.startsWithIgnoreCase ("https://")
                              || (! url.containsChar (' ') && url.containsChar ('.'));
    if (! looksLikeUrl)
        return "https://www.youtube.com/results?search_query=" + juce::URL::addEscapeChars (url, true);
    if (! url.startsWithIgnoreCase ("http"))
        return "https://" + url;
    return url;
}

bool BrowsePage::opensExternally() const
{
    return proc.getSettings().getString ("openSitesExternally", hasEmbeddedBrowser() ? "0" : "1") == "1";
}

void BrowsePage::setOpensExternally (bool shouldOpenExternally)
{
    proc.getSettings().setString ("openSitesExternally", shouldOpenExternally ? "1" : "0");
    updateOpenModeButton();
    ctx.toast (shouldOpenExternally ? "Sites now open in your own browser. Copy a video's link there, then hit HQ SNAG here."
                                    : "Sites now open inside Sample Snagger (LIVE REC and hindsight work here).");
}

void BrowsePage::updateOpenModeButton()
{
    openModeBtn.setButtonText (opensExternally() ? juce::String ("Opens in: My browser") : juce::String ("Opens in: Sample Snagger"));
    openModeBtn.setToggleState (opensExternally(), juce::dontSendNotification);
    resized();
}

void BrowsePage::openInSystemBrowser (const juce::String& url)
{
    auto u = normaliseUrl (url);
    if (u.isEmpty()) return;
    if (juce::URL (u).launchInDefaultBrowser())
        ctx.toast ("Opened in your browser. To grab audio from there: copy the video's link, then hit HQ SNAG here.");
    else
        ctx.toast ("Couldn't open your browser.", true);
}

void BrowsePage::goTo (juce::String url)
{
    url = normaliseUrl (url);
    if (url.isEmpty())
        return;
    urlEditedByUser = false;

    if (opensExternally())
    {
        urlField.setText (url, false);
        openInSystemBrowser (url);
        return;
    }

    currentUrl = url;
    urlField.setText (url, false);
    markIn = markOut = -1.0;
    updateCaptureUi();

#if JUCE_WEB_BROWSER
    if (browser != nullptr)
    {
        browser->goToURL (url);
        if (capture != nullptr) capture->pageChanged();
    }
    else
    {
        pendingUrl = url;
        createBrowserIfNeeded();
    }
#endif
}

void BrowsePage::pageVisibilityChanged (bool nowVisible)
{
    pageVisible = nowVisible;
    if (nowVisible)
        createBrowserIfNeeded();
    if (capture == nullptr)
        return;

    if (nowVisible)
        capture->setArmed (true);
    else if (! capture->isRecording())
    {
        evaluate ("window.__snag ? window.__snag.pauseAll() : (document.querySelectorAll('video,audio').forEach(function(m){m.pause()}), true)", nullptr);
        capture->setArmed (false);
    }
}

//==============================================================================
void BrowsePage::toggleLiveRec()
{
    if (! hasEmbeddedBrowser())
    {
        ctx.toast ("LIVE REC needs the built-in browser (available on macOS and Windows). Use HQ SNAG with a link, or REC INPUT.", true);
        return;
    }

    if (! capture->isRecording())
    {
        capture->startRecording();
        liveRecStarted = juce::Time::getMillisecondCounter();
        if (capture->getNumTaps() == 0)
            ctx.toast ("Recording - press play on the video");
    }
    else
    {
        auto audio = capture->stopRecording();
        if (audio == nullptr || edit::peakLevel (*audio) < 1.0e-4f)
        {
            ctx.toast ("Nothing was captured. Make sure the video is playing (and not muted on the page), then try again.", true);
        }
        else
        {
            withPageInfo ([this, audio] (const juce::var& info)
            {
                auto title = actions::niceTitle (info.getProperty ("title", "").toString().isNotEmpty()
                                                     ? info.getProperty ("title", "").toString()
                                                     : info.getProperty ("url", currentUrl).toString());
                actions::addCapture (proc, audio, title + " (live)", "Live", info.getProperty ("url", currentUrl).toString());
                ctx.toast ("Captured " + formatTime (audio->lengthSeconds(), true) + "s - it's in the tray. Open STUDIO to chop it.");
            });
        }
    }
    updateCaptureUi();
}

static AudioData::Ptr trimSilence (const AudioData& a)
{
    const int n = a.getNumSamples();
    const float thr = 1.0e-4f;
    int s = 0, e = n;
    auto loud = [&] (int i)
    {
        for (int c = 0; c < a.getNumChannels(); ++c)
            if (std::abs (a.buffer.getSample (c, i)) > thr) return true;
        return false;
    };
    while (s < n && ! loud (s)) ++s;
    while (e > s && ! loud (e - 1)) --e;
    if (e - s < 64) return nullptr;
    return edit::crop (a, s, e);
}

void BrowsePage::grabLast (double seconds)
{
    if (! hasEmbeddedBrowser() || capture == nullptr)
    {
        ctx.toast ("Hindsight grabs need the built-in browser (macOS / Windows).", true);
        return;
    }

    auto raw = capture->grabLast (seconds);
    auto audio = raw != nullptr ? trimSilence (*raw) : nullptr;
    if (audio == nullptr)
    {
        ctx.toast ("Nothing has played yet. Press play on a video - Sample Snagger keeps the last 60 s so you can grab it after you hear it.", true);
        return;
    }

    withPageInfo ([this, audio, seconds] (const juce::var& info)
    {
        auto title = actions::niceTitle (info.getProperty ("title", "").toString().isNotEmpty()
                                             ? info.getProperty ("title", "").toString()
                                             : info.getProperty ("url", currentUrl).toString());
        actions::addCapture (proc, audio, title + " (last " + juce::String ((int) seconds) + "s)", "Live",
                             info.getProperty ("url", currentUrl).toString());
        ctx.toast ("Grabbed the last " + juce::String ((int) seconds) + "s - it's in the tray.");
    });
}

void BrowsePage::markPoint (bool isIn)
{
    withPageInfo ([this, isIn] (const juce::var& info)
    {
        const double t = (double) info.getProperty ("time", -1.0);
        if (t < 0)
        {
            ctx.toast ("No playing video found on this page.", true);
            return;
        }
        if (isIn) { markIn = t; if (markOut >= 0 && markOut <= markIn) markOut = -1.0; }
        else      { markOut = t; if (markIn >= 0 && markIn >= markOut) markIn = -1.0; }
        updateCaptureUi();
    });
}

void BrowsePage::snagHq()
{
    auto typed = urlField.getText().trim();

    if (opensExternally())
    {
        // Sites are open in the user's own browser, so the link they just copied there is what they
        // mean - unless they've typed / pasted a link into our address bar since it was last set.
        auto clip = juce::SystemClipboard::getTextFromClipboard().trim();
        const bool clipIsLink = clip.startsWithIgnoreCase ("http") && ! clip.containsAnyOf (" \r\n\t");
        const bool fieldIsFresh = urlEditedByUser && typed.startsWithIgnoreCase ("http");
        if (clipIsLink && ! fieldIsFresh)
            typed = clip;

        if (! typed.startsWithIgnoreCase ("http"))
        {
            ctx.toast ("Copy the video's link in your browser (click the address bar, then Ctrl+C / Cmd+C), then hit HQ SNAG.", true);
            return;
        }
        urlField.setText (typed, false);
        urlEditedByUser = false;
        actions::downloadUrl (proc, typed);
        return;
    }

    // A link typed / pasted into the address bar wins over the page that's open.
    if (typed.startsWithIgnoreCase ("http") && (typed != currentUrl || ! hasEmbeddedBrowser()))
    {
        urlField.setText (typed, false);
        urlEditedByUser = false;
        actions::downloadUrl (proc, typed);
        return;
    }

    withPageInfo ([this] (const juce::var& info)
    {
        auto url = info.getProperty ("url", "").toString();
        if (url.isEmpty()) url = urlField.getText().trim();
        double in = markIn, out = markOut;
        if (in >= 0 && out < 0)
        {
            const double t = (double) info.getProperty ("time", -1.0);
            if (t > in) out = t;              // IN set, no OUT: snag up to where the video is now
        }
        if (in < 0 && out > 0) in = 0.0;
        actions::downloadUrl (proc, url, in, out, info.getProperty ("title", "").toString());
    });
}

void BrowsePage::toggleInputRec()
{
    if (proc.isRecordingInput())
    {
        auto clip = proc.stopInputRecording();
        if (clip == nullptr || edit::peakLevel (*clip->audio) < 1.0e-4f)
            ctx.toast ("The input was silent. Check your input device (standalone: gear > Audio Settings).", true);
        else
        {
            proc.session.add (clip, true);
            proc.setSamplerClip (clip);
            ctx.toast ("Recorded " + formatTime (clip->audio->lengthSeconds(), true) + "s from the input.");
        }
    }
    else
    {
        if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
            standalone::unmuteInput();
        juce::String err;
        if (! proc.startInputRecording (err))
            ctx.toast (err, true);
    }
    updateCaptureUi();
}

//==============================================================================
void BrowsePage::timerCallback()
{
    if (pageVisible)
        createBrowserIfNeeded();

    if (capture != nullptr)
        meter.setLevel (capture->isArmed() ? capture->getLevel() : 0.0f);

    updateCaptureUi();

    // keep the address bar in sync with single-page sites like YouTube
    static int counter = 0;
    if (pageVisible && ++counter % 10 == 0 && ! urlField.hasKeyboardFocus (true) && hasEmbeddedBrowser())
    {
        evaluate ("location.href", [this] (const juce::var& v)
        {
            auto u = v.toString();
            if (u.startsWith ("http") && u != currentUrl)
            {
                currentUrl = u;
                if (! urlField.hasKeyboardFocus (true))
                {
                    urlField.setText (u, false);
                    urlEditedByUser = false;
                }
                markIn = markOut = -1.0;
            }
        });
    }
}

void BrowsePage::updateCaptureUi()
{
    const bool rec = capture != nullptr && capture->isRecording();
    liveRecBtn.setToggleState (rec, juce::dontSendNotification);
    liveRecBtn.setCaption (rec ? "STOP  " + formatTime (capture->getRecordedSeconds()) : "LIVE REC");

    juce::String info;
    if (! hasEmbeddedBrowser())
        info = "Browser not available in this build";
    else if (capture == nullptr || ! capture->isArmed())
        info = "Capture paused";
    else if (capture->getNumTaps() == 0)
        info = "Press play on a video to start listening";
    else if (capture->getContextState() == "suspended")
        info = "Click the video once to enable capture";
    else if (rec)
        info = "Recording what's playing";
    else
        info = "Listening - " + juce::String ((int) capture->getAvailableHindsight()) + "s in hindsight";
    captureInfo.setText (info, juce::dontSendNotification);
    captureInfo.setColour (juce::Label::textColourId, rec ? col::redHot : col::textDim);

    markInBtn.setButtonText (markIn >= 0 ? "IN " + formatTime (markIn, true) : "SET IN");
    markOutBtn.setButtonText (markOut >= 0 ? "OUT " + formatTime (markOut, true) : "SET OUT");
    markInBtn.setToggleState (markIn >= 0, juce::dontSendNotification);
    markOutBtn.setToggleState (markOut >= 0, juce::dontSendNotification);
    clearMarksBtn.setEnabled (markIn >= 0 || markOut >= 0);
    snagBtn.setCaption (markIn >= 0 || markOut >= 0 ? "HQ SNAG SECTION" : "HQ SNAG");

    const bool inRec = proc.isRecordingInput();
    recInputBtn.setToggleState (inRec, juce::dontSendNotification);
    setStyle (recInputBtn, inRec ? "red" : "gold");
    recInputBtn.setCaption (inRec ? "STOP  " + formatTime (proc.getInputRecordSeconds()) : "REC INPUT");

#if JUCE_WEB_BROWSER
    backBtn.setEnabled (true);
#else
    backBtn.setEnabled (false);
    fwdBtn.setEnabled (false);
    reloadBtn.setEnabled (false);
#endif
}

//==============================================================================
void BrowsePage::resized()
{
    auto r = getLocalBounds().reduced (14, 10);

    auto nav = r.removeFromTop (44);
    navArea = nav;
    nav = nav.reduced (10, 4);
    for (auto* b : { &backBtn, &fwdBtn, &reloadBtn, &homeBtn })
    {
        b->setBounds (nav.removeFromLeft (34).reduced (2));
    }
    nav.removeFromLeft (8);
    openExternalBtn.setBounds (nav.removeFromRight (34).reduced (2));
    nav.removeFromRight (6);
    goBtn.setBounds (nav.removeFromRight (64).reduced (0, 2));
    nav.removeFromRight (8);
    urlField.setBounds (nav.reduced (0, 2));

    r.removeFromTop (8);
    auto links = r.removeFromTop (26);
    const int modeW = (int) juce::GlyphArrangement::getStringWidth (theme::ui (11.5f, true), openModeBtn.getButtonText()) + 34;
    openModeBtn.setBounds (links.removeFromRight (modeW));
    links.removeFromRight (10);
    for (auto* b : quickLinks)
    {
        const int w = (int) juce::GlyphArrangement::getStringWidth (ui (11.5f, true), b->getButtonText()) + 28;
        if (w > links.getWidth()) { b->setVisible (false); continue; }
        b->setVisible (true);
        b->setBounds (links.removeFromLeft (w));
        links.removeFromLeft (6);
    }

    r.removeFromTop (10);
    captureBarArea = r.removeFromBottom (82);
    r.removeFromBottom (10);
    browserArea = r;

#if JUCE_WEB_BROWSER
    if (browser != nullptr)
        browser->setBounds (browserArea.reduced (1));
#endif

    // capture bar: four groups with small headings on top
    auto bar = captureBarArea.reduced (14, 8);
    bar.removeFromTop (18);   // headings
    auto row = bar.withSizeKeepingCentre (bar.getWidth(), 40);

    auto g1 = row.removeFromLeft (juce::jmin (300, row.getWidth() / 4 + 10));
    liveRecBtn.setBounds (g1.removeFromLeft (138));
    g1.removeFromLeft (10);
    auto infoCol = g1;
    meter.setBounds (infoCol.removeFromTop (18).withSizeKeepingCentre (infoCol.getWidth(), 6));
    captureInfo.setBounds (infoCol.withTrimmedLeft (-4));

    row.removeFromLeft (16);
    auto g2 = row.removeFromLeft (4 * 46 + 3 * 5);
    for (auto* b : grabButtons)
    {
        b->setBounds (g2.removeFromLeft (46).reduced (0, 6));
        g2.removeFromLeft (5);
    }

    row.removeFromLeft (18);
    auto g4 = row.removeFromRight (juce::jmin (270, row.getWidth() / 3));
    recInputBtn.setBounds (g4.removeFromRight (128));
    g4.removeFromRight (8);
    importBtn.setBounds (g4);

    row.removeFromRight (18);
    auto g3 = row;
    snagBtn.setBounds (g3.removeFromRight (juce::jmin (178, g3.getWidth() / 2)));
    g3.removeFromRight (8);
    clearMarksBtn.setBounds (g3.removeFromRight (26).reduced (0, 7));
    g3.removeFromRight (4);
    const int chipW = juce::jmax (40, (g3.getWidth() - 5) / 2);
    markInBtn.setBounds (g3.removeFromLeft (chipW).reduced (0, 6));
    g3.removeFromLeft (5);
    markOutBtn.setBounds (g3.removeFromLeft (chipW).reduced (0, 6));
}

void BrowsePage::paint (juce::Graphics& g)
{
    // address bar
    glassWindow (g, navArea.toFloat().reduced (3.0f, 1.0f), 9.0f, 0.8f);

    // browser frame
    auto frame = browserArea.toFloat().expanded (1.0f);
    glassWindow (g, frame, 8.0f, 0.0f);
    goldBorder (g, frame, 8.0f, 1.0f, 0.5f);

#if ! JUCE_WEB_BROWSER
    auto inner = browserArea.toFloat().reduced (40.0f);
    auto logo = inner.withSizeKeepingCentre (84.0f, 84.0f).translated (0.0f, -70.0f);
    drawLogoMark (g, logo);
    goldText (g, "Built-in browser unavailable in this build", inner.withSizeKeepingCentre (inner.getWidth(), 30.0f).translated (0, 10),
              display (20.0f), juce::Justification::centred);
    g.setColour (col::textDim);
    g.setFont (ui (13.0f));
    g.drawFittedText ("The embedded web browser uses WebKit (macOS) or WebView2 (Windows).\n"
                      "Paste a YouTube / SoundCloud / TikTok link in the address bar and hit HQ SNAG, "
                      "or use IMPORT and REC INPUT.",
                      inner.withSizeKeepingCentre (juce::jmin (620.0f, inner.getWidth()), 70.0f).translated (0, 58).toNearestInt(),
                      juce::Justification::centred, 3);
#endif

    // capture bar
    auto bar = captureBarArea.toFloat().reduced (3.0f, 1.0f);
    glassWindow (g, bar, 10.0f, 0.8f);

    auto heads = captureBarArea.reduced (14, 8).removeFromTop (16).toFloat();
    auto place = [&] (juce::Component& c, const juce::String& text)
    {
        sectionLabel (g, text, juce::Rectangle<float> ((float) c.getX(), heads.getY(), 220.0f, heads.getHeight()));
    };
    place (liveRecBtn, "Capture");
    place (*grabButtons.getFirst(), "Hindsight - grab last");
    place (markInBtn, "HQ Snag (yt-dlp)");
    place (importBtn, "More sources");

    // divider lines
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    for (auto* c : { static_cast<juce::Component*> (grabButtons.getFirst()), static_cast<juce::Component*> (&markInBtn), static_cast<juce::Component*> (&importBtn) })
        g.drawVerticalLine (c->getX() - 10, bar.getY() + 12.0f, bar.getBottom() - 12.0f);
}

void BrowsePage::paintOverChildren (juce::Graphics& g)
{
    // neon REC ring around the browser while recording
    if (capture != nullptr && capture->isRecording())
    {
        juce::Path p;
        p.addRoundedRectangle (browserArea.toFloat().expanded (2.0f), 8.0f);
        juce::Path s;
        juce::PathStrokeType (2.0f).createStrokedPath (s, p);
        neonGlow (g, s, col::red, 12.0f, 0.9f);
        g.setColour (col::red);
        g.strokePath (p, juce::PathStrokeType (1.5f));

        const bool blink = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
        auto badge = juce::Rectangle<float> ((float) browserArea.getRight() - 96.0f, (float) browserArea.getY() + 10.0f, 84.0f, 24.0f);
        g.setColour (col::bg0.withAlpha (0.85f));
        g.fillRoundedRectangle (badge, 12.0f);
        g.setColour (col::red);
        g.drawRoundedRectangle (badge, 12.0f, 1.0f);
        if (blink) g.fillEllipse (badge.withWidth (24.0f).withSizeKeepingCentre (8.0f, 8.0f).translated (6.0f, 0.0f));
        g.setFont (ui (11.0f, true));
        g.drawText ("REC " + formatTime (capture->getRecordedSeconds()), badge.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
    }
}

} // namespace snag
