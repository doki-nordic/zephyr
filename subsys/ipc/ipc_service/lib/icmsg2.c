
#include <stdint.h>
#include <stdbool.h>

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
	uint16_t local_session_ack = value >> 16;*/
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

static void mbox_callback(const struct device *instance, uint32_t channel,
			  void *user_data, struct mbox_msg *msg_data)
{
	struct icmsg_data_t *dev_data = user_data;
#if ICMSG_NO_THREAD_ENABLED && !ICMSG_DEDICATED_THREAD_ENABLED && !ICMSG_SHARED_THREAD_ENABLED && !ICMSG_SYSTEM_WORK_QUEUE_ENABLED
	callback_handling(dev_data->conf, dev_data);
#elif (ICMSG_DEDICATED_THREAD_ENABLED || ICMSG_SHARED_THREAD_ENABLED) && !ICMSG_NO_THREAD_ENABLED && !ICMSG_SYSTEM_WORK_QUEUE_ENABLED
	k_work_submit_to_queue(dev_data->conf->workq, &dev_data->mbox_work);
#elif ICMSG_SYSTEM_WORK_QUEUE_ENABLED && !ICMSG_NO_THREAD_ENABLED && !ICMSG_DEDICATED_THREAD_ENABLED && !ICMSG_SHARED_THREAD_ENABLED
	k_work_submit(&dev_data->mbox_work);
#else
	switch (dev_data->thread_mode) {
#if ICMSG_NO_THREAD_ENABLED
	case ICMSG_THREAD_MODE_NONE:
		callback_handling(conf, dev_data);
		break;
#endif
#if ICMSG_DEDICATED_THREAD_ENABLED || ICMSG_SHARED_THREAD_ENABLED
	case ICMSG_THREAD_MODE_DEDICATED:
	case ICMSG_THREAD_MODE_SHARED:
		k_work_submit_to_queue(conf->workq, &dev_data->mbox_work);
		break;
#endif
#if ICMSG_SYSTEM_WORK_QUEUE_ENABLED
	case ICMSG_THREAD_MODE_SYSTEM:
		k_work_submit(&dev_data->mbox_work);
		break;
#endif
	default:
		__ASSERT_NO_MSG(false);
		break;
	}
#endif
}
