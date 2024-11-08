
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdint.h>
#include <stdbool.h>

#include "ipc_icxmsg_common.h"

#include "icmsg.h"

LOG_MODULE_REGISTER(icmsg, CONFIG_ICBMSG_LOG_LEVEL);


enum {
	ICMSG_STATE_UNINITIALIZED, /**< Instance is not initialized yet. Sending will fail. */
	ICMSG_STATE_INITIALIZING, /**< Instance is initializing - waiting for remote to acknowledge. Sending will fail. */
	ICMSG_STATE_CONNECTED, /**< Instance is connected. Sending will be successful.*/
	ICMSG_STATE_DISCONNECTED, /**< Instance was connected, but get disconnected. Sending will be silently discarded, because it there may be old sends. */
};

#if ICMSG_SHARED_THREAD_ENABLED || ICBMSG_SHARED_THREAD_ENABLED
static K_THREAD_STACK_DEFINE(icxmsg_shared_stack, ICMSG_SHARED_THREAD_STACK_SIZE);
struct k_work_q icxmsg_shared_workq;
#endif

static void receive_message(const struct icmsg_config_t *conf, struct icmsg_data_t *dev_data, uint32_t rx_write_index, uint32_t local_session)
{
	volatile const uint32_t *src_ptr;
	volatile const uint32_t *fifo_end;
	volatile const uint32_t *msg_end;
	uint32_t *dst_ptr;
	uint32_t header;
	uint8_t msg_session;
	uint32_t msg_length;
	uint32_t msg_words;
	uint32_t total_incoming_words;
	uint8_t msg_session_expected;
	uint32_t rx_read_index;
	uint32_t fifo_used_words;

	// Read data that should be handled inside lock
	msg_session_expected = local_session & 0xFF;
	rx_read_index = dev_data->rx.read_index;

	// Pointer that will be used to read message
	src_ptr = &conf->rx.buffer[rx_read_index];

	// Invalidate message header
	sys_cache_data_invd_range((void*)src_ptr, sizeof(uint32_t));
	__sync_synchronize();

	// Read and interpret message header
	header = *src_ptr;
	msg_session = header >> 24;
	msg_length = header & 0xFFFFFF;
	msg_words = (msg_length + 3) / 4;

	// Read total words in all incoming messages (not only the current one)
	fifo_used_words = rx_write_index - rx_read_index;
	if ((int)fifo_used_words < 0) {
		fifo_used_words += conf->rx.buffer_words;
	}

	// Message must not be bigger than total pending data in RX FIFO
	if (msg_words >= fifo_used_words) {
		__ASSERT(false, "Incoming message is corrupted.");
		rx_read_index = rx_write_index;
		// todo: what to do with the errors
		goto update_index_and_return;
	}

	// Increment local RX read index to the end of current message
	rx_read_index += 1 + msg_words;
	if (rx_read_index > conf->rx.buffer_words) {
		rx_read_index -= conf->rx.buffer_words;
	}

	if (msg_words > conf->rx_buffer_words) {
	
		__ASSERT(false, "Incoming message does not fit into RX buffer.");
		// todo: what to do with the errors
	
	} else if (msg_session == msg_session_expected) {

		// Calculate pointer where copying will end
		fifo_end = &conf->rx.buffer[conf->rx.buffer_words];
		msg_end = &conf->rx.buffer[rx_read_index];

		// Pointer that will be used to write message
		dst_ptr = conf->rx_buffer;

		// Move source pointer to data after header
		src_ptr++;

		// If message was wrapped, we need to copy first part from "ptr" to "fifo_end".
		if (msg_end < src_ptr) {
			sys_cache_data_invd_range((void*)src_ptr, (uint8_t*)fifo_end - (uint8_t*)src_ptr);
			__sync_synchronize();
			while (src_ptr != fifo_end) {
				*dst_ptr = *src_ptr;
				dst_ptr++;
				src_ptr++;
			};
			src_ptr = &conf->rx.buffer;
		}

		// The remaining part of the message from "ptr" to "msg_end".
		sys_cache_data_invd_range((void*)src_ptr, (uint8_t*)msg_end - (uint8_t*)src_ptr);
		__sync_synchronize();
		while (src_ptr != msg_end) {
			*dst_ptr = *src_ptr;
			dst_ptr++;
			src_ptr++;
		}

		// Call the user callback
		dev_data->cb->received(conf->rx_buffer, msg_length, dev_data->ctx);
	}

	// todo: ICMsg will not support send timeout. It would cause additional unnecessary
	// interrupts after receiving each message. Or, DTS option?
update_index_and_return:

	conf->rw_ctrl->rx_read_index = rx_read_index;
	dev_data->rx.read_index = rx_read_index;
}

