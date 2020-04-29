#include "audio_splitting_filter_buffer.h"

#include <stdint.h>

#include "webrtc/common_audio/include/audio_util.h"
#include "webrtc/common_audio/channel_buffer.h"
#include "webrtc/modules/audio_processing/splitting_filter.h"

struct _audio_splitting_filter_buffer {
	int num_bands;
	int channels;

	webrtc::SplittingFilter* sp;

	webrtc::IFChannelBuffer* data;
	webrtc::IFChannelBuffer* bands;
};

audio_splitting_filter_buffer* audio_splitting_filter_buffer_create()
{
	audio_splitting_filter_buffer* fp;

	fp = (audio_splitting_filter_buffer*)malloc(sizeof(*fp));
	if (fp) {
		memset(fp, 0, sizeof(*fp));
	}

	return fp;
}

void audio_splitting_filter_buffer_free(audio_splitting_filter_buffer* sp)
{
	if (sp->bands != sp->data) {
		delete  sp->bands;
	}

	delete sp->data;

	free(sp);
}

int audio_splitting_filter_buffer_init(audio_splitting_filter_buffer* sp, 
	size_t num_channels, size_t num_bands, size_t num_frames)
{
	switch (num_bands)
	{
	case 1:
		break;
	case 2:
	case 3:
		sp->sp = new webrtc::SplittingFilter(num_channels, num_bands, num_frames);
		if (!sp->sp) {
			return -1;
		}
		break;
	default:
		return -1;
		break;
	}

	sp->data = new webrtc::IFChannelBuffer(num_frames, num_channels, num_bands);
	
	if (num_bands != 1) {
		sp->bands = new webrtc::IFChannelBuffer(num_frames, num_channels, num_bands);
	}
	else {
		sp->bands = sp->data;
	}

	sp->channels = num_channels;
	sp->num_bands = num_bands;

	return 0;
}

int audio_splitting_filter_buffer_fill_data(audio_splitting_filter_buffer* sp,
	const uint8_t* data, size_t size)
{
	memcpy(sp->data->ibuf()->bands(0)[0], data, size);

	audio_splitting_filter_buffer_analysis(sp);

	return size;
}

int audio_splitting_filter_buffer_get_data(audio_splitting_filter_buffer* sp,
	uint8_t* data, size_t size)
{
	audio_splitting_filter_buffer_synthesis(sp);

	memcpy(data, sp->data->ibuf_const()->bands(0)[0], size);

	return size;
}

const float* const* audio_splitting_filter_buffer_get_fbands_const(audio_splitting_filter_buffer* sp, 
	int channel)
{
	return sp->bands->fbuf_const()->bands(channel);
}

const int16_t* const* audio_splitting_filter_buffer_get_ibands_const(audio_splitting_filter_buffer* sp,
	int channel)
{
	return sp->bands->ibuf_const()->bands(channel);
}

float* const* audio_splitting_filter_buffer_get_fbands(audio_splitting_filter_buffer* sp,
	int channel)
{
	return sp->bands->fbuf()->bands(channel);
}

int16_t* const* audio_splitting_filter_buffer_get_ibands(audio_splitting_filter_buffer* sp,
	int channel)
{
	return sp->bands->ibuf()->bands(channel);
}

void audio_splitting_filter_buffer_analysis(audio_splitting_filter_buffer* sp)
{
	if ((sp->num_bands != 2 && sp->num_bands != 3) || !sp->sp) {
		return;
	}

	sp->sp->Analysis(sp->data, sp->bands);
}

void audio_splitting_filter_buffer_synthesis(audio_splitting_filter_buffer* sp)
{
	if ((sp->num_bands != 2 && sp->num_bands != 3) || !sp->sp) {
		return;
	}

	sp->sp->Synthesis(sp->bands, sp->data);
}
