#include "ui_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>
#include "images/howdy_images.h"

static const char *TAG = "UIManager";

// Global UI manager instance
static ui_manager_t s_ui_manager = {0};
static bool s_initialized = false;

// Color definitions - HowdyTTS theme
#define HOWDY_COLOR_PRIMARY     lv_color_hex(0x1a73e8)  // Google Blue
#define HOWDY_COLOR_SECONDARY   lv_color_hex(0x34a853)  // Google Green
#define HOWDY_COLOR_ACCENT      lv_color_hex(0xfbbc04)  // Google Yellow
#define HOWDY_COLOR_ERROR       lv_color_hex(0xea4335)  // Google Red
#define HOWDY_COLOR_BACKGROUND  lv_color_hex(0x202124)  // Dark background
#define HOWDY_COLOR_SURFACE     lv_color_hex(0x303134)  // Card background
#define HOWDY_COLOR_ON_SURFACE  lv_color_hex(0xe8eaed)  // Text on surface

// Button callback for center character/mute button
static void center_button_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Howdy character clicked - toggling mute");
        s_ui_manager.muted = !s_ui_manager.muted;
        
        // Update mic icon based on mute state
        if (s_ui_manager.current_state == UI_STATE_IDLE) {
            lv_label_set_text(s_ui_manager.mic_icon, s_ui_manager.muted ? "🔇" : "🎤");
        }
        
        ESP_LOGI(TAG, "Audio %s", s_ui_manager.muted ? "muted" : "unmuted");
        
    } else if (code == LV_EVENT_PRESSING) {
        // Visual feedback - scale the character image slightly
        lv_obj_set_style_transform_zoom(s_ui_manager.howdy_character, 240, 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        // Return character to normal size
        lv_obj_set_style_transform_zoom(s_ui_manager.howdy_character, 256, 0);
    }
}

