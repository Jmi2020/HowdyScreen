#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESP32-P4 HowdyScreen Phase 4: Audio Interface Header
 * 
 * Smart Microphone + Speaker + Display Interface for HowdyTTS
 * 
 * The ESP32-P4 HowdyScreen device functions as:
 * - 🎤 Smart microphone that captures voice and streams to Mac server
 * - 🔊 Smart speaker that receives TTS audio from Mac server and plays it
 * - 📺 Display that shows program states (listening, processing, speaking, idle)
 * 
 * NO local AI processing - all STT/TTS happens on the Mac server.
 * The device is purely an audio interface with visual feedback.
 */

/**
 * @brief Initialize HowdyScreen Audio Interface System
 * 
 * Sets up:
 * - Audio Interface Coordinator (microphone + speaker management)
 * - WebSocket Client (bidirectional audio streaming)
 * - Service Discovery (automatic HowdyTTS server detection)
 * - UI Manager (visual state feedback)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_phase4_init(void);

/**
 * @brief Start voice listening mode
 * 
 * Activates microphone capture and begins streaming audio to HowdyTTS server.
 * Can be triggered by:
 * - Touch screen interaction
 * - Physical button press
 * - Voice activation (if implemented)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_phase4_start_listening(void);

/**
 * @brief Stop voice listening mode
 * 
 * Stops microphone capture and audio streaming.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_phase4_stop_listening(void);

/**
 * @brief Print system status for debugging
 * 
 * Displays current status of:
 * - Audio interface (microphone, speaker, voice detection)
 * - WebSocket connection
 * - System readiness
 */
void howdy_phase4_print_status(void);

#ifdef __cplusplus
}
#endif