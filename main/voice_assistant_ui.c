#include "voice_assistant_ui.h"
#include "images/howdy_images.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>

static const char *TAG = "VoiceAssistantUI";

// HowdyTTS Color Scheme (Google-inspired)
#define HOWDY_COLOR_PRIMARY     0x4285f4    // Blue
#define HOWDY_COLOR_SECONDARY   0x34a853    // Green  
#define HOWDY_COLOR_WARNING     0xfbbc04    // Yellow
#define HOWDY_COLOR_ERROR       0xea4335    // Red
#define HOWDY_COLOR_BACKGROUND  0x1a1a1a    // Dark background
#define HOWDY_COLOR_TEXT_WHITE  0xffffff    // White text
#define HOWDY_COLOR_TEXT_GRAY   0x888888    // Gray text

// UI State
static va_ui_config_t s_config = {0};
static va_ui_state_t s_current_state = VA_UI_STATE_IDLE;
static va_gesture_t s_last_gesture = VA_GESTURE_NONE;
static SemaphoreHandle_t s_ui_mutex = NULL;
static bool s_initialized = false;

// LVGL Objects
static lv_obj_t *s_main_screen = NULL;
static lv_obj_t *s_boot_screen = NULL;      // Boot/welcome screen
static lv_obj_t *s_circular_container = NULL;
static lv_obj_t *s_voice_level_arc = NULL;
static lv_obj_t *s_center_button = NULL;
static lv_obj_t *s_state_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_wifi_indicator = NULL;
static lv_obj_t *s_howdy_character = NULL;  // Howdy character image
static lv_obj_t *s_boot_howdy_character = NULL;  // Boot screen Howdy character

// Animation objects
static lv_anim_t s_pulse_animation;

// Audio visualization
static va_audio_data_t s_current_audio = {0};
static esp_timer_handle_t s_animation_timer = NULL;

// Forward declarations
static void create_circular_layout(void);
static void create_voice_level_visualization(void);
static void create_center_interaction_area(void);
static void create_howdy_character(void);
static void create_boot_screen(void);
static void setup_touch_handlers(void);
static void update_state_visuals(va_ui_state_t state, bool animate);
static void update_howdy_character(va_ui_state_t state);
static void touch_event_handler(lv_event_t *e);
static void animation_timer_callback(void *arg);

esp_err_t va_ui_init(const va_ui_config_t *config)
{
    if (s_initialized) {
        ESP_LOGI(TAG, "Voice Assistant UI already initialized");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing Voice Assistant UI for %dx%d display", 
             config->display_width, config->display_height);

    // Copy configuration
    s_config = *config;

    // Create UI mutex
    s_ui_mutex = xSemaphoreCreateMutex();
    if (!s_ui_mutex) {
        ESP_LOGE(TAG, "Failed to create UI mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create main screen
    s_main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_main_screen, lv_color_hex(HOWDY_COLOR_BACKGROUND), 0);

    // Create circular layout optimized for round display
    create_circular_layout();
    
    // Create voice level visualization
    create_voice_level_visualization();
    
    // Create center interaction area
    create_center_interaction_area();
    
    // Create Howdy character image
    create_howdy_character();
    
    // Setup touch event handlers
    setup_touch_handlers();

    // Create animation timer for smooth updates
    if (s_config.enable_animations) {
        esp_timer_create_args_t timer_args = {
            .callback = animation_timer_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "va_ui_anim"
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_animation_timer), TAG, "Failed to create animation timer");
        ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_animation_timer, 16667), TAG, "Failed to start animation timer"); // 60fps
    }

    // Load the screen
    lv_scr_load(s_main_screen);

    // Set initial state
    va_ui_set_state(VA_UI_STATE_IDLE, false);

    s_initialized = true;
    ESP_LOGI(TAG, "Voice Assistant UI initialized successfully");
    return ESP_OK;
}

