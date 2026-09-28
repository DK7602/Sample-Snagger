// Sample Snagger - headless test runner + UI screenshot renderer.
//
//   SnaggerTests                 run unit tests
//   SnaggerTests --shots DIR     also render every screen to PNGs in DIR
//   SnaggerTests --network       also test the helper-tool downloads (needs internet)
//   SnaggerTests --ai            also download the AI models and run real stem separation

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Actions.h"
#include "../Source/core/EditOps.h"
#include "../Source/core/QuickSplit.h"
#include "../Source/core/AudioFileIO.h"
#include "../Source/core/WebCapture.h"
#include "../Source/core/AiStems.h"
#include "../Source/core/ProcessAudioCapture.h"
#include <iostream>
#include <thread>
#include <atomic>

using namespace snag;

static int failures = 0, passes = 0;

#define CHECK(cond, msg) do { if (cond) { ++passes; std::cout << "  ok    " << msg << "\n"; } \
                              else { ++failures; std::cout << "  FAIL  " << msg << "   (" #cond ")\n"; } } while (0)

static void pump (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

//==============================================================================
// Synthetic material
static AudioData::Ptr makeClicks (double sr, double seconds, double interval)
{
    juce::AudioBuffer<float> b (2, (int) (sr * seconds));
    b.clear();
    for (double t = 0.25; t < seconds - 0.1; t += interval)
    {
        const int s0 = (int) (t * sr);
        for (int i = 0; i < (int) (0.03 * sr) && s0 + i < b.getNumSamples(); ++i)
        {
            const float env = std::exp (-(float) i / (float) (0.005 * sr));
            const float v = env * std::sin ((float) i * 2.0f * juce::MathConstants<float>::pi * 180.0f / (float) sr)
                          + env * 0.5f * (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f);
            b.setSample (0, s0 + i, v * 0.8f);
            b.setSample (1, s0 + i, v * 0.8f);
        }
    }
    return AudioData::make (std::move (b), sr);
}

struct DemoMix
{
    AudioData::Ptr mix, vocal, drums, bass, keys;
};

/** A little 8-bar "soul loop": centred vocal line, wide keys, centred drums + bass. */
static DemoMix makeDemoMix (double sr, double seconds, double bpm = 92.0)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> voc (2, n), dr (2, n), bs (2, n), ky (2, n);
    for (auto* b : { &voc, &dr, &bs, &ky }) b->clear();
    auto& rnd = juce::Random::getSystemRandom();
    const double beat = 60.0 / bpm;
    const float twoPi = juce::MathConstants<float>::twoPi;

    // chords: Am7 - Dm9 - G13 - Cmaj7 (wide stereo, different voicing L / R)
    const float chords[4][4] = { { 220.0f, 261.6f, 329.6f, 392.0f }, { 174.6f, 220.0f, 261.6f, 329.6f },
                                 { 196.0f, 246.9f, 293.7f, 349.2f }, { 164.8f, 196.0f, 246.9f, 329.6f } };
    const float bassNotes[4] = { 55.0f, 73.4f, 49.0f, 65.4f };
    const float melody[16] = { 659.3f, 587.3f, 523.3f, 587.3f, 659.3f, 784.0f, 659.3f, 0.0f,
                               587.3f, 523.3f, 493.9f, 523.3f, 587.3f, 523.3f, 440.0f, 0.0f };

    double phaseV = 0, phaseB = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const int bar = (int) (t / (4 * beat)) % 4;
        const double inBeat = std::fmod (t, beat);
        const int beatIdx = (int) (t / beat);

        // keys: soft electric-piano-ish, alternate partials hard left/right
        float kl = 0, kr = 0;
        for (int k = 0; k < 4; ++k)
        {
            const float f = chords[bar][k];
            const float env = 0.55f + 0.45f * std::exp (-(float) std::fmod (t, 2 * beat) * 2.0f);
            const float v = std::sin (twoPi * f * (float) t) * 0.06f * env + std::sin (twoPi * f * 2.0f * (float) t) * 0.015f * env;
            if (k % 2 == 0) kl += v * 1.0f, kr += v * 0.15f; else kr += v * 1.0f, kl += v * 0.15f;
        }
        ky.setSample (0, i, kl);
        ky.setSample (1, i, kr);

        // bass (centre)
        phaseB += bassNotes[bar] / sr;
        const float bv = (float) (std::sin (twoPi * phaseB) * 0.28 * (0.6 + 0.4 * std::exp (-inBeat * 3.0)));
        bs.setSample (0, i, bv);
        bs.setSample (1, i, bv);

        // vocal-ish lead (centre): band-limited saw with vibrato and a gentle envelope
        const int step = (int) (t / (beat / 2)) % 16;
        const float f0 = melody[step];
        float vv = 0;
        if (f0 > 0)
        {
            const double noteT = std::fmod (t, beat / 2);
            const float vib = 1.0f + 0.006f * std::sin (twoPi * 5.5f * (float) t);
            phaseV += f0 * vib / sr;
            for (int h = 1; h <= 8; ++h)
                vv += (float) std::sin (twoPi * phaseV * h) / (float) h * (h == 3 || h == 4 ? 1.6f : 1.0f);
            vv *= 0.07f * (float) juce::jmin (1.0, noteT * 30.0) * (float) (0.7 + 0.3 * std::exp (-noteT * 4.0));
        }
        voc.setSample (0, i, vv);
        voc.setSample (1, i, vv);

        // drums (centre): kick on 1 & 3, snare on 2 & 4, hats on 8ths
        float d = 0;
        if (beatIdx % 2 == 0 && inBeat < 0.25)
            d += (float) (std::sin (twoPi * (50.0 + 90.0 * std::exp (-inBeat * 30.0)) * inBeat) * std::exp (-inBeat * 12.0) * 0.7);
        if (beatIdx % 2 == 1 && inBeat < 0.2)
            d += (rnd.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-inBeat * 22.0) * 0.35f
               + (float) (std::sin (twoPi * 190.0 * inBeat) * std::exp (-inBeat * 30.0) * 0.25);
        const double inHalf = std::fmod (t, beat / 2);
        if (inHalf < 0.05)
            d += (rnd.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-inHalf * 90.0) * 0.12f;
        dr.setSample (0, i, d);
        dr.setSample (1, i, d);
    }

    juce::AudioBuffer<float> mix (2, n);
    mix.clear();
    for (auto* b : { &voc, &dr, &bs, &ky })
        for (int c = 0; c < 2; ++c)
            mix.addFrom (c, 0, *b, c, 0, n);

    DemoMix m;
    m.mix   = AudioData::make (std::move (mix), sr);
    m.vocal = AudioData::make (std::move (voc), sr);
    m.drums = AudioData::make (std::move (dr), sr);
    m.bass  = AudioData::make (std::move (bs), sr);
    m.keys  = AudioData::make (std::move (ky), sr);
    return m;
}

