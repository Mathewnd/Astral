#include <kernel/pci.h>
#include <logging.h>
#include <kernel/hda.h>
#include <kernel/init.h>
#include <kernel/page.h>
#include <list.h>
#include <semaphore.h>
#include <kernel/alloc.h>
#include <kernel/audio.h>
#include <errno.h>

#define HDA_FORMAT_CHANNELS_2 1
#define HDA_FORMAT_BASE_44100 (1 << 14)
#define HDA_FORMAT_MULT_2 (1 << 11)
#define HDA_FORMAT_MULT_4 (3 << 11)
#define HDA_FORMAT_DIV_2 (1 << 8)
#define HDA_FORMAT_DIV_3 (2 << 8)
#define HDA_FORMAT_DIV_4 (3 << 8)
#define HDA_FORMAT_DIV_6 (5 << 8)
#define HDA_FORMAT_BITS_32 4

#define HDA_WIDGET_CAPS_STEREO (1 << 0)
#define HDA_WIDGET_CAPS_FORMAT_OVERRIDE (1 << 4)
#define HDA_WIDGET_CAPS_DIGITAL (1 << 8)

#define HDA_PCM_CAPS_B8 (1 << 16)
#define HDA_PCM_CAPS_B16 (1 << 17)
#define HDA_PCM_CAPS_B32 (1 << 20)
#define HDA_PCM_CAPS_RATE_8KHZ (1 << 0)
#define HDA_PCM_CAPS_RATE_11KHZ (1 << 1)
#define HDA_PCM_CAPS_RATE_16KHZ (1 << 2)
#define HDA_PCM_CAPS_RATE_22KHZ (1 << 3)
#define HDA_PCM_CAPS_RATE_32KHZ (1 << 4)
#define HDA_PCM_CAPS_RATE_44KHZ (1 << 5)
#define HDA_PCM_CAPS_RATE_48KHZ (1 << 6)
#define HDA_PCM_CAPS_RATE_88KHZ (1 << 7)
#define HDA_PCM_CAPS_RATE_96KHZ (1 << 8)
#define HDA_PCM_CAPS_RATE_176KHZ (1 << 9)
#define HDA_PCM_CAPS_RATE_192KHZ (1 << 10)
#define HDA_STREAM_FORMAT_PCM (1 << 0)

#define HDA_BUFFER_PAGE_COUNT 2
#define HDA_BUFFER_TOTAL_SIZE (HDA_BUFFER_PAGE_COUNT * PAGE_SIZE)

#define HDA_BDL_FLAGS_IOC 1

#define INTEL_HDA_TCSEL 0x44
#define INTEL_HDA_DEVC 0x78
#define INTEL_HDA_DEVC_NOSNOOP (1 << 11)

typedef struct {
	uint64_t address;
	uint32_t length;
	uint32_t flags;
} __attribute__((packed)) hda_bdl_entry_t;

#define HDA_SD_CTL_BYTE0_RESET 1
#define HDA_SD_CTL_BYTE0_RUN 2
#define HDA_SD_CTL_BYTE0_IOCE 4
#define HDA_SD_CTL_BYTE0_FEIE 8
#define HDA_SD_CTL_BYTE0_DEIE 16
#define HDA_SD_CTL_BYTE2_BI_OUTPUT (1 << 3)

#define HDA_STREAM_STOP_TIMEOUT_US 10000
#define HDA_STREAM_STOP_POLL_US 100

#define HDA_SD_STS_BCIS (1 << 2)
#define HDA_SD_STS_FIFOE (1 << 3)
#define HDA_SD_STS_DESE (1 << 4)

typedef struct {
	uint8_t ctl[3];
	uint8_t sts;
	uint32_t lpib;
	uint32_t cbl;
	uint16_t lvi;
	uint16_t reserved1;
	uint16_t fifod;
	uint16_t fmt;
	uint32_t reserved2;
	uint32_t bdpl;
	uint32_t bdph;
} __attribute__((packed)) hda_sd_t;

#define GCAP_64OK 1

#define GCTL_CRST 1

#define SIZE_CAP_256 (1 << 6)
#define SIZE_CAP_16 (1 << 5)
#define SIZE_CAP_2 (1 << 4)

#define SIZE_256 2
#define SIZE_16 1
#define SIZE_2 0

#define CORBRP_RST (1 << 15)
#define CORBCTL_DMA_ENABLE (1 << 1)

typedef struct {
	uint16_t gcap;
	uint8_t  vmin;
	uint8_t  vmaj;
	uint16_t outpay;
	uint16_t inpay;
	uint32_t gctl;
	uint16_t wakeen;
	uint16_t statests;
	uint16_t gsts;
	uint8_t reserved1[6];
	uint16_t outstrmpay;
	uint16_t instrmpay;
	uint32_t reserved2;
	uint32_t intctl;
	uint32_t intsts;
	uint64_t reserved4;
	uint32_t walclk;
	uint32_t reserved5;
	uint32_t ssync;
	uint32_t reserved6;
	uint32_t corblbase;
	uint32_t corbhbase;
	uint16_t corbwp;
	uint16_t corbrp;
	uint8_t  corbctl;
	uint8_t  corbsts;
	uint8_t  corbsize;
	uint8_t  reserved7;
	uint32_t rirblbase;
	uint32_t rirbhbase;
	uint16_t rirbwp;
	uint16_t rintcnt;
	uint8_t  rirbctl;
	uint8_t  rirbsts;
	uint8_t  rirbsize;
	uint8_t  reserved8;
	uint32_t icoi;
	uint32_t icii;
	uint16_t icis;
	uint8_t  reserved9[6];
	uint32_t dpiblbase;
	uint32_t dpibubase;
	uint64_t reserved10;
	hda_sd_t sds[];
} __attribute__((packed)) hda_regs_t;

#define HDA_WIDGET_TYPE_AUDIO_OUTPUT 0
#define HDA_WIDGET_TYPE_AUDIO_INPUT 1
#define HDA_WIDGET_TYPE_AUDIO_MIXER 2
#define HDA_WIDGET_TYPE_AUDIO_SELECTOR 3
#define HDA_WIDGET_TYPE_PIN_COMPLEX 4
#define HDA_WIDGET_TYPE_POWER_WIDGET 5
#define HDA_WIDGET_TYPE_VOLUME_KNOB 6
#define HDA_WIDGET_TYPE_BEEP_GENERATOR 7

#define HDA_PIN_CAPS_OUTPUT_CAPABLE (1 << 4)
#define HDA_AMP_CAP_NUM_STEPS(x) (((x) >> 8) & 0x7f)

typedef struct hda_widget hda_widget_t;
typedef struct hda_widget {
	int type;
	int nid;
	size_t connection_count;
	hda_widget_t **connections;
	uint32_t pin_caps;
	uint32_t widget_caps;
	uint32_t pcm_caps;
	uint32_t stream_formats;
	uint32_t input_amp_caps;
	uint32_t output_amp_caps;
	uint32_t volume_knob_caps;
	uint32_t defaults;
} hda_widget_t;

typedef struct {
	int type;
	int nid;
	size_t widget_count;
	size_t starting_node;
	uint32_t pcm_caps;
	uint32_t stream_formats;
	hda_widget_t *widgets;
} hda_fg_t;

typedef struct {
	list_node_t list_node;
	semaphore_t semaphore;
	uint32_t response;
} hda_codec_waiter_t;

