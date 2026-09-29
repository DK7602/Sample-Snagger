#include "AudioFileIO.h"
#include "Process.h"
#include "Settings.h"

namespace snag::audioio
{

static const juce::StringArray& nativeExtensions()
{
    static const juce::StringArray exts { ".wav", ".wave", ".aif", ".aiff", ".aifc", ".flac", ".ogg", ".mp3"
                                         #if JUCE_MAC || JUCE_WINDOWS
                                          , ".m4a", ".aac", ".caf"
                                         #endif
                                         #if JUCE_WINDOWS
                                          , ".wma"
                                         #endif
                                        };
    return exts;
}

static const juce::StringArray& otherExtensions()
{
    static const juce::StringArray exts { ".mp4", ".m4v", ".mov", ".mkv", ".webm", ".avi", ".flv", ".wmv", ".3gp",
                                          ".opus", ".m4a", ".aac", ".wma", ".caf", ".ac3", ".mpg", ".mpeg", ".ts",
                                          ".mts", ".alac", ".ape", ".wv", ".amr", ".oga", ".mka", ".gif" };
    return exts;
}

juce::String importWildcard()
{
    juce::StringArray all;
    for (auto& e : nativeExtensions()) all.addIfNotAlreadyThere ("*" + e);
    for (auto& e : otherExtensions())  all.addIfNotAlreadyThere ("*" + e);
    return all.joinIntoString (";");
}

bool isNativeAudioFile (const juce::File& f)
{
    return nativeExtensions().contains (f.getFileExtension().toLowerCase());
}

bool isImportable (const juce::File& f)
{
    auto e = f.getFileExtension().toLowerCase();
    return nativeExtensions().contains (e) || otherExtensions().contains (e);
}

//==============================================================================
static LoadResult readWithJuce (const juce::File& file, std::function<void (float)> progress,
                                std::function<bool()> shouldCancel)
{
    LoadResult r;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        r.error = "Unsupported audio format";
        return r;
    }

    const double sr = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    auto total = (juce::int64) reader->lengthInSamples;
    if (total <= 0)
    {
        r.error = "The file contains no audio";
        return r;
    }

    const auto maxLen = (juce::int64) (maxSeconds * sr);
    if (total > maxLen)
    {
        total = maxLen;
        r.truncated = true;
    }

    const int numCh = juce::jlimit (1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> buf (numCh, (int) total);
    buf.clear();

    const int block = 1 << 16;
    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        if (shouldCancel && shouldCancel())
        {
            r.error = "Cancelled";
            return r;
        }

        const int n = (int) juce::jmin ((juce::int64) block, total - pos);
        reader->read (&buf, (int) pos, n, pos, true, numCh > 1);
        if (progress)
            progress ((float) (pos + n) / (float) total);
    }

    // Collapse identical L/R into mono? No: keep stereo so stems/drag behave predictably.
    r.audio = AudioData::make (std::move (buf), sr);
    return r;
}

