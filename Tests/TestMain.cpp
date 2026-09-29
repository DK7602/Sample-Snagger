// Sample Snagger - headless test runner + UI screenshot renderer.
//
//   SnaggerTests                 run unit tests
//   SnaggerTests --shots DIR     also render every screen to PNGs in DIR
//   SnaggerTests --network       also test the helper-tool downloads (needs internet)
//   SnaggerTests --ai            also download the AI models and run real stem separation
//   SnaggerTests --claw DIR      also render frames of the stem-separation animation to DIR

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Actions.h"
#include "../Source/core/EditOps.h"
#include "../Source/core/QuickSplit.h"
#include "../Source/core/AudioFileIO.h"
#include "../Source/core/WebCapture.h"
#include "../Source/core/AiStems.h"
#include "../Source/core/ProcessAudioCapture.h"
#include "../Source/core/KeyDetect.h"
#include "../Source/core/PadFx.h"
#include "../Source/core/FxRack.h"
#include "../Source/core/AudioToMidi.h"
#include <iostream>
#include <thread>
#include <set>
#include <atomic>

using namespace snag;

static int failures = 0, passes = 0;

/** On GitHub Actions, lines like "::notice::..." show up as annotations on the run page. */
static bool onCI()   { return juce::SystemStats::getEnvironmentVariable ("GITHUB_ACTIONS", {}) == "true"; }
static void ciAnnotate (const char* kind, const juce::String& msg)
{
    if (onCI())
        std::cout << "::" << kind << " title=" << juce::SystemStats::getOperatingSystemName() << "::" << msg << std::endl;
}

#define CHECK(cond, msg) do { if (cond) { ++passes; std::cout << "  ok    " << msg << "\n"; } \
                              else { ++failures; std::cout << "  FAIL  " << msg << "   (" #cond ")\n"; \
                                     ciAnnotate ("error", juce::String ("FAIL ") + juce::String (msg)); } } while (0)

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

/** Dominant frequency of channel 0 by zero crossings (fine for a sine). */
static double zeroCrossHz (const AudioData& a)
{
    const float* x = a.buffer.getReadPointer (0);
    int zc = 0;
    const int n = a.getNumSamples();
    for (int i = 1; i < n; ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++zc;
    return zc * 0.5 / (n / a.sampleRate);
}

static void testVocalRemoval()
{
    std::cout << "\n[taking vocals out of the music]\n";
    const double sr = 44100.0;
    const int n = (int) (sr * 4.0);
    juce::Random rng (42);

    // a sung-ish line (harmonics with vibrato, in syllables) over drums, bass and a pad
    juce::AudioBuffer<float> v (2, n), o (2, n);
    v.clear(); o.clear();
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double f0 = 220.0 * std::pow (2.0, (std::floor (t * 2.0) - 3.0 * std::floor (t * 2.0 / 3.0)) * 2.0 / 12.0)
                        * (1.0 + 0.012 * std::sin (6.283 * 5.5 * t));
        ph += 6.283185307 * f0 / sr;
        const double env = std::pow (std::sin (3.14159 * std::fmod (t * 2.0, 1.0)), 2.0);
        double voice = 0.0;
        for (int h = 1; h <= 12; ++h)
            voice += std::sin (ph * h) / (h * 1.2);
        const float vs = (float) (0.22 * env * voice);
        const double beat = std::fmod (t, 0.5);
        const float drums = (float) ((beat < 0.06 ? (rng.nextFloat() * 2.0f - 1.0f) * (1.0 - beat / 0.06) * 0.5 : 0.0)
                                     + std::sin (6.283 * 60.0 * beat) * std::exp (-beat * 30.0) * 0.6);
        const float bass = (float) (0.25 * std::sin (6.283 * 55.0 * t));
        const float pad = (float) (0.06 * (std::sin (6.283 * 330.0 * t) + std::sin (6.283 * 415.3 * t) + std::sin (6.283 * 494.0 * t)));
        for (int c = 0; c < 2; ++c) { v.setSample (c, i, vs); o.setSample (c, i, drums + bass + pad * (c == 0 ? 1.0f : 0.8f)); }
    }
    auto vocals = AudioData::make (juce::AudioBuffer<float> (v), sr);
    auto others = AudioData::make (juce::AudioBuffer<float> (o), sr);
    auto mix = edit::mix ({ vocals, others }, { 1, 1 });

    // what an AI typically gives back: most of the voice, not all of it (reverb, breaths... are missed)
    juce::AudioBuffer<float> est (v);
    est.applyGain (0.7f);
    auto vocalEstimate = AudioData::make (std::move (est), sr);

    auto leak = [&] (const AudioData& music)   // how much of the true voice is still in the music
    {
        double num = 0, den = 0;
        for (int i = 0; i < n; ++i) { num += music.buffer.getSample (0, i) * v.getSample (0, i); den += (double) v.getSample (0, i) * v.getSample (0, i); }
        return num / den;
    };
    auto keep = [&] (const AudioData& music) { return correlation (music, *others); };

    // identity: nothing to remove -> the mix comes back unchanged
    {
        juce::AudioBuffer<float> silent (2, n); silent.clear();
        auto none = AudioData::make (std::move (silent), sr);
        auto same = edit::musicWithoutVocals (*mix, *none, mix.get());
        double maxErr = 0;
        for (int i = 0; i < n; ++i) maxErr = juce::jmax (maxErr, (double) std::abs (same->buffer.getSample (0, i) - mix->buffer.getSample (0, i)));
        CHECK (maxErr < 1e-3, "with no vocals the mask gives back the mix unchanged (max err " + juce::String (maxErr, 6) + ")");
    }

    juce::AudioBuffer<float> sub (2, n);
    for (int c = 0; c < 2; ++c) { sub.copyFrom (c, 0, mix->buffer, c, 0, n); sub.addFrom (c, 0, vocalEstimate->buffer, c, 0, n, -1.0f); }
    auto subtracted = AudioData::make (std::move (sub), sr);
    auto fromResidual = edit::musicWithoutVocals (*mix, *vocalEstimate, nullptr);
    auto fromOthers   = edit::musicWithoutVocals (*mix, *vocalEstimate, others.get());

    const double l0 = leak (*subtracted), l1 = leak (*fromResidual), l2 = leak (*fromOthers);
    auto dB = [] (double x) { return juce::String (20.0 * std::log10 (juce::jmax (1e-6, std::abs (x))), 1) + " dB"; };
    std::cout << "  vocal left in the music:  mix - vocals " << dB (l0) << ",  masked " << dB (l1) << ",  masked with the other parts " << dB (l2) << "\n";
    CHECK (std::abs (l1) < std::abs (l0) * 0.5, "the mask takes out at least 6 dB more of the vocal than plain subtraction");
    CHECK (std::abs (l2) < std::abs (l0) * 0.25, "with the AI's other parts, at least 12 dB more");
    CHECK (keep (*fromResidual) > 0.9 && keep (*fromOthers) > 0.9, "and the instruments stay (corr " + juce::String (keep (*fromResidual), 3) + " / " + juce::String (keep (*fromOthers), 3) + ")");

    // A deep, processed voice ("Test your might") that the vocal stem gets right but the other
    // parts' estimate ALSO contains: a Wiener mask alone only turns it down ~8 dB.
    {
        juce::AudioBuffer<float> both (o);
        for (int c = 0; c < 2; ++c) both.addFrom (c, 0, v, c, 0, n, 0.8f);
        auto othersWithVoice = AudioData::make (std::move (both), sr);
        auto exactVocals = AudioData::make (juce::AudioBuffer<float> (v), sr);
        auto music = edit::musicWithoutVocals (*mix, *exactVocals, othersWithVoice.get());
        const double l = leak (*music);
        std::cout << "  voice the other parts also claim: left in the music " << dB (l) << "\n";
        CHECK (std::abs (l) < 0.056, "a voice the other parts also claim still comes out (below -25 dB)");
        CHECK (keep (*music) > 0.9, "and the instruments stay (corr " + juce::String (keep (*music), 3) + ")");
    }
}

