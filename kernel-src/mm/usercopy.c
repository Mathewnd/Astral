#include <kernel/usercopy.h>
#include <arch/context.h>
#include <arch/cpu.h>

typedef struct {
	void *dst;
	void *src;
	size_t size;
} memcpydesc_t;

typedef struct {
	const char *str;
	size_t *size;
} strlendesc_t;

typedef struct {
	uint32_t *value;
	uint32_t *address;
} atomic32desc_t;

static void memcpyinternal(context_t *ctx, void *arg) {
	memcpydesc_t *desc = arg;

	// this sets up the thread so that in the next invalid page fault, it will jump to
	// this context with CTX_RET set to EFAULT
	current_thread()->usercopyctx = ctx;

	memcpy(desc->dst, desc->src, desc->size);

	// no errors!
	CTX_RET(ctx) = 0;
	current_thread()->usercopyctx = NULL;
}

static void strleninternal(context_t *ctx, void *arg) {
	strlendesc_t *desc = arg;

	// this sets up the thread so that in the next invalid page fault, it will jump to
	// this context with CTX_RET set to EFAULT
	current_thread()->usercopyctx = ctx;

	*desc->size = strlen(desc->str);

	// no errors!
	CTX_RET(ctx) = 0;
	current_thread()->usercopyctx = NULL;
}

static void atomic32internal(context_t *ctx, void *arg) {
	atomic32desc_t *desc = arg;

	// this sets up the thread so that in the next invalid page fault, it will jump to
	// this context with CTX_RET set to EFAULT
	current_thread()->usercopyctx = ctx;

	__atomic_load(desc->address, desc->value, __ATOMIC_SEQ_CST);

	// no errors!
	CTX_RET(ctx) = 0;
	current_thread()->usercopyctx = NULL;
}

int usercopy_touser(void *user, void *kernel, size_t size) {
	memcpydesc_t desc = {
		.dst = user,
		.src = kernel,
		.size = size
	};

	if (IS_USER_ADDRESS(user) == false)
		return EFAULT;

	return arch_context_saveandcall(memcpyinternal, NULL, &desc);
}

int usercopy_fromuser(void *kernel, void *user, size_t size) {
	memcpydesc_t desc = {
		.dst = kernel,
		.src = user,
		.size = size
	};

	if (IS_USER_ADDRESS(user) == false)
		return EFAULT;

	return arch_context_saveandcall(memcpyinternal, NULL, &desc);
}

int usercopy_strlen(const char *str, size_t *size) {
	strlendesc_t desc = {
		.str = str,
		.size = size
	};

	if (IS_USER_ADDRESS(str) == false)
		return EFAULT;

	return arch_context_saveandcall(strleninternal, NULL, &desc);
}

int usercopy_fromuseratomic32(uint32_t *user32, uint32_t *value) {
	atomic32desc_t desc = {
		.value = value,
		.address = user32
	};

	if (IS_USER_ADDRESS(user32) == false)
		return EFAULT;

	return arch_context_saveandcall(atomic32internal, NULL, &desc);
}
