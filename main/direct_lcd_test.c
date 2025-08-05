/*
 * Direct LCD Test for ESP32-P4 with JD9365
 * Based on working demo code - bypasses BSP for direct control
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_dma_utils.h"
#include "esp_lcd_jd9365.h"

static const char *TAG = "DirectLCDTest";

// Display parameters from working demo
#define TEST_LCD_H_RES (800)
#define TEST_LCD_V_RES (800)
#define TEST_LCD_BIT_PER_PIXEL (24)
#define TEST_PIN_NUM_LCD_RST (27)
#define TEST_PIN_NUM_BK_LIGHT (26)
#define TEST_LCD_BK_LIGHT_ON_LEVEL (0)  // ACTIVE LOW!
#define TEST_LCD_BK_LIGHT_OFF_LEVEL (1)
#define TEST_MIPI_DSI_LANE_NUM (2)
#define TEST_MIPI_DSI_PHY_PWR_LDO_CHAN (3)
#define TEST_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)
#define TEST_MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB888)

// LCD initialization commands from working demo
static const jd9365_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0xE1, (uint8_t[]){0x93}, 1, 0},
    {0xE2, (uint8_t[]){0x65}, 1, 0},
    {0xE3, (uint8_t[]){0xF8}, 1, 0},
    {0x80, (uint8_t[]){0x01}, 1, 0},
    {0xE0, (uint8_t[]){0x01}, 1, 0},
    {0x00, (uint8_t[]){0x00}, 1, 0},
    {0x01, (uint8_t[]){0x41}, 1, 0},
    {0x03, (uint8_t[]){0x10}, 1, 0},
    {0x04, (uint8_t[]){0x44}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x18, (uint8_t[]){0xD0}, 1, 0},
    {0x19, (uint8_t[]){0x00}, 1, 0},
    {0x1A, (uint8_t[]){0x00}, 1, 0},
    {0x1B, (uint8_t[]){0xD0}, 1, 0},
    {0x1C, (uint8_t[]){0x00}, 1, 0},
    {0x24, (uint8_t[]){0xFE}, 1, 0},
    {0x35, (uint8_t[]){0x26}, 1, 0},
    {0x37, (uint8_t[]){0x09}, 1, 0},
    {0x38, (uint8_t[]){0x04}, 1, 0},
    {0x39, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x0A}, 1, 0},
    {0x3C, (uint8_t[]){0x78}, 1, 0},
    {0x3D, (uint8_t[]){0xFF}, 1, 0},
    {0x3E, (uint8_t[]){0xFF}, 1, 0},
    {0x3F, (uint8_t[]){0xFF}, 1, 0},
    {0x40, (uint8_t[]){0x00}, 1, 0},
    {0x41, (uint8_t[]){0x64}, 1, 0},
    {0x42, (uint8_t[]){0xC7}, 1, 0},
    {0x43, (uint8_t[]){0x18}, 1, 0},
    {0x44, (uint8_t[]){0x0B}, 1, 0},
    {0x45, (uint8_t[]){0x14}, 1, 0},
    {0x55, (uint8_t[]){0x02}, 1, 0},
    {0x57, (uint8_t[]){0x49}, 1, 0},
    {0x59, (uint8_t[]){0x0A}, 1, 0},
    {0x5A, (uint8_t[]){0x1B}, 1, 0},
    {0x5B, (uint8_t[]){0x19}, 1, 0},
    // Add essential commands for display on
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120}, // Sleep out with 120ms delay
    {0x29, (uint8_t[]){0x00}, 1, 20},  // Display on with 20ms delay
    {0x35, (uint8_t[]){0x00}, 1, 0},   // Tearing effect line on
};

void app_main(void)
{
    ESP_LOGI(TAG, "=== Direct LCD JD9365 Test Starting ===");
    
    // Step 1: Initialize backlight GPIO (we know this works)
    ESP_LOGI(TAG, "Step 1: Configuring backlight GPIO%d", TEST_PIN_NUM_BK_LIGHT);
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << TEST_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    
    // Turn backlight OFF during initialization
    gpio_set_level(TEST_PIN_NUM_BK_LIGHT, TEST_LCD_BK_LIGHT_OFF_LEVEL);
    ESP_LOGI(TAG, "Backlight OFF during initialization");
    
    // Step 2: Power on MIPI DSI PHY
    ESP_LOGI(TAG, "Step 2: Powering on MIPI DSI PHY");
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = TEST_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = TEST_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY powered on with %dmV", TEST_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV);
    
    // Step 3: Initialize MIPI DSI bus
    ESP_LOGI(TAG, "Step 3: Initializing MIPI DSI bus");
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = TEST_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 1000, // 1000 Mbps
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));
    ESP_LOGI(TAG, "MIPI DSI bus initialized with %d lanes", TEST_MIPI_DSI_LANE_NUM);
    
    // Step 4: Create DBI panel IO
    ESP_LOGI(TAG, "Step 4: Creating DBI panel IO");
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));
    ESP_LOGI(TAG, "DBI panel IO created");
    
    // Step 5: Create LCD panel with JD9365 driver
    ESP_LOGI(TAG, "Step 5: Creating JD9365 LCD panel");
    esp_lcd_panel_handle_t panel_handle = NULL;
    
    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 80,
        .virtual_channel = 0,
        .pixel_format = TEST_MIPI_DPI_PX_FORMAT,
        .num_fbs = 1,
        .video_timing = {
            .h_size = TEST_LCD_H_RES,
            .v_size = TEST_LCD_V_RES,
            .hsync_back_porch = 20,
            .hsync_pulse_width = 20,
            .hsync_front_porch = 40,
            .vsync_back_porch = 12,
            .vsync_pulse_width = 4,
            .vsync_front_porch = 24,
        },
        .flags.use_dma2d = true,
    };
    
    jd9365_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = TEST_MIPI_DSI_LANE_NUM,
        },
    };
    
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TEST_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = TEST_LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365(mipi_dbi_io, &panel_config, &panel_handle));
    ESP_LOGI(TAG, "JD9365 panel created");
    
    // Step 6: Reset and initialize panel
    ESP_LOGI(TAG, "Step 6: Resetting LCD panel");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "Step 7: Initializing LCD panel");
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "Step 8: Turning display ON");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Step 9: Turn on backlight
    ESP_LOGI(TAG, "Step 9: Turning backlight ON");
    gpio_set_level(TEST_PIN_NUM_BK_LIGHT, TEST_LCD_BK_LIGHT_ON_LEVEL);
    
    // Step 10: Draw test pattern
    ESP_LOGI(TAG, "Step 10: Drawing color bars");
    
    // Allocate a small buffer for color bars
    uint8_t *color_buffer = (uint8_t *)heap_caps_calloc(1, TEST_LCD_H_RES * 100 * 3, MALLOC_CAP_DMA);
    if (color_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate color buffer!");
        return;
    }
    
    // Draw red, green, blue, white bars
    uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
    const char *color_names[] = {"RED", "GREEN", "BLUE", "WHITE"};
    
    for (int color_idx = 0; color_idx < 4; color_idx++) {
        uint32_t color = colors[color_idx];
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        
        ESP_LOGI(TAG, "Drawing %s bar at y=%d", color_names[color_idx], color_idx * 200);
        
        // Fill buffer with color
        for (int i = 0; i < TEST_LCD_H_RES * 100; i++) {
            color_buffer[i * 3 + 0] = r;
            color_buffer[i * 3 + 1] = g;
            color_buffer[i * 3 + 2] = b;
        }
        
        // Draw the color bar
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, color_idx * 200, 
                                                  TEST_LCD_H_RES, (color_idx + 1) * 200, 
                                                  color_buffer));
        vTaskDelay(pdMS_TO_TICKS(500)); // Show each color for 500ms
    }
    
    free(color_buffer);
    
    ESP_LOGI(TAG, "=== Direct LCD Test Complete ===");
    ESP_LOGI(TAG, "You should see 4 color bars: RED, GREEN, BLUE, WHITE");
    ESP_LOGI(TAG, "If display is still blank, check:");
    ESP_LOGI(TAG, "  1. Power supply to display");
    ESP_LOGI(TAG, "  2. MIPI DSI cable connections");
    ESP_LOGI(TAG, "  3. Reset pin connection (GPIO27)");
    
    // Keep alive
    while (1) {
        ESP_LOGI(TAG, "Test running... Free heap: %lu", esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}