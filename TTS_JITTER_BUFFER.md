TTS Playback Jitter Buffer

Overview
- Goal: Make TTS playback reliable by decoupling variable network framing from fixed I2S write cadence.
- Frame: 20 ms @ 16 kHz mono, 16-bit PCM = 320 samples (640 bytes).

Implementation
- Module: components/audio_processor/src/tts_jitter_buffer.c (+ include/tts_jitter_buffer.h)
- Audio processor:
  - Initializes jitter buffer using sample_rate/50 frame size.
  - audio_processor_write_data() now enqueues PCM into the jitter buffer (non-blocking).
  - audio_playback_task() pops exactly one frame every 20 ms and writes to I2S TX.
  - Underrun → writes silence; Overflow → drops oldest frame.

Public API Changes
- audio_processor_write_data(): clarified semantics to enqueue for playback.
- audio_processor_get_playback_depth(): returns queued frames (for UI/telemetry).

Why this helps
- Absorbs network jitter and coalesces arbitrary binary frame sizes arriving from WebSocket.
- Ensures I2S sees constant-size writes on a steady 20 ms cadence, reducing underruns.
- Keeps latency bounded via a small buffer (6–12 frames by default).

Next steps (optional)
- Make min/target/max depth tunable via Kconfig.
- Report underrun/overflow counters via /status and UI.
- Gate start of playback until depth >= target (e.g., 3 frames) to avoid early underruns.

