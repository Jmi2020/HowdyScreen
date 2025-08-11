#include "enhanced_udp_audio.h"
#include "udp_audio_streamer.h"
#include "enhanced_vad.h"
#include "esp32_p4_wake_word.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "EnhancedUDP";

// Enhanced UDP audio state
typedef struct {
    enhanced_udp_audio_config_t config;
    enhanced_udp_audio_stats_t stats;
    
    // VAD integration state
    enhanced_udp_vad_event_cb_t vad_callback;
    void *vad_callback_user_data;
    
    // Transmission optimization
    uint64_t last_voice_packet_time;
    uint64_t last_silence_packet_time;
    uint32_t current_sequence;
    bool is_in_voice_segment;
    
    // Performance tracking
    uint64_t total_processing_time_us;
    uint32_t packets_processed;
    
    // State flags
    bool initialized;
    bool vad_streaming_active;
} enhanced_udp_audio_state_t;

static enhanced_udp_audio_state_t s_enhanced_udp_state = {0};

// Default enhanced configuration
static const enhanced_udp_audio_config_t DEFAULT_ENHANCED_CONFIG = {
    .basic_config = {
        .server_ip = "192.168.1.100",
        .server_port = 8003,
        .local_port = 0,
        .buffer_size = 2048,
        .packet_size_ms = 20,           // 20ms packets
        .enable_compression = false     // Raw PCM for minimal latency
    },
    
    .enable_vad_transmission = true,
    .enable_vad_optimization = true,
    .vad_update_interval_ms = 20,       // Every packet
    .confidence_reporting_threshold = 0, // Report all confidence levels
    
    .enable_adaptive_bitrate = false,   // Keep bitrate constant for now
    .enable_silence_suppression = true,
    .silence_packet_interval_ms = 100   // 100ms during silence
};

esp_err_t enhanced_udp_audio_init(const enhanced_udp_audio_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid enhanced UDP configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_enhanced_udp_state.initialized) {
        ESP_LOGW(TAG, "Enhanced UDP audio already initialized");
        return ESP_OK;
    }
    
    // Initialize basic UDP audio first
    esp_err_t ret = udp_audio_init(&config->basic_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize basic UDP audio: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize enhanced state
    memset(&s_enhanced_udp_state, 0, sizeof(enhanced_udp_audio_state_t));
    s_enhanced_udp_state.config = *config;
    s_enhanced_udp_state.initialized = true;
    s_enhanced_udp_state.last_voice_packet_time = esp_timer_get_time();
    s_enhanced_udp_state.last_silence_packet_time = esp_timer_get_time();
    
    ESP_LOGI(TAG, "Enhanced UDP audio initialized - VAD transmission: %s, optimization: %s",
             config->enable_vad_transmission ? "enabled" : "disabled",
             config->enable_vad_optimization ? "enabled" : "disabled");
    
    return ESP_OK;
}

esp_err_t enhanced_udp_audio_deinit(void)
{
    if (!s_enhanced_udp_state.initialized) {
        return ESP_OK;
    }
    
    // Deinitialize basic UDP audio
    esp_err_t ret = udp_audio_deinit();
    
    // Clear enhanced state
    memset(&s_enhanced_udp_state, 0, sizeof(enhanced_udp_audio_state_t));
    
    ESP_LOGI(TAG, "Enhanced UDP audio deinitialized");
    return ret;
}

