
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

	if (dev_data->state == ICMSG_STATE_UNINITIALIZED) {
		return -ENRDY;
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
