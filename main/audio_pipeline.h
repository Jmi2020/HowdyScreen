#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"

typedef struct {
    float overall_level;    // Overall audio level (0.0 - 1.0)
    float bass;            // Bass frequency content
    float mid;             // Mid frequency content  
    float treble;          // Treble frequency content
    bool voice_detected;   // Voice activity detection
} audio_analysis_t;

typedef struct {
    i2s_chan_handle_t tx_handle;
    i2s_chan_handle_t rx_handle;
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t codec_dev;
    bool initialized;
} audio_pipeline_t;

/**
 * Initialize the audio pipeline with ES8311 codec
 */
esp_err_t audio_pipeline_init(audio_pipeline_t *pipeline);

/**
 * Start audio capture and playback
 */
esp_err_t audio_pipeline_start(audio_pipeline_t *pipeline);

/**
 * Stop audio pipeline
 */
esp_err_t audio_pipeline_stop(audio_pipeline_t *pipeline);

/**
 * Read audio samples from microphone
 */
size_t audio_pipeline_read(audio_pipeline_t *pipeline, int16_t *buffer, size_t frames);

/**
 * Write audio samples to speaker
 */
size_t audio_pipeline_write(audio_pipeline_t *pipeline, const int16_t *buffer, size_t frames);

/**
 * Analyze audio buffer for visualization
 */
void audio_analyze_buffer(const int16_t *buffer, size_t frames, audio_analysis_t *analysis);

/**
 * Deinitialize audio pipeline
 */
esp_err_t audio_pipeline_deinit(audio_pipeline_t *pipeline);

#endif // AUDIO_PIPELINE_H