#ifndef AUDIO_SPLITTING_FILTER_BUFFER_H_
#define AUDIO_SPLITTING_FILTER_BUFFER_H_

#if defined(_MSC_VER)
    //  Microsoft 
    #define EXPORT __declspec(dllexport)
    #define IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
    //  GCC
    #define EXPORT __attribute__((visibility("default")))
    #define IMPORT
#else
    //  do nothing and hope for the best?
    #define EXPORT
    #define IMPORT
    #pragma warning Unknown dynamic link import/export semantics.
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct _audio_splitting_filter_buffer audio_splitting_filter_buffer;

EXPORT 
audio_splitting_filter_buffer* audio_splitting_filter_buffer_create();

EXPORT 
void audio_splitting_filter_buffer_free(audio_splitting_filter_buffer* sp);

EXPORT
int audio_splitting_filter_buffer_init(audio_splitting_filter_buffer *sp, 
	size_t num_channels, size_t num_bands, size_t num_frames);

EXPORT 
int audio_splitting_filter_buffer_fill_data(audio_splitting_filter_buffer* sp,
	const uint8_t *data, size_t size);

EXPORT
int audio_splitting_filter_buffer_get_data(audio_splitting_filter_buffer* sp,
	uint8_t* data, size_t size);

EXPORT
const float* const* audio_splitting_filter_buffer_get_fbands_const(audio_splitting_filter_buffer* sp,
	int channel);

EXPORT 
const int16_t* const* audio_splitting_filter_buffer_get_ibands_const(audio_splitting_filter_buffer* sp,
	int channel);

EXPORT 
float* const* audio_splitting_filter_buffer_get_fbands(audio_splitting_filter_buffer* sp,
	int channel);

EXPORT 
int16_t* const* audio_splitting_filter_buffer_get_ibands(audio_splitting_filter_buffer* sp,
	int channel);

EXPORT 
void audio_splitting_filter_buffer_analysis(audio_splitting_filter_buffer* sp);

EXPORT 
void audio_splitting_filter_buffer_synthesis(audio_splitting_filter_buffer* sp);

#ifdef __cplusplus
}
#endif

#endif // !AUDIO_SPLITTING_FILTER_BUFFER_H_

