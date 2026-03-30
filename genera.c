/*
 * Genera — Generative Note & Chord Sequencer for Schwung
 *
 * MIDI FX that produces evolving melodic patterns:
 *   - Scale-quantized note generation with 20 scales
 *   - Chord generation (triad to 6-note, up to 6 polyphony)
 *   - Capture: loop last N steps as co-sequence
 *   - Evolve: probabilistic note variation
 *   - Stutter: random rhythm overlay
 *   - Octaves: random octave transposition
 *   - Velocity modes: Fixed, Random, Moving (triangle)
 *   - Sync: Internal BPM or Move MIDI clock
 *
 * License: MIT
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "host/plugin_api_v1.h"
#include "host/midi_fx_api_v1.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * Constants
 * ══════════════════════════════════════════════════════════════════════════════ */

#define MAX_STEPS           64
#define MAX_CHORD_NOTES     6
#define MAX_ACTIVE_NOTES    128
#define DEFAULT_BPM         120
#define DEFAULT_SAMPLE_RATE 44100
#define BASE_MIDI_NOTE      48   /* C3 — center of generative range */
#define MIN_MIDI_NOTE       24   /* C1 */
#define MAX_MIDI_NOTE       108  /* C8 */

#define NUM_SCALES          20
#define NUM_DIVISIONS       18
#define NUM_ROOTS           12

/* ══════════════════════════════════════════════════════════════════════════════
 * Enums
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef enum { SYNC_INTERNAL = 0, SYNC_CLOCK } sync_mode_t;
typedef enum { VEL_FIXED = 0, VEL_RANDOM, VEL_MOVING } vel_mode_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * Scale definitions — intervals from root (semitones)
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *name;
    int len;
    int intervals[12];
} scale_def_t;

static const scale_def_t g_scales[NUM_SCALES] = {
    { "Major",       7, {0,2,4,5,7,9,11,0,0,0,0,0} },
    { "Minor",       7, {0,2,3,5,7,8,10,0,0,0,0,0} },
    { "Dorian",      7, {0,2,3,5,7,9,10,0,0,0,0,0} },
    { "Phrygian",    7, {0,1,3,5,7,8,10,0,0,0,0,0} },
    { "Lydian",      7, {0,2,4,6,7,9,11,0,0,0,0,0} },
    { "Mixolydian",  7, {0,2,4,5,7,9,10,0,0,0,0,0} },
    { "Aeolian",     7, {0,2,3,5,7,8,10,0,0,0,0,0} },
    { "Locrian",     7, {0,1,3,5,6,8,10,0,0,0,0,0} },
    { "Harm Min",    7, {0,2,3,5,7,8,11,0,0,0,0,0} },
    { "Melod Min",   7, {0,2,3,5,7,9,11,0,0,0,0,0} },
    { "Penta Maj",   5, {0,2,4,7,9,0,0,0,0,0,0,0} },
    { "Penta Min",   5, {0,3,5,7,10,0,0,0,0,0,0,0} },
    { "Blues",        6, {0,3,5,6,7,10,0,0,0,0,0,0} },
    { "Whole Tone",  6, {0,2,4,6,8,10,0,0,0,0,0,0} },
    { "Diminished",  8, {0,2,3,5,6,8,9,11,0,0,0,0} },
    { "Augmented",   6, {0,3,4,7,8,11,0,0,0,0,0,0} },
    { "Chromatic",  12, {0,1,2,3,4,5,6,7,8,9,10,11} },
    { "Hung Min",    7, {0,2,3,6,7,8,11,0,0,0,0,0} },
    { "Arabic",      7, {0,1,4,5,7,8,11,0,0,0,0,0} },
    { "Japanese",    5, {0,1,5,7,8,0,0,0,0,0,0,0} },
};

/* ══════════════════════════════════════════════════════════════════════════════
 * Time division definitions — notes per beat (as ratio: multiply BPM)
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *name;
    double notes_per_beat; /* how many steps fit in one beat */
} division_def_t;

/* T = triplet (3 in the space of 2), D = dotted (1.5x duration = 2/3 rate) */
static const division_def_t g_divisions[NUM_DIVISIONS] = {
    { "1/1",   0.25  },    /* whole note: 1 step per 4 beats */
    { "1/1T",  0.375 },    /* whole triplet */
    { "1/1D",  0.1667},    /* whole dotted */
    { "1/2",   0.5   },    /* half note */
    { "1/2T",  0.75  },    /* half triplet */
    { "1/2D",  0.3333},    /* half dotted */
    { "1/4",   1.0   },    /* quarter note */
    { "1/4T",  1.5   },    /* quarter triplet */
    { "1/4D",  0.6667},    /* quarter dotted */
    { "1/8",   2.0   },    /* eighth note (default) */
    { "1/8T",  3.0   },    /* eighth triplet */
    { "1/8D",  1.3333},    /* eighth dotted */
    { "1/16",  4.0   },    /* sixteenth */
    { "1/16T", 6.0   },    /* sixteenth triplet */
    { "1/16D", 2.6667},    /* sixteenth dotted */
    { "1/32",  8.0   },    /* thirty-second */
    { "1/32T", 12.0  },    /* thirty-second triplet */
    { "1/32D", 5.3333},    /* thirty-second dotted */
};

static const char *g_root_names[NUM_ROOTS] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static const char *g_vel_mode_names[3] = { "Fixed", "Random", "Moving" };
static const char *g_sync_names[2] = { "Internal", "Move" };

