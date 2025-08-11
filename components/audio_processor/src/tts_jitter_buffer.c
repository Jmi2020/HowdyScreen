#include "tts_jitter_buffer.h"
#include <stdlib.h>
#include <string.h>

typedef struct tts_jitter_buffer_t {
    size_t frame_samples;
    size_t frame_bytes;
    size_t capacity_frames;
    size_t head;   // read index
    size_t tail;   // write index
    size_t depth;  // frames queued
    int16_t *frames; // contiguous buffer: capacity_frames * frame_samples
    // partial accumulation buffer for non-frame-aligned pushes
    int16_t *accum;
    size_t accum_count;
} tts_jitter_buffer_t;

tts_jitter_buffer_t *tts_jb_create(size_t frame_samples, size_t min_frames, size_t max_frames)
{
    (void)min_frames; // For now, only use max capacity; playout target handled by caller timing
    if (frame_samples == 0 || max_frames == 0) return NULL;

    tts_jitter_buffer_t *jb = (tts_jitter_buffer_t *)calloc(1, sizeof(*jb));
    if (!jb) return NULL;

    jb->frame_samples = frame_samples;
    jb->frame_bytes = frame_samples * sizeof(int16_t);
    jb->capacity_frames = max_frames;
    jb->frames = (int16_t *)malloc(jb->capacity_frames * jb->frame_bytes);
    jb->accum = (int16_t *)malloc(jb->frame_bytes);
    if (!jb->frames || !jb->accum) {
        free(jb->frames);
        free(jb->accum);
        free(jb);
        return NULL;
    }
    return jb;
}

void tts_jb_destroy(tts_jitter_buffer_t *jb)
{
    if (!jb) return;
    free(jb->frames);
    free(jb->accum);
    free(jb);
}

void tts_jb_reset(tts_jitter_buffer_t *jb)
{
    if (!jb) return;
    jb->head = jb->tail = jb->depth = 0;
    jb->accum_count = 0;
}

static void push_frame(tts_jitter_buffer_t *jb, const int16_t *frame)
{
    if (jb->depth == jb->capacity_frames) {
        // overflow: drop oldest by advancing head
        jb->head = (jb->head + 1) % jb->capacity_frames;
        jb->depth--;
    }
    int16_t *dst = jb->frames + (jb->tail * jb->frame_samples);
    memcpy(dst, frame, jb->frame_bytes);
    jb->tail = (jb->tail + 1) % jb->capacity_frames;
    jb->depth++;
}

size_t tts_jb_push(tts_jitter_buffer_t *jb, const int16_t *samples, size_t sample_count)
{
    if (!jb || !samples || sample_count == 0) return 0;

    size_t consumed = 0;

    // If we have partial accumulation, fill to frame
    if (jb->accum_count > 0) {
        size_t need = jb->frame_samples - jb->accum_count;
        size_t to_copy = (sample_count < need) ? sample_count : need;
        memcpy(jb->accum + jb->accum_count, samples, to_copy * sizeof(int16_t));
        jb->accum_count += to_copy;
        samples += to_copy;
        sample_count -= to_copy;
        consumed += to_copy;
        if (jb->accum_count == jb->frame_samples) {
            push_frame(jb, jb->accum);
            jb->accum_count = 0;
        }
    }

    // Push full frames directly
    while (sample_count >= jb->frame_samples) {
        push_frame(jb, samples);
        samples += jb->frame_samples;
        sample_count -= jb->frame_samples;
        consumed += jb->frame_samples;
    }

    // Store remainder in accum
    if (sample_count > 0) {
        memcpy(jb->accum, samples, sample_count * sizeof(int16_t));
        jb->accum_count = sample_count;
        consumed += sample_count;
    }

    return consumed;
}

bool tts_jb_pop_frame(tts_jitter_buffer_t *jb, int16_t *out_frame, bool *false_underrun)
{
    if (!jb || !out_frame) return false;
    if (jb->depth == 0) {
        // underrun: provide silence
        memset(out_frame, 0, jb->frame_bytes);
        if (false_underrun) *false_underrun = true;
        return false;
    }
    int16_t *src = jb->frames + (jb->head * jb->frame_samples);
    memcpy(out_frame, src, jb->frame_bytes);
    jb->head = (jb->head + 1) % jb->capacity_frames;
    jb->depth--;
    if (false_underrun) *false_underrun = false;
    return true;
}

size_t tts_jb_depth(tts_jitter_buffer_t *jb)
{
    if (!jb) return 0;
    return jb->depth;
}

