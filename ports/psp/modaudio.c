// audio: sound for scripts. WAV and MP3 files, and raw samples from Python.
//
//   s = audio.play("beep.wav", loop=False, volume=1.0)   -> Sound
//   s.stop(); s.pause(); s.resume(); s.volume = 0.5; s.playing
//   audio.volume(0.8); audio.stop()
//   out = audio.Stream(rate=22050, channels=1, bits=16)
//   out.write(buf); out.space(); out.close()
//
// A mixer thread mixes up to VOICES sounds and streams into one hardware
// channel, 16-bit stereo at 44.1 kHz, resampling each to that rate. WAV data
// is loaded into the GC heap; MP3s stream from the file through the PSP's
// hardware decoder (at most MP3_SLOTS at once). A play() with every voice
// busy stops the oldest sound. Buffers the mixer reads are held in root
// pointers, so a Sound dropped by the script keeps playing safely.
//
// The mixer thread never touches Python objects; the voice table is shared
// under a semaphore.
#include <string.h>

#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspmp3.h>
#include <psputility.h>

#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"

#define VOICES          (8)
#define MP3_SLOTS       (2)
#define OUT_RATE        (44100)
#define OUT_FRAMES      (1024)      // per hardware buffer: about 23 ms
#define VOLUME_ONE      (256)
#define STREAM_BUFFER_MS_DEFAULT (500)

int __path_absolute(const char *in, char *out, int len);

typedef enum { KIND_NONE, KIND_PCM, KIND_STREAM, KIND_MP3 } voice_kind_t;

typedef struct {
    int handle;     // sceMp3 handle, -1 if unused
    SceUID fd;
    // The decoder's buffers, sizes from the SDK sample.
    unsigned char stream_buf[16 * 1024] __attribute__((aligned(64)));
    unsigned char pcm_buf[16 * (1152 / 2)] __attribute__((aligned(64)));
} mp3_slot_t;

typedef struct {
    voice_kind_t kind;
    uint32_t gen;           // bumped each time the voice is reused
    uint32_t started;       // for "stop the oldest"
    bool paused;
    bool loop;
    int volume;             // 0..VOLUME_ONE
    int channels;           // 1 or 2
    int bits;               // 8 or 16 (MP3 and streams are 16... streams may be 8)
    uint32_t step;          // source frames per output frame, 16.16
    uint32_t frac;          // position within the current frame, 16.16
    // PCM: the whole sound. MP3: the last decoded chunk. Stream: ring buffer.
    const uint8_t *data;
    size_t frames;          // frames in data (PCM, MP3 chunk) or ring capacity (stream)
    size_t pos;             // next frame to play
    // Stream ring: frames written / read, counted up forever.
    volatile size_t written;
    volatile size_t read;
    bool closed;
    int mp3;                // mp3 slot index, or -1
} voice_t;

static voice_t voices[VOICES];
static mp3_slot_t mp3_slots[MP3_SLOTS];
static SceUID lock = -1;
static SceUID mixer_thread = -1;
static int audio_channel = -1;
static int master_volume = VOLUME_ONE;
static uint32_t start_counter;
static bool mp3_ready;

static int16_t out_buf[2][OUT_FRAMES * 2] __attribute__((aligned(64)));
static int32_t mix_buf[OUT_FRAMES * 2];

// 8 = VOICES: the declaration is copied into the VM state as text, where
// VOICES isn't defined.
MP_REGISTER_ROOT_POINTER(void *psp_audio_buffers[8]);
MP_STATIC_ASSERT(VOICES == 8);

static void take(void) {
    sceKernelWaitSema(lock, 1, NULL);
}

static void give(void) {
    sceKernelSignalSema(lock, 1);
}

// MP3: refill the decoder's stream buffer from the file. Returns bytes read.
static int mp3_fill(mp3_slot_t *slot) {
    unsigned char *dst;
    SceInt32 towrite, srcpos;
    if (sceMp3GetInfoToAddStreamData(slot->handle, &dst, &towrite, &srcpos) < 0) {
        return -1;
    }
    sceIoLseek32(slot->fd, srcpos, PSP_SEEK_SET);
    int n = sceIoRead(slot->fd, dst, towrite);
    if (n < 0) {
        return -1;
    }
    sceMp3NotifyAddStreamData(slot->handle, n);
    return n;
}