static double energy (const AudioData& a)
{
    double e = 0;
    for (int c = 0; c < a.getNumChannels(); ++c)
    {
        auto* d = a.buffer.getReadPointer (c);
        for (int i = 0; i < a.getNumSamples(); ++i) e += (double) d[i] * d[i];
    }
    return e;
}

static double correlation (const AudioData& a, const AudioData& b)
{
    double ab = 0, aa = 0, bb = 0;
    const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
    for (int c = 0; c < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++c)
    {
        auto* x = a.buffer.getReadPointer (c);
        auto* y = b.buffer.getReadPointer (c);
        for (int i = 0; i < n; ++i) { ab += (double) x[i] * y[i]; aa += (double) x[i] * x[i]; bb += (double) y[i] * y[i]; }
    }
    return ab / std::sqrt (juce::jmax (1e-12, aa * bb));
}

//==============================================================================
/** Lag (in samples, -maxLag..maxLag) at which b best lines up with a, on channel 0. */
static int bestLag (const AudioData& a, const AudioData& b, int maxLag)
{
    const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
    const float* x = a.buffer.getReadPointer (0);
    const float* y = b.buffer.getReadPointer (0);
    int best = 0; double bestV = -1e30;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double s = 0;
        for (int i = maxLag; i < n - maxLag; ++i)
            s += (double) x[i] * y[i + lag];
        if (s > bestV) { bestV = s; best = lag; }
    }
    return best;
}

static void testResample()
{
    std::cout << "\n[resampling]\n";
    auto demo = makeDemoMix (48000.0, 3.0);
    auto down = edit::resample (*demo.mix, 44100.0);
    auto back = edit::resample (*down, 48000.0);
    CHECK (std::abs (down->getNumSamples() - juce::roundToInt (demo.mix->getNumSamples() * 44100.0 / 48000.0)) <= 1,
           "48k -> 44.1k keeps the length");
    CHECK (back->getNumSamples() == demo.mix->getNumSamples(), "and back again gives the same length");
    const int lag = bestLag (*demo.mix, *back, 16);
    CHECK (lag == 0, "48k -> 44.1k -> 48k adds no delay (lag " + juce::String (lag) + " samples)");

    // What the AI stems rely on: original minus the round-tripped copy must be (nearly) silent.
    // Test signal: 60 random tones up to 18 kHz (everything a voice has, and more).
    {
        const int n = 48000 * 2;
        juce::AudioBuffer<float> sig (1, n);
        sig.clear();
        juce::Random rng (7);
        for (int t = 0; t < 60; ++t)
        {
            const double f = 60.0 + rng.nextDouble() * 17940.0, ph = rng.nextDouble() * 6.283;
            for (int i = 0; i < n; ++i)
                sig.addSample (0, i, 0.01f * (float) std::sin (ph + 6.283185307 * f * i / 48000.0));
        }
        auto orig = AudioData::make (std::move (sig), 48000.0);
        auto rt = edit::resample (*edit::resample (*orig, 44100.0), 48000.0);
        double err = 0, ref = 0;
        for (int i = 4000; i < n - 4000; ++i)
        {
            const double d = orig->buffer.getSample (0, i) - rt->buffer.getSample (0, i);
            err += d * d; ref += (double) orig->buffer.getSample (0, i) * orig->buffer.getSample (0, i);
        }
        const double db = 10.0 * std::log10 (err / juce::jmax (1e-12, ref));
        CHECK (db < -50.0, "original minus round trip is " + juce::String (db, 1) + " dB below 18 kHz (vocals cancel out of the music)");
    }
}

