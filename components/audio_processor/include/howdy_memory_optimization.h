/**
 * @file howdy_memory_optimization.h
 * @brief ESP32-P4 Memory Optimization for Dual Protocol HowdyTTS Integration
 * 
 * MEMORY ARCHITECTURE: ESP32-P4 Dual-Core RISC-V + PSRAM Optimization
 * 
 * This module provides comprehensive memory management optimizations specifically 
 * designed for ESP32-P4 dual-core architecture with HowdyTTS dual protocol support.
 * 
 * Key Optimizations:
 * 1. PSRAM Integration - Optimal use of external PSRAM for audio buffers
 * 2. Dual-Core Memory Management - Core-specific allocation strategies  
 * 3. Audio Buffer Optimization - Ring buffers optimized for real-time streaming
 * 4. Protocol Buffer Management - Efficient WebSocket + UDP buffer handling
 * 5. OPUS Codec Memory - Optimized memory usage for OPUS encoding/decoding
 * 6. UI Memory Management - LVGL framebuffer optimization for 800x800 round display
 * 7. Network Buffer Optimization - Efficient packet handling for dual protocols
 * 8. Memory Pool Management - Pre-allocated pools for zero-allocation real-time paths
 * 
 * ESP32-P4 Memory Layout:
 * - Core 0: UI, Network Management, Protocol Coordination
 * - Core 1: Audio Processing, Real-time Streaming, OPUS Encoding
 * - PSRAM: Large audio buffers, UI framebuffers, Network buffers
 * - Internal RAM: Critical real-time data, ISR handlers, DMA descriptors
 */

#pragma once

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// MEMORY CONFIGURATION FOR ESP32-P4 DUAL PROTOCOL
//=============================================================================

// ESP32-P4 Memory Capabilities
#define HOWDY_HEAP_CAPS_INTERNAL        (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define HOWDY_HEAP_CAPS_PSRAM          (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define HOWDY_HEAP_CAPS_DMA            (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)
#define HOWDY_HEAP_CAPS_32BIT          (MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL)

// Audio Memory Allocation Strategy
#define HOWDY_AUDIO_BUFFER_SIZE         (16 * 1024)    // 16KB audio ring buffer
#define HOWDY_OPUS_BUFFER_SIZE          (8 * 1024)     // 8KB OPUS working buffer
#define HOWDY_AUDIO_POOL_COUNT          8              // 8 pre-allocated audio buffers
#define HOWDY_AUDIO_FRAME_SIZE          640            // 20ms at 16kHz mono (320 samples * 2 bytes)

// Network Memory Configuration
#define HOWDY_UDP_BUFFER_SIZE           2048           // UDP packet buffer
#define HOWDY_WEBSOCKET_BUFFER_SIZE     4096           // WebSocket message buffer
#define HOWDY_HTTP_BUFFER_SIZE          1024           // HTTP response buffer
#define HOWDY_NETWORK_POOL_COUNT        4              // 4 pre-allocated network buffers

// UI Memory Configuration (800x800 Round Display)
#define HOWDY_DISPLAY_WIDTH             800
#define HOWDY_DISPLAY_HEIGHT            800
#define HOWDY_DISPLAY_BUFFER_SIZE       (HOWDY_DISPLAY_WIDTH * HOWDY_DISPLAY_HEIGHT * 2) // 16-bit color
#define HOWDY_FRAMEBUFFER_COUNT         2              // Double buffering
#define HOWDY_UI_OBJECT_POOL_COUNT      20             // Pre-allocated LVGL objects

// Memory Pool Types
typedef enum {
    HOWDY_POOL_AUDIO_FRAMES = 0,       // Audio frame buffers
    HOWDY_POOL_NETWORK_PACKETS,        // Network packet buffers  
    HOWDY_POOL_OPUS_WORK,              // OPUS encoder/decoder working buffers
    HOWDY_POOL_UI_OBJECTS,             // LVGL object pools
    HOWDY_POOL_PROTOCOL_MESSAGES,      // Protocol message buffers
    HOWDY_POOL_COUNT                   // Total number of pool types
} howdy_memory_pool_type_t;

// Memory allocation statistics
typedef struct {
    size_t total_allocated;            // Total memory allocated
    size_t psram_allocated;            // PSRAM memory allocated
    size_t internal_allocated;         // Internal RAM allocated
    size_t dma_allocated;              // DMA-capable memory allocated
    size_t peak_allocated;             // Peak memory usage
    uint32_t allocation_count;         // Number of allocations
    uint32_t pool_hits;                // Pool allocation hits
    uint32_t pool_misses;              // Pool allocation misses
    uint32_t fragmentation_level;      // Fragmentation percentage
} howdy_memory_stats_t;

