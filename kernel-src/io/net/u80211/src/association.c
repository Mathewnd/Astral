#include <u80211/association.h>
#include <u80211/kernel_interface.h>
#include <u80211/status.h>
#include <u80211/util.h>

#define AUTH_TIMEOUT 5000
#define ASSOC_TIMEOUT 5000
#define DEAUTHENTICATION_REASON_LEAVING 3
// if more space is needed, this can be changed
#define ASSOCIATION_INFORMATION_ELEMENTS_MAX_SIZE 1024

typedef struct {
	void *completion_work;
	void *auth_timeout_work;
	void *auth_timeout_timer;
	void *assoc_timeout_work;
	void *assoc_timeout_timer;
	unsigned int generation;
	u80211_device_t *device;
	u80211_ap_t *ap; // this keeps a reference, the reference in the device is only incremented only when fully associated
	void *information_elements;
	size_t information_elements_size;
} u80211_association_context_t;

typedef struct {
	u80211_list_node_t node;
	void *semaphore;
	int result;
} u80211_association_waiter_t;

static void wake_association_waiters(u80211_device_t *device, int result) {
	u80211_list_node_t *node;
	while ((node = u80211_list_pop_front(&device->association_waiters)) != NULL) {
		u80211_association_waiter_t *waiter = container_of(node, u80211_association_waiter_t, node);
		waiter->result = result;
		u80211_kernel_signal_semaphore(waiter->semaphore);
	}
}

static void destroy_association_context(u80211_association_context_t *association_context) {
	u80211_kernel_free_timer(association_context->auth_timeout_timer);
	u80211_kernel_free_timer(association_context->assoc_timeout_timer);
	u80211_kernel_free_work(association_context->completion_work);
	u80211_kernel_free_work(association_context->auth_timeout_work);
	u80211_kernel_free_work(association_context->assoc_timeout_work);
	if (association_context->information_elements != NULL)
		u80211_kernel_free(association_context->information_elements);
	u80211_kernel_free(association_context);
}

bool u80211_association_is_duplicate(u80211_device_t *device, const u80211_header_description_t *header, int expected_state) {
	bool duplicate = false;
	u80211_kernel_acquire_spinlock(device->association_spinlock);
	bool active_stream = u80211_get_device_state(device) == expected_state && device->ap != NULL &&
		u80211_mac_address_equal(&device->ap->mac_address, &header->addresses[1]);
	if (active_stream) {
		duplicate = (header->frame_control & U80211_HEADER_FRAME_CONTROL_RETRY) &&
			device->received_sequence_control_valid && device->received_sequence_control == header->sequence_control;
		if (!duplicate) {
			device->received_sequence_control = header->sequence_control;
			device->received_sequence_control_valid = true;
		}
	}
	u80211_kernel_release_spinlock(device->association_spinlock);
	return duplicate;
}

static void release_disconnected_ap(void *ctx) {
	u80211_device_t *device = ctx;

	u80211_kernel_acquire_spinlock(device->association_spinlock);
	u80211_ap_t *ap = device->disconnected_ap;
	device->disconnected_ap = NULL;
	u80211_kernel_release_spinlock(device->association_spinlock);

	if (ap != NULL)
		u80211_ap_release(ap);
}

static void cleanup_association(void *ctx) {
	u80211_device_t *device = ctx;

	u80211_kernel_acquire_spinlock(device->association_spinlock);
	u80211_association_context_t *association_context = device->association_cleanup_context;
	u80211_ap_t *disconnected_ap = device->disconnected_ap;
	device->disconnected_ap = NULL;
	u80211_kernel_release_spinlock(device->association_spinlock);

	if (association_context != NULL) {
		u80211_ap_release(association_context->ap);
		destroy_association_context(association_context);
	}
	if (disconnected_ap != NULL)
		u80211_ap_release(disconnected_ap);

	if (association_context != NULL) {
		u80211_kernel_acquire_spinlock(device->association_spinlock);
		device->association_cleanup_context = NULL;
		device->association_cleanup_pending = false;
		wake_association_waiters(device, device->association_result);
		u80211_kernel_release_spinlock(device->association_spinlock);
	}
}

static void schedule_association_cleanup(u80211_device_t *device, u80211_association_context_t *association_context) {
	device->association_context = NULL;
	device->association_cleanup_context = association_context;
	device->association_cleanup_pending = true;
}

