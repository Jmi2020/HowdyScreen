idf_component_register(
    SRCS "src/ui_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lvgl_port lvgl
    PRIV_REQUIRES esp_timer
)