static void create_circular_layout(void)
{
    ESP_LOGI(TAG, "Creating circular layout for %dx%d round display", s_config.display_width, s_config.display_height);

    // Main circular container (optimized for round display)
    s_circular_container = lv_obj_create(s_main_screen);
    lv_obj_set_size(s_circular_container, s_config.circular_container_size, s_config.circular_container_size);
    lv_obj_center(s_circular_container);
    lv_obj_set_style_radius(s_circular_container, s_config.circular_container_size / 2, 0);
    lv_obj_set_style_bg_color(s_circular_container, lv_color_hex(HOWDY_COLOR_BACKGROUND), 0);
    lv_obj_set_style_border_width(s_circular_container, 0, 0);
    lv_obj_set_style_pad_all(s_circular_container, 20, 0);
    lv_obj_set_style_clip_corner(s_circular_container, true, 0);

    // HowdyTTS branding at top of circle
    lv_obj_t *title_label = lv_label_create(s_circular_container);
    lv_label_set_text(title_label, "HowdyTTS");
    lv_obj_set_style_text_color(title_label, lv_color_hex(HOWDY_COLOR_PRIMARY), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 40);

    // Subtitle
    lv_obj_t *subtitle_label = lv_label_create(s_circular_container);
    lv_label_set_text(subtitle_label, "Voice Assistant");
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(HOWDY_COLOR_TEXT_GRAY), 0);
    lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_14, 0);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_MID, 0, 70);

    // State label (positioned in lower arc)
    s_state_label = lv_label_create(s_circular_container);
    lv_label_set_text(s_state_label, "Ready");
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(HOWDY_COLOR_SECONDARY), 0);
    lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_state_label, LV_ALIGN_BOTTOM_MID, 0, -80);

    // Status label
    s_status_label = lv_label_create(s_circular_container);
    lv_label_set_text(s_status_label, "Touch to activate");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(HOWDY_COLOR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -50);

    // WiFi indicator (top-right of circle)
    s_wifi_indicator = lv_label_create(s_circular_container);
    lv_label_set_text(s_wifi_indicator, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_wifi_indicator, lv_color_hex(HOWDY_COLOR_TEXT_GRAY), 0);
    lv_obj_set_style_text_font(s_wifi_indicator, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wifi_indicator, LV_ALIGN_TOP_RIGHT, -20, 30);
}