static void testEditOps()
{
    std::cout << "\n[edit ops]\n";
    auto demo = makeDemoMix (44100.0, 4.0);
    auto& a = *demo.mix;
    const int n = a.getNumSamples();

    auto c = edit::crop (a, 1000, 5000);
    CHECK (c->getNumSamples() == 4000, "crop keeps exactly the selection");
    auto r = edit::removeRange (a, 1000, 5000);
    CHECK (r->getNumSamples() == n - 4000, "cut removes exactly the selection");
    auto rr = edit::reverse (*edit::reverse (a, 0, n), 0, n);
    CHECK (correlation (*rr, a) > 0.99999, "reverse twice == original");
    auto nm = edit::normalize (a, 0, n, -0.3f);
    CHECK (std::abs (juce::Decibels::gainToDecibels (edit::peakLevel (*nm)) + 0.3f) < 0.05f, "normalize hits -0.3 dBFS");
    auto fi = edit::fade (a, 0, 2000, true);
    CHECK (std::abs (fi->buffer.getSample (0, 0)) < 1e-6f, "fade in starts from silence");
    auto g = edit::gain (a, 0, n, -6.0f);
    CHECK (std::abs (edit::peakLevel (*g) / edit::peakLevel (a) - 0.501f) < 0.01f, "gain -6 dB halves the level");
    auto mono = edit::toMono (*demo.keys);
    CHECK (correlation (AudioData (juce::AudioBuffer<float> (mono->buffer), 44100.0), *mono) > 0.999 &&
           std::abs (mono->buffer.getSample (0, 5000) - mono->buffer.getSample (1, 5000)) < 1e-7f, "mono makes L == R");
    auto f = edit::filter (a, 200.0f, 0.0f);
    CHECK (energy (*f) < energy (a) * 0.9, "low cut removes bass energy");

    auto st = edit::pitchTime (a, 0.0f, 1.5, true, false);
    CHECK (std::abs (st->getNumSamples() - n * 1.5) < 2, "stretch x1.5 gives 1.5x length");
    auto ps = edit::pitchTime (a, 5.0f, 1.0, true, false);
    CHECK (ps->getNumSamples() == n && edit::peakLevel (*ps) > 0.05f, "pitch +5 st keeps length and has audio");
    auto tape = edit::pitchTime (a, 12.0f, 1.0, false, true);
    CHECK (std::abs (tape->getNumSamples() - n / 2) < 2, "tape +12 st halves the length");
    auto rs = edit::resample (a, 48000.0);
    CHECK (std::abs (rs->getNumSamples() - n * 48000.0 / 44100.0) < 3 && rs->sampleRate == 48000.0, "resample 44.1k -> 48k");
    auto short1 = edit::pitchTime (*edit::crop (a, 0, 800), 3.0f, 1.0, true, false);
    CHECK (short1->getNumSamples() == 800, "pitching a very short sample works");
}

static void testChopping()
{
    std::cout << "\n[chops + tempo]\n";
    auto clicks = makeClicks (44100.0, 4.25, 0.5);   // hits at 0.25, 0.75, ... 3.75  -> 8 hits, 120 BPM
    auto found = edit::detectTransients (*clicks, 0.5f);
    int good = 0;
    for (double t = 0.25; t < 4.1; t += 0.5)
        for (auto s : found)
            if (std::abs (s / 44100.0 - t) < 0.015) { ++good; break; }
    CHECK (good >= 7 && (int) found.size() <= 9, "transient detection finds the hits (" + juce::String (good) + "/8, " + juce::String ((int) found.size()) + " found)");

    auto bpm = edit::estimateBpm (*makeClicks (44100.0, 8.25, 0.5));
    CHECK (std::abs (bpm - 120.0) < 1.5, "tempo of a 120 BPM click = " + juce::String (bpm));
    auto bpm2 = edit::estimateBpm (*makeClicks (44100.0, 10.25, 60.0 / 93.0));
    CHECK (std::abs (bpm2 - 93.0) < 1.5, "tempo of a 93 BPM click = " + juce::String (bpm2));
    auto demo = makeDemoMix (44100.0, 10.0, 92.0);
    auto bpm3 = edit::estimateBpm (*demo.mix);
    CHECK (std::abs (bpm3 - 92.0) < 2.0 || std::abs (bpm3 - 184.0) < 3.0, "tempo of the 92 BPM demo loop = " + juce::String (bpm3));

    auto eq = edit::equalSlices (1000, 4);
    CHECK (eq.size() == 3 && eq[0] == 250 && eq[2] == 750, "equal slices");
}

static void testQuickSplit()
{
    std::cout << "\n[quick split]\n";
    auto demo = makeDemoMix (44100.0, 6.0);
    auto r = QuickSplit::separate (*demo.mix, true);
    CHECK (r.error.isEmpty() && r.stems.size() == 4, "4 stems produced");
    if (r.stems.size() != 4) return;

    std::vector<AudioData::Ptr> layers;
    for (auto& s : r.stems) layers.push_back (s.audio);
    auto sum = edit::mix (layers, { 1, 1, 1, 1 });
    double maxErr = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < demo.mix->getNumSamples(); ++i)
            maxErr = juce::jmax (maxErr, (double) std::abs (sum->buffer.getSample (c, i) - demo.mix->buffer.getSample (c, i)));
    CHECK (maxErr < 2e-3, "stems sum back to the original (max err " + juce::String (maxErr, 6) + ")");

    auto find = [&] (const char* name) { for (auto& s : r.stems) if (s.name == name) return s.audio; return AudioData::Ptr(); };
    auto v = find ("vocals"), d = find ("drums"), b = find ("bass"), o = find ("other");
    const double cv = correlation (*v, *demo.vocal);
    CHECK (cv > 0.6, "vocal stem follows the vocal (corr " + juce::String (cv, 3) + ")");
    CHECK (correlation (*v, *demo.keys) < 0.35, "vocal stem mostly excludes the wide keys (corr " + juce::String (correlation (*v, *demo.keys), 3) + ")");
    CHECK (correlation (*d, *demo.drums) > 0.5, "drum stem follows the drums (corr " + juce::String (correlation (*d, *demo.drums), 3) + ")");
    CHECK (correlation (*b, *demo.bass) > 0.4, "bass stem follows the bass (corr " + juce::String (correlation (*b, *demo.bass), 3) + ")");
    CHECK (correlation (*o, *demo.keys) > 0.5, "other stem follows the keys (corr " + juce::String (correlation (*o, *demo.keys), 3) + ")");

    auto two = QuickSplit::separate (*demo.mix, false);
    CHECK (two.stems.size() == 2 && two.stems[1].name == "music", "vocals + music mode");

    auto monoMix = edit::toMono (*demo.mix);
    CHECK (QuickSplit::isEffectivelyMono (*monoMix), "mono detection");
    auto mr = QuickSplit::separate (*monoMix, false);
    CHECK (mr.error.isNotEmpty(), "mono 2-stem gives a helpful error");
}

