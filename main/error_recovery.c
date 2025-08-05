#include "error_recovery.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ErrorRecovery";

#define MAX_ACTIVE_ERRORS 10
#define ERROR_HISTORY_SIZE 50

// Error recovery state
static struct {
    error_recovery_config_t config;
    error_callback_t callback;
    
    // Active errors
    system_error_info_t active_errors[MAX_ACTIVE_ERRORS];
    uint32_t active_error_count;
    
    // Error history
    system_error_info_t error_history[ERROR_HISTORY_SIZE];
    uint32_t history_index;
    
    // Statistics
    uint32_t total_errors;
    uint32_t critical_errors;
    uint32_t recovery_attempts;
    
    // State
    bool is_initialized;
    SemaphoreHandle_t mutex;
    TaskHandle_t recovery_task_handle;
} s_error_recovery = {
    .active_error_count = 0,
    .history_index = 0,
    .total_errors = 0,
    .critical_errors = 0,
    .recovery_attempts = 0,
    .is_initialized = false,
    .mutex = NULL
};

// Forward declarations
static void error_recovery_task(void *pvParameters);
static recovery_strategy_t determine_recovery_strategy(system_error_type_t type, uint32_t occurrence_count);
static esp_err_t execute_recovery_strategy(const system_error_info_t *error_info);
static const char* error_type_to_string(system_error_type_t type);
static const char* recovery_strategy_to_string(recovery_strategy_t strategy);

esp_err_t error_recovery_init(const error_recovery_config_t *config, error_callback_t callback)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_error_recovery.is_initialized) {
        ESP_LOGI(TAG, "Error recovery already initialized");
        return ESP_OK;
    }
    
    // Create mutex
    s_error_recovery.mutex = xSemaphoreCreateMutex();
    if (!s_error_recovery.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Copy configuration
    s_error_recovery.config = *config;
    s_error_recovery.callback = callback;
    
    // Create recovery task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        error_recovery_task,
        "error_recovery",
        4096,
        NULL,
        10,  // Medium priority
        &s_error_recovery.recovery_task_handle,
        0   // Core 0
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create recovery task");
        vSemaphoreDelete(s_error_recovery.mutex);
        return ESP_FAIL;
    }
    
    s_error_recovery.is_initialized = true;
    
    ESP_LOGI(TAG, "Error recovery initialized");
    ESP_LOGI(TAG, "Config - Max retries: %lu, Retry delay: %lums, Watchdog: %s",
             config->max_retry_attempts, config->retry_delay_ms,
             config->enable_watchdog ? "ON" : "OFF");
    
    return ESP_OK;
}

esp_err_t error_recovery_report(system_error_type_t type, esp_err_t error_code,
                               const char *component, const char *description)
{
    if (!s_error_recovery.is_initialized) {
        ESP_LOGE(TAG, "Error recovery not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_error_recovery.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for error reporting");
        return ESP_ERR_TIMEOUT;
    }
    
    // Check if this error type already exists
    system_error_info_t *existing_error = NULL;
    for (uint32_t i = 0; i < s_error_recovery.active_error_count; i++) {
        if (s_error_recovery.active_errors[i].type == type &&
            strcmp(s_error_recovery.active_errors[i].component_name, component ? component : "") == 0) {
            existing_error = &s_error_recovery.active_errors[i];
            break;
        }
    }
    
    if (existing_error) {
        // Update existing error
        existing_error->occurrence_count++;
        existing_error->timestamp = esp_timer_get_time() / 1000;
        existing_error->error_code = error_code;
        
        ESP_LOGW(TAG, "Error repeated: %s in %s (count: %lu)",
                 error_type_to_string(type), component ? component : "unknown",
                 existing_error->occurrence_count);
    } else {
        // Add new error if space available
        if (s_error_recovery.active_error_count < MAX_ACTIVE_ERRORS) {
            system_error_info_t *new_error = &s_error_recovery.active_errors[s_error_recovery.active_error_count];
            
            new_error->type = type;
            new_error->error_code = error_code;
            new_error->timestamp = esp_timer_get_time() / 1000;
            new_error->occurrence_count = 1;
            new_error->component_name = component ? component : "unknown";
            new_error->description = description ? description : "No description";
            new_error->recovery_strategy = determine_recovery_strategy(type, 1);
            new_error->is_critical = (type == ERROR_TYPE_HARDWARE_FAULT || 
                                    type == ERROR_TYPE_MEMORY_ERROR);
            
            s_error_recovery.active_error_count++;
            
            ESP_LOGE(TAG, "New error: %s in %s - %s (strategy: %s)",
                     error_type_to_string(type), new_error->component_name,
                     new_error->description, recovery_strategy_to_string(new_error->recovery_strategy));
        } else {
            ESP_LOGE(TAG, "Error buffer full - cannot add new error");
        }
    }
    
    // Add to history
    system_error_info_t *history_entry = &s_error_recovery.error_history[s_error_recovery.history_index];
    history_entry->type = type;
    history_entry->error_code = error_code;
    history_entry->timestamp = esp_timer_get_time() / 1000;
    history_entry->occurrence_count = existing_error ? existing_error->occurrence_count : 1;
    history_entry->component_name = component ? component : "unknown";
    history_entry->description = description ? description : "No description";
    
    s_error_recovery.history_index = (s_error_recovery.history_index + 1) % ERROR_HISTORY_SIZE;
    
    // Update statistics
    s_error_recovery.total_errors++;
    if (type == ERROR_TYPE_HARDWARE_FAULT || type == ERROR_TYPE_MEMORY_ERROR) {
        s_error_recovery.critical_errors++;
    }
    
    // Notify callback
    if (s_error_recovery.callback && existing_error) {
        s_error_recovery.callback(existing_error);
    } else if (s_error_recovery.callback && s_error_recovery.active_error_count > 0) {
        s_error_recovery.callback(&s_error_recovery.active_errors[s_error_recovery.active_error_count - 1]);
    }
    
    xSemaphoreGive(s_error_recovery.mutex);
    
    // Wake up recovery task
    if (s_error_recovery.recovery_task_handle) {
        xTaskNotifyGive(s_error_recovery.recovery_task_handle);
    }
    
    return ESP_OK;
}