static bool callback_iteration(struct icmsg_data_t *dev_data, bool lock)
{
	bool rerun = false;
	const struct icmsg_config_t *conf = dev_data->conf;
	k_spinlock_key_t key;
	uint32_t session_handshake;
	uint16_t remote_session_req;
	uint16_t local_session_ack;
	bool notify_remote = false;

	sys_cache_data_invd_range((void*)conf->ro_ctrl, sizeof(*conf->ro_ctrl));
	__sync_synchronize();

	if (lock) {
		key = k_spin_lock(&dev_data->lock);
	}

	// Read session handshake data from shared memory
	session_handshake = conf->ro_ctrl->session_handshake;
	remote_session_req = session_handshake & 0xFFFF;
	local_session_ack = session_handshake >> 16;

	switch (dev_data->state) {

	case ICMSG_STATE_INITIALIZING:
		// todo: check if remote is v1.0
		// We are initializing, so we are able to acknowledge remote session immediately
		if (dev_data->remote_session != remote_session_req) {
			// Setup RX FIFO data, since remote has already prepared its TX
			dev_data->rx.read_index =  conf->ro_ctrl->rx_write_index;
			conf->rw_ctrl->rx_read_index = dev_data->rx.read_index;
			// Acknowledge
			dev_data->remote_session = remote_session_req;
			conf->rw_ctrl->remote_session_ack = dev_data->remote_session;
			notify_remote = true;
		}
		// Remote acknowledged our local session, so connection is ready
		if (local_session_ack == dev_data->local_session
			&& (dev_data->remote_session & 0x8000) == 0) {
			/* Since we got remote_session_req and local_session_ack at once, we know
			 * that there was not race condition between them. Sessions request is
			 * set by remote before session acknowledgement, so we known that at this
			 * point both sides have valid session identifiers.
			 */
			dev_data->state = ICMSG_STATE_CONNECTED;
			if (lock) {
				k_spin_unlock(&dev_data->lock, key);
			}
			if (dev_data->cb->bound) {
				dev_data->cb->bound(dev_data->ctx);
			}
			// Rerun handler in new state.
			rerun = true;
			notify_remote = true;
			goto synchronize_and_return;
		}
		break;

	case ICMSG_STATE_CONNECTED:
		if (dev_data->remote_session != remote_session_req) {
			// Remote session has change, so we are disconnected now.
			// Only solution is to call open() again to open the session from the
			// beginning.
			dev_data->state = ICMSG_STATE_DISCONNECTED;
			if (lock) {
				k_spin_unlock(&dev_data->lock, key);
			}
			if (dev_data->cb->unbound) {
				dev_data->cb->unbound(dev_data->ctx);
			}
			// Return handler without rerunning, because there is nothing to do in the
			// UNINITIALIZED state.
		} else {
			if (lock) {
				k_spin_unlock(&dev_data->lock, key);
			}
			uint32_t rx_write_index = conf->ro_ctrl->rx_write_index;
			uint32_t local_session = dev_data->local_session;
			if (rx_write_index != dev_data->rx.read_index) {
				receive_message(conf, dev_data, rx_write_index, dev_data->local_session);
				rerun = (rx_write_index != dev_data->rx.read_index);
			}
		}
		goto synchronize_and_return;

	case ICMSG_STATE_UNINITIALIZED:
	case ICMSG_STATE_DISCONNECTED:
	default:
		break;
	}

	if (lock) {
		k_spin_unlock(&dev_data->lock, key);
	}

synchronize_and_return:

	// Flush the read-write control block
	__sync_synchronize();
	sys_cache_data_flush_range((void*)conf->rw_ctrl, sizeof(*conf->rw_ctrl));

	if (notify_remote) {
		(void)mbox_send_dt(&conf->mbox_tx, NULL);
	}

	return rerun;
}

