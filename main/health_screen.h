#pragma once

#include "health_input.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HEALTH_SCREEN_ACTIVE,
    HEALTH_SCREEN_DIM,
    HEALTH_SCREEN_ASLEEP,
} health_screen_mode_t;

/* Zero initialization starts the idle clock at uptime zero. This policy owns
 * no hardware state; callers must supply the actual panel sleep state. */
typedef struct {
    uint64_t last_activity_ms;
    uint8_t held;
    bool suppress_gesture;
    bool keep_awake;
} health_screen_t;

void health_screen_init(health_screen_t *screen, uint64_t now_ms);
void health_screen_activity(health_screen_t *screen, uint64_t now_ms);

/* Every event counts as activity. Return true to consume a wake gesture,
 * including LONG and overlapping keys, through the final key release.
 * Call before health_input_event(), passing the actual hardware sleep state. */
bool health_screen_key(health_screen_t *screen, health_key_t key,
                       health_key_event_t event, uint64_t now_ms, bool asleep);

/* Use monotonic uptime in milliseconds. Holding a key or keep_awake keeps the
 * target ACTIVE; ending either protection starts a complete idle interval.
 * This computes a target only: failed hardware transitions change no policy. */
health_screen_mode_t health_screen_target(health_screen_t *screen,
                                          uint64_t now_ms, bool keep_awake);