typedef struct {
	hda_widget_t **widgets;
	size_t size;
} hda_path_t;

typedef struct hda hda_t;
typedef struct hda_output_path hda_output_path_t;

typedef struct {
	list_t waiter_list;
	spinlock_t waiter_list_lock;
	size_t function_group_count;
	size_t starting_node;
	hda_fg_t *function_groups;
	size_t path_count;
	hda_path_t *output_paths;
} hda_codec_t;

typedef struct {
	audio_stream_t generic;
	uint16_t hda_format;
	int format;
	int speed;
	int channels;
	hda_t *hda;
	volatile hda_sd_t *sd;
	void *bdl_phys;
	size_t tag;
	size_t descriptor_index;
	bool bi;
	bool allocated;
	bool active;
	hda_output_path_t *owner;
	size_t last;
	size_t fill_ptr;
	uint32_t last_lpib;
	uint64_t bytes_played;
	eventheader_t underrun_event;
} hda_stream_t;

struct hda {
	volatile hda_regs_t *regs;
	volatile uint32_t *corb;
	size_t corb_entries;
	size_t corbwp;
	spinlock_t corb_lock;
	semaphore_t corb_space_semaphore;
	volatile uint64_t *rirb;
	size_t rirb_entries;
	size_t rirbrp;
	hda_codec_t codecs[15];
	volatile hda_sd_t *input_sd;
	volatile hda_sd_t *output_sd;
	volatile hda_sd_t *bi_sd;
	size_t output_stream_count;
	size_t input_stream_count;
	size_t bi_stream_count;
	size_t allocated_output_stream_count;
	size_t allocated_input_stream_count;
	size_t allocated_bi_stream_count;
	size_t current_stream_tag;
	spinlock_t stream_lock;
	semaphore_t output_stream_semaphore;
	size_t usable_output_stream_count;
	hda_stream_t **streams;
};

struct hda_output_path {
	hda_t *hda;
	int codec;
	hda_path_t path;
	hda_widget_t *converter;
	uint32_t supported_formats;
	int default_format;
	int default_speed;
	int default_channels;
	uint16_t hda_format;
	bool digital;
};

typedef struct {
	int speed;
	uint32_t pcm_cap;
	uint16_t format;
} hda_rate_t;

static const hda_rate_t hda_rates[] = {
	{ 48000, HDA_PCM_CAPS_RATE_48KHZ, 0 },
	{ 44100, HDA_PCM_CAPS_RATE_44KHZ, HDA_FORMAT_BASE_44100 },
	{ 96000, HDA_PCM_CAPS_RATE_96KHZ, HDA_FORMAT_MULT_2 },
	{ 88200, HDA_PCM_CAPS_RATE_88KHZ, HDA_FORMAT_BASE_44100 | HDA_FORMAT_MULT_2 },
	{ 192000, HDA_PCM_CAPS_RATE_192KHZ, HDA_FORMAT_MULT_4 },
	{ 176400, HDA_PCM_CAPS_RATE_176KHZ, HDA_FORMAT_BASE_44100 | HDA_FORMAT_MULT_4 },
	{ 32000, HDA_PCM_CAPS_RATE_32KHZ, HDA_FORMAT_MULT_2 | HDA_FORMAT_DIV_3 },
	{ 22050, HDA_PCM_CAPS_RATE_22KHZ, HDA_FORMAT_BASE_44100 | HDA_FORMAT_DIV_2 },
	{ 16000, HDA_PCM_CAPS_RATE_16KHZ, HDA_FORMAT_DIV_3 },
	{ 11025, HDA_PCM_CAPS_RATE_11KHZ, HDA_FORMAT_BASE_44100 | HDA_FORMAT_DIV_4 },
	{ 8000, HDA_PCM_CAPS_RATE_8KHZ, HDA_FORMAT_DIV_6 }
};

static uint32_t hda_set_converter_format(hda_t *hda, uint32_t codec, uint32_t nid, uint16_t format);
static bool hda_format_to_stream_format(hda_widget_t *converter, int format, int speed, int channels, uint16_t *hda_format);
static int hda_default_output_speed(hda_widget_t *converter);

static uint32_t hda_stream_mask(hda_t *hda) {
	size_t count = hda->input_stream_count + hda->output_stream_count + hda->bi_stream_count;
	return count >= 30 ? 0x3fffffff : ((1u << count) - 1);
}

static bool hda_sd_set_reset(volatile hda_sd_t *sd, bool set) {
	if (set)
		sd->ctl[0] |= HDA_SD_CTL_BYTE0_RESET;
	else
		sd->ctl[0] &= ~HDA_SD_CTL_BYTE0_RESET;

	int loops = 0;
	bool done = false;
	while (loops < 20) {
		if (set ? (sd->ctl[0] & HDA_SD_CTL_BYTE0_RESET) : ((sd->ctl[0] & HDA_SD_CTL_BYTE0_RESET) == 0)) {
			done = true;
			break;
		}

		sched_sleep_us(100);
		++loops;
	}

	return done;
}

static bool hda_stream_stop(hda_stream_t *stream) {
	stream->sd->ctl[0] &= ~(HDA_SD_CTL_BYTE0_RUN | HDA_SD_CTL_BYTE0_IOCE | HDA_SD_CTL_BYTE0_FEIE | HDA_SD_CTL_BYTE0_DEIE);

	size_t waited = 0;
	while (stream->sd->ctl[0] & HDA_SD_CTL_BYTE0_RUN) {
		if (waited >= HDA_STREAM_STOP_TIMEOUT_US) {
			printf("hda: stopping stream timed out\n");
			return false;
		}

		sched_sleep_us(HDA_STREAM_STOP_POLL_US);
		waited += HDA_STREAM_STOP_POLL_US;
	}

	stream->sd->sts = HDA_SD_STS_BCIS | HDA_SD_STS_FIFOE | HDA_SD_STS_DESE;
	return true;
}

static void hda_stream_start(hda_stream_t *stream) {
	stream->sd->ctl[0] |= HDA_SD_CTL_BYTE0_RUN | HDA_SD_CTL_BYTE0_IOCE;
}

static bool hda_reset_stream(hda_stream_t *stream) {
	if (!hda_stream_stop(stream))
		return false;

	long ipl = spinlock_acquire_raise_ipl(&stream->hda->stream_lock, IPL_AUDIO);
	spinlock_release_lower_ipl(&stream->hda->stream_lock, ipl);

	if (!hda_sd_set_reset(stream->sd, true)) {
		printf("hda: setting stream reset bit timed out\n");
		return false;
	}

	if (!hda_sd_set_reset(stream->sd, false)) {
		printf("hda: clearing stream reset bit timed out\n");
		return false;
	}

	stream->sd->sts = HDA_SD_STS_BCIS | HDA_SD_STS_FIFOE | HDA_SD_STS_DESE;
	stream->sd->cbl = HDA_BUFFER_TOTAL_SIZE;
	stream->sd->lvi = HDA_BUFFER_PAGE_COUNT - 1;
	stream->sd->fmt = stream->hda_format;
	stream->sd->bdpl = (uint64_t)stream->bdl_phys & 0xffffffff;
	stream->sd->bdph = ((uint64_t)stream->bdl_phys >> 32) & 0xffffffff;
	stream->sd->ctl[0] = HDA_SD_CTL_BYTE0_IOCE;
	stream->sd->ctl[2] = (stream->tag << 4) | (stream->bi ? HDA_SD_CTL_BYTE2_BI_OUTPUT : 0);
	stream->last_lpib = 0;
	stream->bytes_played = 0;
	stream->fill_ptr = 0;

	return true;
}

