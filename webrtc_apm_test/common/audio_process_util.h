#ifndef _AUDIO_PROCESS_UTIL_H_
#define _AUDIO_PROCESS_UTIL_H_

#include "stdint.h"

typedef struct _audio_proc_ctx audio_proc_ctx;


audio_proc_ctx* ap_ctx_create();

void ap_ctx_free(audio_proc_ctx* ap_ctx);

audio_proc_ctx* ap_ctx_get_default();

int ap_ctx_ref(audio_proc_ctx* ap_ctx);

int ap_ctx_unref(audio_proc_ctx* ap_ctx);

int ap_ctx_init(audio_proc_ctx* ap_ctx, uint32_t channels,
    uint32_t nearend_freq, uint32_t farend_freq, uint32_t aec_delay);

void ap_ctx_fini(audio_proc_ctx* ap_ctx);

int ap_ctx_set_farend_is_ready(audio_proc_ctx* ap_ctx, int ready);

int ap_ctx_set_nearend_is_ready(audio_proc_ctx* ap_ctx, int ready);

int ap_ctx_set_ns_is_enable(audio_proc_ctx* ap_ctx, int enable);

int ap_ctx_set_agc_is_enable(audio_proc_ctx* ap_ctx, int enable);

int ap_ctx_should_do_aec(audio_proc_ctx* ap_ctx);

int ap_ctx_set_aec_config(audio_proc_ctx* ap_ctx);

int ap_ctx_reset(audio_proc_ctx* ap_ctx);

int ap_ctx_cleanup(audio_proc_ctx* ap_ctx);

int ap_ctx_set_aec_farend_info(audio_proc_ctx* ap_ctx,
    uint32_t farend_freq, uint32_t farend_channels);

int ap_ctx_set_nearend_info(audio_proc_ctx* ap_ctx,
    uint32_t nearend_freq, uint32_t nearend_channels);

int ap_ctx_push_farend(audio_proc_ctx* ap_ctx, const uint8_t* data, size_t size);

int ap_ctx_push_nearend(audio_proc_ctx* ap_ctx, const uint8_t* data, size_t size);

int ap_ctx_do_process(audio_proc_ctx* ap_ctx, const int16_t const *farend, 
    const int16_t const *nearend, size_t nr_samples, 
    int16_t dest[], size_t len);

// typedef int (*ap_ctx_process_pft)(const uint8_t* src, uint8_t* dest, size_t size, void *args);

int ap_ctx_try_process(audio_proc_ctx* ap_ctx, 
    uint8_t *out, size_t out_size);

int ap_ctx_get_per_proc_bytes(audio_proc_ctx* ap_ctx);

#endif