static void create_main_screen(void)
{
    ESP_LOGI(TAG, "Creating main screen...");
    
    // Create main screen
    s_ui_manager.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_COLOR_BACKGROUND, 0);
    
    // Main container (centered)
    lv_obj_t *main_container = lv_obj_create(s_ui_manager.screen);
    lv_obj_set_size(main_container, 800, 800);
    lv_obj_center(main_container);
    lv_obj_set_style_bg_color(main_container, HOWDY_COLOR_BACKGROUND, 0);
    lv_obj_set_style_border_width(main_container, 0, 0);
    lv_obj_set_style_pad_all(main_container, 0, 0);
    
    // Title label
    lv_obj_t *title_label = lv_label_create(main_container);
    lv_label_set_text(title_label, "HowdyTTS");
    lv_obj_set_style_text_color(title_label, HOWDY_COLOR_ON_SURFACE, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 40);
    
    // Audio level arc (circular meter)
    s_ui_manager.level_arc = lv_arc_create(main_container);
    lv_obj_set_size(s_ui_manager.level_arc, 300, 300);
    lv_obj_center(s_ui_manager.level_arc);
    lv_obj_set_style_arc_width(s_ui_manager.level_arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui_manager.level_arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_arc_set_range(s_ui_manager.level_arc, 0, 100);
    lv_arc_set_value(s_ui_manager.level_arc, 0);
    lv_obj_remove_style(s_ui_manager.level_arc, NULL, LV_PART_KNOB);
    
    // Howdy character image (center of circular display)
    s_ui_manager.howdy_character = lv_img_create(main_container);
    lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_armraisehowdy);
    lv_obj_set_size(s_ui_manager.howdy_character, 128, 128);
    lv_obj_center(s_ui_manager.howdy_character);
    
    // Center button (transparent overlay for touch interaction)
    s_ui_manager.center_button = lv_btn_create(main_container);
    lv_obj_set_size(s_ui_manager.center_button, 140, 140);
    lv_obj_center(s_ui_manager.center_button);
    lv_obj_set_style_bg_opa(s_ui_manager.center_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_ui_manager.center_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(s_ui_manager.center_button, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_ui_manager.center_button, center_button_event_cb, LV_EVENT_ALL, NULL);
    
    // Microphone icon (floating over character when needed)
    s_ui_manager.mic_icon = lv_label_create(main_container);
    lv_label_set_text(s_ui_manager.mic_icon, "");  // Initially hidden
    lv_obj_set_style_text_color(s_ui_manager.mic_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ui_manager.mic_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(s_ui_manager.mic_icon, LV_ALIGN_CENTER, 0, 80);  // Below character
    
    // Status label
    s_ui_manager.status_label = lv_label_create(main_container);
    lv_label_set_text(s_ui_manager.status_label, "Initializing...");
    lv_obj_set_style_text_color(s_ui_manager.status_label, HOWDY_COLOR_ON_SURFACE, 0);
    lv_obj_set_style_text_font(s_ui_manager.status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_ui_manager.status_label, LV_ALIGN_BOTTOM_MID, 0, -120);
    
    // WiFi status label (bottom left)
    s_ui_manager.wifi_label = lv_label_create(main_container);
    lv_label_set_text(s_ui_manager.wifi_label, LV_SYMBOL_WIFI " Connecting...");
    lv_obj_set_style_text_color(s_ui_manager.wifi_label, HOWDY_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(s_ui_manager.wifi_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_ui_manager.wifi_label, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    
    // Time/system status (bottom right)
    lv_obj_t *system_label = lv_label_create(main_container);
    lv_label_set_text(system_label, "HowdyScreen v1.0");
    lv_obj_set_style_text_color(system_label, HOWDY_COLOR_ON_SURFACE, 0);
    lv_obj_set_style_text_font(system_label, &lv_font_montserrat_14, 0);
    lv_obj_align(system_label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    
    ESP_LOGI(TAG, "Main screen created successfully");
}

static void update_ui_for_state(ui_state_t state)
{
    if (!s_initialized) {
        return;
    }
    
    switch (state) {
        case UI_STATE_INIT:
            lv_label_set_text(s_ui_manager.status_label, "Initializing...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SURFACE, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdybackward);  // Looking back/starting up
            lv_label_set_text(s_ui_manager.mic_icon, "");  // Hide icon during init
            break;
            
        case UI_STATE_IDLE:
            lv_label_set_text(s_ui_manager.status_label, "Tap to speak");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_PRIMARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_armraisehowdy);  // Friendly greeting pose
            lv_label_set_text(s_ui_manager.mic_icon, s_ui_manager.muted ? "🔇" : "🎤");
            break;
            
        case UI_STATE_LISTENING:
            lv_label_set_text(s_ui_manager.status_label, "Listening...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SECONDARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdyleft);  // Listening pose
            lv_label_set_text(s_ui_manager.mic_icon, "🎧");
            break;
            
        case UI_STATE_PROCESSING:
            lv_label_set_text(s_ui_manager.status_label, "Processing...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_ACCENT, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdymidget);  // Thinking pose
            lv_label_set_text(s_ui_manager.mic_icon, "🤔");
            break;
            
        case UI_STATE_SPEAKING:
            lv_label_set_text(s_ui_manager.status_label, "Speaking...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SECONDARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdyright2);  // Speaking pose
            lv_label_set_text(s_ui_manager.mic_icon, "🔊");
            break;
            
        case UI_STATE_ERROR:
            lv_label_set_text(s_ui_manager.status_label, "Error - Check connection");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_ERROR, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdybackward);  // Confused/error pose
            lv_label_set_text(s_ui_manager.mic_icon, "⚠️");
            break;
    }
    
    s_ui_manager.current_state = state;
    ESP_LOGI(TAG, "UI state updated to: %d", state);
}

esp_err_t ui_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "UI manager already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing UI manager...");
    
    // Initialize UI manager structure
    memset(&s_ui_manager, 0, sizeof(ui_manager_t));
    s_ui_manager.current_state = UI_STATE_INIT;
    s_ui_manager.muted = false;
    s_ui_manager.wifi_signal_strength = 0;
    
    // Create main screen
    create_main_screen();
    
    // Load the screen
    lv_scr_load(s_ui_manager.screen);
    
    s_initialized = true;
    ESP_LOGI(TAG, "UI manager initialized successfully");
    
    return ESP_OK;
}

esp_err_t ui_manager_set_state(ui_state_t state)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "UI manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    update_ui_for_state(state);
    return ESP_OK;
}

esp_err_t ui_manager_update_audio_level(int level)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Clamp level to 0-100
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    
    lv_arc_set_value(s_ui_manager.level_arc, level);
    return ESP_OK;
}

esp_err_t ui_manager_set_wifi_strength(int strength)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_ui_manager.wifi_signal_strength = strength;
    
    char wifi_text[32];
    if (strength == 0) {
        snprintf(wifi_text, sizeof(wifi_text), LV_SYMBOL_WIFI " Disconnected");
        lv_obj_set_style_text_color(s_ui_manager.wifi_label, HOWDY_COLOR_ERROR, 0);
    } else if (strength < 25) {
        snprintf(wifi_text, sizeof(wifi_text), LV_SYMBOL_WIFI " Weak (%d%%)", strength);
        lv_obj_set_style_text_color(s_ui_manager.wifi_label, HOWDY_COLOR_ACCENT, 0);
    } else if (strength < 75) {
        snprintf(wifi_text, sizeof(wifi_text), LV_SYMBOL_WIFI " Good (%d%%)", strength);
        lv_obj_set_style_text_color(s_ui_manager.wifi_label, HOWDY_COLOR_SECONDARY, 0);
    } else {
        snprintf(wifi_text, sizeof(wifi_text), LV_SYMBOL_WIFI " Excellent (%d%%)", strength);
        lv_obj_set_style_text_color(s_ui_manager.wifi_label, HOWDY_COLOR_SECONDARY, 0);
    }
    
    lv_label_set_text(s_ui_manager.wifi_label, wifi_text);
    return ESP_OK;
}

esp_err_t ui_manager_set_mute(bool muted)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_ui_manager.muted = muted;
    
    // Update center button appearance
    if (muted) {
        lv_obj_set_style_bg_color(s_ui_manager.center_button, HOWDY_COLOR_ERROR, 0);
        lv_label_set_text(s_ui_manager.mic_icon, LV_SYMBOL_VOLUME_MID);
    } else {
        lv_obj_set_style_bg_color(s_ui_manager.center_button, HOWDY_COLOR_PRIMARY, 0);
        lv_label_set_text(s_ui_manager.mic_icon, LV_SYMBOL_AUDIO);
    }
    
    return ESP_OK;
}

