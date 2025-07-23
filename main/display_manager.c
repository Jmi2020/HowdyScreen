#include "display_manager.h"
#include "howdy_config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display_manager";

// Color definitions for HowdyTTS theme
#define COLOR_BACKGROUND    lv_color_hex(0x0a0a0a)
#define COLOR_PRIMARY       lv_color_hex(0x1a73e8)
#define COLOR_ACCENT        lv_color_hex(0x34a853)
#define COLOR_WARNING       lv_color_hex(0xfbbc04)
#define COLOR_ERROR         lv_color_hex(0xea4335)
#define COLOR_TEXT_PRIMARY  lv_color_hex(0xffffff)
#define COLOR_TEXT_SECONDARY lv_color_hex(0x9aa0a6)

// Display buffer - use internal SRAM for better performance
static lv_color_t display_buf1[DISPLAY_WIDTH * 100];
static lv_color_t display_buf2[DISPLAY_WIDTH * 100];

// Display driver functions (to be implemented based on your display driver)
static void display_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    // TODO: Implement display flush for your specific display driver
    // This would typically involve sending pixel data to the display controller
    
    // For now, just mark as flushed
    lv_disp_flush_ready(disp_drv);
}

static void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    // TODO: Implement touch reading for your specific touch controller
    // This would typically involve reading touch coordinates via I2C
    
    data->state = LV_INDEV_STATE_REL;
    data->point.x = 0;
    data->point.y = 0;
}

static void center_button_event_cb(lv_event_t *e)
{
    display_manager_t *manager = (display_manager_t*)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        // Toggle mute state
        manager->muted = !manager->muted;
        
        if (manager->muted) {
            lv_obj_set_style_bg_color(manager->center_button, COLOR_ERROR, 0);
            lv_label_set_text(lv_obj_get_child(manager->center_button, 0), LV_SYMBOL_VOLUME_MID);
            display_set_status(manager, "Muted", COLOR_ERROR);
        } else {
            lv_obj_set_style_bg_color(manager->center_button, COLOR_PRIMARY, 0);
            lv_label_set_text(lv_obj_get_child(manager->center_button, 0), LV_SYMBOL_AUDIO);
            display_set_status(manager, "Active", COLOR_ACCENT);
        }
        
        ESP_LOGI(TAG, "Mute toggled: %s", manager->muted ? "ON" : "OFF");
    }
}

esp_err_t display_manager_init(display_manager_t *manager)
{
    if (!manager) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(manager, 0, sizeof(display_manager_t));

    ESP_LOGI(TAG, "Initializing display manager");

    // Initialize LVGL
    lv_init();

    // Initialize display buffer
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, display_buf1, display_buf2, DISPLAY_WIDTH * 100);

    // Initialize display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    
    manager->display = lv_disp_drv_register(&disp_drv);
    if (!manager->display) {
        ESP_LOGE(TAG, "Failed to register display driver");
        return ESP_FAIL;
    }

    // Initialize touch input
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    
    manager->indev = lv_indev_drv_register(&indev_drv);
    if (!manager->indev) {
        ESP_LOGE(TAG, "Failed to register input driver");
        return ESP_FAIL;
    }

    manager->initialized = true;
    ESP_LOGI(TAG, "Display manager initialized successfully");
    return ESP_OK;
}

