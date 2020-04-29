#include "audio_process_util.h"

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include <atomic.h>

#include <webrtc/modules/audio_processing/aec/echo_cancellation.h>
#include <webrtc/modules/audio_processing/ns/noise_suppression.h>
#include <webrtc/modules/audio_processing/agc/legacy/gain_control.h>

#include "resample.h"
#include "audio_splitting_filter_buffer.h"

#include "ring_buffer.h"




#if 1
#define AUDIO_STAREM_DEBUG_EXT(file_name, fp_name, buf, size)                   \
    do{                                                                         \
        static FILE *fp_name = NULL;                                            \
        if(!fp_name){                                                           \
            fp_name = fopen((file_name), "wb");                                 \
        }                                                                       \
                                                                                \
        if(!fp_name){                                                           \
            break;                                                              \
        }                                                                       \
                                                                                \
        size_t res = fwrite((buf), 1, (size), fp_name);                         \
        if(res != size){                                                        \
            fprintf(stderr, "Failed to save file(%s).\n", (file_name));         \
        }                                                                       \
                                                                                \
    }while(0)
#else
    #define AUDIO_STAREM_DEBUG_EXT(...)
#endif

// #define WEBRTC_MOBILE

#ifdef WEBRTC_MOBILE
#include <webrtc/modules/audio_processing/ns/noise_suppression_x.h>
#include "webrtc/modules/audio_processing/aecm/echo_control_mobile.h"

#define NsHandle NsxHandle
#define WebRtcNs_Create WebRtcNsx_Create
#define WebRtcNs_Init WebRtcNsx_Init
#define WebRtcNs_Free WebRtcNsx_Free

#define WebRtcAec_Create WebRtcAecm_Create
#define WebRtcAec_Init(handle, clock, sclock) WebRtcAecm_Init(handle, clock)
#define WebRtcAec_Free WebRtcAecm_Free
#define WebRtcAec_get_error_code WebRtcAecm_get_error_code
#define WebRtcAec_enable_delay_agnostic(core, enable)
#define WebRtcAec_set_config WebRtcAecm_set_config
// #define WebRtcAec_BufferFarend WebRtcAecm_BufferFarend
#define AecConfig AecmConfig

typedef short sample;

#else
#include <webrtc/modules/audio_processing/ns/noise_suppression.h>

typedef float sample;
#endif

#define MAX_NUM_BANDS      (8)
#define MAX_NUM_CHANNELS   (2)
#define WEBRTC_PROC_LEN    (160)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(ARRAY) (sizeof((ARRAY)) / sizeof((ARRAY)[0]))
#endif

#if 0
#define audio_proc_log_warn(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
#define audio_proc_log_info audio_proc_log_warn
#else
#include "w_log.h"

#define audio_proc_log_warn LOGW
#define audio_proc_log_info LOGI
#endif


#define check_try_retrun(ctx, ret) do{              \
    if(!ctx || !ctx->inited){                       \
        return (ret);                               \
    }                                               \
}while(0)


enum {
AP_CTX_PLAYBACK_MODE = 0,
AP_CTX_RECORD_MODE,
AP_CTX_OUT_MODE,
NR_AP_CTX_AUDIO_MODE
};

struct _audio_proc_ctx {
    int            inited;
    int            samples_per_proc;
    int            bytes_per_sample;
    int            bytes_per_proc;

    void          *aec[MAX_NUM_CHANNELS];

    NsHandle      *ns[MAX_NUM_CHANNELS]; // NsxHandle
    void          *agc[MAX_NUM_CHANNELS];

    bool           ns_enable;
    bool           agc_enable;
    bool           aec_enable;
    bool           playback_ready;
    bool           record_ready;

    int            agc_mic_level;
    int            ns_policy;
    
    int            channels;

    int            num_frames;

    struct rs_data *resampler[NR_AP_CTX_AUDIO_MODE]; // one for playback, one for record, one for output

    int            aec_delay; // in ms
    uint32_t       nearend_freq, farend_freq, proc_freq;
    uint32_t       nearend_channels, farend_channels; // webrtc only support one mono when process aec
    ring_buffer_t *farend_rbuf; // farend ring buffer
    ring_buffer_t *nearend_rbuf; // nearend ring buffer
    uint8_t       *end_buf_for_rbuf;

    int            num_bands;

    audio_splitting_filter_buffer* farend_sfb[MAX_NUM_CHANNELS];
    audio_splitting_filter_buffer* nearend_sfb[MAX_NUM_CHANNELS];
    audio_splitting_filter_buffer* out_sfb[MAX_NUM_CHANNELS];

    int16_t        farend_chs[MAX_NUM_CHANNELS][480];
    int16_t        nearend_chs[MAX_NUM_CHANNELS][480];
    int16_t        out_chs[MAX_NUM_CHANNELS][480];

    volatile int   ref;

    WebRtcAgcConfig agc_conf;
    AecConfig       aec_conf;
};

