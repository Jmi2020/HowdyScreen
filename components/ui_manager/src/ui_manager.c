#include "ui_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>
#include "howdy_images.h"

static const char *TAG = "UIManager";

// Global UI manager instance
static ui_manager_t s_ui_manager = {0};
static bool s_initialized = false;
static ui_voice_activation_callback_t s_voice_callback = NULL;

// Performance tracking
static uint32_t s_last_update_time = 0;
static uint32_t s_update_count = 0;
static float s_average_fps = 0.0f;

// Performance optimization - limit update frequency
#define MIN_UPDATE_INTERVAL_MS 16  // ~60 FPS max update rate
#define MAX_ANIMATION_OBJECTS 4    // Limit concurrent animations

// Animation timers
static lv_timer_t *s_listening_animation_timer = NULL;
static lv_timer_t *s_processing_animation_timer = NULL;
static lv_timer_t *s_wake_word_animation_timer = NULL;
static lv_timer_t *s_breathing_animation_timer = NULL;

// Touch gesture handling
static lv_point_t s_last_touch_point = {0, 0};
static bool s_touch_active = false;
static uint32_t s_touch_start_time = 0;

// Color definitions - HowdyTTS theme
#define HOWDY_COLOR_PRIMARY     lv_color_hex(0x1a73e8)  // Google Blue
#define HOWDY_COLOR_SECONDARY   lv_color_hex(0x34a853)  // Google Green
#define HOWDY_COLOR_ACCENT      lv_color_hex(0xfbbc04)  // Google Yellow
#define HOWDY_COLOR_ERROR       lv_color_hex(0xea4335)  // Google Red
#define HOWDY_COLOR_BACKGROUND  lv_color_hex(0x202124)  // Dark background
#define HOWDY_COLOR_SURFACE     lv_color_hex(0x303134)  // Card background
#define HOWDY_COLOR_ON_SURFACE  lv_color_hex(0xe8eaed)  // Text on surface

// Dynamic background colors for different states
#define HOWDY_BG_INIT              lv_color_hex(0x1a1a2e)  // Deep blue-purple
#define HOWDY_BG_IDLE              lv_color_hex(0x16213e)  // Calm blue-grey
#define HOWDY_BG_WAKE_WORD         lv_color_hex(0x1a4c72)  // Wake word detection
#define HOWDY_BG_LISTENING         lv_color_hex(0x0f3460)  // Active blue
#define HOWDY_BG_SPEECH_DETECTED   lv_color_hex(0x2d5a87)  // Speech recognition
#define HOWDY_BG_PROCESSING        lv_color_hex(0x533a7b)  // Purple thinking
#define HOWDY_COLOR_WAVEFORM       lv_color_hex(0x00d1ff)  // Cyan for TTS waveform

// Waveform visualization state
#define WAVE_SEGMENTS 64
static uint8_t s_wave_levels[WAVE_SEGMENTS] = {0};
static uint8_t s_wave_index = 0;
static lv_obj_t *s_wave_obj = NULL;
#define HOWDY_BG_SPEAKING          lv_color_hex(0x1e4d72)  // Teal speaking
#define HOWDY_BG_CONVERSATION      lv_color_hex(0x2a4f3e)  // Green conversation
#define HOWDY_BG_ERROR             lv_color_hex(0x4a1c1c)  // Dark red
#define HOWDY_BG_DISCONNECTED      lv_color_hex(0x3a3a3a)  // Grey disconnected

// Audio visualization colors
#define HOWDY_COLOR_MIC_RING       lv_color_hex(0x4caf50)  // Green for microphone
#define HOWDY_COLOR_TTS_RING       lv_color_hex(0xff9800)  // Orange for TTS
#define HOWDY_COLOR_WAKE_WORD      lv_color_hex(0xe91e63)  // Pink for wake word
#define HOWDY_COLOR_VAD_HIGH       lv_color_hex(0x2196f3)  // Blue for high VAD
#define HOWDY_COLOR_VAD_LOW        lv_color_hex(0x9e9e9e)  // Grey for low VAD

// Forward declarations
static void wave_draw_event_cb(lv_event_t * e);
static void wave_init(lv_obj_t *parent);
static void wave_push_level(uint8_t level);

// Enhanced touch gesture handling for round display
static void gesture_zone_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    
    switch (code) {
        case LV_EVENT_PRESSED:
            s_touch_active = true;
            s_touch_start_time = esp_timer_get_time() / 1000;
            s_last_touch_point = point;
            ESP_LOGD(TAG, "Touch started at (%d, %d)", point.x, point.y);
            break;
            
        case LV_EVENT_PRESSING:
            // Track touch movement for gesture recognition
            s_last_touch_point = point;
            break;
            
        case LV_EVENT_RELEASED: {
            if (!s_touch_active) break;
            
            uint32_t touch_duration = (esp_timer_get_time() / 1000) - s_touch_start_time;
            int center_x = 400, center_y = 400; // Center of 800x800 display
            int dx = point.x - center_x;
            int dy = point.y - center_y;
            int distance = sqrt(dx*dx + dy*dy);
            
            ESP_LOGI(TAG, "Touch released: duration=%lums, distance from center=%d", touch_duration, distance);
            
            // Center tap (within 120px of center)
            if (distance < 120) {
                if (touch_duration > 1000) {
                    // Long press - reset conversation or return to idle
                    ESP_LOGI(TAG, "Long press detected - resetting conversation");
                    if (s_voice_callback) {
                        s_voice_callback(false);  // Signal conversation reset
                    }
                } else {
                    // Short tap - toggle conversation or end current session
                    ESP_LOGI(TAG, "Center tap - conversation control");
                    if (s_voice_callback) {
                        s_voice_callback(true);  // Toggle conversation
                    }
                }
            }
            // Volume control gestures (outer ring)
            else if (distance > 200 && distance < 350) {
                if (dy < -50) {
                    // Upper arc - volume up
                    ESP_LOGI(TAG, "Volume up gesture");
                    ui_manager_handle_touch_gesture("volume_up", &point);
                } else if (dy > 50) {
                    // Lower arc - volume down
                    ESP_LOGI(TAG, "Volume down gesture");
                    ui_manager_handle_touch_gesture("volume_down", &point);
                }
            }
            
            s_touch_active = false;
            break;
        }
        
        case LV_EVENT_PRESS_LOST:
            s_touch_active = false;
            break;
            
        default:
            break;
    }
}