static void callback_handling(struct icmsg_data_t *dev_data, bool lock)
{
	const struct icmsg_config_t *conf = dev_data->conf;
	bool rerun;
	
	do {
		rerun = callback_iteration(dev_data, lock);
		if (rerun && conf->yield_on_more_input && ICMSG_YIELD_ON_MORE_INPUT) {
			if (conf->dedicated_workq && ICMSG_DEDICATED_THREAD_ENABLED) {
				k_yield();
			} else if (conf->workq != NULL && ICMSG_WORKQUEUE_ENABLED) {
				k_work_submit_to_queue(dev_data->conf->workq, &dev_data->work);
				return;
			}
		}
	} while (rerun);
}


static void mbox_callback(const struct device *instance, uint32_t channel, void *user_data,
			  struct mbox_msg *msg_data)
{
	struct icmsg_data_t *dev_data = user_data;

#if ICMSG_NO_THREAD_ENABLED && ICMSG_WORKQUEUE_ENABLED
	if (dev_data->conf->workq == NULL) {
		callback_handling(dev_data, false);
	} else {
		k_work_submit_to_queue(dev_data->conf->workq, &dev_data->work);
	}
#elif ICMSG_NO_THREAD_ENABLED
	callback_handling(dev_data, false);
#else
	k_work_submit_to_queue(dev_data->conf->workq, &dev_data->work);
#endif
}

static void work_process(struct k_work *item)
{
	struct icmsg_data_t *dev_data = CONTAINER_OF(item, struct icmsg_data_t, work);

	callback_handling(dev_data, true);
}

