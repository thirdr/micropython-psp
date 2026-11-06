// pico3d on the GE (pico3d_ge.cpp): the calls pspdisplay and the launcher use.
#ifndef PICO3D_GE_H
#define PICO3D_GE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;       // pico3d draws on the GE (false: always the CPU)
    bool queue;         // render()/draw() queue until a sync
    uint32_t batches;   // trips to VRAM and back
    uint32_t draws;     // meshes the GE drew
    uint32_t triangles; // triangles sent to the GE
    uint32_t fallbacks; // draws the CPU did instead
    uint32_t busy_us;   // time spent waiting for the GE
    uint32_t last_us;   // the last batch's wait
} pico3d_ge_stats_t;

// Turn the GE or queuing on or off; -1 leaves a setting as it is. Either
// change finishes anything queued first.
void pico3d_ge_set(int enabled, int queue);

// Finish anything queued: the GE draws it and the image gets the result.
void pico3d_ge_sync(void);

// Between scripts: finish anything queued, back to the defaults (GE on,
// queuing off), stats cleared.
void pico3d_ge_reset(void);

void pico3d_ge_get_stats(pico3d_ge_stats_t *out);

// Copy w x h pixels (rows src_stride and dst_stride pixels apart) into VRAM
// with the GE, finishing anything queued first. False, having done nothing,
// if the GE can't (addresses not 16-byte aligned, strides not multiples of 8).
bool pico3d_ge_copy_to_vram(const void *src, int src_stride, void *dst, int dst_stride,
                            int w, int h);

#ifdef __cplusplus
}
#endif

#endif // PICO3D_GE_H
