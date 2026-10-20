#include "health_screen.h"

#include <assert.h>
#include <stdio.h>

static void expect_target(health_screen_t *screen, uint64_t now_ms,
                          bool keep_awake, health_screen_mode_t expected)
{
    assert(health_screen_target(screen, now_ms, keep_awake) == expected);
}

/* Exercise the same consumption boundary used by the application, with the
 * real business input policy rather than a substitute action counter. */
static void expect_key(health_screen_t *screen, health_input_t *input,
                       health_key_t key, health_key_event_t event,
                       uint64_t now_ms, bool asleep, bool consumed,
                       health_action_t action)
{
    bool actual = health_screen_key(screen, key, event, now_ms, asleep);
    assert(actual == consumed);
    health_action_t actual_action = actual ? HEALTH_ACTION_NONE
        : health_input_event(input, key, event);
    assert(actual_action == action);
}

static void test_idle_boundaries(void)
{
    const uint64_t starts[] = {
        0, 73, UINT64_C(4294967295) - 60000, UINT64_C(1) << 48,
    };
    for (unsigned i = 0; i < sizeof(starts) / sizeof(starts[0]); i++) {
        uint64_t start = starts[i];
        health_screen_t screen = {0};
        health_screen_init(&screen, start);
        expect_target(&screen, start, false, HEALTH_SCREEN_ACTIVE);
        expect_target(&screen, start + 119999, false, HEALTH_SCREEN_ACTIVE);
        expect_target(&screen, start + 120000, false, HEALTH_SCREEN_DIM);
        expect_target(&screen, start + 299999, false, HEALTH_SCREEN_DIM);
        expect_target(&screen, start + 300000, false, HEALTH_SCREEN_ASLEEP);
        expect_target(&screen, start + 600000, false, HEALTH_SCREEN_ASLEEP);
    }

    health_screen_t zero_initialized = {0};
    expect_target(&zero_initialized, 119999, false, HEALTH_SCREEN_ACTIVE);
    expect_target(&zero_initialized, 120000, false, HEALTH_SCREEN_DIM);
    expect_target(&zero_initialized, 300000, false, HEALTH_SCREEN_ASLEEP);
}

static void test_activity_reset(void)
{
    health_screen_t screen = {0};
    expect_target(&screen, 120000, false, HEALTH_SCREEN_DIM);
    health_screen_activity(&screen, 150000);
    expect_target(&screen, 150000, false, HEALTH_SCREEN_ACTIVE);
    expect_target(&screen, 269999, false, HEALTH_SCREEN_ACTIVE);
    expect_target(&screen, 270000, false, HEALTH_SCREEN_DIM);
    health_screen_activity(&screen, 449999);
    expect_target(&screen, 450000, false, HEALTH_SCREEN_ACTIVE);
    expect_target(&screen, 749998, false, HEALTH_SCREEN_DIM);
    expect_target(&screen, 749999, false, HEALTH_SCREEN_ASLEEP);

    /* Each input event refreshes idle time, not only the initial PRESS. */
    health_input_t input = {0};
    expect_key(&screen, &input, HEALTH_KEY_UP, HEALTH_KEY_PRESS,
               800000, false, false, HEALTH_ACTION_NONE);
    expect_key(&screen, &input, HEALTH_KEY_UP, HEALTH_KEY_LONG,
               900000, false, false, HEALTH_ACTION_PREVIOUS_MODE);
    expect_key(&screen, &input, HEALTH_KEY_UP, HEALTH_KEY_RELEASE,
               1000000, false, false, HEALTH_ACTION_NONE);
    expect_target(&screen, 1119999, false, HEALTH_SCREEN_ACTIVE);
    expect_target(&screen, 1120000, false, HEALTH_SCREEN_DIM);
    expect_target(&screen, 1300000, false, HEALTH_SCREEN_ASLEEP);
}

static void test_keep_awake(void)
{
    health_screen_t screen = {0};
    expect_target(&screen, 300000, false, HEALTH_SCREEN_ASLEEP);
    expect_target(&screen, 400000, true, HEALTH_SCREEN_ACTIVE);
    expect_target(&screen, 900000, true, HEALTH_SCREEN_ACTIVE);
    /* The protected operation ended between polls: start at this observation,
     * even though the last keep-awake poll was more than five minutes ago. */
    expect_target(&screen, 1500000, false, HEALTH_SCREEN_ACTIVE);
    expect_target(&screen, 1619999, false, HEALTH_SCREEN_ACTIVE);
    expect_target(&screen, 1620000, false, HEALTH_SCREEN_DIM);
    expect_target(&screen, 1799999, false, HEALTH_SCREEN_DIM);
    expect_target(&screen, 1800000, false, HEALTH_SCREEN_ASLEEP);
}

