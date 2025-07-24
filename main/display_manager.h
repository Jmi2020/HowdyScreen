#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "lvgl.h"
#include "audio_pipeline.h"
#include "esp_err.h"

// Forward declarations for simplified build
typedef enum {
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_ERROR
} network_state_t;

typedef struct {
    lv_disp_t *display;
    lv_indev_t *indev;
    lv_obj_t *screen;
    lv_obj_t *audio_meter;
    lv_obj_t *status_label;
    lv_obj_t *wifi_label;
    lv_obj_t *level_arc;
    lv_obj_t *center_button;
    lv_obj_t *howdy_gif;  // Howdy animation during processing
    lv_meter_indicator_t *level_indicator;
    bool initialized;
    bool muted;
    bool processing;  // Flag to show processing animation
} display_manager_t;

/**
 * Initialize the display manager and LVGL
 */
esp_err_t display_manager_init(display_manager_t *manager);

/**
 * Create the main audio interface
 */
esp_err_t display_create_ui(display_manager_t *manager);

/**
 * Update audio level visualization
 */
void display_update_audio_level(display_manager_t *manager, const audio_analysis_t *analysis);

/**
 * Update network status display
 */
void display_update_network_status(display_manager_t *manager, network_state_t state, int rssi);

/**
 * Set status message
 */
void display_set_status(display_manager_t *manager, const char *status, lv_color_t color);

/**
 * Handle LVGL timer (call regularly)
 */
void display_task_handler(display_manager_t *manager);

/**
 * Set mute state
 */
void display_set_mute(display_manager_t *manager, bool muted);

/**
 * Get mute state
 */
bool display_is_muted(display_manager_t *manager);

/**
 * Show/hide processing animation
 */
void display_show_processing(display_manager_t *manager, bool show);

/**
 * Deinitialize display manager
 */
esp_err_t display_manager_deinit(display_manager_t *manager);

#endif // DISPLAY_MANAGER_H