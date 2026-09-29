#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/AudioFileIO.h"
#include "core/EditOps.h"

using namespace snag;

//==============================================================================
juce::AudioProcessor::BusesProperties SnaggerProcessor::makeBuses()
{
    auto buses = BusesProperties()
                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    // An optional input lets you record audio routed into the plug-in (REC INPUT).
    // It's on by default in the standalone app; in DAWs it's an optional side input.
    // AU instruments stay input-less for maximum host compatibility.
    const auto wrapper = juce::PluginHostType::getPluginLoadedAs();
    if (wrapper != juce::AudioProcessor::wrapperType_AudioUnit
        && wrapper != juce::AudioProcessor::wrapperType_AudioUnitv3)
    {
        buses = buses.withInput ("Input", juce::AudioChannelSet::stereo(),
                                 wrapper == juce::AudioProcessor::wrapperType_Standalone);
    }
    return buses;
}

SnaggerProcessor::SnaggerProcessor()
    : AudioProcessor (makeBuses())
{
    for (auto& g : layerGains)
        g = 1.0f;

    writerThread.startThread();
    startTimer (1000);
}

SnaggerProcessor::~SnaggerProcessor()
{
    aliveFlag->store (false);
    stopTimer();
    jobs.cancelAll();
    stopInputRecording();
    writerThread.stopThread (2000);
}