static void create_voice_level_visualization(void)
{
    ESP_LOGI(TAG, "Creating voice level visualization");

    // Voice level arc (outer ring for audio visualization)
    s_voice_level_arc = lv_arc_create(s_circular_container);
    lv_obj_set_size(s_voice_level_arc, s_config.circular_container_size - 80, s_config.circular_container_size - 80);
    lv_obj_center(s_voice_level_arc);
    
    // Configure arc appearance
    lv_arc_set_range(s_voice_level_arc, 0, 100);
    lv_arc_set_value(s_voice_level_arc, 0);
    lv_arc_set_bg_angles(s_voice_level_arc, 0, 360);
    
    // Styling for voice level visualization
    lv_obj_set_style_arc_width(s_voice_level_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_voice_level_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_voice_level_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_voice_level_arc, lv_color_hex(HOWDY_COLOR_PRIMARY), LV_PART_INDICATOR);
    
    // Remove knob (we don't want user interaction on the arc)
    lv_obj_set_style_bg_opa(s_voice_level_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(s_voice_level_arc, LV_OBJ_FLAG_CLICKABLE);
}

static void create_center_interaction_area(void)
{
    ESP_LOGI(TAG, "Creating center interaction area");

    // Central touch area (main interaction button) - made smaller to accommodate Howdy
    s_center_button = lv_btn_create(s_circular_container);
    lv_obj_set_size(s_center_button, 160, 160);
    lv_obj_center(s_center_button);
    lv_obj_set_style_radius(s_center_button, 80, 0);
    lv_obj_set_style_bg_color(s_center_button, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_border_width(s_center_button, 2, 0);
    lv_obj_set_style_border_color(s_center_button, lv_color_hex(HOWDY_COLOR_PRIMARY), 0);
    lv_obj_set_style_bg_opa(s_center_button, LV_OPA_70, 0);  // Semi-transparent for Howdy to show through

    // Add press effect
    lv_obj_set_style_transform_zoom(s_center_button, 256, LV_STATE_DEFAULT);
    lv_obj_set_style_transform_zoom(s_center_button, 240, LV_STATE_PRESSED);
}

static void create_howdy_character(void)
{
    ESP_LOGI(TAG, "Creating Howdy character image");

    // Create Howdy character image in the center
    s_howdy_character = lv_img_create(s_circular_container);
    lv_img_set_src(s_howdy_character, &howdy_img_armraisehowdy);  // Default to greeting pose
    lv_obj_center(s_howdy_character);
    
    // Style the character image
    lv_obj_set_style_img_opa(s_howdy_character, LV_OPA_COVER, 0);
    
    // Make sure the character is above the center button but below the touch area
    lv_obj_move_background(s_howdy_character);
    lv_obj_move_foreground(s_center_button);
}

static void setup_touch_handlers(void)
{
    ESP_LOGI(TAG, "Setting up touch event handlers");

    // Add touch event handler to center button
    lv_obj_add_event_cb(s_center_button, touch_event_handler, LV_EVENT_ALL, NULL);
    
    // Add touch event handler to main container for gesture detection
    lv_obj_add_event_cb(s_circular_container, touch_event_handler, LV_EVENT_ALL, NULL);
}

static void touch_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = lv_event_get_target(e);
    
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    switch (code) {
        case LV_EVENT_PRESSED:
            ESP_LOGI(TAG, "Touch pressed");
            if (target == s_center_button) {
                // Immediate visual feedback
                lv_obj_set_style_bg_color(s_center_button, lv_color_hex(HOWDY_COLOR_PRIMARY), LV_STATE_PRESSED);
            }
            break;
            
        case LV_EVENT_LONG_PRESSED:
            ESP_LOGI(TAG, "Long press detected - Hold to speak");
            s_last_gesture = VA_GESTURE_LONG_PRESS;
            if (s_current_state == VA_UI_STATE_IDLE) {
                va_ui_set_state(VA_UI_STATE_LISTENING, true);
            }
            break;
            
        case LV_EVENT_CLICKED:
            ESP_LOGI(TAG, "Quick tap detected");
            s_last_gesture = VA_GESTURE_TAP;
            // Toggle mute or activate voice assistant
            break;
            
        case LV_EVENT_RELEASED:
            ESP_LOGI(TAG, "Touch released"); 
            if (target == s_center_button) {
                lv_obj_set_style_bg_color(s_center_button, lv_color_hex(0x2a2a2a), LV_STATE_DEFAULT);
            }
            
            // If we were listening via long press, stop listening
            if (s_current_state == VA_UI_STATE_LISTENING && s_last_gesture == VA_GESTURE_LONG_PRESS) {
                va_ui_set_state(VA_UI_STATE_PROCESSING, true);
            }
            break;
            
        default:
            break;
    }
    
    xSemaphoreGive(s_ui_mutex);
}

static void update_state_visuals(va_ui_state_t state, bool animate)
{
    if (!s_initialized) return;

    const char *state_text = "Unknown";
    uint32_t state_color = HOWDY_COLOR_TEXT_WHITE;
    uint32_t arc_color = HOWDY_COLOR_PRIMARY;
    
    switch (state) {
        case VA_UI_STATE_IDLE:
            state_text = "Ready";
            state_color = HOWDY_COLOR_SECONDARY;
            arc_color = HOWDY_COLOR_SECONDARY;
            lv_label_set_text(s_status_label, "Touch to activate");
            break;
            
        case VA_UI_STATE_LISTENING:
            state_text = "Listening";
            state_color = HOWDY_COLOR_PRIMARY;
            arc_color = HOWDY_COLOR_PRIMARY;
            lv_label_set_text(s_status_label, "Speak now...");
            
            // Start pulsing animation for listening state
            if (animate && s_config.enable_animations) {
                lv_anim_init(&s_pulse_animation);
                lv_anim_set_var(&s_pulse_animation, s_center_button);
                lv_anim_set_values(&s_pulse_animation, 256, 280);
                lv_anim_set_time(&s_pulse_animation, 1000);
                lv_anim_set_repeat_count(&s_pulse_animation, LV_ANIM_REPEAT_INFINITE);
                lv_anim_set_playback_time(&s_pulse_animation, 500);
                lv_anim_set_exec_cb(&s_pulse_animation, (lv_anim_exec_xcb_t)lv_obj_set_style_transform_zoom);
                lv_anim_start(&s_pulse_animation);
            }
            break;
            
        case VA_UI_STATE_PROCESSING:
            state_text = "Processing";
            state_color = HOWDY_COLOR_WARNING;
            arc_color = HOWDY_COLOR_WARNING;
            lv_label_set_text(s_status_label, "Thinking...");
            
            // Stop pulsing animation
            lv_anim_del(&s_pulse_animation, NULL);
            lv_obj_set_style_transform_zoom(s_center_button, 256, 0);
            break;
            
        case VA_UI_STATE_SPEAKING:
            state_text = "Speaking";
            state_color = HOWDY_COLOR_SECONDARY;
            arc_color = HOWDY_COLOR_SECONDARY;
            lv_label_set_text(s_status_label, "Playing response...");
            break;
            
        case VA_UI_STATE_CONNECTING:
            state_text = "Connecting";
            state_color = HOWDY_COLOR_WARNING;
            arc_color = HOWDY_COLOR_WARNING;
            lv_label_set_text(s_status_label, "Setting up WiFi...");
            break;
            
        case VA_UI_STATE_ERROR:
            state_text = "Error";
            state_color = HOWDY_COLOR_ERROR;
            arc_color = HOWDY_COLOR_ERROR;
            lv_label_set_text(s_status_label, "Connection failed");
            break;
    }
    
    // Update state label
    lv_label_set_text(s_state_label, state_text);
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(state_color), 0);
    
    // Update arc color
    lv_obj_set_style_arc_color(s_voice_level_arc, lv_color_hex(arc_color), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(s_center_button, lv_color_hex(arc_color), 0);
    
    ESP_LOGI(TAG, "UI state updated to: %s", state_text);
}

static void animation_timer_callback(void *arg)
{
    // Update audio visualization at 60fps
    if (s_current_state == VA_UI_STATE_LISTENING && s_current_audio.voice_detected) {
        int level_value = (int)(s_current_audio.voice_level * 100);
        
        if (xSemaphoreTake(s_ui_mutex, 0) == pdTRUE) {
            lv_arc_set_value(s_voice_level_arc, level_value);
            xSemaphoreGive(s_ui_mutex);
        }
    }
}

esp_err_t va_ui_set_state(va_ui_state_t state, bool animate)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "UI not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire UI mutex");
        return ESP_ERR_TIMEOUT;
    }

    // If we have a boot screen active, switch to main UI
    if (s_boot_screen && lv_scr_act() == s_boot_screen) {
        ESP_LOGI(TAG, "Transitioning from boot screen to main Voice Assistant UI");
        lv_scr_load(s_main_screen);
    }

    s_current_state = state;
    update_state_visuals(state, animate);
    update_howdy_character(state);  // Update Howdy's pose based on state
    
    xSemaphoreGive(s_ui_mutex);
    return ESP_OK;
}

