#include "enhanced_vad.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "EnhancedVAD";

// Enhanced VAD instance structure
struct enhanced_vad_instance {
    enhanced_vad_config_t config;
    
    // State tracking
    bool current_voice_state;
    bool previous_voice_state;
    uint64_t voice_start_time;
    uint64_t silence_start_time;
    uint64_t last_process_time;
    
    // Statistics
    enhanced_vad_stats_t stats;
    
    // Adaptive threshold state
    float current_noise_floor;
    float noise_floor_accumulator;
    uint32_t noise_samples_count;
    uint16_t current_threshold;
    
    // Spectral analysis state
    float *fft_buffer;                  // FFT working buffer
    float *window;                      // Windowing function
    uint16_t fft_size;                 // FFT size (power of 2)
    
    // Multi-frame consistency state
    bool *frame_decisions;             // Circular buffer for frame decisions
    float *frame_confidences;          // Circular buffer for confidences
    uint8_t frame_buffer_index;       // Current position in circular buffer
    uint8_t filled_frames;            // Number of frames in buffer
    
    // Conversation-aware state
    vad_conversation_context_t conversation_context;
    uint64_t context_change_time;      // Time when context last changed
    float current_tts_level;           // Current TTS audio level (0.0-1.0)
    float context_adapted_threshold;   // Threshold after conversation adaptation
    bool echo_suppression_active;      // Current echo suppression status
    
    // Performance tracking
    uint64_t total_processing_time_us;
    uint32_t processing_count;
};

// Default configuration optimized for ESP32-P4
static const enhanced_vad_config_t DEFAULT_CONFIG = {
    // Basic energy detection
    .amplitude_threshold = 2000,
    .silence_threshold_ms = 1500,      // Reduced from 3000ms for faster response
    .min_voice_duration_ms = 200,      // Minimum voice segment
    .sample_rate = 16000,
    
    // Adaptive threshold
    .noise_floor_alpha = 0.05f,        // 5% adaptation rate
    .snr_threshold_db = 8.0f,          // 8dB SNR threshold
    .adaptation_window_ms = 500,       // 500ms adaptation window
    
    // Spectral analysis
    .zcr_threshold_min = 5,
    .zcr_threshold_max = 200,
    .low_freq_ratio_threshold = 0.4f,
    .spectral_rolloff_threshold = 0.85f,
    
    // Consistency checking
    .consistency_frames = 5,           // 5-frame consistency
    .confidence_threshold = 0.6f,      // 60% confidence threshold
    
    // Conversation-aware configuration
    .conversation = {
        .idle_threshold_multiplier = 80,        // 0.8x - Higher sensitivity for wake word
        .listening_threshold_multiplier = 100,  // 1.0x - Normal sensitivity
        .speaking_threshold_multiplier = 150,   // 1.5x - Lower sensitivity during TTS  
        .echo_suppression_db = 15,             // 15dB echo suppression
        .tts_fade_time_ms = 200                // 200ms fade time for TTS transitions
    },
    
    // Enable all features including conversation-aware processing
    .feature_flags = ENHANCED_VAD_ENABLE_ADAPTIVE_THRESHOLD | 
                    ENHANCED_VAD_ENABLE_SPECTRAL_ANALYSIS | 
                    ENHANCED_VAD_ENABLE_CONSISTENCY_CHECK |
                    ENHANCED_VAD_ENABLE_SNR_ANALYSIS |
                    ENHANCED_VAD_ENABLE_CONVERSATION_AWARE,
    
    .processing_mode = 0               // Full processing mode
};

