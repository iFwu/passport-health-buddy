#pragma once
#include <stddef.h>
#include <stdint.h>

#define PLANK_AUDIO_RATE 16000U
#define PLANK_AUDIO_VOLUME 80U
#define PLANK_AUDIO_CHUNK 512U

enum {
    HEALTH_SOUND_RELAX = 1u << 10,
    HEALTH_SOUND_FINISH = 1u << 11,
    HEALTH_SOUND_REMIND = 1u << 12,
    HEALTH_SOUND_OK = 1u << 13,
    HEALTH_SOUND_VOLUME = 1u << 14,
    HEALTH_SOUND_LISTEN = 1u << 15,
    HEALTH_SOUND_SENT = 1u << 16,
    HEALTH_SOUND_ERROR = 1u << 17,
};

/* Render one exact sound ID as mono signed PCM. Arbitrary offsets are stateless;
 * returns zero for invalid IDs, NULL output, zero capacity, or end of sound.
 * Bell/soft/wood cues include the preview's 30 ms silent tail. */
size_t plank_audio_render(unsigned effect, size_t offset, int16_t *pcm, size_t capacity);
