# ESP32-P4-WiFi6-Touch-LCD-XC Hardware Setup Guide

## Overview

This guide provides detailed instructions for setting up the ESP32-P4-WiFi6-Touch-LCD-XC development board for the HowdyScreen project. This board features a dual-chip architecture with the ESP32-P4 as the main processor and ESP32-C6 as the WiFi co-processor.

## Board Specifications

### ESP32-P4-WiFi6-Touch-LCD-XC Features

- **Main Processor**: ESP32-P4 (RISC-V dual-core, 400 MHz)
- **WiFi Co-processor**: ESP32-C6 (RISC-V single-core, 160 MHz)
- **Display**: 3.4" Round LCD, 800×800 resolution, MIPI-DSI interface
- **Touch Controller**: CST9217 capacitive touch controller
- **Audio Codec**: ES8311 (24-bit DAC/ADC)
- **Memory**: 8MB PSRAM, 16MB Flash
- **Connectivity**: WiFi 6 (2.4GHz), Bluetooth LE 5.0
- **Storage**: MicroSD card slot
- **USB**: USB-C for programming and power

### Key Components Layout

```
┌─────────────────────────────────────────┐
│                                         │
│    ┌─────────┐      ┌─────────┐        │
│    │ ESP32-P4│      │ ESP32-C6│        │
│    │         │ SDIO │         │        │
│    │         │◄────►│         │        │
│    └─────────┘      └─────────┘        │
│                                         │
│  ┌───────────────────────────────────┐  │
│  │                                   │  │
│  │      3.4" Round LCD Display      │  │
│  │         800×800 pixels           │  │
│  │    CST9217 Touch Controller      │  │
│  │                                   │  │
│  └───────────────────────────────────┘  │
│                                         │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐ │
│  │ ES8311  │  │MicroSD  │  │  USB-C  │ │
│  │ Codec   │  │  Slot   │  │  Port   │ │
│  └─────────┘  └─────────┘  └─────────┘ │
└─────────────────────────────────────────┘
```

## Hardware Setup Procedure

### Step 1: Unboxing and Initial Inspection

1. **Package Contents Verification**:
   - ESP32-P4-WiFi6-Touch-LCD-XC board
   - USB-C cable
   - Mounting standoffs (if included)
   - Quick start guide

2. **Visual Inspection**:
   - Check for physical damage to the board
   - Verify all connectors are intact
   - Ensure the LCD display is clean and undamaged
   - Check that both ESP32-P4 and ESP32-C6 chips are properly seated

3. **Power Input Verification**:
   - Locate the USB-C connector
   - Verify power LED indicators (if present)
   - Check voltage selection jumpers (if any)

### Step 2: Power Supply Configuration

#### Power Requirements

| Component | Voltage | Current | Notes |
|-----------|---------|---------|-------|
| ESP32-P4 | 3.3V | 500mA peak | Main processor |
| ESP32-C6 | 3.3V | 300mA peak | WiFi co-processor |
| LCD Display | 3.3V | 200mA | With backlight |
| Audio Codec | 3.3V | 50mA | ES8311 |
| **Total** | **5V USB** | **1.5A recommended** | **Via USB-C** |

#### Power Connection

```
USB-C Power Supply Connections:
┌─────────────┐
│   USB-C     │
│  Connector  │
├─────────────┤
│ VBUS (5V)   │ ──► Power Regulator ──► 3.3V to all components
│ GND         │ ──► Ground plane
│ D+/D-       │ ──► USB data for programming
│ CC1/CC2     │ ──► USB-C detection
└─────────────┘
```

**Setup Instructions**:
1. Connect the provided USB-C cable to the board
2. Connect the other end to a USB power supply (minimum 2A recommended)
3. Verify power LEDs illuminate (check board documentation for LED positions)
4. Measure 3.3V on test points if available

### Step 3: Display and Touch Configuration

#### Display Specifications

| Parameter | Value |
|-----------|-------|
| Size | 3.4 inches diagonal |
| Resolution | 800×800 pixels |
| Interface | MIPI-DSI |
| Color Depth | 16-bit RGB565 |
| Viewing Angle | 170° (all directions) |
| Backlight | White LED, PWM control |

#### Touch Controller Specifications

| Parameter | Value |
|-----------|-------|
| Controller | CST9217 |
| Interface | I2C |
| Points | Up to 5 simultaneous |
| Resolution | 800×800 |
| I2C Address | 0x1A |

