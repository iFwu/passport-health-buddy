#include "plank_audio.h"
#include "plank_core.h"

/* Selected passport-sounds.html palette, at 16 kHz. Constants are generated
 * offline: phase_step = round(hz * partial * 2^32 / 16000),
 * decay_step = round(32 * 2^24 / (16000 * tau)),
 * gain = round(amp * partial_gain / sum(partial_gains) * 32768).
 * BELL partials: 1/.35/.12 at 1/2/3; SOFT: 1/.18 at 1/2;
 * WOOD: 1/.35 at 1/2.76. No runtime floating point or heap is needed. */
typedef struct {
    uint16_t start, length;
    uint32_t decay_step;
    uint32_t phase_step[3];
    int16_t gain[3];
} note_t;

typedef struct {
    uint32_t phase[3];
    uint32_t decay;
    unsigned elapsed;
} voice_t;

/* One complete sine period, Q15, with a repeated endpoint for interpolation. */
static const int16_t sine[257] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602,
    6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
    32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
    30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683,
    27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868,
    18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
    12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179,
    6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
    0, -804, -1608, -2410, -3212, -4011, -4808, -5602,
    -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179,
    -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
    0,
};

/* Q15 exp(-i/32), i=0..160. All selected notes end before five tau. */
static const uint16_t decay[161] = {
    32768, 31760, 30783, 29836, 28918, 28028, 27166, 26330,
    25520, 24735, 23974, 23236, 22521, 21828, 21157, 20506,
    19875, 19263, 18671, 18096, 17539, 17000, 16477, 15970,
    15479, 15002, 14541, 14093, 13660, 13239, 12832, 12437,
    12055, 11684, 11324, 10976, 10638, 10311, 9994, 9686,
    9388, 9099, 8819, 8548, 8285, 8030, 7783, 7544,
    7312, 7087, 6869, 6657, 6452, 6254, 6061, 5875,
    5694, 5519, 5349, 5185, 5025, 4871, 4721, 4575,
    4435, 4298, 4166, 4038, 3914, 3793, 3676, 3563,
    3454, 3347, 3244, 3145, 3048, 2954, 2863, 2775,
    2690, 2607, 2527, 2449, 2374, 2301, 2230, 2161,
    2095, 2030, 1968, 1907, 1849, 1792, 1737, 1683,
    1631, 1581, 1533, 1485, 1440, 1395, 1352, 1311,
    1271, 1231, 1194, 1157, 1121, 1087, 1053, 1021,
    990, 959, 930, 901, 873, 846, 820, 795,
    771, 747, 724, 702, 680, 659, 639, 619,
    600, 582, 564, 546, 530, 513, 498, 482,
    467, 453, 439, 426, 412, 400, 387, 376,
    364, 353, 342, 331, 321, 311, 302, 292,
    283, 275, 266, 258, 250, 242, 235, 228,
    221,
};

static const note_t ten[] = {
    /* G6 WOOD, tau=12 ms, amp=.25 */
    {0, 800, 2796203, {420906795, 1161702754, 0}, {6068, 2124, 0}},
};
static const note_t thirty[] = {
    /* E6 BELL, tau=90 ms, amp=.45 */
    {0, 6400, 372827, {353932149, 707864297, 1061796446}, {10031, 3511, 1204}},
};
static const note_t minute[] = {
    /* C6/E6/G6 BELL, tau=120/120/260 ms, amp=.48/.48/.55 */
    {0, 7200, 279620, {280917705, 561835409, 842753114}, {10700, 3745, 1284}},
    {1200, 7200, 279620, {353932149, 707864297, 1061796446}, {10700, 3745, 1284}},
    {2400, 14400, 129056, {420906795, 841813590, 1262720385}, {12260, 4291, 1471}},
};
static const note_t stop[] = {
    /* G6/C6 BELL, tau=80/300 ms, amp=.5/.6 */
    {0, 4000, 419430, {420906795, 841813590, 1262720385}, {11146, 3901, 1337}},
    {2080, 16000, 111848, {280917705, 561835409, 842753114}, {13375, 4681, 1605}},
};
static const note_t relax[] = {
    /* E6/C6 SOFT, tau=100/220 ms, amp=.4/.42 */
    {0, 4800, 335544, {353932149, 707864297, 0}, {11108, 1999, 0}},
    {2720, 11200, 152520, {280917705, 561835409, 0}, {11663, 2099, 0}},
};
static const note_t finish[] = {
    /* C6/E6/G6/C7 BELL, tau=150/150/150/400 ms, amp=.45/.45/.45/.5 */
    {0, 8000, 223696, {280917705, 561835409, 842753114}, {10031, 3511, 1204}},
    {1360, 8000, 223696, {353932149, 707864297, 1061796446}, {10031, 3511, 1204}},
    {2720, 8000, 223696, {420906795, 841813590, 1262720385}, {10031, 3511, 1204}},
    {4080, 22400, 83886, {561835409, 1123670819, 1685506228}, {11146, 3901, 1337}},
};
static const note_t remind[] = {
    /* G5/E6 SOFT, tau=250/400 ms, amp=.45/.5 */
    {0, 9600, 134218, {210450713, 420901426, 0}, {12496, 2249, 0}},
    {3520, 20800, 83886, {353932149, 707864297, 0}, {13885, 2499, 0}},
};
static const note_t ok[] = {
    /* G5/C6 SOFT, tau=50/90 ms, amp=.42/.5 */
    {0, 2400, 671089, {210450713, 420901426, 0}, {11663, 2099, 0}},
    {960, 5120, 372827, {280917705, 561835409, 0}, {13885, 2499, 0}},
};
static const note_t volume[] = {
    /* A5 SOFT, tau=20 ms, amp=.45 */
    {0, 960, 1677722, {236223201, 472446403, 0}, {12496, 2249, 0}},
};
static const note_t listen[] = {
    /* E5/A5 SOFT, tau=30/40 ms, amp=.38/.4 */
    {0, 960, 1118481, {176966074, 353932149, 0}, {10552, 1899, 0}},
    {720, 1440, 838861, {236223201, 472446403, 0}, {11108, 1999, 0}},
};
static const note_t sent[] = {
    /* A5/E5 SOFT, tau=30/40 ms, amp=.38/.4 */
    {0, 960, 1118481, {236223201, 472446403, 0}, {10552, 1899, 0}},
    {720, 1440, 838861, {176966074, 353932149, 0}, {11108, 1999, 0}},
};
static const note_t error[] = {
    /* E5/C#5 SOFT, tau=80/120 ms, amp=.5/.5 */
    {0, 3520, 419430, {176966074, 353932149, 0}, {13885, 2499, 0}},
    {1920, 5120, 279620, {148812564, 297625127, 0}, {13885, 2499, 0}},
};

