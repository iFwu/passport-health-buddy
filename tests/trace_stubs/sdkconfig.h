#pragma once

/* Match ESP-IDF bool symbols: disabled means undefined, never defined as 0.
 * Compile twice with -DTEST_TRACE_TEXT=0 and -DTEST_TRACE_TEXT=1. */
#ifndef TEST_TRACE_TEXT
#define TEST_TRACE_TEXT 0
#endif
#if TEST_TRACE_TEXT
#ifndef CONFIG_HEALTH_VOICE_TRACE_TEXT
#define CONFIG_HEALTH_VOICE_TRACE_TEXT 1
#endif
#endif
