#pragma once

#include "AudioData.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <vector>

namespace snag::midi
{

/** One transcribed note. */
struct Note
{
    double start = 0, end = 0;     // seconds
    int pitch = 60;                // MIDI note (60 = middle C)
    float velocity = 0.8f;         // 0..1
};

struct Options
{
    float sensitivity = 0.5f;      // 0 = only clear notes .. 1 = catch quiet ones too
    float minNoteMs = 128.0f;      // shorter notes are dropped
    bool melodyOnly = false;       // one note at a time (the strongest), e.g. for a vocal or lead line
    int lowest = 21, highest = 108;
};

/** What the network hears: per-frame likelihoods (86 frames a second). */
struct Posteriors
{
    int frames = 0;
    std::vector<float> note, onset;      // frames x 88 (A0..C8)
    std::vector<float> contour;          // frames x 264 (1/3 semitones)
};

/** Runs Spotify's Basic Pitch network (built in, Apache 2.0) over the audio, on all cores. */
bool analyse (const AudioData&, Posteriors& out, juce::String& error,
              std::function<void (float)> progress = {}, std::function<bool()> shouldCancel = {});

/** Turns the network's output into notes (Basic Pitch's own note-tracking rules). */
std::vector<Note> notesFrom (const Posteriors&, const Options&);

/** Convenience: analyse + notesFrom. */
std::vector<Note> transcribe (const AudioData&, const Options&, juce::String& error,
                              std::function<void (float)> progress = {}, std::function<bool()> shouldCancel = {});

/** A standard MIDI file (type 1, 960 ticks a beat) with the notes placed at `bpm`. */
juce::MidiFile toMidiFile (const std::vector<Note>&, double bpm, const juce::String& trackName);
bool writeMidiFile (const std::vector<Note>&, double bpm, const juce::String& trackName, const juce::File&);

/** A quick electric-piano-ish rendering of the notes, to hear what was found. */
AudioData::Ptr renderNotes (const std::vector<Note>&, double sampleRate, double lengthSeconds);

juce::String noteRangeText (const std::vector<Note>&);

} // namespace snag::midi