audio_proc_ctx* ap_ctx_create()
{
    audio_proc_ctx* ctx;
    
    ctx = (audio_proc_ctx*)malloc(sizeof(audio_proc_ctx));
    if(ctx){
        memset(ctx, 0, sizeof(*ctx));
    }
    
    return ctx;
}

void ap_ctx_free(audio_proc_ctx* ap_ctx)
{
    free(ap_ctx);
}

audio_proc_ctx* ap_ctx_get_default()
{
    static audio_proc_ctx ctx;

    return &ctx;
}

int ap_ctx_ref(audio_proc_ctx* ap_ctx)
{
    check_try_retrun(ap_ctx, 0);

    atomic_add(1, &ap_ctx->ref);

    return ap_ctx->ref;
}

int ap_ctx_unref(audio_proc_ctx* ap_ctx)
{
    check_try_retrun(ap_ctx, -1);

    atomic_sub(1, &ap_ctx->ref);

    // !!! TODO !!! should be atomic also(atomic get/read) or with mutex
    if(!ap_ctx->ref){
        ap_ctx_fini(ap_ctx);
        
        return 0;
    }else{
        return ap_ctx->ref;
    }
}

int ap_ctx_set_aec_config(audio_proc_ctx* ap_ctx)
{
    int rc;

    check_try_retrun(ap_ctx, -1);

    for (int i = 0; i < ap_ctx->channels; ++i) {
        rc = WebRtcAec_set_config(ap_ctx->aec[i], ap_ctx->aec_conf);
        if (rc != 0) {
            audio_proc_log_warn("Failed to set aec %d config", i);
            return -1;
        }
    }

    return 0;
}

int ap_ctx_init(audio_proc_ctx* ap_ctx, uint32_t channels,
    uint32_t nearend_freq, uint32_t farend_freq, uint32_t aec_delay)
{
    int rc;

    if (ap_ctx->inited) {
        audio_proc_log_warn("audio process context is inited %d, channels %d, farend freq %d, nearend freq %d",
            ap_ctx->inited, channels, farend_freq, nearend_freq);
        return ap_ctx->inited;
    }

    ap_ctx->bytes_per_sample = sizeof(sample);
    ap_ctx->samples_per_proc = (nearend_freq > 8000) ? 160 : 80;
    ap_ctx->bytes_per_proc = ap_ctx->samples_per_proc * ap_ctx->bytes_per_sample;
    ap_ctx->aec_delay = aec_delay; // ~100 ms

    static int perm_freqs[] = {8000, 16000, 32000, 48000};
    bool vaild = false;

    for(int i = 0; i < ARRAY_SIZE(perm_freqs); ++i){
        if(nearend_freq == perm_freqs[i]){
            vaild = true;
            break;
        }
    }

#ifdef WEBRTC_MOBILE
    ap_ctx->proc_freq = 16000; // 8000;
#else
    ap_ctx->proc_freq = vaild ? MIN(48000, nearend_freq) : 32000;
#endif
    
    ap_ctx->nearend_freq = nearend_freq;
    ap_ctx->farend_freq = farend_freq;

    ap_ctx->channels = channels;

    ap_ctx->nearend_channels = channels;
    ap_ctx->farend_channels = channels;

    ap_ctx->num_frames = (80 * ap_ctx->proc_freq) / 8000;

    audio_proc_log_info("channels %d, nearend_freq %d, proc_freq %d, farend_freq %d, aec_delay %d",
        channels, ap_ctx->proc_freq, nearend_freq, farend_freq, aec_delay);

    #define RING_BUFFER_SIZE (1024 * 1024 * 8)
    ap_ctx->end_buf_for_rbuf = (uint8_t *)malloc(RING_BUFFER_SIZE * 2);
    if(!ap_ctx->end_buf_for_rbuf){
        audio_proc_log_warn("Failed to alloc buffer for ring buffer");
        return -1;
    }

    ap_ctx->farend_rbuf = ring_buffer_create();
    ap_ctx->nearend_rbuf = ring_buffer_create();

    if (!ap_ctx->farend_rbuf || !ap_ctx->nearend_rbuf) {
        // !!! TODO !!! cleanup
        audio_proc_log_warn("Failed to create ring buffer");
        return -1;
    }

    ring_buffer_init(ap_ctx->farend_rbuf, ap_ctx->end_buf_for_rbuf, RING_BUFFER_SIZE);
    ring_buffer_init(ap_ctx->nearend_rbuf, ap_ctx->end_buf_for_rbuf + RING_BUFFER_SIZE, RING_BUFFER_SIZE);

    ap_ctx->num_bands = (ap_ctx->proc_freq <= 8000) ? 1 : (ap_ctx->proc_freq / 16000);

    
#ifdef WEBRTC_MOBILE
    ap_ctx->aec_conf.echoMode = 2;
    ap_ctx->aec_conf.cngMode = AecmTrue;
#else
    ap_ctx->aec_conf.nlpMode = kAecNlpModerate; // kAecNlpAggressive;
    ap_ctx->aec_conf.skewMode = kAecFalse;
    
#if SHOW_DELAY_METRICS
    ap_ctx->aec_conf.metricsMode = kAecTrue;
    ap_ctx->aec_conf.delay_logging = kAecTrue;
#else
    ap_ctx->aec_conf.metricsMode = kAecFalse;
    ap_ctx->aec_conf.delay_logging = kAecFalse;
#endif
    
#endif
    
    ap_ctx->agc_conf.compressionGaindB = 9;
    ap_ctx->agc_conf.limiterEnable = 1;
    ap_ctx->agc_conf.targetLevelDbfs = 3;

    ap_ctx->agc_mic_level = 128;
    
    ap_ctx->ns_policy = 3;

    ap_ctx->resampler[AP_CTX_PLAYBACK_MODE] = resample_init(ap_ctx->farend_freq, ap_ctx->proc_freq, 8192);
    ap_ctx->resampler[AP_CTX_RECORD_MODE] = resample_init(ap_ctx->nearend_freq, ap_ctx->proc_freq, 8192);
    ap_ctx->resampler[AP_CTX_OUT_MODE] = resample_init(ap_ctx->proc_freq, ap_ctx->nearend_freq, 8192);

    ap_ctx_reset(ap_ctx);

    ap_ctx->inited = 1;

    return 0;
}

