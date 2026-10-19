#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { HEALTH_KEY_UP, HEALTH_KEY_DOWN, HEALTH_KEY_OK } health_key_t;
typedef enum { HEALTH_KEY_PRESS, HEALTH_KEY_LONG, HEALTH_KEY_RELEASE } health_key_event_t;
typedef enum {
    HEALTH_ACTION_NONE,
    HEALTH_ACTION_VOLUME_UP,
    HEALTH_ACTION_VOLUME_DOWN,
    HEALTH_ACTION_PREVIOUS_MODE,
    HEALTH_ACTION_NEXT_MODE,
    HEALTH_ACTION_CONFIRM,
    HEALTH_ACTION_VOICE_START,
    HEALTH_ACTION_VOICE_STOP,
} health_action_t;

/* Zero initialization is ready for use. The first pressed key owns a gesture;
 * overlapping keys must be released and pressed again to acquire ownership.
 * Hardware CLICK/DOUBLE events are deliberately not part of this interface. */
typedef struct {
    uint8_t held;
    health_key_t owner;
    bool active;
    bool long_sent;
} health_input_t;

health_action_t health_input_event(health_input_t *input, health_key_t key,
                                   health_key_event_t event);