static void hda_update_playback_position(hda_stream_t *stream) {
	uint32_t lpib = stream->sd->lpib;
	uint32_t delta;

	if (lpib >= stream->last_lpib)
		delta = lpib - stream->last_lpib;
	else
		delta = HDA_BUFFER_TOTAL_SIZE - stream->last_lpib + lpib;

	stream->last_lpib = lpib;
	stream->bytes_played += delta;
}

static void stream_start(audio_stream_t *p) {
	hda_stream_t *stream = (hda_stream_t *)p;

	hda_bdl_entry_t *bdl = MAKE_HHDM(stream->bdl_phys);
	for (int i = 0; i < HDA_BUFFER_PAGE_COUNT; ++i) {
		void *buffer = MAKE_HHDM((void *)bdl[i].address);
		audio_take_stream_data(&stream->generic, buffer, PAGE_SIZE);
	}

	hda_stream_start(stream);
}

static void stream_stop(audio_stream_t *p) {
	hda_stream_t *stream = (hda_stream_t *)p;
	hda_stream_stop(stream);
}

static void stream_wait_for_playback(audio_stream_t *p) {
	hda_stream_t *stream = (hda_stream_t *)p;

	eventlistener_t listener;
	for (;;) {
		EVENT_INITLISTENER(&listener);
		EVENT_ATTACH(&listener, &stream->underrun_event);

		int ret = EVENT_WAIT(&listener, 0);

		EVENT_DETACHALL(&listener);
		if (ret == 0)
			return;
	}
}

static void stream_reset(audio_stream_t *p) {
	hda_stream_t *stream = (hda_stream_t *)p;
	hda_reset_stream(stream);
}

static void stream_get_info(audio_stream_t *p, int what, void *buf) {
	hda_stream_t *stream = (hda_stream_t *)p;

	switch (what) {
		case AUDIO_STREAM_INFO_FRAGMENT_SIZE:
			*(size_t *)buf = PAGE_SIZE;
			break;
		case AUDIO_STREAM_INFO_SPEED:
			*(int *)buf = stream->speed;
			break;
		case AUDIO_STREAM_INFO_CHANNELS:
			*(int *)buf = stream->channels;
			break;
		case AUDIO_STREAM_INFO_FORMAT:
			*(int *)buf = stream->format;
			break;
		case AUDIO_STREAM_INFO_SUPPORTED_FORMATS:
			*(int *)buf = stream->owner ? stream->owner->supported_formats : 0;
			break;
		case AUDIO_STREAM_INFO_FIFO_FRAMES: {
			size_t sample_size = 0;
			switch (stream->format) {
				case AUDIO_FORMAT_U8:
					sample_size = 1;
					break;
				case AUDIO_FORMAT_S16_LE:
					sample_size = 2;
					break;
				case AUDIO_FORMAT_S32_LE:
					sample_size = 4;
					break;
			}

			hda_update_playback_position(stream);
			uint64_t dma_bytes = stream->generic.bytes_submitted - stream->bytes_played;
			if (dma_bytes > HDA_BUFFER_TOTAL_SIZE)
				dma_bytes = HDA_BUFFER_TOTAL_SIZE;
			*(int *)buf = dma_bytes / (sample_size * stream->channels);
			break;
		}
		case AUDIO_STREAM_INFO_PLAYED_FRAMES: {
			size_t sample_size = 0;
			switch (stream->format) {
				case AUDIO_FORMAT_U8:
					sample_size = 1;
					break;
				case AUDIO_FORMAT_S16_LE:
					sample_size = 2;
					break;
				case AUDIO_FORMAT_S32_LE:
					sample_size = 4;
					break;
			}
			if (sample_size == 0 || stream->channels <= 0) {
				*(uint64_t *)buf = 0;
				break;
			}

			hda_update_playback_position(stream);
			*(uint64_t *)buf = stream->bytes_played / (sample_size * stream->channels);
			break;
		}
	}
}

static int hda_set_stream_configuration(hda_stream_t *stream, int format, int speed, int channels) {
	hda_output_path_t *output_path = stream->owner;
	if (output_path == NULL)
		return ENODEV;

	uint16_t hda_format;
	if ((output_path->supported_formats & format) == 0 ||
			!hda_format_to_stream_format(output_path->converter, format, speed, channels, &hda_format)) {
		return EINVAL;
	}

	if (stream->generic.playing || RINGBUFFER_DATACOUNT(&stream->generic.ringbuffer)) {
		return EBUSY;
	}

	if (!hda_reset_stream(stream))
		return EIO;

	stream->sd->fmt = hda_format;
	hda_set_converter_format(stream->hda, output_path->codec, output_path->converter->nid, hda_format);
	stream->hda_format = hda_format;
	stream->format = format;
	stream->speed = speed;
	stream->channels = channels;
	stream->generic.bytes_submitted = 0;
	stream->generic.bytes_written = 0;
	stream->generic.bytes_played = 0;
	return 0;
}

static int stream_set_format(audio_stream_t *p, int format, int *selected) {
	hda_stream_t *stream = (hda_stream_t *)p;
	*selected = stream->format;
	if (format == AUDIO_FORMAT_QUERY) {
		return 0;
	}

	int error = hda_set_stream_configuration(stream, format, stream->speed, stream->channels);
	if (error == EINVAL)
		return 0;
	if (error == 0)
		*selected = format;

	return error;
}

static int stream_set_speed(audio_stream_t *p, int speed, int *selected) {
	hda_stream_t *stream = (hda_stream_t *)p;
	*selected = stream->speed;
	int error = hda_set_stream_configuration(stream, stream->format, speed, stream->channels);
	if (error == EINVAL)
		return 0;
	if (error == 0)
		*selected = speed;

	return error;
}

static int stream_set_channels(audio_stream_t *p, int channels, int *selected) {
	hda_stream_t *stream = (hda_stream_t *)p;
	*selected = stream->channels;
	int error = hda_set_stream_configuration(stream, stream->format, stream->speed, channels);
	if (error == EINVAL)
		return 0;
	if (error == 0)
		*selected = channels;

	return error;
}

static audio_stream_ops_t hda_stream_ops = {
	.start = stream_start,
	.stop = stream_stop,
	.wait_for_playback = stream_wait_for_playback,
	.reset = stream_reset,
	.get_info = stream_get_info,
	.set_format = stream_set_format,
	.set_speed = stream_set_speed,
	.set_channels = stream_set_channels
};

