idf_component_register(
    SRCS "src/audio_processor.c" 
         "src/audio_pipeline.c"
         "src/audio_memory_buffer.c"
         "src/voice_activity_detector.c"
         "src/continuous_audio_processor.c"
         "src/dual_i2s_manager.c"
         "src/udp_audio_streamer.c"
         "src/tts_audio_handler.c"
         "src/stt_audio_handler.c"
         "src/audio_interface_coordinator.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer websocket_client lwip
    PRIV_REQUIRES esp_ringbuf spi_flash
)