//=============================================================================
// CORE-SPECIFIC MEMORY MANAGEMENT
//=============================================================================

/**
 * @brief Initialize memory management system for dual-core operation
 * 
 * Sets up optimized memory pools and allocation strategies for ESP32-P4
 * 
 * @param enable_psram Use external PSRAM for large buffers
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_memory_init(bool enable_psram);

/**
 * @brief Allocate memory optimized for specific core usage
 * 
 * @param size Memory size to allocate
 * @param core_id Target core ID (0 or 1)
 * @param caps Memory capabilities (internal, PSRAM, DMA, etc.)
 * @return void* Allocated memory pointer or NULL
 */
void* howdy_memory_alloc_for_core(size_t size, uint8_t core_id, uint32_t caps);

/**
 * @brief Free memory allocated with howdy_memory_alloc_for_core
 * 
 * @param ptr Memory pointer to free
 */
void howdy_memory_free(void* ptr);

//=============================================================================
// AUDIO MEMORY OPTIMIZATION
//=============================================================================

/**
 * @brief Allocate audio buffer optimized for real-time processing
 * 
 * Allocates from pre-allocated pool for zero-latency operation
 * 
 * @param frame_count Number of audio frames (each frame = 320 samples)
 * @return void* Audio buffer pointer or NULL
 */
void* howdy_audio_buffer_alloc(uint8_t frame_count);

/**
 * @brief Return audio buffer to pool
 * 
 * @param buffer Audio buffer to return
 */
void howdy_audio_buffer_free(void* buffer);

/**
 * @brief Create optimized audio ring buffer for dual protocol streaming
 * 
 * @param buffer_size Ring buffer size in bytes
 * @param use_psram Allocate buffer in PSRAM
 * @return RingbufHandle_t Ring buffer handle or NULL
 */
RingbufHandle_t howdy_create_audio_ringbuffer(size_t buffer_size, bool use_psram);

/**
 * @brief Allocate OPUS encoder/decoder working buffer
 * 
 * @param encoder True for encoder, false for decoder
 * @return void* OPUS working buffer or NULL
 */
void* howdy_opus_buffer_alloc(bool encoder);

/**
 * @brief Free OPUS working buffer
 * 
 * @param buffer OPUS buffer to free
 * @param encoder True if encoder buffer, false if decoder buffer
 */
void howdy_opus_buffer_free(void* buffer, bool encoder);

//=============================================================================
// NETWORK MEMORY OPTIMIZATION
//=============================================================================

/**
 * @brief Allocate network packet buffer for dual protocol operation
 * 
 * @param protocol_type 0=UDP, 1=WebSocket, 2=HTTP
 * @return void* Network buffer or NULL
 */
void* howdy_network_buffer_alloc(uint8_t protocol_type);

/**
 * @brief Free network packet buffer
 * 
 * @param buffer Network buffer to free
 * @param protocol_type Protocol type used for allocation
 */
void howdy_network_buffer_free(void* buffer, uint8_t protocol_type);

/**
 * @brief Pre-allocate network buffers for zero-latency packet handling
 * 
 * @param udp_count Number of UDP buffers to pre-allocate
 * @param websocket_count Number of WebSocket buffers to pre-allocate
 * @param http_count Number of HTTP buffers to pre-allocate
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_preallocate_network_buffers(uint8_t udp_count, uint8_t websocket_count, uint8_t http_count);

//=============================================================================
// UI MEMORY OPTIMIZATION FOR 800x800 ROUND DISPLAY
//=============================================================================

/**
 * @brief Allocate optimized framebuffer for 800x800 round display
 * 
 * @param buffer_count Number of framebuffers (1=single, 2=double buffering)
 * @return void** Array of framebuffer pointers or NULL
 */
void** howdy_ui_framebuffer_alloc(uint8_t buffer_count);

/**
 * @brief Free UI framebuffers
 * 
 * @param framebuffers Array of framebuffer pointers
 * @param buffer_count Number of buffers to free
 */
void howdy_ui_framebuffer_free(void** framebuffers, uint8_t buffer_count);

/**
 * @brief Allocate LVGL object from pre-allocated pool
 * 
 * @param object_type LVGL object type identifier
 * @param size Object size in bytes
 * @return void* LVGL object pointer or NULL
 */