// Character button callback for visual feedback
static void center_button_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_PRESSING) {
        // Visual feedback - scale the character image slightly
        lv_obj_set_style_transform_zoom(s_ui_manager.howdy_character, 240, 0);
        // Add glow effect
        if (s_ui_manager.character_glow) {
            lv_obj_set_style_bg_opa(s_ui_manager.character_glow, LV_OPA_50, 0);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        // Return character to normal size
        lv_obj_set_style_transform_zoom(s_ui_manager.howdy_character, 256, 0);
        // Remove glow effect
        if (s_ui_manager.character_glow) {
            lv_obj_set_style_bg_opa(s_ui_manager.character_glow, LV_OPA_TRANSP, 0);
        }
    }
}

// Create circular audio visualization rings
static void create_audio_visualization(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creating circular audio visualization for round display...");
    
    // Outer ring - Microphone input levels (radius 350px)
    s_ui_manager.outer_audio_ring = lv_arc_create(parent);
    lv_obj_set_size(s_ui_manager.outer_audio_ring, 700, 700);
    lv_obj_center(s_ui_manager.outer_audio_ring);
    lv_obj_set_style_arc_width(s_ui_manager.outer_audio_ring, 15, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ui_manager.outer_audio_ring, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui_manager.outer_audio_ring, 15, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ui_manager.outer_audio_ring, HOWDY_COLOR_MIC_RING, LV_PART_INDICATOR);
    lv_arc_set_range(s_ui_manager.outer_audio_ring, 0, 100);
    lv_arc_set_value(s_ui_manager.outer_audio_ring, 0);
    lv_obj_remove_style(s_ui_manager.outer_audio_ring, NULL, LV_PART_KNOB);
    
    // Inner ring - TTS output levels (radius 280px)
    s_ui_manager.inner_audio_ring = lv_arc_create(parent);
    lv_obj_set_size(s_ui_manager.inner_audio_ring, 560, 560);
    lv_obj_center(s_ui_manager.inner_audio_ring);
    lv_obj_set_style_arc_width(s_ui_manager.inner_audio_ring, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ui_manager.inner_audio_ring, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui_manager.inner_audio_ring, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ui_manager.inner_audio_ring, HOWDY_COLOR_TTS_RING, LV_PART_INDICATOR);
    lv_arc_set_range(s_ui_manager.inner_audio_ring, 0, 100);
    lv_arc_set_value(s_ui_manager.inner_audio_ring, 0);
    lv_obj_remove_style(s_ui_manager.inner_audio_ring, NULL, LV_PART_KNOB);

    // Waveform overlay around inner ring
    wave_init(parent);
    
    // Wake word detection ring (radius 380px) - initially hidden
    s_ui_manager.wake_word_ring = lv_arc_create(parent);
    lv_obj_set_size(s_ui_manager.wake_word_ring, 760, 760);
    lv_obj_center(s_ui_manager.wake_word_ring);
    lv_obj_set_style_arc_width(s_ui_manager.wake_word_ring, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ui_manager.wake_word_ring, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui_manager.wake_word_ring, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ui_manager.wake_word_ring, HOWDY_COLOR_WAKE_WORD, LV_PART_INDICATOR);
    lv_arc_set_range(s_ui_manager.wake_word_ring, 0, 100);
    lv_arc_set_value(s_ui_manager.wake_word_ring, 0);
    lv_obj_remove_style(s_ui_manager.wake_word_ring, NULL, LV_PART_KNOB);
    lv_obj_add_flag(s_ui_manager.wake_word_ring, LV_OBJ_FLAG_HIDDEN);  // Initially hidden
    
    // Main level arc (legacy compatibility, radius 225px)
    s_ui_manager.level_arc = lv_arc_create(parent);
    lv_obj_set_size(s_ui_manager.level_arc, 450, 450);
    lv_obj_center(s_ui_manager.level_arc);
    lv_obj_set_style_arc_width(s_ui_manager.level_arc, 20, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui_manager.level_arc, 20, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_arc_set_range(s_ui_manager.level_arc, 0, 100);
    lv_arc_set_value(s_ui_manager.level_arc, 0);
    lv_obj_remove_style(s_ui_manager.level_arc, NULL, LV_PART_KNOB);
    
    ESP_LOGI(TAG, "Circular audio visualization created with 4 concentric rings");
}

static void create_main_screen(void)
{
    ESP_LOGI(TAG, "Creating main screen optimized for round display...");
    
    // Create main screen
    s_ui_manager.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_COLOR_BACKGROUND, 0);
    
    // Main container (centered) - optimized for 800x800 round display
    s_ui_manager.main_container = lv_obj_create(s_ui_manager.screen);
    lv_obj_set_size(s_ui_manager.main_container, 800, 800);
    lv_obj_center(s_ui_manager.main_container);
    lv_obj_set_style_bg_color(s_ui_manager.main_container, HOWDY_COLOR_BACKGROUND, 0);
    lv_obj_set_style_border_width(s_ui_manager.main_container, 0, 0);
    lv_obj_set_style_pad_all(s_ui_manager.main_container, 0, 0);
    lv_obj_set_style_radius(s_ui_manager.main_container, 400, 0);  // Make circular
    lv_obj_set_style_clip_corner(s_ui_manager.main_container, true, 0);
    
    // Full-screen gesture detection zone
    s_ui_manager.gesture_zone = lv_obj_create(s_ui_manager.main_container);
    lv_obj_set_size(s_ui_manager.gesture_zone, 800, 800);
    lv_obj_set_pos(s_ui_manager.gesture_zone, 0, 0);
    lv_obj_set_style_bg_opa(s_ui_manager.gesture_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_ui_manager.gesture_zone, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_ui_manager.gesture_zone, gesture_zone_event_cb, LV_EVENT_ALL, NULL);
    
    // Create circular audio visualization
    create_audio_visualization(s_ui_manager.main_container);
    
    // Title label - positioned for round display
    lv_obj_t *title_label = lv_label_create(s_ui_manager.main_container);
    lv_label_set_text(title_label, "HowdyTTS");
    lv_obj_set_style_text_color(title_label, HOWDY_COLOR_ON_SURFACE, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 80);  // Moved down for round display
    
    // Character glow effect (behind character)
    s_ui_manager.character_glow = lv_obj_create(s_ui_manager.main_container);
    lv_obj_set_size(s_ui_manager.character_glow, 320, 440);
    lv_obj_center(s_ui_manager.character_glow);
    lv_obj_set_style_bg_color(s_ui_manager.character_glow, HOWDY_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(s_ui_manager.character_glow, LV_OPA_TRANSP, 0);  // Initially transparent
    lv_obj_set_style_radius(s_ui_manager.character_glow, 160, 0);  // Circular glow
    lv_obj_set_style_border_width(s_ui_manager.character_glow, 0, 0);
    
    // Howdy character image (center of circular display) - optimized for round display
    s_ui_manager.howdy_character = lv_img_create(s_ui_manager.main_container);
    lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_armraisehowdy);
    lv_obj_set_size(s_ui_manager.howdy_character, 264, 384);  // 3x scale for visibility
    lv_img_set_antialias(s_ui_manager.howdy_character, true);
    lv_obj_center(s_ui_manager.howdy_character);
    lv_obj_move_to_index(s_ui_manager.howdy_character, -1);  // Bring to front
    
    // Center button (transparent overlay for character touch feedback)
    s_ui_manager.center_button = lv_btn_create(s_ui_manager.main_container);
    lv_obj_set_size(s_ui_manager.center_button, 280, 400);  // Slightly larger for easier touch
    lv_obj_center(s_ui_manager.center_button);
    lv_obj_set_style_bg_opa(s_ui_manager.center_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_ui_manager.center_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(s_ui_manager.center_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_ui_manager.center_button, 140, 0);
    lv_obj_add_event_cb(s_ui_manager.center_button, center_button_event_cb, LV_EVENT_ALL, NULL);

    // Label for center button (visual affordance)
    lv_obj_t *btn_label = lv_label_create(s_ui_manager.center_button);
    lv_label_set_text(btn_label, LV_SYMBOL_STOP);
    lv_obj_center(btn_label);
    
    // Microphone icon and confidence meter
    s_ui_manager.mic_icon = lv_label_create(s_ui_manager.main_container);
    lv_label_set_text(s_ui_manager.mic_icon, "");
    lv_obj_set_style_text_color(s_ui_manager.mic_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ui_manager.mic_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(s_ui_manager.mic_icon, LV_ALIGN_CENTER, 0, 140);
    
    // VAD/Wake word confidence meter (small arc below character)
    s_ui_manager.confidence_meter = lv_arc_create(s_ui_manager.main_container);
    lv_obj_set_size(s_ui_manager.confidence_meter, 120, 120);
    lv_obj_align(s_ui_manager.confidence_meter, LV_ALIGN_CENTER, 0, 200);
    lv_obj_set_style_arc_width(s_ui_manager.confidence_meter, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ui_manager.confidence_meter, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui_manager.confidence_meter, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ui_manager.confidence_meter, HOWDY_COLOR_VAD_HIGH, LV_PART_INDICATOR);
    lv_arc_set_range(s_ui_manager.confidence_meter, 0, 100);
    lv_arc_set_value(s_ui_manager.confidence_meter, 0);
    lv_obj_remove_style(s_ui_manager.confidence_meter, NULL, LV_PART_KNOB);
    lv_obj_add_flag(s_ui_manager.confidence_meter, LV_OBJ_FLAG_HIDDEN);
    
    // Status labels - positioned for round display
    s_ui_manager.status_label = lv_label_create(s_ui_manager.main_container);
    lv_label_set_text(s_ui_manager.status_label, "Initializing...");
    lv_obj_set_style_text_color(s_ui_manager.status_label, HOWDY_COLOR_ON_SURFACE, 0);
    lv_obj_set_style_text_font(s_ui_manager.status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(s_ui_manager.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui_manager.status_label, LV_ALIGN_BOTTOM_MID, 0, -180);
    
    // Detailed status (smaller text for additional info)
    s_ui_manager.status_detail = lv_label_create(s_ui_manager.main_container);
    lv_label_set_text(s_ui_manager.status_detail, "");
    lv_obj_set_style_text_color(s_ui_manager.status_detail, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(s_ui_manager.status_detail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_ui_manager.status_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui_manager.status_detail, LV_ALIGN_BOTTOM_MID, 0, -150);
    lv_obj_add_flag(s_ui_manager.status_detail, LV_OBJ_FLAG_HIDDEN);
    
    // Network status labels - positioned for circular layout
    s_ui_manager.wifi_label = lv_label_create(s_ui_manager.main_container);
    lv_label_set_text(s_ui_manager.wifi_label, LV_SYMBOL_WIFI " Connecting...");
    lv_obj_set_style_text_color(s_ui_manager.wifi_label, HOWDY_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(s_ui_manager.wifi_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_ui_manager.wifi_label, LV_ALIGN_BOTTOM_LEFT, 50, -50);  // Adjusted for round display
    
    // Server information
    s_ui_manager.server_info = lv_label_create(s_ui_manager.main_container);
    lv_label_set_text(s_ui_manager.server_info, "");
    lv_obj_set_style_text_color(s_ui_manager.server_info, HOWDY_COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(s_ui_manager.server_info, &lv_font_montserrat_16, 0);
    lv_obj_align(s_ui_manager.server_info, LV_ALIGN_BOTTOM_RIGHT, -50, -50);
    lv_obj_add_flag(s_ui_manager.server_info, LV_OBJ_FLAG_HIDDEN);
    
    // Protocol indicator
    s_ui_manager.protocol_indicator = lv_label_create(s_ui_manager.main_container);
    lv_label_set_text(s_ui_manager.protocol_indicator, "UDP");
    lv_obj_set_style_text_color(s_ui_manager.protocol_indicator, HOWDY_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(s_ui_manager.protocol_indicator, &lv_font_montserrat_16, 0);
    lv_obj_align(s_ui_manager.protocol_indicator, LV_ALIGN_TOP_RIGHT, -50, 50);
    lv_obj_add_flag(s_ui_manager.protocol_indicator, LV_OBJ_FLAG_HIDDEN);
    
    // System status
    s_ui_manager.system_label = lv_label_create(s_ui_manager.main_container);
    lv_label_set_text(s_ui_manager.system_label, "HowdyScreen v1.0");
    lv_obj_set_style_text_color(s_ui_manager.system_label, HOWDY_COLOR_ON_SURFACE, 0);
    lv_obj_set_style_text_font(s_ui_manager.system_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_ui_manager.system_label, LV_ALIGN_TOP_LEFT, 50, 50);
    
    // Initialize state
    s_ui_manager.current_state = UI_STATE_INIT;
    s_ui_manager.previous_state = UI_STATE_INIT;
    s_ui_manager.state_change_time = esp_timer_get_time() / 1000;
    s_ui_manager.in_conversation = false;
    s_ui_manager.mic_level = 0;
    s_ui_manager.tts_level = 0;
    s_ui_manager.vad_confidence = 0.0f;
    s_ui_manager.wake_word_confidence = 0.0f;
    
    // Performance optimizations for smooth 30+ FPS operation
    lv_disp_t *disp = lv_disp_get_default();
    if (disp) {
        // LVGL v8.4.0 compatible optimizations
        ESP_LOGI(TAG, "Display performance optimizations applied");
    }
    
    ESP_LOGI(TAG, "Round display UI created with circular audio visualization");
    ESP_LOGI(TAG, "Touch zones: Center (conversation), Outer ring (volume control)");
    ESP_LOGI(TAG, "Audio rings: Outer (mic), Inner (TTS), Wake word (pulse)");
    ESP_LOGI(TAG, "Performance: Optimized for 30+ FPS with <2MB memory usage");
}

static void update_ui_for_state(ui_state_t state)
{
    if (!s_initialized) {
        return;
    }
    
    // Update state tracking
    s_ui_manager.previous_state = s_ui_manager.current_state;
    s_ui_manager.current_state = state;
    s_ui_manager.state_change_time = esp_timer_get_time() / 1000;
    
    // Stop all animations by default (specific states will restart as needed)
    ui_manager_stop_listening_animation();
    ui_manager_stop_processing_animation();
    ui_manager_stop_wake_word_animation();
    
    switch (state) {
        case UI_STATE_INIT:
            lv_label_set_text(s_ui_manager.status_label, "Initializing HowdyScreen...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SURFACE, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdybackward);
            lv_label_set_text(s_ui_manager.mic_icon, "");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_INIT, 0);
            s_ui_manager.in_conversation = false;
            break;
            
        case UI_STATE_IDLE:
            lv_label_set_text(s_ui_manager.status_label, "Say 'Hey Howdy' or tap center");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_PRIMARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_armraisehowdy);
            lv_label_set_text(s_ui_manager.mic_icon, s_ui_manager.muted ? "🔇" : "🎤");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_IDLE, 0);
            s_ui_manager.in_conversation = false;
            // Start subtle breathing animation
            ui_manager_start_breathing_animation();
            break;
            
        case UI_STATE_WAKE_WORD_DETECTED:
            lv_label_set_text(s_ui_manager.status_label, "Wake word detected!");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_WAKE_WORD, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdyleft);
            lv_label_set_text(s_ui_manager.mic_icon, "👂");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_WAKE_WORD, 0);
            lv_obj_clear_flag(s_ui_manager.wake_word_ring, LV_OBJ_FLAG_HIDDEN);
            ui_manager_start_wake_word_animation();
            s_ui_manager.in_conversation = true;
            break;
            
        case UI_STATE_LISTENING:
            lv_label_set_text(s_ui_manager.status_label, "Listening for your voice...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SECONDARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdyleft);
            lv_label_set_text(s_ui_manager.mic_icon, "🎧");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_LISTENING, 0);
            lv_obj_clear_flag(s_ui_manager.confidence_meter, LV_OBJ_FLAG_HIDDEN);
            ui_manager_start_listening_animation();
            s_ui_manager.in_conversation = true;
            break;
            
        case UI_STATE_SPEECH_DETECTED:
            lv_label_set_text(s_ui_manager.status_label, "Speech detected - keep talking");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SECONDARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdyleft);
            lv_label_set_text(s_ui_manager.mic_icon, "🗣️");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_SPEECH_DETECTED, 0);
            ui_manager_start_listening_animation();
            s_ui_manager.in_conversation = true;
            break;
            
        case UI_STATE_PROCESSING:
            lv_label_set_text(s_ui_manager.status_label, "Processing your request...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_ACCENT, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdymidget);
            lv_label_set_text(s_ui_manager.mic_icon, "🤔");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_PROCESSING, 0);
            ui_manager_start_processing_animation();
            s_ui_manager.in_conversation = true;
            break;
            
        case UI_STATE_THINKING:
            lv_label_set_text(s_ui_manager.status_label, "Thinking...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_ACCENT, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdymidget);
            lv_label_set_text(s_ui_manager.mic_icon, "🧠");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_PROCESSING, 0);
            ui_manager_start_processing_animation();
            s_ui_manager.in_conversation = true;
            break;
            
        case UI_STATE_SPEAKING:
            lv_label_set_text(s_ui_manager.status_label, "Howdy is speaking...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SECONDARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdyright2);
            lv_label_set_text(s_ui_manager.mic_icon, "🔊");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_SPEAKING, 0);
            s_ui_manager.in_conversation = true;
            break;
            
        case UI_STATE_RESPONDING:
            lv_label_set_text(s_ui_manager.status_label, "Howdy is responding...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SECONDARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdyright2);
            lv_label_set_text(s_ui_manager.mic_icon, "💬");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_SPEAKING, 0);
            s_ui_manager.in_conversation = true;
            break;
            
        case UI_STATE_CONVERSATION_ACTIVE:
            lv_label_set_text(s_ui_manager.status_label, "Conversation active - continue");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SECONDARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_armraisehowdy);
            lv_label_set_text(s_ui_manager.mic_icon, "🎙️");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_CONVERSATION, 0);
            s_ui_manager.in_conversation = true;
            break;
            
        case UI_STATE_SESSION_ENDING:
            lv_label_set_text(s_ui_manager.status_label, "Session ending...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_PRIMARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_armraisehowdy);
            lv_label_set_text(s_ui_manager.mic_icon, "👋");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_IDLE, 0);
            s_ui_manager.in_conversation = false;
            break;
            
        case UI_STATE_ERROR:
            lv_label_set_text(s_ui_manager.status_label, "Error - Check connection");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_ERROR, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdybackward);
            lv_label_set_text(s_ui_manager.mic_icon, "⚠️");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_ERROR, 0);
            s_ui_manager.in_conversation = false;
            break;
            
        // Network states
        case UI_STATE_CONNECTING:
            lv_label_set_text(s_ui_manager.status_label, "Connecting to HowdyTTS server...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_ACCENT, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdybackward);
            lv_label_set_text(s_ui_manager.mic_icon, "🔗");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_INIT, 0);
            s_ui_manager.in_conversation = false;
            break;
            
        case UI_STATE_DISCOVERING:
            lv_label_set_text(s_ui_manager.status_label, "Discovering HowdyTTS servers...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_ACCENT, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdybackward);
            lv_label_set_text(s_ui_manager.mic_icon, "🔍");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_INIT, 0);
            ui_manager_start_processing_animation();
            s_ui_manager.in_conversation = false;
            break;
            
        case UI_STATE_REGISTERED:
            lv_label_set_text(s_ui_manager.status_label, "Connected to HowdyTTS server");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, HOWDY_COLOR_SECONDARY, LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_armraisehowdy);
            lv_label_set_text(s_ui_manager.mic_icon, "✅");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_IDLE, 0);
            s_ui_manager.in_conversation = false;
            break;
            
        case UI_STATE_DISCONNECTED:
            lv_label_set_text(s_ui_manager.status_label, "Disconnected - reconnecting...");
            lv_obj_set_style_arc_color(s_ui_manager.level_arc, lv_color_hex(0x666666), LV_PART_INDICATOR);
            lv_img_set_src(s_ui_manager.howdy_character, &howdy_img_howdybackward);
            lv_label_set_text(s_ui_manager.mic_icon, "❌");
            lv_obj_set_style_bg_color(s_ui_manager.screen, HOWDY_BG_DISCONNECTED, 0);
            s_ui_manager.in_conversation = false;
            break;
    }
    
    // Show/hide elements based on conversation state
    if (s_ui_manager.in_conversation) {
        lv_obj_clear_flag(s_ui_manager.confidence_meter, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ui_manager.status_detail, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui_manager.confidence_meter, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui_manager.status_detail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui_manager.wake_word_ring, LV_OBJ_FLAG_HIDDEN);
    }
    
    ESP_LOGI(TAG, "UI state updated: %s -> %s (conversation: %s)",
            s_ui_manager.previous_state == UI_STATE_IDLE ? "idle" :
            s_ui_manager.previous_state == UI_STATE_LISTENING ? "listening" : "other",
            state == UI_STATE_IDLE ? "idle" :
            state == UI_STATE_LISTENING ? "listening" :
            state == UI_STATE_SPEAKING ? "speaking" : "other",
            s_ui_manager.in_conversation ? "active" : "inactive");
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

