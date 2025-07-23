#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <stdint.h>
#include "esp_err.h"
#include "audio_pipeline.h"

typedef struct {
    uint8_t r, g, b;
} rgb_color_t;

typedef enum {
    LED_MODE_OFF,
    LED_MODE_AUDIO_REACTIVE,
    LED_MODE_BREATHING,
    LED_MODE_RAINBOW,
    LED_MODE_SOLID
} led_mode_t;

typedef struct {
    uint8_t *led_data;
    size_t led_count;
    led_mode_t mode;
    rgb_color_t base_color;
    float brightness;
    bool initialized;
    uint32_t animation_counter;
} led_controller_t;

/**
 * Initialize LED controller with WS2812B
 */
esp_err_t led_controller_init(led_controller_t *controller);

/**
 * Set LED mode
 */
esp_err_t led_controller_set_mode(led_controller_t *controller, led_mode_t mode);

/**
 * Set base color for solid/breathing modes
 */
esp_err_t led_controller_set_color(led_controller_t *controller, uint8_t r, uint8_t g, uint8_t b);

/**
 * Set overall brightness (0.0 - 1.0)
 */
esp_err_t led_controller_set_brightness(led_controller_t *controller, float brightness);

/**
 * Update LEDs with audio analysis data
 */
esp_err_t led_controller_update_audio(led_controller_t *controller, const audio_analysis_t *analysis);

/**
 * Update LED animation (call regularly)
 */
esp_err_t led_controller_update_animation(led_controller_t *controller);

/**
 * Turn off all LEDs
 */
esp_err_t led_controller_clear(led_controller_t *controller);

/**
 * Deinitialize LED controller
 */
esp_err_t led_controller_deinit(led_controller_t *controller);

#endif // LED_CONTROLLER_H