static void association_process_teardown(u80211_device_t *device, const u80211_mac_address_t *address, bool deauthentication) {
	bool cleanup_needed = false;

	u80211_kernel_acquire_spinlock(device->association_spinlock);

	if (!device->ap || !u80211_mac_address_equal(&device->ap->mac_address, address))
		goto leave;

	int state = u80211_get_device_state(device);
	bool attempt_in_progress = state == U80211_DEVICE_STATE_AUTHENTICATING || state == U80211_DEVICE_STATE_ASSOCIATING;
	if (state != U80211_DEVICE_STATE_ASSOCIATED && (!deauthentication || !attempt_in_progress))
		goto leave;

	if (!u80211_set_device_state(device, state, U80211_DEVICE_STATE_DOWN))
		goto leave;

	if (attempt_in_progress) {
		u80211_association_context_t *association_context = device->association_context;
		if (association_context == NULL)
			goto leave;
		schedule_association_cleanup(device, association_context);
		device->association_result = U80211_STATUS_REJECTED;
		cleanup_needed = true;
	} else {
		device->disconnected_ap = device->ap;
		cleanup_needed = true;
	}

	device->ap = NULL;
	++device->association_generation;

leave:
	u80211_kernel_release_spinlock(device->association_spinlock);
	if (cleanup_needed)
		u80211_kernel_enqueue_work(device->association_cleanup_work, cleanup_association, device);
}

void u80211_association_process_deauthentication(u80211_device_t *device, u80211_deauthentication_data_t *deauthentication_data) {
	association_process_teardown(device, &deauthentication_data->address, true);
}

void u80211_association_process_disassociation(u80211_device_t *device, u80211_disassociation_data_t *disassociation_data) {
	association_process_teardown(device, &disassociation_data->address, false);
}

void u80211_association_process_response(u80211_device_t *device, u80211_association_response_data_t *association_data) {
	bool cleanup_needed = false;
	u80211_kernel_acquire_spinlock(device->association_spinlock);

	u80211_association_context_t *association_context = device->association_context;
	if (!association_context || !device->ap)
		goto leave;

	if (!u80211_mac_address_equal(&association_context->ap->mac_address, &association_data->address))
		goto leave;

	int new_state = association_data->status ? U80211_DEVICE_STATE_DOWN : U80211_DEVICE_STATE_ASSOCIATED;
	if (!u80211_set_device_state(device, U80211_DEVICE_STATE_ASSOCIATING, new_state))
		goto leave;

	if (association_data->status)
		device->ap = NULL;
	else
		u80211_ap_hold(device->ap);

	schedule_association_cleanup(device, association_context);
	++device->association_generation;
	device->association_result = association_data->status ? U80211_STATUS_REJECTED : U80211_STATUS_SUCCESS;
	cleanup_needed = true;

leave:
	u80211_kernel_release_spinlock(device->association_spinlock);
	if (cleanup_needed)
		u80211_kernel_enqueue_work(device->association_cleanup_work, cleanup_association, device);
}

static void association_timeout(void *ctx, int expected_state) {
	u80211_association_context_t *association_context = ctx;
	u80211_device_t *device = association_context->device;
	bool cleanup_needed = false;

	u80211_kernel_acquire_spinlock(device->association_spinlock);

	// not associating/different association generation
	if (device->association_context != association_context || !device->ap ||
			device->association_generation != association_context->generation)
		goto leave;

	if (u80211_set_device_state(device, expected_state, U80211_DEVICE_STATE_DOWN)) {
		device->ap = NULL;
		schedule_association_cleanup(device, association_context);
		++device->association_generation;
		device->association_result = U80211_STATUS_TIMED_OUT;
		cleanup_needed = true;
	}

leave:
	u80211_kernel_release_spinlock(device->association_spinlock);
	if (cleanup_needed)
		u80211_kernel_enqueue_work(device->association_cleanup_work, cleanup_association, device);
}

static void assoc_timeout(void *ctx) {
	association_timeout(ctx, U80211_DEVICE_STATE_ASSOCIATING);
}

static void auth_completion_work(void *ctx) {
	u80211_association_context_t *association_context = ctx;
	u80211_device_t *device = association_context->device;

	u80211_send_association_request(device, association_context->information_elements, association_context->information_elements_size);
	u80211_kernel_enqueue_delayed_work(association_context->assoc_timeout_work, association_context->assoc_timeout_timer,
		assoc_timeout, association_context, ASSOC_TIMEOUT);
}