int icmsg_send(const struct icmsg_config_t *conf,
	       struct icmsg_data_t *dev_data,
	       const void *msg, size_t len)
{
	uint32_t words_needed = 1 + (len + 3) / 4;

	// Locking should be done on upper layer

	int state = dev_data->state;
	if (state != ICMSG_STATE_CONNECTED) {
		/* todo: compatibility
		if (state == ICMSG_STATE_COMPATIBILITY && ICMSG_COMPATIBILITY_ENABLED) {
			return icmsg_send_v1(conf, dev_data, msg, len);
		} else*/ if (state <= ICMSG_STATE_INITIALIZING) {
			return -EINVAL;
		} else {
			return 0;
		}
	}

	if (words_needed >= conf->tx.buffer_words) {
		return -ENOMEM;
	}

	// Calculate the number of words available on the FIFO using private indexes.
	sys_cache_data_invd_range((void*)conf->ro_ctrl, sizeof(*conf->ro_ctrl));
	__sync_synchronize();
	uint32_t read_index = conf->ro_ctrl->tx_read_index % conf->tx.buffer_words;
	uint32_t words_available =
		(conf->tx.buffer_words - (dev_data->tx.write_index - read_index) - 1)
		% conf->tx.buffer_words;
	if (words_needed > words_available) {
		// Wake up remote if it is sleeping
		(void)mbox_send_dt(&conf->mbox_tx, NULL);
		return -ENOMEM; // todo: some other error code?
	}

	// Calculate indexes and pointer needed for placing data to FIFO
	uint32_t end_byte_index = 4 * dev_data->tx.write_index + 4 + len;
	if (end_byte_index >= 4 * conf->tx.buffer_words) {
		end_byte_index -= 4 * conf->tx.buffer_words;
	}
	uint32_t* start_ptr = &conf->tx.buffer[dev_data->tx.write_index];
	uint32_t* wrap_ptr = &conf->tx.buffer[conf->tx.buffer_words];
	uint8_t* end_byte_ptr = (uint8_t*)conf->tx.buffer + end_byte_index;
	// Ending word pointer points to end of whole words, so it is rounded down
	// pointer to the actual end end_byte_ptr.
	uint32_t* end_word_ptr = &conf->tx.buffer[end_byte_index / 4];
	uint32_t* ptr = start_ptr;

	// Write header
	uint32_t header = len | (dev_data->remote_session << 24);
	*ptr = header;
	ptr++;

	// Copy data word-by-word as much as possible
	uint32_t* src_ptr = msg;
	if ((uintptr_t)msg & 3 == 0) {
		while (true) {
			if (ptr == wrap_ptr) {
				ptr = conf->tx.buffer;
			}
			if (ptr == end_word_ptr) {
				break;
			}
			*ptr = *src_ptr;
			ptr++;
			src_ptr++;
		}
	}

	// Copy remaining data byte-by-byte
	uint8_t* byte_ptr = ptr;
	uint8_t* src_byte_ptr = src_ptr;
	while (true) {
		if (byte_ptr == (uint8_t*)wrap_ptr) {
			byte_ptr = (uint8_t*)conf->tx.buffer;
		}
		if (byte_ptr == end_byte_ptr) {
			break;
		}
		*byte_ptr = *src_byte_ptr;
		byte_ptr++;
		src_byte_ptr++;
	}

	// Flush cache at updated area
	__sync_synchronize();
	if (end_byte_ptr >= (uint8_t*)start_ptr) {
		// No wrap was done, flush from start_ptr to end_byte_ptr.
		sys_cache_data_flush_range((uint8_t*)start_ptr, end_byte_ptr - (uint8_t*)start_ptr);
	} else {
		// Wrap was done, so flush from start_ptr to the end of buffer and...
		sys_cache_data_flush_range((uint8_t*)start_ptr, (uint8_t*)wrap_ptr - (uint8_t*)start_ptr);
		// from beginning of buffer to the end_byte_ptr.
		sys_cache_data_flush_range(conf->tx.buffer, end_byte_ptr - (uint8_t*)conf->tx.buffer);
	}

	// Update write index and flush it.
	dev_data->tx.write_index = (end_byte_ptr - (uint8_t*)conf->tx.buffer + 3) / 4;
	conf->rw_ctrl->tx_write_index = dev_data->tx.write_index;
	__sync_synchronize();
	sys_cache_data_flush_range(&conf->rw_ctrl->tx_write_index, sizeof(conf->rw_ctrl->tx_write_index));

	// Notify the remote
	return mbox_send_dt(&conf->mbox_tx, NULL);
}

int icmsg_open(const struct icmsg_config_t *conf, struct icmsg_data_t *dev_data,
	       const struct ipc_service_cb *cb, void *ctx)
{
	k_spinlock_key_t key;
	int err;
	uint32_t local_session_ack;
	bool is_first_time;

	// Initialize basic fields
	is_first_time = (dev_data->conf != conf);
	dev_data->conf = conf;
	dev_data->cb = cb;
	dev_data->ctx = ctx;

	// Invalidate control blocks, since we need to read from them.
	sys_cache_data_invd_range((void*)conf->rw_ctrl, sizeof(*conf->rw_ctrl));
	sys_cache_data_invd_range((void*)conf->ro_ctrl, sizeof(*conf->ro_ctrl));
	__sync_synchronize();

	key = k_spin_lock(&dev_data->lock);

	dev_data->remote_session = -1;

	// Copy TX read and write indexes into local storage and validate it.
	dev_data->tx.write_index = conf->rw_ctrl->tx_write_index % conf->tx.buffer_words;

	// Set protocol version number
	conf->rw_ctrl->version = 0;

	// Calculate local session number (must be different from current req, ack and zero).
	dev_data->local_session = conf->rw_ctrl->local_session_req;
	local_session_ack = conf->ro_ctrl->local_session_ack;
	do {
		dev_data->local_session = (dev_data->local_session + 1) & 0x7FFFF;
	} while (dev_data->local_session == local_session_ack || (dev_data->local_session & 0xFF) == 0);

	// Write requested session number back to control block
	conf->rw_ctrl->local_session_req = dev_data->local_session;

	// Go to INITIALIZING state
	dev_data->state = ICMSG_STATE_INITIALIZING;

	k_spin_unlock(&dev_data->lock, key);

	// Make sure that control block has been written.
	__sync_synchronize();
	sys_cache_data_flush_range((void*)conf->rw_ctrl, sizeof(*conf->rw_ctrl));

	if (is_first_time) {
#if ICMSG_DEDICATED_THREAD_ENABLED || ICMSG_SHARED_THREAD_ENABLED || ICMSG_SYSTEM_WORK_QUEUE_ENABLED
		k_work_init(&dev_data->work, work_process);
#endif
		err = mbox_register_callback_dt(&conf->mbox_rx, mbox_callback, dev_data);
		err = err | mbox_set_enabled_dt(&conf->mbox_rx, true);
		if (err != 0) {
			LOG_ERR("Error setting MBOX callback");
			return -EIO; // todo: what error code?
		}
	}

	(void)mbox_send_dt(&conf->mbox_tx, NULL);

	return 0;
}

