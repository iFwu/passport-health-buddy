#pragma once
#include <stddef.h>
#include <stdint.h>

#define PLANK_AUDIO_RATE 16000U
#define PLANK_AUDIO_VOLUME 80U
#define PLANK_AUDIO_CHUNK 512U

size_t plank_audio_render(unsigned effect, size_t offset, int16_t *pcm, size_t capacity);
