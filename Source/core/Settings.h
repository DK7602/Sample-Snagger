#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace snag
{

//==============================================================================
namespace paths
{
    /** ~/Library/Application Support/Sample Snagger, %APPDATA%\Sample Snagger, ~/.config/Sample Snagger */
    juce::File appDataDir();
    juce::File toolsDir();
    juce::File sessionsDir();
    juce::File tempDir();          // downloads, conversions, drag exports
    juce::File dragExportDir();
    juce::File webDataDir();
    juce::File defaultLibraryDir(); // ~/Documents/Sample Snagger/Samples

    /** Returns a unique, filesystem-safe version of a name. */
    juce::String safeFileName (const juce::String& name, int maxLen = 80);
}

//==============================================================================
/** App-wide settings, shared by every plug-in instance (use via juce::SharedResourcePointer). */
class Settings
{
public:
    Settings();
    ~Settings();

    juce::File getLibraryDir() const;
    void setLibraryDir (const juce::File&);

    juce::String getCookiesBrowser() const;        // "", "chrome", "firefox", "safari", "edge", "brave"
    void setCookiesBrowser (const juce::String&);

    int  getExportBitDepth() const;                 // 16, 24, 32 (float)
    void setExportBitDepth (int);

    juce::String getToolOverride (const juce::String& toolName) const;
    void setToolOverride (const juce::String& toolName, const juce::String& path);

    juce::String getString (const juce::String& key, const juce::String& def = {}) const;
    void setString (const juce::String& key, const juce::String& value);

    juce::PropertiesFile& getProps() { return *props; }

private:
    std::unique_ptr<juce::PropertiesFile> props;
    juce::InterProcessLock lock { "SampleSnaggerSettings" };
};

} // namespace snag
