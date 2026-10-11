#include "WebCapture.h"
#include "EditOps.h"
#include "SnaggerBinaryData.h"

namespace snag
{

WebCapture::WebCapture (Evaluator e) : evaluate (std::move (e)) {}

WebCapture::~WebCapture()
{
    stopTimer();
    processCapture.reset();
}

void WebCapture::setProcessSource (std::function<juce::uint32()> finder)
{
    findProcess = std::move (finder);
    processRetryAt = 0;
}

void WebCapture::pollProcessAudio()
{
    if (! findProcess || ! ProcessAudioCapture::isSupported())
        return;

    if (! (armed || recording))
    {
        processCapture.reset();   // nothing to listen for
        return;
    }

    if (processCapture == nullptr || ! processCapture->isRunning())
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (now < processRetryAt)
            return;
        processRetryAt = now + 4000;

        const auto pid = findProcess();
        if (pid == 0)
            return;

        auto cap = std::make_unique<ProcessAudioCapture>();
        const bool ok = cap->start (pid, [this] (const float* frames, int n)
        {
            const std::lock_guard<std::mutex> lock (pendingLock);
            if (pending.size() < (size_t) (48000 * 2 * 20))   // never hold more than 20 s
                pending.insert (pending.end(), frames, frames + (size_t) n * 2);
        });
        if (ok)
            processCapture = std::move (cap);
        else
        {
            DBG ("Browser process audio unavailable: " << cap->getLastError());
            processRetryAt = now + 30000;   // e.g. Windows 10: use the page script instead, check again later
            return;
        }
    }

    std::vector<float> take;
    {
        const std::lock_guard<std::mutex> lock (pendingLock);
        take.swap (pending);
    }
    if (take.empty())
        return;

    float peak = 0.0f;
    for (auto v : take)
        peak = juce::jmax (peak, std::abs (v));
    level = juce::jmax (peak, level * 0.7f);
    pushFrames (take.data(), (int) (take.size() / 2), processCapture->getSampleRate());
}

juce::String WebCapture::getTapScript()
{
    return juce::String::fromUTF8 (SnaggerBinary::webtap_js, SnaggerBinary::webtap_jsSize);
}

void WebCapture::setArmed (bool shouldListen)
{
    armed = shouldListen;

    if (evaluate && injected)
        evaluate ("window.__snag ? window.__snag.arm(" + juce::String (armed ? "true" : "false") + ") : false", nullptr);

    if (armed || recording)
        startTimerHz (8);
    else
    {
        stopTimer();
        processCapture.reset();
    }

    if (onStateChanged) onStateChanged();
}

void WebCapture::startRecording()
{
    recL.clear();
    recR.clear();
    recording = true;
    if (! armed)
        setArmed (true);
    startTimerHz (8);
    if (onStateChanged) onStateChanged();
}

AudioData::Ptr WebCapture::stopRecording()
{
    if (! recording)
        return nullptr;

    recording = false;
    if (! armed)
        stopTimer();

    if (onStateChanged) onStateChanged();

    if (recL.empty() || sampleRate <= 0)
        return nullptr;

    juce::AudioBuffer<float> b (2, (int) recL.size());
    b.copyFrom (0, 0, recL.data(), (int) recL.size());
    b.copyFrom (1, 0, recR.data(), (int) recR.size());
    recL.clear(); recL.shrink_to_fit();
    recR.clear(); recR.shrink_to_fit();
    return AudioData::make (std::move (b), sampleRate);
}

double WebCapture::getRecordedSeconds() const
{
    return sampleRate > 0 ? (double) recL.size() / sampleRate : 0.0;
}

double WebCapture::getAvailableHindsight() const
{
    return sampleRate > 0 ? (double) ringFilled / sampleRate : 0.0;
}

AudioData::Ptr WebCapture::grabLast (double seconds) const
{
    if (sampleRate <= 0 || ringFilled <= 0)
        return nullptr;

    const int size = ring.getNumSamples();
    const int n = juce::jmin (ringFilled, (int) (seconds * sampleRate));
    if (n <= 0)
        return nullptr;

    juce::AudioBuffer<float> b (2, n);
    const int start = (ringWrite - n + size) % size;
    for (int c = 0; c < 2; ++c)
    {
        const int first = juce::jmin (n, size - start);
        b.copyFrom (c, 0, ring, c, start, first);
        if (first < n)
            b.copyFrom (c, first, ring, c, 0, n - first);
    }
    return AudioData::make (std::move (b), sampleRate);
}