void ap_ctx_fini(audio_proc_ctx* ap_ctx)
{
    // check_try_retrun(ap_ctx, void);
    if(!ap_ctx || !ap_ctx->inited){
        return;
    }

    for(int i = 0; i < ap_ctx->channels; ++i){
        if(ap_ctx->aec[i]){
            WebRtcAec_Free(ap_ctx->aec[i]);
            ap_ctx->aec[i] = NULL;
        }

        if(ap_ctx->agc[i]){
            WebRtcAgc_Free(ap_ctx->agc[i]);
            ap_ctx->agc[i] = NULL;
        }

        if(ap_ctx->ns[i]){
            WebRtcNs_Free(ap_ctx->ns[i]);
            ap_ctx->ns[i] = NULL;
        }
    }

    resample_close(ap_ctx->resampler[AP_CTX_PLAYBACK_MODE]);
    resample_close(ap_ctx->resampler[AP_CTX_RECORD_MODE]);
    resample_close(ap_ctx->resampler[AP_CTX_OUT_MODE]);

    ring_buffer_destroy(ap_ctx->farend_rbuf);
    ring_buffer_destroy(ap_ctx->nearend_rbuf);

    free(ap_ctx->end_buf_for_rbuf);

    memset(ap_ctx, 0, sizeof(*ap_ctx));

    audio_proc_log_info("audio proc context was freed");
}


static const char *apt_ctx_state[] = {"not ready", "ready"};

int ap_ctx_set_farend_is_ready(audio_proc_ctx* ap_ctx, int ready)
{
    bool state, old_state;

    check_try_retrun(ap_ctx, 0);

    state = !!ready;
    old_state = ap_ctx->playback_ready;

    audio_proc_log_warn("audio proc playback state (%s -> %s)", 
        apt_ctx_state[old_state], apt_ctx_state[state]);

    ap_ctx->playback_ready = state;

    return ready;
}

int ap_ctx_set_nearend_is_ready(audio_proc_ctx* ap_ctx, int ready)
{
    bool state, old_state;

    check_try_retrun(ap_ctx, 0);

    state = !!ready;
    old_state = ap_ctx->record_ready;

    audio_proc_log_warn("audio proc record state (%s -> %s)", 
        apt_ctx_state[old_state], apt_ctx_state[state]);
    
    ap_ctx->record_ready = state;
    
    return ready;
}

int ap_ctx_should_do_aec(audio_proc_ctx* ap_ctx)
{
    check_try_retrun(ap_ctx, 0);

    return (ap_ctx->playback_ready && ap_ctx->record_ready &&  ap_ctx->aec_enable);
}

int ap_ctx_set_ns_is_enable(audio_proc_ctx* ap_ctx, int enable)
{
    check_try_retrun(ap_ctx, 0);

    ap_ctx->ns_enable = enable;

    return enable;
}

int ap_ctx_set_agc_is_enable(audio_proc_ctx* ap_ctx, int enable)
{
    check_try_retrun(ap_ctx, 0);

    ap_ctx->agc_enable = enable;

    return enable;
}