static void testFileIO (const juce::File& tmp)
{
    std::cout << "\n[files]\n";
    auto demo = makeDemoMix (48000.0, 2.0);
    auto wav = tmp.getChildFile ("roundtrip.wav");
    CHECK (audioio::writeWav (*demo.mix, wav, 24), "write 24-bit wav");
    auto back = audioio::loadFile (wav, {});
    CHECK (back.audio != nullptr && back.audio->getNumSamples() == demo.mix->getNumSamples() && back.audio->sampleRate == 48000.0, "read it back");
    if (back.audio != nullptr)
        CHECK (correlation (*back.audio, *demo.mix) > 0.99999, "24-bit round trip is transparent");

    // video import through ffmpeg
    juce::File ffmpeg ("/usr/bin/ffmpeg");
    if (ffmpeg.existsAsFile())
    {
        auto mp4 = tmp.getChildFile ("clip.mp4");
        juce::String out;
        ProcessRunner::runAndCapture ({ ffmpeg.getFullPathName(), "-y", "-hide_banner", "-loglevel", "error",
                                        "-f", "lavfi", "-i", "testsrc=size=320x240:rate=25:duration=3",
                                        "-f", "lavfi", "-i", "sine=frequency=440:duration=3:sample_rate=44100",
                                        "-shortest", "-c:v", "mpeg4", "-c:a", "aac", mp4.getFullPathName() }, out, 60000);
        CHECK (mp4.existsAsFile(), "made a test mp4 with ffmpeg");
        auto v = audioio::loadFile (mp4, ffmpeg);
        CHECK (v.audio != nullptr && std::abs (v.audio->lengthSeconds() - 3.0) < 0.1 && edit::peakLevel (*v.audio) > 0.05f,
               "imported audio from the mp4 (" + juce::String (v.audio != nullptr ? v.audio->lengthSeconds() : 0.0, 2) + "s)");

        auto noFfmpeg = audioio::loadFile (mp4, {});
        CHECK (noFfmpeg.audio == nullptr && noFfmpeg.error.containsIgnoreCase ("ffmpeg"), "without ffmpeg you get a clear message");
    }

    CHECK (audioio::isImportable (juce::File ("/x/y.webm")) && audioio::isImportable (juce::File ("/x/y.MP3")) && ! audioio::isImportable (juce::File ("/x/y.txt")),
           "file type filter");
}

static void testWebCapture()
{
    std::cout << "\n[web capture]\n";
    WebCapture cap (nullptr);
    std::vector<float> frames (48000 * 2);
    for (int i = 0; i < 48000; ++i) { frames[(size_t) (2 * i)] = std::sin (i * 0.05f) * 0.5f; frames[(size_t) (2 * i + 1)] = -frames[(size_t) (2 * i)]; }

    // simulate what webtap.js drain() returns: JSON with base64 float32 interleaved
    juce::MemoryBlock mb (frames.data(), frames.size() * sizeof (float));
    auto* o = new juce::DynamicObject();
    o->setProperty ("sr", 48000);
    o->setProperty ("n", 48000);
    o->setProperty ("b64", juce::Base64::toBase64 (mb.getData(), mb.getSize()));
    o->setProperty ("lvl", 0.5);
    o->setProperty ("st", "running");
    o->setProperty ("taps", 1);
    o->setProperty ("playing", true);
    cap.ingest (juce::JSON::toString (juce::var (o)));   // string form (WebView2 / WebKit)

    CHECK (std::abs (cap.getAvailableHindsight() - 1.0) < 0.001, "decoded 1 s of browser audio");
    auto last = cap.grabLast (0.5);
    CHECK (last != nullptr && last->getNumSamples() == 24000, "grab last 0.5 s");
    CHECK (last != nullptr && std::abs (last->buffer.getSample (0, 23999) - frames[(size_t) (2 * 47999)]) < 1e-6f
                            && std::abs (last->buffer.getSample (1, 23999) - frames[(size_t) (2 * 47999 + 1)]) < 1e-6f, "samples are bit-exact, channels intact");

    cap.startRecording();
    for (int k = 0; k < 3; ++k) cap.pushFrames (frames.data(), 48000, 48000.0);
    auto rec = cap.stopRecording();
    CHECK (rec != nullptr && rec->getNumSamples() == 3 * 48000, "forward recording captured 3 s");

    for (int k = 0; k < 70; ++k) cap.pushFrames (frames.data(), 48000, 48000.0);
    CHECK (std::abs (cap.getAvailableHindsight() - WebCapture::hindsightSeconds) < 0.01, "hindsight ring caps at 60 s");
    CHECK (WebCapture::getTapScript().contains ("__snag") && WebCapture::getTapScript().contains ("captureStream"), "tap script is embedded");
}