#### Connection Verification

**MIPI-DSI Display Connection**:
```
ESP32-P4 ──► Display Panel
DSI_CLK+ ──► MIPI Clock+
DSI_CLK- ──► MIPI Clock-
DSI_D0+ ──► MIPI Data 0+
DSI_D0- ──► MIPI Data 0-
DSI_D1+ ──► MIPI Data 1+
DSI_D1- ──► MIPI Data 1-
```

**Touch Controller I2C Connection**:
```
ESP32-P4 Pin │ CST9217 Pin │ Function
GPIO_7       │ SDA         │ I2C Data
GPIO_8       │ SCL         │ I2C Clock
GPIO_23      │ RST         │ Reset (active low)
GND          │ INT         │ Interrupt (not used)
3.3V         │ VCC         │ Power
GND          │ GND         │ Ground
```

### Step 4: Audio System Configuration

#### ES8311 Audio Codec

| Parameter | Value |
|-----------|-------|
| Resolution | 24-bit |
| Sample Rate | 8kHz - 96kHz |
| Interface | I2S |
| Control | I2C (address 0x18) |
| Input | Microphone + Line |
| Output | Headphone + Speaker |

#### I2S Audio Connections

```
ESP32-P4 Pin │ ES8311 Pin │ Function
GPIO_13      │ MCLK       │ Master Clock (12.288MHz)
GPIO_12      │ SCLK       │ Serial Clock (Bit Clock)
GPIO_10      │ LRCK       │ Word Select (LR Clock)
GPIO_9       │ SDOUT      │ Serial Data Out (Playback)
GPIO_11      │ SDIN       │ Serial Data In (Recording)
GPIO_7       │ SDA        │ I2C Data (shared with touch)
GPIO_8       │ SCL        │ I2C Clock (shared with touch)
GPIO_53      │ PA_EN      │ Power Amplifier Enable
```

#### Audio Setup Verification

1. **I2C Communication Test**:
   ```bash
   # Use ESP-IDF i2c_tools to scan for devices
   idf.py monitor
   # In monitor, use I2C scanner to verify 0x18 (ES8311) and 0x1A (CST9217)
   ```

2. **Audio Codec Initialization**:
   - Verify MCLK signal present (12.288MHz)
   - Check I2S clock signals during audio operations
   - Test speaker output with test tone
   - Verify microphone input with simple recording

### Step 5: WiFi Co-processor Configuration

#### ESP32-C6 SDIO Interface

The ESP32-C6 communicates with the ESP32-P4 via SDIO interface for WiFi functionality.

```
ESP32-P4 Pin │ ESP32-C6 Pin │ Function
GPIO_36      │ CLK          │ SDIO Clock
GPIO_37      │ CMD          │ SDIO Command
GPIO_35      │ D0           │ SDIO Data 0
GPIO_34      │ D1           │ SDIO Data 1
GPIO_33      │ D2           │ SDIO Data 2
GPIO_48      │ D3           │ SDIO Data 3
GPIO_47      │ EN           │ ESP32-C6 Reset/Enable
```

#### SDIO Configuration Verification

1. **Signal Integrity Check**:
   - Verify all SDIO lines have proper pull-up resistors (10kΩ typical)
   - Check signal quality with oscilloscope if available
   - Ensure stable power supply to ESP32-C6

2. **Communication Test**:
   ```c
   // Test SDIO communication
   esp_err_t test_sdio_communication() {
       // Initialize SDIO host
       sdmmc_host_t host = SDMMC_HOST_DEFAULT();
       host.slot = SDMMC_HOST_SLOT_1;
       
       sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
       slot_config.clk = GPIO_36;
       slot_config.cmd = GPIO_37;
       slot_config.d0 = GPIO_35;
       // ... configure other pins
       
       return sdmmc_host_init_slot(host.slot, &slot_config);
   }
   ```

### Step 6: Storage Configuration

#### MicroSD Card Setup

| Parameter | Value |
|-----------|-------|
| Interface | SDMMC 4-bit |
| Speed | Up to 25MHz |
| Capacity | Up to 32GB (FAT32) |
| Voltage | 3.3V |

#### SD Card Pin Configuration

```
ESP32-P4 Pin │ SD Card Pin │ Function
GPIO_43      │ CLK         │ Clock
GPIO_44      │ CMD         │ Command
GPIO_39      │ D0          │ Data 0
GPIO_40      │ D1          │ Data 1
GPIO_41      │ D2          │ Data 2
GPIO_42      │ D3          │ Data 3
```