int icmsg_close(const struct icmsg_config_t *conf, struct icmsg_data_t *dev_data)
{
	k_spinlock_key_t key;
	int err;

	err = mbox_set_enabled_dt(&conf->mbox_rx, false);
	err = err | mbox_register_callback_dt(&conf->mbox_rx, NULL, NULL);
	if (err != 0) {
		return -EIO;
	}

#if ICMSG_DEDICATED_THREAD_ENABLED || ICMSG_SHARED_THREAD_ENABLED || ICMSG_SYSTEM_WORK_QUEUE_ENABLED
	if (conf->workq != NULL) {
		(void)k_work_cancel(&dev_data->work);
	}
#endif

	key = k_spin_lock(&dev_data->lock);

	// Set close bit in local session, this will inform remote about disconnect.
	dev_data->local_session = (dev_data->local_session + 1) | 0x8000;
	conf->rw_ctrl->local_session_req = dev_data->local_session;

	// Go to UNINITIALIZED state
	dev_data->state = ICMSG_STATE_UNINITIALIZED;
	dev_data->conf = NULL;

	k_spin_unlock(&dev_data->lock, key);

	// Make sure that control block has been written.
	__sync_synchronize();
	sys_cache_data_flush_range((void*)conf->rw_ctrl, sizeof(*conf->rw_ctrl));

	// Send notification to remote about session closed.
	(void)mbox_send_dt(&conf->mbox_tx, NULL);

	return 0;
}

void icmsg_init()
{
	static bool initialized = false;
	if (initialized) {
		return;
	}

#if ICMSG_SHARED_THREAD_ENABLED || ICBMSG_SHARED_THREAD_ENABLED

	static const struct k_work_queue_config cfg = { .name = "icmsg_shared_workq" };

	k_work_queue_start(&icxmsg_shared_workq,
			   icxmsg_shared_stack,
			   K_KERNEL_STACK_SIZEOF(icxmsg_shared_stack),
			   ICMSG_SHARED_THREAD_PRIORITY,
			   &cfg);

#endif

	initialized = true;
}