esp_err_t ui_manager_update_status(const char *status)
{
    if (!s_initialized || !status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    lv_label_set_text(s_ui_manager.status_label, status);
    return ESP_OK;
}

ui_state_t ui_manager_get_state(void)
{
    return s_ui_manager.current_state;
}

bool ui_manager_is_muted(void)
{
    return s_ui_manager.muted;
}

// Animation timers
static lv_timer_t *s_listening_animation_timer = NULL;
static lv_timer_t *s_processing_animation_timer = NULL;
static uint16_t s_animation_step = 0;

/**
 * @brief Listening animation callback (pulsing effect)
 */
static void listening_animation_cb(lv_timer_t *timer)
{
    if (!s_initialized || !s_ui_manager.center_button) {
        return;
    }
    
    // Create pulsing effect by scaling the center button
    s_animation_step = (s_animation_step + 10) % 360;
    float scale_factor = 1.0f + 0.1f * sinf(s_animation_step * 3.14159f / 180.0f);
    uint16_t scale = (uint16_t)(256 * scale_factor); // LV_IMG_ZOOM_NONE = 256
    
    lv_obj_set_style_transform_zoom(s_ui_manager.center_button, scale, 0);
}

/**
 * @brief Processing animation callback (spinner)
 */
static void processing_animation_cb(lv_timer_t *timer)
{
    if (!s_initialized || !s_ui_manager.level_arc) {
        return;
    }
    
    // Rotate the arc to create spinner effect
    s_animation_step = (s_animation_step + 10) % 360;
    lv_obj_set_style_transform_angle(s_ui_manager.level_arc, s_animation_step * 10, 0);
}

esp_err_t ui_manager_show_voice_assistant_state(const char *state_name, 
                                               const char *status_text, 
                                               float audio_level)
{
    if (!s_initialized || !state_name || !status_text) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Voice assistant state: %s - %s (level: %.2f)", state_name, status_text, audio_level);
    
    // Update status text
    lv_label_set_text(s_ui_manager.status_label, status_text);
    
    // Update audio level (convert 0.0-1.0 to 0-100)
    int level_percent = (int)(audio_level * 100);
    ui_manager_update_audio_level(level_percent);
    
    // Update UI based on HowdyTTS server states (exact match)
    if (strcmp(state_name, "waiting") == 0 || strcmp(state_name, "READY") == 0) {
        ui_manager_set_state(UI_STATE_IDLE);
        ui_manager_stop_listening_animation();
        ui_manager_stop_processing_animation();
        
    } else if (strcmp(state_name, "listening") == 0 || strcmp(state_name, "LISTENING") == 0) {
        ui_manager_set_state(UI_STATE_LISTENING);
        ui_manager_start_listening_animation();
        ui_manager_stop_processing_animation();
        
    } else if (strcmp(state_name, "thinking") == 0 || strcmp(state_name, "PROCESSING") == 0) {
        ui_manager_set_state(UI_STATE_PROCESSING);
        ui_manager_stop_listening_animation();
        ui_manager_start_processing_animation();
        
    } else if (strcmp(state_name, "speaking") == 0 || strcmp(state_name, "SPEAKING") == 0) {
        ui_manager_set_state(UI_STATE_SPEAKING);
        ui_manager_stop_listening_animation();
        ui_manager_stop_processing_animation();
        
    } else if (strcmp(state_name, "ending") == 0) {
        // HowdyTTS "ending" state - show ending message then auto-return to waiting
        ui_manager_set_state(UI_STATE_IDLE);
        ui_manager_stop_listening_animation();
        ui_manager_stop_processing_animation();
        // Status text should show "Goodbye, partner! Happy trails!" or similar
        
    } else if (strcmp(state_name, "ERROR") == 0 || strcmp(state_name, "DISCONNECTED") == 0) {
        ui_manager_set_state(UI_STATE_ERROR);
        ui_manager_stop_listening_animation();
        ui_manager_stop_processing_animation();
        
    } else if (strcmp(state_name, "SEARCHING") == 0) {
        ui_manager_set_state(UI_STATE_INIT);
        ui_manager_stop_listening_animation();
        ui_manager_start_processing_animation();
    }
    
    return ESP_OK;
}

esp_err_t ui_manager_start_listening_animation(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop any existing animation
    if (s_listening_animation_timer) {
        lv_timer_del(s_listening_animation_timer);
    }
    
    // Start pulsing animation (60fps for smooth effect)
    s_animation_step = 0;
    s_listening_animation_timer = lv_timer_create(listening_animation_cb, 16, NULL);
    
    ESP_LOGI(TAG, "Started listening animation");
    return ESP_OK;
}

esp_err_t ui_manager_stop_listening_animation(void)
{
    if (s_listening_animation_timer) {
        lv_timer_del(s_listening_animation_timer);
        s_listening_animation_timer = NULL;
        
        // Reset button scale
        if (s_initialized && s_ui_manager.center_button) {
            lv_obj_set_style_transform_zoom(s_ui_manager.center_button, 256, 0);
        }
        
        ESP_LOGI(TAG, "Stopped listening animation");
    }
    
    return ESP_OK;
}

esp_err_t ui_manager_start_processing_animation(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop any existing animation
    if (s_processing_animation_timer) {
        lv_timer_del(s_processing_animation_timer);
    }
    
    // Start spinner animation (30fps)
    s_animation_step = 0;
    s_processing_animation_timer = lv_timer_create(processing_animation_cb, 33, NULL);
    
    ESP_LOGI(TAG, "Started processing animation");
    return ESP_OK;
}

esp_err_t ui_manager_stop_processing_animation(void)
{
    if (s_processing_animation_timer) {
        lv_timer_del(s_processing_animation_timer);
        s_processing_animation_timer = NULL;
        
        // Reset arc rotation
        if (s_initialized && s_ui_manager.level_arc) {
            lv_obj_set_style_transform_angle(s_ui_manager.level_arc, 0, 0);
        }
        
        ESP_LOGI(TAG, "Stopped processing animation");
    }
    
    return ESP_OK;
}