#### SD Card Testing

1. **Format SD Card**:
   - Use FAT32 file system
   - Maximum 32GB capacity recommended
   - Use quality SD card (Class 10 or better)

2. **Mount Test**:
   ```c
   esp_err_t test_sd_card() {
       esp_vfs_fat_sdmmc_mount_config_t mount_config = {
           .format_if_mount_failed = false,
           .max_files = 5,
           .allocation_unit_size = 16 * 1024
       };
       
       sdmmc_card_t* card;
       const char mount_point[] = "/sdcard";
       
       return esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, 
                                      &mount_config, &card);
   }
   ```

### Step 7: System Integration Testing

#### Power-On Self Test (POST)

```c
esp_err_t hardware_post(void) {
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "Starting Hardware POST...");
    
    // Test 1: Power rails
    if (!check_power_rails()) {
        ESP_LOGE(TAG, "Power rail test FAILED");
        ret = ESP_FAIL;
    }
    
    // Test 2: I2C devices
    if (!test_i2c_devices()) {
        ESP_LOGE(TAG, "I2C device test FAILED");
        ret = ESP_FAIL;
    }
    
    // Test 3: Display
    if (!test_display()) {
        ESP_LOGE(TAG, "Display test FAILED");
        ret = ESP_FAIL;
    }
    
    // Test 4: Touch controller
    if (!test_touch_controller()) {
        ESP_LOGE(TAG, "Touch controller test FAILED");
        ret = ESP_FAIL;
    }
    
    // Test 5: Audio codec
    if (!test_audio_codec()) {
        ESP_LOGE(TAG, "Audio codec test FAILED");
        ret = ESP_FAIL;
    }
    
    // Test 6: SDIO communication
    if (!test_sdio_interface()) {
        ESP_LOGE(TAG, "SDIO test FAILED");
        ret = ESP_FAIL;
    }
    
    // Test 7: SD card
    if (!test_sd_card_interface()) {
        ESP_LOGW(TAG, "SD card test FAILED (non-critical)");
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Hardware POST PASSED");
    } else {
        ESP_LOGE(TAG, "Hardware POST FAILED");
    }
    
    return ret;
}
```

## Pin Assignment Reference

### Complete Pin Mapping

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| **I2C Interface** | | | |
| GPIO_7 | I2C_SDA | Bidirectional | Shared: Audio codec + Touch |
| GPIO_8 | I2C_SCL | Output | I2C clock line |
| **Audio Interface** | | | |
| GPIO_9 | I2S_DOUT | Output | Audio data to codec |
| GPIO_10 | I2S_LRCK | Output | Word select clock |
| GPIO_11 | I2S_DSIN | Input | Audio data from codec |
| GPIO_12 | I2S_SCLK | Output | Bit clock |
| GPIO_13 | I2S_MCLK | Output | Master clock (12.288MHz) |
| GPIO_53 | POWER_AMP_EN | Output | Speaker amplifier enable |
| **Display Interface** | | | |
| GPIO_23 | TOUCH_RST | Output | Touch controller reset |
| GPIO_26 | LCD_BACKLIGHT | Output | PWM backlight control |
| GPIO_27 | LCD_RST | Output | Display reset |
| **SDIO Interface (ESP32-C6)** | | | |
| GPIO_33 | SDIO_D2 | Bidirectional | SDIO data line 2 |
| GPIO_34 | SDIO_D1 | Bidirectional | SDIO data line 1 |
| GPIO_35 | SDIO_D0 | Bidirectional | SDIO data line 0 |
| GPIO_36 | SDIO_CLK | Output | SDIO clock |
| GPIO_37 | SDIO_CMD | Bidirectional | SDIO command |
| GPIO_47 | C6_RESET | Output | ESP32-C6 reset |
| GPIO_48 | SDIO_D3 | Bidirectional | SDIO data line 3 |
| **SD Card Interface** | | | |
| GPIO_39 | SD_D0 | Bidirectional | SD card data 0 |
| GPIO_40 | SD_D1 | Bidirectional | SD card data 1 |
| GPIO_41 | SD_D2 | Bidirectional | SD card data 2 |
| GPIO_42 | SD_D3 | Bidirectional | SD card data 3 |
| GPIO_43 | SD_CLK | Output | SD card clock |
| GPIO_44 | SD_CMD | Bidirectional | SD card command |

