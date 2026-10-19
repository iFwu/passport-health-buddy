#include "plank_audio.h"
#include "plank_core.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int16_t reference[PLANK_AUDIO_RATE * 6];
static int16_t chunked[PLANK_AUDIO_RATE * 2];

typedef struct {
    unsigned effect;
    const char *name;
    size_t length, sounding_end;
    unsigned first_hz, last_hz;
    size_t first_end, last_solo;
    int square;
} cue_t;

/* Sample-accurate preview lengths, including the 30 ms tail and JavaScript
 * Math.ceil rounding. first_end excludes overlap; last_solo excludes earlier
 * notes so frequency checks also cover the direction of each melody. */
static const cue_t cues[] = {
    {PLANK_EFFECT_PREP_1, "prep1", 1920, 1920, 1000, 1000, 1920, 0, 1},
    {PLANK_EFFECT_PREP_2, "prep2", 1920, 1920, 1000, 1000, 1920, 0, 1},
    {PLANK_EFFECT_PREP_3, "prep3", 1920, 1920, 1000, 1000, 1920, 0, 1},
    {PLANK_EFFECT_PREP_4, "prep4", 1920, 1920, 1000, 1000, 1920, 0, 1},
    {PLANK_EFFECT_START, "start", 5600, 5600, 2000, 2000, 5600, 0, 1},
    {PLANK_EFFECT_CUE_10, "ten", 1280, 800, 1568, 1568, 800, 0, 0},
    {PLANK_EFFECT_CUE_30, "thirty", 6881, 6400, 1319, 1319, 6400, 0, 0},
    {PLANK_EFFECT_CUE_60, "minute", 17280, 16800, 1047, 1568, 1200, 8400, 0},
    {PLANK_EFFECT_STOP, "stop", 18560, 18080, 1568, 1047, 2080, 4000, 0},
    {HEALTH_SOUND_RELAX, "relax", 14400, 13920, 1319, 1047, 2720, 4800, 0},
    {HEALTH_SOUND_FINISH, "finish", 26960, 26480, 1047, 2093, 1360, 10720, 0},
    {HEALTH_SOUND_REMIND, "remind", 24800, 24320, 784, 1319, 3520, 9600, 0},
    {HEALTH_SOUND_OK, "ok", 6561, 6080, 784, 1047, 960, 2400, 0},
    {HEALTH_SOUND_VOLUME, "vol", 1440, 960, 880, 880, 960, 0, 0},
    {HEALTH_SOUND_LISTEN, "listen", 2640, 2160, 659, 880, 720, 960, 0},
    {HEALTH_SOUND_SENT, "sent", 2640, 2160, 880, 659, 720, 960, 0},
    {HEALTH_SOUND_ERROR, "error", 7520, 7040, 659, 554, 1920, 3520, 0},
};

static void le(FILE *file, unsigned value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++) assert(fputc((value >> (8 * i)) & 255, file) != EOF);
}

static void wav(const char *directory, const char *name, const int16_t *samples, size_t count)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.wav", directory, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite("RIFF", 1, 4, file) == 4); le(file, 36 + (unsigned)count * 2, 4);
    assert(fwrite("WAVEfmt ", 1, 8, file) == 8); le(file, 16, 4);
    le(file, 1, 2); le(file, 1, 2); le(file, PLANK_AUDIO_RATE, 4);
    le(file, PLANK_AUDIO_RATE * 2, 4); le(file, 2, 2); le(file, 16, 2);
    assert(fwrite("data", 1, 4, file) == 4); le(file, (unsigned)count * 2, 4);
    for (size_t i = 0; i < count; i++) le(file, (uint16_t)samples[i], 2);
    assert(fclose(file) == 0);
}

static uint64_t energy(size_t start, size_t end)
{
    uint64_t sum = 0;
    for (size_t i = start; i < end; i++) sum += (int64_t)reference[i] * reference[i];
    return sum;
}

static void frequency(size_t start, size_t end, unsigned hz)
{
    if (end - start > 1600) end = start + 1600;
    assert(end > start + 200);
    unsigned crossings = 0;
    int previous = 0;
    for (size_t i = start; i < end; i++) {
        int sign = (reference[i] > 0) - (reference[i] < 0);
        if (!sign) continue;
        if (previous < 0 && sign > 0) crossings++;
        previous = sign;
    }
    int64_t difference = (int64_t)crossings * PLANK_AUDIO_RATE - (int64_t)hz * (end - start);
    /* Broad pitch invariant, not an incidental exact crossing count. */
    assert(difference > -(int64_t)PLANK_AUDIO_RATE * 2);
    assert(difference < (int64_t)PLANK_AUDIO_RATE * 2);
}