enhanced_vad_handle_t enhanced_vad_init(const enhanced_vad_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid enhanced VAD configuration");
        return NULL;
    }
    
    struct enhanced_vad_instance *vad = heap_caps_malloc(sizeof(struct enhanced_vad_instance), MALLOC_CAP_DEFAULT);
    if (!vad) {
        ESP_LOGE(TAG, "Failed to allocate enhanced VAD instance");
        return NULL;
    }
    
    // Initialize VAD instance
    memset(vad, 0, sizeof(struct enhanced_vad_instance));
    vad->config = *config;
    vad->last_process_time = esp_timer_get_time();
    vad->current_noise_floor = config->amplitude_threshold * 0.3f;  // Initial noise floor
    vad->current_threshold = config->amplitude_threshold;
    
    // Initialize conversation-aware state
    vad->conversation_context = VAD_CONVERSATION_IDLE;  // Start in idle state
    vad->context_change_time = esp_timer_get_time();
    vad->current_tts_level = 0.0f;
    vad->context_adapted_threshold = config->amplitude_threshold;
    vad->echo_suppression_active = false;
    
    // Initialize spectral analysis if enabled
    if (config->feature_flags & ENHANCED_VAD_ENABLE_SPECTRAL_ANALYSIS) {
        vad->fft_size = 512;  // 32ms at 16kHz
        
        // Allocate FFT buffers
        vad->fft_buffer = heap_caps_malloc(vad->fft_size * sizeof(float), MALLOC_CAP_DEFAULT);
        vad->window = heap_caps_malloc(vad->fft_size * sizeof(float), MALLOC_CAP_DEFAULT);
        
        if (!vad->fft_buffer || !vad->window) {
            ESP_LOGE(TAG, "Failed to allocate spectral analysis buffers");
            enhanced_vad_deinit(vad);
            return NULL;
        }
        
        // Initialize Hann window
        for (int i = 0; i < vad->fft_size; i++) {
            vad->window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (vad->fft_size - 1)));
        }
    }
    
    // Initialize consistency checking if enabled
    if (config->feature_flags & ENHANCED_VAD_ENABLE_CONSISTENCY_CHECK) {
        vad->frame_decisions = heap_caps_malloc(config->consistency_frames * sizeof(bool), MALLOC_CAP_DEFAULT);
        vad->frame_confidences = heap_caps_malloc(config->consistency_frames * sizeof(float), MALLOC_CAP_DEFAULT);
        
        if (!vad->frame_decisions || !vad->frame_confidences) {
            ESP_LOGE(TAG, "Failed to allocate consistency checking buffers");
            enhanced_vad_deinit(vad);
            return NULL;
        }
        
        memset(vad->frame_decisions, 0, config->consistency_frames * sizeof(bool));
        memset(vad->frame_confidences, 0, config->consistency_frames * sizeof(float));
    }
    
    ESP_LOGI(TAG, "Enhanced VAD initialized - features: 0x%lx, threshold: %d, sample_rate: %d Hz", 
             config->feature_flags, config->amplitude_threshold, config->sample_rate);
    
    return vad;
}

esp_err_t enhanced_vad_deinit(enhanced_vad_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Free spectral analysis buffers
    if (handle->fft_buffer) {
        heap_caps_free(handle->fft_buffer);
    }
    if (handle->window) {
        heap_caps_free(handle->window);
    }
    
    // Free consistency checking buffers
    if (handle->frame_decisions) {
        heap_caps_free(handle->frame_decisions);
    }
    if (handle->frame_confidences) {
        heap_caps_free(handle->frame_confidences);
    }
    
    ESP_LOGI(TAG, "Enhanced VAD deinitialized - detections: %lu, avg confidence: %.2f", 
             handle->stats.detection_count, handle->stats.average_confidence);
    
    heap_caps_free(handle);
    return ESP_OK;
}

// Helper function to apply conversation context to threshold
static void apply_conversation_context(enhanced_vad_handle_t handle)
{
    if (!(handle->config.feature_flags & ENHANCED_VAD_ENABLE_CONVERSATION_AWARE)) {
        handle->context_adapted_threshold = handle->current_threshold;
        return;
    }
    
    uint16_t multiplier;
    switch (handle->conversation_context) {
        case VAD_CONVERSATION_IDLE:
            multiplier = handle->config.conversation.idle_threshold_multiplier;
            break;
        case VAD_CONVERSATION_LISTENING:
            multiplier = handle->config.conversation.listening_threshold_multiplier;
            break;
        case VAD_CONVERSATION_SPEAKING:
            multiplier = handle->config.conversation.speaking_threshold_multiplier;
            // Apply additional echo suppression during TTS
            if (handle->current_tts_level > 0.0f) {
                float echo_reduction = powf(10.0f, -handle->config.conversation.echo_suppression_db / 20.0f);
                multiplier = (uint16_t)(multiplier * (1.0f + handle->current_tts_level * (1.0f - echo_reduction)));
            }
            break;
        case VAD_CONVERSATION_PROCESSING:
        default:
            multiplier = handle->config.conversation.listening_threshold_multiplier;
            break;
    }
    
    // Apply context multiplier
    handle->context_adapted_threshold = (handle->current_threshold * multiplier) / 100;
    
    // Apply echo suppression if TTS is active
    handle->echo_suppression_active = (handle->conversation_context == VAD_CONVERSATION_SPEAKING && 
                                      handle->current_tts_level > 0.1f);
}