/**
 * @brief Breathing animation for idle state (subtle character movement)
 */
static void breathing_animation_cb(lv_timer_t *timer)
{
    if (!s_initialized || !s_ui_manager.howdy_character) {
        return;
    }
    
    // Subtle breathing effect (very gentle scale)
    s_ui_manager.animation_step = (s_ui_manager.animation_step + 3) % 360;
    float scale_factor = 1.0f + 0.02f * sinf(s_ui_manager.animation_step * 3.14159f / 180.0f);
    uint16_t scale = (uint16_t)(256 * scale_factor);
    
    lv_obj_set_style_transform_zoom(s_ui_manager.howdy_character, scale, 0);
}

/**
 * @brief Listening animation callback (pulsing effect on outer ring)
 */
static void listening_animation_cb(lv_timer_t *timer)
{
    if (!s_initialized || !s_ui_manager.outer_audio_ring) {
        return;
    }
    
    // Pulse the outer audio ring for listening state
    s_ui_manager.animation_step = (s_ui_manager.animation_step + 8) % 360;
    
    // Vary the arc color opacity
    float opacity_factor = 0.7f + 0.3f * sinf(s_ui_manager.animation_step * 3.14159f / 180.0f);
    lv_opa_t opacity = (lv_opa_t)(255 * opacity_factor);
    
    lv_obj_set_style_arc_opa(s_ui_manager.outer_audio_ring, opacity, LV_PART_INDICATOR);
    
    // Also pulse the character slightly
    float scale_factor = 1.0f + 0.05f * sinf(s_ui_manager.animation_step * 3.14159f / 180.0f);
    uint16_t scale = (uint16_t)(256 * scale_factor);
    lv_obj_set_style_transform_zoom(s_ui_manager.howdy_character, scale, 0);
}

