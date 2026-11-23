/*
 * Moonwalker launcher - procedural synthwave loop.
 *
 * Everything here is generated from the song position: there are no audio
 * files, no tables of sampled data and no persistent per-voice phase
 * accumulators.  Each sample is a pure function of
 * (position mod loop_length), so the loop wraps without a discontinuity.
 *
 * Song: A minor, Am - F - C - G, one bar per chord, eight bars (two passes;
 * the second pass doubles the arpeggio an octave up).  104 BPM at 44.1 kHz,
 * i.e. 25440 samples per beat, which divides evenly into 16th notes and into
 * the 32-beat loop.
 *
 * Layers: kick on every beat, one sub-bass note per beat, a 16th-note
 * arpeggio, a quiet detuned-saw pad per chord and a noise hat on the
 * off-beats.  A slow LFO sweeps the arpeggio/pad lowpass cutoff.
 */
#include "launcher_music.h"

#include <math.h>
#include <SDL.h>

#define MW_MUSIC_RATE    44100
#define MW_BEAT_SAMPLES  25440                         /* 104.0 BPM */
#define MW_STEP_SAMPLES  (MW_BEAT_SAMPLES / 4)         /* 16th note */
#define MW_BEATS_PER_BAR 4
#define MW_BARS          8
#define MW_LOOP_SAMPLES  (MW_BARS * MW_BEATS_PER_BAR * MW_BEAT_SAMPLES)

#define MW_TWO_PI 6.28318530717958647692
#define MW_PI     3.14159265358979323846
#define MW_MASTER 0.62

typedef struct {
    double bass;      /* root, one octave below the pad */
    double pad_root;  /* pad root */
    double pad_fifth; /* pad fifth */
    double arp[4];    /* root, third, fifth, octave */
} mw_chord;

/* Am, F, C, G */
static const mw_chord k_chords[4] = {
    { 55.00, 110.00, 164.81, { 220.00, 261.63, 329.63, 440.00 } },
    { 43.65,  87.31, 130.81, { 174.61, 220.00, 261.63, 349.23 } },
    { 65.41, 130.81, 196.00, { 261.63, 329.63, 392.00, 523.25 } },
    { 49.00,  98.00, 146.83, { 196.00, 246.94, 293.66, 392.00 } },
};

static SDL_AudioDeviceID g_dev;
static int g_owns_audio;
static long long g_pos;

/* Sine of a phase measured in cycles.  The phase is folded before the
 * polynomial so it stays accurate across the thousands of cycles a held note
 * runs through, and Bhaskara I's approximation (|error| < 0.0016, about
 * -56 dB) is far cheaper than the C library call.  Pure in both arguments. */
static double sin_cycles(double cyc)
{
    double x, y, ay, num, den;
    cyc -= floor(cyc);
    x = cyc >= 0.5 ? cyc - 1.0 : cyc; /* [-0.5, 0.5) */
    y = x * MW_TWO_PI;
    ay = y < 0.0 ? -y : y;
    num = 16.0 * ay * (MW_PI - ay);
    den = 5.0 * MW_PI * MW_PI - 4.0 * ay * (MW_PI - ay);
    return y < 0.0 ? -num / den : num / den;
}

/* Exponential pluck whose value is exactly zero at both ends of its slot, so
 * consecutive slots (and the loop wrap) join without a step. */
static double slot_env(double x, double k)
{
    double tail;
    if (x <= 0.0 || x >= 1.0) return 0.0;
    tail = exp(-k);
    return (exp(-k * x) - tail) / (1.0 - tail);
}

/* slot_env() with a short linear attack, used by the bass and arpeggio. */
static double pluck_env(double x, double attack, double k)
{
    double a;
    if (x <= 0.0 || x >= 1.0) return 0.0;
    a = x < attack ? x / attack : 1.0;
    return a * slot_env(x, k);
}

/* Saw-ish tone: the harmonics of a naive saw weighted by the magnitude
 * response of a one-pole lowpass at 'cutoff'.  This is the stateless
 * equivalent of running a naive saw through a one-pole filter, which keeps
 * every note a pure function of its own local time. */
static double saw_note(double freq, double t, double cutoff, int maxh)
{
    double sum = 0.0;
    int n;
    for (n = 1; n <= maxh; n++) {
        double hf = freq * (double)n;
        double ratio;
        if (hf > 12000.0) break;
        ratio = hf / cutoff;
        sum += (1.0 / sqrt(1.0 + ratio * ratio)) * sin_cycles(hf * t) / (double)n;
    }
    return sum * 0.63661977; /* 2/pi */
}