/** Chords I-IV-V-I (major) or i-iv-V-i (minor) with a bass line, 1 s each, in any key. */
static AudioData::Ptr makeProgression (int tonic, bool minor, double sr, double cents = 0.0, bool drums = false)
{
    const int third = minor ? 3 : 4;
    const int chords[4][3] = { { 0, third, 7 }, { 5, 5 + (minor ? 3 : 4), 12 }, { 7, 11, 14 }, { 0, third, 7 } };
    const int n = (int) (sr * 4.0);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rng (tonic * 2 + (minor ? 1 : 0));
    const double tune = std::pow (2.0, cents / 1200.0);
    for (int ch = 0; ch < 4; ++ch)
    {
        const int s0 = (int) (ch * sr), s1 = (int) ((ch + 1) * sr);
        for (int v = 0; v < 4; ++v)
        {
            const int note = v < 3 ? 60 + ((tonic + chords[ch][v]) % 12) : 36 + (tonic + chords[ch][0]) % 12;   // + bass
            const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0) * tune;
            for (int i = s0; i < s1; ++i)
            {
                const double t = (i - s0) / sr;
                double y = 0;
                for (int h = 1; h <= 6; ++h) y += std::sin (6.283185307 * f * h * t) / h;
                const float env = (float) (std::exp (-t * 1.5) * juce::jmin (1.0, t * 200.0));
                const float g = v < 3 ? 0.08f : 0.12f;
                b.addSample (0, i, (float) y * env * g);
                b.addSample (1, i, (float) y * env * g);
            }
        }
    }
    if (drums)   // kick + noisy hats on every 8th
        for (int i = 0; i < n; ++i)
        {
            const double t = std::fmod (i / sr, 0.25);
            const float kick = (float) (std::sin (6.283 * 55.0 * t * (1.0 + 2.0 * std::exp (-t * 40.0))) * std::exp (-t * 12.0) * 0.4);
            const float hat = (rng.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-std::fmod (i / sr, 0.125) * 60.0) * 0.15f;
            for (int c = 0; c < 2; ++c) b.addSample (c, i, kick + hat);
        }
    return AudioData::make (std::move (b), sr);
}

static void testKeyDetect()
{
    std::cout << "\n[key detection]\n";
    int right = 0, rightNoisy = 0, rightDetuned = 0;
    juce::StringArray misses;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (bool minor : { false, true })
        {
            const key::Key expected { tonic, minor };
            const auto k = key::detect (*makeProgression (tonic, minor, 44100.0));
            if (k == expected) ++right; else misses.add (key::name (expected) + "->" + key::name (k));
            if (key::detect (*makeProgression (tonic, minor, 48000.0, 0.0, true)) == expected) ++rightNoisy;
            if (key::detect (*makeProgression (tonic, minor, 44100.0, 30.0)) == expected) ++rightDetuned;
        }
    CHECK (right == 24, "all 24 keys found (" + juce::String (right) + "/24) " + misses.joinIntoString (" "));
    CHECK (rightNoisy >= 23, "with drums on top: " + juce::String (rightNoisy) + "/24");
    CHECK (rightDetuned >= 23, "on a recording 30 cents sharp: " + juce::String (rightDetuned) + "/24");
    const auto tuned = key::detect (*makeProgression (9, true, 44100.0, 30.0));
    CHECK (std::abs (tuned.tuningCents - 30.0f) <= 6.0f, "and it measures the tuning (" + juce::String (tuned.tuningCents, 1) + " cents)");

    CHECK (key::name ({ 9, true }) == "Am" && key::camelot ({ 9, true }) == "8A", "A minor = Am = 8A");
    CHECK (key::name ({ 0, false }) == "C" && key::camelot ({ 0, false }) == "8B", "C major = 8B");
    CHECK (key::camelot ({ 7, false }) == "9B" && key::camelot ({ 11, false }) == "1B" && key::camelot ({ 5, false }) == "7B", "Camelot wheel: G 9B, B 1B, F 7B");
    CHECK (key::camelot ({ 4, true }) == "9A" && key::camelot ({ 2, true }) == "7A", "Em 9A, Dm 7A");
    CHECK (key::semitonesTo ({ 9, true }, 11) == 2 && key::semitonesTo ({ 9, true }, 3) == -6 + 0 || key::semitonesTo ({ 9, true }, 3) == 6 - 12,
           "A minor -> B minor is +2, -> D# is the nearest way round");
    CHECK (key::name (key::Key { 9, true }.transposed (2)) == "Bm" && key::name (key::Key { 0, false }.transposed (-1)) == "B", "transposing keys");
    CHECK (key::longName ({ 10, false }) == "B flat major", "long names (" + key::longName ({ 10, false }) + ")");
}

