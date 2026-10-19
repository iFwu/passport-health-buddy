idf_component_register(
    SRCS "main.c"
         "plank_core.c"
         "plank_audio.c"
         "plank_storage.c"
         "plank_ui.c"
         "plank_digits.c"
         "../assets/fonts/plank_zh_16.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "qjson.c"
         "b64_stream.c"
         "audio_dsp.c"
         "portal_form.c"
         "tls_mfl.c"
         "health_exercise.c"
         "health_log.c"
         "health_activity.c"
         "health_motion.c"
         "health_cfg.c"
         "health_wifi.c"
         "health_portal.c"
         "health_ui.c"
         "health_input.c"
         "step_rx.c"
         "step_voice.c"
         "step_trace.c"
         "voice_trace.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_client esp_http_server
                 mbedtls esp_websocket_client
    EMBED_TXTFILES "portal.html"
)

target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=mbedtls_ssl_config_defaults"
    "-Wl,-u,__wrap_mbedtls_ssl_config_defaults")