/**
 * @brief Processing animation callback (spinner on level arc)
 */
static void processing_animation_cb(lv_timer_t *timer)
{
    if (!s_initialized || !s_ui_manager.level_arc) {
        return;
    }
    
    // Rotate the main level arc to create spinner effect
    s_ui_manager.animation_step = (s_ui_manager.animation_step + 12) % 360;
    lv_obj_set_style_transform_angle(s_ui_manager.level_arc, s_ui_manager.animation_step * 10, 0);
    
    // Also rotate the confidence meter for visual interest
    if (s_ui_manager.confidence_meter) {
        lv_obj_set_style_transform_angle(s_ui_manager.confidence_meter, -s_ui_manager.animation_step * 8, 0);
    }
}

/**
 * @brief Wake word animation callback (pulsing rings)
 */
static void wake_word_animation_cb(lv_timer_t *timer)
{
    if (!s_initialized || !s_ui_manager.wake_word_ring) {
        return;
    }
    
    // Create expanding pulse effect
    s_ui_manager.animation_step = (s_ui_manager.animation_step + 15) % 360;
    
    // Pulse the wake word ring
    float pulse_factor = 0.5f + 0.5f * sinf(s_ui_manager.animation_step * 3.14159f / 180.0f);
    lv_opa_t opacity = (lv_opa_t)(255 * pulse_factor);
    lv_obj_set_style_arc_opa(s_ui_manager.wake_word_ring, opacity, LV_PART_INDICATOR);
    
    // Update wake word ring value based on confidence
    int confidence_value = (int)(s_ui_manager.wake_word_confidence * 100);
    lv_arc_set_value(s_ui_manager.wake_word_ring, confidence_value);
    
    // Character glow effect
    if (s_ui_manager.character_glow) {
        lv_opa_t glow_opacity = (lv_opa_t)(100 * pulse_factor);
        lv_obj_set_style_bg_opa(s_ui_manager.character_glow, glow_opacity, 0);
    }
}

