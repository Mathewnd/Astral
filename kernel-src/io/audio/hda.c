#include <kernel/pci.h>
#include <logging.h>
#include <kernel/hda.h>
#include <kernel/init.h>
#include <kernel/page.h>
#include <list.h>
#include <semaphore.h>
#include <kernel/alloc.h>
#include <kernel/audio.h>

// TODO: support mono format as well
#define HDA_FORMAT_48KHZ_16BIT_STEREO 0x11

#define HDA_BUFFER_PAGE_COUNT 2
#define HDA_BUFFER_TOTAL_SIZE (HDA_BUFFER_PAGE_COUNT * PAGE_SIZE)

#define HDA_BDL_FLAGS_IOC 1

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
	volatile hda_sd_t *sd;
	void *bdl_phys;
	size_t tag;
	bool bi;
	hda_path_t path;
	size_t last;
	size_t fill_ptr;
	uint32_t last_lpib;
	uint64_t bytes_played;
	eventheader_t underrun_event;
} hda_stream_t;

typedef struct {
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
	hda_stream_t **streams;
} hda_t;

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

static void hda_stream_stop(hda_stream_t *stream) {
	stream->sd->ctl[0] &= ~HDA_SD_CTL_BYTE0_RUN;
}

static void hda_stream_start(hda_stream_t *stream) {
	stream->sd->ctl[0] |= HDA_SD_CTL_BYTE0_RUN;
}

static bool hda_reset_stream(hda_stream_t *stream) {
	hda_stream_stop(stream);

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
	stream->sd->fmt = HDA_FORMAT_48KHZ_16BIT_STEREO;
	stream->sd->bdpl = (uint64_t)stream->bdl_phys & 0xffffffff;
	stream->sd->bdph = ((uint64_t)stream->bdl_phys >> 32) & 0xffffffff;
	stream->sd->ctl[0] = HDA_SD_CTL_BYTE0_IOCE;
	stream->sd->ctl[2] = (stream->tag << 4) | (stream->bi ? HDA_SD_CTL_BYTE2_BI_OUTPUT : 0);
	stream->last_lpib = 0;
	stream->bytes_played = 0;
	stream->fill_ptr = 0;

	return 0;
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
			*(int *)buf = 48000;
			break;
		case AUDIO_STREAM_INFO_CHANNELS:
			*(int *)buf = 2;
			break;
		case AUDIO_STREAM_INFO_FORMAT:
			*(int *)buf = AUDIO_FORMAT_S16_LE;
			break;
		case AUDIO_STREAM_INFO_FIFO_FRAMES: {
			hda_update_playback_position(stream);
			uint64_t dma_bytes = stream->generic.bytes_submitted - stream->bytes_played;
			if (dma_bytes > HDA_BUFFER_TOTAL_SIZE)
				dma_bytes = HDA_BUFFER_TOTAL_SIZE;
			*(int *)buf = dma_bytes / 4; // TODO: handle other channel counts and formats here
			break;
		}
		case AUDIO_STREAM_INFO_PLAYED_FRAMES: {
			hda_update_playback_position(stream);
			*(uint64_t *)buf = stream->bytes_played / 4; // TODO: handle other channel counts and formats here
			break;
		}
	}
}

static audio_stream_ops_t hda_stream_ops = {
	.start = stream_start,
	.stop = stream_stop,
	.wait_for_playback = stream_wait_for_playback,
	.reset = stream_reset,
	.get_info = stream_get_info
};

