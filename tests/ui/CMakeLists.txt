cmake_minimum_required(VERSION 3.16)
project(plank_ui_test C CXX)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(../../managed_components/lvgl__lvgl lvgl)
add_executable(test_plank_ui test_plank_ui.c ../../main/plank_ui.c
    ../../main/plank_core.c ../../main/ui_pixel.c ../../main/plank_digits.c
    ../../assets/fonts/plank_zh_16.c)
target_include_directories(test_plank_ui PRIVATE ../../main)
target_link_libraries(test_plank_ui PRIVATE lvgl::lvgl)
add_executable(test_plank_audio ../test_plank_audio.c ../../main/plank_audio.c ../../main/plank_core.c)
target_include_directories(test_plank_audio PRIVATE ../../main)
