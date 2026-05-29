#include <kernel/audio.h>
#include <kernel/interrupt.h>
#include <kernel/devfs.h>
#include <logging.h>
#include <kernel/usercopy.h>

static size_t bytes_to_frames(int fmt, int channels, size_t bytes) {
	size_t sample_size = 0;
	switch (fmt) {
		case AUDIO_FORMAT_S16_LE:
			sample_size = 2;
			break;
	}

	return bytes / (sample_size * channels);
}

#define MAX_STREAM 64
#define AUDIO_RINGBUFFER_SIZE (8 * 1024)

static int current_minor = 0;
static audio_stream_t *streams[MAX_STREAM];

static int audio_open(int minor, vnode_t **vnode, int flags) {
	audio_stream_t *stream = streams[minor];
	if (stream == NULL)
		return ENODEV;

	int error = 0;
	MUTEX_ACQUIRE(&stream->mutex);

	if (stream->opened) {
		error = EBUSY;
		goto leave;
	}

	stream->opened = true;

	leave:
	MUTEX_RELEASE(&stream->mutex);
	return error;
}

static int audio_close(int minor, int flags) {
	audio_stream_t *stream = streams[minor];
	if (stream == NULL)
		return ENODEV;

	MUTEX_ACQUIRE(&stream->mutex);

	if ((flags & V_FFLAGS_CLOSE_URGENT) == 0 && stream->playing)
		stream->ops->wait_for_playback(stream);

	stream->ops->stop(stream);
	stream->ops->reset(stream);
	ringbuffer_truncate(&stream->ringbuffer, 0xffffffff);
	stream->playing = false;
	stream->opened = false;
	stream->underruns = 0;
	stream->trigger = AUDIO_TRIGGER_OUTPUT | AUDIO_TRIGGER_INPUT;
	stream->bytes_submitted = 0;
	stream->bytes_written = 0;
	stream->bytes_played = 0;

	MUTEX_RELEASE(&stream->mutex);
	return 0;
}

static int internal_poll(audio_stream_t *stream, polldata_t *data, int events) {
	int revents = 0;

	if ((events & POLLOUT) && RINGBUFFER_FREESPACE(&stream->ringbuffer))
		revents |= POLLOUT;

	if (data && events && revents == 0)
		poll_add(&stream->pollheader, data, events);

	return revents;
}

static int audio_write(int minor, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *writec) {
	audio_stream_t *stream = streams[minor];
	if (stream == NULL)
		return ENODEV;

	polldesc_t desc = {0};
	int error = poll_initdesc(&desc, 1);
	if (error)
		return error;

	MUTEX_ACQUIRE(&stream->mutex);
	for (;;) {
		int revents = internal_poll(stream, &desc.data[0], POLLOUT);
		if (revents)
			break;

		if (flags & V_FFLAGS_NONBLOCKING) {
			error = EAGAIN;
			goto leave;
		}

		MUTEX_RELEASE(&stream->mutex);

		error = poll_dowait(&desc, 0);

		MUTEX_ACQUIRE(&stream->mutex);
		if (error)
			goto leave;

		poll_leave(&desc);
	}

	size_t done = iovec_iterator_write_to_ringbuffer(iovec_iterator, &stream->ringbuffer, size);
	if (done == RINGBUFFER_USER_COPY_FAILED) {
		error = EFAULT;
		goto leave;
	}

	__assert(done > 0);
	stream->bytes_written += done;
	*writec = done;

	if (stream->trigger & AUDIO_TRIGGER_OUTPUT && !stream->playing) {
		stream->ops->start(stream);
		stream->playing = true;
	}

	leave:
	poll_leave(&desc);
	poll_destroydesc(&desc);
	MUTEX_RELEASE(&stream->mutex);
	return error;
}

