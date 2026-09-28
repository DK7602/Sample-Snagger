#include "StandaloneBridge.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#if defined (JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace snag::standalone
{
    bool isStandalone()
    {
       #if defined (JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
        return juce::StandalonePluginHolder::getInstance() != nullptr;
       #else
        return false;
       #endif
    }

    void unmuteInput()
    {
       #if defined (JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
        // Sample Snagger never passes its input to the output, so there's no feedback risk.
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
       #endif
    }

    void showAudioSettings()
    {
       #if defined (JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->showAudioSettingsDialog();
       #endif
    }
}