void u80211_association_process_authentication(u80211_device_t *device, u80211_auth_data_t *auth_data) {
	bool cleanup_needed = false;
	u80211_kernel_acquire_spinlock(device->association_spinlock);

	// not associating/different AP
	if (!device->ap || !u80211_mac_address_equal(&device->ap->mac_address, &auth_data->address))
		goto leave;

	// not the same algo/different transaction stage than expected
	if (auth_data->auth_algorithm != U80211_AUTH_ALGORITHM_OPEN || auth_data->auth_transaction != 2)
		goto leave;

	if (auth_data->status) {
		// failure
		if (!u80211_set_device_state(device, U80211_DEVICE_STATE_AUTHENTICATING, U80211_DEVICE_STATE_DOWN))
			goto leave;

		u80211_association_context_t *association_context = device->association_context;
		device->ap = NULL;
		schedule_association_cleanup(device, association_context);
		++device->association_generation;
		device->association_result = U80211_STATUS_REJECTED;
		cleanup_needed = true;
	} else {
		if (!u80211_set_device_state(device, U80211_DEVICE_STATE_AUTHENTICATING, U80211_DEVICE_STATE_ASSOCIATING))
			goto leave;

		u80211_association_context_t *ctx = device->association_context;
		u80211_kernel_enqueue_work(ctx->completion_work, auth_completion_work, ctx);
	}

leave:
	u80211_kernel_release_spinlock(device->association_spinlock);
	if (cleanup_needed)
		u80211_kernel_enqueue_work(device->association_cleanup_work, cleanup_association, device);
}

static void auth_timeout(void *ctx) {
	association_timeout(ctx, U80211_DEVICE_STATE_AUTHENTICATING);
}

int u80211_associate(u80211_device_t *device, u80211_ap_t *ap, const void *information_elements, size_t information_elements_size) {
	if (information_elements_size > ASSOCIATION_INFORMATION_ELEMENTS_MAX_SIZE)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	release_disconnected_ap(device);

	u80211_association_context_t *association_context = u80211_kernel_allocate(sizeof(u80211_association_context_t));
	if (association_context == NULL)
		return U80211_STATUS_ENOMEM;

	association_context->completion_work = u80211_kernel_allocate_work();
	if (association_context->completion_work == NULL) {
		u80211_kernel_free(association_context);
		return U80211_STATUS_ENOMEM;
	}

	association_context->auth_timeout_work = u80211_kernel_allocate_work();
	if (association_context->auth_timeout_work == NULL) {
		u80211_kernel_free_work(association_context->completion_work);
		u80211_kernel_free(association_context);
		return U80211_STATUS_ENOMEM;
	}

	association_context->auth_timeout_timer = u80211_kernel_allocate_timer();
	if (association_context->auth_timeout_timer == NULL) {
		u80211_kernel_free_work(association_context->auth_timeout_work);
		u80211_kernel_free_work(association_context->completion_work);
		u80211_kernel_free(association_context);
		return U80211_STATUS_ENOMEM;
	}

	association_context->assoc_timeout_work = u80211_kernel_allocate_work();
	if (association_context->assoc_timeout_work == NULL) {
		u80211_kernel_free_timer(association_context->auth_timeout_timer);
		u80211_kernel_free_work(association_context->auth_timeout_work);
		u80211_kernel_free_work(association_context->completion_work);
		u80211_kernel_free(association_context);
		return U80211_STATUS_ENOMEM;
	}

	association_context->assoc_timeout_timer = u80211_kernel_allocate_timer();
	if (association_context->assoc_timeout_timer == NULL) {
		u80211_kernel_free_work(association_context->assoc_timeout_work);
		u80211_kernel_free_timer(association_context->auth_timeout_timer);
		u80211_kernel_free_work(association_context->auth_timeout_work);
		u80211_kernel_free_work(association_context->completion_work);
		u80211_kernel_free(association_context);
		return U80211_STATUS_ENOMEM;
	}

	association_context->information_elements = NULL;
	association_context->information_elements_size = information_elements_size;
	if (information_elements_size != 0) {
		association_context->information_elements = u80211_kernel_allocate(information_elements_size);
		if (association_context->information_elements == NULL) {
			destroy_association_context(association_context);
			return U80211_STATUS_ENOMEM;
		}

		u80211_memcpy(association_context->information_elements, information_elements, information_elements_size);
	}

	association_context->device = device;
	association_context->ap = ap;

	u80211_kernel_acquire_spinlock(device->association_spinlock);
	if (device->association_cleanup_pending ||
			!u80211_set_device_state(device, U80211_DEVICE_STATE_DOWN, U80211_DEVICE_STATE_AUTHENTICATING)) {
		u80211_kernel_release_spinlock(device->association_spinlock);
		destroy_association_context(association_context);
		return U80211_STATUS_BUSY;
	}

	association_context->generation = device->association_generation;
	device->association_context = association_context;
	device->association_result = U80211_STATUS_UNKNOWN_ERROR;
	device->ap = ap;
	device->received_sequence_control = 0;
	device->received_sequence_control_valid = false;
	u80211_ap_hold(ap);
	u80211_kernel_release_spinlock(device->association_spinlock);

	u80211_auth_data_t auth_data = {
		.address = ap->mac_address,
		.auth_algorithm = U80211_AUTH_ALGORITHM_OPEN,
		.auth_transaction = 1,
		.status = 0,
	};
	device->ops->set_channel(device, ap->channel);
	u80211_send_authentication(device, &auth_data);

	u80211_kernel_enqueue_delayed_work(association_context->auth_timeout_work, association_context->auth_timeout_timer,
		auth_timeout, association_context, AUTH_TIMEOUT);

	return 0;
}