static int audio_poll(int minor, polldata_t *data, int events) {
	audio_stream_t *stream = streams[minor];
	if (stream == NULL)
		return POLLERR;

	MUTEX_ACQUIRE(&stream->mutex);
	int revents = internal_poll(stream, data, events);
	MUTEX_RELEASE(&stream->mutex);
	return revents;
}

typedef struct {
	int fragments;
	int fragstotal;
	int fragsize;
	int bytes;
} audio_buf_info_t;

typedef struct {
	int play_underruns;
	int rec_overruns;
	unsigned int play_ptradjust;
	unsigned int rec_ptradjust;
	int play_errorcount;
	int rec_errorcount;
	int play_lasterror;
	int rec_lasterror;
	int play_errorparm;
	int rec_errorparm;
	int filler[16];
} audio_errinfo_t;

typedef struct {
	long long samples;
	int fifo_samples;
	int filler[32];
} oss_count_t;

#define SNDCTL_DSP_GETOSPACE 0x8010500c
#define SNDCTL_DSP_SPEED 0xc0045002
#define SNDCTL_DSP_CHANNELS 0xc0045006
#define SNDCTL_DSP_SETFRAGMENT 0xc004500a
#define SNDCTL_DSP_GETFMTS 0x8004500b
#define SNDCTL_DSP_SETFMT 0xc0045005
#define SNDCTL_DSP_SETTRIGGER 0x40045010
#define SNDCTL_DSP_GETERROR 0x80685019
#define SNDCTL_DSP_CURRENT_OPTR 0x80905024

static int audio_ioctl(int minor, unsigned long request, void *arg, int *result, cred_t *cred) {
	audio_stream_t *stream = streams[minor];
	if (stream == NULL)
		return ENODEV;

	MUTEX_ACQUIRE(&stream->mutex);
	size_t fragment_size;
	stream->ops->get_info(stream, AUDIO_STREAM_INFO_FRAGMENT_SIZE, &fragment_size);

	int error;
	switch (request) {
		case SNDCTL_DSP_GETOSPACE: {
			size_t free_space = RINGBUFFER_FREESPACE(&stream->ringbuffer);
			audio_buf_info_t info = {
				.fragments = free_space / fragment_size,
				.fragstotal = AUDIO_RINGBUFFER_SIZE / fragment_size,
				.fragsize = fragment_size,
				.bytes = free_space
			};

			error = USERCOPY_POSSIBLY_TO_USER(arg, &info, sizeof(info));
			break;
		}
		case SNDCTL_DSP_SPEED: {
			int speed;
			stream->ops->get_info(stream, AUDIO_STREAM_INFO_SPEED, &speed);

			error = USERCOPY_POSSIBLY_TO_USER(arg, &speed, sizeof(speed));
			break;
		}
		case SNDCTL_DSP_CHANNELS: {
			int channels;
			stream->ops->get_info(stream, AUDIO_STREAM_INFO_CHANNELS, &channels);

			error = USERCOPY_POSSIBLY_TO_USER(arg, &channels, sizeof(channels));
			break;
		}
		case SNDCTL_DSP_GETFMTS:
		case SNDCTL_DSP_SETFMT: {
			int fmt;
			stream->ops->get_info(stream, AUDIO_STREAM_INFO_FORMAT, &fmt);

			error = USERCOPY_POSSIBLY_TO_USER(arg, &fmt, sizeof(fmt));
			break;
		}
		case SNDCTL_DSP_SETFRAGMENT: {
			int f = ((AUDIO_RINGBUFFER_SIZE / fragment_size) << 16) | log2(fragment_size);
			error = USERCOPY_POSSIBLY_TO_USER(arg, &f, sizeof(f));
			break;
		}
		case SNDCTL_DSP_SETTRIGGER: {
			int v;
			error = USERCOPY_POSSIBLY_FROM_USER(&v, arg, sizeof(v));
			if (error)
				break;

			bool old_output = stream->trigger & AUDIO_TRIGGER_OUTPUT;
			bool new_output = v & AUDIO_TRIGGER_OUTPUT;

			stream->trigger = v;

			if (old_output && !new_output && stream->playing) {
				stream->ops->stop(stream);
				stream->ops->reset(stream);
				ringbuffer_truncate(&stream->ringbuffer, 0xffffffff);
				stream->bytes_submitted = 0;
				stream->bytes_written = 0;
				stream->bytes_played = 0;
				stream->playing = false;
			}

			if (!stream->playing && !old_output && new_output && RINGBUFFER_DATACOUNT(&stream->ringbuffer)) {
				stream->ops->start(stream);
				stream->playing = true;
			}

			break;
		}
		case SNDCTL_DSP_GETERROR: {
			audio_errinfo_t info;
			memset(&info, 0, sizeof(info));
			info.play_underruns = stream->underruns;
			stream->underruns = 0;

			error = USERCOPY_POSSIBLY_TO_USER(arg, &info, sizeof(info));
			break;
		}
		case SNDCTL_DSP_CURRENT_OPTR: {
			oss_count_t count;
			memset(&count, 0, sizeof(count));

			int channels, fmt;
			stream->ops->get_info(stream, AUDIO_STREAM_INFO_CHANNELS, &channels);
			stream->ops->get_info(stream, AUDIO_STREAM_INFO_FORMAT, &fmt);

			count.fifo_samples += bytes_to_frames(fmt, channels, RINGBUFFER_DATACOUNT(&stream->ringbuffer));
			uint64_t played_frames;
			stream->ops->get_info(stream, AUDIO_STREAM_INFO_PLAYED_FRAMES, &played_frames);
			count.samples = played_frames;
			if (stream->playing) {
				int fifo_frames;
				stream->ops->get_info(stream, AUDIO_STREAM_INFO_FIFO_FRAMES, &fifo_frames);
//				count.fifo_samples += fifo_frames;
			}

//			count.fifo_samples = 10;

			error = USERCOPY_POSSIBLY_TO_USER(arg, &count, sizeof(count));
			break;
		}
		default:
			error = ENOTTY;
	}

	MUTEX_RELEASE(&stream->mutex);

	return error;
}

