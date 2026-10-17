#include "plank_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void start(plank_model_t *m, uint64_t now)
{
    assert(plank_key(m, PLANK_OK, now) == PLANK_EFFECT_PREP_1);
    assert(m->state == PLANK_PREPARING);
    assert(plank_prepare_seconds(m, now) == 4);
    assert(plank_tick(m, now + 999) == 0);
    assert(plank_tick(m, now + 1000) == PLANK_EFFECT_PREP_2);
    assert(plank_tick(m, now + 2000) == PLANK_EFFECT_PREP_3);
    assert(plank_tick(m, now + 3000) == PLANK_EFFECT_PREP_4);
    assert(plank_tick(m, now + PLANK_PREPARE_MS - 1) == 0);
    assert(plank_prepare_seconds(m, now + PLANK_PREPARE_MS - 1) == 1);
    assert(plank_tick(m, now + PLANK_PREPARE_MS) == PLANK_EFFECT_START);
    assert(plank_elapsed(m, now + PLANK_PREPARE_MS) == 0);
}

static void test_cancel_and_stop(void)
{
    for (plank_key_t k = PLANK_UP; k <= PLANK_OK; k++) {
        plank_model_t m;
        plank_init(&m, NULL, true);
        plank_key(&m, PLANK_OK, 1000);
        plank_key(&m, k, 3000);
        assert(m.state == PLANK_READY && m.history.count == 0);
        assert(plank_tick(&m, 6000) == 0);
        start(&m, 10000);
        assert(plank_key(&m, k, 10000 + PLANK_PREPARE_MS + 600) ==
               (PLANK_EFFECT_STOP | PLANK_EFFECT_SAVE));
        assert(m.result_ms == 600 && m.history.duration_ms[0] == 600);
        assert(!plank_record_saved(&m, 0));
        assert(m.save == PLANK_SAVE_PENDING);
        plank_key(&m, PLANK_OK, 16000);
        assert(m.state == PLANK_RESULT && m.history.count == 1);
        plank_saved(&m, true);
        assert(plank_record_saved(&m, 0));
    }
    plank_model_t m;
    plank_init(&m, NULL, true);
    start(&m, 0);
    plank_key(&m, PLANK_DOWN, PLANK_PREPARE_MS);
    assert(m.history.count == 1 && m.history.duration_ms[0] == 0);
}

static void test_delayed_ticks(void)
{
    plank_model_t m;
    plank_init(&m, NULL, true);
    plank_key(&m, PLANK_OK, 100);
    assert(plank_tick(&m, 9000) == PLANK_EFFECT_START);
    assert(plank_elapsed(&m, 9000) == 0);
    assert(plank_tick(&m, 18999) == 0);
    assert(plank_tick(&m, 19000) == PLANK_EFFECT_CUE_10);
    assert(plank_tick(&m, 29000) == PLANK_EFFECT_CUE_10);
    assert(plank_tick(&m, 39000) == PLANK_EFFECT_CUE_30);
    assert(plank_tick(&m, 39001) == 0);
    assert(plank_tick(&m, 59000) == PLANK_EFFECT_CUE_10);
    assert(plank_tick(&m, 69000) == PLANK_EFFECT_CUE_60);
    assert(plank_tick(&m, 159000) == PLANK_EFFECT_CUE_30);
    assert(plank_tick(&m, 159001) == 0);
    assert(plank_elapsed(&m, 159001) == 150001);
    assert(plank_elapsed(&m, 8999) == 0);
    plank_key(&m, PLANK_UP, 159020);
    assert(m.result_ms == 150020);
}

static void test_periodic_cue_priority_after_delay(void)
{
    plank_model_t m;
    plank_init(&m, NULL, true);
    start(&m, 0);
    assert(plank_tick(&m, 66000) == PLANK_EFFECT_CUE_60);
    assert(plank_tick(&m, 66001) == 0);
    assert(plank_tick(&m, 96000) == PLANK_EFFECT_CUE_30);
    assert(plank_tick(&m, 125000) == PLANK_EFFECT_CUE_60);
}

