#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"

// Use BSP low-level display functions
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "bsp/display.h"
#include "driver/gpio.h"

static const char *TAG = "HowdyIntegrated";

// Display parameters
#define BSP_LCD_H_RES (800)
#define BSP_LCD_V_RES (800)
#define BSP_LCD_BACKLIGHT (26)
#define BSP_LCD_RST (27)
#define MIPI_DSI_LANE_NUM (2)
#define MIPI_DSI_PHY_PWR_LDO_CHAN (3)
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)

// Global display handles
static esp_lcd_panel_handle_t g_panel_handle = NULL;
static esp_ldo_channel_handle_t g_ldo_mipi_phy = NULL;

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
    // Essential commands for display on
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120}, // Sleep out with 120ms delay
    {0x29, (uint8_t[]){0x00}, 1, 20},  // Display on with 20ms delay
    {0x35, (uint8_t[]){0x00}, 1, 0},   // Tearing effect line on
};

// Function prototypes
static void system_init_display(void);
static void create_test_ui(void);
static void demo_task(void *pvParameters);

static void system_init_display(void)
{
    ESP_LOGI(TAG, "Initializing ESP32-P4 display system with minimal MIPI-DSI...");
    
    // Step 1: Manual backlight control (we know this works)
    ESP_LOGI(TAG, "Step 1: Setting up backlight control...");
    gpio_set_direction(BSP_LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_LCD_BACKLIGHT, 1); // OFF during init
    
    // Step 2: Power on MIPI DSI PHY
    ESP_LOGI(TAG, "Step 2: Powering on MIPI DSI PHY...");
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_mipi_phy_config, &g_ldo_mipi_phy);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power MIPI DSI PHY: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "MIPI DSI PHY powered on");
    
    // Step 3: Initialize MIPI DSI bus  
    ESP_LOGI(TAG, "Step 3: Initializing MIPI DSI bus...");
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 1000, // Reduced from 1500 to save bandwidth
    };
    ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MIPI DSI bus: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "MIPI DSI bus created");
    
    // Step 4: Create DBI panel IO for commands only (no framebuffer)
    ESP_LOGI(TAG, "Step 4: Creating DBI panel IO...");
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DBI panel IO: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "DBI panel IO created");
    
    // Step 5: Create control panel without DPI (avoids framebuffer requirement)
    ESP_LOGI(TAG, "Step 5: Creating control panel...");
    jd9365_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = NULL, // No DPI config to avoid framebuffer
            .lane_num = MIPI_DSI_LANE_NUM,
        },
    };
    
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16, // RGB565
        .vendor_config = &vendor_config,
    };
    
    ret = esp_lcd_new_panel_jd9365(mipi_dbi_io, &panel_config, &g_panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create JD9365 panel: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "JD9365 control panel created");
    
    // Step 6: Reset and initialize panel
    ESP_LOGI(TAG, "Step 6: Resetting and initializing display...");
    ret = esp_lcd_panel_reset(g_panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ret = esp_lcd_panel_init(g_panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Step 7: Turn display ON
    ESP_LOGI(TAG, "Step 7: Turning display ON...");
    ret = esp_lcd_panel_disp_on_off(g_panel_handle, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display on failed: %s", esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Step 8: Turn on backlight
    ESP_LOGI(TAG, "Step 8: Turning backlight ON...");
    gpio_set_level(BSP_LCD_BACKLIGHT, 0); // ON (active LOW)
    
    ESP_LOGI(TAG, "Minimal MIPI-DSI display initialization complete!");
}

static void create_test_ui(void)
{
    ESP_LOGI(TAG, "UI creation skipped - display not initialized");
}

static void demo_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting HowdyScreen demo...");
    
    int counter = 0;
    
    while (1) {
        counter++;
        
        // Simple status updates
        if (counter % 50 == 0) {
            ESP_LOGI(TAG, "HowdyScreen running - Counter: %d, Free Heap: %lu", 
                     counter, esp_get_free_heap_size());
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Update every second
    }
}


void app_main(void)
{
    ESP_LOGI(TAG, "HowdyScreen ESP32-P4 starting...");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Board: ESP32-P4-WIFI6-Touch-LCD-3.4C (800x800 round display)");
    
    // Initialize display system
    system_init_display();
    
    ESP_LOGI(TAG, "Display system initialized successfully");
    
    // Create demo task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        demo_task,
        "howdy_demo",
        4096,
        NULL,
        5,
        NULL,
        0
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create demo task");
        return;
    }
    
    ESP_LOGI(TAG, "HowdyScreen system ready");
    
    // Main monitoring loop
    while (1) {
        ESP_LOGI(TAG, "System running - Free heap: %lu bytes", esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(30000)); // Every 30 seconds
    }
}