static void test_held_keys(void)
{
    health_screen_t screen = {0};
    health_input_t input = {0};
    expect_key(&screen, &input, HEALTH_KEY_OK, HEALTH_KEY_PRESS,
               0, false, false, HEALTH_ACTION_NONE);
    expect_target(&screen, 600000, false, HEALTH_SCREEN_ACTIVE);
    expect_key(&screen, &input, HEALTH_KEY_OK, HEALTH_KEY_LONG,
               600001, false, false, HEALTH_ACTION_VOICE_START);
    expect_key(&screen, &input, HEALTH_KEY_DOWN, HEALTH_KEY_PRESS,
               600002, false, false, HEALTH_ACTION_NONE);
    expect_key(&screen, &input, HEALTH_KEY_OK, HEALTH_KEY_RELEASE,
               600003, false, false, HEALTH_ACTION_VOICE_STOP);
    expect_target(&screen, 1200000, false, HEALTH_SCREEN_ACTIVE);
    expect_key(&screen, &input, HEALTH_KEY_DOWN, HEALTH_KEY_RELEASE,
               1200001, false, false, HEALTH_ACTION_NONE);
    /* No target call is needed at release to start the complete idle period. */
    expect_target(&screen, 1320000, false, HEALTH_SCREEN_ACTIVE);
    expect_target(&screen, 1320001, false, HEALTH_SCREEN_DIM);
    expect_target(&screen, 1500001, false, HEALTH_SCREEN_ASLEEP);
}

static void test_wake_gestures(void)
{
    const health_action_t shorts[] = {
        HEALTH_ACTION_VOLUME_UP, HEALTH_ACTION_VOLUME_DOWN, HEALTH_ACTION_CONFIRM,
    };
    const health_action_t longs[] = {
        HEALTH_ACTION_PREVIOUS_MODE, HEALTH_ACTION_NEXT_MODE, HEALTH_ACTION_VOICE_START,
    };
    for (health_key_t owner = HEALTH_KEY_UP; owner <= HEALTH_KEY_OK; owner++) {
        for (unsigned long_press = 0; long_press < 2; long_press++) {
            health_screen_t screen = {0};
            health_input_t input = {0};
            expect_key(&screen, &input, owner, HEALTH_KEY_PRESS,
                       300000, true, true, HEALTH_ACTION_NONE);
            expect_target(&screen, 300000, false, HEALTH_SCREEN_ACTIVE);
            /* Hardware has woken, but the entire first gesture stays hidden. */
            if (long_press) {
                expect_key(&screen, &input, owner, HEALTH_KEY_LONG,
                           300600, false, true, HEALTH_ACTION_NONE);
                expect_key(&screen, &input, owner, HEALTH_KEY_LONG,
                           300601, false, true, HEALTH_ACTION_NONE);
            }
            expect_key(&screen, &input, owner, HEALTH_KEY_RELEASE,
                       301000, false, true, HEALTH_ACTION_NONE);
            assert(!input.active && !input.held);
            expect_target(&screen, 420999, false, HEALTH_SCREEN_ACTIVE);
            expect_target(&screen, 421000, false, HEALTH_SCREEN_DIM);

            /* A fresh tap and hold must still reach normal business actions. */
            expect_key(&screen, &input, owner, HEALTH_KEY_PRESS,
                       422000, false, false, HEALTH_ACTION_NONE);
            expect_key(&screen, &input, owner, HEALTH_KEY_RELEASE,
                       422001, false, false, shorts[owner]);
            expect_key(&screen, &input, owner, HEALTH_KEY_PRESS,
                       423000, false, false, HEALTH_ACTION_NONE);
            expect_key(&screen, &input, owner, HEALTH_KEY_LONG,
                       424000, false, false, longs[owner]);
            expect_key(&screen, &input, owner, HEALTH_KEY_RELEASE,
                       425000, false, false,
                       owner == HEALTH_KEY_OK ? HEALTH_ACTION_VOICE_STOP : HEALTH_ACTION_NONE);
            assert(!input.active && !input.held);
        }
    }
}

