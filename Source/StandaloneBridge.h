#pragma once

/** Tiny bridge to the JUCE standalone-app wrapper (audio settings, input mute).
    Kept in one .cpp so the heavy standalone header is included exactly once. */
namespace snag::standalone
{
    bool isStandalone();
    void unmuteInput();
    void showAudioSettings();
}