static void mp3_release(int i) {
    mp3_slot_t *slot = &mp3_slots[i];
    if (slot->handle >= 0) {
        sceMp3ReleaseMp3Handle(slot->handle);
        slot->handle = -1;
    }
    if (slot->fd >= 0) {
        sceIoClose(slot->fd);
        slot->fd = -1;
    }
}

// Ends a voice. Call with the lock held.
static void voice_free(int i) {
    voice_t *v = &voices[i];
    if (v->kind == KIND_MP3 && v->mp3 >= 0) {
        mp3_release(v->mp3);
    }
    v->kind = KIND_NONE;
    v->data = NULL;
    v->mp3 = -1;
    MP_STATE_VM(psp_audio_buffers)[i] = NULL;
}

// MP3: decode the next chunk into v->data. False at the end.
static bool mp3_next_chunk(voice_t *v) {
    mp3_slot_t *slot = &mp3_slots[v->mp3];
    for (int tries = 0; tries < 2; tries++) {
        if (sceMp3CheckStreamDataNeeded(slot->handle) > 0) {
            mp3_fill(slot);
        }
        SceShort16 *pcm;
        int bytes = sceMp3Decode(slot->handle, &pcm);
        if (bytes > 0) {
            v->data = (const uint8_t *)pcm;
            v->frames = bytes / (2 * v->channels);
            v->pos = 0;
            return true;
        }
        if (sceMp3CheckStreamDataNeeded(slot->handle) <= 0) {
            break;
        }
    }
    return false;
}

// One source frame as 16-bit left/right.
static inline void frame_at(const voice_t *v, const uint8_t *data, size_t i, int *l, int *r) {
    if (v->bits == 8) {
        const uint8_t *p = data + i * v->channels;
        *l = ((int)p[0] - 128) << 8;
        *r = v->channels == 2 ? ((int)p[1] - 128) << 8 : *l;
    } else {
        const int16_t *p = (const int16_t *)data + i * v->channels;
        *l = p[0];
        *r = v->channels == 2 ? p[1] : *l;
    }
}

// Mixes one voice into mix_buf. Returns false when the voice has finished.
static bool mix_voice(voice_t *v) {
    int vol = v->volume * master_volume / VOLUME_ONE;
    for (int n = 0; n < OUT_FRAMES; n++) {
        int l0, r0, l1, r1;
        if (v->kind == KIND_STREAM) {
            size_t avail = v->written - v->read;
            if (avail == 0) {
                if (v->closed) {
                    return false;
                }
                return true;    // underrun: silence until more is written
            }
            size_t i = v->read % v->frames;
            frame_at(v, v->data, i, &l0, &r0);
            if (avail > 1) {
                frame_at(v, v->data, (i + 1) % v->frames, &l1, &r1);
            } else {
                l1 = l0, r1 = r0;
            }
        } else {
            if (v->pos >= v->frames) {
                if (v->kind == KIND_MP3) {
                    if (!mp3_next_chunk(v)) {
                        return false;
                    }
                } else if (v->loop) {
                    v->pos = 0;
                } else {
                    return false;
                }
            }
            frame_at(v, v->data, v->pos, &l0, &r0);
            if (v->pos + 1 < v->frames) {
                frame_at(v, v->data, v->pos + 1, &l1, &r1);
            } else if (v->kind == KIND_PCM && v->loop) {
                frame_at(v, v->data, 0, &l1, &r1);
            } else {
                l1 = l0, r1 = r0;
            }
        }
        // Linear interpolation between the two frames.
        int f = v->frac >> 8;   // 0..255
        int l = l0 + (((l1 - l0) * f) >> 8);
        int r = r0 + (((r1 - r0) * f) >> 8);
        mix_buf[n * 2] += l * vol / VOLUME_ONE;
        mix_buf[n * 2 + 1] += r * vol / VOLUME_ONE;

        v->frac += v->step;
        size_t advance = v->frac >> 16;
        v->frac &= 0xffff;
        if (v->kind == KIND_STREAM) {
            size_t avail = v->written - v->read;
            v->read += advance < avail ? advance : avail;
        } else {
            v->pos += advance;
        }
    }
    return true;
}

