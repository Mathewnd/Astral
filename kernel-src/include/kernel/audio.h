#ifndef _AUDIO_H
#define _AUDIO_H

#include <ringbuffer.h>
#include <mutex.h>
#include <stdbool.h>
#include <kernel/poll.h>

typedef struct audio_stream audio_stream_t;

typedef struct {
	int (*acquire_stream)(void *private, bool nonblocking, audio_stream_t **stream);
	void (*release_stream)(void *private, audio_stream_t *stream);
} audio_device_ops_t;

#define AUDIO_STREAM_INFO_FRAGMENT_SIZE 1
#define AUDIO_STREAM_INFO_SPEED 2
#define AUDIO_STREAM_INFO_CHANNELS 3
#define AUDIO_STREAM_INFO_FORMAT 4
#define AUDIO_STREAM_INFO_FIFO_FRAMES 5
#define AUDIO_STREAM_INFO_PLAYED_FRAMES 6
#define AUDIO_STREAM_INFO_SUPPORTED_FORMATS 7

#define AUDIO_FORMAT_QUERY 0x00000000
#define AUDIO_FORMAT_MU_LAW 0x00000001
#define AUDIO_FORMAT_A_LAW 0x00000002
#define AUDIO_FORMAT_IMA_ADPCM 0x00000004
#define AUDIO_FORMAT_U8 0x00000008
#define AUDIO_FORMAT_S16_LE 0x00000010
#define AUDIO_FORMAT_S16_BE 0x00000020
#define AUDIO_FORMAT_S8 0x00000040
#define AUDIO_FORMAT_U16_LE 0x00000080
#define AUDIO_FORMAT_U16_BE 0x00000100
#define AUDIO_FORMAT_MPEG 0x00000200
#define AUDIO_FORMAT_AC3 0x00000400
#define AUDIO_FORMAT_VORBIS 0x00000800
#define AUDIO_FORMAT_S32_LE 0x00001000
#define AUDIO_FORMAT_S32_BE 0x00002000
#define AUDIO_FORMAT_FLOAT 0x00004000
#define AUDIO_FORMAT_S24_LE 0x00008000
#define AUDIO_FORMAT_S24_BE 0x00010000
#define AUDIO_FORMAT_SPDIF_RAW 0x00020000
#define AUDIO_FORMAT_S24_PACKED 0x00040000

#define AUDIO_TRIGGER_INPUT 1
#define AUDIO_TRIGGER_OUTPUT 2

typedef struct {
	void (*start)(audio_stream_t *);
	void (*stop)(audio_stream_t *);
	void (*wait_for_playback)(audio_stream_t *);
	void (*reset)(audio_stream_t *);
	int (*set_format)(audio_stream_t *, int format, int *selected);
	int (*set_speed)(audio_stream_t *, int speed, int *selected);
	int (*set_channels)(audio_stream_t *, int channels, int *selected);
	void (*get_info)(audio_stream_t *, int what, void *buf);
} audio_stream_ops_t;

typedef struct audio_stream {
	mutex_t mutex;
	ringbuffer_t ringbuffer;
	pollheader_t pollheader;
	audio_stream_ops_t *ops;
	bool playing;
	int trigger;
	size_t underruns;
	size_t bytes_submitted;
	size_t bytes_written;
	size_t bytes_played;
} audio_stream_t;

bool audio_take_stream_data(audio_stream_t *stream, void *buffer, size_t size);
int audio_initialize_stream(audio_stream_t *stream, audio_stream_ops_t *stream_ops);
int audio_register_device(audio_device_ops_t *device_ops, void *private);

#endif