static LoadResult convertWithFfmpeg (const juce::File& file, const juce::File& ffmpeg,
                                     std::function<void (float)> progress, std::function<bool()> shouldCancel)
{
    LoadResult r;
    if (! ffmpeg.existsAsFile())
    {
        r.error = "This file type needs FFmpeg. Open Settings (gear icon) and click Install next to FFmpeg.";
        return r;
    }

    auto tmp = uniqueFile (paths::tempDir(), "convert_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));

    juce::StringArray args { ffmpeg.getFullPathName(), "-hide_banner", "-nostdin", "-y",
                             "-i", file.getFullPathName(),
                             "-vn", "-map", "0:a:0?", "-ac", "2",
                             "-t", juce::String ((int) maxSeconds),
                             "-c:a", "pcm_f32le", "-f", "wav", tmp.getFullPathName() };

    double durationSecs = 0.0;
    ProcessRunner runner;
    juce::StringArray tail;

    auto parseTime = [] (const juce::String& t) -> double
    {
        auto parts = juce::StringArray::fromTokens (t.trim(), ":", {});
        if (parts.size() != 3) return 0.0;
        return parts[0].getDoubleValue() * 3600.0 + parts[1].getDoubleValue() * 60.0 + parts[2].getDoubleValue();
    };

    const int code = runner.run (args, [&] (const juce::String& line)
    {
        tail.add (line);
        if (tail.size() > 8) tail.remove (0);

        if (line.contains ("Duration:"))
            durationSecs = parseTime (line.fromFirstOccurrenceOf ("Duration:", false, false).upToFirstOccurrenceOf (",", false, false));
        else if (line.contains ("time=") && durationSecs > 0 && progress)
            progress ((float) juce::jlimit (0.0, 1.0, parseTime (line.fromFirstOccurrenceOf ("time=", false, false).upToFirstOccurrenceOf (" ", false, false)) / durationSecs) * 0.8f);
    }, 30 * 60 * 1000, shouldCancel);

    if (code == -2)
    {
        tmp.deleteFile();
        r.error = "Cancelled";
        return r;
    }

    if (code != 0 || ! tmp.existsAsFile())
    {
        tmp.deleteFile();
        r.error = "FFmpeg could not read this file";
        for (auto& l : tail)
            if (l.containsIgnoreCase ("error") || l.containsIgnoreCase ("invalid") || l.containsIgnoreCase ("does not contain"))
                r.error << ": " << l.trim();
        if (tail.joinIntoString (" ").containsIgnoreCase ("matches no streams") || tail.joinIntoString (" ").containsIgnoreCase ("does not contain any stream"))
            r.error = "This file has no audio track";
        return r;
    }

    r = readWithJuce (tmp, [progress] (float p) { if (progress) progress (0.8f + p * 0.2f); }, shouldCancel);
    tmp.deleteFile();
    return r;
}

LoadResult loadFile (const juce::File& file, const juce::File& ffmpeg,
                     std::function<void (float)> progress, std::function<bool()> shouldCancel)
{
    if (! file.existsAsFile())
        return { nullptr, "File not found: " + file.getFullPathName(), false };

    if (isNativeAudioFile (file))
    {
        auto r = readWithJuce (file, progress, shouldCancel);
        if (r.audio != nullptr || r.error == "Cancelled")
            return r;
    }

    return convertWithFfmpeg (file, ffmpeg, progress, shouldCancel);
}

//==============================================================================
bool writeWav (const AudioData& audio, const juce::File& dest, int bitDepth, int startSample, int numSamples)
{
    if (numSamples < 0)
        numSamples = audio.getNumSamples() - startSample;

    startSample = juce::jlimit (0, audio.getNumSamples(), startSample);
    numSamples  = juce::jlimit (0, audio.getNumSamples() - startSample, numSamples);

    dest.getParentDirectory().createDirectory();
    dest.deleteFile();

    std::unique_ptr<juce::OutputStream> out (dest.createOutputStream());
    if (out == nullptr)
        return false;

    juce::WavAudioFormat wav;
    const int bits = bitDepth == 16 ? 16 : (bitDepth == 32 ? 32 : 24);
    const auto options = juce::AudioFormatWriterOptions{}
                            .withSampleRate (audio.sampleRate)
                            .withNumChannels (audio.getNumChannels())
                            .withBitsPerSample (bits)
                            .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                          : juce::AudioFormatWriterOptions::SampleFormat::integral);

    auto writer = wav.createWriterFor (out, options);   // takes ownership of the stream on success
    if (writer == nullptr)
        return false;

    return writer->writeFromAudioSampleBuffer (audio.buffer, startSample, numSamples);
}

juce::File uniqueFile (const juce::File& dir, const juce::String& name, const juce::String& ext)
{
    dir.createDirectory();
    auto base = paths::safeFileName (name);
    auto f = dir.getChildFile (base + ext);
    for (int i = 2; f.exists(); ++i)
        f = dir.getChildFile (base + " (" + juce::String (i) + ")" + ext);
    return f;
}

juce::File writeDragFile (const AudioData& audio, const juce::String& name, int bitDepth, int startSample, int numSamples)
{
    auto dir = paths::dragExportDir();

    // Keep the drag folder tidy: remove exports older than a week.
    for (auto& old : dir.findChildFiles (juce::File::findFiles, false, "*.wav;*.mid"))
        if (old.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days (7))
            old.deleteFile();

    auto f = uniqueFile (dir, name);
    if (writeWav (audio, f, bitDepth, startSample, numSamples))
        return f;
    return {};
}

} // namespace snag::audioio
