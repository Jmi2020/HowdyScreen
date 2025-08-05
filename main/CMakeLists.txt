idf_component_register(SRCS "howdy_minimal_integration.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer bsp esp_event nvs_flash
                                audio_processor websocket_client)
