#include "Settings.h"

namespace snag
{

namespace paths
{
    juce::File appDataDir()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        auto dir = base.getChildFile ("Application Support").getChildFile ("Sample Snagger");
       #else
        auto dir = base.getChildFile ("Sample Snagger");
       #endif
        dir.createDirectory();
        return dir;
    }

    static juce::File sub (const char* name)
    {
        auto d = appDataDir().getChildFile (name);
        d.createDirectory();
        return d;
    }

    juce::File toolsDir()       { return sub ("Tools"); }
    juce::File sessionsDir()    { return sub ("Sessions"); }
    juce::File tempDir()        { return sub ("Temp"); }
    juce::File webDataDir()     { return sub ("WebView"); }

    juce::File dragExportDir()
    {
        // A DAW usually keeps pointing at a dropped file where it lies. So these live with the
        // user's other samples (not in a temp folder) and are never cleaned up behind their back.
        auto d = defaultLibraryDir().getSiblingFile ("Dragged to DAW");
        if (d.createDirectory().failed())
        {
            d = appDataDir().getChildFile ("Dragged to DAW");
            d.createDirectory();
        }
        return d;
    }

    juce::File defaultLibraryDir()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("Sample Snagger").getChildFile ("Samples");
    }

    juce::String safeFileName (const juce::String& name, int maxLen)
    {
        // Keep the name as it reads - accents, Japanese, Cyrillic and all - and only take out what
        // a file system can't have: path characters, control characters, leading / trailing dots.
        juce::String s;
        for (auto t = name.getCharPointer(); ! t.isEmpty();)
        {
            const auto c = t.getAndAdvance();
            if (c < 32 || c == 127)
                continue;
            s << (juce::String ("\\/:*?\"<>|").containsChar (c) ? (juce::juce_wchar) '_' : c);
        }

        auto tidy = [] (juce::String t)
        {
            t = t.trim();
            while (t.startsWithChar ('.'))                        t = t.substring (1).trimStart();   // no hidden files
            while (t.endsWithChar ('.') || t.endsWithChar (' '))  t = t.dropLastCharacters (1);      // Windows refuses these
            return t;
        };
        s = tidy (s);

        // A long name keeps its ending too: that's where " - Vocals", " - chop 3" or " (cut)" is.
        maxLen = juce::jmax (16, maxLen);
        if (s.length() > maxLen)
        {
            const int tail = juce::jmin (24, maxLen / 3);
            s = tidy (s.substring (0, maxLen - tail - 2)) + ".." + tidy (s.substring (s.length() - tail));
        }
        while (s.getNumBytesAsUTF8() > 200 && s.length() > 1)     // most file systems allow 255 bytes
            s = tidy (s.substring (0, s.length() / 2 - 1) + s.substring (s.length() / 2 + 1));

        // names Windows keeps for devices ("con.wav" can't be made there)
        const auto stem = s.upToFirstOccurrenceOf (".", false, false).trim().toUpperCase();
        for (auto* reserved : { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" })
            if (stem == reserved)
            {
                s = "_" + s;
                break;
            }

        return s.isEmpty() ? juce::String ("Sample") : s;
    }
}

//==============================================================================
Settings::Settings()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "settings";
    o.filenameSuffix      = ".xml";
    o.folderName          = {};
    o.osxLibrarySubFolder = "Application Support";
    o.processLock         = &lock;
    o.storageFormat       = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 500;

    props = std::make_unique<juce::PropertiesFile> (paths::appDataDir().getChildFile ("settings.xml"), o);
}

Settings::~Settings()
{
    props->saveIfNeeded();
}

juce::File Settings::getLibraryDir() const
{
    auto p = props->getValue ("libraryDir");
    juce::File f = p.isNotEmpty() && juce::File::isAbsolutePath (p) ? juce::File (p) : paths::defaultLibraryDir();
    f.createDirectory();
    return f;
}

void Settings::setLibraryDir (const juce::File& f)       { props->setValue ("libraryDir", f.getFullPathName()); }

juce::String Settings::getCookiesBrowser() const          { return props->getValue ("cookiesBrowser", ""); }
void Settings::setCookiesBrowser (const juce::String& s)  { props->setValue ("cookiesBrowser", s); }

int Settings::getExportBitDepth() const                   { return props->getIntValue ("exportBits", 24); }
void Settings::setExportBitDepth (int b)                  { props->setValue ("exportBits", b); }

juce::String Settings::getToolOverride (const juce::String& n) const          { return props->getValue ("tool_" + n); }
void Settings::setToolOverride (const juce::String& n, const juce::String& p) { props->setValue ("tool_" + n, p); }

juce::String Settings::getString (const juce::String& k, const juce::String& d) const { return props->getValue (k, d); }
void Settings::setString (const juce::String& k, const juce::String& v)               { props->setValue (k, v); }

} // namespace snag
