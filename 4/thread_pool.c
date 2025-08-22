#include "thread_pool.h"
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

enum thread_task_state {
	TASK_STATE_NEW, // задача создана, но ещё не добавлена в пул
	TASK_STATE_IN_POOL, // задача добавлена в пул и находится в очереди
	TASK_STATE_RUNNING, // задача выполняется рабочим потоком
	TASK_STATE_FINISHED // задача выполнена, результат выполнения доступен
};

struct thread_task {
	thread_task_f function;
	void *arg;
	void *result;

	pthread_mutex_t mutex;
	pthread_cond_t cond;

	enum thread_task_state state;

	bool detached;

	struct thread_task *next;
};

struct thread_pool {
	pthread_t *threads;

	int thread_count;
	int idle_thread_count;
	int max_thread_count;

	struct thread_task *task_queue_head;
	struct thread_task *task_queue_tail;
	int queued_task_count;

	pthread_mutex_t queue_mutex;
	pthread_cond_t queue_cond;

	bool is_shutting_down;
};

/* IMPLEMENTED */
int thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
	if (max_thread_count < 1 || max_thread_count > TPOOL_MAX_THREADS)
		return TPOOL_ERR_INVALID_ARGUMENT;

	struct thread_pool *new_pool = calloc(1, sizeof(struct thread_pool));
	new_pool->threads = calloc(max_thread_count, sizeof(struct thread_pool));

	new_pool->max_thread_count = max_thread_count;

	pthread_mutex_init(&new_pool->queue_mutex, NULL);
	pthread_cond_init(&new_pool->queue_cond, NULL);

	*pool = new_pool;

	return 0;
}

/* IMPLEMENTED */
int thread_pool_thread_count(const struct thread_pool *pool)
{
	return pool->thread_count;
}

/* IMPLEMENTED */
int thread_pool_delete(struct thread_pool *pool)
{
	pthread_mutex_lock(&pool->queue_mutex);

	if (pool->task_queue_head || pool->queued_task_count > 0 || pool->idle_thread_count != pool->thread_count)
	{
		pthread_mutex_unlock(&pool->queue_mutex);
		return TPOOL_ERR_HAS_TASKS;
	}

	pool->is_shutting_down = true;

	/* Пробуждение всех ожидающих потоков */
	pthread_cond_broadcast(&pool->queue_cond);
	pthread_mutex_unlock(&pool->queue_mutex);

	for (int i = 0; i < pool->thread_count; i++)
		pthread_join(pool->threads[i], NULL);

	pthread_mutex_destroy(&pool->queue_mutex);
	pthread_cond_destroy(&pool->queue_cond);

	free(pool->threads);
	free(pool);

	return 0;
}

static void thread_task_destroy(struct thread_task *task)
{
	pthread_mutex_destroy(&task->mutex);
	pthread_cond_destroy(&task->cond);
	free(task);
}

/*
*  Ф-ция потока, обрабатывающая задачи из очереди пула.
*  Выполняет задачи из очереди, пока пуо не будет удален.
*/
static void* worker_thread_function(void *thread_pool)
{
	struct thread_pool *pool = thread_pool;

	for (;;) {
		pthread_mutex_lock(&pool->queue_mutex);

		while (!pool->task_queue_head && !pool->is_shutting_down)
			pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);

		if (pool->is_shutting_down) {
			pthread_mutex_unlock(&pool->queue_mutex);
			break;
		}

		struct thread_task *task = pool->task_queue_head;
		pool->task_queue_head = task->next;
		if (!pool->task_queue_head)
			pool->task_queue_tail = NULL;

		task->next = NULL;
		pool->queued_task_count--;
		pool->idle_thread_count--;

		pthread_mutex_unlock(&pool->queue_mutex);

		pthread_mutex_lock(&task->mutex);
		task->state = TASK_STATE_RUNNING;
		pthread_mutex_unlock(&task->mutex);

		void *result = task->function(task->arg);

		bool destroy_task = false;

		pthread_mutex_lock(&task->mutex);
		destroy_task = task->detached;
		task->result = result;
		task->state = TASK_STATE_FINISHED;
		pthread_cond_broadcast(&task->cond);
		pthread_mutex_unlock(&task->mutex);

		pthread_mutex_lock(&pool->queue_mutex);
		pool->idle_thread_count++;
		pthread_mutex_unlock(&pool->queue_mutex);

		if (destroy_task)
			thread_task_destroy(task);
	}

	return NULL;
}