static int mixer(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int which = 0;
    for (;;) {
        take();
        bool any = false;
        memset(mix_buf, 0, sizeof(mix_buf));
        for (int i = 0; i < VOICES; i++) {
            voice_t *v = &voices[i];
            if (v->kind == KIND_NONE || v->paused) {
                continue;
            }
            any = true;
            if (!mix_voice(v)) {
                voice_free(i);
            }
        }
        give();
        if (!any) {
            // Nothing playing: idle instead of feeding silence.
            sceKernelDelayThread(10 * 1000);
            continue;
        }
        int16_t *out = out_buf[which];
        for (int n = 0; n < OUT_FRAMES * 2; n++) {
            int s = mix_buf[n];
            out[n] = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
        }
        sceAudioOutputPannedBlocking(audio_channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, out);
        which ^= 1;
    }
    return 0;
}

static void audio_init(void) {
    if (mixer_thread >= 0) {
        return;
    }
    for (int i = 0; i < MP3_SLOTS; i++) {
        mp3_slots[i].handle = -1;
        mp3_slots[i].fd = -1;
    }
    for (int i = 0; i < VOICES; i++) {
        voices[i].mp3 = -1;
    }
    lock = sceKernelCreateSema("audio", 0, 1, 1, NULL);
    audio_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, OUT_FRAMES, PSP_AUDIO_FORMAT_STEREO);
    if (lock < 0 || audio_channel < 0) {
        mp_raise_OSError(MP_EIO);
    }
    // Above the main thread (0x20), so the sound doesn't stutter while a
    // script is busy.
    mixer_thread = sceKernelCreateThread("audio mixer", mixer, 0x12, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (mixer_thread < 0) {
        mp_raise_OSError(MP_EIO);
    }
    sceKernelStartThread(mixer_thread, 0, NULL);
}

// A free voice, or the oldest one stopped to make room. Call with the lock held.
static int voice_claim(void) {
    int best = 0;
    for (int i = 0; i < VOICES; i++) {
        if (voices[i].kind == KIND_NONE) {
            best = i;
            goto found;
        }
        if (voices[i].started < voices[best].started) {
            best = i;
        }
    }
    voice_free(best);
found:
    voices[best].gen++;
    voices[best].started = ++start_counter;
    voices[best].paused = false;
    voices[best].frac = 0;
    voices[best].pos = 0;
    voices[best].mp3 = -1;
    return best;
}

static mp_int_t volume_arg(mp_obj_t v) {
    mp_float_t f = mp_obj_get_float(v);
    if (f < 0 || f > 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("volume must be 0.0 to 1.0"));
    }
    return (mp_int_t)(f * VOLUME_ONE + MICROPY_FLOAT_CONST(0.5));
}

// Sound

typedef struct {
    mp_obj_base_t base;
    int voice;
    uint32_t gen;
} audio_sound_obj_t;

static const mp_obj_type_t audio_sound_type;

// The voice, if this Sound still owns it.
static voice_t *sound_voice(audio_sound_obj_t *self) {
    voice_t *v = &voices[self->voice];
    return v->gen == self->gen && v->kind != KIND_NONE ? v : NULL;
}

static mp_obj_t sound_stop(mp_obj_t self_in) {
    audio_sound_obj_t *self = MP_OBJ_TO_PTR(self_in);
    take();
    if (sound_voice(self)) {
        voice_free(self->voice);
    }
    give();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sound_stop_obj, sound_stop);

static void sound_set_paused(audio_sound_obj_t *self, bool paused) {
    take();
    voice_t *v = sound_voice(self);
    if (v) {
        v->paused = paused;
    }
    give();
}

static mp_obj_t sound_pause(mp_obj_t self_in) {
    sound_set_paused(MP_OBJ_TO_PTR(self_in), true);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sound_pause_obj, sound_pause);

static mp_obj_t sound_resume(mp_obj_t self_in) {
    sound_set_paused(MP_OBJ_TO_PTR(self_in), false);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sound_resume_obj, sound_resume);

// .playing (read-only) and .volume (read/write).
static void sound_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    audio_sound_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        voice_t *v = sound_voice(self);
        if (attr == MP_QSTR_playing) {
            dest[0] = mp_obj_new_bool(v != NULL && !v->paused);
        } else if (attr == MP_QSTR_volume) {
            dest[0] = mp_obj_new_float(v ? (mp_float_t)v->volume / VOLUME_ONE : 0);
        } else {
            dest[1] = MP_OBJ_SENTINEL;   // look in locals_dict
        }
    } else if (dest[1] != MP_OBJ_NULL && attr == MP_QSTR_volume) {
        mp_int_t vol = volume_arg(dest[1]);
        take();
        voice_t *v = sound_voice(self);
        if (v) {
            v->volume = vol;
        }
        give();
        dest[0] = MP_OBJ_NULL;
    }
}

