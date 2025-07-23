#ifndef HOWDY_CONFIG_H
#define HOWDY_CONFIG_H

#include "sdkconfig.h"

// Audio codec (ES8311) pin definitions
#define I2S_MCLK        GPIO_NUM_13
#define I2S_SCLK        GPIO_NUM_12  
#define I2S_ASDOUT      GPIO_NUM_11  // Audio serial data out
#define I2S_LRCK        GPIO_NUM_10  // Left/right channel clock
#define I2S_DSDIN       GPIO_NUM_9   // Audio serial data in
#define AUDIO_PA_CTRL   GPIO_NUM_53  // Power amplifier control

// I2C pins for ES8311 control
#define I2C_SDA         GPIO_NUM_7
#define I2C_SCL         GPIO_NUM_8

// LED ring control
#define LED_DATA_PIN    GPIO_NUM_16  // WS2812B data
#define LED_COUNT       109          // Total LEDs in ring

// Audio configuration
#define SAMPLE_RATE     16000
#define CHANNELS        1
#define BITS_PER_SAMPLE 16
#define FRAME_SIZE      320          // 20ms @ 16kHz
#define AUDIO_BUFFER_SIZE (FRAME_SIZE * 2)

// Network configuration
#define UDP_PORT        8000
#define MAX_PACKET_SIZE 256

// Display configuration
#define DISPLAY_WIDTH   800
#define DISPLAY_HEIGHT  800

// Task priorities
#define AUDIO_TASK_PRIORITY     24
#define NETWORK_TASK_PRIORITY   20
#define DISPLAY_TASK_PRIORITY   15
#define LED_TASK_PRIORITY       10

// Stack sizes
#define AUDIO_TASK_STACK_SIZE   4096
#define NETWORK_TASK_STACK_SIZE 4096
#define DISPLAY_TASK_STACK_SIZE 8192
#define LED_TASK_STACK_SIZE     2048

#endif // HOWDY_CONFIG_H