/* Deterministic white noise keyed on the loop sample index. */
static double noise_at(long long n)
{
    unsigned long long x = (unsigned long long)n * 0x9E3779B97F4A7C15ull;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (double)(x >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

/* One mono sample for a position inside the loop. */
static double music_sample(long long idx)
{
    int beat = (int)(idx / MW_BEAT_SAMPLES);
    int beat_pos = (int)(idx % MW_BEAT_SAMPLES);
    int bar = beat / MW_BEATS_PER_BAR;       /* 0..7 */
    const mw_chord *ch = &k_chords[bar & 3];
    double bt = (double)beat_pos / (double)MW_MUSIC_RATE;
    double bx = (double)beat_pos / (double)MW_BEAT_SAMPLES;
    double lfo = 0.5 + 0.5 * sin_cycles((double)idx /
                                         (double)(16 * MW_BEAT_SAMPLES));
    double mix = 0.0;

    /* Kick: pitch-dropping sine blip on every beat. */
    {
        double amp = slot_env(bx, 6.5);
        if (amp > 0.0) {
            double ph = 45.0 * bt + (105.0 / 25.0) * (1.0 - exp(-25.0 * bt));
            mix += 0.34 * amp * sin_cycles(ph);
        }
    }

    /* Sub bass: one short note per beat on the root of the chord. */
    {
        double amp = pluck_env(bx, 0.03, 4.0);
        if (amp > 0.0) {
            double ph = ch->bass * bt;
            mix += amp * (0.20 * sin_cycles(ph) + 0.035 * sin_cycles(2.0 * ph));
        }
    }

    /* Arpeggio: four chord tones per bar, one 16th note each. */
    {
        int step = beat_pos / MW_STEP_SAMPLES;
        int step_pos = beat_pos % MW_STEP_SAMPLES;
        double sx = (double)step_pos / (double)MW_STEP_SAMPLES;
        double st = (double)step_pos / (double)MW_MUSIC_RATE;
        double amp = pluck_env(sx, 0.02, 5.0);
        if (amp > 0.0) {
            double cut = 2600.0 * (0.35 + 0.65 * lfo);
            double f = ch->arp[step & 3];
            mix += 0.10 * amp * saw_note(f, st, cut, 6);
            if (bar >= 4)
                mix += 0.055 * amp * saw_note(f * 2.0, st, cut, 6);
        }
    }

    /* Pad: detuned saw pair plus a fifth, held for one bar, lowpassed. */
    {
        int bar_pos = (beat % MW_BEATS_PER_BAR) * MW_BEAT_SAMPLES + beat_pos;
        double px = (double)bar_pos / (double)(MW_BEATS_PER_BAR * MW_BEAT_SAMPLES);
        double pt = (double)bar_pos / (double)MW_MUSIC_RATE;
        double env;
        if (px < 0.12) env = px / 0.12;
        else if (px > 0.88) env = (1.0 - px) / 0.12;
        else env = 1.0;
        if (env > 0.0) {
            double cut = 600.0 * (0.40 + 0.60 * lfo);
            double g = 0.035 * env;
            mix += g * (saw_note(ch->pad_root * 0.996, pt, cut, 5) +
                        saw_note(ch->pad_root * 1.004, pt, cut, 5));
            mix += g * 0.6 * saw_note(ch->pad_fifth, pt, cut, 5);
        }
    }

    /* Hat: short high-passed noise burst on each off-beat. */
    if (bx >= 0.5) {
        double hx = (bx - 0.5) * 2.0; /* 0..1 across the back half of the beat */
        double env = slot_env(hx, 14.0);
        double n0 = noise_at(idx);
        double n1 = noise_at(idx > 0 ? idx - 1 : MW_LOOP_SAMPLES - 1);
        mix += 0.030 * env * 0.5 * (n0 - n1);
    }

    mix *= MW_MASTER;
    if (mix > 0.5) mix = 0.5;
    else if (mix < -0.5) mix = -0.5;
    return mix;
}

/* Fills the device buffer.  The only state is the song position (not a voice
 * phase), which is wrapped modulo the loop length. */
static void music_callback(void *userdata, Uint8 *stream, int len)
{
    Sint16 *out = (Sint16 *)stream;
    int frames = len / (int)sizeof(Sint16);
    int i;
    (void)userdata;
    for (i = 0; i < frames; i++) {
        double s = music_sample(g_pos);
        g_pos++;
        if (g_pos >= MW_LOOP_SAMPLES) g_pos = 0;
        out[i] = (Sint16)(s * 32767.0);
    }
}

int mw_music_start(void)
{
    SDL_AudioSpec want;
    if (g_dev) return 1;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 0;
        g_owns_audio = 1;
    }
    SDL_zero(want);
    want.freq = MW_MUSIC_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = music_callback;
    want.userdata = NULL;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!g_dev) {
        if (g_owns_audio) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            g_owns_audio = 0;
        }
        return 0;
    }
    g_pos = 0; /* no callback runs until the device is unpaused below */
    SDL_PauseAudioDevice(g_dev, 0);
    return 1;
}

void mw_music_stop(void)
{
    if (g_dev) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
    if (g_owns_audio) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        g_owns_audio = 0;
    }
}