static void error_recovery_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Error recovery task started");
    
    while (true) {
        // Wait for error notification or timeout
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
        
        if (xSemaphoreTake(s_error_recovery.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Process active errors
            for (uint32_t i = 0; i < s_error_recovery.active_error_count; i++) {
                system_error_info_t *error = &s_error_recovery.active_errors[i];
                
                // Check if error needs recovery action
                uint32_t time_since_error = (esp_timer_get_time() / 1000) - error->timestamp;
                
                if (time_since_error > s_error_recovery.config.retry_delay_ms &&
                    error->recovery_strategy != RECOVERY_STRATEGY_NONE) {
                    
                    ESP_LOGI(TAG, "Attempting recovery for %s error in %s",
                             error_type_to_string(error->type), error->component_name);
                    
                    esp_err_t recovery_result = execute_recovery_strategy(error);
                    s_error_recovery.recovery_attempts++;
                    
                    if (recovery_result == ESP_OK) {
                        ESP_LOGI(TAG, "Recovery successful for %s", error->component_name);
                        
                        // Remove error from active list
                        for (uint32_t j = i; j < s_error_recovery.active_error_count - 1; j++) {
                            s_error_recovery.active_errors[j] = s_error_recovery.active_errors[j + 1];
                        }
                        s_error_recovery.active_error_count--;
                        i--; // Adjust index since we removed an item
                    } else {
                        ESP_LOGW(TAG, "Recovery failed for %s: %s", 
                                error->component_name, esp_err_to_name(recovery_result));
                        
                        // Update recovery strategy based on failure
                        if (error->occurrence_count >= s_error_recovery.config.max_retry_attempts) {
                            error->recovery_strategy = determine_recovery_strategy(error->type, 
                                                                                 error->occurrence_count);
                        }
                    }
                }
            }
            
            xSemaphoreGive(s_error_recovery.mutex);
        }
    }
}

static recovery_strategy_t determine_recovery_strategy(system_error_type_t type, uint32_t occurrence_count)
{
    switch (type) {
        case ERROR_TYPE_WIFI_CONNECTION:
            if (occurrence_count < 3) return RECOVERY_STRATEGY_RETRY;
            else if (occurrence_count < 5) return RECOVERY_STRATEGY_RESTART_COMPONENT;
            else return RECOVERY_STRATEGY_RESTART_SYSTEM;
            
        case ERROR_TYPE_SERVER_DISCOVERY:
        case ERROR_TYPE_WEBSOCKET_CONNECTION:
        case ERROR_TYPE_UDP_STREAMING:
            if (occurrence_count < 5) return RECOVERY_STRATEGY_RETRY;
            else return RECOVERY_STRATEGY_RESTART_COMPONENT;
            
        case ERROR_TYPE_AUDIO_PROCESSING:
            if (occurrence_count < 3) return RECOVERY_STRATEGY_RETRY;
            else return RECOVERY_STRATEGY_RESTART_COMPONENT;
            
        case ERROR_TYPE_DISPLAY_FAILURE:
            if (occurrence_count < 2) return RECOVERY_STRATEGY_RESTART_COMPONENT;
            else return RECOVERY_STRATEGY_SAFE_MODE;
            
        case ERROR_TYPE_MEMORY_ERROR:
        case ERROR_TYPE_HARDWARE_FAULT:
            return RECOVERY_STRATEGY_RESTART_SYSTEM;
            
        default:
            return RECOVERY_STRATEGY_RETRY;
    }
}