static AudioData::Ptr makeSine (double hz, double seconds, double sr = 44100.0, float amp = 0.5f)
{
    juce::AudioBuffer<float> b (2, (int) (sr * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int c = 0; c < 2; ++c)
            b.setSample (c, i, amp * (float) std::sin (6.283185307 * hz * i / sr));
    return AudioData::make (std::move (b), sr);
}

static double rmsOf (const AudioData& a, int start = 0, int len = -1, int ch = 0)
{
    if (len < 0) len = a.getNumSamples() - start;
    return a.buffer.getRMSLevel (ch, start, juce::jmax (1, len));
}

static int zeroCrossings (const AudioData& a, int start, int len)
{
    int z = 0;
    for (int i = start + 1; i < start + len && i < a.getNumSamples(); ++i)
        if ((a.buffer.getSample (0, i - 1) < 0) != (a.buffer.getSample (0, i) < 0)) ++z;
    return z;
}

static void testPads()
{
    std::cout << "\n[per-pad controls]\n";
    const double sr = 44100.0;
    auto tone = makeSine (440.0, 1.0, sr);
    const int s = 11025, e = 33075;   // a 0.5 s chop

    auto plain = renderPad (*tone, s, e, {});
    double maxDiff = 0;
    for (int i = 2000; i < plain->getNumSamples() - 2000; ++i)
        maxDiff = juce::jmax (maxDiff, (double) std::abs (plain->buffer.getSample (0, i) - tone->buffer.getSample (0, s + i)));
    CHECK (std::abs (plain->getNumSamples() - (e - s)) <= 2 && maxDiff < 1.0e-4, "default pad plays the chop untouched (max diff " + juce::String (maxDiff, 6) + ")");

    PadParams loud; loud.gainDb = 6.0f;
    CHECK (std::abs (rmsOf (*renderPad (*tone, s, e, loud), 4000, 10000) / rmsOf (*plain, 4000, 10000) - 2.0) < 0.02, "GAIN +6 dB doubles it");

    PadParams up; up.semitones = 12.0f;
    auto high = renderPad (*tone, s, e, up);
    CHECK (std::abs (high->getNumSamples() - (e - s) / 2) <= 3, "PITCH +12 plays twice as fast (sampler style)");
    CHECK (std::abs (zeroCrossings (*high, 1000, 8000) - 2 * zeroCrossings (*plain, 1000, 8000)) <= 3, "... an octave higher");

    PadParams rev; rev.reverse = true;
    auto chirp = AudioData::make ([&] { juce::AudioBuffer<float> b (2, 44100); for (int i = 0; i < 44100; ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, (float) i / 44100.0f); return b; }(), sr);
    auto back = renderPad (*chirp, 1000, 21000, rev);
    CHECK (back->buffer.getSample (0, 5000) > back->buffer.getSample (0, 15000) && std::abs (back->buffer.getSample (0, 5000) - chirp->buffer.getSample (0, 21000 - 1 - 5000)) < 1.0e-3,
           "REVERSE plays it backwards");

    auto bright = makeSine (6000.0, 1.0, sr), deep = makeSine (80.0, 1.0, sr);
    PadParams lp; lp.filter = -0.6f;
    PadParams hp; hp.filter = 0.5f;
    const double lpDrop = juce::Decibels::gainToDecibels (rmsOf (*renderPad (*bright, s, e, lp), 4000, 10000) / rmsOf (*renderPad (*bright, s, e, {}), 4000, 10000));
    const double hpDrop = juce::Decibels::gainToDecibels (rmsOf (*renderPad (*deep, s, e, hp), 4000, 10000) / rmsOf (*renderPad (*deep, s, e, {}), 4000, 10000));
    const double lpPass = juce::Decibels::gainToDecibels (rmsOf (*renderPad (*deep, s, e, lp), 4000, 10000) / rmsOf (*renderPad (*deep, s, e, {}), 4000, 10000));
    CHECK (lpDrop < -20.0 && std::abs (lpPass) < 1.5, "FILTER low-pass (" + filterText (lp.filter) + ") cuts 6 kHz by " + juce::String (lpDrop, 1) + " dB, keeps 80 Hz");
    CHECK (hpDrop < -15.0, "FILTER high-pass (" + filterText (hp.filter) + ") cuts 80 Hz by " + juce::String (hpDrop, 1) + " dB");
    CHECK (filterText (0.0f) == "Off" && filterText (-1.0f).startsWith ("LP") && filterText (1.0f).startsWith ("HP"), "filter readout");

    PadParams slow; slow.attackMs = 400.0f;
    auto swell = renderPad (*tone, s, e, slow);
    CHECK (rmsOf (*swell, 0, 2000) < 0.1 * rmsOf (*plain, 0, 2000) + 1e-4 && rmsOf (*swell, 17640, 2000) > 0.9 * rmsOf (*plain, 17640, 2000),
           "ATTACK fades it in");
    PadParams longRel; longRel.releaseMs = 150.0f;
    auto faded = renderPad (*tone, s, e, longRel);
    const int n = faded->getNumSamples();
    CHECK (rmsOf (*faded, n - 2205, 2205) < 0.5 * rmsOf (*plain, n - 2205, 2205), "RELEASE fades the chop's end");

    PadParams any; any.gainDb = -3.5f; any.semitones = 7.0f; any.reverse = true; any.attackMs = 12.0f; any.releaseMs = 300.0f; any.filter = -0.25f;
    CHECK (PadParams::fromString (any.toString()) == any && PadParams::fromString ({}).isDefault(), "pad settings save and load");
}

static void testFx()
{
    std::cout << "\n[FX rack]\n";
    const double sr = 44100.0;
    auto tone = makeSine (220.0, 1.0, sr, 0.4f);

    auto same = renderFx (*tone, {}, 120.0);
    CHECK (same->getNumSamples() == tone->getNumSamples() && std::abs (rmsOf (*same) - rmsOf (*tone)) < 1e-6, "all off: untouched");

    // an impulse through the delay: echoes land on the beat grid, ping-ponging left / right
    juce::AudioBuffer<float> imp (2, (int) sr); imp.clear(); imp.setSample (0, 100, 1.0f); imp.setSample (1, 100, 1.0f);
    auto click = AudioData::make (std::move (imp), sr);
    FxSettings d; d.delayOn = true; d.division = 8; /* 1/4 */ d.feedback = 0.5f; d.delayMix = 0.5f;
    auto echo = renderFx (*click, d, 120.0);
    auto peakNear = [&] (int ch, double t) { return echo->buffer.getMagnitude (ch, (int) (100 + t * sr) - 300, 600); };
    CHECK (peakNear (0, 0.5) > 0.2f && peakNear (1, 0.5) < 0.05f, "DELAY 1/4 at 120 BPM: first echo after 0.5 s, on the left");
    CHECK (peakNear (1, 1.0) > 0.1f, "second echo 0.5 s later on the right (ping-pong)");
    CHECK (echo->getNumSamples() > click->getNumSamples(), "the echoes ring out past the end (" + juce::String (echo->getNumSamples() / sr, 2) + " s)");
    auto fast = renderFx (*click, d, 150.0);
    CHECK (fast->buffer.getMagnitude (0, (int) (100 + 0.4 * sr) - 300, 600) > 0.2f, "...and follows the tempo (150 BPM -> 0.4 s)");

    FxSettings rv; rv.reverbOn = true; rv.size = 0.8f; rv.reverbMix = 0.5f;
    auto wet = renderFx (*click, rv, 120.0);
    CHECK (wet->getNumSamples() > click->getNumSamples() && rmsOf (*wet, (int) (sr * 0.3), (int) (sr * 0.3)) > 1e-3, "REVERB leaves a tail");

    FxSettings dr; dr.driveOn = true; dr.drive = 0.8f; dr.tone = 1.0f;
    auto hot = renderFx (*tone, dr, 120.0);
    // harmonics: energy at 3 x 220 Hz appears
    auto energyAt = [sr] (const AudioData& a, double hz)
    {
        double re = 0, im = 0;
        for (int i = 4410; i < 4410 + 22050; ++i) { const double w = 6.283185307 * hz * i / sr; re += a.buffer.getSample (0, i) * std::cos (w); im += a.buffer.getSample (0, i) * std::sin (w); }
        return std::sqrt (re * re + im * im);
    };
    const double h3 = juce::Decibels::gainToDecibels (energyAt (*hot, 660.0) / energyAt (*hot, 220.0));
    CHECK (h3 > -30.0 && energyAt (*tone, 660.0) / energyAt (*tone, 220.0) < 0.001, "DRIVE adds harmonics (3rd at " + juce::String (h3, 1) + " dB)");
    const double lvl = juce::Decibels::gainToDecibels (rmsOf (*hot) / rmsOf (*tone));
    CHECK (std::abs (lvl) < 8.0, "...without a big jump in level (" + juce::String (lvl, 1) + " dB)");

    FxSettings lo; lo.lofiOn = true; lo.bits = 4.0f; lo.rateKHz = 44.0f; lo.vinyl = 0.0f;
    auto crushed = renderFx (*tone, lo, 120.0);
    std::set<int> steps;
    for (int i = 0; i < 20000; ++i) steps.insert (juce::roundToInt (crushed->buffer.getSample (0, i) * 1000.0f));
    CHECK (steps.size() < 40, "LO-FI 4 bits: only a handful of levels (" + juce::String ((int) steps.size()) + ")");
    FxSettings vin; vin.lofiOn = true; vin.bits = 16.0f; vin.rateKHz = 44.0f; vin.vinyl = 1.0f;
    juce::AudioBuffer<float> quiet (2, (int) sr); quiet.clear();
    auto crackle = renderFx (*AudioData::make (std::move (quiet), sr), vin, 120.0);
    CHECK (crackle->buffer.getMagnitude (0, 0, crackle->getNumSamples()) > 0.005f, "VINYL crackles and hisses over silence");

    FxSettings all; all.lofiOn = all.driveOn = all.delayOn = all.reverbOn = true; all.division = 3; all.size = 0.33f;
    CHECK (FxSettings::fromString (all.toString()) == all && ! FxSettings::fromString ({}).anyOn(), "FX settings save and load");

    // real-time chain == offline render (same code): process in odd block sizes
    FxChain rt; rt.prepare (sr);
    juce::AudioBuffer<float> live (tone->buffer);
    for (int st = 0, blk = 1; st < live.getNumSamples(); st += blk, blk = blk % 997 + 13)
        rt.process (live.getWritePointer (0, st), live.getWritePointer (1, st), juce::jmin (blk, live.getNumSamples() - st), all, 120.0);
    auto off = renderFx (*tone, all, 120.0);
    double diff = 0;
    for (int i = 0; i < live.getNumSamples(); ++i) diff = juce::jmax (diff, (double) std::abs (live.getSample (0, i) - off->buffer.getSample (0, i)));
    CHECK (diff < 1.0e-4, "what you drag matches what you hear (block size doesn't matter, diff " + juce::String (diff, 6) + ")");
}

/** A tone with a few harmonics and a piano-ish decay. */
static void addNote (juce::AudioBuffer<float>& b, double sr, int midiNote, double start, double len, float amp = 0.25f)
{
    const double hz = 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
    const int s0 = (int) (start * sr), s1 = juce::jmin (b.getNumSamples(), (int) ((start + len) * sr));
    for (int i = s0; i < s1; ++i)
    {
        const double t = (i - s0) / sr;
        double y = 0;
        for (int h = 1; h <= 5; ++h) y += std::sin (6.283185307 * hz * h * t) / (h * h);
        const double env = std::exp (-t * 1.5) * juce::jmin (1.0, t * 300.0) * juce::jmin (1.0, (len - t) * 60.0);
        for (int c = 0; c < b.getNumChannels(); ++c) b.addSample (c, i, (float) (y * env) * amp);
    }
}

static void testAudioToMidi()
{
    std::cout << "\n[audio to MIDI]\n";
    const double sr = 44100.0;

    // a melody: C4 E4 G4 C5 A4, half a second each
    juce::AudioBuffer<float> mel (2, (int) (sr * 3.0)); mel.clear();
    const int melody[] = { 60, 64, 67, 72, 69 };
    for (int k = 0; k < 5; ++k) addNote (mel, sr, melody[k], 0.2 + k * 0.5, 0.45);
    juce::String err;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto notes = midi::transcribe (*AudioData::make (std::move (mel), sr), {}, err);
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    CHECK (err.isEmpty(), "the built-in note detector runs (" + juce::String (juce::roundToInt (ms)) + " ms for 3 s) " + err);
    juce::StringArray got;
    for (auto& n : notes) got.add (juce::String (n.pitch) + "@" + juce::String (n.start, 2));
    bool melodyRight = notes.size() == 5;
    for (size_t k = 0; melodyRight && k < 5; ++k)
        melodyRight = notes[k].pitch == melody[k] && std::abs (notes[k].start - (0.2 + (double) k * 0.5)) < 0.06;
    CHECK (melodyRight, "a 5-note melody comes out as the right notes at the right times (" + got.joinIntoString (" ") + ")");

    // a chord: C major, held
    juce::AudioBuffer<float> ch (2, (int) (sr * 2.0)); ch.clear();
    for (int p : { 60, 64, 67 }) addNote (ch, sr, p, 0.3, 1.2, 0.18f);
    auto chordAudio = AudioData::make (std::move (ch), sr);
    midi::Posteriors post;
    CHECK (midi::analyse (*chordAudio, post, err), "chord analysed");
    auto chord = midi::notesFrom (post, {});
    std::set<int> pitches;
    for (auto& n : chord) pitches.insert (n.pitch);
    CHECK (pitches.count (60) && pitches.count (64) && pitches.count (67) && pitches.size() <= 4,
           "a C major chord gives C4, E4, G4 (" + midi::noteRangeText (chord) + ")");
    midi::Options mono; mono.melodyOnly = true;
    auto top = midi::notesFrom (post, mono);
    CHECK (top.size() == 1, "MELODY mode keeps one note at a time (" + juce::String ((int) top.size()) + ")");

    // MIDI file: tempo + notes land on the right ticks
    std::vector<midi::Note> twoBeats { { 0.0, 0.5, 60, 0.8f }, { 0.5, 1.0, 62, 0.5f } };
    auto mf = midi::toMidiFile (twoBeats, 120.0, "Test");
    const auto* track = mf.getTrack (0);
    int ons = 0; double secondOn = -1;
    for (auto* e : *track)
        if (e->message.isNoteOn()) { ++ons; if (e->message.getNoteNumber() == 62) secondOn = e->message.getTimeStamp(); }
    CHECK (mf.getTimeFormat() == 960 && ons == 2 && std::abs (secondOn - 960.0) < 0.5, "MIDI file at 120 BPM: the 2nd note starts on beat 2 (tick 960)");
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("snagger-test.mid");
    CHECK (midi::writeMidiFile (twoBeats, 120.0, "Test", tmp) && tmp.getSize() > 30, "writes a .mid file");
    tmp.deleteFile();
    auto heard = midi::renderNotes (twoBeats, 44100.0, 1.0);
    CHECK (heard->getNumSamples() >= 44100 && heard->buffer.getMagnitude (0, 0, heard->getNumSamples()) > 0.05f, "LISTEN renders the notes");
}

static void testStemParts()
{
    std::cout << "\n[stem parts]\n";
    CHECK (actions::aiModeFor (actions::Engine::ai, { "vocals", "music" }) == ai::Mode::vocalsMusic, "vocals + music uses the fine-tuned vocal model");
    CHECK (actions::aiModeFor (actions::Engine::ai, { "drums" }) == ai::Mode::fourStems, "drums alone uses the 4-part model");
    CHECK (actions::aiModeFor (actions::Engine::aiMax, { "drums", "bass" }) == ai::Mode::fourStemsMax, "Max uses the fine-tuned models");
    CHECK (actions::aiModeFor (actions::Engine::aiMax, { "vocals", "music" }) == ai::Mode::fourStemsMax, "Max builds the music from all four specialists too");
    CHECK (actions::aiModeFor (actions::Engine::ai, { "vocals", "piano" }) == ai::Mode::sixStems, "guitar / piano use the 6-part model");

    SnaggerProcessor p;
    juce::StringArray messages;
    p.onNotify = [&messages] (const juce::String& m, bool err) { messages.add ((err ? "ERR: " : "") + m); };
    auto demo = makeDemoMix (44100.0, 4.0);
    Clip::Ptr c (new Clip());
    c->name = "Mix";
    c->audio = demo.mix;
    p.session.add (c, true);
    auto waitJobs = [&p]
    {
        pump (200);
        for (int i = 0; i < 600 && p.jobs.isBusy(); ++i) pump (100);
        pump (200);
    };

    actions::separate (p, c, actions::Engine::quick, { "drums", "music" });
    waitJobs();
    auto stems = p.session.getStemsOf (*c);
    juce::StringArray names;
    for (auto& st : stems) names.add (st->stemName);
    CHECK (names.joinIntoString (",") == "music,drums", "only the picked parts come back, in menu order (" + names.joinIntoString (",") + ")");

    actions::separate (p, c, actions::Engine::quick, { "vocals", "music" });
    waitJobs();
    stems = p.session.getStemsOf (*c);
    if (stems.size() == 2)
    {
        auto sum = edit::mix ({ stems[0]->audio, stems[1]->audio }, { 1, 1 });
        double maxErr = 0;
        for (int i = 0; i < sum->getNumSamples(); ++i)
            maxErr = juce::jmax (maxErr, (double) std::abs (sum->buffer.getSample (0, i) - demo.mix->buffer.getSample (0, i)));
        CHECK (maxErr < 1e-4, "vocals + music add up to the original exactly");
    }
    else
        CHECK (false, "vocals + music came back");

    actions::separate (p, c, actions::Engine::quick, { "guitar" });
    waitJobs();
    CHECK (messages.size() > 0 && messages[messages.size() - 1].contains ("guitar"), "Quick Split explains it can't do guitar / piano");
    p.onNotify = nullptr;
}

namespace snag
{
/** Drives the STUDIO knobs the way a person would. */
struct StudioPageTester
{
    static void run()
    {
        std::cout << "\n[studio knobs - live, with DEFAULT]\n";
        auto p = std::make_unique<SnaggerProcessor>();
        p->prepareToPlay (44100.0, 512);
        auto ed = std::unique_ptr<SnaggerEditor> (dynamic_cast<SnaggerEditor*> (p->createEditor()));
        ed->setSize (1280, 820);
        auto& page = ed->getStudioPage();

        // a 440 Hz tone
        juce::AudioBuffer<float> b (2, 44100 * 2);
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                b.setSample (ch, i, 0.25f * (float) std::sin (6.283185307 * 440.0 * i / 44100.0));
        Clip::Ptr c (new Clip());
        c->name = "Tone";
        c->audio = AudioData::make (std::move (b), 44100.0);
        auto original = c->audio;
        p->session.add (c, true);
        page.setClip (c);

        auto settle = [&p]
        {
            pump (400);
            for (int i = 0; i < 300 && p->jobs.isBusy(); ++i) pump (100);
            pump (300);
        };

        page.pitchKnob.slider.setValue (12.0);   // up an octave
        settle();
        CHECK (std::abs (zeroCrossHz (*c->audio) - 880.0) < 25.0, "moving PITCH applies straight away (" + juce::String (zeroCrossHz (*c->audio), 0) + " Hz)");
        CHECK (std::abs (page.pitchKnob.slider.getValue() - 12.0) < 0.01, "and the knob stays where it was put");
        CHECK (c->adjustBase == original, "the original is kept");

        const float peakPitched = c->audio->buffer.getMagnitude (0, 0, c->audio->getNumSamples());
        page.gainKnob.slider.setValue (6.0);
        settle();
        const float peakLoud = c->audio->buffer.getMagnitude (0, 0, c->audio->getNumSamples());
        CHECK (std::abs (juce::Decibels::gainToDecibels (peakLoud / peakPitched) - 6.0f) < 0.6f, "GAIN applies on top of the pitch change");
        CHECK (std::abs (zeroCrossHz (*c->audio) - 880.0) < 25.0, "  ...keeping the pitch");

        page.stretchKnob.slider.setValue (150.0);
        settle();
        CHECK (std::abs (c->audio->getNumSamples() - 44100 * 3) < 4410, "LENGTH 150% makes it 1.5x as long");

        page.pitchDefaultBtn.triggerClick();
        settle();
        CHECK (std::abs (zeroCrossHz (*c->audio) - 440.0) < 15.0 && std::abs (c->audio->getNumSamples() - 44100 * 2) < 100,
               "PITCH & TIME DEFAULT restores the original pitch and length");
        CHECK (std::abs (page.pitchKnob.slider.getValue()) < 0.01 && std::abs (page.stretchKnob.slider.getValue() - 100.0) < 0.01, "  ...and its knobs");
        CHECK (std::abs (page.gainKnob.slider.getValue() - 6.0) < 0.01, "  ...leaving TONE alone");

        page.toneDefaultBtn.triggerClick();
        settle();
        CHECK (c->audio == original && c->adjustBase == nullptr, "TONE DEFAULT too: the exact original is back");

        page.undo();
        CHECK (std::abs (page.gainKnob.slider.getValue() - 6.0) < 0.01, "undo brings the knob back with the sound");

        ed.reset();
    }
};
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
                CHECK (correlation (*sum, *demo.mix) > 0.9, "vocals + music still add up to (nearly) the original (corr " + juce::String (correlation (*sum, *demo.mix), 3) + ")");
                CHECK (bestLag (*demo.mix, *stems[1].audio, 32) == 0, "music lines up with the original to the sample");
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

    // Vocals + music, end to end, on AI Studio and on Max: the music must not keep the voice.
    // With SNAGGER_TEST_VOICE (CI makes one with espeak-ng) a real spoken voice goes into the mix;
    // otherwise it's measured against the demo's synth lead, which the AI may well call "music".
    // (edit::mix treats a gain <= 0 as "muted", so subtract by hand)
    auto minus = [] (const AudioData& a, const AudioData& b)
    {
        const int n = a.getNumSamples(), chans = juce::jmax (a.getNumChannels(), b.getNumChannels());
        juce::AudioBuffer<float> out (chans, n);
        for (int c = 0; c < chans; ++c)
        {
            out.copyFrom (c, 0, a.buffer, juce::jmin (c, a.getNumChannels() - 1), 0, n);
            out.addFrom (c, 0, b.buffer, juce::jmin (c, b.getNumChannels() - 1), 0, juce::jmin (n, b.getNumSamples()), -1.0f);
        }
        return AudioData::make (std::move (out), a.sampleRate);
    };
    AudioData::Ptr voice = demo.vocal, song = demo.mix, instruments = minus (*demo.mix, *demo.vocal);
    bool realVoice = false;
    {
        const juce::File voiceFile (juce::SystemStats::getEnvironmentVariable ("SNAGGER_TEST_VOICE", {}));
        if (voiceFile.existsAsFile())
        {
            auto r = audioio::loadFile (voiceFile, {});
            if (r.audio != nullptr && r.audio->getNumSamples() > 1000)
            {
                auto v = edit::resample (*r.audio, demo.mix->sampleRate);
                const int n = demo.mix->getNumSamples();
                juce::AudioBuffer<float> b (2, n);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)   // loop the phrase for the whole demo
                        b.setSample (c, i, v->buffer.getSample (juce::jmin (c, v->getNumChannels() - 1), i % v->getNumSamples()));
                auto rms = [] (const juce::AudioBuffer<float>& x) { return x.getRMSLevel (0, 0, x.getNumSamples()); };
                b.applyGain (0.8f * rms (demo.mix->buffer) / juce::jmax (1.0e-6f, rms (b)));   // about as loud as the band
                voice = AudioData::make (std::move (b), demo.mix->sampleRate);
                instruments = demo.mix;   // the synth lead stays in, as an instrument
                song = edit::mix ({ demo.mix, voice }, { 1.0f, 1.0f });
                realVoice = true;
                std::cout << "  (testing with a spoken voice from " << voiceFile.getFileName() << ")\n";
            }
        }
    }
    auto leakOf = [&voice] (const AudioData& music)
    {
        double num = 0, den = 0;
        const int n = juce::jmin (music.getNumSamples(), voice->getNumSamples());
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const double v = voice->buffer.getSample (c, i);
                num += music.buffer.getSample (juce::jmin (c, music.getNumChannels() - 1), i) * v;
                den += v * v;
            }
        return den > 0 ? std::abs (num / den) : 0.0;
    };
    auto dB = [] (double x) { return juce::String (20.0 * std::log10 (juce::jmax (1e-6, x)), 1) + " dB"; };

    for (auto engine : { actions::Engine::ai, actions::Engine::aiMax })
    {
        const juce::String name = engine == actions::Engine::aiMax ? "AI Studio Max" : "AI Studio";
        SnaggerProcessor p;
        juce::StringArray errors;
        p.onNotify = [&errors] (const juce::String& m, bool err) { if (err) errors.add (m); };
        Clip::Ptr c (new Clip());
        c->name = "Demo";
        c->audio = song;
        p.session.add (c, true);

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        actions::separate (p, c, engine, { "vocals", "music" });
        pump (200);
        for (int i = 0; i < 30 * 60 * 10 && p.jobs.isBusy(); ++i)   // up to 30 minutes
            pump (100);
        pump (200);
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

        AudioData::Ptr vocals, music;
        for (auto& st : p.session.getStemsOf (*c))
        {
            if (st->stemName == "vocals") vocals = st->audio;
            if (st->stemName == "music")  music = st->audio;
        }
        CHECK (vocals != nullptr && music != nullptr, name + ": vocals + music in " + juce::String (juce::roundToInt (secs)) + " s " + errors.joinIntoString ("; "));
        if (vocals == nullptr || music == nullptr)
            continue;

        auto subtracted = minus (*song, *vocals);   // the old way
        // SDR of the music against the true instruments (the usual separation score, higher = cleaner)
        auto sdr = [&instruments, &minus] (const AudioData& music)
        {
            auto err = minus (music, *instruments);
            double e = 0, o = 0;
            for (int c = 0; c < 2; ++c)
            {
                const int n = juce::jmin (err->getNumSamples(), instruments->getNumSamples());
                for (int i = 0; i < n; ++i)
                {
                    const double d = err->buffer.getSample (juce::jmin (c, err->getNumChannels() - 1), i);
                    const double x = instruments->buffer.getSample (juce::jmin (c, instruments->getNumChannels() - 1), i);
                    e += d * d;
                    o += x * x;
                }
            }
            return 10.0 * std::log10 (juce::jmax (1e-12, o) / juce::jmax (1e-12, e));
        };
        const double now = leakOf (*music), before = leakOf (*subtracted);
        const auto line = name + (realVoice ? " (spoken voice)" : " (synth lead)") + ": voice left in the music " + dB (now)
                        + " (mix - vocals: " + dB (before) + "); music SDR " + juce::String (sdr (*music), 1)
                        + " dB (mix - vocals: " + juce::String (sdr (*subtracted), 1) + " dB); vocal stem catches " + dB (leakOf (*vocals));
        std::cout << "  " << line << "\n";
        ciAnnotate ("notice", line);
        if (realVoice)
            CHECK (now < 0.1, name + ": the voice left in the music is below -20 dB (" + dB (now) + ")");
        CHECK (bestLag (*song, *music, 32) == 0, name + ": music lines up with the original to the sample");
    }
}

