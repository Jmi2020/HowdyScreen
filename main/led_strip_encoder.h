#ifndef LED_STRIP_ENCODER_H
#define LED_STRIP_ENCODER_H

#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Type of LED strip model
 */
typedef enum {
    LED_MODEL_WS2812,  /*!< WS2812 */
    LED_MODEL_SK6812,  /*!< SK6812 */
} led_model_t;

/**
 * @brief LED strip encoder configuration
 */
typedef struct {
    uint32_t resolution; /*!< Encoder resolution, in Hz */
    led_model_t led_model; /*!< LED model */
} led_strip_encoder_config_t;

/**
 * @brief Create RMT encoder for encoding LED strip pixels into RMT symbols
 *
 * @param[in] config Encoder configuration
 * @param[out] ret_encoder Returned encoder handle
 * @return
 *      - ESP_ERR_INVALID_ARG for any invalid arguments
 *      - ESP_ERR_NO_MEM out of memory when creating encoder
 *      - ESP_OK if creating encoder successfully
 */
esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);

#ifdef __cplusplus
}
#endif

#endif // LED_STRIP_ENCODER_H