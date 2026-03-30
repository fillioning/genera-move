# CLAUDE.md — Genera

## What is this?
Genera is a generative note and chord sequencer (midi_fx) for Ableton Move via Schwung.
It produces evolving melodic patterns quantized to scales, with chord generation,
rhythmic stutter, octave variation, and a capture/loop co-sequence feature.

## Architecture
- Single C file: `genera.c`
- API: `midi_fx_api_v1_t`, entry point `move_midi_fx_init`
- No audio processing — MIDI output only via host callback
- Two UI pages: Genera (main), Sync (timing/velocity)

## Key Parameters
- **Root/Scale**: Chromatic root + 20 scale types
- **Steps**: 1-64 sequence length
- **Capture**: Loop last N steps as co-sequence (0=off)
- **Evolve**: % note variation
- **Stutter**: % random rhythm overlay
- **Octaves**: % random octave shift
- **Chord**: % chance of chord output (max 6-note polyphony)
- **Sync**: Internal BPM or Move clock
- **Division**: 18 time divisions (straight/triplet/dotted)
- **Velocity Mode**: Fixed, Random, Moving (triangle wave)

## Build
```bash
./build-module.sh    # Docker cross-compile for ARM64
./install.sh         # Deploy to Move via SSH
```

## Invariants
- Never use rand() — seeded LCG only
- Never allocate in tick() or process_midi()
- Always emit all-notes-off in destroy_instance()
- Handle transport: 0xFA (start), 0xFB (continue), 0xFC (stop), 0xF8 (clock)
- Return -1 from get_param for unknown keys
- api_version = 1 in module.json
- Install path: modules/midi_fx/genera/
