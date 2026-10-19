#include "health_input.h"

#include <assert.h>
#include <stdio.h>

static void expect(health_input_t *input, health_key_t key, health_key_event_t event,
                   health_action_t action)
{
    assert(health_input_event(input, key, event) == action);
}

int main(void)
{
    const health_action_t shorts[] = {
        HEALTH_ACTION_VOLUME_UP, HEALTH_ACTION_VOLUME_DOWN, HEALTH_ACTION_CONFIRM,
    };
    const health_action_t longs[] = {
        HEALTH_ACTION_PREVIOUS_MODE, HEALTH_ACTION_NEXT_MODE, HEALTH_ACTION_VOICE_START,
    };
    for (health_key_t key = HEALTH_KEY_UP; key <= HEALTH_KEY_OK; key++) {
        health_input_t input = {0};
        /* Two quick taps are two complete gestures, without a CLICK delay. */
        for (unsigned tap = 0; tap < 2; tap++) {
            expect(&input, key, HEALTH_KEY_PRESS, HEALTH_ACTION_NONE);
            expect(&input, key, HEALTH_KEY_PRESS, HEALTH_ACTION_NONE);
            expect(&input, key, HEALTH_KEY_RELEASE, shorts[key]);
            expect(&input, key, HEALTH_KEY_RELEASE, HEALTH_ACTION_NONE);
        }
        expect(&input, key, HEALTH_KEY_PRESS, HEALTH_ACTION_NONE);
        expect(&input, key, HEALTH_KEY_LONG, longs[key]);
        for (unsigned repeat = 0; repeat < 5; repeat++) {
            expect(&input, key, HEALTH_KEY_LONG, HEALTH_ACTION_NONE);
            expect(&input, key, HEALTH_KEY_PRESS, HEALTH_ACTION_NONE);
        }
        expect(&input, key, HEALTH_KEY_RELEASE,
               key == HEALTH_KEY_OK ? HEALTH_ACTION_VOICE_STOP : HEALTH_ACTION_NONE);
        expect(&input, key, HEALTH_KEY_RELEASE, HEALTH_ACTION_NONE);
        expect(&input, key, HEALTH_KEY_LONG, HEALTH_ACTION_NONE);
        /* A hold must not consume the next short gesture. */
        expect(&input, key, HEALTH_KEY_PRESS, HEALTH_ACTION_NONE);
        expect(&input, key, HEALTH_KEY_RELEASE, shorts[key]);
        assert(!input.active && !input.held);
    }

    /* Every ordered pair checks that an overlapping key cannot steal, stop,
     * or repeat the owning gesture, even after the owner is released. */
    for (health_key_t owner = HEALTH_KEY_UP; owner <= HEALTH_KEY_OK; owner++) {
        for (health_key_t other = HEALTH_KEY_UP; other <= HEALTH_KEY_OK; other++) {
            if (owner == other) continue;
            health_input_t input = {0};
            expect(&input, owner, HEALTH_KEY_PRESS, HEALTH_ACTION_NONE);
            expect(&input, other, HEALTH_KEY_PRESS, HEALTH_ACTION_NONE);
            expect(&input, other, HEALTH_KEY_LONG, HEALTH_ACTION_NONE);
            expect(&input, owner, HEALTH_KEY_LONG, longs[owner]);
            expect(&input, other, HEALTH_KEY_RELEASE, HEALTH_ACTION_NONE);
            expect(&input, other, HEALTH_KEY_PRESS, HEALTH_ACTION_NONE);
            expect(&input, owner, HEALTH_KEY_RELEASE,
                   owner == HEALTH_KEY_OK ? HEALTH_ACTION_VOICE_STOP : HEALTH_ACTION_NONE);
            expect(&input, other, HEALTH_KEY_LONG, HEALTH_ACTION_NONE);
            expect(&input, other, HEALTH_KEY_PRESS, HEALTH_ACTION_NONE);
            expect(&input, other, HEALTH_KEY_RELEASE, HEALTH_ACTION_NONE);
            expect(&input, other, HEALTH_KEY_PRESS, HEALTH_ACTION_NONE);
            expect(&input, other, HEALTH_KEY_RELEASE, shorts[other]);
            assert(!input.active && !input.held);
        }
    }

    health_input_t input = {0};
    expect(&input, HEALTH_KEY_OK, HEALTH_KEY_LONG, HEALTH_ACTION_NONE);
    expect(&input, HEALTH_KEY_OK, HEALTH_KEY_RELEASE, HEALTH_ACTION_NONE);
    assert(health_input_event(NULL, HEALTH_KEY_OK, HEALTH_KEY_PRESS) == HEALTH_ACTION_NONE);
    assert(health_input_event(&input, (health_key_t)99, HEALTH_KEY_PRESS) == HEALTH_ACTION_NONE);
    assert(health_input_event(&input, HEALTH_KEY_OK, (health_key_event_t)99) == HEALTH_ACTION_NONE);
    assert(!input.active && !input.held);
    puts("Health input: PASS (release shorts, two taps, one long, PTT ownership, no stealing)");
    return 0;
}