static devops_t devops = {
	.open = audio_open,
	.close = audio_close,
	.write = audio_write,
	.poll = audio_poll,
	.ioctl = audio_ioctl
};

int audio_register_stream(audio_stream_t *stream) {
	int minor = __atomic_fetch_add(&current_minor, 1, __ATOMIC_RELAXED);
	if (minor >= MAX_STREAM)
		return ENXIO;

	streams[minor] = stream;

	char name[10];
	snprintf(name, 10, "dsp%d", minor);
	int error = devfs_register(&devops, name, V_TYPE_CHDEV, DEV_MAJOR_AUDIO, minor, 0666, NULL);
	return error;
}

bool audio_take_stream_data(audio_stream_t *stream, void *buffer, size_t size) {
	size_t read_count = ringbuffer_read(&stream->ringbuffer, buffer, size);
	memset((void *)((uintptr_t)buffer + read_count), 0, size - read_count);
	if (read_count > 0)
		poll_event(&stream->pollheader, POLLOUT);

	stream->bytes_submitted += size;
	return read_count > 0;
}

int audio_initialize_stream(audio_stream_t *stream, audio_stream_ops_t *ops) {
	int e = ringbuffer_init(&stream->ringbuffer, AUDIO_RINGBUFFER_SIZE);
	if (e)
		return e;

	MUTEX_INIT(&stream->mutex);
	POLL_INITHEADER(&stream->pollheader);
	stream->opened = false;
	stream->playing = false;
	stream->ops = ops;
	stream->trigger = AUDIO_TRIGGER_INPUT | AUDIO_TRIGGER_OUTPUT;
	stream->underruns = 0;
	stream->bytes_submitted = 0;
	stream->bytes_written = 0;
	stream->bytes_played = 0;
	return 0;
}