static void continuity(const cue_t *cue)
{
    const size_t blocks[] = {1, 7, 63, 64, 95, 96, 127, 511, PLANK_AUDIO_CHUNK, 997};
    for (size_t block = 0; block < sizeof(blocks) / sizeof(blocks[0]); block++) {
        size_t offset = 0, n;
        while ((n = plank_audio_render(cue->effect, offset, chunked + offset, blocks[block])) > 0)
            offset += n;
        assert(offset == cue->length);
        assert(memcmp(reference, chunked, cue->length * sizeof(reference[0])) == 0);
    }
    /* Independent seeks, including envelope, note-onset and end boundaries. */
    for (size_t offset = 0; offset < cue->length; offset += 113) {
        size_t n = plank_audio_render(cue->effect, offset, chunked, 137);
        size_t expected = cue->length - offset;
        if (expected > 137) expected = 137;
        assert(n == expected);
        assert(memcmp(reference + offset, chunked, n * sizeof(reference[0])) == 0);
    }
    assert(plank_audio_render(cue->effect, cue->length - 1, chunked, SIZE_MAX) == 1);
    assert(chunked[0] == reference[cue->length - 1]);
    assert(plank_audio_render(cue->effect, cue->length, chunked, 1) == 0);
    assert(plank_audio_render(cue->effect, SIZE_MAX, chunked, 1) == 0);
}

static void invalid_inputs(void)
{
    const unsigned invalid[] = {
        0, PLANK_EFFECT_SAVE, PLANK_EFFECT_START | PLANK_EFFECT_STOP,
        HEALTH_SOUND_OK | HEALTH_SOUND_SENT, PLANK_EFFECT_START | HEALTH_SOUND_OK,
        1u << 18, UINT_MAX,
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        chunked[0] = 1234;
        assert(plank_audio_render(invalid[i], 0, chunked, 100) == 0);
        assert(chunked[0] == 1234);
    }
    for (size_t i = 0; i < sizeof(cues) / sizeof(cues[0]); i++) {
        assert(plank_audio_render(cues[i].effect, 0, NULL, 100) == 0);
        chunked[0] = 1234;
        assert(plank_audio_render(cues[i].effect, 0, chunked, 0) == 0);
        assert(chunked[0] == 1234);
        /* Render accepts a single cue, not a bitmask or an unrelated core action. */
        assert(plank_audio_render(cues[i].effect | PLANK_EFFECT_SAVE, 0, chunked, 1) == 0);
    }
}

int main(int argc, char **argv)
{
    for (size_t i = 0; i < sizeof(cues) / sizeof(cues[0]); i++) {
        const cue_t *cue = &cues[i];
        size_t length = plank_audio_render(cue->effect, 0, reference, sizeof(reference) / sizeof(reference[0]));
        assert(length == cue->length);
        continuity(cue);
        int peak = 0;
        for (size_t j = 0; j < length; j++) {
            int absolute = reference[j] < 0 ? -reference[j] : reference[j];
            if (absolute > peak) peak = absolute;
            /* None of these selected overlaps should reach the safety limiter. */
            assert(absolute < 31129);
            if (cue->square) {
                unsigned phase = (unsigned)(j * cue->first_hz % PLANK_AUDIO_RATE);
                assert(reference[j] == (phase < PLANK_AUDIO_RATE / 2 ? 4015 : -4015));
            }
        }
        assert(peak > 1000);
        frequency(64, cue->first_end - 96, cue->first_hz);
        frequency(cue->last_solo + 64, cue->sounding_end - 96, cue->last_hz);
        if (!cue->square) {
            assert(reference[0] == 0);
            for (size_t j = cue->sounding_end; j < length; j++) assert(reference[j] == 0);
            assert(energy(cue->sounding_end - 256, cue->sounding_end) > 0);
            assert(energy(cue->sounding_end - 256, cue->sounding_end) < energy(64, 320));
        }
        if (argc == 2) wav(argv[1], cue->name, reference, length);
    }
    invalid_inputs();
    size_t prep_length = plank_audio_render(PLANK_EFFECT_PREP_1, 0, reference, PLANK_AUDIO_RATE);
    for (unsigned effect = PLANK_EFFECT_PREP_2; effect <= PLANK_EFFECT_PREP_4; effect <<= 1) {
        size_t other_length = plank_audio_render(effect, 0, chunked, PLANK_AUDIO_RATE);
        assert(other_length == prep_length && memcmp(reference, chunked, prep_length * sizeof(reference[0])) == 0);
    }
    /* The minute cue is a sustained, overlapping upward arpeggio, never the
     * former three equal beeps with 70 ms silent holes. */
    plank_audio_render(PLANK_EFFECT_CUE_60, 0, reference, sizeof(reference) / sizeof(reference[0]));
    assert(energy(90 * 16, 160 * 16) > 1000000);
    assert(energy(250 * 16, 320 * 16) > 1000000);
    assert(energy(600 * 16, 800 * 16) > 1000000);
    if (argc == 2) {
        memset(reference, 0, sizeof(reference));
        plank_model_t model;
        plank_init(&model, NULL, true);
        unsigned effect = plank_key(&model, PLANK_OK, 0);
        plank_audio_render(effect, 0, reference, PLANK_AUDIO_RATE);
        for (unsigned step = 1; step < 4; step++) {
            uint64_t at = step * 1000;
            effect = plank_tick(&model, at);
            plank_audio_render(effect, 0, reference + at * 16, PLANK_AUDIO_RATE);
        }
        effect = plank_tick(&model, PLANK_PREPARE_MS);
        plank_audio_render(effect, 0, reference + PLANK_PREPARE_MS * 16, PLANK_AUDIO_RATE);
        wav(argv[1], "preparation", reference, PLANK_AUDIO_RATE * 6);
    }
    puts("Plank/health audio: PASS (palette durations, pitches, envelopes, ranges, boundaries, chunk continuity)");
}
