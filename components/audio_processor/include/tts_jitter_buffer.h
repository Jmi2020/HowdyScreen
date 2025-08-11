#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tts_jitter_buffer_t tts_jitter_buffer_t;

// Create a jitter buffer for fixed-size PCM frames
// frame_samples: samples per frame (e.g., 320 for 20 ms @ 16 kHz)
// min_frames, max_frames: target and absolute capacity in frames
tts_jitter_buffer_t *tts_jb_create(size_t frame_samples, size_t min_frames, size_t max_frames);

// Destroy and free resources
void tts_jb_destroy(tts_jitter_buffer_t *jb);

// Reset (drop all queued data)
void tts_jb_reset(tts_jitter_buffer_t *jb);

// Push PCM samples. Accepts multiples of frame size; excess is buffered.
// Returns number of samples accepted. Drops oldest on overflow.
size_t tts_jb_push(tts_jitter_buffer_t *jb, const int16_t *samples, size_t sample_count);

// Pop exactly one frame into out buffer. If underrun, fills silence and returns false_underrun=true.
// Returns true when real audio provided; false when silence was provided due to underrun.
bool tts_jb_pop_frame(tts_jitter_buffer_t *jb, int16_t *out_frame, bool *false_underrun);

// Current queued frames
size_t tts_jb_depth(tts_jitter_buffer_t *jb);

#ifdef __cplusplus
}
#endif

