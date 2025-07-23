#include "led_controller.h"
#include "howdy_config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "led_controller";

// WS2812B timing requirements (in nanoseconds)
#define WS2812_T0H_NS    400
#define WS2812_T0L_NS    850
#define WS2812_T1H_NS    700
#define WS2812_T1L_NS    600
#define WS2812_RESET_US  280

// Ring configuration - 7 concentric rings with center LED
static const uint8_t ring_starts[] = {0, 1, 7, 19, 37, 61, 85, 109};
static const uint8_t ring_sizes[] = {1, 6, 12, 18, 24, 24, 24, 0};

static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static rmt_transmit_config_t tx_config = {
    .loop_count = 0,
};

// Color utilities
static rgb_color_t hsv_to_rgb(float h, float s, float v) {
    rgb_color_t rgb = {0};
    
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    
    float r, g, b;
    
    if (h >= 0 && h < 60) {
        r = c; g = x; b = 0;
    } else if (h >= 60 && h < 120) {
        r = x; g = c; b = 0;
    } else if (h >= 120 && h < 180) {
        r = 0; g = c; b = x;
    } else if (h >= 180 && h < 240) {
        r = 0; g = x; b = c;
    } else if (h >= 240 && h < 300) {
        r = x; g = 0; b = c;
    } else {
        r = c; g = 0; b = x;
    }
    
    rgb.r = (uint8_t)((r + m) * 255);
    rgb.g = (uint8_t)((g + m) * 255);
    rgb.b = (uint8_t)((b + m) * 255);
    
    return rgb;
}

static void set_led_color(led_controller_t *controller, uint16_t index, rgb_color_t color) {
    if (index >= controller->led_count) return;
    
    // Apply brightness
    uint8_t r = (uint8_t)(color.r * controller->brightness);
    uint8_t g = (uint8_t)(color.g * controller->brightness);
    uint8_t b = (uint8_t)(color.b * controller->brightness);
    
    // WS2812B expects GRB format
    controller->led_data[index * 3 + 0] = g;
    controller->led_data[index * 3 + 1] = r;
    controller->led_data[index * 3 + 2] = b;
}

static void set_ring_color(led_controller_t *controller, uint8_t ring, rgb_color_t color, float intensity) {
    if (ring >= 7) return;
    
    rgb_color_t scaled_color = {
        .r = (uint8_t)(color.r * intensity),
        .g = (uint8_t)(color.g * intensity),
        .b = (uint8_t)(color.b * intensity)
    };
    
    uint8_t start = ring_starts[ring];
    uint8_t size = ring_sizes[ring];
    
    for (uint8_t i = 0; i < size; i++) {
        set_led_color(controller, start + i, scaled_color);
    }
}

static void add_sparkle_effect(led_controller_t *controller, float intensity) {
    if (intensity < 0.7f) return;
    
    int sparkle_count = (int)(intensity * 8);
    for (int i = 0; i < sparkle_count; i++) {
        uint16_t pos = rand() % controller->led_count;
        rgb_color_t white = {255, 255, 255};
        set_led_color(controller, pos, white);
    }
}

esp_err_t led_controller_init(led_controller_t *controller) {
    if (!controller) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(controller, 0, sizeof(led_controller_t));
    
    ESP_LOGI(TAG, "Initializing LED controller");
    
    // Allocate LED data buffer
    controller->led_count = LED_COUNT;
    controller->led_data = calloc(controller->led_count * 3, sizeof(uint8_t));
    if (!controller->led_data) {
        ESP_LOGE(TAG, "Failed to allocate LED data buffer");
        return ESP_ERR_NO_MEM;
    }
    
    // Configure RMT channel
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_DATA_PIN,
        .mem_block_symbols = 64,
        .resolution_hz = 10000000, // 10MHz resolution, 1 tick = 0.1µs
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = true,
    };
    
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_chan_config, &led_chan), TAG, "Failed to create RMT channel");
    
    // Create LED strip encoder
    led_strip_encoder_config_t encoder_config = {
        .resolution = tx_chan_config.resolution_hz,
        .led_model = LED_MODEL_WS2812
    };
    
    ESP_RETURN_ON_ERROR(rmt_new_led_strip_encoder(&encoder_config, &led_encoder), TAG, "Failed to create LED encoder");
    
    // Enable RMT channel
    ESP_RETURN_ON_ERROR(rmt_enable(led_chan), TAG, "Failed to enable RMT channel");
    
    // Set default values
    controller->mode = LED_MODE_AUDIO_REACTIVE;
    controller->base_color = (rgb_color_t){0, 150, 255}; // Blue
    controller->brightness = 0.5f;
    controller->initialized = true;
    
    ESP_LOGI(TAG, "LED controller initialized with %d LEDs", controller->led_count);
    return ESP_OK;
}

