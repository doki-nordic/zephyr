
#include <unistd.h>

#include <zephyr/cache.h>
#include <zephyr/drivers/mbox.h>

int sys_cache_data_flush_range(void *addr, size_t size)
{
    return 0;
}

int sys_cache_data_invd_range(void *addr, size_t size)
{
    return 0;
}

/*size_t sys_cache_data_line_size_get(void)
{
    return 0;
}*/


__syscall int mbox_send(const struct device *dev, mbox_channel_id_t channel_id,
			const struct mbox_msg *msg)
{
	return -ENOSYS;
}

__syscall int mbox_set_enabled(const struct device *dev,
			       mbox_channel_id_t channel_id, bool enabled)
{
	return -ENOSYS;
}

__syscall int32_t k_sleep(k_timeout_t timeout)
{
	usleep(timeout.ticks * 1000);
}
