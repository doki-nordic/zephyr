
#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>

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

pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;

void lock_global() {
	pthread_mutex_lock(&global_mutex);
}

void unlock_global() {
	pthread_mutex_unlock(&global_mutex);
}

void *work_queue_run(void *ptr)
{
	struct k_work_q *queue = ptr;
	lock_global();
	while (true) {
		while (sys_slist_peek_head(&queue->pending)) {
			struct k_work* work = (void*)sys_slist_get(&queue->pending);
			work->queue = NULL;
			unlock_global();
			work->handler(work);
			lock_global();
		}
		pthread_cond_wait(queue->cv, &global_mutex);
	}
}


void k_work_queue_start(struct k_work_q *queue,
			k_thread_stack_t *stack, size_t stack_size,
			int prio, const struct k_work_queue_config *cfg)
{
	static pthread_cond_t cv_initializer = PTHREAD_COND_INITIALIZER;
	pthread_t* thread = (pthread_t*)malloc(sizeof(pthread_t));
	queue->cv = malloc(sizeof(pthread_cond_t));
	memcpy(queue->cv, &cv_initializer, sizeof(pthread_cond_t));
	pthread_create(thread, NULL, work_queue_run, (void*)queue);
	if (cfg && cfg->name) {
		pthread_setname_np(*thread, cfg->name);
		// int pthread_getname_np(pthread_self(), char name[.size], size_t size);
	}
}

void k_work_init(struct k_work *work, k_work_handler_t handler)
{
	memset(work, 0, sizeof(*work));
	work->handler = handler;
}

int k_work_cancel(struct k_work *work)
{
	lock_global();
	if (work->queue) {
		(void)sys_slist_find_and_remove(&work->queue->pending, &work->node);
		work->queue = NULL;
	}
	unlock_global();
	return 0;
}

int k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work)
{
	lock_global();
	if (work->queue != NULL) {
		return work->queue != queue ? -EINVAL : 0;
	}
	work->queue = queue;
	sys_slist_append(&queue->pending, &work->node);
	unlock_global();
	pthread_cond_signal(queue->cv);
	return 0;
}

void k_yield(void)
{
	sched_yield();
}
