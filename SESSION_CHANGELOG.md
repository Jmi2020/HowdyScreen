# Session Changelog — ESP32‑P4 HowdyScreen

Date: 2025‑08‑10

Objective
- Make ESP32‑P4 reliably act as a low‑latency network microphone and TTS speaker for HowdyTTS with stable playback despite network jitter.

Summary of Changes
- Added a fixed‑frame TTS playback jitter buffer and rewired the audio playback path to a steady 20 ms cadence.
- Clarified and adjusted the audio playback enqueue API and exposed queue depth for telemetry/UI.
- Documented the jitter buffer design and rationale.
- Fixed WiFi retry configuration to honor menuconfig and improved disconnect diagnostics.
- Upgraded round UI: added circular waveform overlay, center STOP button to interrupt TTS, and refined circular layout for 800×800 display.

Details
- New module: TTS jitter buffer
  - Files:
    - components/audio_processor/include/tts_jitter_buffer.h
    - components/audio_processor/src/tts_jitter_buffer.c
  - Functionality:
    - Fixed frame size (default 20 ms at 16 kHz = 320 samples).
    - Push accepts arbitrary PCM sizes, slices into frames, drops oldest on overflow.
    - Pop returns exactly one frame; on underrun, produces silence.

- Audio processor playback integration
  - File: components/audio_processor/src/audio_processor.c
    - New: initializes jitter buffer based on `sample_rate/50` (20 ms frames).
    - New: playback task runs a 20 ms playout clock and writes fixed frames to I2S TX.
    - Changed: `audio_processor_write_data()` now enqueues PCM into jitter buffer (non‑blocking) instead of writing directly to I2S.
    - New: `audio_processor_get_playback_depth()` to report queued frames.
  - File: components/audio_processor/include/audio_processor.h
    - Updated API docs for `audio_processor_write_data()` (enqueue semantics).
    - Added prototype for `audio_processor_get_playback_depth()`.

- Build configuration
  - File: components/audio_processor/CMakeLists.txt
    - Added `src/tts_jitter_buffer.c` to SRCS.

- Documentation
  - File: TTS_JITTER_BUFFER.md — design notes, benefits, and next steps for jitter buffer.
  - File: UI_ASSET_GUIDE.md — how to convert images for 800×800 round display (to be used with LVGL image assets).

- WiFi manager (menuconfig + diagnostics)
  - File: components/wifi_manager/src/wifi_manager.c
    - Now reads `CONFIG_HOWDY_WIFI_MAX_RETRY` from menuconfig and logs the applied value.
    - Logs `WIFI_EVENT_STA_DISCONNECTED` reason code to aid troubleshooting (e.g., auth fail, AP not found).

Rationale
- WebSocket/TCP delivery jitter causes variable arrival timing/size for TTS audio. Feeding I2S directly from network chunks leads to underruns and glitches.
- A small, fixed‑frame jitter buffer decouples variable network timing from deterministic I2S cadence, improving reliability while keeping latency bounded (6–12 frames typical).

Assumptions & Scope
- Audio profile is 16 kHz, 16‑bit mono. Frame = 20 ms (320 samples). Works with current minimal dual_i2s_manager; will continue to work once real I2S/codec init is enabled.
- Existing path from WebSocket → audio_interface_coordinator → audio_processor_write_data() is used to route TTS audio into the jitter buffer; no protocol changes required.

Validation (next actions)
- Build: `idf.py set-target esp32p4 && idf.py build` (then flash/monitor).
- Observe steady playback without frequent underruns during TTS.
- Optionally surface playback depth via logs/UI for visibility.

Planned Follow‑ups
- Add Kconfig options for jitter target/min/max and expose underrun/overflow counters via /status and the LVGL status bar.
- Gate playback start until depth ≥ N frames (e.g., 3) to reduce initial underruns.
- Align dual_i2s_manager with real ES8311 setup, preserving 20 ms frame cadence.
- UI enhancements (round display)
  - File: components/ui_manager/src/ui_manager.c
    - New: lightweight circular waveform renderer using LVGL draw API (no large canvas buffers).
    - New: center button now shows a stop icon and triggers voice callback; main app now interrupts playback.
    - Added waveform history ring and integrated with ui_manager_update_tts_level().
  - File: components/ui_manager/include/ui_manager.h
    - No API changes required for waveform; existing update_tts_level used.

- Audio interface
  - Files: components/audio_processor/include/audio_interface_coordinator.h, src/audio_interface_coordinator.c
    - New: audio_interface_interrupt_playback() to stop I2S playback and clear queued TTS.
  - File: main/howdy_phase6_howdytts_integration.c
    - Center touch callback now calls audio_interface_interrupt_playback() and updates UI to idle.