//==============================================================================
//==============================================================================
static void testKnobTyping()
{
    std::cout << "\n[knobs - double-click the number to type a value]\n";
    Knob k ("PITCH", -24.0, 24.0, 0.0, 0.1, " st");
    k.setSize (86, 118);
    auto approx = [] (double a, double b, double tol = 1.0e-6) { return std::abs (a - b) <= tol; };

    CHECK (approx (k.parseTyped ("3.5"), 3.5), "\"3.5\" reads as 3.5");
    CHECK (approx (k.parseTyped ("-2 st"), -2.0), "\"-2 st\" reads as -2");
    CHECK (approx (k.parseTyped ("+7"), 7.0), "\"+7\" reads as 7");
    CHECK (approx (k.parseTyped ("2,5"), 2.5), "\"2,5\" (decimal comma) reads as 2.5");
    CHECK (approx (k.parseTyped ("5k"), 5000.0) && approx (k.parseTyped ("2.5 kHz"), 2500.0), "\"5k\" / \"2.5 kHz\" read as Hz");
    CHECK (approx (k.parseTyped ("off"), 0.0), "\"off\" gives the knob's default");
    CHECK (std::isnan (k.parseTyped ("abc")), "text without a number is ignored");

    // what Apple Silicon leaves after snapping 0 to the knob's 0.1 steps
    k.setValueSilently (-24.0 + 240.0 * 0.1 + 1.33e-15);

    int changes = 0;
    k.onChange = [&] { ++changes; };

    auto editor = [&k]() -> juce::TextEditor*
    {
        for (auto* c : k.getChildren())
            if (auto* t = dynamic_cast<juce::TextEditor*> (c))
                if (t->isVisible())
                    return t;
        return nullptr;
    };

    // double-click on the number (below the dial)
    const juce::Point<float> onNumber ((float) k.getWidth() * 0.5f, (float) k.getHeight() - 21.0f);
    k.mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), onNumber, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          &k, &k, juce::Time::getCurrentTime(), onNumber, juce::Time::getCurrentTime(), 2, false));
    auto* t = editor();
    CHECK (t != nullptr, "double-clicking the number opens a text box");
    if (t != nullptr)
    {
        CHECK (t->getText() == "0", "the box starts with the current value (" + t->getText().toStdString() + ")");
        t->setText ("7.25", false);
        t->onReturnKey();
        CHECK (approx (k.slider.getValue(), 7.25, 0.051), "typing 7.25 + Return sets the knob (" + juce::String (k.slider.getValue()).toStdString() + ")");
        CHECK (changes > 0, "typing a value applies it straight away (onChange)");
    }
    pump (30);
    CHECK (editor() == nullptr, "the text box closes afterwards");

    k.startTyping();
    if (auto* t2 = editor()) { t2->setText ("99", false); t2->onReturnKey(); }
    CHECK (approx (k.slider.getValue(), 24.0), "values past the end stop at the knob's range");

    k.startTyping();
    if (auto* t3 = editor()) { t3->setText ("-5", false); t3->onEscapeKey(); }
    CHECK (approx (k.slider.getValue(), 24.0), "Escape cancels without changing the value");

    k.startTyping();
    if (auto* t4 = editor()) { t4->setText ("nonsense", false); t4->onReturnKey(); }
    CHECK (approx (k.slider.getValue(), 24.0), "text that isn't a number leaves the value alone");
    pump (30);
}

