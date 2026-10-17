#include "plank_audio.h"
#include "plank_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int16_t reference[PLANK_AUDIO_RATE * 6];
static int16_t chunked[PLANK_AUDIO_RATE];

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

int main(int argc, char **argv)
{
    const unsigned effects[] = {PLANK_EFFECT_START, PLANK_EFFECT_STOP, PLANK_EFFECT_CUE_10,
        PLANK_EFFECT_CUE_30, PLANK_EFFECT_CUE_60, PLANK_EFFECT_PREP_1, PLANK_EFFECT_PREP_2,
        PLANK_EFFECT_PREP_3, PLANK_EFFECT_PREP_4};
    const unsigned lengths[] = {PLANK_START_SIGNAL_MS, 470, 90, 250, 410, 120, 120, 120, 120};
    const char *names[] = {"start", "stop", "ten", "thirty", "minute",
                           "prep1", "prep2", "prep3", "prep4"};
    for (unsigned i = 0; i < sizeof(effects) / sizeof(effects[0]); i++) {
        size_t length = plank_audio_render(effects[i], 0, reference, PLANK_AUDIO_RATE);
        assert(length == lengths[i] * 16U);
        for (unsigned block = 1; block <= PLANK_AUDIO_CHUNK; block += 73) {
            size_t offset = 0, n;
            while ((n = plank_audio_render(effects[i], offset, chunked + offset, block)) > 0) offset += n;
            assert(offset == length && memcmp(reference, chunked, length * 2) == 0);
        }
        for (size_t j = 0; j < length; j++) assert(reference[j] >= -6000 && reference[j] <= 6000);
        if (argc == 2) wav(argv[1], names[i], reference, length);
    }
    size_t length = plank_audio_render(PLANK_EFFECT_CUE_10, 0, reference, PLANK_AUDIO_RATE);
    unsigned crossings = 0;
    for (size_t i = 1; i < length; i++) crossings += reference[i] > 0 && reference[i - 1] < 0;
    assert(crossings == 89);
    plank_audio_render(PLANK_EFFECT_CUE_30, 0, reference, PLANK_AUDIO_RATE);
    for (size_t i = 90 * 16; i < 160 * 16; i++) assert(reference[i] == 0);
    assert(plank_audio_render(0, 0, reference, 100) == 0);
    size_t prep_length = plank_audio_render(PLANK_EFFECT_PREP_1, 0, reference, PLANK_AUDIO_RATE);
    for (unsigned effect = PLANK_EFFECT_PREP_2; effect <= PLANK_EFFECT_PREP_4; effect <<= 1) {
        size_t other_length = plank_audio_render(effect, 0, chunked, PLANK_AUDIO_RATE);
        assert(other_length == prep_length && memcmp(reference, chunked, prep_length * 2) == 0);
    }
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
    puts("Plank audio: PASS (duration, frequency, silence, chunk continuity)");
}