static const mp_rom_map_elem_t sound_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&sound_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_pause), MP_ROM_PTR(&sound_pause_obj) },
    { MP_ROM_QSTR(MP_QSTR_resume), MP_ROM_PTR(&sound_resume_obj) },
};
static MP_DEFINE_CONST_DICT(sound_locals, sound_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    audio_sound_type,
    MP_QSTR_Sound,
    MP_TYPE_FLAG_NONE,
    attr, sound_attr,
    locals_dict, &sound_locals
    );

static mp_obj_t sound_new(int voice) {
    audio_sound_obj_t *self = mp_obj_malloc(audio_sound_obj_t, &audio_sound_type);
    self->voice = voice;
    self->gen = voices[voice].gen;
    return MP_OBJ_FROM_PTR(self);
}

// play()

static uint32_t step_for(int rate) {
    return (uint32_t)(((uint64_t)rate << 16) / OUT_RATE);
}

static uint32_t le32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p) {
    return p[0] | (p[1] << 8);
}

// Reads a whole file into the GC heap.
static uint8_t *read_file(const char *path, size_t *len) {
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    SceOff size = sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    uint8_t *buf = m_new_maybe(uint8_t, size);
    if (buf == NULL) {
        sceIoClose(fd);
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("WAV file too big"));
    }
    int n = sceIoRead(fd, buf, size);
    sceIoClose(fd);
    if (n != size) {
        mp_raise_OSError(MP_EIO);
    }
    *len = size;
    return buf;
}

static void play_wav(int i, const char *path, bool loop, int volume) {
    size_t len;
    uint8_t *file = read_file(path, &len);
    if (len < 12 || memcmp(file, "RIFF", 4) != 0 || memcmp(file + 8, "WAVE", 4) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("not a WAV file"));
    }
    int channels = 0, rate = 0, bits = 0;
    const uint8_t *data = NULL;
    size_t data_len = 0;
    for (size_t p = 12; p + 8 <= len;) {
        uint32_t size = le32(file + p + 4);
        const uint8_t *body = file + p + 8;
        if (size > len - p - 8) {
            size = len - p - 8;
        }
        if (memcmp(file + p, "fmt ", 4) == 0 && size >= 16) {
            if (le16(body) != 1) {
                mp_raise_ValueError(MP_ERROR_TEXT("WAV must be PCM"));
            }
            channels = le16(body + 2);
            rate = le32(body + 4);
            bits = le16(body + 14);
        } else if (memcmp(file + p, "data", 4) == 0) {
            data = body;
            data_len = size;
        }
        p += 8 + size + (size & 1);
    }
    if (data == NULL || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate < 1000 || rate > 96000) {
        mp_raise_ValueError(MP_ERROR_TEXT("WAV must be 8 or 16-bit, mono or stereo"));
    }
    take();
    voice_t *v = &voices[i];
    v->kind = KIND_PCM;
    v->loop = loop;
    v->volume = volume;
    v->channels = channels;
    v->bits = bits;
    v->step = step_for(rate);
    v->data = data;
    v->frames = data_len / (channels * bits / 8);
    MP_STATE_VM(psp_audio_buffers)[i] = file;
    give();
}

static void play_mp3(int i, const char *path, bool loop, int volume) {
    if (!mp3_ready) {
        if (sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC) < 0 || sceUtilityLoadModule(PSP_MODULE_AV_MP3) < 0 || sceMp3InitResource() < 0) {
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("can't start the MP3 decoder"));
        }
        mp3_ready = true;
    }
    take();
    // A free decoder slot, or the one used by the oldest MP3.
    int s = -1;
    for (int k = 0; k < MP3_SLOTS && s < 0; k++) {
        if (mp3_slots[k].handle < 0) {
            s = k;
        }
    }
    if (s < 0) {
        int oldest = -1;
        for (int k = 0; k < VOICES; k++) {
            if (voices[k].kind == KIND_MP3 && (oldest < 0 || voices[k].started < voices[oldest].started)) {
                oldest = k;
            }
        }
        s = voices[oldest].mp3;
        voice_free(oldest);
    }
    mp3_slot_t *slot = &mp3_slots[s];
    slot->fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (slot->fd < 0) {
        give();
        mp_raise_OSError(MP_ENOENT);
    }
    SceMp3InitArg init = {
        .mp3StreamStart = 0,
        .mp3StreamEnd = sceIoLseek32(slot->fd, 0, PSP_SEEK_END),
        .mp3Buf = slot->stream_buf,
        .mp3BufSize = sizeof(slot->stream_buf),
        .pcmBuf = slot->pcm_buf,
        .pcmBufSize = sizeof(slot->pcm_buf),
    };
    slot->handle = sceMp3ReserveMp3Handle(&init);
    if (slot->handle < 0 || mp3_fill(slot) < 0 || sceMp3Init(slot->handle) < 0) {
        mp3_release(s);
        give();
        mp_raise_ValueError(MP_ERROR_TEXT("can't play this MP3"));
    }
    sceMp3SetLoopNum(slot->handle, loop ? -1 : 0);
    voice_t *v = &voices[i];
    v->kind = KIND_MP3;
    v->mp3 = s;
    v->loop = loop;
    v->volume = volume;
    // The decoder always outputs stereo, copying a mono file to both sides
    // (the SDK sample's bytes / (2 * channels) is wrong for mono).
    v->channels = 2;
    v->bits = 16;
    v->step = step_for(sceMp3GetSamplingRate(slot->handle));
    v->data = NULL;
    v->frames = 0;
    give();
}