int ap_ctx_reset(audio_proc_ctx* ap_ctx)
{
    int rc;

    check_try_retrun(ap_ctx, -1);

    rc = 0;

    ap_ctx->ns_enable = true;
    ap_ctx->aec_enable = true;
    ap_ctx->agc_enable = true;

    audio_proc_log_info("proc_freq %d, nearend_freq %d, farend_freq %d",
        ap_ctx->proc_freq, ap_ctx->nearend_freq, ap_ctx->farend_freq);

    for (int i = 0; i < ap_ctx->channels; ++i) {
        if(!ap_ctx->aec[i]){
            ap_ctx->aec[i] = WebRtcAec_Create();
        }

        if(ap_ctx->aec[i] && ap_ctx->aec_enable){
            
            rc = WebRtcAec_Init(ap_ctx->aec[i], ap_ctx->proc_freq, ap_ctx->proc_freq);
            if (rc != 0) {
                WebRtcAec_Free(ap_ctx->aec[i]);
                ap_ctx->aec[i] = NULL;
            
                ap_ctx->aec_enable = false;
                
                audio_proc_log_warn("Failed to init aec %d engin for %d", i, rc);
            }else{
                WebRtcAec_enable_delay_agnostic(WebRtcAec_aec_core(ap_ctx->aec[i]), 1);

                rc = WebRtcAec_set_config(ap_ctx->aec[i], ap_ctx->aec_conf);
                if (rc != 0) {
                    ap_ctx->aec_enable = false;
                    
                    audio_proc_log_warn("Failed to set aec %d config", i);
                }
            }

        }else{
            ap_ctx->aec_enable = false;
        }


        if(!ap_ctx->ns[i]){
            ap_ctx->ns[i] = WebRtcNs_Create();
        }

        if(ap_ctx->ns[i] && ap_ctx->ns_enable){
            
            rc = WebRtcNs_Init(ap_ctx->ns[i], ap_ctx->proc_freq);
            if (rc != 0) {
                WebRtcNs_Free(ap_ctx->ns[i]);
                ap_ctx->ns[i] = NULL;

                ap_ctx->ns_enable = false;

                audio_proc_log_warn("Failed to init ns %d engin for %d", i, rc);
            }
            else {
                WebRtcNs_set_policy((NsHandle*)ap_ctx->ns[i], ap_ctx->ns_policy);
            }
        }else{
            ap_ctx->ns_enable = false;
        }


        if (!ap_ctx->agc[i]){
            ap_ctx->agc[i] = WebRtcAgc_Create();
        }
        
        if (ap_ctx->agc[i] && ap_ctx->agc_enable) {

            rc = WebRtcAgc_Init(ap_ctx->agc[i], 0, 255, kAgcModeAdaptiveAnalog, ap_ctx->proc_freq);
            if(rc != 0){
                WebRtcAgc_Free(ap_ctx->agc[i]);
                ap_ctx->agc[i] = NULL;

                ap_ctx->agc_enable = false;

                audio_proc_log_warn("Failed to init agc %d engin for %d", i, rc);
            }else{
                WebRtcAgc_set_config(ap_ctx->agc[i], ap_ctx->agc_conf);
            }
           
        }else{
            ap_ctx->agc_enable = false;
        }
    }

    audio_proc_log_info("audio proc: aec enable %d, agc enable %d, ns enable %d", 
        ap_ctx->aec_enable, ap_ctx->agc_enable, ap_ctx->ns_enable);
    
    return rc;
}

int ap_ctx_cleanup(audio_proc_ctx* ap_ctx)
{

    for (int i = 0; i < ap_ctx->channels; ++i) {
        if (ap_ctx->aec[i]) {
            WebRtcAec_Free(ap_ctx->aec[i]);
            ap_ctx->aec[i] = NULL;
        }

        if (ap_ctx->ns[i]) {
            WebRtcNs_Free(ap_ctx->ns[i]);
            ap_ctx->ns[i] = NULL;
        }

        if (ap_ctx->agc[i]) {
            WebRtcAgc_Free(ap_ctx->agc[i]);
            ap_ctx->agc[i] = NULL;
        }
    }

    if (ap_ctx->farend_rbuf) {
        ring_buffer_destroy(ap_ctx->farend_rbuf);
        ap_ctx->farend_rbuf = NULL;
    }

    if (ap_ctx->nearend_rbuf) {
        ring_buffer_destroy(ap_ctx->nearend_rbuf);
        ap_ctx->nearend_rbuf = NULL;
    }

    return 0;
}

int ap_ctx_set_aec_farend_info(audio_proc_ctx* ap_ctx,
    uint32_t farend_freq, uint32_t farend_channels)
{
    check_try_retrun(ap_ctx, -1);

    // ap_ctx->proc_freq = farend_freq;
    ap_ctx->farend_freq = farend_freq;
    ap_ctx->farend_channels = farend_channels;

    return ap_ctx_reset(ap_ctx);
}

int ap_ctx_set_nearend_info(audio_proc_ctx* ap_ctx,
    uint32_t nearend_freq, uint32_t nearend_channels)
{
    check_try_retrun(ap_ctx, -1);

    ap_ctx->nearend_freq = nearend_freq;
    ap_ctx->nearend_channels = nearend_channels;

    return ap_ctx_reset(ap_ctx);
}
    
