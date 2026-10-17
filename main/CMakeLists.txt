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
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash
)
