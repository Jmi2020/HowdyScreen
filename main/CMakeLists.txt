idf_component_register(SRCS "howdy_phase3b_display_test.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer esp_event ui_manager wifi_manager service_discovery)