//==============================================================================
bool SnaggerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    if (layouts.inputBuses.size() > 0)
    {
        const auto in = layouts.getMainInputChannelSet();
        if (! in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

void SnaggerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    for (auto& v : voices)
    {
        v.active = false;
        v.state = nullptr;
    }
    pRunning = false;
    fxChain.prepare (currentSampleRate);
    fxBus.setSize (2, juce::jmax (1024, samplesPerBlock * 2), false, true, false);
    fxWasOn = false;
}

void SnaggerProcessor::releaseResources() {}

//==============================================================================
void SnaggerProcessor::setSamplerClip (Clip::Ptr clip, int selStart, int selEnd)
{
    samplerClip = clip;
    SamplerState::Ptr s;

    if (clip != nullptr && clip->audio != nullptr)
    {
        s = new SamplerState();
        s->audio = clip->audio;
        s->bounds = clip->sliceBoundaries();
        const int n = clip->audio->getNumSamples();
        s->selStart = juce::jlimit (0, n, selStart);
        s->selEnd   = selEnd < 0 ? n : juce::jlimit (s->selStart, n, selEnd);
        if (s->selEnd - s->selStart < 64)
        {
            s->selStart = 0;
            s->selEnd = n;
        }
        s->pads = clip->pads;
        s->fx = clip->fx;
        s->bpm = clip->bpm;
    }
    samplerShared.publish (s);
}

void SnaggerProcessor::sendUiNote (int note, float velocity)
{
    int s1, n1, s2, n2;
    uiNoteFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
        uiNotes[(size_t) s1] = { note, velocity };
    else if (n2 > 0)
        uiNotes[(size_t) s2] = { note, velocity };
    uiNoteFifo.finishedWrite (n1 + n2);
}

//==============================================================================
void SnaggerProcessor::preview (std::vector<AudioData::Ptr> layers, int start, int end, bool loop, bool withFx)
{
    layers.erase (std::remove (layers.begin(), layers.end(), nullptr), layers.end());
    if (layers.empty())
    {
        stopPreview();
        return;
    }

    PreviewState::Ptr p (new PreviewState());
    const int n = layers.front()->getNumSamples();
    p->layers = std::move (layers);
    p->start = juce::jlimit (0, n, start);
    p->end   = end < 0 ? n : juce::jlimit (p->start, n, end);
    if (p->end - p->start < 32)
    {
        p->start = 0;
        p->end = n;
    }
    p->loop = loop;
    p->fx = withFx;
    p->serial = ++previewSerial;
    previewPlaying = true;
    previewPos = (double) p->start;
    previewShared.publish (p);
}

void SnaggerProcessor::previewClip (const Clip& clip, int start, int end, bool loop, bool withFx)
{
    preview ({ clip.audio }, start, end, loop, withFx);
}

void SnaggerProcessor::stopPreview()
{
    PreviewState::Ptr p (new PreviewState());
    p->stop = true;
    p->serial = ++previewSerial;
    previewShared.publish (p);
    previewPlaying = false;
}

AudioData* SnaggerProcessor::getPreviewSource() const
{
    auto p = previewShared.getPublished();
    if (p == nullptr || p->stop || p->layers.empty())
        return nullptr;
    return p->layers.front().get();
}

//==============================================================================
void SnaggerProcessor::noteOn (int note, float velocity, SamplerState::Ptr state)
{
    if (state == nullptr || state->audio == nullptr)
        return;

    const int n = state->audio->getNumSamples();
    int start = 0, end = n;
    double pitch = 1.0;
    int sliceIndex = -1;
    PadParams params;

    if (midiMode.load() == chops)
    {
        const int idx = note - rootNote.load();
        const int numSlices = (int) state->bounds.size() - 1;
        if (idx < 0 || idx >= numSlices)
            return;
        start = state->bounds[(size_t) idx];
        end   = state->bounds[(size_t) idx + 1];
        sliceIndex = idx;
        if (idx < (int) state->pads.size())
            params = state->pads[(size_t) idx];
    }
    else
    {
        start = state->selStart;
        end   = state->selEnd;
        pitch = std::pow (2.0, (note - rootNote.load()) / 12.0);
        params.attackMs = 1.5f;
        params.releaseMs = 40.0f;
    }

    // Retrigger the same note, otherwise take a free voice or steal the oldest.
    Voice* v = nullptr;
    for (auto& cand : voices)
        if (cand.active && cand.note == note) { v = &cand; break; }
    if (v == nullptr)
        for (auto& cand : voices)
            if (! cand.active) { v = &cand; break; }
    if (v == nullptr)
    {
        v = &voices[0];
        for (auto& cand : voices)
            if (cand.age < v->age) v = &cand;
    }

    v->state = state;
    v->active = true;
    v->note = note;
    v->pv.begin (*state->audio, start, end, params, currentSampleRate, pitch, juce::jlimit (0.0f, 1.0f, velocity));
    v->age = ++voiceAge;

    if (sliceIndex >= 0)
    {
        lastTriggeredSlice = sliceIndex;
        ++triggerCounter;
    }
}

void SnaggerProcessor::noteOff (int note)
{
    if (oneShot.load() && midiMode.load() == chops)
        return;

    for (auto& v : voices)
        if (v.active && v.note == note)
            v.pv.noteOff();
}

void SnaggerProcessor::renderVoices (juce::AudioBuffer<float>& out, int startSample, int num)
{
    const float master = masterGain.load();
    float* L = out.getWritePointer (0);
    float* R = out.getWritePointer (1);
    for (auto& v : voices)
    {
        if (! v.active || v.state == nullptr)
            continue;
        for (int i = 0; i < num; ++i)
        {
            float l = 0, r = 0;
            if (! v.pv.next (l, r))
            {
                v.active = false;
                v.state = nullptr;   // SamplerState is kept alive by the release pool, never freed here
                break;
            }
            L[startSample + i] += l * master;
            R[startSample + i] += r * master;
        }
    }
}

void SnaggerProcessor::renderPreview (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet, int num)
{
    auto latest = previewShared.read();
    if (latest != nullptr && latest->serial != activePreviewSerial)
    {
        activePreviewSerial = latest->serial;
        activePreview = latest;
        if (latest->stop)
        {
            pRunning = false;
        }
        else
        {
            pRunning = true;
            pPos = latest->start;
            pEnv = 0.0f;
        }
    }

    if (! pRunning || activePreview == nullptr || activePreview->layers.empty())
    {
        previewPlaying = false;
        return;
    }

    auto& p = *activePreview;
    auto& out = p.fx ? wet : dry;
    const double rate = p.layers.front()->sampleRate / currentSampleRate;
    const float attack = 1.0f / (float) juce::jmax (1.0, currentSampleRate * 0.003);
    const int fadeLen = (int) juce::jmax (8.0, currentSampleRate * 0.005);
    const int outCh = out.getNumChannels();

    float gains[8];
    for (size_t l = 0; l < 8; ++l)
        gains[l] = layerGains[l].load();

    for (int i = 0; i < num; ++i)
    {
        if (pPos >= p.end)
        {
            if (p.loop)
                pPos = p.start + (pPos - p.end);
            else
            {
                pRunning = false;
                break;
            }
        }

        pEnv = juce::jmin (1.0f, pEnv + attack);
        const double remaining = (p.end - pPos) / rate;
        const float tail = (! p.loop && remaining < fadeLen) ? (float) (remaining / fadeLen) : 1.0f;
        const float amp = pEnv * tail * masterGain.load();

        float l = 0, r = 0;
        for (size_t li = 0; li < p.layers.size() && li < 8; ++li)
        {
            const auto& b = p.layers[li]->buffer;
            const int n = b.getNumSamples();
            if (pPos >= n || gains[li] <= 0.0f)
                continue;
            l += hermiteAt (b.getReadPointer (0), n, pPos) * gains[li];
            r += hermiteAt (b.getReadPointer (juce::jmin (1, b.getNumChannels() - 1)), n, pPos) * gains[li];
        }

        out.addSample (0, i, l * amp);
        if (outCh > 1)
            out.addSample (1, i, r * amp);
        pPos += rate;
    }

    previewPlaying = pRunning;
    previewPos = pRunning ? pPos : -1.0;
}

//==============================================================================
void SnaggerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    // Host tempo
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    // Record whatever comes in on the input bus (standalone: your audio interface).
    if (numIn > 0)
    {
        float pk = 0;
        for (int c = 0; c < numIn; ++c)
            pk = juce::jmax (pk, buffer.getMagnitude (c, 0, numSamples));
        inputLevel = juce::jmax (pk, inputLevel.load() * 0.9f);

        const juce::ScopedLock sl (writerLock);
        if (auto* w = activeWriter.load())
        {
            const float* chans[2] = { buffer.getReadPointer (0), buffer.getReadPointer (juce::jmin (1, numIn - 1)) };
            w->write (chans, numSamples);
            recordedSamples += numSamples;
        }
    }

    buffer.clear();   // instrument: never pass input through (avoids feedback)
    if (fxBus.getNumSamples() < numSamples)
        fxBus.setSize (2, numSamples, false, false, true);   // (only if the host sends a bigger block than promised)
    fxBus.clear (0, numSamples);

    auto state = samplerShared.read();

    // UI pad notes
    {
        int s1, n1, s2, n2;
        uiNoteFifo.prepareToRead (uiNoteFifo.getNumReady(), s1, n1, s2, n2);
        auto handle = [&] (int idx)
        {
            auto [note, vel] = uiNotes[(size_t) idx];
            if (vel > 0) noteOn (note, vel, state); else noteOff (note);
        };
        for (int i = 0; i < n1; ++i) handle (s1 + i);
        for (int i = 0; i < n2; ++i) handle (s2 + i);
        uiNoteFifo.finishedRead (n1 + n2);
    }

    // MIDI, sample-accurate
    int pos = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int t = juce::jlimit (0, numSamples, meta.samplePosition);
        if (t > pos)
        {
            renderVoices (fxBus, pos, t - pos);
            pos = t;
        }
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity(), state);
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& v : voices) v.pv.noteOff();
    }
    if (pos < numSamples)
        renderVoices (fxBus, pos, numSamples - pos);

    renderPreview (buffer, fxBus, numSamples);

    // FX rack (pads + STUDIO playback), synced to the host's tempo, else the sample's
    const bool fxOn = state != nullptr && state->fx.anyOn();
    if (fxOn)
    {
        if (! fxWasOn)
            fxChain.reset();
        const double bpm = hostBpm.load() > 0 ? hostBpm.load() : (state->bpm > 0 ? state->bpm : 120.0);
        fxChain.process (fxBus.getWritePointer (0), fxBus.getWritePointer (1), numSamples, state->fx, bpm);
    }
    fxWasOn = fxOn;
    if (buffer.getNumChannels() > 1)
    {
        buffer.addFrom (0, 0, fxBus, 0, 0, numSamples);
        buffer.addFrom (1, 0, fxBus, 1, 0, numSamples);
    }
    else if (buffer.getNumChannels() == 1)
    {
        buffer.addFrom (0, 0, fxBus, 0, 0, numSamples, 0.5f);
        buffer.addFrom (0, 0, fxBus, 1, 0, numSamples, 0.5f);
    }

    float pk = 0;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        pk = juce::jmax (pk, buffer.getMagnitude (c, 0, numSamples));
    outputLevel = juce::jmax (pk, outputLevel.load() * 0.9f);
}

