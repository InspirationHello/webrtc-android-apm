#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define WEBRTC_MOBILE

#include "common/ring_buffer.h"
#include "common/audio_process_util.h"


#define LOGW(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
#define LOGI           LOGW

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(ARRAY) (sizeof((ARRAY)) / sizeof((ARRAY)[0]))
#endif

#define AUDIO_STAREM_DEBUG(file_name, fp_name, buf, size)            \
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


typedef struct _global{
    int         channels;
    int         frequency;
    int         aec_delay;
    const char *farend_path;
    const char *nearend_path;
    const char *out_path;
}global;

static global session = {
    .channels     = 2,
    .frequency    = 48000,
    .aec_delay    = 100,
    .farend_path  = "/sdcard/farend_for_playback.pcm",
    .nearend_path = "/sdcard/nearend_for_record.pcm",
    .out_path     = "/sdcard/after_apm.pcm"
};

static audio_proc_ctx *ap_ctx;



typedef struct _cmd_t cmd_t;

typedef int (*cmd_op_pft)(cmd_t *cmd, const char *to_parse);

struct _cmd_t{
    const char *name;
    const char *opt;
    cmd_op_pft  parse;
    void       *pval; // value pointer
    bool        is_alloc; // alloc from heap
    const void *def_pval; // default value pointer
    const char *desc;
};

static int parse_integer(cmd_t *cmd, const char *to_parse)
{
    int  val;
    int *pval;

    pval = (int *)cmd->pval;

    val = atoi(to_parse);
    if(!val){
        if(cmd->def_pval){
            val = (int)cmd->def_pval;
        }
    }
    
    if(pval){
       *pval = val;
    }
    
    return 0;
}

static int parse_string(cmd_t *cmd, const char *to_parse)
{
    int size;
    char *tmp;
    char **ppval;

    ppval = (char **)cmd->pval;
    size = strlen(to_parse) + 1;
    
    tmp = (char *)malloc(size * sizeof(*tmp));
    if(tmp){
        cmd->is_alloc = true;
         
        snprintf(tmp, size, "%s", to_parse);

        *ppval = tmp;
    }else{
        *ppval = (char*)cmd->def_pval;
    }

    return 0;
}

cmd_t cmds[] = {
    {
        .name     = "channels",
        .opt      = "-c",
        .parse    = parse_integer,
        .desc     = "pcm channels",
        .def_pval = (void*)(2),
        .pval     = &session.channels
    }, {
        .name     = "frequency",
        .opt      = "-f",
        .parse    = parse_integer,
        .desc     = "pcm frequency",
        .def_pval = (void*)(48000),
        .pval     = &session.frequency
    }, {
        .name     = "aec_delay",
        .opt      = "-d",
        .parse    = parse_integer,
        .desc     = "pcm frequency",
        .def_pval = (void*)(100),
        .pval     = &session.aec_delay
    }, {
        .name     = "farend_file",
        .opt      = "-far",
        .parse    = parse_string,
        .desc     = "pcm aec farend",
        .def_pval = (void*)("farend.pcm"),
        .pval     = &session.farend_path
    }, {
        .name     = "nearend_file",
        .opt      = "-near",
        .parse    = parse_string,
        .desc     = "pcm aec nearend",
        .def_pval = (void*)("nearend.pcm"),
        .pval     = &session.nearend_path
    }, {
        .name     = "out_file",
        .opt      = "-o",
        .parse    = parse_string,
        .desc     = "pcm after apm",
        .def_pval = (void*)("out.pcm"),
        .pval     = &session.out_path
    }, {
        .name     = NULL,
        .opt      = NULL,
        .parse    = NULL
    }
};

/*
* /system/lib/webrtc_apm_test -far /sdcard/read_from_playback.pcm -near /sdcard/read_from_record.pcm
* busybox ls -lh /sdcard/
*/
int main(int argc, const char *argv[])
{
    FILE   *farend_fp, *nearend_fp;
    int     rc;
    int     processed;
    uint8_t buffer[1920 * 2] = { 0 };

    for(int i = 1; i < argc; ++i){
        bool is_last = (i + 1 >= argc);
        const char *arg = is_last ? NULL : argv[i + 1];

        for(cmd_t *cmd = cmds; cmd && cmd->name; ++cmd){
            if(!strcmp(cmd->opt, argv[i])){
                cmd->parse(cmd, arg);

                i += 1;
                break;
            }
        }
    }

    LOGW("channels %d, frequency %d, aec delay %d",
        session.channels, session.frequency, session.aec_delay);
    
    ap_ctx = ap_ctx_get_default();

    farend_fp = fopen(session.farend_path, "rb");
    nearend_fp = fopen(session.nearend_path, "rb");

    if(!farend_fp || !nearend_fp){
        LOGW("Failed to open file %s %s", session.farend_path, session.nearend_path);
        return -1;
    }

    rc = ap_ctx_init(ap_ctx, session.channels, session.frequency, session.frequency, session.aec_delay);
    if(rc < 0){
        LOGW("Failed to init audio process context");
    }else{
        ap_ctx_ref(ap_ctx);
        ap_ctx_set_nearend_info(ap_ctx, session.frequency, session.channels);
        ap_ctx_set_nearend_is_ready(ap_ctx, 1);

        ap_ctx_set_aec_farend_info(ap_ctx, session.frequency, session.channels);
        ap_ctx_set_farend_is_ready(ap_ctx, 1);
    }


    do {
        rc = fread(buffer, 1, sizeof(buffer), farend_fp);
        if (!rc) {
            break;
        }

        ap_ctx_push_farend(ap_ctx, buffer, rc);

        rc = fread(buffer, 1, sizeof(buffer), nearend_fp);
        if (!rc) {
            break;
        }

        ap_ctx_push_nearend(ap_ctx, buffer, rc);
    } while (rc > 0);


    do{
        processed = ap_ctx_try_process(ap_ctx, buffer, ap_ctx_get_per_proc_bytes(ap_ctx));

        if(processed > 0){
            // LOGI("ap_ctx_do_aec processed %d", processed);

            AUDIO_STAREM_DEBUG(session.out_path, from_playback_fp,
                buffer, processed);
        }else{
            LOGW("ap_ctx_do_aec Faile to processed %d", processed);
            break;
        }
    }while(processed > 0);

    ap_ctx_set_nearend_is_ready(ap_ctx, 0);
    ap_ctx_set_farend_is_ready(ap_ctx, 0);
    ap_ctx_unref(ap_ctx);

    for(cmd_t *cmd = cmds; cmd && cmd->name; ++cmd){
        if(cmd->is_alloc && cmd->pval){
            char **ptr = (char **)cmd->pval;
            free(*ptr);
            *ptr = NULL;
        }
    }

    return 0;
}

