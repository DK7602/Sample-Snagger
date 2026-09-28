#pragma once

#include "Theme.h"
#include <functional>
#include <vector>

namespace snag
{

//==============================================================================
/** Plays while stems are being separated: the gold claw comes down, clutches a section of the
    yellow sound track, the section glows red and the claw pulls it out - on repeat. When the stems
    are ready the track bursts into pieces (in the stems' colours) and the stem lanes appear.

    Everything is drawn from vectors relative to the component's size, so it scales with the
    window. The glowing track is rendered once per size into two cached images (yellow and red);
    each frame only clips, moves and blits them, and draws the claw. */
class SeparationAnimation : public juce::Component, private juce::Timer
{
public:
    SeparationAnimation();

    void start();                                           // begin looping (shows the component)
    void explode (const juce::Array<juce::Colour>& stemColours);   // stems are in: burst, then hide
    void stop();                                            // hide straight away (cancelled / failed)
    bool isLooping() const noexcept     { return state == State::looping; }
    bool isActive() const noexcept      { return state != State::idle; }
    bool isFrozen() const noexcept      { return state != State::idle && frozenLoop >= 0.0; }

    void setStatus (const juce::String& text, float progress);   // progress 0..1, < 0 = unknown

    /** True while the window is mid-resize and the cached glow is being stretched rather than redrawn. */
    bool isStretchingForResize() const noexcept   { return ! imageMap.isIdentity() || ! backdropMap.isIdentity(); }

    /** Called as the burst runs (0..1, always ending with 1) - the page fades the stem lanes in with it. */
    std::function<void (float)> onExplodeProgress;

    /** For screenshots / tests: show one fixed moment (burstSeconds >= 0: that far into the burst). */
    void freezeAt (double loopSeconds, double burstSeconds = -1.0,
                   const juce::Array<juce::Colour>& stemColours = {});

    static constexpr double loopLength  = 4.2;
    static constexpr double burstLength = 1.15;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class State { idle, looping, exploding };

    struct Geo
    {
        juce::Rectangle<float> window, scene, status;
        float C = 0, L = 0, A = 0;          // claw height, talon length, track amplitude
        float cx = 0, trackY = 0, x0 = 0, x1 = 0, secX0 = 0, secX1 = 0;
        float grabHubY = 0, hiddenHubY = 0, awayHubY = 0;
        juce::Rectangle<float> section() const { return { secX0, trackY - A * 1.9f, secX1 - secX0, A * 3.8f }; }
    };

    struct Pose
    {
        float hubY = 0, open = 1, red = 0;
        bool holding = false;               // the claw has the section (drawn at the claw, not on the track)
        bool gap = false;                   // the section is missing from the track
        float refill = 0;                   // how much of the gap has grown back (0..1)
        float lift = 0;                     // how far the held section has moved up from the track
        float sway = 0, tilt = 0;
        float edgeGlow = 0;                 // embers on the torn ends
    };

    struct Shard
    {
        juce::Path shape;                   // piece of the waveform, around (0, 0)
        juce::Point<float> origin, velocity;
        float spin = 0;
        juce::Colour colour;
        float delay = 0;
    };

    struct Spark
    {
        juce::Point<float> origin, velocity;
        float life = 0, width = 1;
        juce::Colour colour;
    };

    void timerCallback() override;
    double loopTime() const;
    double burstTime() const;

    Geo geometry() const;
    Pose poseAt (double t, const Geo&) const;
    void buildBars (const Geo&);
    void buildShards (const Geo&, const juce::Array<juce::Colour>& colours);
    void ensureImages (const Geo&, float scale);
    juce::Path wavePath (const Geo&, int from, int to) const;
    float barX (const Geo&, int i) const;

    void drawTrackPart (juce::Graphics&, const juce::Image&, juce::Rectangle<float> clip,
                        float alpha, juce::AffineTransform move = {}, bool excludeClip = false) const;
    void drawScene (juce::Graphics&, const Geo&, const Pose&, double t) const;
    void drawClawBody (juce::Graphics&, const Geo&, float hubY, float open) const;
    void drawTalons (juce::Graphics&, const Geo&, float hubY, float open, float red,
                     juce::Point<float> glowCentre) const;
    void drawSparks (juce::Graphics&, const Geo&, double t) const;
    void drawBurst (juce::Graphics&, const Geo&, double bt) const;
    void drawStatus (juce::Graphics&, const Geo&, float alpha) const;

    State state = State::idle;
    double startMs = 0.0, burstStartMs = 0.0, burstLoopTime = 0.0;
    double frozenLoop = -1.0, frozenBurst = -1.0;
    bool burstFromGap = false;

    std::vector<float> bars;                // waveform peaks 0..1
    int grab0 = 0, grab1 = 0;               // first / last peak the claw takes
    std::vector<Shard> shards;
    std::vector<Spark> burstSparks;

    juce::Image yellowTrack, redTrack;      // the glowing track, pre-rendered at physical resolution
    juce::Image backdrop;                   // the glass window behind the scene
    float imageScale = 0.0f;
    juce::Rectangle<int> imageSize;
    juce::Point<float> imageOrigin;         // where the images' top-left sits, in component coordinates
    float imageX0 = 0, imageX1 = 1, imageTrackY = 0, imageA = 1;   // the layout they were drawn for
    juce::AffineTransform imageMap, backdropMap;                  // stretch them while the window is being resized
    double lastResizeMs = 0.0;

    juce::String status;
    float progress = -1.0f;
};

} // namespace snag