static int split_stereo_to_mono(const int16_t *stereo, size_t stereo_len, 
    int16_t *left, int16_t *right, size_t mono_size)
{
    int proc_len;
    int16_t *left_ch, *right_ch;

    proc_len = MIN(stereo_len, (mono_size * 2));

    proc_len &= -2;

    left_ch = left;
    right_ch = right;

    for (int i = 0; i < proc_len; ++i) {
        if (i % 2 == 0) {
            *left_ch++ = stereo[i];
        }
        else {
            *right_ch++ = stereo[i];
        }
    }

    return (proc_len / 2);
}

static int merge_mono_to_stereo(const int16_t* left, const int16_t* right, size_t mono_len, 
    int16_t* stereo, size_t stereo_size)
{
    int proc_len, stereo_idx;

    proc_len = MIN(mono_len, (stereo_size / 2));
    stereo_idx = 0;

    for (int i = 0; i < proc_len; ++i) {
        stereo[stereo_idx++] = left[i];
        stereo[stereo_idx++] = right[i];
    }

    return stereo_idx;
}

int ap_ctx_do_process(audio_proc_ctx* ap_ctx, const int16_t const *farend, 
    const int16_t const *nearend, size_t nr_samples, 
    int16_t dest[], size_t len)
{
    int      rc, avail_bytes, proced_samples, nproc;
    int      to_proc_samples, to_proc_bytes, to_proc_bytes_mono, proc_channels;
    int16_t *out;
    const int16_t *farend_sample, *nearend_sample;
    int is_stereo, need_do_aec;
    
    proced_samples = 0;
    out = dest;
    farend_sample = farend;
    nearend_sample = nearend;

    ap_ctx_ref(ap_ctx);

    avail_bytes = nr_samples * sizeof(int16_t);
    to_proc_samples = ap_ctx->num_frames;
    to_proc_bytes_mono = to_proc_samples * sizeof(int16_t);
    to_proc_bytes = to_proc_bytes_mono * ap_ctx->channels;
    proc_channels = 1; // seems like must be one channel (eg. mono)

    is_stereo = (ap_ctx->channels > 1);
    need_do_aec = (farend != NULL) && ap_ctx_should_do_aec(ap_ctx);

    if (avail_bytes < to_proc_bytes) {
        ap_ctx_unref(ap_ctx);
        
        audio_proc_log_warn("Failed to do aec for not enough data, avail %d to proc %d", 
            avail_bytes, to_proc_bytes);
        return -1;
    }

    assert(to_proc_bytes_mono <= sizeof(ap_ctx->farend_chs[0]));

    audio_proc_log_info("total samples %d, frame num %d, channels %d, bands %d, bandsize %d,"
        "to proc samples %d bytes %d %d, avail bytes %d, need do aec %d",
        nr_samples, ap_ctx->num_frames, ap_ctx->channels, ap_ctx->num_bands, ap_ctx->samples_per_proc, 
        to_proc_samples, to_proc_bytes_mono, to_proc_bytes, avail_bytes, need_do_aec);

    while ((avail_bytes >= to_proc_bytes) && (len > 0)) {

        nproc = 0;

        if (is_stereo) {
            if (need_do_aec && farend_sample) {
                split_stereo_to_mono(farend_sample, nr_samples, ap_ctx->farend_chs[0], ap_ctx->farend_chs[1], to_proc_samples);
            }
            
            split_stereo_to_mono(nearend_sample, nr_samples, ap_ctx->nearend_chs[0], ap_ctx->nearend_chs[1], to_proc_samples);
        }
        else {
            if (need_do_aec && farend_sample) {
                memcpy(ap_ctx->farend_chs[0], farend_sample, to_proc_bytes_mono);
            }
            
            memcpy(ap_ctx->nearend_chs[0], nearend_sample, to_proc_bytes_mono);
        }

        for (int i = 0; i < ap_ctx->channels; ++i) {

            if (!ap_ctx->farend_sfb[i]) {
                ap_ctx->farend_sfb[i] = audio_splitting_filter_buffer_create();
                audio_splitting_filter_buffer_init(ap_ctx->farend_sfb[i], proc_channels, ap_ctx->num_bands, ap_ctx->num_frames);
            }

            if (!ap_ctx->nearend_sfb[i]) {
                ap_ctx->nearend_sfb[i] = audio_splitting_filter_buffer_create();
                audio_splitting_filter_buffer_init(ap_ctx->nearend_sfb[i], proc_channels, ap_ctx->num_bands, ap_ctx->num_frames);
            }

            if (!ap_ctx->out_sfb[i]) {
                ap_ctx->out_sfb[i] = audio_splitting_filter_buffer_create();
                audio_splitting_filter_buffer_init(ap_ctx->out_sfb[i], proc_channels, ap_ctx->num_bands, ap_ctx->num_frames);
            }

            audio_splitting_filter_buffer_fill_data(ap_ctx->farend_sfb[i], (uint8_t*)ap_ctx->farend_chs[i], to_proc_bytes_mono);
            audio_splitting_filter_buffer_fill_data(ap_ctx->nearend_sfb[i], (uint8_t*)ap_ctx->nearend_chs[i], to_proc_bytes_mono);

#if 0
            AUDIO_STAREM_DEBUG_EXT("./when_aec_farend.pcm", when_aec_farend_fp,
                ap_ctx->farend_chs[i], to_proc_bytes_mono);

            AUDIO_STAREM_DEBUG_EXT("./when_aec_nearend.pcm", when_aec_nearend_fp,
                ap_ctx->nearend_chs[i], to_proc_bytes_mono);
#endif

            if (ap_ctx->agc_enable) {
                const int16_t* const* nearend_ibands_c = audio_splitting_filter_buffer_get_ibands_const(ap_ctx->nearend_sfb[i], 0);
                int16_t* const* nearend_ibands = audio_splitting_filter_buffer_get_ibands(ap_ctx->nearend_sfb[i], 0);
                int16_t _agcOut[3][320];
                int16_t* agcOut[3];

                for (int j = 0; j < ap_ctx->num_bands; j++) {
                    agcOut[j] = _agcOut[j];
                }

                uint8_t saturation;
                rc = WebRtcAgc_AddMic(ap_ctx->agc[i], (int16_t**)nearend_ibands, ap_ctx->num_bands, ap_ctx->samples_per_proc);
                if (rc != 0) {
                    audio_proc_log_warn("Failed to add mic for agc");
                }

                rc = WebRtcAgc_Process(ap_ctx->agc[i], nearend_ibands_c, ap_ctx->num_bands, ap_ctx->samples_per_proc,
                    agcOut, ap_ctx->agc_mic_level, &ap_ctx->agc_mic_level, 0, &saturation);
                if (rc != 0) {
                    audio_proc_log_warn("Failed to process agc");
                }

                for (int j = 0; j < ap_ctx->num_bands; j++) {
                    memcpy((void*)nearend_ibands[j], _agcOut[j], ap_ctx->samples_per_proc * sizeof(int16_t));
                }
            }

#ifdef WEBRTC_MOBILE
            const int16_t* const* nearend_ibands_c = audio_splitting_filter_buffer_get_ibands_const(ap_ctx->nearend_sfb[i], 0);
            int16_t* const* nearend_ibands = audio_splitting_filter_buffer_get_ibands(ap_ctx->nearend_sfb[i], 0);

            int16_t _nsxOut[3][320];
            int16_t* nsxOut[3];
            for (int j = 0; j < ap_ctx->num_bands; j++) {
                nsxOut[j] = _nsxOut[j];
            }
                
            if(ap_ctx->ns_enable){
                WebRtcNsx_Process((NsxHandle*)ap_ctx->ns[i], nearend_ibands_c, ap_ctx->num_bands,
                    nsxOut);

                for (int j = 0; j < ap_ctx->num_bands; j++) {
                    memcpy((void*)nearend_ibands[j], nsxOut[j], ap_ctx->samples_per_proc * sizeof(int16_t));
                }
            }

            if (need_do_aec) {
                rc = WebRtcAecm_BufferFarend(ap_ctx->aec[i], audio_splitting_filter_buffer_get_ibands_const(ap_ctx->farend_sfb[i], 0)[0], 
                    ap_ctx->samples_per_proc);
                if(rc != 0){
                    audio_proc_log_warn("WebRtcAecm_BufferFarend failed.");
                }

                rc = WebRtcAecm_Process(ap_ctx->aec[i], nearend_ibands_c[0],
                    (ap_ctx->ns_enable ? nsxOut[0] : NULL),
                    audio_splitting_filter_buffer_get_ibands(ap_ctx->out_sfb[i], 0)[0], ap_ctx->samples_per_proc,
                    ap_ctx->aec_delay);
                if(rc != 0){
                    audio_proc_log_warn("WebRtcAecm_Process failed.");
                }

                for (int j = 0; j < ap_ctx->num_bands; j++) {
                    memcpy((void*)nearend_ibands[j], audio_splitting_filter_buffer_get_ibands(ap_ctx->out_sfb[i], 0)[j], 
                        ap_ctx->samples_per_proc * sizeof(int16_t));
                }
            }

            audio_splitting_filter_buffer_get_data(ap_ctx->nearend_sfb[i], (uint8_t*)ap_ctx->out_chs[i], to_proc_bytes_mono);
#else
            if (ap_ctx->ns_enable) {
                const float* const* nearend_fbands_c = audio_splitting_filter_buffer_get_fbands_const(ap_ctx->nearend_sfb[i], 0);
                float* const* nearend_fbands = audio_splitting_filter_buffer_get_fbands(ap_ctx->nearend_sfb[i], 0);

                // WebRtcNsx_Process((NsxHandle*)ns, (const short* const*)nsIn, 3, nsOut);
                float _nsOut[3][320];
                float* nsOut[3];
                for (int j = 0; j < ap_ctx->num_bands; j++) {
                    nsOut[j] = _nsOut[j];
                }

                WebRtcNs_Analyze(ap_ctx->ns[i], nearend_fbands_c[0]);

                WebRtcNs_Process((NsHandle*)ap_ctx->ns[i], nearend_fbands_c, ap_ctx->num_bands, nsOut);

                for (int j = 0; j < ap_ctx->num_bands; j++) {
                    memcpy((void*)nearend_fbands[j], _nsOut[j], ap_ctx->samples_per_proc * sizeof(float));
                }
            }

            if (need_do_aec) {
                rc = WebRtcAec_BufferFarend(ap_ctx->aec[i],
                    audio_splitting_filter_buffer_get_fbands_const(ap_ctx->farend_sfb[i], 0)[0],
                    ap_ctx->samples_per_proc);
                if (rc != 0) {
                    audio_proc_log_warn("WebRtcAec_BufferFarend failed.");
                }

                rc = WebRtcAec_Process(ap_ctx->aec[i],
                    audio_splitting_filter_buffer_get_fbands_const(ap_ctx->nearend_sfb[i], 0),
                    ap_ctx->num_bands,
                    audio_splitting_filter_buffer_get_fbands(ap_ctx->out_sfb[i], 0),
                    (size_t)ap_ctx->samples_per_proc, ap_ctx->aec_delay, 0);
                if (rc != 0) {
                    audio_proc_log_warn("WebRtcAec_Process failed.");
                }

                audio_splitting_filter_buffer_get_data(ap_ctx->out_sfb[i], (uint8_t*)ap_ctx->out_chs[i], to_proc_bytes_mono);

                /*audio_proc_log_info("do aec... to proc bytes %d, aec delay %d, samples per proc %d", 
                    to_proc_bytes_mono, ap_ctx->aec_delay, ap_ctx->samples_per_proc);*/
            }
            else {
                audio_splitting_filter_buffer_get_data(ap_ctx->nearend_sfb[i], (uint8_t*)ap_ctx->out_chs[i], to_proc_bytes_mono);
            }
#endif
        }

        if (is_stereo) {
            nproc = merge_mono_to_stereo(ap_ctx->out_chs[0], ap_ctx->out_chs[1], to_proc_samples, out, len);
            if (nproc <= 0) {
                audio_proc_log_warn("Failed to merge mono to stereo for %d", nproc);
                break;
            }
        }
        else {
            memcpy(out, ap_ctx->out_chs[0], MIN(to_proc_bytes_mono, len));
            nproc = to_proc_samples;
        }

        len            -= nproc;
        out            += nproc;
        proced_samples += nproc;

        nr_samples     -= to_proc_samples * ap_ctx->channels;
        farend_sample  += to_proc_samples * ap_ctx->channels;
        nearend_sample += to_proc_samples * ap_ctx->channels;

        avail_bytes    -= to_proc_bytes;
    }

    ap_ctx_unref(ap_ctx);

    return proced_samples;
}