void* howdy_ui_object_alloc(uint16_t object_type, size_t size);

/**
 * @brief Return LVGL object to pool
 * 
 * @param object Object pointer to return
 * @param object_type Object type used for allocation
 */
void howdy_ui_object_free(void* object, uint16_t object_type);

//=============================================================================
// MEMORY POOL MANAGEMENT
//=============================================================================

/**
 * @brief Create memory pool for specific buffer type
 * 
 * @param pool_type Type of memory pool to create
 * @param buffer_size Size of each buffer in pool
 * @param buffer_count Number of buffers in pool
 * @param use_psram Allocate pool in PSRAM
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_memory_pool_create(howdy_memory_pool_type_t pool_type, size_t buffer_size, 
                                  uint8_t buffer_count, bool use_psram);

/**
 * @brief Allocate buffer from memory pool
 * 
 * @param pool_type Pool type to allocate from
 * @param timeout_ms Allocation timeout (0 = no wait, portMAX_DELAY = wait forever)
 * @return void* Buffer pointer or NULL if timeout/pool empty
 */
void* howdy_memory_pool_alloc(howdy_memory_pool_type_t pool_type, TickType_t timeout_ms);

/**
 * @brief Return buffer to memory pool
 * 
 * @param pool_type Pool type to return to
 * @param buffer Buffer pointer to return
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_memory_pool_free(howdy_memory_pool_type_t pool_type, void* buffer);

/**
 * @brief Get memory pool statistics
 * 
 * @param pool_type Pool type to query
 * @param total_buffers Total buffers in pool
 * @param available_buffers Available buffers in pool  
 * @param peak_usage Peak usage count
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_memory_pool_stats(howdy_memory_pool_type_t pool_type, uint8_t* total_buffers,
                                 uint8_t* available_buffers, uint8_t* peak_usage);

//=============================================================================
// MEMORY MONITORING AND OPTIMIZATION
//=============================================================================

/**
 * @brief Get comprehensive memory statistics
 * 
 * @param stats Output statistics structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_memory_get_stats(howdy_memory_stats_t* stats);

/**
 * @brief Optimize memory layout for current usage patterns
 * 
 * Analyzes allocation patterns and optimizes pool sizes and locations
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_memory_optimize_layout(void);

/**
 * @brief Check for memory leaks and fragmentation
 * 
 * @param report_fragmentation Log fragmentation details
 * @param report_leaks Log potential memory leaks
 * @return esp_err_t ESP_OK if no issues found
 */
esp_err_t howdy_memory_health_check(bool report_fragmentation, bool report_leaks);

/**
 * @brief Force garbage collection and memory defragmentation
 * 
 * @param aggressive_mode Perform more thorough but slower cleanup
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_memory_garbage_collect(bool aggressive_mode);

/**
 * @brief Set memory usage warning thresholds
 * 
 * @param warning_threshold_percent Warn when memory usage exceeds this percentage
 * @param critical_threshold_percent Critical alert threshold
 * @param callback Callback function for threshold alerts
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_memory_set_thresholds(uint8_t warning_threshold_percent, 
                                     uint8_t critical_threshold_percent,
                                     void (*callback)(uint8_t usage_percent, bool critical));

//=============================================================================
// UTILITY FUNCTIONS
//=============================================================================

/**
 * @brief Get optimal buffer size for current network conditions
 * 
 * @param protocol_type 0=UDP, 1=WebSocket
 * @param network_quality Network quality score (0-100)
 * @return size_t Optimal buffer size in bytes
 */
size_t howdy_memory_get_optimal_buffer_size(uint8_t protocol_type, uint8_t network_quality);

/**
 * @brief Check if sufficient memory is available for operation
 * 
 * @param required_bytes Required memory in bytes
 * @param preferred_caps Preferred memory capabilities
 * @return bool True if sufficient memory available
 */
bool howdy_memory_check_availability(size_t required_bytes, uint32_t preferred_caps);

/**
 * @brief Get memory allocation recommendation for specific use case
 * 
 * @param use_case Use case identifier ("audio", "network", "ui", etc.)
 * @param data_size Data size to allocate for
 * @param recommended_caps Output recommended capabilities
 * @param recommended_core Output recommended core ID
 * @return size_t Recommended allocation size
 */
size_t howdy_memory_get_recommendation(const char* use_case, size_t data_size, 
                                      uint32_t* recommended_caps, uint8_t* recommended_core);

#ifdef __cplusplus
}
#endif