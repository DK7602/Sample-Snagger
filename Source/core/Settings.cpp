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
        auto d = tempDir().getChildFile ("Drag");
        d.createDirectory();
        return d;
    }

    juce::File defaultLibraryDir()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("Sample Snagger").getChildFile ("Samples");
    }

    juce::String safeFileName (const juce::String& name, int maxLen)
    {
        auto s = juce::File::createLegalFileName (name.trim()).replaceCharacters ("\\/:*?\"<>|", "_________");
        s = s.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-().,&'!#+=[]")
             .trim();
        if (s.isEmpty())
            s = "Sample";
        return s.substring (0, maxLen).trim();
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
