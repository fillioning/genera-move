# CLAUDE.md — Genera

## What is this?
Genera is a generative note and chord sequencer (midi_fx) for Ableton Move via Schwung.
It produces evolving melodic patterns quantized to scales, with chord generation,
a chaotic stutter engine with beat-repeat, octave variation, humanize, and capture/loop.

## Architecture
- Single C file: `genera.c`
- API: `midi_fx_api_v1_t`, entry point `move_midi_fx_init`
- No audio processing — MIDI output only via host callback
- Two UI pages: Genera (8 knobs + Scale/Gen Mode menu), Sync (8 knobs)
- Deterministic seeded LCG — no rand()

## Key Parameters
- **Root/Scale**: Chromatic root + 20 scale types
- **Gen Mode**: Up, Down, Up/Down, Down/Up, Exclude, Walk, Random
- **Steps**: 1-64 sequence length
- **Capture**: Loop last N steps as co-sequence (0=off)
- **Evolve**: % note deviation per step
- **Stutter**: Off-grid chaotic notes + beat-repeat bursts (Chaos/Timed modes)
- **Octaves**: % random octave shift (also controls stutter octave range ±1 to ±3)
- **Chord**: % chance of chord output (max 6-note polyphony)
- **Velocity Mode**: Human (default, ±20%), Fixed, Random, Moving (triangle)
- **Humanize**: Timing jitter (±15% of step interval at 100%)
- **Division**: 18 time divisions ordered slow→fast (dotted/normal/triplet)
- **Sync**: Internal BPM (20-500) or Move MIDI clock

## Build
```bash
./build-module.sh    # Docker cross-compile for ARM64
./install.sh         # Deploy to Move via SSH
```

## Invariants
- Never use rand() — seeded LCG only (rng_state for main, stutter_rng for stutter)
- Never allocate in tick() or process_midi()
- Always emit all-notes-off in destroy_instance()
- Handle transport: 0xFA (start), 0xFB (continue), 0xFC (stop), 0xF8 (clock)
- Return -1 from get_param for unknown keys
- api_version = 1 in module.json
- Install path: modules/midi_fx/genera/
- Stutter floor: never go below 1 octave under sequence root
- RNG re-seeded on each transport start (Random mode varies each play)
