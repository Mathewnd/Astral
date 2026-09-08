#include <u80211/michael.h>
#include <u80211/string.h>
#include <u80211/util.h>

typedef struct {
	uint32_t left;
	uint32_t right;
	uint8_t pending[4];
	size_t pending_size;
} michael_context_t;

static uint32_t rotate_left(uint32_t value, unsigned int amount) {
	return (value << amount) | (value >> (32 - amount));
}

static uint32_t rotate_right(uint32_t value, unsigned int amount) {
	return (value >> amount) | (value << (32 - amount));
}

static uint32_t exchange_bytes(uint32_t value) {
	return ((value & 0x00ff00ff) << 8) | ((value & 0xff00ff00) >> 8);
}

static void michael_block(michael_context_t *context, uint32_t word) {
	context->left ^= word;
	context->right ^= rotate_left(context->left, 17);
	context->left += context->right;
	context->right ^= exchange_bytes(context->left);
	context->left += context->right;
	context->right ^= rotate_left(context->left, 3);
	context->left += context->right;
	context->right ^= rotate_right(context->left, 2);
	context->left += context->right;
}

static void michael_update(michael_context_t *context, const void *data, size_t size) {
	const uint8_t *bytes = data;

	if (context->pending_size != 0) {
		while (size != 0 && context->pending_size < sizeof(context->pending)) {
			context->pending[context->pending_size++] = *bytes++;
			size--;
		}

		if (context->pending_size == sizeof(context->pending)) {
			michael_block(context, deserialize_le32(context->pending));
			context->pending_size = 0;
		}
	}

	while (size >= sizeof(uint32_t)) {
		michael_block(context, deserialize_le32(bytes));
		bytes += sizeof(uint32_t);
		size -= sizeof(uint32_t);
	}

	if (size != 0) {
		u80211_memcpy(context->pending, bytes, size);
		context->pending_size = size;
	}
}

void u80211_michael_mic(const uint8_t key[8], const uint8_t da[6], const uint8_t sa[6], uint8_t priority, const void *data, size_t len, uint8_t out[8]) {
	uint8_t pseudo_header[16];
	u80211_memcpy(pseudo_header, da, 6);
	u80211_memcpy(pseudo_header + 6, sa, 6);
	pseudo_header[12] = priority;
	u80211_memset(pseudo_header + 13, 0, 3);

	michael_context_t context = {
		.left = deserialize_le32(key),
		.right = deserialize_le32(key + 4),
	};
	michael_update(&context, pseudo_header, sizeof(pseudo_header));
	michael_update(&context, data, len);

	const uint8_t padding_marker = 0x5a;
	michael_update(&context, &padding_marker, sizeof(padding_marker));
	const uint8_t zeroes[4] = { 0 };
	if (context.pending_size != 0)
		michael_update(&context, zeroes, sizeof(context.pending) - context.pending_size);
	michael_update(&context, zeroes, sizeof(zeroes));

	serialize_le32(out, context.left);
	serialize_le32(out + 4, context.right);
}
