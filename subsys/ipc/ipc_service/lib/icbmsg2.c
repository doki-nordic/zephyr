


static void mbox_callback(const struct device *instance, uint32_t channel,
			  void *user_data, struct mbox_msg *msg_data)
{
	if (dev_data->waiting_allocations > 0) {
		// Retry allocation on waiting allocators.
		sem_give(&dev_data->alloc_sem);
		// It might be release-only notification, so process the
		// incoming message only if there is any.
		if (icmsg_handler_needed()) {
			k_work_submit_to_queue();
		}
	} else {
		k_work_submit_to_queue();
	}
}

static int read_release_requests_and_allocate_available_blocks(int num_blocks)
{
	if (read_release_requests()) {
		return allocate_available_blocks(blocks);
	} else {
		return -1;
	}
}

static int allocate_buffers()
{
	uint32_t blocks;
	// Allocate buffers immediately.
	int result = allocate_available_blocks(blocks);
	if (result >= 0) {
		return result;
	}
	// We don't have space available now, so read incoming block release requests and retry.
	result = read_release_requests_and_allocate_available_blocks(blocks);
	if (result >= 0) {
		return result;
	}
	// Still no space, we need to wait (or return if waiting is not allowed).
	if (timeout == K_NO_WAIT) {
		return -ENOMEM;
	}
#ifdef CONFIG_MULTITHREADING
	// Tell remote that we are waiting, so it will send mbox notification after each release.
	spin_lock();
	dev_data->waiting_allocators++;
	dev_data->rw_ctrl->waiting_allocators = dev_data->waiting_allocators;
	__sync_synchronize(); // is it needed before spin_unlock?
	spin_unlock();
	cache_flush();
	// The waiting loop
	// Start with checking again the incoming release requests since remote may release something in the meantime.
	// The remote first releases the blocks and after that checks waiting_allocators, so
	// other race conditions will no happen here.
	while (true) {
		// Check if we have enough space now
		result = read_release_requests_and_allocate_available_blocks(blocks);
		if (result >= 0) {
			// Give semaphore back, since there may be more allocators waiting.
			sem_give(&dev_data->alloc_sem);
			break;
		}
		// There is no space for sure, so going to sleep
		timeout = calc_timeout(time_point);
		result = sem_wait(&dev_data->alloc_sem, timeout);
		if (result < 0) {
			break;
		}
	}
	spin_lock();
	dev_data->waiting_allocators--;
	dev_data->rw_ctrl->waiting_allocators = dev_data->waiting_allocators;
	__sync_synchronize(); // is it needed before spin_unlock?
	spin_unlock();
	cache_flush();
#else
	// Timeouts are not allowed in no-multithreading mode
	result = -EIO; // Some other error code
#endif /* CONFIG_MULTITHREADING */
	return result;
}