static hda_stream_t *hda_initialize_output_stream(hda_t *hda, size_t stream_n, size_t tag, bool bi) {
	hda_stream_t *stream = alloc(sizeof(hda_stream_t));
	__assert(stream);
	__assert(audio_initialize_stream(&stream->generic, &hda_stream_ops) == 0);
	EVENT_INITHEADER(&stream->underrun_event);
	stream->hda = hda;
	stream->sd = &hda->regs->sds[stream_n];
	stream->descriptor_index = stream_n;
	stream->tag = tag;
	stream->bi = bi;
	stream->hda_format = 0;
	stream->format = AUDIO_FORMAT_QUERY;
	stream->speed = 0;
	stream->channels = 0;

	stream->bdl_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
	__assert(stream->bdl_phys);
	hda_bdl_entry_t *bdl = MAKE_HHDM(stream->bdl_phys);

	for (int i = 0; i < HDA_BUFFER_PAGE_COUNT; ++i) {
		void *phys_page = mm_alloc_page(MEMORY_SECTION_DEFAULT);
		__assert(phys_page);
		memset(MAKE_HHDM(phys_page), 0, PAGE_SIZE);

		bdl[i].address = (uint64_t)phys_page;
		bdl[i].length = PAGE_SIZE;
		bdl[i].flags = HDA_BDL_FLAGS_IOC;
	}

	hda->streams[stream_n] = stream;

	return stream;
}

static void hda_initialize_output_stream_pool(hda_t *hda) {
	while (hda->allocated_output_stream_count < hda->output_stream_count && hda->current_stream_tag <= 15) {
		size_t i = hda->allocated_output_stream_count++;
		size_t stream_n = hda->input_stream_count + i;
		hda_initialize_output_stream(hda, stream_n, hda->current_stream_tag++, false);
		++hda->usable_output_stream_count;
	}

	while (hda->allocated_bi_stream_count < hda->bi_stream_count && hda->current_stream_tag <= 15) {
		size_t i = hda->allocated_bi_stream_count++;
		size_t stream_n = hda->input_stream_count + hda->output_stream_count + i;
		hda_initialize_output_stream(hda, stream_n, hda->current_stream_tag++, true);
		++hda->usable_output_stream_count;
	}

	if (hda->output_stream_count + hda->bi_stream_count > hda->usable_output_stream_count)
		printf("hda: only %lu output streams are usable because stream tags are exhausted\n", hda->usable_output_stream_count);

	SEMAPHORE_INIT(&hda->output_stream_semaphore, hda->usable_output_stream_count);
}

static hda_path_t hda_find_path(hda_fg_t *fg, hda_widget_t *pin, int goal_type) {
	typedef struct {
		list_node_t list_node;
		hda_path_t path;
	} path_entry_t;

	list_t queue;
	list_init(&queue);

	bool *visited = alloc(sizeof(bool) * fg->widget_count);
	__assert(visited);
	memset(visited, 0, sizeof(bool) * fg->widget_count);

	path_entry_t *entry = alloc(sizeof(path_entry_t));
	__assert(entry);
	entry->path.widgets = alloc(sizeof(hda_widget_t *));
	__assert(entry->path.widgets);
	entry->path.size = 1;
	entry->path.widgets[0] = pin;
	visited[pin->nid - fg->starting_node] = true;

	list_push_back(&queue, &entry->list_node);

	hda_path_t output_path = {0};
	for (;;) {
		list_node_t *node = list_pop_front(&queue);
		entry = (path_entry_t *)node;

		if (entry == NULL)
			break;

		hda_widget_t *current = entry->path.widgets[entry->path.size - 1];
		if (current->type == goal_type) {
			output_path = entry->path;
			free(entry);
			break;
		}

		for (size_t i = 0; i < current->connection_count; ++i) {
			hda_widget_t *next = current->connections[i];
			size_t id_0 = next->nid - fg->starting_node;
			if (visited[id_0] || (next->type != HDA_WIDGET_TYPE_AUDIO_MIXER && next->type != HDA_WIDGET_TYPE_AUDIO_SELECTOR && next->type != goal_type))
				continue;

			visited[id_0] = true;

			path_entry_t *new_entry = alloc(sizeof(path_entry_t));
			__assert(new_entry);

			new_entry->path.size = entry->path.size + 1;
			new_entry->path.widgets = alloc(sizeof(hda_widget_t *) * new_entry->path.size);
			__assert(new_entry->path.widgets);

			memcpy(new_entry->path.widgets, entry->path.widgets, sizeof(hda_widget_t *) * entry->path.size);
			new_entry->path.widgets[new_entry->path.size - 1] = next;

			list_push_back(&queue, &new_entry->list_node);
		}

		free(entry->path.widgets);
		free(entry);
	}

	free(visited);

	for (;;) {
		list_node_t *node = list_pop_front(&queue);
		if (node == NULL)
			break;

		entry = (path_entry_t *)node;

		free(entry->path.widgets);
		free(entry);
	}

	return output_path;
}

static void hda_stream_fill_data(hda_stream_t *stream) {
	int current = stream->sd->lpib / PAGE_SIZE;
	if (stream->fill_ptr == current) {
		if (RINGBUFFER_DATACOUNT(&stream->generic.ringbuffer) == 0) {
			EVENT_SIGNAL(&stream->underrun_event);
			++stream->generic.underruns;
		}

		stream->fill_ptr = (current + 1) % HDA_BUFFER_PAGE_COUNT;
	}

	hda_bdl_entry_t *bdl = MAKE_HHDM(stream->bdl_phys);
	while (stream->fill_ptr != current) {
		void *buffer = MAKE_HHDM((void *)bdl[stream->fill_ptr].address);
		bool had_data = audio_take_stream_data(&stream->generic, buffer, PAGE_SIZE);
		stream->fill_ptr = (stream->fill_ptr + 1) % HDA_BUFFER_PAGE_COUNT;
		if (!had_data) {
			++stream->generic.underruns;
			EVENT_SIGNAL(&stream->underrun_event);
			break;
		}
	}
}

static void stream_irq(hda_t *hda) {
	uint32_t status = hda->regs->intsts & hda_stream_mask(hda);
	spinlock_acquire(&hda->stream_lock);

	for (int i = 0; i < 30; ++i) {
		if ((status & (1 << i)) == 0)
			continue;

		hda_stream_t *stream = hda->streams[i];
		if (stream == NULL)
			 continue;

		uint8_t strsts = stream->sd->sts;
		stream->sd->sts = strsts & (HDA_SD_STS_BCIS | HDA_SD_STS_DESE | HDA_SD_STS_FIFOE);
		if (stream->active && (strsts & HDA_SD_STS_BCIS))
			hda_stream_fill_data(stream);
	}

	spinlock_release(&hda->stream_lock);
}

static void rirb_irq(hda_t *hda) {
	uint16_t rirbsts = hda->regs->rirbsts;
	hda->regs->rirbsts = rirbsts;

	while (hda->rirbrp != hda->regs->rirbwp) {
		++hda->rirbrp;
		if (hda->rirbrp == hda->rirb_entries)
			hda->rirbrp = 0;

		uint64_t entry = hda->rirb[hda->rirbrp];

		if ((entry & (1lu << 36)) == 0) {
			uint32_t codec = (entry >> 32) & 0xf;
			spinlock_acquire(&hda->codecs[codec].waiter_list_lock);
			list_node_t *node = list_pop_front(&hda->codecs[codec].waiter_list);
			__assert(node);
			spinlock_release(&hda->codecs[codec].waiter_list_lock);

			hda_codec_waiter_t *waiter = (hda_codec_waiter_t *)node;
			waiter->response = entry & 0xffffffff;
			semaphore_signal(&waiter->semaphore);
		}
	}
}

static void hda_isr(isr_t *isr, context_t *) {
	hda_t *hda = isr->priv;

	if (hda->regs->rirbsts)
		rirb_irq(hda);

	if (hda->regs->intsts & hda_stream_mask(hda))
		stream_irq(hda);
}