static esp_err_t execute_recovery_strategy(const system_error_info_t *error_info)
{
    switch (error_info->recovery_strategy) {
        case RECOVERY_STRATEGY_RETRY:
            ESP_LOGI(TAG, "Retrying operation for %s", error_info->component_name);
            // Component should handle retry internally
            return ESP_OK;
            
        case RECOVERY_STRATEGY_RESTART_COMPONENT:
            ESP_LOGI(TAG, "Restarting component: %s", error_info->component_name);
            return error_recovery_restart_component(error_info->component_name);
            
        case RECOVERY_STRATEGY_RESTART_SYSTEM:
            ESP_LOGW(TAG, "System restart required due to critical error");
            vTaskDelay(pdMS_TO_TICKS(1000)); // Give time for logging
            esp_restart();
            return ESP_OK;
            
        case RECOVERY_STRATEGY_SAFE_MODE:
            ESP_LOGW(TAG, "Entering safe mode due to persistent errors");
            // Disable non-essential components
            return ESP_OK;
            
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t error_recovery_restart_component(const char *component_name)
{
    if (!component_name) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Attempting to restart component: %s", component_name);
    
    // Component-specific restart logic
    if (strcmp(component_name, "wifi") == 0) {
        // Restart WiFi
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_start();
    } else if (strcmp(component_name, "websocket") == 0) {
        // Restart WebSocket (would need to be implemented in websocket_client)
        ESP_LOGI(TAG, "WebSocket restart not implemented yet");
    } else if (strcmp(component_name, "udp_audio") == 0) {
        // Restart UDP audio (would need to be implemented)
        ESP_LOGI(TAG, "UDP audio restart not implemented yet");
    } else {
        ESP_LOGW(TAG, "Unknown component for restart: %s", component_name);
        return ESP_ERR_NOT_FOUND;
    }
    
    return ESP_OK;
}

bool error_recovery_has_errors(void)
{
    return s_error_recovery.active_error_count > 0;
}

uint32_t error_recovery_get_error_count(void)
{
    return s_error_recovery.active_error_count;
}

esp_err_t error_recovery_clear_errors(void)
{
    if (xSemaphoreTake(s_error_recovery.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    s_error_recovery.active_error_count = 0;
    ESP_LOGI(TAG, "All active errors cleared");
    
    xSemaphoreGive(s_error_recovery.mutex);
    return ESP_OK;
}

esp_err_t error_recovery_get_stats(uint32_t *total_errors, uint32_t *critical_errors, 
                                  uint32_t *recovery_attempts)
{
    if (total_errors) *total_errors = s_error_recovery.total_errors;
    if (critical_errors) *critical_errors = s_error_recovery.critical_errors;
    if (recovery_attempts) *recovery_attempts = s_error_recovery.recovery_attempts;
    
    return ESP_OK;
}

static const char* error_type_to_string(system_error_type_t type)
{
    switch (type) {
        case ERROR_TYPE_NONE: return "NONE";
        case ERROR_TYPE_WIFI_CONNECTION: return "WIFI_CONNECTION";
        case ERROR_TYPE_SERVER_DISCOVERY: return "SERVER_DISCOVERY";
        case ERROR_TYPE_WEBSOCKET_CONNECTION: return "WEBSOCKET_CONNECTION";
        case ERROR_TYPE_UDP_STREAMING: return "UDP_STREAMING";
        case ERROR_TYPE_AUDIO_PROCESSING: return "AUDIO_PROCESSING";
        case ERROR_TYPE_DISPLAY_FAILURE: return "DISPLAY_FAILURE";
        case ERROR_TYPE_MEMORY_ERROR: return "MEMORY_ERROR";
        case ERROR_TYPE_HARDWARE_FAULT: return "HARDWARE_FAULT";
        default: return "UNKNOWN";
    }
}

static const char* recovery_strategy_to_string(recovery_strategy_t strategy)
{
    switch (strategy) {
        case RECOVERY_STRATEGY_NONE: return "NONE";
        case RECOVERY_STRATEGY_RETRY: return "RETRY";
        case RECOVERY_STRATEGY_RESTART_COMPONENT: return "RESTART_COMPONENT";
        case RECOVERY_STRATEGY_RESTART_SYSTEM: return "RESTART_SYSTEM";
        case RECOVERY_STRATEGY_SAFE_MODE: return "SAFE_MODE";
        default: return "UNKNOWN";
    }
}