//==============================================================================
static void testClawAnimation()
{
    std::cout << "\n[stems - claw animation while separating]\n";

    // rendering: every moment of the loop and the burst draws, at small and large sizes
    {
        SeparationAnimation a;
        for (auto size : { juce::Point<int> (560, 260), juce::Point<int> (1250, 520), juce::Point<int> (1900, 900) })
        {
            a.setSize (size.x, size.y);
            bool drewTrack = true;
            double firstMs = 0.0, total = 0.0;
            int frames = 0;
            juce::Image img (juce::Image::ARGB, size.x, size.y, true);
            for (double t = 0.0; t < SeparationAnimation::loopLength; t += 0.1)
            {
                a.freezeAt (t);
                a.setStatus ("Separating (2 of 4) on 8 cores", 0.43f);
                img.clear (img.getBounds());
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                {
                    juce::Graphics g (img);
                    a.paintEntireComponent (g, true);
                }
                const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                if (frames++ == 0) firstMs = ms; else total += ms;
                // the left end of the track is always there (well away from the claw)
                bool yellowish = false;
                const int y0 = juce::roundToInt ((float) size.y * 0.35f), y1 = juce::roundToInt ((float) size.y * 0.7f);
                for (int y = y0; y < y1 && ! yellowish; ++y)
                    for (int x = size.x / 6; x < size.x / 6 + 30 && ! yellowish; ++x)
                    {
                        auto c = img.getPixelAt (x, y);
                        yellowish = c.getRed() > 200 && c.getGreen() > 150 && c.getBlue() < 120;
                    }
                drewTrack = drewTrack && yellowish;
            }
            const double avg = total / juce::jmax (1, frames - 1);
            const auto label = juce::String (size.x) + "x" + juce::String (size.y);
            CHECK (drewTrack, "the yellow track shows in every frame at " + label);
            CHECK (size.x > 1300 || avg < 50.0, "a frame takes " + juce::String (avg, 1) + " ms at " + label
                                                 + " (first one, building the cache: " + juce::String (firstMs, 0) + " ms)");
        }

        // resizing while it plays: the glow is stretched until the size settles, then redrawn
        {
            SeparationAnimation live;
            live.setSize (1000, 400);
            live.start();
            auto paintOnce = [&live]
            {
                juce::Image img (juce::Image::ARGB, live.getWidth(), live.getHeight(), true);
                juce::Graphics g (img);
                live.paintEntireComponent (g, true);
            };
            paintOnce();
            CHECK (! live.isStretchingForResize(), "drawn crisply at its size");
            live.setSize (1100, 460);
            paintOnce();
            CHECK (live.isStretchingForResize(), "mid-resize it stretches the cached glow (smooth resizing)");
            pump (320);
            paintOnce();
            CHECK (! live.isStretchingForResize(), "once the size settles it's redrawn crisply");
            live.stop();
        }

        // the grabbed section glows red while the claw has it
        a.setSize (1100, 480);
        a.freezeAt (1.85);
        auto img = a.createComponentSnapshot (a.getLocalBounds());
        int red = 0;
        for (int y = 150; y < 380; ++y)
            for (int x = 480; x < 620; ++x)
            {
                auto c = img.getPixelAt (x, y);
                if (c.getRed() > 200 && c.getGreen() < 110 && c.getBlue() < 120) ++red;
            }
        CHECK (red > 200, "the clutched section turns red (" + juce::String (red) + " red pixels)");

        juce::Array<juce::Colour> colours { theme::stemColour ("vocals"), theme::stemColour ("music") };
        for (double bt : { 0.0, 0.2, 0.6, 1.1 })
        {
            a.freezeAt (2.4, bt, colours);
            auto b = a.createComponentSnapshot (a.getLocalBounds());
            juce::ignoreUnused (b);
        }
        CHECK (true, "the burst draws at every stage");
    }

    // the Stems page runs it for the sample being split, then bursts into the new stems
    auto p = std::make_unique<SnaggerProcessor>();
    p->prepareToPlay (44100.0, 512);
    auto ed = std::unique_ptr<SnaggerEditor> (dynamic_cast<SnaggerEditor*> (p->createEditor()));
    ed->setSize (1280, 820);
    ed->showTab (Tab::stems);
    auto& page = ed->getStemsPage();
    auto& claw = page.getClaw();

    auto mix = makeClicks (44100.0, 2.0, 0.25);
    Clip::Ptr c (new Clip());
    c->name = "Loop";
    c->audio = mix;
    p->session.add (c, true);
    pump (100);

    std::atomic<bool> release { false }, fail { false };
    auto startJob = [&]
    {
        release = false;
        const auto id = c->id;
        auto* proc = p.get();
        p->jobs.start ("AI Split", [&release, &fail] (Job& job)
        {
            job.setStatus ("Separating on 4 CPU cores");
            job.setProgress (0.4f);
            while (! release) juce::Thread::sleep (5);
            if (fail) job.fail ("Test failure");
        }, [proc, id, mix] (Job& job)
        {
            if (job.hasFailed()) return;
            auto parent = proc->session.findById (id);
            for (auto name : { "vocals", "music" })
            {
                Clip::Ptr s (new Clip());
                s->name = "Loop - " + prettyStemName (name);
                s->kind = "Stem"; s->parentId = id; s->stemName = name; s->audio = mix;
                proc->session.add (s, false);
            }
            proc->session.select (parent.get());
        }, id);
    };

    startJob();
    pump (200);
    CHECK (claw.isLooping() && claw.isVisible(), "the claw plays while the split runs");
    CHECK (page.getLanes().isEmpty(), "no stem lanes yet");

    release = true;
    pump (250);
    CHECK (page.getLanes().size() == 2, "stem lanes are built when the split finishes");
    CHECK (claw.isActive() && ! claw.isLooping(), "the track bursts into the stems");
    pump (1400);
    CHECK (! claw.isActive() && ! claw.isVisible(), "the animation is gone after the burst");
    CHECK (page.getLanes().size() == 2 && page.areLanesFullyShown(), "every stem lane is fully shown");

    // a failed split: the claw just goes away, the old stems stay put
    fail = true;
    startJob();
    pump (200);
    CHECK (claw.isLooping(), "the claw plays again for a second split");
    release = true;
    pump (300);
    CHECK (! claw.isActive(), "a failed split stops the animation (no burst)");
    CHECK (page.getLanes().size() == 2 && page.areLanesFullyShown(), "the previous stems are still there and visible");
    fail = false;

    ed.reset();
    pump (50);
}