// TODO: there is the possibility that the buffer could be overrun and this could possibly wait forever.
static uint32_t hda_submit_verb_and_wait(hda_t *hda, uint32_t codec, uint32_t nid, uint32_t verb) {
	uint32_t corb_entry = (codec << 28) | (nid << 20) | verb;

	semaphore_wait(&hda->corb_space_semaphore, false);
	long ipl = spinlock_acquire_raise_ipl(&hda->corb_lock, IPL_AUDIO);

	hda_codec_waiter_t waiter;
	SEMAPHORE_INIT(&waiter.semaphore, 0);

	spinlock_acquire(&hda->codecs[codec].waiter_list_lock);
	list_push_back(&hda->codecs[codec].waiter_list, &waiter.list_node);
	spinlock_release(&hda->codecs[codec].waiter_list_lock);

	++hda->corbwp;
	if (hda->corbwp == hda->corb_entries)
		hda->corbwp = 0;

	hda->corb[hda->corbwp] = corb_entry;
	hda->regs->corbwp = (hda->regs->corbwp & 0xff00) | hda->corbwp;
	spinlock_release_lower_ipl(&hda->corb_lock, ipl);

	semaphore_wait(&waiter.semaphore, false);
	semaphore_signal(&hda->corb_space_semaphore);

	return waiter.response;
}

#define HDA_PARAM_VENDOR_ID 0x0
#define HDA_PARAM_REVISION_ID 0x2
#define HDA_PARAM_SUB_NODE_COUNT 0x4
#define HDA_PARAM_FUNCTION_GROUP_TYPE 0x5
#define HDA_PARAM_AUDIO_WIDGET_CAPS 0x9
#define HDA_PARAM_PCM 0xa
#define HDA_PARAM_STREAM_FORMATS 0xb
#define HDA_PARAM_PIN_CAPS 0xc
#define HDA_PARAM_INPUT_AMP_CAPS 0xd
#define HDA_PARAM_CONNECTION_LIST_LENGTH 0xe
#define HDA_PARAM_POWER_STATES 0x0f
#define HDA_PARAM_OUTPUT_AMP_CAPS 0x12
#define HDA_PARAM_VOLUME_KNOB_CAPS 0x13

#define HDA_FUNCTION_GROUP_TYPE_AFG 1

static uint32_t hda_get_parameter(hda_t *hda, uint32_t codec, uint32_t nid, uint32_t id) {
	return hda_submit_verb_and_wait(hda, codec, nid, 0xf0000 | id);
}

static uint32_t hda_get_configuration_default(hda_t *hda, uint32_t codec, uint32_t nid) {
	return hda_submit_verb_and_wait(hda, codec, nid, 0xf1c00);
}

#define HDA_WIDGET_POWER_STATE_ON 0

static uint32_t hda_set_power_state(hda_t *hda, uint32_t codec, uint32_t nid, uint32_t state) {
	return hda_submit_verb_and_wait(hda, codec, nid, 0x70500 | state);
}

static uint32_t hda_set_connect_sel(hda_t *hda, uint32_t codec, uint32_t nid, uint32_t sel) {
	return hda_submit_verb_and_wait(hda, codec, nid, 0x70100 | sel);
}

static uint32_t hda_set_amplifier_gain_mute(hda_t *hda, uint32_t codec, uint32_t nid, uint32_t index, bool output, bool mute, uint32_t gain) {
	return hda_submit_verb_and_wait(hda, codec, nid, 0x33000 | gain | (index << 8) | (mute ? 0x80 : 0) | (output ? 0x8000 : 0x4000));
}

static uint32_t hda_set_pin_widget_control(hda_t *hda, uint32_t codec, uint32_t nid, bool out_enable) {
	return hda_submit_verb_and_wait(hda, codec, nid, 0x70700 | (out_enable ? 0x40 : 0));
}

static uint32_t hda_set_eapd_btl(hda_t *hda, uint32_t codec, uint32_t nid, bool eapd) {
	return hda_submit_verb_and_wait(hda, codec, nid, 0x70c00 | (eapd ? 2 : 0));
}

static uint32_t hda_set_converter_format(hda_t *hda, uint32_t codec, uint32_t nid, uint16_t format) {
	return hda_submit_verb_and_wait(hda, codec, nid, 0x20000 | format);
}

static uint32_t hda_set_converter_stream_channel(hda_t *hda, uint32_t codec, uint32_t nid, uint8_t stream, uint8_t base_channel) {
	return hda_submit_verb_and_wait(hda, codec, nid, 0x70600 | (stream << 4) | base_channel);
}

static int hda_acquire_output_stream(void *private, bool nonblocking, audio_stream_t **generic) {
	hda_output_path_t *output_path = private;
	hda_t *hda = output_path->hda;

	if (nonblocking) {
		if (!semaphore_test(&hda->output_stream_semaphore))
			return EBUSY;
	} else {
		int error = semaphore_wait(&hda->output_stream_semaphore, true);
		if (error)
			return error;
	}

	hda_stream_t *stream = NULL;
	long ipl = spinlock_acquire_raise_ipl(&hda->stream_lock, IPL_AUDIO);
	for (size_t i = hda->input_stream_count; i < hda->input_stream_count + hda->output_stream_count + hda->bi_stream_count; ++i) {
		hda_stream_t *candidate = hda->streams[i];
		if (candidate && !candidate->allocated) {
			candidate->allocated = true;
			candidate->owner = output_path;
			stream = candidate;
			break;
		}
	}
	spinlock_release_lower_ipl(&hda->stream_lock, ipl);
	__assert(stream);

	stream->hda_format = output_path->hda_format;
	stream->format = output_path->default_format;
	stream->speed = output_path->default_speed;
	stream->channels = output_path->default_channels;
	if (!hda_reset_stream(stream)) {
		printf("hda: failed to reset allocated stream %lu\n", stream->descriptor_index);
		ipl = spinlock_acquire_raise_ipl(&hda->stream_lock, IPL_AUDIO);
		stream->owner = NULL;
		spinlock_release_lower_ipl(&hda->stream_lock, ipl);
		return EIO;
	}

	hda_set_converter_format(hda, output_path->codec, output_path->converter->nid, output_path->hda_format);
	hda_set_converter_stream_channel(hda, output_path->codec, output_path->converter->nid, stream->tag, 0);

	ipl = spinlock_acquire_raise_ipl(&hda->stream_lock, IPL_AUDIO);
	stream->active = true;
	hda->regs->intctl |= 1u << stream->descriptor_index;
	spinlock_release_lower_ipl(&hda->stream_lock, ipl);

	*generic = &stream->generic;
	return 0;
}

