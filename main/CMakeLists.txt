idf_component_register(SRCS "howdy_phase2_test.c"
                             "simple_wifi_manager.c"
                             "voice_assistant_ui.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer bsp esp_event nvs_flash esp_wifi esp_netif lwip
                                audio_processor service_discovery)