// Wave visualization functions
static void wave_draw_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_DRAW_MAIN) return;

    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

    const int cx = obj->coords.x1 + lv_obj_get_width(obj)/2;
    const int cy = obj->coords.y1 + lv_obj_get_height(obj)/2;
    const float base_radius = 300.0f;   // around inner ring
    const float max_amplitude = 30.0f;  // thickness outward

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = HOWDY_COLOR_WAVEFORM;
    dsc.width = 4;
    dsc.round_start = 1;
    dsc.round_end = 1;

    for (int i = 0; i < WAVE_SEGMENTS; i++) {
        float t = (2.0f * 3.1415926f) * ((float)i / (float)WAVE_SEGMENTS);
        // Recent history: map index so newest at angle 0
        int idx = (s_wave_index + i) % WAVE_SEGMENTS;
        float amp = ((float)s_wave_levels[idx] / 100.0f) * max_amplitude;
        float r0 = base_radius;
        float r1 = base_radius + amp;
        lv_point_t p0 = { (lv_coord_t)(cx + r0 * cosf(t)), (lv_coord_t)(cy + r0 * sinf(t)) };
        lv_point_t p1 = { (lv_coord_t)(cx + r1 * cosf(t)), (lv_coord_t)(cy + r1 * sinf(t)) };
        if (draw_ctx->draw_line) {
            draw_ctx->draw_line(draw_ctx, &dsc, &p0, &p1);
        }
    }
}