static void test_overlapping_wake_gestures(void)
{
    for (health_key_t owner = HEALTH_KEY_UP; owner <= HEALTH_KEY_OK; owner++) {
        for (health_key_t other = HEALTH_KEY_UP; other <= HEALTH_KEY_OK; other++) {
            if (owner == other) continue;
            for (unsigned owner_first = 0; owner_first < 2; owner_first++) {
                health_screen_t screen = {0};
                health_input_t input = {0};
                expect_key(&screen, &input, owner, HEALTH_KEY_PRESS,
                           300000, true, true, HEALTH_ACTION_NONE);
                expect_key(&screen, &input, other, HEALTH_KEY_PRESS,
                           300001, false, true, HEALTH_ACTION_NONE);
                expect_key(&screen, &input, other, HEALTH_KEY_LONG,
                           301000, false, true, HEALTH_ACTION_NONE);
                health_key_t first = owner_first ? owner : other;
                health_key_t last = owner_first ? other : owner;
                expect_key(&screen, &input, first, HEALTH_KEY_RELEASE,
                           301001, false, true, HEALTH_ACTION_NONE);
                expect_key(&screen, &input, last, HEALTH_KEY_LONG,
                           302000, false, true, HEALTH_ACTION_NONE);
                /* Even a new overlapping press after the wake key's release
                 * remains part of the consumed gesture until every key is up. */
                expect_key(&screen, &input, first, HEALTH_KEY_PRESS,
                           302001, false, true, HEALTH_ACTION_NONE);
                expect_key(&screen, &input, last, HEALTH_KEY_RELEASE,
                           302002, false, true, HEALTH_ACTION_NONE);
                expect_key(&screen, &input, first, HEALTH_KEY_RELEASE,
                           302003, false, true, HEALTH_ACTION_NONE);
                assert(!screen.held && !input.active && !input.held);
                expect_key(&screen, &input, HEALTH_KEY_OK, HEALTH_KEY_PRESS,
                           303000, false, false, HEALTH_ACTION_NONE);
                expect_key(&screen, &input, HEALTH_KEY_OK, HEALTH_KEY_RELEASE,
                           303001, false, false, HEALTH_ACTION_CONFIRM);
            }
        }
    }
}

static void test_failed_wake(void)
{
    health_screen_t screen = {0};
    health_input_t input = {0};
    /* Computing ACTIVE must not pretend that a failed hardware wake succeeded.
     * Each later gesture is still wake-only while the caller reports asleep. */
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        uint64_t now = UINT64_C(300000) + attempt * UINT64_C(1000);
        expect_key(&screen, &input, HEALTH_KEY_OK, HEALTH_KEY_PRESS,
                   now, true, true, HEALTH_ACTION_NONE);
        expect_target(&screen, now, false, HEALTH_SCREEN_ACTIVE);
        expect_key(&screen, &input, HEALTH_KEY_OK, HEALTH_KEY_LONG,
                   now + 500, true, true, HEALTH_ACTION_NONE);
        expect_key(&screen, &input, HEALTH_KEY_OK, HEALTH_KEY_RELEASE,
                   now + 501, true, true, HEALTH_ACTION_NONE);
        assert(!input.active && !input.held);
    }
    expect_key(&screen, &input, HEALTH_KEY_UP, HEALTH_KEY_PRESS,
               304000, true, true, HEALTH_ACTION_NONE);
    expect_key(&screen, &input, HEALTH_KEY_UP, HEALTH_KEY_RELEASE,
               304001, false, true, HEALTH_ACTION_NONE);
    expect_key(&screen, &input, HEALTH_KEY_OK, HEALTH_KEY_PRESS,
               305000, false, false, HEALTH_ACTION_NONE);
    expect_key(&screen, &input, HEALTH_KEY_OK, HEALTH_KEY_RELEASE,
               305001, false, false, HEALTH_ACTION_CONFIRM);
}

int main(void)
{
    test_idle_boundaries();
    test_activity_reset();
    test_keep_awake();
    test_held_keys();
    test_wake_gestures();
    test_overlapping_wake_gestures();
    test_failed_wake();
    puts("Health screen: PASS (idle, protection, wake consumption, failed wake, 64-bit uptime)");
    return 0;
}