static void test_failure_retry_and_reload(void)
{
    plank_model_t m;
    plank_init(&m, NULL, true);
    start(&m, 0);
    plank_key(&m, PLANK_OK, PLANK_PREPARE_MS + 400);
    plank_saved(&m, false);
    assert(m.save == PLANK_SAVE_FAILED && !plank_record_saved(&m, 0));
    assert(plank_key(&m, PLANK_UP, 6000) == PLANK_EFFECT_SAVE);
    plank_saved(&m, false);
    assert(m.history.count == 1);
    plank_key(&m, PLANK_DOWN, 6100);
    plank_key(&m, PLANK_OK, 6200);
    assert(m.state == PLANK_READY);
    assert(plank_key(&m, PLANK_UP, 6300) == PLANK_EFFECT_SAVE);
    plank_saved(&m, false);
    start(&m, 10000);
    plank_key(&m, PLANK_OK, 10000 + PLANK_PREPARE_MS + 800);
    assert(m.history.count == 2 && m.history.duration_ms[1] == 400);
    plank_saved(&m, true);
    assert(plank_record_saved(&m, 0) && plank_record_saved(&m, 1));
    plank_model_t reboot;
    plank_init(&reboot, &m.history, true);
    assert(reboot.state == PLANK_READY && reboot.history.count == 2);
    assert(plank_record_saved(&reboot, 0));
    start(&reboot, 0);
    plank_model_t power_loss;
    plank_init(&power_loss, &m.history, true);
    assert(power_loss.history.count == 2 && power_loss.state == PLANK_READY);
}

static void test_history_window(void)
{
    plank_model_t m;
    plank_init(&m, NULL, true);
    for (unsigned i = 0; i < 25; i++) {
        start(&m, i * 10000);
        plank_key(&m, PLANK_OK, i * 10000 + PLANK_PREPARE_MS + i);
        plank_saved(&m, i < 22);
    }
    assert(m.history.count == 20 && m.history.revision == 25);
    for (unsigned i = 0; i < 20; i++) {
        assert(m.history.duration_ms[i] == 24 - i);
        assert(plank_record_saved(&m, i) == (i >= 3));
    }
    plank_key(&m, PLANK_DOWN, 300000);
    assert(m.state == PLANK_HISTORY && m.history_index == 0);
    plank_key(&m, PLANK_UP, 300000);
    assert(m.history_index == 0);
    for (unsigned i = 0; i < 30; i++) plank_key(&m, PLANK_DOWN, 300000);
    assert(m.history_index == 19);
    plank_key(&m, PLANK_OK, 300000);
    assert(m.state == PLANK_READY);
}

static void test_bad_storage_and_format(void)
{
    plank_history_t h = { .version = 2 };
    plank_model_t m;
    plank_init(&m, &h, true);
    assert(!m.storage_available);
    start(&m, 0);
    assert(plank_key(&m, PLANK_OK, PLANK_PREPARE_MS + 300) == PLANK_EFFECT_STOP);
    assert(m.save == PLANK_SAVE_FAILED);
    assert(plank_key(&m, PLANK_UP, 5400) == 0);
    assert(m.history.duration_ms[0] == 300);
    h = (plank_history_t){ .version = 1, .count = 21, .revision = 21 };
    assert(!plank_history_valid(&h));
    h.count = 1;
    assert(!plank_history_valid(&h));
    h.revision = UINT64_MAX;
    assert(!plank_history_valid(&h));
    const uint64_t times[] = {0, 99, 600, 59999, 60000, 3599999, 3600000, 359999999, UINT64_MAX};
    const char *expected[] = {"00:00.00", "00:00.09", "00:00.60", "00:59.99", "01:00.00",
                              "59:59.99", "01:00:00", "99:59:59", "99h+"};
    for (unsigned i = 0; i < sizeof(times) / sizeof(times[0]); i++) {
        char text[16];
        plank_format_time(times[i], text, sizeof(text));
        assert(strcmp(text, expected[i]) == 0);
    }
    char short_text[2];
    plank_format_time(60000, short_text, sizeof(short_text));
    assert(short_text[1] == '\0');
}

int main(void)
{
    test_cancel_and_stop();
    test_delayed_ticks();
    test_periodic_cue_priority_after_delay();
    test_failure_retry_and_reload();
    test_history_window();
    test_bad_storage_and_format();
    puts("Plank core: PASS");
    return 0;
}
