idf_component_register(
    SRCS "src/ui_manager.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/main"
    REQUIRES esp_lvgl_port lvgl
    PRIV_REQUIRES esp_timer
)