static void hda_release_output_stream(void *private, audio_stream_t *generic) {
	hda_output_path_t *output_path = private;
	hda_stream_t *stream = (hda_stream_t *)generic;
	hda_t *hda = output_path->hda;
	__assert(stream->owner == output_path);

	long ipl = spinlock_acquire_raise_ipl(&hda->stream_lock, IPL_AUDIO);
	hda->regs->intctl &= ~(1u << stream->descriptor_index);
	stream->active = false;
	spinlock_release_lower_ipl(&hda->stream_lock, ipl);

	hda_set_converter_stream_channel(hda, output_path->codec, output_path->converter->nid, 0, 0);

	if (!hda_reset_stream(stream)) {
		printf("hda: failed to reset released stream %lu; keeping it unavailable\n", stream->descriptor_index);
		ipl = spinlock_acquire_raise_ipl(&hda->stream_lock, IPL_AUDIO);
		stream->owner = NULL;
		spinlock_release_lower_ipl(&hda->stream_lock, ipl);
		return;
	}

	ipl = spinlock_acquire_raise_ipl(&hda->stream_lock, IPL_AUDIO);
	stream->owner = NULL;
	stream->allocated = false;
	spinlock_release_lower_ipl(&hda->stream_lock, ipl);
	semaphore_signal(&hda->output_stream_semaphore);
}

static audio_device_ops_t hda_audio_device_ops = {
	.acquire_stream = hda_acquire_output_stream,
	.release_stream = hda_release_output_stream
};

// TODO: we should really be careful to not choose paths that go through the same selector/dac
static void hda_initialize_path(hda_t *hda, int codec, hda_fg_t *fg, hda_path_t path, bool output) {
	__assert(output); // TODO: this entire function hardcodes an output path for now

	for (size_t i = 0; i < path.size; ++i)
		hda_set_power_state(hda, codec, path.widgets[i]->nid, HDA_WIDGET_POWER_STATE_ON);

	for (size_t i = 0; i < path.size - 1; ++i) {
		hda_widget_t *target = path.widgets[i];
		hda_widget_t *source = path.widgets[i + 1];

		if (target->connection_count == 1)
			continue;

		for (size_t j = 0; j < target->connection_count; ++j) {
			if (target->connections[j] == source) {
				hda_set_connect_sel(hda, codec, target->nid, j);
				break;
			}
		}
	}

	for (size_t i = 0; i < path.size; ++i) {
		hda_widget_t *target = path.widgets[i];
		if (target->output_amp_caps)
			hda_set_amplifier_gain_mute(hda, codec, target->nid, 0, true, false, HDA_AMP_CAP_NUM_STEPS(target->output_amp_caps));

		if (target->input_amp_caps && i != path.size - 1) {
			hda_widget_t *source = path.widgets[i + 1];

			uint32_t line;
			for (line = 0; line < target->connection_count; ++line)
				if (target->connections[line] == source)
					break;

			hda_set_amplifier_gain_mute(hda, codec, target->nid, line, false, false, HDA_AMP_CAP_NUM_STEPS(target->input_amp_caps));
		}
	}

	hda_widget_t *pin = path.widgets[0];

	hda_set_pin_widget_control(hda, codec, pin->nid, true);

	hda_set_eapd_btl(hda, codec, pin->nid, true);
}

static hda_widget_t *get_widget_from_nid(hda_fg_t *fg, uint32_t nid) {
	for (size_t i = 0; i < fg->widget_count; ++i) {
		if (fg->widgets[i].nid == nid)
			return &fg->widgets[i];
	}

	return NULL;
}

static void hda_get_connections(hda_t *hda, uint32_t codec, hda_fg_t *fg, hda_widget_t *widget) {
	uint32_t res = hda_get_parameter(hda, codec, widget->nid, HDA_PARAM_CONNECTION_LIST_LENGTH);
	size_t connection_list_length = res & 0x7f;
	bool long_nids = res & 0x80;

	widget->connections = alloc(sizeof(hda_widget_t *));
	__assert(widget->connections);

	uint32_t connection_iterator = 0;
	uint32_t range_start = 0;
	for (uint32_t current = 0; current < connection_list_length; ++current) {
		if ((current % (long_nids ? 2 : 4)) == 0)
			res = hda_submit_verb_and_wait(hda, codec, widget->nid, 0xf0200 | current);

		uint32_t entry;
		bool range;
		if (long_nids) {
			entry = res & 0x7fff;
			range = res & 0x8000;
		} else {
			entry = res & 0x7f;
			range = res & 0x80;
		}

		res >>= long_nids ? 16 : 8;

		if (range) {
			range_start = entry;
			continue;
		}

		if (range_start) {
			size_t range_size = entry - range_start;
			widget->connection_count += range_size;
			widget->connections = realloc(widget->connections, widget->connection_count * sizeof(hda_widget_t *));
			__assert(widget->connections);

			for (size_t i = 0; i < range_size; ++i)
				widget->connections[connection_iterator + i] = get_widget_from_nid(fg, range_start + i);

			connection_iterator += range_size;
			range_start = 0;
			continue;
		}

		++widget->connection_count;
		widget->connections = realloc(widget->connections, widget->connection_count * sizeof(hda_widget_t *));
		__assert(widget->connections);
		widget->connections[connection_iterator] = get_widget_from_nid(fg, entry);
		++connection_iterator;
	}
}

static bool reset_set_wait(volatile hda_regs_t *regs, bool set, size_t timeoutms) {
	if (set)
		regs->gctl |= GCTL_CRST;
	else
		regs->gctl &= ~GCTL_CRST;

	int loops = 0;
	bool done = false;
	while (loops < timeoutms * 10) {
		if (set ? (regs->gctl & GCTL_CRST) : ((regs->gctl & GCTL_CRST) == 0)) {
			done = true;
			break;
		}

		sched_sleep_us(100);
		++loops;
	}

	return done;
}

static void get_sizes(uint8_t reg, uint8_t *size_reg, size_t *size_count) {
	if (reg & SIZE_CAP_256) {
		*size_reg = SIZE_256;
		*size_count = 256;
	} else if (reg & SIZE_CAP_16) {
		*size_reg = SIZE_16;
		*size_count = 16;
	} else {
		*size_reg = SIZE_2;
		*size_count = 2;
	}
}

static bool corb_reset_read_pointer(volatile hda_regs_t *regs, size_t timeoutms) {
	regs->corbrp = CORBRP_RST;

	int loops = 0;
	while ((regs->corbrp & CORBRP_RST) == 0) {
		if (loops++ >= timeoutms * 10)
			return false;
		sched_sleep_us(100);
	}

	regs->corbrp = 0;
	loops = 0;
	while (regs->corbrp & CORBRP_RST) {
		if (loops++ >= timeoutms * 10)
			return false;
		sched_sleep_us(100);
	}

	return true;
}

static bool hda_rate_to_stream_format(hda_widget_t *converter, int speed, uint16_t *rate_format) {
	for (size_t i = 0; i < sizeof(hda_rates) / sizeof(hda_rates[0]); ++i) {
		if (hda_rates[i].speed != speed)
			continue;
		if ((converter->pcm_caps & hda_rates[i].pcm_cap) == 0)
			return false;

		*rate_format = hda_rates[i].format;
		return true;
	}

	return false;
}

static int hda_default_output_speed(hda_widget_t *converter) {
	if ((converter->stream_formats & HDA_STREAM_FORMAT_PCM) == 0)
		return 0;

	for (size_t i = 0; i < sizeof(hda_rates) / sizeof(hda_rates[0]); ++i) {
		if (converter->pcm_caps & hda_rates[i].pcm_cap)
			return hda_rates[i].speed;
	}

	return 0;
}