// Layer 1: Enhanced energy detection with adaptive noise floor
static esp_err_t process_energy_detection(enhanced_vad_handle_t handle, 
                                         const int16_t *buffer, 
                                         size_t samples,
                                         enhanced_vad_result_t *result)
{
    // Calculate RMS amplitude and max amplitude
    float rms_sum = 0;
    uint16_t max_amplitude = 0;
    
    for (size_t i = 0; i < samples; i++) {
        uint16_t amplitude = abs(buffer[i]);
        if (amplitude > max_amplitude) {
            max_amplitude = amplitude;
        }
        rms_sum += (float)(buffer[i] * buffer[i]);
    }
    
    float rms_amplitude = sqrtf(rms_sum / samples);
    result->max_amplitude = max_amplitude;
    
    // Adaptive noise floor calculation
    if (handle->config.feature_flags & ENHANCED_VAD_ENABLE_ADAPTIVE_THRESHOLD) {
        float alpha = handle->config.noise_floor_alpha;
        
        // Update noise floor when no voice is detected (using previous frame result)
        if (!handle->current_voice_state) {
            handle->current_noise_floor = alpha * rms_amplitude + (1.0f - alpha) * handle->current_noise_floor;
            handle->stats.adaptations_count++;
        }
        
        // Calculate SNR
        if (handle->config.feature_flags & ENHANCED_VAD_ENABLE_SNR_ANALYSIS) {
            result->snr_db = 20.0f * log10f((rms_amplitude + 1.0f) / (handle->current_noise_floor + 1.0f));
        }
        
        // Adaptive threshold based on noise floor
        handle->current_threshold = (uint16_t)(handle->current_noise_floor * powf(10.0f, handle->config.snr_threshold_db / 20.0f));
        
        // Ensure minimum threshold
        if (handle->current_threshold < handle->config.amplitude_threshold / 4) {
            handle->current_threshold = handle->config.amplitude_threshold / 4;
        }
    } else {
        handle->current_threshold = handle->config.amplitude_threshold;
    }
    
    result->noise_floor = (uint16_t)handle->current_noise_floor;
    
    // Apply conversation context to threshold
    apply_conversation_context(handle);
    
    // Use conversation-adapted threshold for voice detection
    uint16_t effective_threshold = handle->context_adapted_threshold;
    bool energy_voice_detected = (max_amplitude > effective_threshold);
    
    // Additional echo suppression during TTS playback
    if (handle->echo_suppression_active && handle->current_tts_level > 0.1f) {
        // Reduce detection confidence during TTS playback
        float echo_factor = 1.0f - (handle->current_tts_level * 0.5f); // Up to 50% reduction
        if (energy_voice_detected && echo_factor < 0.8f) {
            // Require higher amplitude during TTS to confirm voice
            energy_voice_detected = (max_amplitude > effective_threshold * 1.3f);
        }
    }
    
    return energy_voice_detected ? ESP_OK : ESP_FAIL;
}

