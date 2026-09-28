#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace snag
{

//==============================================================================
/** Records everything a process (and its child processes) plays, straight from the operating
    system - Windows 10 build 20348+ / Windows 11 "process loopback" capture.

    The built-in browser (WebView2) plays page audio in its own processes, so capturing those
    hears every site, including ones whose players a web page script is not allowed to listen to
    (sound files served from another domain). Not available on other systems: isSupported() is
    false there and the page-script capture is used instead. */
class ProcessAudioCapture
{
public:
    ProcessAudioCapture();
    ~ProcessAudioCapture();

    static bool isSupported();

    /** Interleaved stereo float frames at getSampleRate(), from the capture thread.
        Silence is filled in, so the stream keeps pace with the clock even when nothing plays. */
    using Callback = std::function<void (const float* interleaved, int numFrames)>;

    /** Starts capturing `processId` and its children. Returns false (see getLastError()) if the
        system can't do it; blocks for at most a few seconds. */
    bool start (juce::uint32 processId, Callback callback);
    void stop();

    bool isRunning() const;
    double getSampleRate() const noexcept   { return 48000.0; }
    juce::String getLastError() const;

    /** The WebView2 browser process that belongs to our built-in browser (found by its user data
        folder), or 0. */
    static juce::uint32 findWebViewBrowserProcess (const juce::File& userDataFolder);

    static juce::uint32 currentProcessId();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE (ProcessAudioCapture)
};

} // namespace snag