//==============================================================================
bool SnaggerProcessor::hasAudioInput() const
{
    return getTotalNumInputChannels() > 0;
}

bool SnaggerProcessor::startInputRecording (juce::String& error)
{
    if (! hasAudioInput())
    {
       #if defined (JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
        if (wrapperType == wrapperType_Standalone)
            error = "No audio input. Open Audio Settings and choose an input device.";
        else
       #endif
            error = "This track isn't sending audio into Sample Snagger. Enable the plug-in's side input / input bus in your DAW, or use the standalone app.";
        return false;
    }

    stopInputRecording();

    recordingFile = audioio::uniqueFile (paths::tempDir(), "Input Recording " + juce::Time::getCurrentTime().formatted ("%H-%M-%S"));
    recordingFile.deleteFile();

    std::unique_ptr<juce::OutputStream> stream (recordingFile.createOutputStream());
    if (stream == nullptr)
    {
        error = "Can't write the recording file";
        return false;
    }

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                            .withSampleRate (currentSampleRate)
                            .withNumChannels (2)
                            .withBitsPerSample (32)
                            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

    if (auto writer = wav.createWriterFor (stream, options))
    {
        threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 1 << 15);
        recordedSamples = 0;
        const juce::ScopedLock sl (writerLock);
        activeWriter = threadedWriter.get();
        return true;
    }

    error = "Can't create the recording";
    return false;
}

