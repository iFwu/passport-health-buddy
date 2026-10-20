#include "health_screen.h"

void health_screen_init(health_screen_t *screen, uint64_t now_ms)
{
    if (screen) *screen = (health_screen_t){.last_activity_ms = now_ms};
}

void health_screen_activity(health_screen_t *screen, uint64_t now_ms)
{
    if (screen) screen->last_activity_ms = now_ms;
}

bool health_screen_key(health_screen_t *screen, health_key_t key,
                       health_key_event_t event, uint64_t now_ms, bool asleep)
{
    if (!screen) return false;
    health_screen_activity(screen, now_ms);
    if ((unsigned)key > HEALTH_KEY_OK || (unsigned)event > HEALTH_KEY_RELEASE)
        return false;

    uint8_t bit = (uint8_t)(1u << key);
    if (event == HEALTH_KEY_RELEASE)
        screen->held &= (uint8_t)~bit;
    else
        screen->held |= bit;

    if (asleep) screen->suppress_gesture = true;
    bool consume = screen->suppress_gesture;
    if (!screen->held) screen->suppress_gesture = false;
    return consume;
}

health_screen_mode_t health_screen_target(health_screen_t *screen,
                                          uint64_t now_ms, bool keep_awake)
{
    if (!screen) return HEALTH_SCREEN_ACTIVE;

    /* Also reset at the observed end of keep_awake, not at its last poll. */
    if (keep_awake || screen->keep_awake || screen->held)
        health_screen_activity(screen, now_ms);
    screen->keep_awake = keep_awake;
    if (keep_awake || screen->held) return HEALTH_SCREEN_ACTIVE;

    uint64_t idle_ms = now_ms - screen->last_activity_ms;
    if (idle_ms >= UINT64_C(300000)) return HEALTH_SCREEN_ASLEEP;
    if (idle_ms >= UINT64_C(120000)) return HEALTH_SCREEN_DIM;
    return HEALTH_SCREEN_ACTIVE;
}