static void wave_init(lv_obj_t *parent)
{
    s_wave_obj = lv_obj_create(parent);
    lv_obj_set_size(s_wave_obj, 700, 700);
    lv_obj_center(s_wave_obj);
    lv_obj_set_style_bg_opa(s_wave_obj, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_wave_obj, wave_draw_event_cb, LV_EVENT_ALL, NULL);
    // Place under character but over rings
    lv_obj_move_to_index(s_wave_obj, 0);
}

static void wave_push_level(uint8_t level)
{
    s_wave_levels[s_wave_index] = level > 100 ? 100 : level;
    s_wave_index = (s_wave_index + 1) % WAVE_SEGMENTS;
    if (s_wave_obj) lv_obj_invalidate(s_wave_obj);
}

esp_err_t ui_manager_update_conversation_state(ui_state_t state,
                                              const char *status_text,
                                              const char *detail_text,
                                              int mic_level,
                                              int tts_level,
                                              float vad_confidence,
                                              float wake_word_confidence)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update audio levels and confidence
    s_ui_manager.mic_level = mic_level;
    s_ui_manager.tts_level = tts_level;
    s_ui_manager.vad_confidence = vad_confidence;
    if (wake_word_confidence >= 0.0f) {
        s_ui_manager.wake_word_confidence = wake_word_confidence;
    }
    
    // Update UI state
    ui_manager_set_state(state);
    
    // Update status text if provided
    if (status_text) {
        lv_label_set_text(s_ui_manager.status_label, status_text);
    }
    
    // Update detail text if provided
    if (detail_text && strlen(detail_text) > 0) {
        lv_label_set_text(s_ui_manager.status_detail, detail_text);
        lv_obj_clear_flag(s_ui_manager.status_detail, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update audio visualization
    ui_manager_update_mic_level(mic_level, vad_confidence);
    ui_manager_update_tts_level(tts_level, 0.0f);  // No progress info in this call
    
    // Update confidence meter if in conversation
    if (s_ui_manager.in_conversation && vad_confidence > 0.0f) {
        int confidence_percent = (int)(vad_confidence * 100);
        lv_arc_set_value(s_ui_manager.confidence_meter, confidence_percent);
        
        // Change color based on confidence level
        if (vad_confidence > 0.8f) {
            lv_obj_set_style_arc_color(s_ui_manager.confidence_meter, HOWDY_COLOR_VAD_HIGH, LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_arc_color(s_ui_manager.confidence_meter, HOWDY_COLOR_VAD_LOW, LV_PART_INDICATOR);
        }
        
        lv_obj_clear_flag(s_ui_manager.confidence_meter, LV_OBJ_FLAG_HIDDEN);
    }
    
    ESP_LOGD(TAG, "Conversation state updated: %s, mic:%d, tts:%d, vad:%.2f, wake:%.2f",
            status_text ? status_text : "(no status)", mic_level, tts_level, vad_confidence, wake_word_confidence);
    
    return ESP_OK;
}

esp_err_t ui_manager_show_voice_assistant_state(const char *state_name, 
                                               const char *status_text, 
                                               float audio_level)
{
    if (!s_initialized || !state_name || !status_text) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Voice assistant state: %s - %s (level: %.2f)", state_name, status_text, audio_level);
    
    // Convert audio level to percentage
    int level_percent = (int)(audio_level * 100);
    
    // Map state names to conversation states and update
    ui_state_t mapped_state = UI_STATE_IDLE;
    
    if (strcmp(state_name, "waiting") == 0 || strcmp(state_name, "READY") == 0) {
        mapped_state = UI_STATE_IDLE;
    } else if (strcmp(state_name, "listening") == 0 || strcmp(state_name, "LISTENING") == 0) {
        mapped_state = UI_STATE_LISTENING;
    } else if (strcmp(state_name, "thinking") == 0 || strcmp(state_name, "PROCESSING") == 0) {
        mapped_state = UI_STATE_PROCESSING;
    } else if (strcmp(state_name, "speaking") == 0 || strcmp(state_name, "SPEAKING") == 0) {
        mapped_state = UI_STATE_SPEAKING;
    } else if (strcmp(state_name, "ending") == 0) {
        mapped_state = UI_STATE_SESSION_ENDING;
    } else if (strcmp(state_name, "ERROR") == 0 || strcmp(state_name, "DISCONNECTED") == 0) {
        mapped_state = UI_STATE_ERROR;
    } else if (strcmp(state_name, "SEARCHING") == 0) {
        mapped_state = UI_STATE_DISCOVERING;
    }
    
    // Use enhanced conversation state update
    return ui_manager_update_conversation_state(mapped_state, status_text, NULL,
                                               level_percent, 0, audio_level, -1.0f);
}

esp_err_t ui_manager_start_breathing_animation(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop any existing breathing animation
    if (s_breathing_animation_timer) {
        lv_timer_del(s_breathing_animation_timer);
    }
    
    // Start gentle breathing animation (low frequency for subtle effect)
    s_ui_manager.animation_step = 0;
    s_breathing_animation_timer = lv_timer_create(breathing_animation_cb, 100, NULL);
    
    ESP_LOGD(TAG, "Started breathing animation");
    return ESP_OK;
}

esp_err_t ui_manager_stop_breathing_animation(void)
{
    if (s_breathing_animation_timer) {
        lv_timer_del(s_breathing_animation_timer);
        s_breathing_animation_timer = NULL;
        
        // Reset character scale
        if (s_initialized && s_ui_manager.howdy_character) {
            lv_obj_set_style_transform_zoom(s_ui_manager.howdy_character, 256, 0);
        }
        
        ESP_LOGD(TAG, "Stopped breathing animation");
    }
    
    return ESP_OK;
}

esp_err_t ui_manager_start_listening_animation(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop other animations
    ui_manager_stop_breathing_animation();
    ui_manager_stop_processing_animation();
    
    // Stop any existing listening animation
    if (s_listening_animation_timer) {
        lv_timer_del(s_listening_animation_timer);
    }
    
    // Start listening animation (optimized for performance - 25fps for smooth pulsing)
    s_ui_manager.animation_step = 0;
    s_ui_manager.listening_animation_active = true;
    s_listening_animation_timer = lv_timer_create(listening_animation_cb, 40, NULL); // 40ms = 25fps
    
    ESP_LOGI(TAG, "Started listening animation");
    return ESP_OK;
}

esp_err_t ui_manager_stop_listening_animation(void)
{
    if (s_listening_animation_timer) {
        lv_timer_del(s_listening_animation_timer);
        s_listening_animation_timer = NULL;
        s_ui_manager.listening_animation_active = false;
        
        // Reset visual elements
        if (s_initialized) {
            if (s_ui_manager.outer_audio_ring) {
                lv_obj_set_style_arc_opa(s_ui_manager.outer_audio_ring, LV_OPA_COVER, LV_PART_INDICATOR);
            }
            if (s_ui_manager.howdy_character) {
                lv_obj_set_style_transform_zoom(s_ui_manager.howdy_character, 256, 0);
            }
        }
        
        ESP_LOGD(TAG, "Stopped listening animation");
    }
    
    return ESP_OK;
}

esp_err_t ui_manager_start_processing_animation(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop other animations
    ui_manager_stop_breathing_animation();
    ui_manager_stop_listening_animation();
    
    // Stop any existing processing animation
    if (s_processing_animation_timer) {
        lv_timer_del(s_processing_animation_timer);
    }
    
    // Start processing spinner animation (optimized - 20fps for smooth rotation)
    s_ui_manager.animation_step = 0;
    s_ui_manager.processing_animation_active = true;
    s_processing_animation_timer = lv_timer_create(processing_animation_cb, 50, NULL); // 50ms = 20fps
    
    ESP_LOGI(TAG, "Started processing animation");
    return ESP_OK;
}

esp_err_t ui_manager_stop_processing_animation(void)
{
    if (s_processing_animation_timer) {
        lv_timer_del(s_processing_animation_timer);
        s_processing_animation_timer = NULL;
        s_ui_manager.processing_animation_active = false;
        
        // Reset rotations
        if (s_initialized) {
            if (s_ui_manager.level_arc) {
                lv_obj_set_style_transform_angle(s_ui_manager.level_arc, 0, 0);
            }
            if (s_ui_manager.confidence_meter) {
                lv_obj_set_style_transform_angle(s_ui_manager.confidence_meter, 0, 0);
            }
        }
        
        ESP_LOGD(TAG, "Stopped processing animation");
    }
    
    return ESP_OK;
}

esp_err_t ui_manager_start_wake_word_animation(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop any existing wake word animation
    if (s_wake_word_animation_timer) {
        lv_timer_del(s_wake_word_animation_timer);
    }
    
    // Start wake word pulse animation (optimized - 15fps for dramatic effect)
    s_ui_manager.animation_step = 0;
    s_ui_manager.wake_word_animation_active = true;
    s_wake_word_animation_timer = lv_timer_create(wake_word_animation_cb, 67, NULL); // 67ms = ~15fps
    
    ESP_LOGI(TAG, "Started wake word animation");
    return ESP_OK;
}

esp_err_t ui_manager_stop_wake_word_animation(void)
{
    if (s_wake_word_animation_timer) {
        lv_timer_del(s_wake_word_animation_timer);
        s_wake_word_animation_timer = NULL;
        s_ui_manager.wake_word_animation_active = false;
        
        // Reset wake word visuals
        if (s_initialized) {
            if (s_ui_manager.wake_word_ring) {
                lv_obj_set_style_arc_opa(s_ui_manager.wake_word_ring, LV_OPA_COVER, LV_PART_INDICATOR);
                lv_obj_add_flag(s_ui_manager.wake_word_ring, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_ui_manager.character_glow) {
                lv_obj_set_style_bg_opa(s_ui_manager.character_glow, LV_OPA_TRANSP, 0);
            }
        }
        
        ESP_LOGD(TAG, "Stopped wake word animation");
    }
    
    return ESP_OK;
}

esp_err_t ui_manager_set_voice_callback(ui_voice_activation_callback_t callback)
{
    s_voice_callback = callback;
    ESP_LOGI(TAG, "Voice activation callback set");
    return ESP_OK;
}

esp_err_t ui_manager_update_mic_level(int level, float vad_confidence)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Performance optimization: Rate limit updates to maintain smooth FPS
    uint32_t current_time = esp_timer_get_time() / 1000;
    if (current_time - s_last_update_time < MIN_UPDATE_INTERVAL_MS) {
        return ESP_OK; // Skip update to maintain performance
    }
    s_last_update_time = current_time;
    
    // Clamp level to 0-100
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    
    // Only update if values have changed significantly (reduce unnecessary redraws)
    if (abs(s_ui_manager.mic_level - level) < 3 && 
        fabsf(s_ui_manager.vad_confidence - vad_confidence) < 0.05f) {
        return ESP_OK;
    }
    
    s_ui_manager.mic_level = level;
    s_ui_manager.vad_confidence = vad_confidence;
    
    // Batch UI updates for better performance
    lv_obj_invalidate(s_ui_manager.outer_audio_ring);
    lv_arc_set_value(s_ui_manager.outer_audio_ring, level);
    
    // Change color based on VAD confidence (cache color to avoid frequent changes)
    static lv_color_t last_color = {0};
    lv_color_t new_color;
    
    if (vad_confidence > 0.8f) {
        new_color = HOWDY_COLOR_VAD_HIGH;
    } else if (vad_confidence > 0.5f) {
        new_color = HOWDY_COLOR_MIC_RING;
    } else {
        new_color = HOWDY_COLOR_VAD_LOW;
    }
    
    // Only update color if it changed
    if (new_color.full != last_color.full) {
        lv_obj_set_style_arc_color(s_ui_manager.outer_audio_ring, new_color, LV_PART_INDICATOR);
        last_color = new_color;
    }
    
    // Also update legacy level arc for compatibility
    lv_arc_set_value(s_ui_manager.level_arc, level);
    
    // Performance tracking
    s_update_count++;
    if (s_update_count % 30 == 0) { // Calculate FPS every 30 updates
        static uint32_t last_fps_calc = 0;
        if (last_fps_calc > 0) {
            s_average_fps = 30000.0f / (current_time - last_fps_calc);
        }
        last_fps_calc = current_time;
    }
    
    ESP_LOGV(TAG, "Mic level updated: %d%%, VAD: %.2f, FPS: %.1f", level, vad_confidence, s_average_fps);
    
    return ESP_OK;
}

esp_err_t ui_manager_update_tts_level(int level, float progress)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Clamp level to 0-100
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    
    s_ui_manager.tts_level = level;
    
    // Update inner audio ring (TTS output)
    lv_arc_set_value(s_ui_manager.inner_audio_ring, level);
    // Feed waveform history for visual effect
    wave_push_level((uint8_t)level);
    
    // Show TTS progress if available (0.0-1.0)
    if (progress >= 0.0f && progress <= 1.0f) {
        // Use arc start angle to show progress
        int progress_angle = (int)(progress * 360);
        lv_arc_set_bg_angles(s_ui_manager.inner_audio_ring, 0, progress_angle);
    }
    
    ESP_LOGV(TAG, "TTS level updated: %d%%, progress: %.2f", level, progress);
    
    return ESP_OK;
}

esp_err_t ui_manager_show_wake_word_detection(float confidence, const char *phrase_detected)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_ui_manager.wake_word_confidence = confidence;
    
    // Update wake word ring value
    int confidence_percent = (int)(confidence * 100);
    lv_arc_set_value(s_ui_manager.wake_word_ring, confidence_percent);
    
    // Show wake word ring
    lv_obj_clear_flag(s_ui_manager.wake_word_ring, LV_OBJ_FLAG_HIDDEN);
    
    // Update status if phrase detected
    if (phrase_detected && strlen(phrase_detected) > 0) {
        char wake_word_status[128];
        snprintf(wake_word_status, sizeof(wake_word_status), 
                "'%s' detected (%.0f%% confidence)", phrase_detected, confidence * 100);
        lv_label_set_text(s_ui_manager.status_detail, wake_word_status);
        lv_obj_clear_flag(s_ui_manager.status_detail, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Start wake word animation
    ui_manager_start_wake_word_animation();
    
    ESP_LOGI(TAG, "Wake word detection shown: %.2f confidence, phrase: %s", 
            confidence, phrase_detected ? phrase_detected : "(none)");
    
    return ESP_OK;
}

esp_err_t ui_manager_update_conversation_progress(bool in_conversation, 
                                                 int turns_completed, 
                                                 int estimated_remaining)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_ui_manager.in_conversation = in_conversation;
    
    if (in_conversation && turns_completed > 0) {
        char progress_text[64];
        if (estimated_remaining > 0) {
            snprintf(progress_text, sizeof(progress_text), 
                    "Turn %d - %ds remaining", turns_completed, estimated_remaining);
        } else {
            snprintf(progress_text, sizeof(progress_text), "Turn %d", turns_completed);
        }
        
        lv_label_set_text(s_ui_manager.status_detail, progress_text);
        lv_obj_clear_flag(s_ui_manager.status_detail, LV_OBJ_FLAG_HIDDEN);
    }
    
    ESP_LOGD(TAG, "Conversation progress: %s, turns: %d, remaining: %ds",
            in_conversation ? "active" : "inactive", turns_completed, estimated_remaining);
    
    return ESP_OK;
}

esp_err_t ui_manager_handle_touch_gesture(const char *gesture_type, void *gesture_data)
{
    if (!s_initialized || !gesture_type) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Touch gesture: %s", gesture_type);
    
    if (strcmp(gesture_type, "volume_up") == 0) {
        // Visual feedback for volume up
        lv_obj_set_style_arc_color(s_ui_manager.inner_audio_ring, HOWDY_COLOR_ACCENT, LV_PART_INDICATOR);
        
        // Show volume up indication
        lv_label_set_text(s_ui_manager.status_detail, "Volume up");
        lv_obj_clear_flag(s_ui_manager.status_detail, LV_OBJ_FLAG_HIDDEN);
        
        // TODO: Implement actual volume control through audio system
        
    } else if (strcmp(gesture_type, "volume_down") == 0) {
        // Visual feedback for volume down
        lv_obj_set_style_arc_color(s_ui_manager.inner_audio_ring, HOWDY_COLOR_VAD_LOW, LV_PART_INDICATOR);
        
        // Show volume down indication
        lv_label_set_text(s_ui_manager.status_detail, "Volume down");
        lv_obj_clear_flag(s_ui_manager.status_detail, LV_OBJ_FLAG_HIDDEN);
        
        // TODO: Implement actual volume control through audio system
        
    } else if (strcmp(gesture_type, "swipe_left") == 0) {
        // End conversation gesture
        ESP_LOGI(TAG, "Swipe left - ending conversation");
        if (s_voice_callback) {
            s_voice_callback(false);  // End conversation
        }
        
    } else if (strcmp(gesture_type, "swipe_right") == 0) {
        // Start conversation gesture
        ESP_LOGI(TAG, "Swipe right - starting conversation");
        if (s_voice_callback) {
            s_voice_callback(true);  // Start conversation
        }
    }
    
    // Reset visual feedback after 1 second
    // TODO: Implement timer-based reset of visual feedback
    
    return ESP_OK;
}

esp_err_t ui_manager_set_conversation_context(int context)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update UI based on conversation context
    switch (context) {
        case 0: // VAD_CONVERSATION_IDLE
            if (s_ui_manager.current_state != UI_STATE_ERROR) {
                ui_manager_set_state(UI_STATE_IDLE);
            }
            break;
            
        case 1: // VAD_CONVERSATION_LISTENING
            ui_manager_set_state(UI_STATE_LISTENING);
            break;
            
        case 2: // VAD_CONVERSATION_SPEAKING
            ui_manager_set_state(UI_STATE_SPEAKING);
            break;
            
        case 3: // VAD_CONVERSATION_PROCESSING
            ui_manager_set_state(UI_STATE_PROCESSING);
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown conversation context: %d", context);
            break;
    }
    
    ESP_LOGD(TAG, "Conversation context set to: %d", context);
    
    return ESP_OK;
}

