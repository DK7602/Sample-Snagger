#include "WebCapture.h"
#include "SnaggerBinaryData.h"

namespace snag
{

WebCapture::WebCapture (Evaluator e) : evaluate (std::move (e)) {}

WebCapture::~WebCapture()
{
    stopTimer();
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
        stopTimer();

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
        sampleRate = sr;
        ring.setSize (2, (int) (hindsightSeconds * sr));
        ring.clear();
        ringWrite = ringFilled = 0;
        recL.clear(); recR.clear();
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
        recL.reserve (recL.size() + (size_t) numFrames);
        recR.reserve (recR.size() + (size_t) numFrames);
        for (int i = 0; i < numFrames; ++i)
        {
            recL.push_back (interleaved[2 * i]);
            recR.push_back (interleaved[2 * i + 1]);
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

    level        = (float) (double) obj.getProperty ("lvl", 0.0);
    mediaPlaying = (bool) obj.getProperty ("playing", false);
    numTaps      = (int) obj.getProperty ("taps", 0);
    contextState = obj.getProperty ("st", "").toString();

    const int n = (int) obj.getProperty ("n", 0);
    const double sr = (double) obj.getProperty ("sr", 0.0);
    const auto b64 = obj.getProperty ("b64", "").toString();

    if (n > 0 && sr > 0 && b64.isNotEmpty())
    {
        juce::MemoryOutputStream mo;
        if (juce::Base64::convertFromBase64 (mo, b64) && (int) mo.getDataSize() >= n * 2 * (int) sizeof (float))
        {
            // copy to guarantee alignment
            std::vector<float> frames ((size_t) n * 2);
            std::memcpy (frames.data(), mo.getData(), frames.size() * sizeof (float));
            pushFrames (frames.data(), n, sr);
        }
    }
}

void WebCapture::timerCallback()
{
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
