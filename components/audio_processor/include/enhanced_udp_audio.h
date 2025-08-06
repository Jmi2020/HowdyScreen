#pragma once

#include "esp_err.h"
#include "udp_audio_streamer.h"
#include "enhanced_vad.h"
#include <stdint.h>
#include <stdbool.h>

// Wake word support - include header for types
#include "esp32_p4_wake_word.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enhanced UDP audio streaming with VAD integration
 * 
 * Extends the basic UDP audio streaming to include Voice Activity Detection
 * information for HowdyTTS server integration. Maintains backward compatibility
 * with existing UDP audio protocol while adding VAD data payload.
 */

// VAD flags for enhanced UDP packets
#define UDP_VAD_FLAG_VOICE_ACTIVE     0x01    // Voice currently detected
#define UDP_VAD_FLAG_SPEECH_START     0x02    // Speech segment started
#define UDP_VAD_FLAG_SPEECH_END       0x04    // Speech segment ended
#define UDP_VAD_FLAG_HIGH_CONFIDENCE  0x08    // High confidence detection
#define UDP_VAD_FLAG_NOISE_UPDATED    0x10    // Noise floor updated
#define UDP_VAD_FLAG_SPECTRAL_VALID   0x20    // Spectral analysis valid
#define UDP_VAD_FLAG_ADAPTIVE_ACTIVE  0x40    // Adaptive threshold active
#define UDP_VAD_FLAG_RESERVED         0x80    // Reserved for future use

// Enhanced packet format version
#define ENHANCED_UDP_AUDIO_VERSION    0x02
#define VERSION_WAKE_WORD            0x03    // Wake word detection version

// Wake word flags for enhanced UDP packets
#define UDP_WAKE_WORD_FLAG_DETECTED   0x01    // Wake word detected
#define UDP_WAKE_WORD_FLAG_CONFIRMED  0x02    // Wake word confirmed by server  
#define UDP_WAKE_WORD_FLAG_REJECTED   0x04    // Wake word rejected by server
#define UDP_WAKE_WORD_FLAG_HIGH_CONF  0x08    // High confidence wake word

/**
 * @brief Enhanced UDP audio packet header with VAD information
 * 
 * Extends the basic udp_audio_header_t with VAD data while maintaining
 * compatibility. The enhanced fields are appended to the basic header.
 */
typedef struct __attribute__((packed)) {
    // Basic UDP audio header (maintains compatibility)
    uint32_t sequence;              // Packet sequence number
    uint16_t sample_count;          // Number of samples in packet
    uint16_t sample_rate;           // Sample rate (16000 Hz)
    uint8_t  channels;              // Number of channels (1 = mono)
    uint8_t  bits_per_sample;       // Bits per sample (16)
    uint16_t flags;                 // Basic packet flags
    
    // Enhanced VAD extension (12 bytes total)
    uint8_t  version;               // Enhanced packet version (0x02)
    uint8_t  vad_flags;             // VAD state flags (see UDP_VAD_FLAG_*)
    uint8_t  vad_confidence;        // VAD confidence (0-255)
    uint8_t  detection_quality;     // Detection quality score (0-255)
    
    uint16_t max_amplitude;         // Maximum amplitude in frame
    uint16_t noise_floor;           // Current adaptive noise floor
    
    uint16_t zero_crossing_rate;    // Zero crossings per frame
    uint8_t  snr_db_scaled;         // SNR in dB * 2 (0-255, divide by 2 for actual dB)
    uint8_t  reserved;              // Reserved for alignment
} enhanced_udp_audio_header_t;

/**
 * @brief Wake word enhanced UDP packet header (VERSION_WAKE_WORD)
 * 
 * Extended header for wake word detection events with 12-byte wake word data
 */
