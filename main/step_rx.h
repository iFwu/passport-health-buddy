// StepFun Realtime 下行事件的流式解析。一条 WebSocket 文本帧可以分多次喂入。
// response.audio.delta 的 base64 不落地，解码成 24 kHz PCM16 后立刻回调。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STEP_TYPE_MAX 56
#define STEP_NAME_MAX 32
#define STEP_ID_MAX 48
#define STEP_ARGS_MAX 160
#define STEP_TEXT_MAX 96

typedef struct {
    char type[STEP_TYPE_MAX];
    char name[STEP_NAME_MAX];
    char call_id[STEP_ID_MAX];
    char arguments[STEP_ARGS_MAX];
    char transcript[STEP_TEXT_MAX];
} step_msg_t;

typedef void (*step_pcm_cb)(void *user, const int16_t *pcm24, size_t samples);

typedef struct step_rx step_rx_t;

step_rx_t *step_rx_create(step_pcm_cb cb, void *user);
void step_rx_destroy(step_rx_t *rx);
void step_rx_reset(step_rx_t *rx);

// 喂入一条消息的一个分片。语法错误返回 false。
bool step_rx_feed(step_rx_t *rx, const char *data, size_t len);

// 当前消息是否已经完整。完整时写出字段并自动准备下一条。
bool step_rx_take(step_rx_t *rx, step_msg_t *msg);