// TODO: fix memory leak
static hda_stream_t *hda_allocate_output_stream(hda_t *hda) {
	if (hda->current_stream_tag > 15) {
		printf("hda: out of stream tags\n");
		return NULL;
	}

	hda_stream_t *stream = alloc(sizeof(hda_stream_t));
	__assert(stream);
	__assert(audio_initialize_stream(&stream->generic, &hda_stream_ops) == 0);
	EVENT_INITHEADER(&stream->underrun_event);

	size_t stream_n;
	if (hda->allocated_output_stream_count < hda->output_stream_count) {
		stream->sd = &hda->output_sd[hda->allocated_output_stream_count];
		stream_n = hda->input_stream_count + hda->allocated_output_stream_count;
		++hda->allocated_output_stream_count;
	} else if (hda->allocated_bi_stream_count < hda->bi_stream_count) {
		stream->sd = &hda->bi_sd[hda->allocated_bi_stream_count];
		stream_n = hda->input_stream_count + hda->output_stream_count + hda->allocated_bi_stream_count;
		++hda->allocated_bi_stream_count;
		stream->bi = true;
	} else {
		printf("hda: no more free streams\n");
		return NULL;
	}

	hda->streams[stream_n] = stream;
	hda->regs->intctl |= (1 << stream_n);

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

	stream->tag = hda->current_stream_tag++;
	hda_reset_stream(stream);

	return stream;
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
	uint32_t status = hda->regs->intsts & 0x3f;

	for (int i = 0; i < 30; ++i) {
		if ((status & (1 << i)) == 0)
			continue;

		hda_stream_t *stream = hda->streams[i];
		if (stream == NULL)
			 continue;

		uint8_t strsts = stream->sd->sts;
		stream->sd->sts = strsts & (HDA_SD_STS_BCIS | HDA_SD_STS_DESE | HDA_SD_STS_FIFOE);
		if (strsts & HDA_SD_STS_BCIS)
			hda_stream_fill_data(stream);
	}
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

	if (hda->regs->intsts & 0x3f)
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
	return hda_submit_verb_and_wait(hda, codec, nid, 0x2000 | format);
}

static uint32_t hda_set_converter_stream_channel(hda_t *hda, uint32_t codec, uint32_t nid, uint8_t stream, uint8_t base_channel) {
	return hda_submit_verb_and_wait(hda, codec, nid, 0x70600 | (stream << 4) | base_channel);
}

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

static void init_function_group(hda_t *hda, int codec, hda_fg_t *function_group) {
	function_group->type = hda_get_parameter(hda, codec, function_group->nid, HDA_PARAM_FUNCTION_GROUP_TYPE) & 0xff;
	if (function_group->type != HDA_FUNCTION_GROUP_TYPE_AFG)
		return;

	uint32_t res = hda_get_parameter(hda, codec, function_group->nid, HDA_PARAM_SUB_NODE_COUNT);
	function_group->widget_count = res & 0xff;
	function_group->starting_node = (res >> 16) & 0xff;

	printf("hda: codec %d audio function group %lu has %lu widgets starting at %lu\n", codec, function_group->nid, function_group->widget_count, function_group->starting_node);

	function_group->widgets = alloc(sizeof(hda_widget_t) * function_group->widget_count);
	__assert(function_group->widgets);

	for (size_t i = 0; i < function_group->widget_count; ++i) {
		hda_widget_t *widget = &function_group->widgets[i];
		widget->nid = function_group->starting_node + i;

		res = hda_get_parameter(hda, codec, widget->nid, HDA_PARAM_AUDIO_WIDGET_CAPS);
		widget->type = (res >> 20) & 0xf;
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

		hda_initialize_path(hda, codec, function_group, path, true);

		hda_stream_t *stream = hda_allocate_output_stream(hda);
		if (stream == NULL) {
			printf("hda: failed to allocate stream\n");
			continue;
		}

		stream->path = path;

		hda_widget_t *converter = path.widgets[path.size - 1];
		hda_set_converter_format(hda, codec, converter->nid, HDA_FORMAT_48KHZ_16BIT_STEREO);
		hda_set_converter_stream_channel(hda, codec, converter->nid, stream->tag, 0);
		__assert(audio_register_stream(&stream->generic) == 0);
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
	regs->corbrp = 0x8000 | (regs->corbrp & 0x7fff);
	regs->corbwp = regs->corbwp & 0xff00;
	regs->corbctl = (regs->corbctl & 0xfc) | 2;

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
	int i = 0;
	for (;;) {
		pcienum_t *e = pci_getenum(-1, -1, -1, 0x8086, 0x2668, -1, i++);
		if (e == NULL)
			break;
		initcontroller(e);
	}
}

INIT_ROUTINE_DEFINE(hda, INIT_ROUTINE_FLAGS_NONE, hda_init, acpi);