Clip::Ptr SnaggerProcessor::stopInputRecording()
{
    {
        const juce::ScopedLock sl (writerLock);
        activeWriter = nullptr;
    }
    if (threadedWriter == nullptr)
        return nullptr;

    threadedWriter.reset();   // flushes + closes the file

    auto loaded = audioio::loadFile (recordingFile, {});
    recordingFile.deleteFile();
    if (loaded.audio == nullptr || loaded.audio->getNumSamples() < 64)
        return nullptr;

    Clip::Ptr c (new Clip());
    c->name = "Recording " + juce::Time::getCurrentTime().formatted ("%H:%M:%S");
    c->kind = "Recording";
    c->origin = "Audio input";
    c->audio = loaded.audio;
    return c;
}

double SnaggerProcessor::getInputRecordSeconds() const
{
    return (double) recordedSamples.load() / currentSampleRate;
}

//==============================================================================
void SnaggerProcessor::timerCallback()
{
    samplerShared.collectGarbage();
    previewShared.collectGarbage();
}

//==============================================================================
void SnaggerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("SAMPLESNAGGER");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("midiMode", midiMode.load(), nullptr);
    root.setProperty ("oneShot", oneShot.load(), nullptr);
    root.setProperty ("rootNote", rootNote.load(), nullptr);
    root.setProperty ("gain", masterGain.load(), nullptr);
    root.appendChild (session.toValueTree(), nullptr);
    root.appendChild (uiState.createCopy(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void SnaggerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("SAMPLESNAGGER"))
        return;

    midiMode   = (int) root.getProperty ("midiMode", 0);
    oneShot    = (bool) root.getProperty ("oneShot", true);
    rootNote   = (int) root.getProperty ("rootNote", 60);
    masterGain = (float) root.getProperty ("gain", 1.0f);

    auto alive = aliveFlag;
    auto restore = [this, root, alive]
    {
        if (! alive->load())
            return;
        session.restore (root.getChildWithName ("SESSION"));
        auto ui = root.getChildWithName ("UI");
        if (ui.isValid())
            uiState.copyPropertiesAndChildrenFrom (ui, nullptr);
        if (auto sel = session.getSelected())
            setSamplerClip (sel);
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        restore();
    else
        juce::MessageManager::callAsync (restore);
}

//==============================================================================
juce::AudioProcessorEditor* SnaggerProcessor::createEditor()
{
    return new SnaggerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SnaggerProcessor();
}