static void update_howdy_character(va_ui_state_t state)
{
    if (!s_howdy_character) return;

    const lv_img_dsc_t *character_image = NULL;
    
    switch (state) {
        case VA_UI_STATE_IDLE:
            character_image = &howdy_img_armraisehowdy;  // Friendly greeting pose
            ESP_LOGI(TAG, "Howdy: Greeting pose (IDLE)");
            break;
            
        case VA_UI_STATE_LISTENING:
            character_image = &howdy_img_howdyleft;  // Looking left, listening attentively
            ESP_LOGI(TAG, "Howdy: Listening pose");
            break;
            
        case VA_UI_STATE_PROCESSING:
            character_image = &howdy_img_howdybackward;  // Thinking pose
            ESP_LOGI(TAG, "Howdy: Thinking pose (PROCESSING)");
            break;
            
        case VA_UI_STATE_SPEAKING:
            character_image = &howdy_img_howdyright;  // Speaking/responding pose
            ESP_LOGI(TAG, "Howdy: Speaking pose");
            break;
            
        case VA_UI_STATE_CONNECTING:
            character_image = &howdy_img_howdyright2;  // Alternative right pose for connecting
            ESP_LOGI(TAG, "Howdy: Connecting pose");
            break;
            
        case VA_UI_STATE_ERROR:
            character_image = &howdy_img_howdybackward;  // Confused/error pose
            ESP_LOGI(TAG, "Howdy: Error pose");
            break;
            
        default:
            character_image = &howdy_img_armraisehowdy;  // Default greeting
            break;
    }
    
    if (character_image) {
        lv_img_set_src(s_howdy_character, character_image);
    }
}

esp_err_t va_ui_update_audio_visualization(const va_audio_data_t *audio_data)
{
    if (!s_initialized || !audio_data) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ui_mutex, 0) == pdTRUE) {
        s_current_audio = *audio_data;
        xSemaphoreGive(s_ui_mutex);
    }

    return ESP_OK;
}

esp_err_t va_ui_set_wifi_status(bool connected, uint8_t signal_strength, const char *ssid)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (connected) {
        lv_obj_set_style_text_color(s_wifi_indicator, lv_color_hex(HOWDY_COLOR_SECONDARY), 0);
        ESP_LOGI(TAG, "WiFi connected: %s (%d%%)", ssid ? ssid : "Unknown", signal_strength);
    } else {
        lv_obj_set_style_text_color(s_wifi_indicator, lv_color_hex(HOWDY_COLOR_ERROR), 0);
        ESP_LOGI(TAG, "WiFi disconnected");
    }

    xSemaphoreGive(s_ui_mutex);
    return ESP_OK;
}