uint8_t enhanced_udp_audio_encode_vad_flags(const enhanced_vad_result_t *vad_result)
{
    if (!vad_result) {
        return 0;
    }
    
    uint8_t flags = 0;
    
    if (vad_result->voice_detected) {
        flags |= UDP_VAD_FLAG_VOICE_ACTIVE;
    }
    
    if (vad_result->speech_started) {
        flags |= UDP_VAD_FLAG_SPEECH_START;
    }
    
    if (vad_result->speech_ended) {
        flags |= UDP_VAD_FLAG_SPEECH_END;
    }
    
    if (vad_result->high_confidence) {
        flags |= UDP_VAD_FLAG_HIGH_CONFIDENCE;
    }
    
    // Add other flags based on VAD analysis
    if (vad_result->zero_crossing_rate > 0) {
        flags |= UDP_VAD_FLAG_SPECTRAL_VALID;
    }
    
    if (vad_result->noise_floor > 0) {
        flags |= UDP_VAD_FLAG_ADAPTIVE_ACTIVE;
    }
    
    return flags;
}

esp_err_t enhanced_udp_audio_build_header(uint32_t sequence,
                                         const int16_t *samples,
                                         size_t sample_count,
                                         uint16_t sample_rate,
                                         const enhanced_vad_result_t *vad_result,
                                         enhanced_udp_audio_header_t *header)
{
    if (!header) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Build basic header
    header->sequence = sequence;
    header->sample_count = sample_count;
    header->sample_rate = sample_rate;
    header->channels = 1;               // Mono
    header->bits_per_sample = 16;       // 16-bit PCM
    header->flags = 0;                  // No compression
    
    // Add enhanced VAD fields
    header->version = ENHANCED_UDP_AUDIO_VERSION;
    
    if (vad_result && s_enhanced_udp_state.config.enable_vad_transmission) {
        header->vad_flags = enhanced_udp_audio_encode_vad_flags(vad_result);
        header->vad_confidence = (uint8_t)(vad_result->confidence * 255.0f);
        header->detection_quality = vad_result->detection_quality;
        header->max_amplitude = vad_result->max_amplitude;
        header->noise_floor = vad_result->noise_floor;
        header->zero_crossing_rate = vad_result->zero_crossing_rate;
        header->snr_db_scaled = (uint8_t)(vad_result->snr_db * 2.0f); // Scale SNR to 0-255
    } else {
        // No VAD data available
        header->vad_flags = 0;
        header->vad_confidence = 0;
        header->detection_quality = 0;
        header->max_amplitude = 0;
        header->noise_floor = 0;
        header->zero_crossing_rate = 0;
        header->snr_db_scaled = 0;
    }
    
    header->reserved = 0;
    
    return ESP_OK;
}

static bool should_transmit_packet(const enhanced_vad_result_t *vad_result)
{
    if (!s_enhanced_udp_state.config.enable_vad_optimization) {
        return true; // Always transmit if optimization disabled
    }
    
    uint64_t current_time = esp_timer_get_time();
    
    // Always transmit if voice is detected
    if (vad_result && vad_result->voice_detected) {
        s_enhanced_udp_state.is_in_voice_segment = true;
        s_enhanced_udp_state.last_voice_packet_time = current_time;
        return true;
    }
    
    // Always transmit speech start/end events
    if (vad_result && (vad_result->speech_started || vad_result->speech_ended)) {
        s_enhanced_udp_state.is_in_voice_segment = vad_result->speech_started;
        return true;
    }
    
    // Handle silence periods with suppression
    if (s_enhanced_udp_state.config.enable_silence_suppression) {
        uint32_t silence_interval_us = s_enhanced_udp_state.config.silence_packet_interval_ms * 1000;
        
        if (current_time - s_enhanced_udp_state.last_silence_packet_time >= silence_interval_us) {
            s_enhanced_udp_state.last_silence_packet_time = current_time;
            return true; // Send periodic silence packets
        }
        
        return false; // Suppress this silence packet
    }
    
    return true; // Default: transmit all packets
}