static int32_t sine_at(uint32_t phase)
{
    unsigned index = phase >> 24;
    int32_t fraction = (phase >> 8) & 65535u;
    return sine[index] + (sine[index + 1] - sine[index]) * fraction / 65536;
}

static int32_t note_sample(const note_t *note, voice_t *voice)
{
    unsigned index = voice->decay >> 24;
    int32_t fraction = (voice->decay >> 8) & 65535u;
    int32_t envelope = decay[index] +
        ((int32_t)decay[index + 1] - decay[index]) * fraction / 65536;
    /* Preview's linear 4 ms attack and final 6 ms release. */
    if (voice->elapsed < 64) envelope = envelope * (int32_t)voice->elapsed / 64;
    unsigned remaining = note->length - voice->elapsed;
    if (remaining < 96) envelope = envelope * (int32_t)remaining / 96;
    int32_t value = 0;
    for (unsigned p = 0; p < 3 && note->gain[p]; p++) {
        value += sine_at(voice->phase[p]) * note->gain[p];
        voice->phase[p] += note->phase_step[p];
    }
    voice->elapsed++;
    voice->decay += note->decay_step;
    return (value / 32768) * envelope / 32768;
}

size_t plank_audio_render(unsigned effect, size_t offset, int16_t *pcm, size_t capacity)
{
    const note_t *notes = NULL;
    unsigned count = 0;
    size_t length = 0;
    unsigned square_hz = 0;
#define CUE(name, samples) do { notes = name; count = sizeof(name) / sizeof(name[0]); length = samples; } while (0)
    switch (effect) {
        case PLANK_EFFECT_PREP_1:
        case PLANK_EFFECT_PREP_2:
        case PLANK_EFFECT_PREP_3:
        case PLANK_EFFECT_PREP_4: square_hz = 1000; length = 1920; break;
        case PLANK_EFFECT_START: square_hz = 2000; length = 5600; break;
        case PLANK_EFFECT_CUE_10: CUE(ten, 1280); break;
        /* Preserve Math.ceil's extra sample for .4+.03 and .06+.32+.03. */
        case PLANK_EFFECT_CUE_30: CUE(thirty, 6881); break;
        case PLANK_EFFECT_CUE_60: CUE(minute, 17280); break;
        case PLANK_EFFECT_STOP: CUE(stop, 18560); break;
        case HEALTH_SOUND_RELAX: CUE(relax, 14400); break;
        case HEALTH_SOUND_FINISH: CUE(finish, 26960); break;
        case HEALTH_SOUND_REMIND: CUE(remind, 24800); break;
        case HEALTH_SOUND_OK: CUE(ok, 6561); break;
        case HEALTH_SOUND_VOLUME: CUE(volume, 1440); break;
        case HEALTH_SOUND_LISTEN: CUE(listen, 2640); break;
        case HEALTH_SOUND_SENT: CUE(sent, 2640); break;
        case HEALTH_SOUND_ERROR: CUE(error, 7520); break;
        default: return 0;
    }
#undef CUE
    if (!pcm || !capacity || offset >= length) return 0;
    size_t written = length - offset;
    if (written > capacity) written = capacity;
    if (square_hz) {
        /* matchedOld: max 50 ms RMS of thirty, minus 3 dB = .12252028.
         * Keep the original phase, hard edges and durations, at +/-4015. */
        unsigned period = PLANK_AUDIO_RATE / square_hz;
        unsigned phase = offset % period;
        for (size_t i = 0; i < written; i++) {
            pcm[i] = phase < period / 2 ? 4015 : -4015;
            if (++phase == period) phase = 0;
        }
        return written;
    }
    voice_t voices[4] = {0};
    for (unsigned n = 0; n < count; n++) {
        unsigned elapsed = offset > notes[n].start ? (unsigned)(offset - notes[n].start) : 0;
        if (elapsed >= notes[n].length) continue;
        voices[n].elapsed = elapsed;
        voices[n].decay = elapsed * notes[n].decay_step;
        for (unsigned p = 0; p < 3; p++)
            voices[n].phase[p] = elapsed * notes[n].phase_step[p];
    }
    for (size_t i = 0; i < written; i++) {
        size_t position = offset + i;
        int32_t mixed = 0;
        for (unsigned n = 0; n < count; n++) {
            if (position >= notes[n].start && position - notes[n].start < notes[n].length)
                mixed += note_sample(&notes[n], &voices[n]);
        }
        /* Mix overlapping notes before the preview's .95 safety limit. */
        if (mixed > 31129) mixed = 31129;
        if (mixed < -31129) mixed = -31129;
        pcm[i] = (int16_t)mixed;
    }
    return written;
}