static bool ends_with(const char *s, size_t len, const char *ext) {
    size_t n = strlen(ext);
    if (len < n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[len - n + i];
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
        if (c != ext[i]) {
            return false;
        }
    }
    return true;
}

static mp_obj_t audio_play(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_path, ARG_loop, ARG_volume };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_path, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_loop, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_volume, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    size_t len;
    const char *name = mp_obj_str_get_data(args[ARG_path].u_obj, &len);
    bool wav = ends_with(name, len, ".wav");
    if (!wav && !ends_with(name, len, ".mp3")) {
        mp_raise_ValueError(MP_ERROR_TEXT("play() takes .wav or .mp3 files"));
    }
    int volume = args[ARG_volume].u_obj == MP_OBJ_NULL ? VOLUME_ONE : volume_arg(args[ARG_volume].u_obj);
    char path[1024];
    if (__path_absolute(name, path, sizeof(path)) < 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    audio_init();
    take();
    int i = voice_claim();
    give();
    if (wav) {
        play_wav(i, path, args[ARG_loop].u_bool, volume);
    } else {
        play_mp3(i, path, args[ARG_loop].u_bool, volume);
    }
    return sound_new(i);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(audio_play_obj, 1, audio_play);

// Stream

typedef struct {
    mp_obj_base_t base;
    int voice;
    uint32_t gen;
    int frame_bytes;
} audio_stream_obj_t;

static const mp_obj_type_t audio_stream_type;

static voice_t *stream_voice(audio_stream_obj_t *self) {
    voice_t *v = &voices[self->voice];
    return v->gen == self->gen && v->kind == KIND_STREAM ? v : NULL;
}

static mp_obj_t stream_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_rate, ARG_channels, ARG_bits, ARG_buffer_ms };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_rate, MP_ARG_INT, {.u_int = 22050} },
        { MP_QSTR_channels, MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_bits, MP_ARG_INT, {.u_int = 16} },
        { MP_QSTR_buffer_ms, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = STREAM_BUFFER_MS_DEFAULT} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    mp_int_t rate = args[ARG_rate].u_int, channels = args[ARG_channels].u_int, bits = args[ARG_bits].u_int;
    mp_int_t ms = args[ARG_buffer_ms].u_int;
    if (rate < 1000 || rate > 96000 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || ms < 20 || ms > 10000) {
        mp_raise_ValueError(MP_ERROR_TEXT("rate 1000-96000, channels 1 or 2, bits 8 or 16, buffer_ms 20-10000"));
    }
    int frame_bytes = channels * bits / 8;
    size_t frames = rate * ms / 1000;
    uint8_t *ring = m_new(uint8_t, frames * frame_bytes);
    audio_init();
    audio_stream_obj_t *self = mp_obj_malloc(audio_stream_obj_t, type);
    take();
    int i = voice_claim();
    voice_t *v = &voices[i];
    v->kind = KIND_STREAM;
    v->loop = false;
    v->volume = VOLUME_ONE;
    v->channels = channels;
    v->bits = bits;
    v->step = step_for(rate);
    v->data = ring;
    v->frames = frames;
    v->written = 0;
    v->read = 0;
    v->closed = false;
    MP_STATE_VM(psp_audio_buffers)[i] = ring;
    self->voice = i;
    self->gen = v->gen;
    self->frame_bytes = frame_bytes;
    give();
    return MP_OBJ_FROM_PTR(self);
}