static void testSamplerSound()
{
    std::cout << "\n[sampler: pad sounds + FX, saved with the project]\n";
    auto p = std::make_unique<SnaggerProcessor>();
    p->prepareToPlay (44100.0, 512);

    Clip::Ptr c (new Clip());
    c->name = "Tone";
    c->audio = makeSine (220.0, 1.0, 44100.0, 0.3f);
    c->slices = { 22050 };                         // two chops of 0.5 s
    PadParams loud; loud.gainDb = 6.0f;
    c->setPad (1, loud);
    p->session.add (c, true);
    p->setSamplerClip (c);

    auto play = [&] (int note, int blocks)
    {
        juce::AudioBuffer<float> out (2, 512);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        double sum = 0;
        std::vector<double> perBlock;
        for (int b = 0; b < blocks; ++b)
        {
            p->processBlock (out, m);
            m.clear();
            const double r = out.getRMSLevel (0, 0, 512);
            perBlock.push_back (r);
            sum += r;
        }
        return perBlock;
    };
    auto quiet = play (60, 60), boosted = play (61, 60);   // each chop plays out (0.5 s) before the next
    const double ratio = boosted[10] / juce::jmax (1e-9, quiet[10]);
    CHECK (std::abs (ratio - 2.0) < 0.1, "pad 2's GAIN +6 dB plays twice as loud as pad 1 (" + juce::String (ratio, 2) + "x)");

    // FX: a reverb tail keeps ringing after the chop ends
    auto tailAfter = [&]
    {
        auto blocks = play (60, 120);   // 0.5 s chop, then ~0.9 s more
        double tail = 0;
        for (size_t b = 60; b < blocks.size(); ++b) tail = juce::jmax (tail, blocks[b]);
        return tail;
    };
    const double dryTail = tailAfter();
    c->fx.reverbOn = true; c->fx.reverbMix = 0.8f; c->fx.size = 0.9f;
    p->setSamplerClip (c);
    const double wetTail = tailAfter();
    CHECK (dryTail < 1e-5 && wetTail > 1e-3, "FX on the pads: the reverb rings on after the chop (" + juce::String (wetTail, 4) + ")");

    // STUDIO playback goes through the FX, stems / library auditions don't
    auto previewTail = [&] (bool fx)
    {
        juce::AudioBuffer<float> fresh (2, 512); juce::MidiBuffer none;
        c->fx.reverbOn = false; p->setSamplerClip (c);                 // clear the old tail...
        for (int b = 0; b < 4; ++b) p->processBlock (fresh, none);
        c->fx.reverbOn = true; p->setSamplerClip (c);                  // ...and start clean
        p->previewClip (*c, 0, 11025, false, fx);
        double tail = 0;
        for (int b = 0; b < 60; ++b) { p->processBlock (fresh, none); if (b > 30) tail = juce::jmax (tail, (double) fresh.getRMSLevel (0, 0, 512)); }
        return tail;
    };
    CHECK (previewTail (true) > 1e-3 && previewTail (false) < 1e-5, "STUDIO playback gets the FX, other auditions stay dry");

    // drag a pad: its sound + the FX, echoes included
    auto dragged = actions::renderPadForExport (*p, *c, 1);
    CHECK (dragged != nullptr && dragged->getNumSamples() > 22050 + 44100 / 2, "a dragged pad includes the reverb tail (" + juce::String (dragged->lengthSeconds(), 2) + " s)");
    c->fx = {};
    auto padOnly = actions::renderPadForExport (*p, *c, 1);
    CHECK (padOnly != nullptr && std::abs (padOnly->getNumSamples() - 22050) <= 2 && std::abs (rmsOf (*padOnly, 2000, 10000) / rmsOf (*c->audio, 2000, 10000) - 2.0) < 0.05,
           "a dragged pad plays like the pad (+6 dB)");

    // everything comes back with the DAW project
    c->fx.delayOn = true; c->fx.division = 3;
    c->keyTonic = 9; c->keyMinor = true; c->keyManual = true;
    PadParams odd; odd.reverse = true; odd.semitones = -3.0f; odd.filter = 0.4f;
    c->setPad (0, odd);
    juce::MemoryBlock state;
    p->getStateInformation (state);
    p->session.flushWrites();
    auto q = std::make_unique<SnaggerProcessor>();
    q->setStateInformation (state.getData(), (int) state.getSize());
    pump (200);
    auto back = q->session.findById (c->id);
    CHECK (back != nullptr && back->padAt (0) == odd && back->padAt (1) == loud && back->fx == c->fx,
           "pad sounds and FX are saved with the project");
    CHECK (back != nullptr && back->keyTonic == 9 && back->keyMinor && back->keyManual, "...and the key");
}

