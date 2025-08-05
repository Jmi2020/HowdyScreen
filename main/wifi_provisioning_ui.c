#include "wifi_provisioning_ui.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_ui";

// Default configuration
static const wifi_ui_config_t default_config = {
    .parent = NULL,
    .show_signal_strength = true,
    .show_security_icons = true,
    .list_item_height = 60,
    .max_networks_shown = 10,
};

// WiFi UI state
static struct {
    wifi_ui_config_t config;
    wifi_ui_state_t state;
    wifi_ui_event_cb_t event_cb;
    void *user_data;
    
    // LVGL objects
    lv_obj_t *main_container;
    lv_obj_t *title_label;
    lv_obj_t *status_label;
    lv_obj_t *network_list;
    lv_obj_t *manual_entry_container;
    lv_obj_t *connection_container;
    lv_obj_t *ap_mode_container;
    lv_obj_t *progress_bar;
    lv_obj_t *qr_code_canvas;
    
    // Input fields
    lv_obj_t *ssid_input;
    lv_obj_t *password_input;
    lv_obj_t *connect_btn;
    lv_obj_t *scan_btn;
    lv_obj_t *manual_btn;
    lv_obj_t *ap_mode_btn;
    lv_obj_t *back_btn;
    
    bool initialized;
    wifi_ap_record_t networks[10];
    uint16_t network_count;
} s_wifi_ui = {
    .state = WIFI_UI_STATE_INIT,
    .initialized = false,
    .network_count = 0,
};

// Forward declarations
static void create_main_screen(void);
static void create_network_list_screen(void);
static void create_manual_entry_screen(void);
static void create_connection_screen(void);
static void create_ap_mode_screen(void);
static void show_screen(lv_obj_t *screen);
static void hide_all_screens(void);
static void network_list_event_cb(lv_event_t *e);
static void manual_entry_event_cb(lv_event_t *e);
static void button_event_cb(lv_event_t *e);
static void notify_event(wifi_ui_event_data_t *event_data);
static const char* get_auth_mode_string(wifi_auth_mode_t auth_mode);
static int get_signal_strength_bars(int8_t rssi);

void wifi_ui_get_default_config(wifi_ui_config_t *config)
{
    if (!config) return;
    *config = default_config;
}