esp_err_t display_create_ui(display_manager_t *manager)
{
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Creating UI");

    // Create main screen
    manager->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(manager->screen, COLOR_BACKGROUND, 0);
    lv_scr_load(manager->screen);

    // Create circular audio meter
    manager->audio_meter = lv_meter_create(manager->screen);
    lv_obj_set_size(manager->audio_meter, 600, 600);
    lv_obj_center(manager->audio_meter);

    // Style the meter
    static lv_style_t meter_style;
    lv_style_init(&meter_style);
    lv_style_set_bg_color(&meter_style, COLOR_BACKGROUND);
    lv_style_set_border_color(&meter_style, COLOR_PRIMARY);
    lv_style_set_border_width(&meter_style, 3);
    lv_style_set_radius(&meter_style, LV_RADIUS_CIRCLE);
    lv_obj_add_style(manager->audio_meter, &meter_style, 0);

    // Add meter scale
    lv_meter_scale_t *scale = lv_meter_add_scale(manager->audio_meter);
    lv_meter_set_scale_range(manager->audio_meter, scale, 0, 100, 270, 135);
    lv_meter_set_scale_ticks(manager->audio_meter, scale, 21, 2, 10, COLOR_TEXT_SECONDARY);
    lv_meter_set_scale_major_ticks(manager->audio_meter, scale, 5, 4, 15, COLOR_TEXT_PRIMARY, 10);

    // Add audio level indicator (arc)
    manager->level_indicator = lv_meter_add_arc(manager->audio_meter, scale, 20, COLOR_ACCENT, 0);
    lv_meter_set_indicator_start_value(manager->audio_meter, manager->level_indicator, 0);
    lv_meter_set_indicator_end_value(manager->audio_meter, manager->level_indicator, 0);

    // Create center button for mute toggle
    manager->center_button = lv_btn_create(manager->screen);
    lv_obj_set_size(manager->center_button, 120, 120);
    lv_obj_center(manager->center_button);
    lv_obj_set_style_radius(manager->center_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(manager->center_button, COLOR_PRIMARY, 0);
    lv_obj_add_event_cb(manager->center_button, center_button_event_cb, LV_EVENT_CLICKED, manager);

    // Add microphone icon to center button
    lv_obj_t *icon = lv_label_create(manager->center_button);
    lv_label_set_text(icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(icon);

    // Create status label
    manager->status_label = lv_label_create(manager->screen);
    lv_label_set_text(manager->status_label, "Initializing...");
    lv_obj_set_style_text_font(manager->status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(manager->status_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(manager->status_label, LV_ALIGN_BOTTOM_MID, 0, -100);

    // Create WiFi status label
    manager->wifi_label = lv_label_create(manager->screen);
    lv_label_set_text(manager->wifi_label, LV_SYMBOL_WIFI " Disconnected");
    lv_obj_set_style_text_font(manager->wifi_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(manager->wifi_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(manager->wifi_label, LV_ALIGN_TOP_RIGHT, -20, 20);

    // Create level arc for additional visualization
    manager->level_arc = lv_arc_create(manager->screen);
    lv_obj_set_size(manager->level_arc, 680, 680);
    lv_obj_center(manager->level_arc);
    lv_arc_set_range(manager->level_arc, 0, 100);
    lv_arc_set_value(manager->level_arc, 0);
    lv_arc_set_bg_angles(manager->level_arc, 135, 45);
    lv_obj_set_style_arc_color(manager->level_arc, COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(manager->level_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(manager->level_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(manager->level_arc, 8, LV_PART_MAIN);
    lv_obj_remove_style(manager->level_arc, NULL, LV_PART_KNOB);

    ESP_LOGI(TAG, "UI created successfully");
    return ESP_OK;
}

void display_update_audio_level(display_manager_t *manager, const audio_analysis_t *analysis)
{
    if (!manager || !analysis || !manager->initialized) {
        return;
    }

    if (manager->muted) {
        // Show muted state
        lv_meter_set_indicator_end_value(manager->audio_meter, manager->level_indicator, 0);
        lv_arc_set_value(manager->level_arc, 0);
        return;
    }

    // Update meter indicator
    int meter_value = (int)(analysis->overall_level * 100);
    lv_meter_set_indicator_end_value(manager->audio_meter, manager->level_indicator, meter_value);
    
    // Update arc indicator
    lv_arc_set_value(manager->level_arc, meter_value);

    // Change colors based on level
    if (analysis->overall_level > 0.8f) {
        lv_obj_set_style_arc_color(manager->level_arc, COLOR_ERROR, LV_PART_INDICATOR);
    } else if (analysis->overall_level > 0.6f) {
        lv_obj_set_style_arc_color(manager->level_arc, COLOR_WARNING, LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_arc_color(manager->level_arc, COLOR_ACCENT, LV_PART_INDICATOR);
    }

    // Voice activity indication
    if (analysis->voice_detected) {
        lv_obj_set_style_border_color(manager->audio_meter, COLOR_ACCENT, 0);
        lv_obj_set_style_border_width(manager->audio_meter, 5, 0);
    } else {
        lv_obj_set_style_border_color(manager->audio_meter, COLOR_PRIMARY, 0);
        lv_obj_set_style_border_width(manager->audio_meter, 3, 0);
    }
}

void display_update_network_status(display_manager_t *manager, network_state_t state, int rssi)
{
    if (!manager || !manager->initialized || !manager->wifi_label) {
        return;
    }

    char wifi_text[64];
    lv_color_t color;

    switch (state) {
        case NETWORK_STATE_CONNECTED:
            snprintf(wifi_text, sizeof(wifi_text), LV_SYMBOL_WIFI " Connected (%d dBm)", rssi);
            color = COLOR_ACCENT;
            break;
        case NETWORK_STATE_CONNECTING:
            strcpy(wifi_text, LV_SYMBOL_WIFI " Connecting...");
            color = COLOR_WARNING;
            break;
        case NETWORK_STATE_DISCONNECTED:
            strcpy(wifi_text, LV_SYMBOL_WIFI " Disconnected");
            color = COLOR_TEXT_SECONDARY;
            break;
        case NETWORK_STATE_ERROR:
            strcpy(wifi_text, LV_SYMBOL_WIFI " Error");
            color = COLOR_ERROR;
            break;
        default:
            strcpy(wifi_text, LV_SYMBOL_WIFI " Unknown");
            color = COLOR_TEXT_SECONDARY;
            break;
    }

    lv_label_set_text(manager->wifi_label, wifi_text);
    lv_obj_set_style_text_color(manager->wifi_label, color, 0);
}

void display_set_status(display_manager_t *manager, const char *status, lv_color_t color)
{
    if (!manager || !status || !manager->initialized || !manager->status_label) {
        return;
    }

    lv_label_set_text(manager->status_label, status);
    lv_obj_set_style_text_color(manager->status_label, color, 0);
}

void display_task_handler(display_manager_t *manager)
{
    if (!manager || !manager->initialized) {
        return;
    }

    lv_timer_handler();
}

void display_set_mute(display_manager_t *manager, bool muted)
{
    if (!manager || !manager->initialized) {
        return;
    }

    manager->muted = muted;
    
    if (manager->center_button && lv_obj_get_child(manager->center_button, 0)) {
        if (muted) {
            lv_obj_set_style_bg_color(manager->center_button, COLOR_ERROR, 0);
            lv_label_set_text(lv_obj_get_child(manager->center_button, 0), LV_SYMBOL_VOLUME_MID);
            display_set_status(manager, "Muted", COLOR_ERROR);
        } else {
            lv_obj_set_style_bg_color(manager->center_button, COLOR_PRIMARY, 0);
            lv_label_set_text(lv_obj_get_child(manager->center_button, 0), LV_SYMBOL_AUDIO);
            display_set_status(manager, "Active", COLOR_ACCENT);
        }
    }
}

bool display_is_muted(display_manager_t *manager)
{
    if (!manager) {
        return false;
    }
    return manager->muted;
}

esp_err_t display_manager_deinit(display_manager_t *manager)
{
    if (!manager) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!manager->initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing display manager");

    // Clean up LVGL objects
    if (manager->screen) {
        lv_obj_del(manager->screen);
    }

    memset(manager, 0, sizeof(display_manager_t));
    ESP_LOGI(TAG, "Display manager deinitialized");
    return ESP_OK;
}