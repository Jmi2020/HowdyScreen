idf_component_register(
    SRCS "src/audio_processor.c" 
         "src/audio_pipeline.c"
         "src/audio_memory_buffer.c"
         "src/voice_activity_detector.c"
         "src/enhanced_vad.c"
         "src/enhanced_udp_audio.c"
         "src/esp32_p4_wake_word.c"
         "src/continuous_audio_processor.c"
         "src/dual_i2s_manager.c"
         "src/udp_audio_streamer.c"
         "src/tts_audio_handler.c"
         "src/stt_audio_handler.c"
         "src/audio_interface_coordinator.c"
         "src/tts_jitter_buffer.c"
         "src/i2c_debug_utils.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer websocket_client lwip waveshare__esp32_p4_wifi6_touch_lcd_xc
    PRIV_REQUIRES esp_ringbuf spi_flash
)
