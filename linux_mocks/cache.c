
#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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



void *work_queue_run(void *ptr)
{
	struct k_work_q *queue = ptr;
	pthread_mutex_lock(queue->mutex);
	while (true) {
		while (sys_slist_peek_head(&queue->pending)) {
			struct k_work* work = (void*)sys_slist_get(&queue->pending);
			work->queue = NULL;
			pthread_mutex_unlock(queue->mutex);
			work->handler(work);
			pthread_mutex_lock(queue->mutex);
		}
		pthread_cond_wait(queue->cv, queue->mutex);
	}
}


void k_work_queue_start(struct k_work_q *queue,
			k_thread_stack_t *stack, size_t stack_size,
			int prio, const struct k_work_queue_config *cfg)
{
	static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;
	static pthread_cond_t cv_initializer = PTHREAD_COND_INITIALIZER;
	pthread_t* thread = (pthread_t*)malloc(sizeof(pthread_t));
	queue->mutex = malloc(sizeof(pthread_mutex_t));
	memcpy(queue->mutex, &mutex_initializer, sizeof(pthread_mutex_t));
	queue->cv = malloc(sizeof(pthread_cond_t));
	memcpy(queue->cv, &cv_initializer, sizeof(pthread_cond_t));
	pthread_create(thread, NULL, work_queue_run, (void*)queue);
	if (cfg && cfg->name) {
		pthread_setname_np(*thread, cfg->name);
		// int pthread_getname_np(pthread_self(), char name[.size], size_t size);
	}
}