// Layer 2: Spectral analysis for speech characteristics
static esp_err_t process_spectral_analysis(enhanced_vad_handle_t handle,
                                         const int16_t *buffer,
                                         size_t samples,
                                         enhanced_vad_result_t *result)
{
    if (!(handle->config.feature_flags & ENHANCED_VAD_ENABLE_SPECTRAL_ANALYSIS) || 
        !handle->fft_buffer || samples < handle->fft_size) {
        return ESP_OK; // Skip spectral analysis
    }
    
    // Copy and window the data
    for (size_t i = 0; i < handle->fft_size && i < samples; i++) {
        handle->fft_buffer[i] = (float)buffer[i] * handle->window[i];
    }
    
    // Pad with zeros if needed
    for (size_t i = samples; i < handle->fft_size; i++) {
        handle->fft_buffer[i] = 0.0f;
    }
    
    // Calculate Zero Crossing Rate (lightweight alternative to full FFT for now)
    uint16_t zero_crossings = 0;
    for (size_t i = 1; i < samples; i++) {
        if ((buffer[i-1] >= 0 && buffer[i] < 0) || (buffer[i-1] < 0 && buffer[i] >= 0)) {
            zero_crossings++;
        }
    }
    
    result->zero_crossing_rate = zero_crossings;
    
    // Simple frequency domain analysis using energy distribution
    float low_freq_energy = 0;
    float total_energy = 0;
    
    // Approximate frequency analysis without full FFT (for performance)
    // This is a simplified approach - could be enhanced with actual FFT later
    for (size_t i = 0; i < samples; i++) {
        float sample_energy = (float)(buffer[i] * buffer[i]);
        total_energy += sample_energy;
        
        // Very rough approximation: early samples represent lower frequencies
        if (i < samples / 3) {
            low_freq_energy += sample_energy;
        }
    }
    
    result->low_freq_energy_ratio = (total_energy > 0) ? (low_freq_energy / total_energy) : 0;
    result->spectral_rolloff = 0.85f; // Placeholder - would need full FFT for accurate calculation
    
    // Speech-like spectral characteristics
    bool spectral_speech_like = (zero_crossings >= handle->config.zcr_threshold_min && 
                                zero_crossings <= handle->config.zcr_threshold_max &&
                                result->low_freq_energy_ratio >= handle->config.low_freq_ratio_threshold);
    
    return spectral_speech_like ? ESP_OK : ESP_FAIL;
}

// Layer 3: Multi-frame consistency checking
static esp_err_t process_consistency_check(enhanced_vad_handle_t handle,
                                         bool energy_decision,
                                         bool spectral_decision,
                                         float current_confidence,
                                         enhanced_vad_result_t *result)
{
    if (!(handle->config.feature_flags & ENHANCED_VAD_ENABLE_CONSISTENCY_CHECK) || 
        !handle->frame_decisions) {
        // No consistency check - use current frame decision
        result->confidence = current_confidence;
        return (energy_decision || spectral_decision) ? ESP_OK : ESP_FAIL;
    }
    
    // Store current frame decision and confidence
    handle->frame_decisions[handle->frame_buffer_index] = (energy_decision || spectral_decision);
    handle->frame_confidences[handle->frame_buffer_index] = current_confidence;
    
    handle->frame_buffer_index = (handle->frame_buffer_index + 1) % handle->config.consistency_frames;
    if (handle->filled_frames < handle->config.consistency_frames) {
        handle->filled_frames++;
    }
    
    // Calculate consistency metrics
    uint8_t positive_frames = 0;
    float avg_confidence = 0;
    
    for (uint8_t i = 0; i < handle->filled_frames; i++) {
        if (handle->frame_decisions[i]) {
            positive_frames++;
        }
        avg_confidence += handle->frame_confidences[i];
    }
    
    if (handle->filled_frames > 0) {
        avg_confidence /= handle->filled_frames;
    }
    
    result->confidence = avg_confidence;
    
    // Decision based on majority vote and confidence threshold
    float consensus_ratio = (float)positive_frames / handle->filled_frames;
    bool consistent_decision = (consensus_ratio >= 0.6f && avg_confidence >= handle->config.confidence_threshold);
    
    result->high_confidence = (avg_confidence >= 0.8f);
    result->detection_quality = (uint8_t)(avg_confidence * 255);
    
    return consistent_decision ? ESP_OK : ESP_FAIL;
}