static int u80211_deauthenticate(u80211_device_t *device, uint16_t reason) {
	u80211_deauthentication_data_t deauthentication_data = {
		.reason = reason,
	};

	u80211_kernel_acquire_spinlock(device->association_spinlock);

	if (device->ap == NULL || u80211_get_device_state(device) != U80211_DEVICE_STATE_ASSOCIATED) {
		// TODO: Support cancelling an authentication or association attempt.
		u80211_kernel_release_spinlock(device->association_spinlock);
		return U80211_STATUS_NOT_ASSOCIATED;
	}

	if (!u80211_set_device_state(device, U80211_DEVICE_STATE_ASSOCIATED, U80211_DEVICE_STATE_DEAUTHENTICATING)) {
		u80211_kernel_release_spinlock(device->association_spinlock);
		return U80211_STATUS_NOT_ASSOCIATED;
	}

	deauthentication_data.address = device->ap->mac_address;

	u80211_kernel_release_spinlock(device->association_spinlock);

	int status = u80211_send_deauthentication(device, &deauthentication_data);
	u80211_ap_t *disconnected_ap = NULL;

	u80211_kernel_acquire_spinlock(device->association_spinlock);
	if (device->ap != NULL && u80211_get_device_state(device) == U80211_DEVICE_STATE_DEAUTHENTICATING) {
		disconnected_ap = device->ap;
		device->ap = NULL;
		++device->association_generation;
		u80211_set_device_state(device, U80211_DEVICE_STATE_DEAUTHENTICATING, U80211_DEVICE_STATE_DOWN);
	}
	u80211_kernel_release_spinlock(device->association_spinlock);

	if (disconnected_ap != NULL)
		u80211_ap_release(disconnected_ap);

	return status;
}

int u80211_disassociate(u80211_device_t *device) {
	return u80211_deauthenticate(device, DEAUTHENTICATION_REASON_LEAVING);
}

int u80211_wait_for_association_completion(u80211_device_t *device) {
	void *semaphore = u80211_kernel_allocate_semaphore(0);
	if (semaphore == NULL)
		return U80211_STATUS_RETRY;

	u80211_association_waiter_t waiter = {
		.semaphore = semaphore,
	};

	u80211_kernel_acquire_spinlock(device->association_spinlock);
	int state = u80211_get_device_state(device);
	if (state != U80211_DEVICE_STATE_AUTHENTICATING && state != U80211_DEVICE_STATE_ASSOCIATING &&
			!device->association_cleanup_pending) {
		int result = device->association_result;
		u80211_kernel_release_spinlock(device->association_spinlock);
		u80211_kernel_free_semaphore(semaphore);
		return result;
	}

	u80211_list_push_back(&device->association_waiters, &waiter.node);
	u80211_kernel_release_spinlock(device->association_spinlock);

	u80211_kernel_wait_semaphore(semaphore);
	u80211_kernel_free_semaphore(semaphore);
	return waiter.result;
}