static void testProcessor()
{
    std::cout << "\n[sampler / processor]\n";
    SnaggerProcessor p;
    p.prepareToPlay (48000.0, 512);

    auto clicks = makeClicks (48000.0, 2.25, 0.5);
    Clip::Ptr c (new Clip());
    c->name = "clicks";
    c->audio = clicks;
    c->slices = edit::detectTransients (*clicks, 0.5f);
    p.session.add (c, true);
    p.setSamplerClip (c);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    auto render = [&] (int blocks)
    {
        float pk = 0;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            p.processBlock (buf, midi);
            midi.clear();
            pk = juce::jmax (pk, buf.getMagnitude (0, 0, 512));
        }
        return pk;
    };

    CHECK (render (4) < 1e-6f, "silent with no notes");
    midi.addEvent (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 110), 10);
    CHECK (render (20) > 0.2f, "MIDI note 61 (C#3) plays chop 2");
    CHECK (p.lastTriggeredSlice.load() == 1, "chop index reported to the UI");
    render (60);   // let the chop finish
    midi.addEvent (juce::MidiMessage::noteOn (1, 90, (juce::uint8) 110), 0);
    CHECK (render (10) < 1e-6f, "notes beyond the chops are ignored");

    p.midiMode = SnaggerProcessor::keys;
    midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0);
    CHECK (render (60) > 0.1f, "KEYS mode plays the sample chromatically");
    p.midiMode = SnaggerProcessor::chops;

    for (int i = 0; i < 200; ++i) render (1);
    p.previewClip (*c, 0, -1, false);
    CHECK (render (30) > 0.2f && p.isPreviewing(), "preview playback");
    p.stopPreview();
    render (2);
    CHECK (! p.isPreviewing(), "preview stops");

    // stems playhead: playback starts where the playhead is
    {
        const int from = c->audio->getNumSamples() / 2;
        p.previewClip (*c, from, -1, false);
        render (4);
        const double pos = p.getPreviewPosition();
        CHECK (p.isPreviewing() && pos >= from && pos < from + 44100, "preview starts at the playhead (" + juce::String ((int) pos) + " >= " + juce::String (from) + ")");
        p.stopPreview();
        render (2);
    }

    // state round trip
    juce::MemoryBlock state;
    p.getStateInformation (state);
    p.session.flushWrites();
    SnaggerProcessor p2;
    p2.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (p2.session.getClips().size() == 1 && p2.session.getClips()[0]->slices == c->slices, "session survives save / reload of the DAW project");
}

static void testProcessAudio()
{
    std::cout << "\n[browser process audio]\n";
    if (! ProcessAudioCapture::isSupported())
    {
        std::cout << "  (not on this system - the page script capture is used)\n";
        return;
    }
    // Build machines have no sound card, so this only proves it opens / closes cleanly.
    ProcessAudioCapture cap;
    std::atomic<int> frames { 0 };
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const bool ok = cap.start (ProcessAudioCapture::currentProcessId(), [&frames] (const float*, int n) { frames += n; });
    const double openMs = juce::Time::getMillisecondCounterHiRes() - t0;
    std::cout << "  start: " << (ok ? "ok" : ("unavailable - " + cap.getLastError()).toStdString()) << " (" << (int) openMs << " ms)\n";
    if (ok)
    {
        juce::Thread::sleep (600);
        cap.stop();
        std::cout << "  frames in 0.6 s: " << frames.load() << "\n";
        CHECK (frames.load() > 48000 / 4, "browser process audio keeps real time (silence is filled in)");
    }
    CHECK (openMs < 9000.0 && ! cap.isRunning(), "browser process audio opens and closes cleanly");
}

static void testLinks()
{
    std::cout << "\n[HQ snag links]\n";
    CHECK (actions::whyNotAMediaPage ("https://www.youtube.com/").isNotEmpty(), "YouTube home page is not a video");
    CHECK (actions::whyNotAMediaPage ("https://www.youtube.com/results?search_query=soul").isNotEmpty(), "YouTube search page is not a video");
    CHECK (actions::whyNotAMediaPage ("https://soundcloud.com").isNotEmpty(), "a site's home page is not a song");
    CHECK (actions::whyNotAMediaPage ("https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=RD").isEmpty(), "YouTube watch link is a video");
    CHECK (actions::whyNotAMediaPage ("https://youtu.be/dQw4w9WgXcQ").isEmpty(), "youtu.be short link is a video");
    CHECK (actions::whyNotAMediaPage ("https://www.youtube.com/shorts/abc123").isEmpty(), "YouTube Shorts link is a video");
    CHECK (actions::whyNotAMediaPage ("https://soundcloud.com/artist/track").isEmpty(), "SoundCloud track link is a song");
}