esp_err_t enhanced_vad_process_audio(enhanced_vad_handle_t handle, 
                                   const int16_t *buffer, 
                                   size_t samples, 
                                   enhanced_vad_result_t *result)
{
    if (!handle || !buffer || !result || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint64_t start_time = esp_timer_get_time();
    uint64_t current_time = start_time;
    
    // Initialize result
    memset(result, 0, sizeof(enhanced_vad_result_t));
    result->frames_processed = ++handle->stats.total_voice_time_ms; // Reuse as frame counter
    
    // Layer 1: Enhanced energy detection
    esp_err_t energy_result = process_energy_detection(handle, buffer, samples, result);
    bool energy_decision = (energy_result == ESP_OK);
    
    // Layer 2: Spectral analysis (if enabled and in appropriate processing mode)
    esp_err_t spectral_result = ESP_FAIL;
    bool spectral_decision = false;
    
    // Skip spectral analysis in high-performance conversation mode to achieve <50ms latency
    bool is_conversation_mode = (handle->config.feature_flags & ENHANCED_VAD_ENABLE_CONVERSATION_AWARE);
    bool skip_spectral_for_performance = is_conversation_mode && 
        (handle->conversation_context == VAD_CONVERSATION_LISTENING || 
         handle->conversation_context == VAD_CONVERSATION_SPEAKING);
    
    if (handle->config.processing_mode <= 1 && !skip_spectral_for_performance) { // Full or optimized mode
        spectral_result = process_spectral_analysis(handle, buffer, samples, result);
        spectral_decision = (spectral_result == ESP_OK);
    }
    
    // Calculate initial confidence based on energy and spectral results
    float current_confidence = 0.0f;
    if (energy_decision) current_confidence += 0.6f;
    if (spectral_decision) current_confidence += 0.4f;
    
    // Layer 3: Multi-frame consistency (optimized for conversation mode)
    esp_err_t final_result;
    bool voice_detected;
    
    // Reduce consistency checking in conversation mode for <50ms latency
    if (skip_spectral_for_performance && energy_decision && current_confidence > 0.5f) {
        // Fast path: trust energy detection in conversation mode with sufficient confidence
        final_result = ESP_OK;
        voice_detected = true;
        result->confidence = current_confidence;
        result->high_confidence = (current_confidence >= 0.6f);
        result->detection_quality = (uint8_t)(current_confidence * 255);
    } else {
        // Normal path: full consistency checking
        final_result = process_consistency_check(handle, energy_decision, spectral_decision, current_confidence, result);
        voice_detected = (final_result == ESP_OK);
    }
    
    // State transition logic
    handle->previous_voice_state = handle->current_voice_state;
    handle->current_voice_state = voice_detected;
    
    // Detect speech start
    if (voice_detected && !handle->previous_voice_state) {
        handle->voice_start_time = current_time;
        handle->silence_start_time = 0;
        result->speech_started = true;
        handle->stats.detection_count++;
        ESP_LOGD(TAG, "Speech started - confidence: %.2f, amp: %d", result->confidence, result->max_amplitude);
    }
    
    // Detect speech end
    if (!voice_detected && handle->previous_voice_state) {
        handle->silence_start_time = current_time;
        
        // Calculate voice duration
        if (handle->voice_start_time > 0) {
            uint32_t voice_duration = (current_time - handle->voice_start_time) / 1000;
            result->voice_duration_ms = voice_duration;
            ESP_LOGD(TAG, "Speech ended - duration: %lu ms, confidence: %.2f", voice_duration, result->confidence);
        }
    }
    
    // Check for definitive speech end
    if (handle->silence_start_time > 0) {
        uint32_t silence_duration = (current_time - handle->silence_start_time) / 1000;
        result->silence_duration_ms = silence_duration;
        
        if (silence_duration >= handle->config.silence_threshold_ms) {
            result->speech_ended = true;
            ESP_LOGD(TAG, "Speech definitively ended after %lu ms silence", silence_duration);
        }
    }
    
    // Update result
    result->voice_detected = voice_detected;
    
    // Update conversation-aware results
    result->conversation_context = handle->conversation_context;
    result->context_adapted_threshold = handle->context_adapted_threshold;
    result->echo_suppression_active = handle->echo_suppression_active;
    
    // Update running voice duration if currently speaking
    if (voice_detected && handle->voice_start_time > 0) {
        result->voice_duration_ms = (current_time - handle->voice_start_time) / 1000;
    }
    
    // Update statistics
    uint64_t processing_time = esp_timer_get_time() - start_time;
    handle->total_processing_time_us += processing_time;
    handle->processing_count++;
    
    if (processing_time > handle->stats.max_processing_time_us) {
        handle->stats.max_processing_time_us = (uint32_t)processing_time;
    }
    
    handle->stats.average_processing_time_us = (uint32_t)(handle->total_processing_time_us / handle->processing_count);
    handle->stats.average_confidence = (handle->stats.average_confidence * (handle->processing_count - 1) + result->confidence) / handle->processing_count;
    handle->stats.current_noise_floor = (uint16_t)handle->current_noise_floor;
    
    // Update noise floor statistics
    if (handle->current_noise_floor < handle->stats.min_noise_floor || handle->stats.min_noise_floor == 0) {
        handle->stats.min_noise_floor = (uint16_t)handle->current_noise_floor;
    }
    if (handle->current_noise_floor > handle->stats.max_noise_floor) {
        handle->stats.max_noise_floor = (uint16_t)handle->current_noise_floor;
    }
    
    handle->last_process_time = current_time;
    
    ESP_LOGV(TAG, "Enhanced VAD processed %zu samples - voice: %s, conf: %.2f, amp: %d, zcr: %d", 
             samples, voice_detected ? "YES" : "NO", result->confidence, result->max_amplitude, result->zero_crossing_rate);
    
    return ESP_OK;
}

esp_err_t enhanced_vad_reset(enhanced_vad_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->current_voice_state = false;
    handle->previous_voice_state = false;
    handle->voice_start_time = 0;
    handle->silence_start_time = 0;
    handle->last_process_time = esp_timer_get_time();
    
    // Reset consistency checking buffers
    if (handle->frame_decisions && handle->frame_confidences) {
        memset(handle->frame_decisions, 0, handle->config.consistency_frames * sizeof(bool));
        memset(handle->frame_confidences, 0, handle->config.consistency_frames * sizeof(float));
        handle->frame_buffer_index = 0;
        handle->filled_frames = 0;
    }
    
    ESP_LOGI(TAG, "Enhanced VAD state reset");
    return ESP_OK;
}

esp_err_t enhanced_vad_get_default_config(uint16_t sample_rate, enhanced_vad_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *config = DEFAULT_CONFIG;
    config->sample_rate = sample_rate;
    
    // Adjust thresholds based on sample rate
    if (sample_rate != 16000) {
        float rate_factor = (float)sample_rate / 16000.0f;
        config->amplitude_threshold = (uint16_t)(config->amplitude_threshold * rate_factor);
    }
    
    return ESP_OK;
}

esp_err_t enhanced_vad_update_config(enhanced_vad_handle_t handle, const enhanced_vad_config_t *config)
{
    if (!handle || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->config = *config;
    ESP_LOGI(TAG, "Enhanced VAD configuration updated - threshold: %d, features: 0x%lx", 
             config->amplitude_threshold, config->feature_flags);
    return ESP_OK;
}

esp_err_t enhanced_vad_get_stats(enhanced_vad_handle_t handle, enhanced_vad_stats_t *stats)
{
    if (!handle || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *stats = handle->stats;
    return ESP_OK;
}

esp_err_t enhanced_vad_set_processing_mode(enhanced_vad_handle_t handle, uint8_t mode)
{
    if (!handle || mode > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->config.processing_mode = mode;
    ESP_LOGI(TAG, "Enhanced VAD processing mode set to: %s", 
             mode == 0 ? "full" : mode == 1 ? "optimized" : "minimal");
    return ESP_OK;
}

esp_err_t enhanced_vad_to_basic_result(const enhanced_vad_result_t *enhanced_result, 
                                     vad_result_t* basic_result)
{
    if (!enhanced_result || !basic_result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Convert enhanced result to basic result for backward compatibility
    basic_result->voice_detected = enhanced_result->voice_detected;
    basic_result->speech_started = enhanced_result->speech_started;
    basic_result->speech_ended = enhanced_result->speech_ended;
    basic_result->max_amplitude = enhanced_result->max_amplitude;
    basic_result->voice_duration_ms = enhanced_result->voice_duration_ms;
    basic_result->silence_duration_ms = enhanced_result->silence_duration_ms;
    
    return ESP_OK;
}

esp_err_t enhanced_vad_set_conversation_context(enhanced_vad_handle_t handle, 
                                              vad_conversation_context_t context)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (handle->conversation_context != context) {
        ESP_LOGI(TAG, "Conversation context changed: %s -> %s",
                handle->conversation_context == VAD_CONVERSATION_IDLE ? "idle" :
                handle->conversation_context == VAD_CONVERSATION_LISTENING ? "listening" :
                handle->conversation_context == VAD_CONVERSATION_SPEAKING ? "speaking" : "processing",
                context == VAD_CONVERSATION_IDLE ? "idle" :
                context == VAD_CONVERSATION_LISTENING ? "listening" :
                context == VAD_CONVERSATION_SPEAKING ? "speaking" : "processing");
        
        handle->conversation_context = context;
        handle->context_change_time = esp_timer_get_time();
        
        // Reset TTS level when leaving speaking state
        if (handle->conversation_context != VAD_CONVERSATION_SPEAKING) {
            handle->current_tts_level = 0.0f;
        }
    }
    
    return ESP_OK;
}

vad_conversation_context_t enhanced_vad_get_conversation_context(enhanced_vad_handle_t handle)
{
    if (!handle) {
        return VAD_CONVERSATION_IDLE;
    }
    
    return handle->conversation_context;
}

esp_err_t enhanced_vad_set_tts_audio_level(enhanced_vad_handle_t handle, 
                                         float tts_level, 
                                         const float *tts_frequency_profile)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Clamp TTS level to valid range
    tts_level = fmaxf(0.0f, fminf(1.0f, tts_level));
    
    if (fabsf(handle->current_tts_level - tts_level) > 0.05f) { // Only log significant changes
        ESP_LOGD(TAG, "TTS audio level updated: %.2f -> %.2f", handle->current_tts_level, tts_level);
    }
    
    handle->current_tts_level = tts_level;
    
    // Automatically set conversation context to speaking when TTS is active
    if (tts_level > 0.1f && handle->conversation_context != VAD_CONVERSATION_SPEAKING) {
        enhanced_vad_set_conversation_context(handle, VAD_CONVERSATION_SPEAKING);
    }
    
    // TODO: Use frequency profile for more sophisticated echo cancellation
    // For now, we use simple energy-based suppression
    (void)tts_frequency_profile; // Suppress unused parameter warning
    
    return ESP_OK;
}

esp_err_t enhanced_vad_get_conversation_config(uint16_t sample_rate, enhanced_vad_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Start with default config
    esp_err_t ret = enhanced_vad_get_default_config(sample_rate, config);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Optimize for conversation flow - more aggressive settings
    config->silence_threshold_ms = 1000;           // Even faster conversation flow for <50ms latency
    config->min_voice_duration_ms = 200;           // Reduced minimum for faster response
    config->confidence_threshold = 0.60f;          // Slightly lower confidence for speed vs accuracy trade-off
    
    // Conversation-aware optimization  
    config->conversation.idle_threshold_multiplier = 75;     // Even higher sensitivity for wake word
    config->conversation.listening_threshold_multiplier = 100; // Normal sensitivity
    config->conversation.speaking_threshold_multiplier = 170; // Even lower sensitivity during TTS
    config->conversation.echo_suppression_db = 18;           // Stronger echo suppression
    config->conversation.tts_fade_time_ms = 150;            // Faster transitions
    
    // Enable conversation-aware processing
    config->feature_flags |= ENHANCED_VAD_ENABLE_CONVERSATION_AWARE;
    
    ESP_LOGI(TAG, "Generated conversation-optimized VAD configuration");
    ESP_LOGI(TAG, "Idle sensitivity: %d%%, Speaking suppression: %d%%, Echo: %ddB", 
            100 * 100 / config->conversation.idle_threshold_multiplier,
            config->conversation.speaking_threshold_multiplier,
            config->conversation.echo_suppression_db);
    
    return ESP_OK;
}