#pragma once

#include "step_rx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void step_trace_init(const char *model, const char *configuration, const char *secret);
uint32_t step_trace_begin(void);
void step_trace_mic_ready(void);
void step_trace_upload(size_t samples, bool sent);
void step_trace_expect_input(uint32_t turn);
void step_trace_expect_response(uint32_t turn);
void step_trace_event_id(char *out, size_t capacity);
void step_trace_sent(uint32_t turn, const char *stage, const char *event_id,
                     const char *call_id, bool sent);
uint32_t step_trace_server(const step_msg_t *message);
uint32_t step_trace_call_turn(const char *call_id);
void step_trace_parse_error(const char *reason);
void step_trace_output_volume(unsigned volume);
void step_trace_received(size_t samples, size_t queued);
void step_trace_played(size_t samples, unsigned volume, bool ok);
void step_trace_playback_done(void);
void step_trace_tool(uint32_t turn, const char *stage, const char *call_id,
                     const char *name, const char *arguments, const char *result,
                     const char *status, const char *reason);
void step_trace_disconnect(void);
