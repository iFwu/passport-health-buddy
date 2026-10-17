#include "plank_audio.h"
#include "plank_core.h"

typedef struct { unsigned hz, ms; } note_t;

size_t plank_audio_render(unsigned effect, size_t offset, int16_t *pcm, size_t capacity)
{
    const note_t *notes;
    size_t count;
    static const note_t prep[] = {{1000, 120}};
    static const note_t start[] = {{2000, PLANK_START_SIGNAL_MS}};
    static const note_t stop[] = {{1000, 180}, {0, 110}, {1000, 180}};
    static const note_t ten[] = {{1000, 90}};
    static const note_t thirty[] = {{800, 90}, {0, 70}, {800, 90}};
    static const note_t minute[] = {{1200, 90}, {0, 70}, {1200, 90}, {0, 70}, {1200, 90}};
    switch (effect) {
        case PLANK_EFFECT_PREP_1:
        case PLANK_EFFECT_PREP_2:
        case PLANK_EFFECT_PREP_3:
        case PLANK_EFFECT_PREP_4: notes = prep; count = 1; break;
        case PLANK_EFFECT_START: notes = start; count = 1; break;
        case PLANK_EFFECT_STOP: notes = stop; count = 3; break;
        case PLANK_EFFECT_CUE_10: notes = ten; count = 1; break;
        case PLANK_EFFECT_CUE_30: notes = thirty; count = 3; break;
        case PLANK_EFFECT_CUE_60: notes = minute; count = 5; break;
        default: return 0;
    }
    size_t written = 0;
    for (size_t i = 0; i < count && written < capacity; i++) {
        size_t length = notes[i].ms * (PLANK_AUDIO_RATE / 1000);
        if (offset >= length) { offset -= length; continue; }
        for (; offset < length && written < capacity; offset++) {
            unsigned phase = (unsigned)(offset * notes[i].hz % PLANK_AUDIO_RATE);
            pcm[written++] = notes[i].hz == 0 ? 0 : phase < PLANK_AUDIO_RATE / 2 ? 6000 : -6000;
        }
        offset = 0;
    }
    return written;
}