static void testNetwork()
{
    std::cout << "\n[helper tool installers - network]\n";
    SnaggerProcessor p;
    auto& tools = p.getTools();

    for (auto t : { ToolManager::Tool::ytdlp, ToolManager::Tool::ffmpeg, ToolManager::Tool::deno })
    {
        Job job ("install", nullptr, nullptr);
        const bool ok = tools.install (t, job);
        CHECK (ok, "installed " + ToolManager::displayName (t) + " -> " + tools.getStatus (t).version + (job.hasFailed() ? "  ERR: " + job.getError() : juce::String()));
    }

    // yt-dlp's CLI flags we rely on
    juce::String out;
    ProcessRunner::runAndCapture ({ tools.getPath (ToolManager::Tool::ytdlp).getFullPathName(), "--help" }, out, 60000);
    for (auto flag : { "--js-runtimes", "--download-sections", "--force-keyframes-at-cuts", "--ffmpeg-location", "--cookies-from-browser", "--print", "--no-simulate", "--encoding" })
        CHECK (out.contains (flag), juce::String ("yt-dlp supports ") + flag);

    // the bundled ffmpeg can convert video
    auto ff = tools.getPath (ToolManager::Tool::ffmpeg);
    auto tmp = juce::File::createTempFile (".mp4");
    ProcessRunner::runAndCapture ({ ff.getFullPathName(), "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=300:duration=2",
                                    "-f", "lavfi", "-i", "color=c=black:s=160x120:d=2", "-shortest", tmp.getFullPathName() }, out, 60000);
    auto r = audioio::loadFile (tmp, ff);
    CHECK (r.audio != nullptr && std::abs (r.audio->lengthSeconds() - 2.0) < 0.1, "downloaded FFmpeg decodes video audio");
    tmp.deleteFile();

    // ---- full HQ SNAG pipeline (yt-dlp + ffmpeg + deno args) against a local web server ----
    auto www = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("snagger-www");
    www.deleteRecursively();
    www.createDirectory();
    ProcessRunner::runAndCapture ({ ff.getFullPathName(), "-y", "-hide_banner", "-loglevel", "error",
                                    "-f", "lavfi", "-i", "testsrc=size=320x240:rate=25:duration=6",
                                    "-f", "lavfi", "-i", "sine=frequency=330:duration=6:sample_rate=48000",
                                    "-shortest", "-c:v", "libx264", "-c:a", "aac", "-pix_fmt", "yuv420p",
                                    www.getChildFile ("music-video.mp4").getFullPathName() }, out, 120000);
    juce::ChildProcess server;
    // a small web server with byte-range support, like real video CDNs
    server.start ({ "python3", juce::File (SNAGGER_TESTS_DIR).getChildFile ("rangeserver.py").getFullPathName(), "8765", www.getFullPathName() });
    juce::Thread::sleep (1200);

    juce::StringArray messages;
    p.onNotify = [&messages] (const juce::String& m, bool err) { messages.add ((err ? "ERR: " : "") + m); std::cout << "        -> " << m << "\n"; };

    auto waitForJobs = [&] (int timeoutMs)
    {
        const auto start = juce::Time::getMillisecondCounter();
        pump (200);
        while (p.jobs.isBusy() && (int) (juce::Time::getMillisecondCounter() - start) < timeoutMs)
            pump (100);
        pump (200);
    };

    actions::downloadUrl (p, "http://127.0.0.1:8765/music-video.mp4");
    waitForJobs (180000);
    CHECK (p.session.getClips().size() == 1 && std::abs (p.session.getClips()[0]->audio->lengthSeconds() - 6.0) < 0.2,
           "HQ SNAG of a whole video -> " + (p.session.getClips().isEmpty() ? juce::String ("nothing") : juce::String (p.session.getClips()[0]->audio->lengthSeconds(), 2) + "s '" + p.session.getClips()[0]->name + "'"));

    actions::downloadUrl (p, "http://127.0.0.1:8765/music-video.mp4", 1.5, 4.0, "Section test");
    waitForJobs (180000);
    CHECK (p.session.getClips().size() == 2 && std::abs (p.session.getClips()[1]->audio->lengthSeconds() - 2.5) < 0.35,
           "HQ SNAG of an IN/OUT section -> " + (p.session.getClips().size() < 2 ? juce::String ("nothing") : juce::String (p.session.getClips()[1]->audio->lengthSeconds(), 2) + "s '" + p.session.getClips()[1]->name + "'"));

    actions::downloadUrl (p, "http://127.0.0.1:8765/does-not-exist.mp4");
    waitForJobs (120000);
    CHECK (messages.size() > 0 && messages[messages.size() - 1].startsWith ("ERR") && messages[messages.size() - 1].contains ("404"), "a bad link gives an error message: " + messages[messages.size() - 1]);

    // import a video file through the job system too
    actions::importFiles (p, { www.getChildFile ("music-video.mp4").getFullPathName() });
    waitForJobs (60000);
    CHECK (p.session.getClips().size() == 3 && p.session.getClips()[2]->kind == "Video", "IMPORT FILE of an mp4 through the job system");

    // A site yt-dlp doesn't know (like a sound-effects library): the page itself fails, so HQ SNAG
    // grabs the audio file the page loaded / is playing instead.
    www.getChildFile ("sounds.html").replaceWithText ("<html><head><title>Sound effects - yodel</title></head><body><h1>Results</h1></body></html>");
    {
        actions::MediaHints hints;
        hints.others.add ("http://127.0.0.1:8765/music-video.mp4");
        const int before = p.session.getClips().size();
        actions::downloadUrl (p, "http://127.0.0.1:8765/sounds.html?q=yodel", -1.0, -1.0, "Sound effects - yodel", hints);
        waitForJobs (180000);
        const bool got = p.session.getClips().size() == before + 1;
        CHECK (got && std::abs (p.session.getClips().getLast()->audio->lengthSeconds() - 6.0) < 0.2,
               "HQ SNAG on a site yt-dlp can't do falls back to the page's audio file -> "
               + (got ? "'" + p.session.getClips().getLast()->name + "'" : messages[messages.size() - 1]));
    }
    {
        actions::MediaHints hints;
        hints.playing.add ("http://127.0.0.1:8765/music-video.mp4");
        const int before = p.session.getClips().size();
        actions::downloadUrl (p, "http://127.0.0.1:8765/sounds.html?q=yodel", 1.0, 3.0, "Sound effects - yodel", hints);
        waitForJobs (180000);
        const bool got = p.session.getClips().size() == before + 1;
        CHECK (got && std::abs (p.session.getClips().getLast()->audio->lengthSeconds() - 2.0) < 0.35,
               "HQ SNAG IN/OUT of the file that's playing -> " + (got ? juce::String (p.session.getClips().getLast()->audio->lengthSeconds(), 2) + "s" : messages[messages.size() - 1]));
    }
    actions::downloadUrl (p, "http://127.0.0.1:8765/sounds.html?q=nothing");
    waitForJobs (120000);
    CHECK (messages[messages.size() - 1].containsIgnoreCase ("LIVE REC"), "a site with nothing to grab says to use LIVE REC: " + messages[messages.size() - 1]);

    server.kill();
    p.onNotify = nullptr;
    p.session.flushWrites();
    www.deleteRecursively();
}