// space(): bytes write() would take without waiting.
static mp_obj_t stream_space(mp_obj_t self_in) {
    audio_stream_obj_t *self = MP_OBJ_TO_PTR(self_in);
    voice_t *v = stream_voice(self);
    if (v == NULL) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    return mp_obj_new_int((v->frames - (v->written - v->read)) * self->frame_bytes);
}
static MP_DEFINE_CONST_FUN_OBJ_1(stream_space_obj, stream_space);

// write(buf): queues the samples, waiting while the queue is full. Returns
// the bytes taken (all of them, unless the stream was closed or stopped).
static mp_obj_t stream_write(mp_obj_t self_in, mp_obj_t buf_in) {
    audio_stream_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_in, &buf, MP_BUFFER_READ);
    size_t total = buf.len / self->frame_bytes, done = 0;
    const uint8_t *src = buf.buf;
    while (done < total) {
        // Under the lock, so the voice can't be taken over mid-copy.
        take();
        voice_t *v = stream_voice(self);
        if (v == NULL || v->closed) {
            give();
            break;
        }
        size_t space = v->frames - (v->written - v->read);
        if (space == 0) {
            give();
            mp_event_wait_ms(2);   // the mixer drains about 23 ms at a time
            continue;
        }
        size_t n = total - done < space ? total - done : space;
        // Copy in up to two pieces around the end of the ring.
        size_t at = v->written % v->frames;
        size_t first = n < v->frames - at ? n : v->frames - at;
        uint8_t *ring = (uint8_t *)v->data;
        memcpy(ring + at * self->frame_bytes, src + done * self->frame_bytes, first * self->frame_bytes);
        memcpy(ring, src + (done + first) * self->frame_bytes, (n - first) * self->frame_bytes);
        v->written += n;
        done += n;
        give();
    }
    return mp_obj_new_int(done * self->frame_bytes);
}
static MP_DEFINE_CONST_FUN_OBJ_2(stream_write_obj, stream_write);

// close(): plays out what's queued, then frees the voice.
static mp_obj_t stream_close(mp_obj_t self_in) {
    audio_stream_obj_t *self = MP_OBJ_TO_PTR(self_in);
    take();
    voice_t *v = stream_voice(self);
    if (v) {
        v->closed = true;
    }
    give();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(stream_close_obj, stream_close);

static const mp_rom_map_elem_t stream_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_space), MP_ROM_PTR(&stream_space_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&stream_close_obj) },
};
static MP_DEFINE_CONST_DICT(stream_locals, stream_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    audio_stream_type,
    MP_QSTR_Stream,
    MP_TYPE_FLAG_NONE,
    make_new, stream_make_new,
    locals_dict, &stream_locals
    );

// Module functions

// volume([v]): the master volume, 0.0-1.0.
static mp_obj_t audio_volume(size_t n_args, const mp_obj_t *args) {
    if (n_args == 1) {
        master_volume = volume_arg(args[0]);
    }
    return mp_obj_new_float((mp_float_t)master_volume / VOLUME_ONE);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(audio_volume_obj, 0, 1, audio_volume);

// Stops every sound and stream, and puts the master volume back. Also used
// by the launcher when a script ends.
void psp_audio_reset(void) {
    if (mixer_thread < 0) {
        return;
    }
    take();
    for (int i = 0; i < VOICES; i++) {
        if (voices[i].kind != KIND_NONE) {
            voice_free(i);
        }
    }
    master_volume = VOLUME_ONE;
    give();
}

static mp_obj_t audio_stop(void) {
    psp_audio_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_stop_obj, audio_stop);

static const mp_rom_map_elem_t audio_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_audio) },
    { MP_ROM_QSTR(MP_QSTR_play), MP_ROM_PTR(&audio_play_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&audio_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_volume), MP_ROM_PTR(&audio_volume_obj) },
    { MP_ROM_QSTR(MP_QSTR_Stream), MP_ROM_PTR(&audio_stream_type) },
    { MP_ROM_QSTR(MP_QSTR_Sound), MP_ROM_PTR(&audio_sound_type) },
    { MP_ROM_QSTR(MP_QSTR_VOICES), MP_ROM_INT(VOICES) },
};
static MP_DEFINE_CONST_DICT(audio_module_globals, audio_module_globals_table);

const mp_obj_module_t audio_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&audio_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_audio, audio_module);
