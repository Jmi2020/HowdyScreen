#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief System error types
 */
typedef enum {
    ERROR_TYPE_NONE = 0,
    ERROR_TYPE_WIFI_CONNECTION,
    ERROR_TYPE_SERVER_DISCOVERY,
    ERROR_TYPE_WEBSOCKET_CONNECTION,
    ERROR_TYPE_UDP_STREAMING,
    ERROR_TYPE_AUDIO_PROCESSING,
    ERROR_TYPE_DISPLAY_FAILURE,
    ERROR_TYPE_MEMORY_ERROR,
    ERROR_TYPE_HARDWARE_FAULT
} system_error_type_t;

/**
 * @brief Error recovery strategy
 */
typedef enum {
    RECOVERY_STRATEGY_NONE = 0,
    RECOVERY_STRATEGY_RETRY,
    RECOVERY_STRATEGY_RESTART_COMPONENT,
    RECOVERY_STRATEGY_RESTART_SYSTEM,
    RECOVERY_STRATEGY_SAFE_MODE
} recovery_strategy_t;

/**
 * @brief Error information
 */
typedef struct {
    system_error_type_t type;
    esp_err_t error_code;
    uint32_t timestamp;
    uint32_t occurrence_count;
    const char *component_name;
    const char *description;
    recovery_strategy_t recovery_strategy;
    bool is_critical;
} system_error_info_t;

/**
 * @brief Error recovery configuration
 */
typedef struct {
    uint32_t max_retry_attempts;
    uint32_t retry_delay_ms;
    uint32_t component_restart_threshold;
    uint32_t system_restart_threshold;
    bool enable_safe_mode;
    bool enable_watchdog;
} error_recovery_config_t;

/**
 * @brief Error callback function
 */
typedef void (*error_callback_t)(const system_error_info_t *error_info);

/**
 * @brief Initialize error recovery system
 * 
 * @param config Recovery configuration
 * @param callback Error notification callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t error_recovery_init(const error_recovery_config_t *config, error_callback_t callback);

/**
 * @brief Report a system error
 * 
 * @param type Error type
 * @param error_code ESP error code
 * @param component Component name
 * @param description Error description
 * @return esp_err_t ESP_OK on success
 */
esp_err_t error_recovery_report(system_error_type_t type, esp_err_t error_code,
                               const char *component, const char *description);

/**
 * @brief Check if system is in error state
 * 
 * @return true if system has active errors
 */
bool error_recovery_has_errors(void);

/**
 * @brief Get current error count
 * 
 * @return uint32_t Number of active errors
 */
uint32_t error_recovery_get_error_count(void);

/**
 * @brief Clear all errors
 * 
 * Use when system has been manually recovered
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t error_recovery_clear_errors(void);

/**
 * @brief Force component restart
 * 
 * @param component_name Component to restart
 * @return esp_err_t ESP_OK on success
 */
esp_err_t error_recovery_restart_component(const char *component_name);

/**
 * @brief Get error statistics
 * 
 * @param total_errors Total errors since boot
 * @param critical_errors Critical errors count
 * @param recovery_attempts Recovery attempts made
 * @return esp_err_t ESP_OK on success
 */
esp_err_t error_recovery_get_stats(uint32_t *total_errors, uint32_t *critical_errors, 
                                  uint32_t *recovery_attempts);

#ifdef __cplusplus
}
#endif