esp_err_t wifi_ui_init(const wifi_ui_config_t *config, wifi_ui_event_cb_t event_cb, void *user_data)
{
    if (s_wifi_ui.initialized) {
        ESP_LOGW(TAG, "WiFi UI already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi provisioning UI");
    
    // Use provided config or defaults
    if (config) {
        s_wifi_ui.config = *config;
    } else {
        wifi_ui_get_default_config(&s_wifi_ui.config);
    }
    
    s_wifi_ui.event_cb = event_cb;
    s_wifi_ui.user_data = user_data;
    
    // Create main container
    if (s_wifi_ui.config.parent) {
        s_wifi_ui.main_container = lv_obj_create(s_wifi_ui.config.parent);
    } else {
        s_wifi_ui.main_container = lv_obj_create(lv_scr_act());
    }
    
    lv_obj_set_size(s_wifi_ui.main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_wifi_ui.main_container);
    lv_obj_set_style_pad_all(s_wifi_ui.main_container, 10, 0);
    
    // Create all screens
    create_main_screen();
    create_network_list_screen();
    create_manual_entry_screen();
    create_connection_screen();
    create_ap_mode_screen();
    
    // Start with main screen
    s_wifi_ui.state = WIFI_UI_STATE_INIT;
    show_screen(NULL); // Show main screen by default
    
    s_wifi_ui.initialized = true;
    ESP_LOGI(TAG, "WiFi UI initialized successfully");
    
    return ESP_OK;
}

esp_err_t wifi_ui_set_state(wifi_ui_state_t state)
{
    if (!s_wifi_ui.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "WiFi UI state transition: %d -> %d", s_wifi_ui.state, state);
    s_wifi_ui.state = state;
    
    hide_all_screens();
    
    switch (state) {
        case WIFI_UI_STATE_INIT:
            show_screen(NULL);
            lv_label_set_text(s_wifi_ui.status_label, "Ready to configure WiFi");
            break;
            
        case WIFI_UI_STATE_SCANNING:
            show_screen(NULL);
            lv_label_set_text(s_wifi_ui.status_label, "Scanning for networks...");
            break;
            
        case WIFI_UI_STATE_NETWORK_LIST:
            show_screen(s_wifi_ui.network_list);
            break;
            
        case WIFI_UI_STATE_MANUAL_ENTRY:
            show_screen(s_wifi_ui.manual_entry_container);
            break;
            
        case WIFI_UI_STATE_CONNECTING:
            show_screen(s_wifi_ui.connection_container);
            break;
            
        case WIFI_UI_STATE_CONNECTED:
            show_screen(s_wifi_ui.connection_container);
            break;
            
        case WIFI_UI_STATE_AP_MODE:
            show_screen(s_wifi_ui.ap_mode_container);
            break;
            
        case WIFI_UI_STATE_ERROR:
            show_screen(s_wifi_ui.connection_container);
            break;
    }
    
    return ESP_OK;
}

wifi_ui_state_t wifi_ui_get_state(void)
{
    return s_wifi_ui.state;
}

esp_err_t wifi_ui_update_network_list(wifi_ap_record_t *ap_records, uint16_t count)
{
    if (!s_wifi_ui.initialized || !ap_records) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Updating network list with %d networks", count);
    
    // Store networks
    uint16_t max_count = sizeof(s_wifi_ui.networks) / sizeof(s_wifi_ui.networks[0]);
    s_wifi_ui.network_count = (count > max_count) ? max_count : count;
    memcpy(s_wifi_ui.networks, ap_records, s_wifi_ui.network_count * sizeof(wifi_ap_record_t));
    
    // Clear existing list
    lv_obj_clean(s_wifi_ui.network_list);
    
    // Add networks to list
    for (int i = 0; i < s_wifi_ui.network_count; i++) {
        lv_obj_t *item = lv_list_add_btn(s_wifi_ui.network_list, 
                                        LV_SYMBOL_WIFI, 
                                        (char*)s_wifi_ui.networks[i].ssid);
        
        // Store network index in user data
        lv_obj_set_user_data(item, (void*)(uintptr_t)i);
        lv_obj_add_event_cb(item, network_list_event_cb, LV_EVENT_CLICKED, NULL);
        
        // Add signal strength and security info
        if (s_wifi_ui.config.show_signal_strength || s_wifi_ui.config.show_security_icons) {
            lv_obj_t *info_label = lv_label_create(item);
            char info_text[64];
            
            int bars = get_signal_strength_bars(s_wifi_ui.networks[i].rssi);
            const char *auth_str = get_auth_mode_string(s_wifi_ui.networks[i].authmode);
            
            snprintf(info_text, sizeof(info_text), "%d dBm | %s | %s", 
                     s_wifi_ui.networks[i].rssi, auth_str, 
                     (bars > 2) ? "Strong" : (bars > 1) ? "Good" : "Weak");
            
            lv_label_set_text(info_label, info_text);
            lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
            lv_obj_align(info_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        }
    }
    
    return ESP_OK;
}

esp_err_t wifi_ui_show_connection_progress(const char *ssid, uint8_t progress_percent)
{
    if (!s_wifi_ui.initialized || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Showing connection progress: %s (%d%%)", ssid, progress_percent);
    
    char status_text[128];
    snprintf(status_text, sizeof(status_text), "Connecting to: %s", ssid);
    lv_label_set_text(s_wifi_ui.status_label, status_text);
    
    if (s_wifi_ui.progress_bar) {
        lv_bar_set_value(s_wifi_ui.progress_bar, progress_percent, LV_ANIM_ON);
    }
    
    return ESP_OK;
}

esp_err_t wifi_ui_show_connection_success(const wifi_connection_info_t *connection_info)
{
    if (!s_wifi_ui.initialized || !connection_info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Showing connection success: %s", connection_info->connected_ssid);
    
    char status_text[200];
    snprintf(status_text, sizeof(status_text), 
             "Connected!\nSSID: %s\nIP: %s\nSignal: %d dBm", 
             connection_info->connected_ssid,
             connection_info->ip_address,
             connection_info->rssi);
    
    lv_label_set_text(s_wifi_ui.status_label, status_text);
    
    if (s_wifi_ui.progress_bar) {
        lv_bar_set_value(s_wifi_ui.progress_bar, 100, LV_ANIM_ON);
    }
    
    return ESP_OK;
}

esp_err_t wifi_ui_show_connection_error(const char *error_message)
{
    if (!s_wifi_ui.initialized || !error_message) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Showing connection error: %s", error_message);
    
    char status_text[128];
    snprintf(status_text, sizeof(status_text), "Connection Failed:\n%s", error_message);
    lv_label_set_text(s_wifi_ui.status_label, status_text);
    
    if (s_wifi_ui.progress_bar) {
        lv_bar_set_value(s_wifi_ui.progress_bar, 0, LV_ANIM_ON);
    }
    
    return ESP_OK;
}

esp_err_t wifi_ui_show_ap_mode_info(const char *ap_ssid, const char *ap_password)
{
    if (!s_wifi_ui.initialized || !ap_ssid || !ap_password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Showing AP mode info: %s", ap_ssid);
    
    char info_text[256];
    snprintf(info_text, sizeof(info_text),
             "AP Mode Active\n\n"
             "Connect to:\n%s\n\n"
             "Password:\n%s\n\n"
             "Then visit:\nhttp://192.168.4.1",
             ap_ssid, ap_password);
    
    lv_label_set_text(s_wifi_ui.status_label, info_text);
    
    return ESP_OK;
}

esp_err_t wifi_ui_show_manual_entry(const char *prefill_ssid)
{
    if (!s_wifi_ui.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (prefill_ssid && s_wifi_ui.ssid_input) {
        lv_textarea_set_text(s_wifi_ui.ssid_input, prefill_ssid);
    }
    
    if (s_wifi_ui.password_input) {
        lv_textarea_set_text(s_wifi_ui.password_input, "");
    }
    
    return ESP_OK;
}

esp_err_t wifi_ui_show_qr_code(const char *ssid, const char *password, const char *auth_type)
{
    if (!s_wifi_ui.initialized || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "QR code generation not implemented yet");
    // TODO: Implement QR code generation
    
    return ESP_OK;
}

esp_err_t wifi_ui_update_signal_strength(int8_t rssi)
{
    // Update signal strength indicator if visible
    ESP_LOGD(TAG, "Signal strength: %d dBm", rssi);
    return ESP_OK;
}

esp_err_t wifi_ui_deinit(void)
{
    if (!s_wifi_ui.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing WiFi UI");
    
    if (s_wifi_ui.main_container) {
        lv_obj_del(s_wifi_ui.main_container);
        s_wifi_ui.main_container = NULL;
    }
    
    memset(&s_wifi_ui, 0, sizeof(s_wifi_ui));
    s_wifi_ui.state = WIFI_UI_STATE_INIT;
    
    ESP_LOGI(TAG, "WiFi UI deinitialized");
    return ESP_OK;
}

// Internal functions

static void create_main_screen(void)
{
    // Title
    s_wifi_ui.title_label = lv_label_create(s_wifi_ui.main_container);
    lv_label_set_text(s_wifi_ui.title_label, "WiFi Setup");
    lv_obj_set_style_text_font(s_wifi_ui.title_label, &lv_font_montserrat_24, 0);
    lv_obj_align(s_wifi_ui.title_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // Status label
    s_wifi_ui.status_label = lv_label_create(s_wifi_ui.main_container);
    lv_label_set_text(s_wifi_ui.status_label, "Ready to configure WiFi");
    lv_obj_set_style_text_align(s_wifi_ui.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_wifi_ui.status_label, LV_ALIGN_CENTER, 0, -50);
    
    // Scan button
    s_wifi_ui.scan_btn = lv_btn_create(s_wifi_ui.main_container);
    lv_obj_set_size(s_wifi_ui.scan_btn, 200, 50);
    lv_obj_align(s_wifi_ui.scan_btn, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_event_cb(s_wifi_ui.scan_btn, button_event_cb, LV_EVENT_CLICKED, (void*)WIFI_UI_EVENT_SCAN_REQUESTED);
    
    lv_obj_t *scan_label = lv_label_create(s_wifi_ui.scan_btn);
    lv_label_set_text(scan_label, "Scan Networks");
    lv_obj_center(scan_label);
    
    // Manual entry button
    s_wifi_ui.manual_btn = lv_btn_create(s_wifi_ui.main_container);
    lv_obj_set_size(s_wifi_ui.manual_btn, 200, 50);
    lv_obj_align(s_wifi_ui.manual_btn, LV_ALIGN_CENTER, 0, 80);
    lv_obj_add_event_cb(s_wifi_ui.manual_btn, button_event_cb, LV_EVENT_CLICKED, (void*)WIFI_UI_EVENT_NETWORK_SELECTED);
    
    lv_obj_t *manual_label = lv_label_create(s_wifi_ui.manual_btn);
    lv_label_set_text(manual_label, "Manual Entry");
    lv_obj_center(manual_label);
    
    // AP mode button
    s_wifi_ui.ap_mode_btn = lv_btn_create(s_wifi_ui.main_container);
    lv_obj_set_size(s_wifi_ui.ap_mode_btn, 200, 50);
    lv_obj_align(s_wifi_ui.ap_mode_btn, LV_ALIGN_CENTER, 0, 140);
    lv_obj_add_event_cb(s_wifi_ui.ap_mode_btn, button_event_cb, LV_EVENT_CLICKED, (void*)WIFI_UI_EVENT_AP_MODE_REQUESTED);
    
    lv_obj_t *ap_label = lv_label_create(s_wifi_ui.ap_mode_btn);
    lv_label_set_text(ap_label, "AP Mode");
    lv_obj_center(ap_label);
}

static void create_network_list_screen(void)
{
    s_wifi_ui.network_list = lv_list_create(s_wifi_ui.main_container);
    lv_obj_set_size(s_wifi_ui.network_list, LV_PCT(90), LV_PCT(70));
    lv_obj_align(s_wifi_ui.network_list, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_flag(s_wifi_ui.network_list, LV_OBJ_FLAG_HIDDEN);
    
    // Back button for network list
    lv_obj_t *back_btn = lv_btn_create(s_wifi_ui.main_container);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(back_btn, button_event_cb, LV_EVENT_CLICKED, (void*)WIFI_UI_EVENT_BACK_PRESSED);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
}

static void create_manual_entry_screen(void)
{
    s_wifi_ui.manual_entry_container = lv_obj_create(s_wifi_ui.main_container);
    lv_obj_set_size(s_wifi_ui.manual_entry_container, LV_PCT(90), LV_PCT(70));
    lv_obj_align(s_wifi_ui.manual_entry_container, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_flag(s_wifi_ui.manual_entry_container, LV_OBJ_FLAG_HIDDEN);
    
    // SSID input
    lv_obj_t *ssid_label = lv_label_create(s_wifi_ui.manual_entry_container);
    lv_label_set_text(ssid_label, "Network Name (SSID):");
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 10, 20);
    
    s_wifi_ui.ssid_input = lv_textarea_create(s_wifi_ui.manual_entry_container);
    lv_obj_set_size(s_wifi_ui.ssid_input, LV_PCT(80), 50);
    lv_obj_align(s_wifi_ui.ssid_input, LV_ALIGN_TOP_MID, 0, 50);
    lv_textarea_set_one_line(s_wifi_ui.ssid_input, true);
    lv_textarea_set_placeholder_text(s_wifi_ui.ssid_input, "Enter WiFi network name");
    
    // Password input
    lv_obj_t *password_label = lv_label_create(s_wifi_ui.manual_entry_container);
    lv_label_set_text(password_label, "Password:");
    lv_obj_align(password_label, LV_ALIGN_TOP_LEFT, 10, 120);
    
    s_wifi_ui.password_input = lv_textarea_create(s_wifi_ui.manual_entry_container);
    lv_obj_set_size(s_wifi_ui.password_input, LV_PCT(80), 50);
    lv_obj_align(s_wifi_ui.password_input, LV_ALIGN_TOP_MID, 0, 150);
    lv_textarea_set_one_line(s_wifi_ui.password_input, true);
    lv_textarea_set_password_mode(s_wifi_ui.password_input, true);
    lv_textarea_set_placeholder_text(s_wifi_ui.password_input, "Enter password");
    
    // Connect button
    s_wifi_ui.connect_btn = lv_btn_create(s_wifi_ui.manual_entry_container);
    lv_obj_set_size(s_wifi_ui.connect_btn, 150, 50);
    lv_obj_align(s_wifi_ui.connect_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_event_cb(s_wifi_ui.connect_btn, manual_entry_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *connect_label = lv_label_create(s_wifi_ui.connect_btn);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_center(connect_label);
    
    // Back button
    s_wifi_ui.back_btn = lv_btn_create(s_wifi_ui.manual_entry_container);
    lv_obj_set_size(s_wifi_ui.back_btn, 100, 50);
    lv_obj_align(s_wifi_ui.back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(s_wifi_ui.back_btn, button_event_cb, LV_EVENT_CLICKED, (void*)WIFI_UI_EVENT_BACK_PRESSED);
    
    lv_obj_t *back_label2 = lv_label_create(s_wifi_ui.back_btn);
    lv_label_set_text(back_label2, "Back");
    lv_obj_center(back_label2);
}

static void create_connection_screen(void)
{
    s_wifi_ui.connection_container = lv_obj_create(s_wifi_ui.main_container);
    lv_obj_set_size(s_wifi_ui.connection_container, LV_PCT(90), LV_PCT(70));
    lv_obj_align(s_wifi_ui.connection_container, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_flag(s_wifi_ui.connection_container, LV_OBJ_FLAG_HIDDEN);
    
    // Connection status (uses shared status_label)
    
    // Progress bar
    s_wifi_ui.progress_bar = lv_bar_create(s_wifi_ui.connection_container);
    lv_obj_set_size(s_wifi_ui.progress_bar, LV_PCT(80), 20);
    lv_obj_align(s_wifi_ui.progress_bar, LV_ALIGN_CENTER, 0, 50);
    lv_bar_set_range(s_wifi_ui.progress_bar, 0, 100);
}

static void create_ap_mode_screen(void)
{
    s_wifi_ui.ap_mode_container = lv_obj_create(s_wifi_ui.main_container);
    lv_obj_set_size(s_wifi_ui.ap_mode_container, LV_PCT(90), LV_PCT(70));
    lv_obj_align(s_wifi_ui.ap_mode_container, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_flag(s_wifi_ui.ap_mode_container, LV_OBJ_FLAG_HIDDEN);
    
    // AP mode info (uses shared status_label)
}

static void show_screen(lv_obj_t *screen)
{
    if (screen) {
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_all_screens(void)
{
    if (s_wifi_ui.network_list) {
        lv_obj_add_flag(s_wifi_ui.network_list, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wifi_ui.manual_entry_container) {
        lv_obj_add_flag(s_wifi_ui.manual_entry_container, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wifi_ui.connection_container) {
        lv_obj_add_flag(s_wifi_ui.connection_container, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wifi_ui.ap_mode_container) {
        lv_obj_add_flag(s_wifi_ui.ap_mode_container, LV_OBJ_FLAG_HIDDEN);
    }
}

static void network_list_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int network_index = (int)(uintptr_t)lv_obj_get_user_data(btn);
    
    if (network_index >= 0 && network_index < s_wifi_ui.network_count) {
        wifi_ui_event_data_t event_data = {
            .event = WIFI_UI_EVENT_NETWORK_SELECTED,
            .data.network_selected = {
                .rssi = s_wifi_ui.networks[network_index].rssi,
                .auth_mode = s_wifi_ui.networks[network_index].authmode
            }
        };
        
        strncpy(event_data.data.network_selected.ssid, 
                (char*)s_wifi_ui.networks[network_index].ssid, 
                sizeof(event_data.data.network_selected.ssid) - 1);
        
        notify_event(&event_data);
    }
}

static void manual_entry_event_cb(lv_event_t *e)
{
    const char *ssid = lv_textarea_get_text(s_wifi_ui.ssid_input);
    const char *password = lv_textarea_get_text(s_wifi_ui.password_input);
    
    if (strlen(ssid) > 0) {
        wifi_ui_event_data_t event_data = {
            .event = WIFI_UI_EVENT_CREDENTIALS_ENTERED
        };
        
        strncpy(event_data.data.credentials_entered.ssid, ssid, 
                sizeof(event_data.data.credentials_entered.ssid) - 1);
        strncpy(event_data.data.credentials_entered.password, password, 
                sizeof(event_data.data.credentials_entered.password) - 1);
        
        notify_event(&event_data);
    }
}

static void button_event_cb(lv_event_t *e)
{
    wifi_ui_event_t event_type = (wifi_ui_event_t)(uintptr_t)lv_event_get_user_data(e);
    
    wifi_ui_event_data_t event_data = {
        .event = event_type
    };
    
    notify_event(&event_data);
}

static void notify_event(wifi_ui_event_data_t *event_data)
{
    if (s_wifi_ui.event_cb) {
        s_wifi_ui.event_cb(event_data, s_wifi_ui.user_data);
    }
}

static const char* get_auth_mode_string(wifi_auth_mode_t auth_mode)
{
    switch (auth_mode) {
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default: return "Unknown";
    }
}

static int get_signal_strength_bars(int8_t rssi)
{
    if (rssi >= -50) return 4;  // Excellent
    if (rssi >= -60) return 3;  // Good
    if (rssi >= -70) return 2;  // Fair
    if (rssi >= -80) return 1;  // Poor
    return 0;                   // Very poor
}