/*
static inline int icmsg_send_short_unchecked(const struct icmsg_config_t *conf,
	       struct icmsg_data_t *dev_data, uint8_t byte0, uint8_t byte1, uint8_t byte2)
{
	uint32_t header = byte0 | (byte1 << 8) | (byte2 << 16) | (dev_data->remote_session << 24);
	uint32_t* ptr = &dev_data->write_buffer[dev_data->tx.write_index];
	*ptr = header;
	__sync_synchronize();
	sys_cache_data_flush_range(ptr, sizeof(*ptr));
	dev_data->tx.write_index++;
	*dev_data->tx.shared_write_index = dev_data->tx.write_index;
	__sync_synchronize();
	sys_cache_data_flush_range(dev_data->tx.shared_write_index, sizeof(*dev_data->tx.shared_write_index));
	return mbox_send_dt(&conf->tx.mbox, NULL);
}


int icmsg_send(const struct icmsg_config_t *conf,
	       struct icmsg_data_t *dev_data,
	       const void *msg, size_t len)
{
	uint32_t words_needed = 1 + (len + 3) / 4;

	// Locking should be done on upper layer

	int state = dev_data->state;
	if (state != ICMSG_STATE_CONNECTED) {
		if (state == ICMSG_STATE_COMPATIBILITY && ICMSG_COMPATIBILITY_ENABLED) {
			return icmsg_send_v1(conf, dev_data, msg, len);
		} else if (state <= ICMSG_STATE_INITIALIZING) {
			return -ENRDY;
		} else {
			return 0;
		}
	}

	// Calculate the number of words available on the FIFO using private indexes.
	uint32_t words_available =
		(dev_data->tx.buffer_words - (dev_data->tx.write_index - dev_data->tx.read_index) - 1)
		% dev_data->tx.buffer_words;

	// If private indexes tells us that there is no space left, read read_index from shared memory
	// and retry.
	if (words_needed > words_available) {
		if (words_needed >= dev_data->tx.buffer_words) {
			return -ENOMEM;
		}
		sys_cache_data_invd_range(dev_data->tx.shared_read_index, sizeof(*dev_data->tx.shared_read_index));
		__sync_synchronize();
		dev_data->tx.read_index = *dev_data->tx.shared_read_index % dev_data->tx.buffer_words;
		words_available =
			(dev_data->tx.buffer_words - (dev_data->tx.write_index - dev_data->tx.read_index) - 1)
			% dev_data->tx.buffer_words;
		if (words_needed > words_available) {
			return -ENOMEM; // todo: some other error code
		}
	}

	// Calculate indexes and pointer needed for placing data to FIFO
	uint32_t end_byte_index = dev_data->tx.write_index + 4 + len;
	if (end_byte_index >= 4 * dev_data->tx.buffer_words) {
		end_byte_index -= 4 * dev_data->tx.buffer_words;
	}
	uint32_t* start_ptr = &dev_data->write_buffer[dev_data->tx.write_index];
	uint32_t* wrap_ptr = &dev_data->write_buffer[dev_data->tx.buffer_words];
	uint8_t* end_byte_ptr = (uint8_t*)dev_data->write_buffer + end_byte_index;
	// Ending word pointer points to end of whole words, so it is rounded down
	// pointer to the actual end end_byte_ptr.
	uint32_t* end_word_ptr = &dev_data->write_buffer[end_byte_index / 4];

	// Write header
	uint32_t header = len | (dev_data->remote_session << 24);
	*ptr = header;
	ptr++;
	if (ptr == wrap_ptr) {
		ptr = dev_data->write_buffer;
	}

	// Copy data word-by-word as much as possible
	uint32_t* ptr = start_ptr;
	uint32_t* src_ptr = msg;
	if ((uintptr_t)msg & 3 == 0) {
		while (ptr != end_ptr) {
			*ptr = *src_ptr;
			ptr++;
			src_ptr++;
			if (ptr == wrap_ptr) {
				ptr = dev_data->write_buffer;
			}
		}
	}

	// Copy remaining data byte-by-byte
	uint8_t* byte_ptr = ptr;
	uint8_t* src_byte_ptr = src_ptr;
	while (byte_ptr != end_byte_ptr) {
		*byte_ptr = *src_byte_ptr;
		byte_ptr++;
		src_byte_ptr++;
		if (byte_ptr == (uint8_t*)wrap_ptr) {
			byte_ptr = (uint8_t*)dev_data->write_buffer;
		}
	}

	// Flush cache at updated area
	__sync_synchronize();
	if (byte_ptr >= (uint8_t*)start_ptr) {
		// No wrap was done, flush from start_ptr to current byte_ptr.
		sys_cache_data_flush_range((uint8_t*)start_ptr, byte_ptr - (uint8_t*)start_ptr);
	} else {
		// Wrap was done, so flush from start_ptr to the end of buffer and...
		sys_cache_data_flush_range((uint8_t*)start_ptr, (uint8_t*)wrap_ptr - (uint8_t*)start_ptr);
		// from beginning of buffer to the current byte_ptr.
		sys_cache_data_flush_range(dev_data->write_buffer, byte_ptr - (uint8_t*)dev_data->write_buffer);
	}

	// Update write index and flush it.
	dev_data->tx.write_index = (byte_ptr - (uint8_t*)dev_data->write_buffer + 3) / 4;
	*dev_data->tx.shared_write_index = dev_data->tx.write_index;
	__sync_synchronize();
	sys_cache_data_flush_range(dev_data->tx.shared_write_index, sizeof(*dev_data->tx.shared_write_index));

	// Notify the remote
	return mbox_send_dt(&conf->mbox_tx, NULL);
}

struct icmsg_config_fifo_t {
	uint32_t *buffer;
	uint32_t buffer_words;
};

struct icmsg_config_t {
	// TX fifo buffer
	struct {
		uint32_t *buffer;
		uint32_t buffer_words;
	} tx;
	// RX fifo buffer
	struct {
		const uint32_t *buffer;
		uint32_t buffer_words;
	} rx;
	// Read-write control fields
	struct
	{
		uint32_t tx_write_index;
		uint32_t rx_read_index;
		uint16_t local_session_req;
		uint16_t remote_session_ack;
	} *rw_ctrl;
	// Read-only control fields
	const struct
	{
		uint32_t rx_write_index;
		uint32_t tx_read_index;
		// Two fields are grouped together allowing simultaneous read
		uint32_t session_handshake;
	} *ro_ctrl;
};

struct icmsg_data_t {
	// local copy of TX FIFO indexes
	struct
	{
		uint32_t read_index;
		uint32_t write_index;
	} tx;
	// local copy of RX FIFO indexes
	struct
	{
		uint32_t read_index;
	} rx;
	// Local session id
	uint32_t local_session;
	// Current remote session id or -1 if unknown.
	uint32_t remote_session;
	const struct icmsg_config_t *conf;
};

static inline void read_remote_session_req_and_local_session_ack(
	const struct icmsg_config_t *conf,
	struct icmsg_data_t *dev_data)
{
	// todo: use directly
	/*uint32_t value = conf->rd_ctrl.session_handshake;
	uint16_t remote_session_req = value & 0xFFFF; // todo: check current architecture big/little endian
	uint16_t local_session_ack = value >> 16;* /
}


int icmsg_open(const struct icmsg_config_t *conf,
	       struct icmsg_data_t *dev_data,
	       const struct ipc_service_cb *cb, void *ctx)
{
	// Locking should be done on upper layer

	// todo: state checking
	// todo: old magic setup
	// todo: invalidate tx and rx metadata

	dev_data->remote_session = -1;
	dev_data->tx.write_index = *dev_data->tx.shared_write_index % dev_data->tx.buffer_words;
	dev_data->local_session = *dev_data->local_session_req + 1;
	while (dev_data->local_session == *dev_data->local_session_ack || dev_data->local_session & 0xFF == 0) {
		dev_data->local_session++;
	}
	*dev_data->local_session_req = dev_data->local_session;
	sys_cache_data_flush_range(dev_data->wr, sizeof(*dev_data->wr));
	mbox_send_dt(&conf->mbox_tx, NULL);
	mbox_send_dt(&conf->mbox_rx, NULL);
}

static void callback_handling(const struct icmsg_config_t *conf,
	       struct icmsg_data_t *dev_data)
{
	// invalidate cache
	uint32_t value = conf->ro_ctrl.session_handshake;
	uint16_t remote_session_req = value & 0xFFFF; // todo: check current architecture big/little endian
	uint16_t local_session_ack = value >> 16;
	if (remote_session_req != dev_data->remote_session) {
		bool closed = (remote_session_req & 1) = 0;
		if (closed) {
			// todo: set state
			dev_data->callbacks->set_state(IPC_SERVICE_STATE_UNCONNECTED);
		} else {
			// todo: set state
			dev_data->callbacks->set_state(IPC_SERVICE_STATE_RESET);
		}
		// todo: if re-opened during callback, maybe continue to process messages
		return;
	}
	// invalidate cache
	uint32_t rx_write_index = conf->ro_ctrl->rx_write_index;
	if (rx_write_index != dev_data->rx.read_index) {
		// todo: receive messages ignoring different session id.
	}
}

*/