struct StudioDecksTester
{
    static void run()
    {
        std::cout << "\n[studio tabs: key, pads, FX, MIDI]\n";
        auto p = std::make_unique<SnaggerProcessor>();
        p->prepareToPlay (44100.0, 512);
        auto ed = std::unique_ptr<SnaggerEditor> (dynamic_cast<SnaggerEditor*> (p->createEditor()));
        ed->setSize (1280, 820);
        ed->showTab (Tab::studio);
        auto& studio = ed->getStudioPage();

        Clip::Ptr c (new Clip());
        c->name = "Progression";
        c->audio = makeProgression (2, false, 44100.0);   // D major
        p->session.add (c, true);
        for (int i = 0; i < 100 && (p->jobs.isBusy() || c->keyTonic < 0); ++i) pump (50);
        CHECK (c->keyTonic == 2 && ! c->keyMinor, "opening a sample finds its key by itself (" + key::name ({ c->keyTonic, c->keyMinor }) + ")");

        // chop it, pick pad 3, shape it
        c->slices = edit::equalSlices (c->audio->getNumSamples(), 4);
        studio.setClip (c);
        studio.setDeck (1);
        studio.getPads().onSliceSelected (2);
        auto& pd = studio.getPadDeck();
        pd.gainKnob.slider.setValue (-6.0, juce::sendNotificationSync);
        pd.filterKnob.slider.setValue (-0.5, juce::sendNotificationSync);
        pd.reverseToggle.setToggleState (true, juce::dontSendNotification);
        pd.reverseToggle.onClick();
        CHECK (std::abs (c->padAt (2).gainDb + 6.0f) < 0.01f && c->padAt (2).filter < -0.4f && c->padAt (2).reverse && c->padAt (0).isDefault(),
               "PADS tab: the knobs shape only the selected pad");
        pd.allBtn.onClick();
        CHECK (c->padAt (0) == c->padAt (2) && c->padAt (3) == c->padAt (2), "COPY TO ALL PADS");
        studio.handleKey (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        CHECK (c->padAt (0).isDefault() && c->padAt (2).reverse, "undo takes back the last pad change");

        // FX
        studio.setDeck (2);
        auto& fd = studio.getFxDeck();
        fd.sizeKnob.slider.setValue (80.0, juce::sendNotificationSync);
        CHECK (c->fx.reverbOn && std::abs (c->fx.size - 0.8f) < 0.001f, "FX tab: turning a REVERB knob switches the reverb on");
        fd.defaultBtn.onClick();
        CHECK (! c->fx.anyOn(), "ALL OFF");

        // MIDI (the sample's notes)
        studio.setDeck (3);
        auto& md = studio.getMidiDeck();
        md.setRange (0, -1);
        md.findNotes();
        for (int i = 0; i < 200 && md.isAnalysing(); ++i) pump (50);
        std::set<int> pcs;
        for (auto& n : md.getNotes()) pcs.insert (n.pitch % 12);
        const std::set<int> dMajor { 2, 4, 6, 7, 9, 11, 1 };
        bool allInKey = ! pcs.empty();
        for (auto pc : pcs) allInKey = allInKey && dMajor.count (pc) > 0;
        CHECK (md.getNotes().size() >= 6 && allInKey, "MIDI tab: finds the chord notes, all in D major (" + midi::noteRangeText (md.getNotes()) + ")");
        auto mid = actions::makeMidiFile (*p, *c, md.getNotes());
        juce::FileInputStream in (mid);
        juce::MidiFile mf;
        CHECK (mid.hasFileExtension ("mid") && mf.readFrom (in) && mf.getNumTracks() == 1, "DRAG MIDI makes a real .mid file");
        mid.deleteFile();

        studio.setDeck (0);
        ed.reset();
        pump (50);
    }
};

/** Frames of the claw animation, for looking at (SnaggerTests --claw DIR). */
static void renderClawFrames (const juce::File& dir)
{
    std::cout << "\n[claw frames]\n";
    dir.createDirectory();
    SeparationAnimation a;
    juce::Array<juce::Colour> colours { theme::stemColour ("vocals"), theme::stemColour ("drums"),
                                        theme::stemColour ("bass"), theme::stemColour ("other") };
    for (auto size : { juce::Point<int> (1250, 520), juce::Point<int> (640, 300) })
    {
        a.setSize (size.x, size.y);
        a.setStatus ("Separating (2 of 4) on 8 cores", 0.43f);
        int frame = 0;
        for (double t = 0.0; t < SeparationAnimation::loopLength; t += 0.15)
        {
            a.freezeAt (t);
            auto img = a.createComponentSnapshot (a.getLocalBounds());
            auto f = dir.getChildFile ("claw-" + juce::String (size.x) + "-" + juce::String (frame++).paddedLeft ('0', 3) + ".png");
            juce::FileOutputStream out (f);
            if (out.openedOk()) { out.setPosition (0); out.truncate(); juce::PNGImageFormat().writeImageToStream (img, out); }
        }
        for (double bt = 0.0; bt < SeparationAnimation::burstLength; bt += 0.1)
        {
            a.freezeAt (2.2, bt, colours);
            auto img = a.createComponentSnapshot (a.getLocalBounds());
            auto f = dir.getChildFile ("claw-" + juce::String (size.x) + "-" + juce::String (frame++).paddedLeft ('0', 3) + ".png");
            juce::FileOutputStream out (f);
            if (out.openedOk()) { out.setPosition (0); out.truncate(); juce::PNGImageFormat().writeImageToStream (img, out); }
        }
    }
    std::cout << "  wrote frames to " << dir.getFullPathName() << "\n";
}

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