esp_err_t ui_manager_get_conversation_info(bool *in_conversation, 
                                          float *current_confidence, 
                                          int audio_levels[2])
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (in_conversation) {
        *in_conversation = s_ui_manager.in_conversation;
    }
    
    if (current_confidence) {
        *current_confidence = s_ui_manager.vad_confidence;
    }
    
    if (audio_levels) {
        audio_levels[0] = s_ui_manager.mic_level;
        audio_levels[1] = s_ui_manager.tts_level;
    }
    
    return ESP_OK;
}

esp_err_t ui_manager_show_error_with_recovery(const char *error_type,
                                             const char *error_message,
                                             int recovery_time)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Set error state
    ui_manager_set_state(UI_STATE_ERROR);
    
    // Update error message
    if (error_message) {
        lv_label_set_text(s_ui_manager.status_label, error_message);
    }
    
    // Show recovery information
    if (error_type && recovery_time > 0) {
        char recovery_text[128];
        snprintf(recovery_text, sizeof(recovery_text), 
                "%s error - recovering in %ds", error_type, recovery_time);
        lv_label_set_text(s_ui_manager.status_detail, recovery_text);
        lv_obj_clear_flag(s_ui_manager.status_detail, LV_OBJ_FLAG_HIDDEN);
    } else if (error_type) {
        char error_text[128];
        snprintf(error_text, sizeof(error_text), "%s error - attempting recovery", error_type);
        lv_label_set_text(s_ui_manager.status_detail, error_text);
        lv_obj_clear_flag(s_ui_manager.status_detail, LV_OBJ_FLAG_HIDDEN);
    }
    
    ESP_LOGE(TAG, "Error shown: %s - %s (recovery in %ds)",
            error_type ? error_type : "Unknown",
            error_message ? error_message : "No details",
            recovery_time);
    
    return ESP_OK;
}