//==============================================================================
static void testBuiltinAi()
{
    std::cout << "\n[built-in AI stems - downloads the models]\n";
    CHECK (ai::isAvailable(), "built-in AI engine available on this CPU (" + juce::SystemStats::getCpuModel() + ")");
    if (! ai::isAvailable()) return;

    std::cout << "  threads: " << ai::defaultThreadCount() << "\n";
    auto demo = makeDemoMix (48000.0, 10.0, 92.0);   // 48 kHz on purpose: exercises the resampling path

    for (auto mode : { ai::Mode::fourStems, ai::Mode::vocalsMusic })
    {
        Job job ("ai", nullptr, nullptr);
        std::vector<ai::Stem> stems;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const bool ok = ai::separate (*demo.mix, mode, job, {}, stems);
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        const juce::String label = mode == ai::Mode::fourStems ? "4-stem" : "vocals + music";
        CHECK (ok, label + " separation ran in " + juce::String (secs, 1) + " s (incl. download) " + job.getError());
        if (! ok) continue;

        for (auto& st : stems)
            CHECK (st.audio->getNumSamples() == demo.mix->getNumSamples() && st.audio->sampleRate == 48000.0,
                   "  " + st.name + " stem is sample-aligned with the source");

        auto find = [&] (const char* n) { for (auto& st : stems) if (st.name == n) return st.audio; return AudioData::Ptr(); };
        if (mode == ai::Mode::fourStems)
        {
            CHECK (stems.size() == 4, "4 stems returned");
            auto d = find ("drums"), b = find ("bass");
            if (d != nullptr) CHECK (correlation (*d, *demo.drums) > 0.4, "AI drum stem matches the drums (corr " + juce::String (correlation (*d, *demo.drums), 3) + ")");
            if (d != nullptr) CHECK (bestLag (*demo.drums, *d, 32) == 0, "AI drum stem lines up with the original to the sample (lag " + juce::String (bestLag (*demo.drums, *d, 32)) + ")");
            if (b != nullptr) CHECK (correlation (*b, *demo.bass) > 0.4, "AI bass stem matches the bass (corr " + juce::String (correlation (*b, *demo.bass), 3) + ")");
            std::vector<AudioData::Ptr> layers;
            for (auto& st : stems) layers.push_back (st.audio);
            auto sum = edit::mix (layers, { 1, 1, 1, 1 });
            CHECK (correlation (*sum, *demo.mix) > 0.9, "AI stems add back up to the mix (corr " + juce::String (correlation (*sum, *demo.mix), 4) + ")");
        }
        else
        {
            CHECK (stems.size() == 2 && stems[0].name == "vocals" && stems[1].name == "music", "vocals + music returned");
            if (stems.size() == 2)
            {
                auto sum = edit::mix ({ stems[0].audio, stems[1].audio }, { 1, 1 });
                double maxErr = 0;
                for (int i = 0; i < sum->getNumSamples(); ++i)
                    maxErr = juce::jmax (maxErr, (double) std::abs (sum->buffer.getSample (0, i) - demo.mix->buffer.getSample (0, i)));
                CHECK (maxErr < 1e-4, "vocals + music add back up to the original exactly");
            }
        }
    }

    // cancellation stops quickly
    {
        Job job ("ai", nullptr, nullptr);
        std::vector<ai::Stem> stems;
        juce::WaitableEvent finished;
        std::thread canceller ([&] { if (! finished.wait (1500)) job.cancel(); });
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const bool ok = ai::separate (*demo.mix, ai::Mode::fourStems, job, {}, stems);
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        finished.signal();
        canceller.join();
        if (secs < 1.4)
            std::cout << "  (separation finished in " << secs << " s, before it could be cancelled)\n";
        else
            CHECK (! ok && stems.empty() && ! job.hasFailed(), "cancel stops the AI (after " + juce::String (secs, 1) + " s)");
    }
}

//==============================================================================
static void savePng (juce::Component& c, const juce::File& f, float scale = 1.0f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
    std::cout << "  shot  " << f.getFullPathName() << "\n";
}