static bool hda_converter_supports_format(hda_widget_t *converter, int format) {
	uint32_t pcm_cap;

	switch (format) {
		case AUDIO_FORMAT_U8:
			pcm_cap = HDA_PCM_CAPS_B8;
			break;
		case AUDIO_FORMAT_S16_LE:
			pcm_cap = HDA_PCM_CAPS_B16;
			break;
		case AUDIO_FORMAT_S32_LE:
			pcm_cap = HDA_PCM_CAPS_B32;
			break;
		default:
			return false;
	}

	return (converter->stream_formats & HDA_STREAM_FORMAT_PCM) != 0 &&
			(converter->pcm_caps & pcm_cap) != 0;
}

static bool hda_format_to_stream_format(hda_widget_t *converter, int format, int speed, int channels, uint16_t *hda_format) {
	uint16_t bits;
	uint16_t rate_format;

	switch (format) {
		case AUDIO_FORMAT_U8:
			bits = 0;
			break;
		case AUDIO_FORMAT_S16_LE:
			bits = 1;
			break;
		case AUDIO_FORMAT_S32_LE:
			bits = HDA_FORMAT_BITS_32;
			break;
		default:
			return false;
	}

	if (!hda_converter_supports_format(converter, format) ||
			!hda_rate_to_stream_format(converter, speed, &rate_format) ||
			channels < 1 || channels > 2 ||
			(channels == 2 && (converter->widget_caps & HDA_WIDGET_CAPS_STEREO) == 0))
		return false;

	*hda_format = rate_format | (bits << 4);
	if (channels == 2)
		*hda_format |= HDA_FORMAT_CHANNELS_2;
	return true;
}

static uint32_t hda_supported_output_formats(hda_widget_t *converter) {
	uint32_t supported_formats = 0;

	if (hda_converter_supports_format(converter, AUDIO_FORMAT_U8))
		supported_formats |= AUDIO_FORMAT_U8;
	if (hda_converter_supports_format(converter, AUDIO_FORMAT_S16_LE))
		supported_formats |= AUDIO_FORMAT_S16_LE;
	if (hda_converter_supports_format(converter, AUDIO_FORMAT_S32_LE))
		supported_formats |= AUDIO_FORMAT_S32_LE;

	return supported_formats;
}

static int hda_default_output_format(uint32_t supported_formats) {
	if (supported_formats & AUDIO_FORMAT_S16_LE)
		return AUDIO_FORMAT_S16_LE;
	if (supported_formats & AUDIO_FORMAT_S32_LE)
		return AUDIO_FORMAT_S32_LE;
	if (supported_formats & AUDIO_FORMAT_U8)
		return AUDIO_FORMAT_U8;
	return AUDIO_FORMAT_QUERY;
}

static void init_function_group(hda_t *hda, int codec, hda_fg_t *function_group) {
	function_group->type = hda_get_parameter(hda, codec, function_group->nid, HDA_PARAM_FUNCTION_GROUP_TYPE) & 0xff;
	if (function_group->type != HDA_FUNCTION_GROUP_TYPE_AFG)
		return;

	uint32_t res = hda_get_parameter(hda, codec, function_group->nid, HDA_PARAM_SUB_NODE_COUNT);
	function_group->widget_count = res & 0xff;
	function_group->starting_node = (res >> 16) & 0xff;
	function_group->pcm_caps = hda_get_parameter(hda, codec, function_group->nid, HDA_PARAM_PCM);
	function_group->stream_formats = hda_get_parameter(hda, codec, function_group->nid, HDA_PARAM_STREAM_FORMATS);

	printf("hda: codec %d audio function group %lu has %lu widgets starting at %lu\n", codec, function_group->nid, function_group->widget_count, function_group->starting_node);

	function_group->widgets = alloc(sizeof(hda_widget_t) * function_group->widget_count);
	__assert(function_group->widgets);

	for (size_t i = 0; i < function_group->widget_count; ++i) {
		hda_widget_t *widget = &function_group->widgets[i];
		widget->nid = function_group->starting_node + i;

		res = hda_get_parameter(hda, codec, widget->nid, HDA_PARAM_AUDIO_WIDGET_CAPS);
		widget->widget_caps = res;
		widget->type = (res >> 20) & 0xf;
		if (res & HDA_WIDGET_CAPS_FORMAT_OVERRIDE) {
			widget->pcm_caps = hda_get_parameter(hda, codec, widget->nid, HDA_PARAM_PCM);
			widget->stream_formats = hda_get_parameter(hda, codec, widget->nid, HDA_PARAM_STREAM_FORMATS);
		} else {
			widget->pcm_caps = function_group->pcm_caps;
			widget->stream_formats = function_group->stream_formats;
		}
		widget->pin_caps = hda_get_parameter(hda, codec, widget->nid, HDA_PARAM_PIN_CAPS);
		widget->input_amp_caps = hda_get_parameter(hda, codec, widget->nid, HDA_PARAM_INPUT_AMP_CAPS);
		widget->output_amp_caps = hda_get_parameter(hda, codec, widget->nid, HDA_PARAM_OUTPUT_AMP_CAPS);
		widget->volume_knob_caps = hda_get_parameter(hda, codec, widget->nid, HDA_PARAM_VOLUME_KNOB_CAPS);
		widget->defaults = hda_get_configuration_default(hda, codec, widget->nid);
	}

	for (size_t i = 0; i < function_group->widget_count; ++i)
		hda_get_connections(hda, codec, function_group, &function_group->widgets[i]);

	for (size_t i = 0; i < function_group->widget_count; ++i) {
		hda_widget_t *widget = &function_group->widgets[i];
		if (widget->type != HDA_WIDGET_TYPE_PIN_COMPLEX)
			continue;

		if ((widget->pin_caps & HDA_PIN_CAPS_OUTPUT_CAPABLE) == 0)
			continue;

		hda_path_t path = hda_find_path(function_group, widget, HDA_WIDGET_TYPE_AUDIO_OUTPUT);
		if (path.size == 0) {
			printf("hda: no valid path from output pin %d to DAC.\n", widget->nid);
			continue;
		}

		printf("hda: got output path (types): ");
		for (size_t i = 0; i < path.size; ++i) {
			printf("%d ", path.widgets[i]->type);
		}
		printf("\n");

		hda_widget_t *converter = path.widgets[path.size - 1];
		uint32_t supported_formats = hda_supported_output_formats(converter);
		int default_speed = hda_default_output_speed(converter);
		int default_channels = (converter->widget_caps & HDA_WIDGET_CAPS_STEREO) ? 2 : 1;
		int default_format = hda_default_output_format(supported_formats);
		if (default_format == AUDIO_FORMAT_QUERY || default_speed == 0) {
			printf("hda: output converter %d has no supported native PCM rate/format.\n", converter->nid);
			continue;
		}

		uint16_t hda_format;
		__assert(hda_format_to_stream_format(converter, default_format, default_speed, default_channels, &hda_format));

		bool digital = (converter->widget_caps & HDA_WIDGET_CAPS_DIGITAL) != 0;
		printf("hda: output converter %d is %s\n", converter->nid, digital ? "digital" : "analog");

		hda_initialize_path(hda, codec, function_group, path, true);
		hda_set_converter_format(hda, codec, converter->nid, hda_format);

		hda_output_path_t *output_path = alloc(sizeof(hda_output_path_t));
		__assert(output_path);
		output_path->hda = hda;
		output_path->codec = codec;
		output_path->path = path;
		output_path->converter = converter;
		output_path->supported_formats = supported_formats;
		output_path->default_format = default_format;
		output_path->default_speed = default_speed;
		output_path->default_channels = default_channels;
		output_path->hda_format = hda_format;
		output_path->digital = digital;
		__assert(audio_register_device(&hda_audio_device_ops, output_path) == 0);
	}
}