int ap_ctx_get_per_proc_bytes(audio_proc_ctx* ap_ctx)
{
    if(!ap_ctx){
        return 0;
    }

    return (ap_ctx->num_frames * ap_ctx->channels * sizeof(int16_t));
}

static int do_resample(struct rs_data *resampler, 
    const uint8_t* data, size_t size, uint8_t* out, size_t out_size)
{
    int rc;
    int in_left, out_left;
    short *in_samples, *out_samples;

    in_left = size / sizeof(short);
    out_left = out_size / sizeof(short);

    in_samples = (short*)data;
    out_samples = (short*)out;

    rc = resample(resampler,
            in_samples, in_left, out_samples, out_left, size == 0);
    if (rc <= 0) {
       // audio_proc_log_warn("Failed to do resample for %d, when left %d %d", 
       //     rc, in_left, out_left);
       return rc;
    }

    return (rc * sizeof(short));
}

int ap_ctx_try_process(audio_proc_ctx* ap_ctx, 
    uint8_t *out, size_t out_size)
{
    int rc;
    int near_avail, far_avail, avail;
    int to_read, copied;

    int16_t *out_buf;

    check_try_retrun(ap_ctx, -1);

    ap_ctx_ref(ap_ctx);

    near_avail = ring_buffer_avail(ap_ctx->nearend_rbuf);
    far_avail = ring_buffer_avail(ap_ctx->farend_rbuf);

    copied = 0;
    avail = near_avail; // MIN(near_avail, far_avail);

    out_buf = out;

#if 1
    int16_t farend_buf[960];
    int16_t nearend_buf[960];
#else
    int16_t farend_buf[640];
    int16_t nearend_buf[640];
    int16_t out_buf[640];
#endif

    to_read = MIN(sizeof(nearend_buf), ap_ctx->num_frames * ap_ctx->channels * sizeof(int16_t));

    /* audio_proc_log_info("avail %d, to read %d, out size %d, size of farend %d, thread id %d", 
        avail, to_read, out_size, sizeof(farend_buf), pthread_self()); */

    while((avail >= to_read) && (out_size > 0)) {
        memset(farend_buf, 0, to_read);
        memset(nearend_buf, 0, to_read);
    
        rc = ring_buffer_read(ap_ctx->farend_rbuf, (uint8_t*)farend_buf, to_read);
        if (rc != to_read) {
            audio_proc_log_info("farend buf size %d, to read %d", rc, to_read);        
            // break;
        }

        AUDIO_STAREM_DEBUG_EXT("/sdcard/read_from_farend.pcm", from_playback_fp,
            farend_buf, rc);

        rc = ring_buffer_read(ap_ctx->nearend_rbuf, (uint8_t*)nearend_buf, to_read);
        if (rc != to_read) {
            audio_proc_log_info("nearend buf size %d, to read %d", rc, to_read);
            // break;
        }

        AUDIO_STAREM_DEBUG_EXT("/sdcard/read_from_nearend.pcm", from_record_fp,
            nearend_buf, rc);

#if 1
        rc = ap_ctx_do_process(ap_ctx, farend_buf, nearend_buf, to_read / sizeof(int16_t), 
                out_buf, out_size / sizeof(int16_t));
        if (rc <= 0) {
            audio_proc_log_warn("Failed to process pcm");
            break;
        }

        AUDIO_STAREM_DEBUG_EXT("/sdcard/do_aec.pcm", do_aec_fp,
            out_buf, (rc * sizeof(int16_t)));
#endif

        avail    -= to_read;
        copied   += to_read;

        out_buf  += rc;
        out_size -= rc * sizeof(out_buf[0]);
    }

    if(ap_ctx->proc_freq != ap_ctx->nearend_freq){
        uint8_t samples_buf[1920 * 4];
        const uint8_t *samples = out;
    
        rc = do_resample(ap_ctx->resampler[AP_CTX_OUT_MODE],
                samples, copied, samples_buf, sizeof(samples_buf));
        if (rc <= 0) {
           // audio_proc_log_warn("Failed to do resample for %d", rc);
           return rc;
        }

        memcpy(out, samples_buf, rc);

        copied = rc;
    }

    ap_ctx_unref(ap_ctx);

    return copied;
}