### Power Distribution

```
USB-C 5V Input
      │
      ▼
┌─────────────┐
│   3.3V LDO  │ ──► 3.3V Main Rail
│  Regulator  │
└─────────────┘
      │
      ├─► ESP32-P4 (500mA max)
      ├─► ESP32-C6 (300mA max)
      ├─► LCD Display (200mA max)
      ├─► Audio Codec (50mA max)
      ├─► Touch Controller (10mA max)
      └─► SD Card (100mA max)
      
Total: ~1.2A typical, 1.5A peak
```

## Mechanical Considerations

### Board Dimensions

- **Size**: 85mm × 85mm × 12mm (approximate)
- **Weight**: ~45g
- **Mounting**: 4× M3 mounting holes
- **Display**: Circular cutout, 68mm diameter
- **Connectors**: USB-C on side edge

### Environmental Specifications

| Parameter | Min | Typical | Max | Unit |
|-----------|-----|---------|-----|------|
| Operating Temperature | -10 | 25 | 70 | °C |
| Storage Temperature | -40 | 25 | 85 | °C |
| Humidity | 10 | 50 | 90 | %RH |
| Supply Voltage | 4.5 | 5.0 | 5.5 | V |

### Heat Dissipation

**Thermal Management**:
- ESP32-P4: 1W typical, 2W peak
- ESP32-C6: 0.5W typical, 1W peak
- Display: 0.7W typical
- Total: ~2.2W system power

**Cooling Requirements**:
- Natural convection sufficient for normal operation
- Ensure adequate airflow around board
- Consider heat sink for continuous high-performance operation

## Troubleshooting Common Hardware Issues

### Power Issues

| Symptom | Possible Cause | Solution |
|---------|---------------|----------|
| Board doesn't power on | Insufficient power supply | Use 2A+ USB power supply |
| Random resets | Power supply noise | Add capacitors, use quality supply |
| High current draw | Short circuit | Check for damaged components |

### Display Issues

| Symptom | Possible Cause | Solution |
|---------|---------------|----------|
| No display output | MIPI-DSI connection | Check flat cable connections |
| Garbled display | Wrong resolution | Verify 800×800 configuration |
| Dim display | Backlight issue | Check GPIO_26 PWM output |
| Touch not working | I2C communication | Verify CST9217 at address 0x1A |

### Audio Issues

| Symptom | Possible Cause | Solution |
|---------|---------------|----------|
| No audio output | ES8311 not detected | Check I2C address 0x18 |
| Distorted audio | Clock issues | Verify MCLK at 12.288MHz |
| No microphone input | Input not configured | Check I2S capture settings |
| Speaker too quiet | Amplifier disabled | Enable GPIO_53 |

### Network Issues

| Symptom | Possible Cause | Solution |
|---------|---------------|----------|
| WiFi not working | ESP32-C6 not responding | Check SDIO connections |
| Poor WiFi range | Antenna issue | Verify antenna connections |
| Frequent disconnects | Power supply | Check C6 power stability |

### Development Issues

| Symptom | Possible Cause | Solution |
|---------|---------------|----------|
| Upload fails | USB connection | Try different USB cable |
| Device not detected | Driver issue | Install CP210x drivers |
| Slow upload | Baud rate | Use 921600 or 460800 baud |

## Safety Considerations

### Electrical Safety

1. **ESD Protection**:
   - Use anti-static wrist strap when handling
   - Store in anti-static bags
   - Ground workstation properly

2. **Power Safety**:
   - Never exceed 5.5V input voltage
   - Use proper USB-C power supplies
   - Avoid reverse polarity connections

3. **Thermal Safety**:
   - Monitor component temperatures
   - Ensure adequate ventilation
   - Don't block cooling airflow

### Handling Precautions

1. **Display Care**:
   - Avoid touching the LCD surface
   - Clean with soft, dry cloth only
   - Protect from impact and pressure

2. **Connector Care**:
   - Insert USB-C cable gently
   - Don't force connectors
   - Verify orientation before insertion

3. **Storage Recommendations**:
   - Store in original packaging
   - Avoid extreme temperatures
   - Keep away from magnetic fields

This hardware setup guide provides comprehensive instructions for properly configuring and validating the ESP32-P4-WiFi6-Touch-LCD-XC board for the HowdyScreen project. Follow these procedures to ensure reliable hardware operation and successful system integration.