/* ══════════════════════════════════════════════════════════════════════════════
 * Helpers
 * ══════════════════════════════════════════════════════════════════════════════ */

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int appendf(char *buf, int buf_len, int *pos, const char *fmt, ...) {
    va_list ap; int n;
    if (!buf || !pos || *pos >= buf_len) return 0;
    va_start(ap, fmt);
    n = vsnprintf(buf + *pos, (size_t)(buf_len - *pos), fmt, ap);
    va_end(ap);
    if (n < 0 || *pos + n >= buf_len) return 0;
    *pos += n;
    return 1;
}

/* Seeded LCG — deterministic, no rand() */
static uint32_t lcg_next(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static int lcg_range(uint32_t *state, int lo, int hi) {
    if (lo >= hi) return lo;
    uint32_t r = lcg_next(state);
    return lo + (int)(r % (uint32_t)(hi - lo + 1));
}

static int lcg_chance(uint32_t *state, int pct) {
    if (pct <= 0) return 0;
    if (pct >= 100) return 1;
    return (int)(lcg_next(state) % 100u) < pct;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Music helpers
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Get the MIDI note for a scale degree relative to root */
static int scale_degree_to_midi(int root, int scale_idx, int degree) {
    const scale_def_t *sc = &g_scales[clamp_int(scale_idx, 0, NUM_SCALES - 1)];
    int octave = degree / sc->len;
    int idx = degree % sc->len;
    if (idx < 0) { idx += sc->len; octave--; }
    int note = BASE_MIDI_NOTE + root + sc->intervals[idx] + octave * 12;
    return clamp_int(note, MIN_MIDI_NOTE, MAX_MIDI_NOTE);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Captured step entry
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int degree;         /* scale degree (relative, can be negative) */
    int chord_size;     /* 0 = single note, 2-6 = chord */
    int chord_degrees[MAX_CHORD_NOTES]; /* extra degrees for chord notes */
    int velocity;
    int octave_shift;   /* octave transposition applied */
} step_entry_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * Active note tracking (for note-off scheduling)
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t note;
    int samples_left;
} active_note_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * Instance
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* Genera page params */
    int root;           /* 0-11 */
    int scale;          /* 0-19 */
    int steps;          /* 1-64 */
    int capture;        /* 0-64 */
    int evolve;         /* 0-100 */
    int stutter;        /* 0-100 */
    int octaves;        /* 0-100 */
    int chord;          /* 0-100 */

    /* Sync page params */
    sync_mode_t sync_mode;
    int tempo;          /* 20-500 BPM */
    int division;       /* 0-17 index into g_divisions */
    vel_mode_t vel_mode;
    int velocity;       /* 0-127 */
    int gate;           /* 1-200 % */

    /* Timing — internal */
    int sample_rate;
    double step_interval_f;
    double samples_until_step_f;
    int timing_dirty;

    /* Timing — external clock */
    int clock_running;
    int clock_counter;
    int clocks_per_step;
    int pending_step_triggers;

    /* Sequence state */
    uint64_t current_step;
    uint32_t rng_state;

    /* Step history (ring buffer for capture) */
    step_entry_t step_history[MAX_STEPS];
    int history_write;   /* next write position */
    int history_count;   /* how many valid entries */

    /* Capture loop */
    step_entry_t capture_buf[MAX_STEPS];
    int capture_len;     /* 0 = no capture active */
    int capture_pos;     /* current position in capture loop */

    /* Active notes for gate-off scheduling */
    active_note_t active_notes[MAX_ACTIVE_NOTES];
    int active_count;

    /* Velocity moving mode state */
    int vel_moving_pos;   /* 0-254 triangle wave position */
    int vel_moving_dir;   /* +1 or -1 */

    /* UI state */
    int current_level;   /* 0=root, 1=genera, 2=sync */

    /* Chain params cache */
    char chain_params_json[4096];
    int chain_params_len;
} genera_instance_t;

static const host_api_v1_t *g_host = NULL;

/* ══════════════════════════════════════════════════════════════════════════════
 * MIDI output helpers
 * ══════════════════════════════════════════════════════════════════════════════ */

static int emit3(uint8_t out[][3], int lens[], int max, int *cnt,
                 uint8_t b0, uint8_t b1, uint8_t b2) {
    if (*cnt >= max) return 0;
    out[*cnt][0] = b0;
    out[*cnt][1] = b1;
    out[*cnt][2] = b2;
    lens[*cnt] = 3;
    (*cnt)++;
    return 1;
}