int ap_ctx_push_farend(audio_proc_ctx* ap_ctx, const uint8_t* data, size_t size)
{
    int rc;
    const uint8_t *samples;
    uint8_t samples_buf[1920 * 4];
    
    check_try_retrun(ap_ctx, -1);

    if(!ap_ctx_should_do_aec(ap_ctx)){
        return 0;
    }

    assert(sizeof(samples_buf) >= size);

    samples = data;
    rc = size;

    ap_ctx_ref(ap_ctx);

    if(ap_ctx->farend_freq != ap_ctx->proc_freq){
        // do resample
        
        rc = do_resample(ap_ctx->resampler[AP_CTX_PLAYBACK_MODE],
                data, size, samples_buf, sizeof(samples_buf));
        if (rc <= 0) {
           ap_ctx_unref(ap_ctx);
           
           audio_proc_log_warn("Failed to do resample for %d", rc);
           return rc;
        }

        samples = samples_buf;

        /* audio_proc_log_info("playback should do resample from %d to %d, size %d to %d", 
            ap_ctx->farend_freq, ap_ctx->proc_freq, size, rc); */
    }

    rc = ring_buffer_write(ap_ctx->farend_rbuf, samples, rc);

    ap_ctx_unref(ap_ctx);

    return size; /* rc */
}