esp_err_t led_controller_set_mode(led_controller_t *controller, led_mode_t mode) {
    if (!controller || !controller->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    controller->mode = mode;
    ESP_LOGI(TAG, "LED mode set to %d", mode);
    return ESP_OK;
}

esp_err_t led_controller_set_color(led_controller_t *controller, uint8_t r, uint8_t g, uint8_t b) {
    if (!controller || !controller->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    controller->base_color.r = r;
    controller->base_color.g = g;
    controller->base_color.b = b;
    
    return ESP_OK;
}

esp_err_t led_controller_set_brightness(led_controller_t *controller, float brightness) {
    if (!controller || !controller->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    
    controller->brightness = brightness;
    return ESP_OK;
}

esp_err_t led_controller_update_audio(led_controller_t *controller, const audio_analysis_t *analysis) {
    if (!controller || !analysis || !controller->initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (controller->mode != LED_MODE_AUDIO_REACTIVE) {
        return ESP_OK;
    }
    
    // Clear previous frame with fade effect
    for (uint16_t i = 0; i < controller->led_count * 3; i++) {
        if (controller->led_data[i] > 20) {
            controller->led_data[i] -= 20;
        } else {
            controller->led_data[i] = 0;
        }
    }
    
    // Center LED responds to overall level
    rgb_color_t center_color = {
        .r = (uint8_t)(255 * analysis->overall_level),
        .g = (uint8_t)(100 * analysis->overall_level),
        .b = (uint8_t)(50 * analysis->overall_level)
    };
    set_led_color(controller, 0, center_color);
    
    // Inner rings respond to bass (red)
    rgb_color_t bass_color = {255, 0, 0};
    for (uint8_t ring = 1; ring <= 2; ring++) {
        set_ring_color(controller, ring, bass_color, analysis->bass);
    }
    
    // Middle rings respond to mids (green)
    rgb_color_t mid_color = {0, 255, 0};
    for (uint8_t ring = 3; ring <= 4; ring++) {
        set_ring_color(controller, ring, mid_color, analysis->mid);
    }
    
    // Outer rings respond to treble (blue)
    rgb_color_t treble_color = {0, 0, 255};
    for (uint8_t ring = 5; ring <= 6; ring++) {
        set_ring_color(controller, ring, treble_color, analysis->treble);
    }
    
    // Add sparkle effect for high treble
    if (analysis->treble > 0.7f) {
        add_sparkle_effect(controller, analysis->treble);
    }
    
    // Voice activity indication - pulse outer ring
    if (analysis->voice_detected) {
        float pulse = 0.5f + 0.5f * sinf(controller->animation_counter * 0.1f);
        rgb_color_t voice_color = {255, 255, 255};
        set_ring_color(controller, 6, voice_color, pulse * 0.3f);
    }
    
    // Transmit LED data
    ESP_RETURN_ON_ERROR(rmt_transmit(led_chan, led_encoder, controller->led_data, 
                                   controller->led_count * 3, &tx_config), TAG, "LED transmit failed");
    
    return ESP_OK;
}

esp_err_t led_controller_update_animation(led_controller_t *controller) {
    if (!controller || !controller->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    controller->animation_counter++;
    
    switch (controller->mode) {
        case LED_MODE_OFF:
            return led_controller_clear(controller);
            
        case LED_MODE_BREATHING: {
            float breath = 0.3f + 0.7f * (0.5f + 0.5f * sinf(controller->animation_counter * 0.02f));
            for (uint16_t i = 0; i < controller->led_count; i++) {
                rgb_color_t color = controller->base_color;
                color.r = (uint8_t)(color.r * breath);
                color.g = (uint8_t)(color.g * breath);
                color.b = (uint8_t)(color.b * breath);
                set_led_color(controller, i, color);
            }
            break;
        }
        
        case LED_MODE_RAINBOW: {
            for (uint16_t i = 0; i < controller->led_count; i++) {
                float hue = (float)(controller->animation_counter + i * 5) * 0.5f;
                hue = fmodf(hue, 360.0f);
                rgb_color_t color = hsv_to_rgb(hue, 1.0f, 1.0f);
                set_led_color(controller, i, color);
            }
            break;
        }
        
        case LED_MODE_SOLID: {
            for (uint16_t i = 0; i < controller->led_count; i++) {
                set_led_color(controller, i, controller->base_color);
            }
            break;
        }
        
        case LED_MODE_AUDIO_REACTIVE:
            // Audio reactive mode is handled by led_controller_update_audio
            return ESP_OK;
            
        default:
            return ESP_OK;
    }
    
    // Transmit LED data for non-audio modes
    ESP_RETURN_ON_ERROR(rmt_transmit(led_chan, led_encoder, controller->led_data, 
                                   controller->led_count * 3, &tx_config), TAG, "LED transmit failed");
    
    return ESP_OK;
}

esp_err_t led_controller_clear(led_controller_t *controller) {
    if (!controller || !controller->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    memset(controller->led_data, 0, controller->led_count * 3);
    
    ESP_RETURN_ON_ERROR(rmt_transmit(led_chan, led_encoder, controller->led_data, 
                                   controller->led_count * 3, &tx_config), TAG, "LED clear failed");
    
    return ESP_OK;
}

esp_err_t led_controller_deinit(led_controller_t *controller) {
    if (!controller) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!controller->initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing LED controller");
    
    // Clear LEDs
    led_controller_clear(controller);
    
    // Clean up RMT resources
    if (led_chan) {
        rmt_disable(led_chan);
        rmt_del_channel(led_chan);
        led_chan = NULL;
    }
    
    if (led_encoder) {
        rmt_del_encoder(led_encoder);
        led_encoder = NULL;
    }
    
    // Free LED data buffer
    if (controller->led_data) {
        free(controller->led_data);
    }
    
    memset(controller, 0, sizeof(led_controller_t));
    ESP_LOGI(TAG, "LED controller deinitialized");
    return ESP_OK;
}