static void init_codec(hda_t *hda, int id) {
	printf("hda: codec at address %d\n", id);
	hda_codec_t *codec = &hda->codecs[id];
	list_init(&codec->waiter_list);
	SPINLOCK_INIT(codec->waiter_list_lock);

	uint32_t res = hda_get_parameter(hda, id, 0, HDA_PARAM_SUB_NODE_COUNT);
	codec->function_group_count = res & 0xff;
	codec->starting_node = (res >> 16) & 0xff;

	printf("hda: codec has %lu function groups starting at node id %lu\n", codec->function_group_count, codec->starting_node);

	codec->function_groups = alloc(sizeof(hda_fg_t) * codec->function_group_count);
	__assert(codec->function_groups);

	for (int i = 0; i < codec->function_group_count; ++i) {
		hda_fg_t *function_group = &codec->function_groups[i];
		function_group->nid = codec->starting_node + i;
		init_function_group(hda, id, function_group);
	}
}

static void initcontroller(pcienum_t *e) {
	pcibar_t bar0 = pci_getbar(e, 0);
	volatile hda_regs_t *regs = (volatile hda_regs_t *)bar0.address;
	__assert(regs);

	pci_setcommand(e, PCI_COMMAND_MMIO, 1);
	pci_setcommand(e, PCI_COMMAND_IO, 0);
	pci_setcommand(e, PCI_COMMAND_IRQDISABLE, 1);
	pci_setcommand(e, PCI_COMMAND_BUSMASTER, 1);

	PCI_WRITE8(e, INTEL_HDA_TCSEL, PCI_READ8(e, INTEL_HDA_TCSEL) & ~0x7);
	PCI_WRITE16(e, INTEL_HDA_DEVC, PCI_READ16(e, INTEL_HDA_DEVC) & ~INTEL_HDA_DEVC_NOSNOOP);

	// enable interrupts
	if (e->msix.exists) {
		pci_initmsix(e);
	} else if (e->msi.exists) {
		pci_initmsi(e, 1);
	} else {
		printf("hda: controller doesn't support msi-x or msi\n");
		return;
	}

	printf("hda: found %lu.%lu controller at %02x:%02x.%x\n", regs->vmaj, regs->vmin, e->bus, e->device, e->function);

	// we don't support controllers without 64-bit addressing for simplicity
	if ((regs->gcap & GCAP_64OK) == 0) {
		printf("hda: no 64-bit support\n");
		return;
	}

	if ((regs->gctl & GCTL_CRST) && !reset_set_wait(regs, false, 1)) {
		printf("hda: starting controller reset timed out\n");
		return;
	}

	if (!reset_set_wait(regs, true, 50)) {
		printf("hda: taking controller out of reset timed out\n");
		return;
	}

	// the specification says to wait 1ms to allow codecs to wake up etc.
	sched_sleep_us(1000);

	// we can fit all of corb/rirb in a single page
	void *corb_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
	__assert(corb_phys);
	void *corb = MAKE_HHDM(corb_phys);
	memset(corb, 0, PAGE_SIZE);

	void *rirb_phys = (void *)((uintptr_t)corb_phys + 1024);
	void *rirb = MAKE_HHDM(rirb_phys);

	uint8_t corbsize;
	size_t corb_entries;
	get_sizes(regs->corbsize, &corbsize, &corb_entries);

	// TODO: less magic values here
	regs->corblbase = (uint64_t)corb_phys & 0xffffffff;
	regs->corbhbase = ((uint64_t)corb_phys >> 32) & 0xffffffff;
	regs->corbsize = (regs->corbsize & 0xfc) | corbsize;
	if (!corb_reset_read_pointer(regs, 1)) {
		printf("hda: resetting CORB read pointer timed out\n");
		return;
	}
	regs->corbwp = 0;
	regs->corbctl = (regs->corbctl & ~CORBCTL_DMA_ENABLE) | CORBCTL_DMA_ENABLE;

	uint8_t rirbsize;
	size_t rirb_entries;
	get_sizes(regs->rirbsize, &rirbsize, &rirb_entries);

	regs->rirblbase = (uint64_t)rirb_phys & 0xffffffff;
	regs->rirbhbase = ((uint64_t)rirb_phys >> 32) & 0xffffffff;
	regs->rirbsize = (regs->rirbsize & 0xfc) | rirbsize;
	regs->rirbwp = 0x8000 | (regs->rirbwp & 0x7fff);
	regs->rintcnt = 1;

	hda_t *hda = alloc(sizeof(hda_t));
	__assert(hda);
	hda->regs = regs;
	hda->corb = corb;
	hda->corb_entries = corb_entries;
	SEMAPHORE_INIT(&hda->corb_space_semaphore, corb_entries);
	SPINLOCK_INIT(hda->corb_lock);
	hda->rirb = rirb;
	hda->rirb_entries = rirb_entries;
	hda->current_stream_tag = 1;

	hda->bi_stream_count = (regs->gcap >> 3) & 0x1f;
	hda->input_stream_count = (regs->gcap >> 8) & 0xf;
	hda->output_stream_count = (regs->gcap >> 12) & 0xf;

	if (hda->input_stream_count)
		hda->input_sd = regs->sds;

	if (hda->output_stream_count)
		hda->output_sd = &regs->sds[hda->input_stream_count];

	if (hda->bi_stream_count)
		hda->bi_sd = &regs->sds[hda->input_stream_count + hda->output_stream_count];

	hda->streams = alloc(sizeof(hda_stream_t *) * (hda->input_stream_count + hda->output_stream_count + hda->bi_stream_count));
	__assert(hda->streams);
	SPINLOCK_INIT(hda->stream_lock);
	hda_initialize_output_stream_pool(hda);

	isr_t *isr = interrupt_allocate(hda_isr, ARCH_EOI, IPL_AUDIO);
	__assert(isr);

	if (e->msix.exists) {
		pci_msixadd(e, 0, INTERRUPT_IDTOVECTOR(isr->id), 1, 0);
		pci_msixsetmask(e, 0);
	} else {
		pci_msisetbase(e, INTERRUPT_IDTOVECTOR(isr->id), 1, 0);
		pci_msisetmask(e, 0, 0);
	}

	isr->priv = hda;
	regs->rirbctl = (regs->rirbctl & 0xf8) | 3;
	regs->intctl = 0xc0000000;

	uint16_t statests = regs->statests & 0x7fff;
	for (int i = 0; i < 15; ++i) {
		if ((statests & (1 << i)) == 0)
			continue;

		init_codec(hda, i);
	}
}

void hda_init() {
	static const int device_ids[] = {0x2668, 0x8c20};

	for (size_t device = 0; device < sizeof(device_ids) / sizeof(device_ids[0]); ++device) {
		int i = 0;
		for (;;) {
			pcienum_t *e = pci_getenum(-1, -1, -1, 0x8086, device_ids[device], -1, i++);
			if (e == NULL)
				break;
			initcontroller(e);
		}
	}
}

INIT_ROUTINE_DEFINE(hda, INIT_ROUTINE_FLAGS_NONE, hda_init, acpi);