int ap_ctx_push_nearend(audio_proc_ctx* ap_ctx, const uint8_t* data, size_t size)
{
    int rc;
    const uint8_t *samples;
    uint8_t samples_buf[1920 * 4];
    
    check_try_retrun(ap_ctx, -1);

    if(!ap_ctx_should_do_aec(ap_ctx)){
        return 0;
    }

    assert(sizeof(samples_buf) >= size);

    samples = data;
    rc = size;

    ap_ctx_ref(ap_ctx);

    if(ap_ctx->nearend_freq != ap_ctx->proc_freq){
        // do resample
        
        rc = do_resample(ap_ctx->resampler[AP_CTX_RECORD_MODE], 
            data, size, samples_buf, sizeof(samples_buf));
        if (rc <= 0) {
           ap_ctx_unref(ap_ctx);
           
           audio_proc_log_warn("Failed to do resample for %d", rc);
           return rc;
        }

        samples = samples_buf;

        /* audio_proc_log_info("record should do resample from %d to %d, size %d to %d", 
            ap_ctx->nearend_freq, ap_ctx->proc_freq, size, rc); */
    }

    rc = ring_buffer_write(ap_ctx->nearend_rbuf, samples, rc);

    ap_ctx_unref(ap_ctx);

    return size; /* rc */
}