static void renderScreens (const juce::File& dir)
{
    std::cout << "\n[screenshots]\n";
    dir.createDirectory();

    auto p = std::make_unique<SnaggerProcessor>();
    p->prepareToPlay (44100.0, 512);

    // library content
    auto libDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SnaggerShotLibrary");
    libDir.deleteRecursively();
    libDir.createDirectory();
    p->getSettings().setLibraryDir (libDir);

    auto demo = makeDemoMix (44100.0, 10.5, 92.0);

    Clip::Ptr soul (new Clip());
    soul->name = "Late Night Soul Loop";
    soul->kind = "Download";
    soul->origin = "https://www.youtube.com/watch?v=example";
    soul->audio = demo.mix;
    soul->bpm = 92.0;
    soul->slices = edit::detectTransients (*demo.mix, 0.35f);
    if (soul->slices.size() > 15)
    {
        // keep 15 markers spread evenly over the whole loop (16 chops)
        std::vector<int> picked;
        for (int i = 0; i < 15; ++i)
            picked.push_back (soul->slices[(size_t) (i * (int) soul->slices.size() / 15)]);
        soul->slices = picked;
    }

    auto addSimple = [&] (const juce::String& name, const juce::String& kind, AudioData::Ptr a)
    {
        Clip::Ptr c (new Clip());
        c->name = name; c->kind = kind; c->audio = a;
        p->session.add (c, false);
        return c;
    };
    addSimple ("Street Performer Sax", "Live", edit::crop (*demo.keys, 0, 44100 * 4));
    addSimple ("Interview - \"keep going\"", "Live", edit::crop (*demo.vocal, 44100, 44100 * 3));
    p->session.add (soul, true);
    addSimple ("Vinyl Crackle Room Tone", "Video", edit::crop (*demo.drums, 0, 44100 * 5));

    auto split = QuickSplit::separate (*demo.mix, true);
    for (auto& s : split.stems)
    {
        Clip::Ptr c (new Clip());
        c->name = soul->name + " - " + prettyStemName (s.name);
        c->kind = "Stem"; c->parentId = soul->id; c->stemName = s.name; c->audio = s.audio;
        p->session.add (c, false);
    }
    p->session.select (soul.get());
    p->setSamplerClip (soul);

    for (auto name : { "Late Night Soul Loop - Vocals", "Soul Loop chop 3", "Kick one-shot", "Sax phrase (cut)" })
        audioio::writeWav (*edit::crop (*demo.mix, 0, 44100 * 2), libDir.getChildFile (juce::String (name) + ".wav"), 24);

    auto ed = std::unique_ptr<SnaggerEditor> (dynamic_cast<SnaggerEditor*> (p->createEditor()));
    ed->setSize (1280, 820);
    pump (300);

    ed->showTab (Tab::browse);
    pump (200);
    savePng (*ed, dir.getChildFile ("1-browse.png"));

    ed->showTab (Tab::studio);
    pump (100);
    auto& studio = ed->getStudioPage();
    juce::ignoreUnused (studio);
    // select chop 5 so the selection + pads show up
    {
        auto b = soul->sliceBoundaries();
        if (b.size() > 6)
        {
            auto* wv = dynamic_cast<WaveformView*> ([&]() -> juce::Component*
            {
                for (auto* ch : studio.getChildren()) if (dynamic_cast<WaveformView*> (ch)) return ch;
                return nullptr;
            }());
            if (wv != nullptr) wv->setSelection (b[4], b[6]);
        }
    }
    p->previewClip (*soul, 44100 * 3, -1, false);
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i) p->processBlock (buf, midi);
    pump (150);
    savePng (*ed, dir.getChildFile ("2-studio.png"));
    p->stopPreview();
    for (int i = 0; i < 4; ++i) p->processBlock (buf, midi);

    ed->showTab (Tab::stems);
    pump (200);
    savePng (*ed, dir.getChildFile ("3-stems.png"));

    ed->showTab (Tab::library);
    pump (200);
    savePng (*ed, dir.getChildFile ("4-library.png"));

    ed->showTab (Tab::browse);
    ed->openSettings();
    pump (400);
    savePng (*ed, dir.getChildFile ("5-settings.png"));
    ed->getSettingsPanel().close();

    ed->showTab (Tab::studio);
    ed->toast ("Snagged in HQ: Late Night Soul Loop");
    pump (150);
    savePng (*ed, dir.getChildFile ("6-studio-hidpi.png"), 2.0f);

    // smallest window size: every tab name must still fit
    ed->setSize (1040, 700);
    ed->showTab (Tab::browse);
    pump (150);
    savePng (*ed, dir.getChildFile ("7-browse-min-size.png"));

    ed.reset();
    p->session.flushWrites();
    libDir.deleteRecursively();
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args (argv + 1, argc - 1);

    std::cout << "Sample Snagger test runner\n";
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("snagger-tests");
    tmp.deleteRecursively();
    tmp.createDirectory();

    testEditOps();
    testResample();
    testChopping();
    testQuickSplit();
    testFileIO (tmp);
    testWebCapture();
    testProcessor();
    testLinks();
    testProcessAudio();
    if (args.contains ("--network"))
        testNetwork();
    if (args.contains ("--ai"))
        testBuiltinAi();

    const int shotIdx = args.indexOf ("--shots");
    if (shotIdx >= 0 && shotIdx + 1 < args.size())
        renderScreens (juce::File (args[shotIdx + 1]));

    tmp.deleteRecursively();
    std::cout << "\n" << passes << " passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
