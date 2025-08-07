/**
 * @file audio_stream_test.c
 * @brief Simple audio streaming test for ESP32-P4 to HowdyTTS server
 * 
 * This test bypasses the HowdyTTS connection logic and directly tests
 * audio capture, processing, and UDP transmission to verify the audio
 * pipeline is working correctly.
 */

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "audio_processor.h"

static const char *TAG = "AudioStreamTest";

// Test configuration
#define TEST_SERVER_IP "192.168.86.39"  // From logs - HowdyTTS server IP
#define TEST_SERVER_PORT 8000
#define TEST_DURATION_SECONDS 10
#define SAMPLES_PER_PACKET 320  // 20ms at 16kHz
#define PACKET_INTERVAL_MS 20

typedef struct {
    int socket_fd;
    struct sockaddr_in server_addr;
    bool running;
    uint32_t packets_sent;
    uint32_t total_samples;
} audio_test_state_t;

static audio_test_state_t test_state = {0};

// Simple PCM audio packet structure (matches ESP32-P4 protocol)
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint16_t sample_rate;
    uint16_t sample_count;
    int16_t audio_data[SAMPLES_PER_PACKET];
} test_audio_packet_t;

/**
 * Audio event callback for test - handles audio processor events
 */
static void test_audio_event_callback(audio_event_t event, void *data, size_t len)
{
    if (event == AUDIO_EVENT_DATA_READY && test_state.running && data && len > 0) {
        // Calculate number of samples (16-bit samples)
        size_t samples = len / sizeof(int16_t);
        int16_t *audio_data = (int16_t *)data;
        
        // Process in chunks of SAMPLES_PER_PACKET
        for (size_t i = 0; i < samples; i += SAMPLES_PER_PACKET) {
            size_t chunk_samples = (i + SAMPLES_PER_PACKET <= samples) ? SAMPLES_PER_PACKET : (samples - i);
            
            // Create test packet
            test_audio_packet_t packet = {
                .timestamp_ms = esp_timer_get_time() / 1000,
                .sample_rate = 16000,
                .sample_count = chunk_samples
            };
            
            // Copy audio data chunk
            memcpy(packet.audio_data, &audio_data[i], chunk_samples * sizeof(int16_t));
            
            // Send UDP packet
            ssize_t sent = sendto(test_state.socket_fd, &packet, sizeof(packet), 0,
                                 (struct sockaddr*)&test_state.server_addr, sizeof(test_state.server_addr));
            
            if (sent > 0) {
                test_state.packets_sent++;
                test_state.total_samples += chunk_samples;
                
                // Log every 50 packets (1 second)
                if (test_state.packets_sent % 50 == 0) {
                    ESP_LOGI(TAG, "📤 Sent %d packets (%d samples) to %s:%d", 
                            test_state.packets_sent, test_state.total_samples,
                            TEST_SERVER_IP, TEST_SERVER_PORT);
                }
            } else {
                ESP_LOGE(TAG, "❌ Failed to send UDP packet: errno %d", errno);
                break;
            }
        }
    }
}

/**
 * Initialize test UDP socket
 */
