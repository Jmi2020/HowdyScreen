idf_component_register(SRCS "howdy_phase6_howdytts_integration.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer esp_event ui_manager wifi_manager websocket_client audio_processor esp32_p4_wifi6_touch_lcd_xc)