/* IMPLEMENTED */
int thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	pthread_mutex_lock(&pool->queue_mutex);

	if (pool->queued_task_count > TPOOL_MAX_TASKS)
	{
		pthread_mutex_unlock(&pool->queue_mutex);
		return TPOOL_ERR_TOO_MANY_TASKS;
	}

	task->next = NULL;
	task->state = TASK_STATE_IN_POOL;
	pool->queued_task_count++;

	if (!pool->task_queue_tail) {
		pool->task_queue_head = task;
		pool->task_queue_tail = task;
	}
	else {
		pool->task_queue_tail->next = task;
		pool->task_queue_tail = task;
	}

	if (pool->thread_count == 0)
	{
		pthread_t new_thread;
		if (pthread_create(&new_thread, NULL, worker_thread_function, pool) == 0)
			pool->threads[pool->thread_count++] = new_thread;

		pool->idle_thread_count++;
	}

	pthread_cond_signal(&pool->queue_cond);
	pthread_mutex_unlock(&pool->queue_mutex);

	return 0;
}

/* IMPLEMENTED */
int thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	struct thread_task *new_task = calloc(1, sizeof(struct thread_task));

	new_task->function = function;
	new_task->arg = arg;
	new_task->state = TASK_STATE_NEW;

	pthread_mutex_init(&new_task->mutex, NULL);
	pthread_cond_init(&new_task->cond, NULL);

	*task = new_task;

	return 0;
}

/* IMPLEMENTED */
bool thread_task_is_finished(const struct thread_task *task)
{
	return task->state == TASK_STATE_FINISHED;
}

/* IMPLEMENTED */
bool thread_task_is_running(const struct thread_task *task)
{
	return task->state == TASK_STATE_RUNNING;
}

/* IMPLEMENTED */
int thread_task_join(struct thread_task *task, void **result)
{
	pthread_mutex_lock(&task->mutex);

	if (task->state == TASK_STATE_NEW)
	{
		pthread_mutex_unlock(&task->mutex);
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}

	while (task->state != TASK_STATE_FINISHED)
		pthread_cond_wait(&task->cond, &task->mutex);

	*result = task->result;

	pthread_mutex_unlock(&task->mutex);

	return 0;
}

#if NEED_TIMED_JOIN

int thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	if (task->state == TASK_STATE_NEW)
		return TPOOL_ERR_TASK_NOT_PUSHED;

	struct timespec timeout_ts;
	clock_gettime(CLOCK_REALTIME, &timeout_ts);

	time_t seconds = (time_t)timeout;
	long nanoseconds = (long) ((timeout - seconds)*1e9);

	timeout_ts.tv_sec += seconds;
	timeout_ts.tv_nsec += nanoseconds;

	pthread_mutex_lock(&task->mutex);

	while (task->state != TASK_STATE_FINISHED)
	{
		int wait_result = pthread_cond_timedwait(&task->cond, &task->mutex, &timeout_ts);
		if (wait_result == ETIMEDOUT) {
			pthread_mutex_unlock(&task->mutex);
			return TPOOL_ERR_TIMEOUT;
		}
	}

	*result = task->result;

	pthread_mutex_unlock(&task->mutex);

	return 0;
}

#endif

/* IMPLEMENTED */
int thread_task_delete(struct thread_task *task)
{
	pthread_mutex_lock(&task->mutex);

	if(task->state == TASK_STATE_IN_POOL || task->state == TASK_STATE_RUNNING) {
		pthread_mutex_unlock(&task->mutex);
		return TPOOL_ERR_TASK_IN_POOL;
	}

	pthread_mutex_unlock(&task->mutex);
	thread_task_destroy(task);

	return 0;
}

#if NEED_DETACH

/* IMPLEMENTED */
int thread_task_detach(struct thread_task *task)
{
	pthread_mutex_lock(&task->mutex);

	if (task->state == TASK_STATE_NEW)
	{
		pthread_mutex_unlock(&task->mutex);
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}

	if (task->state == TASK_STATE_FINISHED)
	{
		thread_task_destroy(task);
	}

	task->detached = true;

	pthread_mutex_unlock(&task->mutex);

	return 0;
}

#endif