esp_err_t enhanced_udp_audio_send_with_vad(const int16_t *samples, 
                                          size_t sample_count,
                                          const enhanced_vad_result_t *vad_result)
{
    if (!s_enhanced_udp_state.initialized) {
        ESP_LOGE(TAG, "Enhanced UDP audio not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!samples || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint64_t start_time = esp_timer_get_time();
    
    // Check if we should transmit this packet
    if (!should_transmit_packet(vad_result)) {
        s_enhanced_udp_state.stats.packets_suppressed++;
        s_enhanced_udp_state.stats.bandwidth_saved_bytes += (sizeof(enhanced_udp_audio_header_t) + sample_count * 2);
        return ESP_OK; // Packet suppressed but return success
    }
    
    // Build enhanced header
    enhanced_udp_audio_header_t header;
    esp_err_t ret = enhanced_udp_audio_build_header(
        s_enhanced_udp_state.current_sequence++,
        samples,
        sample_count,
        16000, // Fixed sample rate: 16kHz
        vad_result,
        &header
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build enhanced header: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Send enhanced packet
    ret = enhanced_udp_audio_send_enhanced_packet(&header, (const uint8_t*)samples, sample_count * sizeof(int16_t));
    
    if (ret == ESP_OK) {
        // Update statistics
        s_enhanced_udp_state.stats.basic_stats.packets_sent++;
        s_enhanced_udp_state.stats.basic_stats.bytes_sent += sizeof(enhanced_udp_audio_header_t) + (sample_count * 2);
        
        if (s_enhanced_udp_state.config.enable_vad_transmission) {
            s_enhanced_udp_state.stats.vad_packets_sent++;
        }
        
        if (vad_result) {
            if (vad_result->voice_detected) {
                s_enhanced_udp_state.stats.voice_packets_sent++;
            } else {
                s_enhanced_udp_state.stats.silence_packets_sent++;
            }
            
            if (vad_result->high_confidence) {
                s_enhanced_udp_state.stats.high_confidence_packets++;
            }
            
            // Update running averages
            float old_avg = s_enhanced_udp_state.stats.average_vad_confidence;
            uint32_t packet_count = s_enhanced_udp_state.stats.vad_packets_sent;
            s_enhanced_udp_state.stats.average_vad_confidence = 
                (old_avg * (packet_count - 1) + vad_result->confidence) / packet_count;
            
            s_enhanced_udp_state.stats.current_noise_floor = vad_result->noise_floor;
            
            if (vad_result->speech_started || vad_result->speech_ended) {
                s_enhanced_udp_state.stats.vad_state_changes++;
            }
        }
        
        // Call VAD event callback if set
        if (vad_result && s_enhanced_udp_state.vad_callback) {
            s_enhanced_udp_state.vad_callback(vad_result, s_enhanced_udp_state.vad_callback_user_data);
        }
        
        // Update performance tracking
        uint64_t processing_time = esp_timer_get_time() - start_time;
        s_enhanced_udp_state.total_processing_time_us += processing_time;
        s_enhanced_udp_state.packets_processed++;
        
        // Update average packet interval
        static uint64_t last_packet_time = 0;
        if (last_packet_time > 0) {
            float interval_ms = (start_time - last_packet_time) / 1000.0f;
            float old_avg_interval = s_enhanced_udp_state.stats.average_packet_interval_ms;
            uint32_t total_packets = s_enhanced_udp_state.stats.basic_stats.packets_sent;
            s_enhanced_udp_state.stats.average_packet_interval_ms = 
                (old_avg_interval * (total_packets - 1) + interval_ms) / total_packets;
        }
        last_packet_time = start_time;
    }
    
    return ret;
}

esp_err_t enhanced_udp_audio_send_enhanced_packet(const enhanced_udp_audio_header_t *header,
                                                 const uint8_t *audio_data,
                                                 size_t audio_size)
{
    if (!s_enhanced_udp_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!header || !audio_data || audio_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // For now, send the enhanced header followed by audio data
    // In a complete implementation, this would use the actual UDP socket
    // Here we convert to basic header for compatibility with existing UDP streamer
    
    udp_audio_header_t basic_header = {
        .sequence = header->sequence,
        .sample_count = header->sample_count,
        .sample_rate = header->sample_rate,
        .channels = header->channels,
        .bits_per_sample = header->bits_per_sample,
        .flags = header->flags
    };
    
    // Use the existing UDP audio streamer for actual transmission
    esp_err_t ret = udp_audio_send_packet(&basic_header, audio_data, audio_size);
    
    if (ret == ESP_OK) {
        ESP_LOGV(TAG, "Sent enhanced packet seq=%lu, vad_flags=0x%02x, confidence=%d",
                 header->sequence, header->vad_flags, header->vad_confidence);
    }
    
    return ret;
}

esp_err_t enhanced_udp_audio_set_vad_callback(enhanced_udp_vad_event_cb_t callback, void *user_data)
{
    s_enhanced_udp_state.vad_callback = callback;
    s_enhanced_udp_state.vad_callback_user_data = user_data;
    return ESP_OK;
}

esp_err_t enhanced_udp_audio_update_vad_settings(bool enable_vad_transmission, 
                                                uint8_t confidence_threshold)
{
    if (!s_enhanced_udp_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_enhanced_udp_state.config.enable_vad_transmission = enable_vad_transmission;
    s_enhanced_udp_state.config.confidence_reporting_threshold = confidence_threshold;
    
    ESP_LOGI(TAG, "VAD settings updated - transmission: %s, threshold: %d",
             enable_vad_transmission ? "enabled" : "disabled", confidence_threshold);
    
    return ESP_OK;
}

esp_err_t enhanced_udp_audio_get_enhanced_stats(enhanced_udp_audio_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_enhanced_udp_state.initialized) {
        // Get basic UDP stats first
        udp_audio_get_stats(&s_enhanced_udp_state.stats.basic_stats);
        *stats = s_enhanced_udp_state.stats;
    } else {
        memset(stats, 0, sizeof(enhanced_udp_audio_stats_t));
    }
    
    return ESP_OK;
}

esp_err_t enhanced_udp_audio_set_silence_suppression(bool enable, uint16_t silence_interval_ms)
{
    if (!s_enhanced_udp_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_enhanced_udp_state.config.enable_silence_suppression = enable;
    s_enhanced_udp_state.config.silence_packet_interval_ms = silence_interval_ms;
    
    ESP_LOGI(TAG, "Silence suppression %s - interval: %d ms",
             enable ? "enabled" : "disabled", silence_interval_ms);
    
    return ESP_OK;
}

esp_err_t enhanced_udp_audio_get_default_config(const udp_audio_config_t *basic_config,
                                               enhanced_udp_audio_config_t *enhanced_config)
{
    if (!enhanced_config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *enhanced_config = DEFAULT_ENHANCED_CONFIG;
    
    if (basic_config) {
        enhanced_config->basic_config = *basic_config;
    }
    
    return ESP_OK;
}

bool enhanced_udp_audio_is_vad_streaming(void)
{
    return s_enhanced_udp_state.initialized && 
           s_enhanced_udp_state.config.enable_vad_transmission &&
           udp_audio_is_streaming();
}

esp_err_t enhanced_udp_audio_reset_enhanced_stats(void)
{
    if (!s_enhanced_udp_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Reset basic stats
    udp_audio_reset_stats();
    
    // Reset enhanced stats
    memset(&s_enhanced_udp_state.stats, 0, sizeof(enhanced_udp_audio_stats_t));
    s_enhanced_udp_state.total_processing_time_us = 0;
    s_enhanced_udp_state.packets_processed = 0;
    
    ESP_LOGI(TAG, "Enhanced UDP audio statistics reset");
    return ESP_OK;
}

// Wake word enhanced UDP functions

uint8_t enhanced_udp_audio_encode_wake_word_flags(const esp32_p4_wake_word_result_t *wake_word_result)
{
    if (!wake_word_result) {
        return 0;
    }
    
    uint8_t flags = 0;
    
    if (wake_word_result->state == WAKE_WORD_STATE_TRIGGERED) {
        flags |= UDP_WAKE_WORD_FLAG_DETECTED;
    }
    
    if (wake_word_result->state == WAKE_WORD_STATE_CONFIRMED) {
        flags |= UDP_WAKE_WORD_FLAG_CONFIRMED;
    }
    
    if (wake_word_result->state == WAKE_WORD_STATE_REJECTED) {
        flags |= UDP_WAKE_WORD_FLAG_REJECTED;
    }
    
    if (wake_word_result->confidence_level >= WAKE_WORD_CONFIDENCE_HIGH) {
        flags |= UDP_WAKE_WORD_FLAG_HIGH_CONF;
    }
    
    return flags;
}

esp_err_t enhanced_udp_audio_build_wake_word_header(uint32_t sequence,
                                                   const int16_t *samples,
                                                   size_t sample_count,
                                                   uint16_t sample_rate,
                                                   const enhanced_vad_result_t *vad_result,
                                                   const esp32_p4_wake_word_result_t *wake_word_result,
                                                   enhanced_udp_wake_word_header_t *header)
{
    if (!header) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Build basic header fields
    header->sequence = sequence;
    header->sample_count = sample_count;
    header->sample_rate = sample_rate;
    header->channels = 1;               // Mono
    header->bits_per_sample = 16;       // 16-bit PCM
    header->flags = 0;                  // No compression
    
    // Enhanced VAD fields  
    header->version = VERSION_WAKE_WORD; // Wake word version
    
    if (vad_result && s_enhanced_udp_state.config.enable_vad_transmission) {
        header->vad_flags = enhanced_udp_audio_encode_vad_flags(vad_result);
        header->vad_confidence = (uint8_t)(vad_result->confidence * 255.0f);
        header->detection_quality = vad_result->detection_quality;
        header->max_amplitude = vad_result->max_amplitude;
        header->noise_floor = vad_result->noise_floor;
        header->zero_crossing_rate = vad_result->zero_crossing_rate;
        header->snr_db_scaled = (uint8_t)(vad_result->snr_db * 2.0f);
    } else {
        // No VAD data available
        header->vad_flags = 0;
        header->vad_confidence = 0;
        header->detection_quality = 0;
        header->max_amplitude = 0;
        header->noise_floor = 0;
        header->zero_crossing_rate = 0;
        header->snr_db_scaled = 0;
    }
    
    header->reserved_vad = 0;
    
    // Wake word specific fields
    if (wake_word_result) {
        header->wake_word_detection_id = wake_word_result->detection_timestamp_ms; // Use timestamp as ID
        header->wake_word_flags = enhanced_udp_audio_encode_wake_word_flags(wake_word_result);
        header->wake_word_confidence = (uint8_t)(wake_word_result->confidence_score * 255.0f);
        header->pattern_match_score = wake_word_result->pattern_match_score;
        header->syllable_count = wake_word_result->syllable_count;
        header->detection_duration_ms = (uint8_t)fmin(255, wake_word_result->detection_duration_ms);
    } else {
        // No wake word data
        header->wake_word_detection_id = 0;
        header->wake_word_flags = 0;
        header->wake_word_confidence = 0;
        header->pattern_match_score = 0;
        header->syllable_count = 0;
        header->detection_duration_ms = 0;
    }
    
    header->wake_word_reserved = 0;
    
    return ESP_OK;
}

esp_err_t enhanced_udp_audio_send_wake_word_packet(const enhanced_udp_wake_word_header_t *header,
                                                  const uint8_t *audio_data,
                                                  size_t audio_size)
{
    if (!s_enhanced_udp_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!header || !audio_data || audio_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // For now, convert to basic header for compatibility with existing UDP streamer
    // In a complete implementation, this would send the full wake word header
    
    udp_audio_header_t basic_header = {
        .sequence = header->sequence,
        .sample_count = header->sample_count,
        .sample_rate = header->sample_rate,
        .channels = header->channels,
        .bits_per_sample = header->bits_per_sample,
        .flags = header->flags | 0x8000  // Set high bit to indicate wake word packet
    };
    
    // Use the existing UDP audio streamer for actual transmission
    esp_err_t ret = udp_audio_send_packet(&basic_header, audio_data, audio_size);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🎯 Sent wake word packet seq=%lu, ww_id=%lu, flags=0x%02x, confidence=%d", 
                header->sequence, header->wake_word_detection_id, 
                header->wake_word_flags, header->wake_word_confidence);
    }
    
    return ret;
}

esp_err_t enhanced_udp_audio_send_with_wake_word(const int16_t *samples, 
                                                size_t sample_count,
                                                const enhanced_vad_result_t *vad_result,
                                                const esp32_p4_wake_word_result_t *wake_word_result)
{
    if (!s_enhanced_udp_state.initialized) {
        ESP_LOGE(TAG, "Enhanced UDP audio not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!samples || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint64_t start_time = esp_timer_get_time();
    
    // Check if we should transmit this packet (considering both VAD and wake word)
    bool should_transmit = should_transmit_packet(vad_result);
    bool has_wake_word = wake_word_result && 
                        (wake_word_result->state == WAKE_WORD_STATE_TRIGGERED || 
                         wake_word_result->state == WAKE_WORD_STATE_CONFIRMED ||
                         wake_word_result->state == WAKE_WORD_STATE_REJECTED);
    
    // Always transmit if wake word detected
    if (has_wake_word) {
        should_transmit = true;
    }
    
    if (!should_transmit) {
        s_enhanced_udp_state.stats.packets_suppressed++;
        s_enhanced_udp_state.stats.bandwidth_saved_bytes += (sizeof(enhanced_udp_wake_word_header_t) + sample_count * 2);
        return ESP_OK; // Packet suppressed but return success
    }
    
    // Build wake word enhanced header
    enhanced_udp_wake_word_header_t header;
    esp_err_t ret = enhanced_udp_audio_build_wake_word_header(
        s_enhanced_udp_state.current_sequence++,
        samples,
        sample_count,
        16000, // Fixed sample rate for now
        vad_result,
        wake_word_result,
        &header
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build wake word header: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Send wake word packet
    ret = enhanced_udp_audio_send_wake_word_packet(&header, (const uint8_t*)samples, sample_count * sizeof(int16_t));
    
    if (ret == ESP_OK) {
        // Update statistics
        s_enhanced_udp_state.stats.basic_stats.packets_sent++;
        s_enhanced_udp_state.stats.basic_stats.bytes_sent += sizeof(enhanced_udp_wake_word_header_t) + (sample_count * 2);
        
        if (s_enhanced_udp_state.config.enable_vad_transmission) {
            s_enhanced_udp_state.stats.vad_packets_sent++;
        }
        
        if (vad_result) {
            if (vad_result->voice_detected) {
                s_enhanced_udp_state.stats.voice_packets_sent++;
            } else {
                s_enhanced_udp_state.stats.silence_packets_sent++;
            }
            
            if (vad_result->high_confidence) {
                s_enhanced_udp_state.stats.high_confidence_packets++;
            }
        }
        
        // Log wake word events
        if (has_wake_word) {
            ESP_LOGI(TAG, "🎤 Wake word packet sent - ID: %lu, confidence: %.2f%%, syllables: %d", 
                    header.wake_word_detection_id, 
                    wake_word_result->confidence_score * 100,
                    wake_word_result->syllable_count);
        }
        
        // Update performance tracking
        uint64_t processing_time = esp_timer_get_time() - start_time;
        s_enhanced_udp_state.total_processing_time_us += processing_time;
        s_enhanced_udp_state.packets_processed++;
    }
    
    return ret;
}