void WebCapture::pushFrames (const float* interleaved, int numFrames, double sr)
{
    if (numFrames <= 0 || sr <= 0)
        return;

    if (std::abs (sr - sampleRate) > 0.5 || ring.getNumSamples() == 0)
    {
        // Another page, or the switch between the page's own audio and the browser's output, can
        // run at another rate. Hindsight starts over, but a LIVE REC that is running keeps what it
        // has: it is converted to the new rate (it used to be thrown away while the button kept counting).
        if (recording && ! recL.empty() && sampleRate > 0)
        {
            juce::AudioBuffer<float> soFar (2, (int) recL.size());
            soFar.copyFrom (0, 0, recL.data(), (int) recL.size());
            soFar.copyFrom (1, 0, recR.data(), (int) recR.size());
            const auto converted = edit::resample (*AudioData::make (std::move (soFar), sampleRate), sr);
            const int n = converted->getNumSamples();
            recL.assign (converted->buffer.getReadPointer (0), converted->buffer.getReadPointer (0) + n);
            recR.assign (converted->buffer.getReadPointer (1), converted->buffer.getReadPointer (1) + n);
        }
        else
        {
            recL.clear();
            recR.clear();
        }

        sampleRate = sr;
        ring.setSize (2, (int) (hindsightSeconds * sr));
        ring.clear();
        ringWrite = ringFilled = 0;
    }

    const int size = ring.getNumSamples();
    auto* l = ring.getWritePointer (0);
    auto* r = ring.getWritePointer (1);
    for (int i = 0; i < numFrames; ++i)
    {
        l[ringWrite] = interleaved[2 * i];
        r[ringWrite] = interleaved[2 * i + 1];
        ringWrite = (ringWrite + 1) % size;
    }
    ringFilled = juce::jmin (size, ringFilled + numFrames);

    if (recording && (double) recL.size() < maxRecordSeconds * sr)
    {
        // (resize grows the storage in big steps; reserving the exact size on every poll made a
        //  copy of the whole recording each time, which stalled the window on long takes)
        const size_t old = recL.size();
        recL.resize (old + (size_t) numFrames);
        recR.resize (old + (size_t) numFrames);
        for (int i = 0; i < numFrames; ++i)
        {
            recL[old + (size_t) i] = interleaved[2 * i];
            recR[old + (size_t) i] = interleaved[2 * i + 1];
        }
    }
}

void WebCapture::ingest (const juce::var& result)
{
    juce::var obj = result;
    if (result.isString())
        obj = juce::JSON::parse (result.toString());

    if (! obj.isObject())
        return;

    // Everything in here comes from the web page, which can be anything: nothing is taken on trust.
    const bool viaProcess = isUsingProcessAudio();
    if (! viaProcess)
    {
        const double lvl = (double) obj.getProperty ("lvl", 0.0);
        level = std::isfinite (lvl) ? (float) juce::jlimit (0.0, 4.0, lvl) : 0.0f;
    }
    mediaPlaying = (bool) obj.getProperty ("playing", false);
    numTaps      = (int) obj.getProperty ("taps", 0);
    blocked      = (int) obj.getProperty ("blocked", 0);
    contextState = obj.getProperty ("st", "").toString();

    if (viaProcess)
        return;   // the browser's own audio is recorded directly - the page's copy isn't needed

    const double claimed = (double) obj.getProperty ("n", 0.0);
    const double sr = (double) obj.getProperty ("sr", 0.0);
    const auto b64 = obj.getProperty ("b64", "").toString();

    // A sane rate, and no more frames than the tap ever queues (10 s; a huge count used to overflow
    // the size check below and read far past the decoded data).
    if (sr >= 8000.0 && sr <= 384000.0 && claimed >= 1.0 && claimed <= sr * 30.0 && b64.isNotEmpty())
    {
        const auto n = (size_t) claimed;
        juce::MemoryOutputStream mo;
        if (juce::Base64::convertFromBase64 (mo, b64) && mo.getDataSize() / (2 * sizeof (float)) >= n)
        {
            // copy to guarantee alignment
            std::vector<float> frames (n * 2);
            std::memcpy (frames.data(), mo.getData(), frames.size() * sizeof (float));
            for (auto& v : frames)
                if (! std::isfinite (v))
                    v = 0.0f;
            pushFrames (frames.data(), (int) n, sr);
        }
    }
}

void WebCapture::timerCallback()
{
    pollProcessAudio();
    poll();
    if (onStateChanged) onStateChanged();
}

void WebCapture::poll()
{
    if (! evaluate)
        return;

    if (waiting)
    {
        // The page never answered (navigation, frozen tab...). Try again after a while.
        if (++pollsWithoutAnswer > 40) { waiting = false; injected = false; }
        return;
    }

    juce::WeakReference<WebCapture> weak (this);

    if (! injected)
    {
        waiting = true;
        pollsWithoutAnswer = 0;
        evaluate (getTapScript(), [weak] (const juce::var&)
        {
            if (auto* self = weak.get())
            {
                self->waiting = false;
                self->injected = true;
                self->evaluate ("window.__snag ? window.__snag.arm(" + juce::String (self->armed ? "true" : "false") + ") : false", nullptr);
            }
        });
        return;
    }

    waiting = true;
    pollsWithoutAnswer = 0;
    evaluate ("window.__snag ? window.__snag.drain() : '__NOINIT__'", [weak] (const juce::var& v)
    {
        if (auto* self = weak.get())
        {
            self->waiting = false;
            if (v.isVoid() || v.isUndefined() || v.toString() == "__NOINIT__")
                self->injected = false;   // new document: inject again on the next tick
            else
                self->ingest (v);
        }
    });
}

} // namespace snag