static void emit_all_notes_off(genera_instance_t *inst,
                                uint8_t out[][3], int lens[], int max, int *cnt) {
    /* Send note-off for all active notes */
    for (int i = 0; i < inst->active_count && *cnt < max; i++) {
        emit3(out, lens, max, cnt, 0x80, inst->active_notes[i].note, 0);
    }
    inst->active_count = 0;
    /* CC 123 = All Notes Off */
    emit3(out, lens, max, cnt, 0xB0, 123, 0);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Note tracking
 * ══════════════════════════════════════════════════════════════════════════════ */

static void note_on(genera_instance_t *inst, uint8_t note, uint8_t vel, int gate_samples,
                    uint8_t out[][3], int lens[], int max, int *cnt) {
    /* Kill existing note-on for same pitch */
    for (int i = 0; i < inst->active_count; i++) {
        if (inst->active_notes[i].note == note) {
            emit3(out, lens, max, cnt, 0x80, note, 0);
            /* Remove from tracking */
            for (int j = i; j < inst->active_count - 1; j++)
                inst->active_notes[j] = inst->active_notes[j + 1];
            inst->active_count--;
            break;
        }
    }

    if (inst->active_count >= MAX_ACTIVE_NOTES) {
        /* Steal oldest */
        emit3(out, lens, max, cnt, 0x80, inst->active_notes[0].note, 0);
        for (int i = 0; i < inst->active_count - 1; i++)
            inst->active_notes[i] = inst->active_notes[i + 1];
        inst->active_count--;
    }

    emit3(out, lens, max, cnt, 0x90, note, vel);
    inst->active_notes[inst->active_count].note = note;
    inst->active_notes[inst->active_count].samples_left = gate_samples;
    inst->active_count++;
}

static void advance_note_timers(genera_instance_t *inst, int frames,
                                 uint8_t out[][3], int lens[], int max, int *cnt) {
    int i = 0;
    while (i < inst->active_count) {
        inst->active_notes[i].samples_left -= frames;
        if (inst->active_notes[i].samples_left <= 0) {
            emit3(out, lens, max, cnt, 0x80, inst->active_notes[i].note, 0);
            for (int j = i; j < inst->active_count - 1; j++)
                inst->active_notes[j] = inst->active_notes[j + 1];
            inst->active_count--;
        } else {
            i++;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Timing
 * ══════════════════════════════════════════════════════════════════════════════ */

static void recalc_internal_timing(genera_instance_t *inst, int sample_rate) {
    if (!inst || sample_rate <= 0) return;
    inst->sample_rate = sample_rate;
    double npb = g_divisions[clamp_int(inst->division, 0, NUM_DIVISIONS - 1)].notes_per_beat;
    if (npb <= 0.0) npb = 2.0;
    double bpm = (double)clamp_int(inst->tempo, 20, 500);
    double step_samples = ((double)sample_rate * 60.0) / (bpm * npb);
    if (step_samples < 1.0) step_samples = 1.0;
    inst->step_interval_f = step_samples;
    if (inst->samples_until_step_f <= 0.0 || inst->samples_until_step_f > step_samples)
        inst->samples_until_step_f = step_samples;
    inst->timing_dirty = 0;
}

static void recalc_clock_timing(genera_instance_t *inst) {
    if (!inst) return;
    double npb = g_divisions[clamp_int(inst->division, 0, NUM_DIVISIONS - 1)].notes_per_beat;
    if (npb <= 0.0) npb = 2.0;
    int clocks = (int)(24.0 / npb + 0.5);
    if (clocks < 1) clocks = 1;
    inst->clocks_per_step = clocks;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Velocity computation
 * ══════════════════════════════════════════════════════════════════════════════ */

static int compute_velocity(genera_instance_t *inst) {
    int base = clamp_int(inst->velocity, 1, 127);
    switch (inst->vel_mode) {
        case VEL_FIXED:
            return base;
        case VEL_RANDOM: {
            int range = base / 3;
            if (range < 5) range = 5;
            int offset = lcg_range(&inst->rng_state, -range, range);
            return clamp_int(base + offset, 1, 127);
        }
        case VEL_MOVING: {
            /* Triangle wave: position 0→127→0 */
            int vel_min = 20;
            int vel_max = base < vel_min ? vel_min : base;
            float t = (float)inst->vel_moving_pos / 127.0f;
            int vel = vel_min + (int)(t * (float)(vel_max - vel_min));
            /* Advance triangle */
            inst->vel_moving_pos += inst->vel_moving_dir * 4;
            if (inst->vel_moving_pos >= 127) {
                inst->vel_moving_pos = 127;
                inst->vel_moving_dir = -1;
            } else if (inst->vel_moving_pos <= 0) {
                inst->vel_moving_pos = 0;
                inst->vel_moving_dir = 1;
            }
            return clamp_int(vel, 1, 127);
        }
    }
    return base;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Step generation — core generative engine
 * ══════════════════════════════════════════════════════════════════════════════ */

static int compute_gate_samples(genera_instance_t *inst) {
    double base = inst->step_interval_f;
    if (base <= 0.0) {
        /* Estimate from BPM */
        int sr = inst->sample_rate > 0 ? inst->sample_rate : DEFAULT_SAMPLE_RATE;
        double npb = g_divisions[clamp_int(inst->division, 0, NUM_DIVISIONS - 1)].notes_per_beat;
        if (npb <= 0.0) npb = 2.0;
        base = ((double)sr * 60.0) / ((double)inst->tempo * npb);
    }
    int samples = (int)(base * (double)inst->gate / 100.0);
    if (samples < 64) samples = 64;
    return samples;
}

static void emit_step(genera_instance_t *inst, const step_entry_t *entry,
                      uint8_t out[][3], int lens[], int max, int *cnt) {
    int vel = entry->velocity;
    int gate = compute_gate_samples(inst);
    int root_note = scale_degree_to_midi(inst->root, inst->scale, entry->degree);

    /* Apply octave shift */
    root_note += entry->octave_shift * 12;
    root_note = clamp_int(root_note, MIN_MIDI_NOTE, MAX_MIDI_NOTE);

    note_on(inst, (uint8_t)root_note, (uint8_t)vel, gate, out, lens, max, cnt);

    /* Chord notes */
    for (int i = 0; i < entry->chord_size && i < MAX_CHORD_NOTES; i++) {
        int cn = scale_degree_to_midi(inst->root, inst->scale, entry->chord_degrees[i]);
        cn += entry->octave_shift * 12;
        cn = clamp_int(cn, MIN_MIDI_NOTE, MAX_MIDI_NOTE);
        if (cn != root_note) /* avoid double-triggering same note */
            note_on(inst, (uint8_t)cn, (uint8_t)vel, gate, out, lens, max, cnt);
    }
}

static void generate_step(genera_instance_t *inst,
                           uint8_t out[][3], int lens[], int max, int *cnt) {
    step_entry_t entry;
    memset(&entry, 0, sizeof(entry));

    /* Base degree: walk through scale degrees based on step position */
    int base_degree = (int)(inst->current_step % (uint64_t)inst->steps);

    /* Evolve: chance to deviate */
    if (lcg_chance(&inst->rng_state, inst->evolve)) {
        int offset = lcg_range(&inst->rng_state, -3, 3);
        base_degree += offset;
    }

    entry.degree = base_degree;
    entry.velocity = compute_velocity(inst);

    /* Octave shift */
    if (lcg_chance(&inst->rng_state, inst->octaves)) {
        int shift = lcg_range(&inst->rng_state, -2, 2);
        if (shift == 0) shift = 1;
        entry.octave_shift = shift;
    }

    /* Chord generation */
    if (lcg_chance(&inst->rng_state, inst->chord)) {
        /* Weighted chord size: 50% triad, 25% 7th, 15% 9th, 10% 6-note */
        int r = lcg_range(&inst->rng_state, 0, 99);
        int chord_notes;
        if (r < 50)      chord_notes = 2; /* triad = root + 2 */
        else if (r < 75)  chord_notes = 3; /* 7th = root + 3 */
        else if (r < 90)  chord_notes = 4; /* 9th = root + 4 */
        else               chord_notes = 5; /* 6-note = root + 5 */

        entry.chord_size = chord_notes;
        for (int i = 0; i < chord_notes; i++) {
            /* Stack scale degrees: 3rd, 5th, 7th, 9th, 11th */
            entry.chord_degrees[i] = entry.degree + (i + 1) * 2;
        }
    }

    /* Store in history */
    inst->step_history[inst->history_write] = entry;
    inst->history_write = (inst->history_write + 1) % MAX_STEPS;
    if (inst->history_count < MAX_STEPS) inst->history_count++;

    /* Emit the main step */
    emit_step(inst, &entry, out, lens, max, cnt);

    /* Stutter: chance to emit an extra random step */
    if (lcg_chance(&inst->rng_state, inst->stutter)) {
        step_entry_t stutter_entry;
        memset(&stutter_entry, 0, sizeof(stutter_entry));
        stutter_entry.degree = base_degree + lcg_range(&inst->rng_state, -5, 5);
        stutter_entry.velocity = compute_velocity(inst);
        if (lcg_chance(&inst->rng_state, inst->octaves)) {
            stutter_entry.octave_shift = lcg_range(&inst->rng_state, -1, 1);
        }
        emit_step(inst, &stutter_entry, out, lens, max, cnt);
    }
}

static void emit_capture_step(genera_instance_t *inst,
                               uint8_t out[][3], int lens[], int max, int *cnt) {
    if (inst->capture_len <= 0) return;

    step_entry_t entry = inst->capture_buf[inst->capture_pos];

    /* Stutter affects captured loop too */
    if (lcg_chance(&inst->rng_state, inst->stutter)) {
        entry.degree += lcg_range(&inst->rng_state, -2, 2);
    }

    /* Octaves affects captured loop too */
    if (lcg_chance(&inst->rng_state, inst->octaves)) {
        entry.octave_shift += lcg_range(&inst->rng_state, -1, 1);
    }

    emit_step(inst, &entry, out, lens, max, cnt);

    inst->capture_pos = (inst->capture_pos + 1) % inst->capture_len;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Capture management
 * ══════════════════════════════════════════════════════════════════════════════ */

static void do_capture(genera_instance_t *inst) {
    int n = clamp_int(inst->capture, 0, MAX_STEPS);
    if (n <= 0 || inst->history_count <= 0) {
        inst->capture_len = 0;
        inst->capture_pos = 0;
        return;
    }
    if (n > inst->history_count) n = inst->history_count;

    /* Copy last n entries from history ring buffer */
    int read_pos = (inst->history_write - n + MAX_STEPS) % MAX_STEPS;
    for (int i = 0; i < n; i++) {
        inst->capture_buf[i] = inst->step_history[(read_pos + i) % MAX_STEPS];
    }
    inst->capture_len = n;
    inst->capture_pos = 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Run one sequencer step
 * ══════════════════════════════════════════════════════════════════════════════ */

static void run_step(genera_instance_t *inst,
                     uint8_t out[][3], int lens[], int max, int *cnt) {
    /* Generate new note/chord */
    generate_step(inst, out, lens, max, cnt);

    /* If capture is active, also emit from capture loop */
    if (inst->capture_len > 0) {
        emit_capture_step(inst, out, lens, max, cnt);
    }

    inst->current_step++;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Transport
 * ══════════════════════════════════════════════════════════════════════════════ */

static void handle_transport_start(genera_instance_t *inst) {
    inst->clock_running = 1;
    inst->clock_counter = 0;
    inst->pending_step_triggers = 1; /* Emit first step immediately */
    inst->current_step = 0;
    inst->vel_moving_pos = 0;
    inst->vel_moving_dir = 1;
}

static int handle_transport_stop(genera_instance_t *inst,
                                  uint8_t out[][3], int lens[], int max) {
    int count = 0;
    emit_all_notes_off(inst, out, lens, max, &count);
    inst->clock_running = 0;
    inst->pending_step_triggers = 0;
    inst->clock_counter = 0;
    inst->current_step = 0;
    inst->samples_until_step_f = inst->step_interval_f > 0.0 ? inst->step_interval_f : 1.0;
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Knob mapping tables
 * ══════════════════════════════════════════════════════════════════════════════ */

static const char *g_genera_knob_keys[8] = {
    "root", "scale", "steps", "capture", "evolve", "stutter", "octaves", "chord"
};
static const char *g_genera_knob_names[8] = {
    "Root", "Scale", "Steps", "Capture", "Evolve", "Stutter", "Octaves", "Chord"
};

static const char *g_sync_knob_keys[8] = {
    "sync", "tempo", "division", "vel_mode", "velocity", "gate", NULL, NULL
};
static const char *g_sync_knob_names[8] = {
    "Sync", "Tempo", "Division", "Vel Mode", "Velocity", "Gate", "", ""
};

/* ══════════════════════════════════════════════════════════════════════════════
 * set_param / get_param — forward declarations
 * ══════════════════════════════════════════════════════════════════════════════ */

static void genera_set_param(void *instance, const char *key, const char *val);
static int genera_get_param(void *instance, const char *key, char *buf, int buf_len);

/* ══════════════════════════════════════════════════════════════════════════════
 * Knob adjust
 * ══════════════════════════════════════════════════════════════════════════════ */

static void knob_adjust_param(genera_instance_t *inst, const char *key, int delta) {
    if (!inst || !key) return;

    /* Enum params: adjust index */
    if (strcmp(key, "root") == 0) {
        inst->root = clamp_int(inst->root + delta, 0, NUM_ROOTS - 1);
        return;
    }
    if (strcmp(key, "scale") == 0) {
        inst->scale = clamp_int(inst->scale + delta, 0, NUM_SCALES - 1);
        return;
    }
    if (strcmp(key, "sync") == 0) {
        int s = (int)inst->sync_mode + delta;
        inst->sync_mode = (sync_mode_t)clamp_int(s, 0, 1);
        inst->timing_dirty = 1;
        recalc_clock_timing(inst);
        return;
    }
    if (strcmp(key, "division") == 0) {
        inst->division = clamp_int(inst->division + delta, 0, NUM_DIVISIONS - 1);
        inst->timing_dirty = 1;
        recalc_clock_timing(inst);
        return;
    }
    if (strcmp(key, "vel_mode") == 0) {
        int v = (int)inst->vel_mode + delta;
        inst->vel_mode = (vel_mode_t)clamp_int(v, 0, 2);
        return;
    }
    if (strcmp(key, "capture") == 0) {
        int old_capture = inst->capture;
        inst->capture = clamp_int(inst->capture + delta, 0, MAX_STEPS);
        if (inst->capture != old_capture) {
            if (inst->capture > 0) do_capture(inst);
            else { inst->capture_len = 0; inst->capture_pos = 0; }
        }
        return;
    }

    /* Integer params: get current, add delta, set back */
    char buf[64];
    if (genera_get_param(inst, key, buf, sizeof(buf)) > 0) {
        int cur = atoi(buf);
        snprintf(buf, sizeof(buf), "%d", cur + delta);
        genera_set_param(inst, key, buf);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * set_param
 * ══════════════════════════════════════════════════════════════════════════════ */

static int find_enum_index(const char *val, const char *const *names, int count) {
    if (!val) return -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(val, names[i]) == 0) return i;
    }
    /* Try numeric */
    int n = atoi(val);
    if (n >= 0 && n < count) return n;
    return -1;
}

static int find_division_index(const char *val) {
    if (!val) return -1;
    for (int i = 0; i < NUM_DIVISIONS; i++) {
        if (strcmp(val, g_divisions[i].name) == 0) return i;
    }
    int n = atoi(val);
    if (n >= 0 && n < NUM_DIVISIONS) return n;
    return -1;
}

static int find_scale_index(const char *val) {
    if (!val) return -1;
    for (int i = 0; i < NUM_SCALES; i++) {
        if (strcmp(val, g_scales[i].name) == 0) return i;
    }
    int n = atoi(val);
    if (n >= 0 && n < NUM_SCALES) return n;
    return -1;
}

static void genera_set_param(void *instance, const char *key, const char *val) {
    genera_instance_t *inst = (genera_instance_t *)instance;
    if (!inst || !key || !val) return;

    /* ── Knob overlay: adjust ── */
    if (strncmp(key, "knob_", 5) == 0 && strstr(key, "_adjust")) {
        int knob_num = atoi(key + 5);
        int delta = atoi(val);
        if (knob_num < 1 || knob_num > 8 || delta == 0) return;

        const char *param_key = NULL;
        if (inst->current_level <= 1) {
            param_key = g_genera_knob_keys[knob_num - 1];
        } else if (inst->current_level == 2) {
            param_key = g_sync_knob_keys[knob_num - 1];
            if (!param_key) return;
        }
        if (param_key) knob_adjust_param(inst, param_key, delta);
        return;
    }

    /* ── Level navigation ── */
    if (strcmp(key, "current_level") == 0 || strcmp(key, "_level") == 0) {
        if (strcmp(val, "root") == 0 || strcmp(val, "Genera") == 0) inst->current_level = 0;
        else if (strcmp(val, "genera") == 0) inst->current_level = 1;
        else if (strcmp(val, "sync") == 0) inst->current_level = 2;
        return;
    }

    /* ── Direct param set ── */
    if (strcmp(key, "root") == 0) {
        int idx = find_enum_index(val, g_root_names, NUM_ROOTS);
        if (idx >= 0) inst->root = idx;
        return;
    }
    if (strcmp(key, "scale") == 0) {
        int idx = find_scale_index(val);
        if (idx >= 0) inst->scale = idx;
        return;
    }
    if (strcmp(key, "steps") == 0) {
        inst->steps = clamp_int(atoi(val), 1, MAX_STEPS);
        return;
    }
    if (strcmp(key, "capture") == 0) {
        int old_capture = inst->capture;
        inst->capture = clamp_int(atoi(val), 0, MAX_STEPS);
        if (inst->capture != old_capture) {
            if (inst->capture > 0) do_capture(inst);
            else { inst->capture_len = 0; inst->capture_pos = 0; }
        }
        return;
    }
    if (strcmp(key, "evolve") == 0) { inst->evolve = clamp_int(atoi(val), 0, 100); return; }
    if (strcmp(key, "stutter") == 0) { inst->stutter = clamp_int(atoi(val), 0, 100); return; }
    if (strcmp(key, "octaves") == 0) { inst->octaves = clamp_int(atoi(val), 0, 100); return; }
    if (strcmp(key, "chord") == 0) { inst->chord = clamp_int(atoi(val), 0, 100); return; }

    if (strcmp(key, "sync") == 0) {
        int idx = find_enum_index(val, g_sync_names, 2);
        if (idx >= 0) {
            inst->sync_mode = (sync_mode_t)idx;
            inst->timing_dirty = 1;
            recalc_clock_timing(inst);
        }
        return;
    }
    if (strcmp(key, "tempo") == 0) {
        inst->tempo = clamp_int(atoi(val), 20, 500);
        inst->timing_dirty = 1;
        return;
    }
    if (strcmp(key, "division") == 0) {
        int idx = find_division_index(val);
        if (idx >= 0) {
            inst->division = idx;
            inst->timing_dirty = 1;
            recalc_clock_timing(inst);
        }
        return;
    }
    if (strcmp(key, "vel_mode") == 0) {
        int idx = find_enum_index(val, g_vel_mode_names, 3);
        if (idx >= 0) inst->vel_mode = (vel_mode_t)idx;
        return;
    }
    if (strcmp(key, "velocity") == 0) { inst->velocity = clamp_int(atoi(val), 0, 127); return; }
    if (strcmp(key, "gate") == 0) { inst->gate = clamp_int(atoi(val), 1, 200); return; }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * get_param
 * ══════════════════════════════════════════════════════════════════════════════ */

static int genera_get_param(void *instance, const char *key, char *buf, int buf_len) {
    genera_instance_t *inst = (genera_instance_t *)instance;
    if (!inst || !key || !buf || buf_len < 1) return -1;

    /* ── Knob overlay: name / value ── */
    if (strncmp(key, "knob_", 5) == 0) {
        int knob_num = atoi(key + 5);
        int is_name = strstr(key, "_name") != NULL;
        int is_value = strstr(key, "_value") != NULL;
        if (knob_num >= 1 && knob_num <= 8 && (is_name || is_value)) {
            int ki = knob_num - 1;
            const char **keys;
            const char **names;

            if (inst->current_level <= 1) {
                keys = g_genera_knob_keys;
                names = g_genera_knob_names;
            } else {
                keys = g_sync_knob_keys;
                names = g_sync_knob_names;
                if (!keys[ki]) return -1;
            }

            if (is_name) return snprintf(buf, buf_len, "%s", names[ki]);
            return genera_get_param(inst, keys[ki], buf, buf_len);
        }
        return -1;
    }

    /* ── Direct param get ── */
    if (strcmp(key, "root") == 0)
        return snprintf(buf, buf_len, "%s", g_root_names[clamp_int(inst->root, 0, NUM_ROOTS - 1)]);
    if (strcmp(key, "scale") == 0)
        return snprintf(buf, buf_len, "%s", g_scales[clamp_int(inst->scale, 0, NUM_SCALES - 1)].name);
    if (strcmp(key, "steps") == 0) return snprintf(buf, buf_len, "%d", inst->steps);
    if (strcmp(key, "capture") == 0) return snprintf(buf, buf_len, "%d", inst->capture);
    if (strcmp(key, "evolve") == 0) return snprintf(buf, buf_len, "%d", inst->evolve);
    if (strcmp(key, "stutter") == 0) return snprintf(buf, buf_len, "%d", inst->stutter);
    if (strcmp(key, "octaves") == 0) return snprintf(buf, buf_len, "%d", inst->octaves);
    if (strcmp(key, "chord") == 0) return snprintf(buf, buf_len, "%d", inst->chord);

    if (strcmp(key, "sync") == 0)
        return snprintf(buf, buf_len, "%s", g_sync_names[clamp_int((int)inst->sync_mode, 0, 1)]);
    if (strcmp(key, "tempo") == 0) return snprintf(buf, buf_len, "%d", inst->tempo);
    if (strcmp(key, "division") == 0)
        return snprintf(buf, buf_len, "%s", g_divisions[clamp_int(inst->division, 0, NUM_DIVISIONS - 1)].name);
    if (strcmp(key, "vel_mode") == 0)
        return snprintf(buf, buf_len, "%s", g_vel_mode_names[clamp_int((int)inst->vel_mode, 0, 2)]);
    if (strcmp(key, "velocity") == 0) return snprintf(buf, buf_len, "%d", inst->velocity);
    if (strcmp(key, "gate") == 0) return snprintf(buf, buf_len, "%d", inst->gate);

    if (strcmp(key, "name") == 0) return snprintf(buf, buf_len, "Genera");
    if (strcmp(key, "bank_name") == 0) return snprintf(buf, buf_len, "Factory");

    if (strcmp(key, "error") == 0) {
        if (inst->sync_mode == SYNC_CLOCK && g_host && g_host->get_clock_status) {
            int st = g_host->get_clock_status();
            if (st == 0) /* UNAVAILABLE */
                return snprintf(buf, buf_len, "Enable MIDI Clock Out in Move settings");
        }
        buf[0] = '\0';
        return 0;
    }

    if (strcmp(key, "chain_params") == 0) {
        if (inst->chain_params_len > 0 && inst->chain_params_len < buf_len) {
            memcpy(buf, inst->chain_params_json, (size_t)inst->chain_params_len);
            buf[inst->chain_params_len] = '\0';
            return inst->chain_params_len;
        }
        return -1;
    }

    /* Full state serialization */
    if (strcmp(key, "state") == 0) {
        int pos = 0;
        if (!appendf(buf, buf_len, &pos,
            "{\"root\":\"%s\",\"scale\":\"%s\",\"steps\":%d,\"capture\":%d,"
            "\"evolve\":%d,\"stutter\":%d,\"octaves\":%d,\"chord\":%d,"
            "\"sync\":\"%s\",\"tempo\":%d,\"division\":\"%s\","
            "\"vel_mode\":\"%s\",\"velocity\":%d,\"gate\":%d}",
            g_root_names[inst->root], g_scales[inst->scale].name,
            inst->steps, inst->capture,
            inst->evolve, inst->stutter, inst->octaves, inst->chord,
            g_sync_names[(int)inst->sync_mode], inst->tempo,
            g_divisions[inst->division].name,
            g_vel_mode_names[(int)inst->vel_mode],
            inst->velocity, inst->gate))
            return -1;
        return pos;
    }

    return -1;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Instance lifecycle
 * ══════════════════════════════════════════════════════════════════════════════ */

static void build_chain_params(genera_instance_t *inst) {
    int pos = 0;
    char *buf = inst->chain_params_json;
    int buf_len = (int)sizeof(inst->chain_params_json);

    appendf(buf, buf_len, &pos, "[");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"root\",\"name\":\"Root\",\"type\":\"enum\",\"options\":[\"C\",\"C#\",\"D\",\"D#\",\"E\",\"F\",\"F#\",\"G\",\"G#\",\"A\",\"A#\",\"B\"]},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"scale\",\"name\":\"Scale\",\"type\":\"enum\",\"options\":[\"Major\",\"Minor\",\"Dorian\",\"Phrygian\",\"Lydian\",\"Mixolydian\",\"Aeolian\",\"Locrian\",\"Harm Min\",\"Melod Min\",\"Penta Maj\",\"Penta Min\",\"Blues\",\"Whole Tone\",\"Diminished\",\"Augmented\",\"Chromatic\",\"Hung Min\",\"Arabic\",\"Japanese\"]},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"steps\",\"name\":\"Steps\",\"type\":\"int\",\"min\":1,\"max\":64,\"step\":1},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"capture\",\"name\":\"Capture\",\"type\":\"int\",\"min\":0,\"max\":64,\"step\":1},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"evolve\",\"name\":\"Evolve\",\"type\":\"int\",\"min\":0,\"max\":100,\"step\":1},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"stutter\",\"name\":\"Stutter\",\"type\":\"int\",\"min\":0,\"max\":100,\"step\":1},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"octaves\",\"name\":\"Octaves\",\"type\":\"int\",\"min\":0,\"max\":100,\"step\":1},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"chord\",\"name\":\"Chord\",\"type\":\"int\",\"min\":0,\"max\":100,\"step\":1},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"sync\",\"name\":\"Sync\",\"type\":\"enum\",\"options\":[\"Internal\",\"Move\"]},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"tempo\",\"name\":\"Tempo\",\"type\":\"int\",\"min\":20,\"max\":500,\"step\":1},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"division\",\"name\":\"Division\",\"type\":\"enum\",\"options\":[\"1/1\",\"1/1T\",\"1/1D\",\"1/2\",\"1/2T\",\"1/2D\",\"1/4\",\"1/4T\",\"1/4D\",\"1/8\",\"1/8T\",\"1/8D\",\"1/16\",\"1/16T\",\"1/16D\",\"1/32\",\"1/32T\",\"1/32D\"]},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"vel_mode\",\"name\":\"Vel Mode\",\"type\":\"enum\",\"options\":[\"Fixed\",\"Random\",\"Moving\"]},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"velocity\",\"name\":\"Velocity\",\"type\":\"int\",\"min\":0,\"max\":127,\"step\":1},");
    appendf(buf, buf_len, &pos,
        "{\"key\":\"gate\",\"name\":\"Gate\",\"type\":\"int\",\"min\":1,\"max\":200,\"step\":1}");
    appendf(buf, buf_len, &pos, "]");

    inst->chain_params_len = pos;
}

static void *genera_create_instance(const char *module_dir, const char *config_json) {
    genera_instance_t *inst = (genera_instance_t *)calloc(1, sizeof(genera_instance_t));
    if (!inst) return NULL;

    /* Defaults */
    inst->root = 0;         /* C */
    inst->scale = 0;        /* Major */
    inst->steps = 16;
    inst->capture = 0;
    inst->evolve = 20;
    inst->stutter = 0;
    inst->octaves = 0;
    inst->chord = 0;

    inst->sync_mode = SYNC_CLOCK;  /* Move BPM by default */
    inst->tempo = DEFAULT_BPM;
    inst->division = 9;    /* 1/8 */
    inst->vel_mode = VEL_FIXED;
    inst->velocity = 100;
    inst->gate = 80;

    inst->rng_state = 12345u;
    inst->vel_moving_dir = 1;
    inst->timing_dirty = 1;
    inst->step_interval_f = 1.0;
    inst->samples_until_step_f = 1.0;
    inst->clocks_per_step = 6;  /* 1/8 note = 24/4 = 6 clocks */

    recalc_clock_timing(inst);
    build_chain_params(inst);

    (void)module_dir;
    (void)config_json;
    return inst;
}

static void genera_destroy_instance(void *instance) {
    genera_instance_t *inst = (genera_instance_t *)instance;
    if (!inst) return;
    /* Best-effort all notes off — but we have no output buffer here.
     * The host should handle lingering notes via CC 123. */
    free(inst);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * process_midi — transport, clock, pass-through
 * ══════════════════════════════════════════════════════════════════════════════ */

static int genera_process_midi(void *instance, const uint8_t *in_msg, int in_len,
                                uint8_t out_msgs[][3], int out_lens[], int max_out) {
    genera_instance_t *inst = (genera_instance_t *)instance;
    int count = 0;
    if (!inst || !in_msg || in_len < 1) return 0;

    uint8_t status = in_msg[0];

    /* ── Transport messages ── */
    if (inst->sync_mode == SYNC_CLOCK) {
        if (status == 0xFA) { /* MIDI Start */
            handle_transport_start(inst);
            return 0;
        }
        if (status == 0xFB) { /* MIDI Continue */
            inst->clock_running = 1;
            return 0;
        }
        if (status == 0xFC) { /* MIDI Stop */
            return handle_transport_stop(inst, out_msgs, out_lens, max_out);
        }
        if (status == 0xF8) { /* Clock tick */
            if (!inst->clock_running) return 0;
            inst->clock_counter++;
            if (inst->clocks_per_step < 1) inst->clocks_per_step = 1;
            if (inst->clock_counter >= inst->clocks_per_step) {
                inst->clock_counter = 0;
                inst->pending_step_triggers++;
            }
            return 0;
        }
    } else {
        /* Internal sync — still handle transport for start/stop */
        if (status == 0xFA || status == 0xFB) {
            handle_transport_start(inst);
            if (inst->timing_dirty || inst->sample_rate <= 0)
                recalc_internal_timing(inst, inst->sample_rate > 0 ? inst->sample_rate : DEFAULT_SAMPLE_RATE);
            return 0;
        }
        if (status == 0xFC) {
            return handle_transport_stop(inst, out_msgs, out_lens, max_out);
        }
    }

    /* Pass through all other MIDI (notes from user, etc.) */
    if (in_len >= 1 && max_out > count) {
        out_msgs[count][0] = in_msg[0];
        out_msgs[count][1] = in_len > 1 ? in_msg[1] : 0;
        out_msgs[count][2] = in_len > 2 ? in_msg[2] : 0;
        out_lens[count] = in_len > 3 ? 3 : in_len;
        count++;
    }

    return count;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * tick — per-block timing engine
 * ══════════════════════════════════════════════════════════════════════════════ */

static int genera_tick(void *instance, int frames, int sample_rate,
                        uint8_t out_msgs[][3], int out_lens[], int max_out) {
    genera_instance_t *inst = (genera_instance_t *)instance;
    int count = 0;
    if (!inst || frames < 0 || max_out < 1) return 0;

    if (inst->timing_dirty || inst->sample_rate != sample_rate)
        recalc_internal_timing(inst, sample_rate);

    /* Advance note-off timers */
    advance_note_timers(inst, frames, out_msgs, out_lens, max_out, &count);

    if (!inst->clock_running) return count;

    if (inst->sync_mode == SYNC_INTERNAL) {
        inst->samples_until_step_f -= (double)frames;
        while (inst->samples_until_step_f <= 0.0 && count < max_out) {
            int local = 0;
            run_step(inst, out_msgs + count, out_lens + count, max_out - count, &local);
            count += local;
            inst->samples_until_step_f += inst->step_interval_f;
            if (inst->samples_until_step_f < 1.0) inst->samples_until_step_f = 1.0;
        }
    } else {
        /* External clock: drain pending steps */
        while (inst->pending_step_triggers > 0 && count < max_out) {
            int local = 0;
            run_step(inst, out_msgs + count, out_lens + count, max_out - count, &local);
            count += local;
            inst->pending_step_triggers--;
        }
    }

    return count;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * API export
 * ══════════════════════════════════════════════════════════════════════════════ */

static midi_fx_api_v1_t g_api = {
    .api_version = MIDI_FX_API_VERSION,
    .create_instance = genera_create_instance,
    .destroy_instance = genera_destroy_instance,
    .process_midi = genera_process_midi,
    .tick = genera_tick,
    .set_param = genera_set_param,
    .get_param = genera_get_param
};

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host) {
    g_host = host;
    return &g_api;
}