    // the new tool tabs
    {
        for (int i = 0; i < 100 && p->jobs.isBusy(); ++i) pump (50);   // key detection
        PadParams shaped; shaped.reverse = true; shaped.filter = -0.35f; shaped.semitones = -5.0f; shaped.releaseMs = 180.0f;
        soul->setPad (4, shaped);
        studio.getPads().onSliceSelected (4);
        p->stopPreview();
        studio.setDeck (1);
        pump (150);
        savePng (*ed, dir.getChildFile ("2b-studio-pads.png"));

        soul->fx.lofiOn = true; soul->fx.reverbOn = true; soul->fx.delayOn = true;
        studio.getFxDeck().setClip (soul);
        studio.setDeck (2);
        pump (150);
        savePng (*ed, dir.getChildFile ("2c-studio-fx.png"));
        soul->fx = {};
        studio.getFxDeck().setClip (soul);

        studio.setDeck (3);
        studio.getMidiDeck().setRange (0, -1);   // the whole loop
        studio.getMidiDeck().findNotes();
        for (int i = 0; i < 400 && studio.getMidiDeck().isAnalysing(); ++i) pump (50);
        pump (150);
        savePng (*ed, dir.getChildFile ("2d-studio-midi.png"));
        studio.setDeck (0);
        soul->pads.clear();
    }

    ed->showTab (Tab::stems);
    pump (200);
    savePng (*ed, dir.getChildFile ("3-stems.png"));

    // the claw animation that plays while stems are being separated, and its burst into the stems
    {
        auto& stemsPage = ed->getStemsPage();
        auto& claw = stemsPage.getClaw();
        claw.setStatus ("Separating (2 of 4) on 8 cores", 0.43f);
        claw.freezeAt (1.9);
        pump (50);
        savePng (*ed, dir.getChildFile ("3b-stems-separating.png"));
        juce::Array<juce::Colour> colours;
        for (auto* n : { "vocals", "drums", "bass", "other" })
            colours.add (theme::stemColour (n));
        claw.freezeAt (2.2, 0.55, colours);
        if (claw.onExplodeProgress) claw.onExplodeProgress (0.55f / (float) SeparationAnimation::burstLength);
        pump (50);
        savePng (*ed, dir.getChildFile ("3c-stems-burst.png"));
        claw.stop();
        pump (50);
    }

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
    ed->showTab (Tab::studio);
    for (int d = 0; d < 4; ++d)
    {
        studio.setDeck (d);
        pump (100);
        savePng (*ed, dir.getChildFile ("8-studio-min-size-" + juce::String (d) + ".png"));
    }
    studio.setDeck (0);

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
    testVocalRemoval();
    testKeyDetect();
    testPads();
    testFx();
    testAudioToMidi();
    testStemParts();
    snag::StudioPageTester::run();
    testKnobTyping();
    testClawAnimation();
    testSamplerSound();
    StudioDecksTester::run();
    testProcessAudio();
    if (args.contains ("--network"))
        testNetwork();
    if (args.contains ("--ai"))
        testBuiltinAi();

    const int bpIdx = args.indexOf ("--dump-basic-pitch");
    if (bpIdx >= 0 && bpIdx + 2 < args.size())
    {
        auto loaded = audioio::loadFile (juce::File (args[bpIdx + 1]), {});
        midi::Posteriors p; juce::String err;
        if (loaded.audio != nullptr && midi::analyse (*loaded.audio, p, err))
        {
            const juce::File outFile (args[bpIdx + 2]);
            juce::FileOutputStream o (outFile);
            o.setPosition (0); o.truncate();
            o.writeInt (p.frames);
            o.write (p.note.data(), p.note.size() * 4); o.write (p.onset.data(), p.onset.size() * 4); o.write (p.contour.data(), p.contour.size() * 4);
            auto notes = midi::notesFrom (p, {});
            o.writeInt ((int) notes.size());
            for (auto& n : notes) { o.writeDouble (n.start); o.writeDouble (n.end); o.writeInt (n.pitch); o.writeFloat (n.velocity); }
            std::cout << "dumped " << p.frames << " frames, " << notes.size() << " notes\n";
        }
        else std::cout << "basic pitch failed: " << err << "\n";
        return 0;
    }

    const int clawIdx = args.indexOf ("--claw");
    if (clawIdx >= 0 && clawIdx + 1 < args.size())
        renderClawFrames (juce::File (args[clawIdx + 1]));

    const int shotIdx = args.indexOf ("--shots");
    if (shotIdx >= 0 && shotIdx + 1 < args.size())
        renderScreens (juce::File (args[shotIdx + 1]));

    tmp.deleteRecursively();
    std::cout << "\n" << passes << " passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
