#include "health_input.h"

health_action_t health_input_event(health_input_t *input, health_key_t key,
                                   health_key_event_t event)
{
    if (!input || (unsigned)key > HEALTH_KEY_OK || (unsigned)event > HEALTH_KEY_RELEASE)
        return HEALTH_ACTION_NONE;

    uint8_t bit = (uint8_t)(1u << key);
    if (event == HEALTH_KEY_PRESS) {
        if (input->held & bit) return HEALTH_ACTION_NONE;
        input->held |= bit;
        if (!input->active) {
            input->owner = key;
            input->active = true;
            input->long_sent = false;
        }
        return HEALTH_ACTION_NONE;
    }
    if (event == HEALTH_KEY_LONG) {
        if (!input->active || input->owner != key || !(input->held & bit) || input->long_sent)
            return HEALTH_ACTION_NONE;
        input->long_sent = true;
        if (key == HEALTH_KEY_UP) return HEALTH_ACTION_PREVIOUS_MODE;
        if (key == HEALTH_KEY_DOWN) return HEALTH_ACTION_NEXT_MODE;
        return HEALTH_ACTION_VOICE_START;
    }

    input->held &= (uint8_t)~bit;
    if (!input->active || input->owner != key) return HEALTH_ACTION_NONE;
    input->active = false;
    if (input->long_sent)
        return key == HEALTH_KEY_OK ? HEALTH_ACTION_VOICE_STOP : HEALTH_ACTION_NONE;
    if (key == HEALTH_KEY_UP) return HEALTH_ACTION_VOLUME_UP;
    if (key == HEALTH_KEY_DOWN) return HEALTH_ACTION_VOLUME_DOWN;
    return HEALTH_ACTION_CONFIRM;
}
