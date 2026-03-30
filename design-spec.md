# Genera — Design Specification

## Overview
Genera is a generative note and chord sequencer MIDI FX for Ableton Move.
It produces evolving melodic patterns with scale-quantized notes, chord generation,
rhythmic stutter, octave variation, and a capture/loop feature.

- **Module ID:** `genera`
- **Component type:** `midi_fx`
- **API version:** 1 (midi_fx_api_v1)

## Parameters

### Page 1: Genera (8 knobs)

| # | Key       | Label    | Type | Range / Options | Default | Description |
|---|-----------|----------|------|-----------------|---------|-------------|
| 1 | root      | Root     | enum | C,C#,D,D#,E,F,F#,G,G#,A,A#,B | C | Root note |
| 2 | scale     | Scale    | enum | 20 scales (see below) | Major | Scale selection |
| 3 | steps     | Steps    | int  | 1-64 | 16 | Sequence length |
| 4 | capture   | Capture  | int  | 0-64 | 0 | 0=off, 1-64=capture last N steps as looping co-sequence |
| 5 | evolve    | Evolve   | int  | 0-100 | 20 | % chance of note variation per step |
| 6 | stutter   | Stutter  | int  | 0-100 | 0 | % of random extra rhythm hits superposed |
| 7 | octaves   | Octaves  | int  | 0-100 | 0 | % chance of random octave shift (+/-1, +/-2) |
| 8 | chord     | Chord    | int  | 0-100 | 0 | % chance of outputting a chord (max 6 notes) |

### Page 2: Sync (5 params, 8 knob slots)

| # | Key       | Label    | Type | Range / Options | Default | Description |
|---|-----------|----------|------|-----------------|---------|-------------|
| 1 | sync      | Sync     | enum | Internal, Move | Move | Clock source |
| 2 | tempo     | Tempo    | int  | 20-500 | 120 | BPM (Internal mode only) |
| 3 | division  | Division | enum | 18 options (see below) | 1/8 | Time division |
| 4 | vel_mode  | Vel Mode | enum | Fixed, Random, Moving | Fixed | Velocity behavior |
| 5 | velocity  | Velocity | int  | 0-127 | 100 | Base velocity |
| 6 | gate      | Gate     | int  | 1-200 | 80 | Gate length (% of step) |
| 7 | — | — | — | — | — | (reserved) |
| 8 | — | — | — | — | — | (reserved) |

## Scales (20)

Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian,
Harmonic Min, Melodic Min, Pentatonic Maj, Pentatonic Min, Blues,
Whole Tone, Diminished, Augmented, Chromatic, Hungarian Min, Arabic, Japanese

## Time Divisions (18)

1/1, 1/1T, 1/1D, 1/2, 1/2T, 1/2D, 1/4, 1/4T, 1/4D,
1/8, 1/8T, 1/8D, 1/16, 1/16T, 1/16D, 1/32, 1/32T, 1/32D

## Chord Generation

- When chord triggers (chord% chance), build a chord from the current scale
- Chord types: triad (3 notes), 7th (4 notes), 9th (5 notes), 6-note extended
- Distribution weighted toward triads: ~50% triad, ~25% 7th, ~15% 9th, ~10% 6-note
- Max polyphony: 6 simultaneous notes per chord
- All chord notes get the same velocity

## Capture Feature

- Capture = 0: off (normal generative mode)
- Capture = 1-64: captures the last N steps into a loop buffer
- The captured sequence loops continuously as a "co-sequence" on top of the main generative output
- Captured loop is affected by Stutter and Octaves parameters
- Changing Capture value re-captures from the current step history

## Velocity Modes

- **Fixed**: All notes use the Velocity parameter value
- **Random**: Velocity = base +/- random variation (range scaled by velocity param)
- **Moving**: Velocity sweeps up and down in a triangle wave pattern (min 20, max = velocity param)

## Evolve Behavior

Each step, there's an `evolve%` chance that the note deviates from the base pattern:
- Pick a random scale degree offset (-3 to +3)
- Apply to the current note, keeping it within the scale
