idf_component_register(SRCS "howdy_minimal_integration.c"
                             "wifi_provisioning.c"
                             "wifi_provisioning_ui.c" 
                             "howdy_wifi_integration.c"
                             "network_manager.c"
                             "voice_assistant_ui.c"
                             "howdytts_client.c" 
                             "howdytts_integration.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer bsp esp_event nvs_flash esp_wifi esp_netif lwip
                                audio_processor websocket_client ui_manager)
