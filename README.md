# Genera

**Generative note and chord sequencer for Ableton Move**

A MIDI FX module for [Schwung](https://github.com/charlesvestal/schwung) that produces evolving melodic patterns quantized to musical scales. Genera creates sequences that breathe and mutate — from precise ascending runs to chaotic stutter bursts with beat-repeat.

## Features

- **20 scales** — Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian, Harmonic Minor, Melodic Minor, Pentatonic Major/Minor, Blues, Whole Tone, Diminished, Augmented, Chromatic, Hungarian Minor, Arabic, Japanese
- **7 generation modes** — Up, Down, Up/Down, Down/Up, Exclude, Walk (drunk walk), Random
- **Stutter engine** — Independent off-grid chaotic rhythm generator with beat-repeat bursts. Two modes: Chaos (wild timing) and Timed (musical subdivisions)
- **Chord generation** — Probabilistic chord stacking (triads through 6-note voicings)
- **Capture/loop** — Record the last N steps as a co-sequence that plays alongside new generation
- **Evolve** — Probabilistic note deviation that makes patterns drift over time
- **Octave shifts** — Random octave transposition with configurable range (also controls stutter octave range)
- **4 velocity modes** — Human (default, +/-20% variation), Fixed, Random (full range), Moving (triangle wave tied to step count)
- **Humanize** — Timing jitter for organic feel (up to +/-15% of step interval)
- **18 time divisions** — Straight, dotted, and triplet from whole notes to 32nd notes, ordered slow to fast
- **Sync** — Internal BPM (20-500) or Move MIDI clock

## Parameters

### Genera Page (8 knobs + menu)

| Knob | Parameter | Range | Default | Description |
|------|-----------|-------|---------|-------------|
| 1 | Root | C-B | D | Scale root note |
| 2 | Division | 1/1D-1/32T | 1/4 | Time division (dotted/normal/triplet) |
| 3 | Steps | 1-64 | 8 | Sequence length in scale degrees |
| 4 | Capture | 0-64 | 0 | Loop last N steps as co-sequence (0=off) |
| 5 | Evolve | 0-100% | 0 | Chance of note deviation per step |
| 6 | Stutter | 0-100% | 0 | Off-grid chaotic note density + beat-repeat |
| 7 | Octaves | 0-100% | 0 | Random octave shift chance (also extends stutter range) |
| 8 | Chord | 0-100% | 0 | Chord generation probability |

**Menu parameters** (jog wheel):
- **Scale** — 20 scale types
- **Gen Mode** — Up, Down, Up/Down, Down/Up, Exclude, Walk, Random

### Sync Page (8 knobs)

| Knob | Parameter | Range | Default | Description |
|------|-----------|-------|---------|-------------|
| 1 | Sync | Internal/Move | Move | Clock source |
| 2 | Tempo | 20-500 BPM | 120 | Internal tempo (ignored in Move sync) |
| 3 | Division | 1/1D-1/32T | 1/4 | Time division |
| 4 | Vel Mode | Human/Fixed/Random/Moving | Human | Velocity generation mode |
| 5 | Velocity | 0-127 | 100 | Base velocity |
| 6 | Gate | 1-200 | 80 | Note gate length (%) |
| 7 | Stut Mode | Chaos/Timed | Chaos | Stutter timing mode |
| 8 | Humanize | 0-100% | 0 | Timing jitter amount |

## Generation Modes

| Mode | Pattern | Description |
|------|---------|-------------|
| **Up** | 0,1,2,...,N | Ascending through scale degrees |
| **Down** | N,...,2,1,0 | Descending through scale degrees |
| **Up/Down** | 0,...,N,N,...,0 | Ascending then descending (repeats endpoints) |
| **Down/Up** | N,...,0,0,...,N | Descending then ascending (repeats endpoints) |
| **Exclude** | 0,...,N-1,...,1 | Up/Down without repeating the top note |
| **Walk** | drunk walk | Random +/-1 or +/-2 steps, clamped to range |
| **Random** | random | Random scale degree each step (re-seeded each play) |

## Stutter Engine

The stutter engine runs on its own independent clock, completely detached from the main sequencer grid. It has two layers:

1. **Off-grid notes** — Fires at random intervals between steps, creating polyrhythmic chaos (Chaos mode) or musical subdivisions (Timed mode)
2. **Beat-repeat bursts** — After any note fires, chance to rapid-fire repeat that same note 2-6 times with velocity decay (like a DJ beat-repeat)

The stutter respects the Octaves parameter for its octave range and never goes lower than one octave below the sequence root.

## Velocity Modes

| Mode | Behavior |
|------|----------|
| **Human** | +/-20% variation around base velocity (default) |
| **Fixed** | Exact base velocity every step |
| **Random** | Fully random 1-127 each step |
| **Moving** | Triangle wave tied to step count (crescendo/decrescendo) |

## Install

### From Module Store

Available via the Schwung Module Store (on-device or desktop installer).

### Manual Install

```bash
# Build
./build-module.sh

# Deploy to Move
./install.sh
```

Requires [Schwung](https://github.com/charlesvestal/schwung) installed on your Ableton Move.

## Architecture

- Single C file: `genera.c` (~1300 lines)
- API: `midi_fx_api_v1_t` via `move_midi_fx_init`
- No audio processing — MIDI output only
- Deterministic seeded LCG (no `rand()`)
- Zero heap allocation in tick/process paths

## License

MIT

## Author

Vincent Fillion