typedef struct __attribute__((packed)) {
    // Basic UDP audio header (maintains compatibility)
    uint32_t sequence;              // Packet sequence number
    uint16_t sample_count;          // Number of samples in packet
    uint16_t sample_rate;           // Sample rate (16000 Hz)
    uint8_t  channels;              // Number of channels (1 = mono)
    uint8_t  bits_per_sample;       // Bits per sample (16)
    uint16_t flags;                 // Basic packet flags
    
    // Enhanced VAD extension (12 bytes)
    uint8_t  version;               // Enhanced packet version (0x03 for wake word)
    uint8_t  vad_flags;             // VAD state flags
    uint8_t  vad_confidence;        // VAD confidence (0-255)
    uint8_t  detection_quality;     // Detection quality score (0-255)
    
    uint16_t max_amplitude;         // Maximum amplitude in frame
    uint16_t noise_floor;           // Current adaptive noise floor
    
    uint16_t zero_crossing_rate;    // Zero crossings per frame
    uint8_t  snr_db_scaled;         // SNR in dB * 2
    uint8_t  reserved_vad;          // Reserved for VAD alignment
    
    // Wake word specific extension (12 bytes)
    uint32_t wake_word_detection_id; // Unique detection ID
    uint8_t  wake_word_flags;       // Wake word flags (see UDP_WAKE_WORD_FLAG_*)
    uint8_t  wake_word_confidence;  // Wake word confidence (0-255)
    uint16_t pattern_match_score;   // Pattern matching score (0-1000)
    uint8_t  syllable_count;        // Detected syllable count
    uint8_t  detection_duration_ms; // Detection duration in ms
    uint16_t wake_word_reserved;    // Reserved for future use
} enhanced_udp_wake_word_header_t;

/**
 * @brief Enhanced UDP audio configuration
 */
typedef struct {
    udp_audio_config_t basic_config;    // Basic UDP configuration
    
    // VAD integration settings
    bool enable_vad_transmission;       // Include VAD data in packets
    bool enable_vad_optimization;       // Optimize transmission based on VAD
    uint8_t vad_update_interval_ms;     // VAD data update interval (10-100ms)
    uint8_t confidence_reporting_threshold; // Min confidence to report (0-255)
    
    // Performance settings
    bool enable_adaptive_bitrate;       // Adapt bitrate based on VAD confidence
    bool enable_silence_suppression;    // Reduce packets during silence
    uint16_t silence_packet_interval_ms; // Packet interval during silence (100-1000ms)
} enhanced_udp_audio_config_t;

/**
 * @brief Enhanced UDP audio statistics with VAD metrics
 */
typedef struct {
    udp_audio_stats_t basic_stats;      // Basic UDP statistics
    
    // VAD transmission statistics
    uint32_t vad_packets_sent;          // Packets with VAD data
    uint32_t voice_packets_sent;        // Packets with voice activity
    uint32_t silence_packets_sent;      // Packets during silence
    uint32_t high_confidence_packets;   // High confidence voice packets
    
    // VAD performance metrics
    float average_vad_confidence;       // Average VAD confidence
    uint16_t current_noise_floor;       // Current noise floor level
    uint32_t vad_state_changes;         // Number of VAD state transitions
    
    // Transmission optimization metrics
    uint32_t packets_suppressed;        // Packets suppressed during silence
    uint32_t bandwidth_saved_bytes;     // Bytes saved through optimization
    float average_packet_interval_ms;   // Average packet transmission interval
} enhanced_udp_audio_stats_t;

/**
 * @brief VAD event callback for UDP transmission
 * 
 * Called when significant VAD events occur that should be transmitted
 */
typedef void (*enhanced_udp_vad_event_cb_t)(const enhanced_vad_result_t *vad_result, void *user_data);

/**
 * @brief Initialize enhanced UDP audio streamer with VAD integration
 * 
 * @param config Enhanced UDP audio configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_init(const enhanced_udp_audio_config_t *config);

/**
 * @brief Deinitialize enhanced UDP audio streamer
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_deinit(void);

/**
 * @brief Send audio samples with VAD information via UDP
 * 
 * Combines audio data with VAD analysis results and transmits using
 * the enhanced UDP protocol with VAD integration.
 * 
 * @param samples Audio samples (16-bit PCM mono)
 * @param sample_count Number of samples
 * @param vad_result VAD analysis results for this audio frame
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_send_with_vad(const int16_t *samples, 
                                          size_t sample_count,
                                          const enhanced_vad_result_t *vad_result);

/**
 * @brief Send enhanced UDP audio packet with full control
 * 
 * For advanced use - send complete packet with enhanced header and VAD data
 * 
 * @param header Enhanced packet header with VAD information
 * @param audio_data Audio payload
 * @param audio_size Audio data size in bytes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_send_enhanced_packet(const enhanced_udp_audio_header_t *header,
                                                 const uint8_t *audio_data,
                                                 size_t audio_size);

/**
 * @brief Set VAD event callback for transmission events
 * 
 * @param callback Callback function for VAD events
 * @param user_data User data passed to callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_set_vad_callback(enhanced_udp_vad_event_cb_t callback, void *user_data);

/**
 * @brief Update VAD transmission settings dynamically
 * 
 * @param enable_vad_transmission Include VAD data in packets
 * @param confidence_threshold Minimum confidence to report (0-255)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_update_vad_settings(bool enable_vad_transmission, 
                                                uint8_t confidence_threshold);

/**
 * @brief Get enhanced UDP audio statistics with VAD metrics
 * 
 * @param stats Output enhanced statistics structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_get_enhanced_stats(enhanced_udp_audio_stats_t *stats);

/**
 * @brief Enable/disable silence suppression optimization
 * 
 * When enabled, reduces packet transmission rate during silence periods
 * to save bandwidth while maintaining protocol compliance.
 * 
 * @param enable Enable silence suppression
 * @param silence_interval_ms Packet interval during silence (100-1000ms)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_set_silence_suppression(bool enable, uint16_t silence_interval_ms);

/**
 * @brief Convert enhanced VAD result to UDP VAD flags
 * 
 * Helper function to encode VAD results into UDP packet flags
 * 
 * @param vad_result Enhanced VAD result
 * @return uint8_t VAD flags for UDP transmission
 */
