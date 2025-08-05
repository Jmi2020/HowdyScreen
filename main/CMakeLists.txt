idf_component_register(SRCS "howdy_phase3b_websocket.c"
                             "simple_wifi_manager.c"
                             "voice_assistant_ui.c"
                             "images/howdy_img_armraisehowdy.c"
                             "images/howdy_img_howdybackward.c"
                             "images/howdy_img_howdyleft.c"
                             "images/howdy_img_howdymidget.c"
                             "images/howdy_img_howdyright.c"
                             "images/howdy_img_howdyright2.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer bsp esp_event nvs_flash esp_wifi esp_netif lwip
                                audio_processor service_discovery howdytts_http_client websocket_client ui_manager)
