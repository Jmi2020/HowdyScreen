idf_component_register(
    SRCS "src/ui_manager.c"
         "images/howdy_img_armraisehowdy.c"
         "images/howdy_img_howdybackward.c"
         "images/howdy_img_howdyleft.c"
         "images/howdy_img_howdymidget.c"
         "images/howdy_img_howdyright.c"
         "images/howdy_img_howdyright2.c"
    INCLUDE_DIRS "include" "images"
    REQUIRES esp_lvgl_port lvgl
    PRIV_REQUIRES esp_timer
)