esp_err_t va_ui_show_message(const char *message, uint32_t duration_ms, uint32_t color)
{
    if (!s_initialized || !message) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    lv_label_set_text(s_status_label, message);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(color), 0);

    xSemaphoreGive(s_ui_mutex);

    ESP_LOGI(TAG, "Message displayed: %s", message);
    return ESP_OK;
}

va_gesture_t va_ui_get_last_gesture(void)
{
    va_gesture_t gesture = s_last_gesture;
    s_last_gesture = VA_GESTURE_NONE;  // Reset after reading
    return gesture;
}

esp_err_t va_ui_set_power_saving(bool enable)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        // Reduce refresh rate, dim display
        ESP_LOGI(TAG, "Power saving mode enabled");
        if (s_animation_timer) {
            esp_timer_stop(s_animation_timer);
        }
    } else {
        // Normal refresh rate
        ESP_LOGI(TAG, "Power saving mode disabled");
        if (s_animation_timer) {
            esp_timer_start_periodic(s_animation_timer, 16667); // 60fps
        }
    }

    return ESP_OK;
}

va_ui_state_t va_ui_get_state(void)
{
    return s_current_state;
}

static void create_boot_screen(void)
{
    ESP_LOGI(TAG, "Creating boot/welcome screen with cowboy Howdy");

    // Create boot screen
    s_boot_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_boot_screen, lv_color_hex(HOWDY_COLOR_BACKGROUND), 0);

    // HowdyTTS Logo/Title at top
    lv_obj_t *title_label = lv_label_create(s_boot_screen);
    lv_label_set_text(title_label, "HowdyTTS");
    lv_obj_set_style_text_color(title_label, lv_color_hex(HOWDY_COLOR_PRIMARY), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 50);

    // Subtitle
    lv_obj_t *subtitle_label = lv_label_create(s_boot_screen);
    lv_label_set_text(subtitle_label, "Voice Assistant");
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(HOWDY_COLOR_TEXT_GRAY), 0);
    lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_14, 0);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_MID, 0, 80);

    // Large cowboy Howdy character in center - the perfect boot screen image!
    s_boot_howdy_character = lv_img_create(s_boot_screen);
    lv_img_set_src(s_boot_howdy_character, &howdy_img_howdymidget);  // Cowboy Howdy with hat and badge
    lv_obj_center(s_boot_howdy_character);
    
    // Make boot character larger and more prominent
    lv_obj_set_style_img_opa(s_boot_howdy_character, LV_OPA_COVER, 0);

    // Welcome message at bottom
    lv_obj_t *welcome_label = lv_label_create(s_boot_screen);
    lv_label_set_text(welcome_label, "Howdy! Welcome to HowdyTTS");
    lv_obj_set_style_text_color(welcome_label, lv_color_hex(HOWDY_COLOR_SECONDARY), 0);
    lv_obj_set_style_text_font(welcome_label, &lv_font_montserrat_14, 0);
    lv_obj_align(welcome_label, LV_ALIGN_BOTTOM_MID, 0, -100);

    // Status message
    lv_obj_t *status_label = lv_label_create(s_boot_screen);
    lv_label_set_text(status_label, "Initializing system...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(HOWDY_COLOR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -50);

    ESP_LOGI(TAG, "Boot screen created successfully");
}

esp_err_t va_ui_show_boot_screen(const char *message, uint32_t timeout_ms)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "UI not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire UI mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Create boot screen if it doesn't exist
    if (!s_boot_screen) {
        create_boot_screen();
    }

    // Update status message if provided
    if (message) {
        // Find status label and update text
        lv_obj_t *status_label = lv_obj_get_child(s_boot_screen, -1);  // Last child (status label)
        if (status_label) {
            lv_label_set_text(status_label, message);
        }
    }

    // Load the boot screen
    lv_scr_load(s_boot_screen);
    
    ESP_LOGI(TAG, "Boot screen displayed: %s", message ? message : "Welcome");

    xSemaphoreGive(s_ui_mutex);

    // TODO: Implement timeout functionality if needed
    if (timeout_ms > 0) {
        // Could set up a timer to automatically hide boot screen after timeout
        ESP_LOGI(TAG, "Boot screen timeout: %lu ms", timeout_ms);
    }

    return ESP_OK;
}