uint8_t enhanced_udp_audio_encode_vad_flags(const enhanced_vad_result_t *vad_result);

/**
 * @brief Build enhanced UDP header from audio and VAD data
 * 
 * Helper function to construct the enhanced header structure
 * 
 * @param sequence Packet sequence number
 * @param samples Audio samples
 * @param sample_count Number of samples
 * @param sample_rate Sample rate
 * @param vad_result VAD analysis results
 * @param header Output header structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_build_header(uint32_t sequence,
                                         const int16_t *samples,
                                         size_t sample_count,
                                         uint16_t sample_rate,
                                         const enhanced_vad_result_t *vad_result,
                                         enhanced_udp_audio_header_t *header);

/**
 * @brief Get default enhanced UDP audio configuration
 * 
 * @param basic_config Basic UDP configuration to extend
 * @param enhanced_config Output enhanced configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_get_default_config(const udp_audio_config_t *basic_config,
                                               enhanced_udp_audio_config_t *enhanced_config);

/**
 * @brief Check if enhanced UDP audio is actively transmitting with VAD
 * 
 * @return true if streaming with VAD integration active
 */
bool enhanced_udp_audio_is_vad_streaming(void);

/**
 * @brief Reset enhanced statistics counters
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_reset_enhanced_stats(void);

/**
 * @brief Send audio frame with wake word detection data
 * 
 * Enhanced version that includes wake word detection information
 * using VERSION_WAKE_WORD packet format.
 * 
 * @param samples Audio samples (16-bit PCM mono)
 * @param sample_count Number of samples
 * @param vad_result Enhanced VAD result (optional)
 * @param wake_word_result Wake word detection result (optional)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_send_with_wake_word(const int16_t *samples, 
                                                size_t sample_count,
                                                const enhanced_vad_result_t *vad_result,
                                                const esp32_p4_wake_word_result_t *wake_word_result);

/**
 * @brief Build wake word enhanced UDP header
 * 
 * @param sequence Packet sequence number
 * @param samples Audio samples
 * @param sample_count Number of samples
 * @param sample_rate Sample rate
 * @param vad_result VAD analysis results (optional)
 * @param wake_word_result Wake word detection result
 * @param header Output wake word header structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_build_wake_word_header(uint32_t sequence,
                                                   const int16_t *samples,
                                                   size_t sample_count,
                                                   uint16_t sample_rate,
                                                   const enhanced_vad_result_t *vad_result,
                                                   const esp32_p4_wake_word_result_t *wake_word_result,
                                                   enhanced_udp_wake_word_header_t *header);

/**
 * @brief Send enhanced wake word packet
 * 
 * @param header Wake word packet header
 * @param audio_data Audio payload
 * @param audio_size Audio data size in bytes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_udp_audio_send_wake_word_packet(const enhanced_udp_wake_word_header_t *header,
                                                  const uint8_t *audio_data,
                                                  size_t audio_size);

/**
 * @brief Encode wake word result to UDP flags
 * 
 * @param wake_word_result Wake word detection result
 * @return uint8_t Wake word flags for UDP transmission
 */
uint8_t enhanced_udp_audio_encode_wake_word_flags(const esp32_p4_wake_word_result_t *wake_word_result);

#ifdef __cplusplus
}
#endif