static esp_err_t init_test_socket(void)
{
    // Create UDP socket
    test_state.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (test_state.socket_fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return ESP_FAIL;
    }
    
    // Configure server address
    memset(&test_state.server_addr, 0, sizeof(test_state.server_addr));
    test_state.server_addr.sin_family = AF_INET;
    test_state.server_addr.sin_port = htons(TEST_SERVER_PORT);
    
    if (inet_pton(AF_INET, TEST_SERVER_IP, &test_state.server_addr.sin_addr) <= 0) {
        ESP_LOGE(TAG, "Invalid server IP address: %s", TEST_SERVER_IP);
        close(test_state.socket_fd);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ UDP socket initialized for %s:%d", TEST_SERVER_IP, TEST_SERVER_PORT);
    return ESP_OK;
}

/**
 * Run audio streaming test
 */
esp_err_t run_audio_stream_test(void)
{
    ESP_LOGI(TAG, "🧪 Starting Audio Stream Test");
    ESP_LOGI(TAG, "Target: %s:%d", TEST_SERVER_IP, TEST_SERVER_PORT);
    ESP_LOGI(TAG, "Duration: %d seconds", TEST_DURATION_SECONDS);
    ESP_LOGI(TAG, "Format: 16kHz, 16-bit, mono, %dms packets", PACKET_INTERVAL_MS);
    
    // Initialize UDP socket
    if (init_test_socket() != ESP_OK) {
        return ESP_FAIL;
    }
    
    // Initialize audio processor if not already done
    audio_processor_config_t audio_config = {
        .sample_rate = 16000,
        .bits_per_sample = 16,
        .channels = 1,
        .dma_buf_count = 2,
        .dma_buf_len = 512,
        .task_priority = 10,
        .task_core = 1
    };
    
    esp_err_t ret = audio_processor_init(&audio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Audio processor init failed: %s", esp_err_to_name(ret));
        close(test_state.socket_fd);
        return ret;
    }
    
    // Reset test state
    test_state.running = true;
    test_state.packets_sent = 0;
    test_state.total_samples = 0;
    
    // Set audio event callback
    ret = audio_processor_set_callback(test_audio_event_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Audio processor callback setup failed: %s", esp_err_to_name(ret));
        close(test_state.socket_fd);
        return ret;
    }
    
    // Start audio capture
    ret = audio_processor_start_capture();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Audio processor start failed: %s", esp_err_to_name(ret));
        test_state.running = false;
        close(test_state.socket_fd);
        return ret;
    }
    
    ESP_LOGI(TAG, "🎤 Audio streaming started - recording for %d seconds...", TEST_DURATION_SECONDS);
    
    // Run test for specified duration - actively get audio data  
    for (int i = 0; i < TEST_DURATION_SECONDS; i++) {
        // Try to get audio data directly from the audio processor
        for (int j = 0; j < 50; j++) { // 50 * 20ms = 1 second
            uint8_t *buffer = NULL;
            size_t length = 0;
            
            esp_err_t get_ret = audio_processor_get_buffer(&buffer, &length);
            if (get_ret == ESP_OK && buffer && length > 0) {
                // Convert buffer to samples
                size_t samples = length / sizeof(int16_t);
                int16_t *audio_samples = (int16_t*)buffer;
                
                // Process in chunks for UDP transmission
                if (samples >= SAMPLES_PER_PACKET) {
                    // Create test packet
                    test_audio_packet_t packet = {
                        .timestamp_ms = esp_timer_get_time() / 1000,
                        .sample_rate = 16000,
                        .sample_count = SAMPLES_PER_PACKET
                    };
                    
                    // Copy first chunk of samples
                    memcpy(packet.audio_data, audio_samples, SAMPLES_PER_PACKET * sizeof(int16_t));
                    
                    // Send UDP packet
                    ssize_t sent = sendto(test_state.socket_fd, &packet, sizeof(packet), 0,
                                         (struct sockaddr*)&test_state.server_addr, sizeof(test_state.server_addr));
                    
                    if (sent > 0) {
                        test_state.packets_sent++;
                        test_state.total_samples += SAMPLES_PER_PACKET;
                        
                        // Log every 50 packets (1 second)
                        if (test_state.packets_sent % 50 == 0) {
                            ESP_LOGI(TAG, "📤 Sent %d packets (%d samples) to %s:%d", 
                                    test_state.packets_sent, test_state.total_samples,
                                    TEST_SERVER_IP, TEST_SERVER_PORT);
                        }
                    }
                }
                
                // Release the buffer
                audio_processor_release_buffer();
            }
            
            vTaskDelay(pdMS_TO_TICKS(20)); // 20ms delay for next packet
        }
        
        ESP_LOGI(TAG, "⏱️  Test progress: %d/%d seconds", i + 1, TEST_DURATION_SECONDS);
    }
    
    // Stop audio capture
    test_state.running = false;
    audio_processor_stop_capture();
    close(test_state.socket_fd);
    
    // Report results
    ESP_LOGI(TAG, "🏁 Audio Stream Test Complete");
    ESP_LOGI(TAG, "📊 Results:");
    ESP_LOGI(TAG, "   Packets sent: %d", test_state.packets_sent);
    ESP_LOGI(TAG, "   Total samples: %d", test_state.total_samples);
    ESP_LOGI(TAG, "   Expected packets: %d", (TEST_DURATION_SECONDS * 1000) / PACKET_INTERVAL_MS);
    ESP_LOGI(TAG, "   Success rate: %.1f%%", 
            (float)test_state.packets_sent / ((TEST_DURATION_SECONDS * 1000) / PACKET_INTERVAL_MS) * 100);
    
    if (test_state.packets_sent > 0) {
        ESP_LOGI(TAG, "✅ Audio streaming test PASSED");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "❌ Audio streaming test FAILED - no packets sent");
        return ESP_FAIL;
    }
}