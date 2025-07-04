#include "corobus.h"

#include "libcoro.h"
#include "rlist.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

struct data_vector {
	unsigned *data;
	size_t size;
	size_t capacity;
};


/** Append @a count messages in @a data to the end of the vector. */
static void data_vector_append_many(struct data_vector *vector,
                                    const unsigned *data, size_t count) {
  if (vector->size + count > vector->capacity) {
    if (vector->capacity == 0)
      vector->capacity = 4;
    else
      vector->capacity *= 2;
    if (vector->capacity < vector->size + count)
      vector->capacity = vector->size + count;
    vector->data =
        realloc(vector->data, sizeof(vector->data[0]) * vector->capacity);
  }
  memcpy(&vector->data[vector->size], data, sizeof(data[0]) * count);
  vector->size += count;
}

/** Append a single message to the vector. */
static void data_vector_append(struct data_vector *vector, unsigned data)
{
	data_vector_append_many(vector, &data, 1);
}

/** Pop @a count of messages into @a data from the head of the vector. */
static void data_vector_pop_first_many(struct data_vector *vector, unsigned *data, size_t count)
{
	assert(count <= vector->size);
	memcpy(data, vector->data, sizeof(data[0]) * count);
	vector->size -= count;
	memmove(vector->data, &vector->data[count], vector->size * sizeof(vector->data[0]));
}

/** Pop a single message from the head of the vector. */
static unsigned data_vector_pop_first(struct data_vector *vector)
{
	unsigned data = 0;
	data_vector_pop_first_many(vector, &data, 1);
	return data;
}

static void data_vector_free(struct data_vector *vector)
{
 	free(vector->data);
}

/**
 * One coroutine waiting to be woken up in a list of other
 * suspended coros.
 */
struct wakeup_entry {
	struct rlist base;
	struct coro *coro;
};

/** A queue of suspended coros waiting to be woken up. */
struct wakeup_queue {
    struct rlist coros;
};

/** Suspend the current coroutine until it is woken up. */
static void wakeup_queue_suspend(struct wakeup_queue *queue)
{
	struct wakeup_entry entry;
	entry.coro = coro_this();
	rlist_add_tail_entry(&queue->coros, &entry, base);
	coro_suspend();
	rlist_del_entry(&entry, base);
}

static void wakeup_queue_wakeup_first(struct wakeup_queue *queue)
{
	if (rlist_empty(&queue->coros)) return;
	struct wakeup_entry *entry = rlist_first_entry(&queue->coros, struct wakeup_entry, base);
	coro_wakeup(entry->coro);
}


struct coro_bus_channel {
	/** Channel max capacity. */
	size_t size_limit;
	/** Coroutines waiting until the channel is not full. */
	struct wakeup_queue send_queue;
	/** Coroutines waiting until the channel is not empty. */
	struct wakeup_queue recv_queue;
	/** Message queue. */
	struct data_vector data;
	/** Variable indicates if channel was closed or not */
    	bool is_closed;
};

struct coro_bus {
	struct coro_bus_channel **channels;
	int channel_count;
};

static struct coro_bus_channel* get_channel_by_descriptor(struct coro_bus *bus, int descriptor)
{
	struct coro_bus_channel **channels = bus->channels;
	if (bus->channel_count <= descriptor || descriptor < 0 || !channels[descriptor])
		return NULL;
    	return channels[descriptor];
}

static enum coro_bus_error_code global_error = CORO_BUS_ERR_NONE;

enum coro_bus_error_code coro_bus_errno(void)
{
	return global_error;
}

void coro_bus_errno_set(enum coro_bus_error_code err)
{
	global_error = err;
}

/* IMPLEMENTED */
struct coro_bus* coro_bus_new(void)
{
	coro_bus_errno_set(CORO_BUS_ERR_NONE);
	struct coro_bus *new_bus = calloc(1, sizeof(*new_bus));
    return new_bus;
}

/* IMPLEMENTED */
void coro_bus_delete(struct coro_bus *bus)
{
	coro_bus_errno_set(CORO_BUS_ERR_NONE);
	int channel_count = bus->channel_count;
	struct coro_bus_channel **channels = bus->channels;
    for (int i = 0; i < channel_count; i++)
    {
        if (channels[i])
	{
             data_vector_free(&channels[i]->data);
             free(channels[i]);
        }
    }
    free(channels);
    free(bus);
}

/* IMPLEMENTED */
int coro_bus_channel_open(struct coro_bus *bus, size_t size_limit)
{
	coro_bus_errno_set(CORO_BUS_ERR_NONE);
	struct coro_bus_channel **channels = bus->channels;
    	struct coro_bus_channel *new_channel = calloc(1, sizeof(struct coro_bus_channel));
    	new_channel->size_limit = size_limit;
    	new_channel->data.data = calloc(1, sizeof(unsigned));
	new_channel->data.capacity = 0;
	new_channel->data.size = 0;
	new_channel->is_closed = false;
	rlist_create(&new_channel->recv_queue.coros);
	rlist_create(&new_channel->send_queue.coros);

	for (int i = 0; i < bus->channel_count; i++)
	{
		if (!channels[i])
		{
			channels[i] = new_channel;
			return i;
		}
	}

    	channels = realloc(channels, (bus->channel_count + 1)*sizeof(*channels));
	channels[bus->channel_count] = new_channel;
	bus->channels = channels;
	bus->channel_count = bus->channel_count + 1;

    	return bus->channel_count - 1;
}

/* IMPLEMENTED */
void coro_bus_channel_close(struct coro_bus *bus, int channel)
{
    struct coro_bus_channel *cur_channel = get_channel_by_descriptor(bus, channel);
    if (!cur_channel)
    {
        return;
    }
    cur_channel->is_closed = true;

    bus->channels[channel] = NULL;
    coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);

    while (!rlist_empty(&cur_channel->recv_queue.coros))
    {
	wakeup_queue_wakeup_first(&cur_channel->recv_queue);
	coro_yield();
    }

    while (!rlist_empty(&cur_channel->send_queue.coros))
    {
        wakeup_queue_wakeup_first(&cur_channel->send_queue);
        coro_yield();
    }

    data_vector_free(&cur_channel->data);
    free(cur_channel);
}

/* IMPLEMENTED */
int coro_bus_send(struct coro_bus *bus, int channel, unsigned data)
{
	coro_bus_errno_set(CORO_BUS_ERR_NONE);
	while (true)
	{
		int res = coro_bus_try_send(bus, channel, data);
		if (!res) return 0;

		enum coro_bus_error_code err = coro_bus_errno();
 		if (err == CORO_BUS_ERR_NO_CHANNEL)
		{
      			coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
      			return -1;
    		}
		else if (err == CORO_BUS_ERR_WOULD_BLOCK)
		{
			struct coro_bus_channel *send_channel = get_channel_by_descriptor(bus, channel);
			if (!send_channel)
			{
                        	coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
                        	return -1;
			}
			wakeup_queue_suspend(&send_channel->send_queue);
		}
	}
}

/* IMPLEMENTED */
int coro_bus_try_send(struct coro_bus *bus, int channel, unsigned data)
{
	coro_bus_errno_set(CORO_BUS_ERR_NONE);
	//printf("channel descriptor: %d\n", channel);
	struct coro_bus_channel *send_channel = get_channel_by_descriptor(bus, channel);
	if (!send_channel)
	{
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}
    if (send_channel->data.size == send_channel->size_limit)
    {
    	coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
        return -1;
    }
    data_vector_append(&send_channel->data, data);
    wakeup_queue_wakeup_first(&send_channel->recv_queue);
    return 0;
}

/* IMPLEMENTED */
int coro_bus_recv(struct coro_bus *bus, int channel, unsigned *data)
{
	coro_bus_errno_set(CORO_BUS_ERR_NONE);
	while (true)
	{
		if(!coro_bus_try_recv(bus, channel, data)) return 0;
		if (coro_bus_errno() != CORO_BUS_ERR_WOULD_BLOCK) return -1;
		struct coro_bus_channel *recv_channel = get_channel_by_descriptor(bus, channel);
		if (!recv_channel)
		{
			coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
			return -1;
		}
		wakeup_queue_suspend(&recv_channel->recv_queue);
	}
}

/* IMPLEMENTED */
int coro_bus_try_recv(struct coro_bus *bus, int channel, unsigned *data)
{
	coro_bus_errno_set(CORO_BUS_ERR_NONE);
	struct coro_bus_channel *recv_channel = get_channel_by_descriptor(bus, channel);
	if (!recv_channel)
	{
                 coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
                 return -1;
	}
	if (recv_channel->data.size == 0)
	{
		coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
		return -1;
	}
	*data = data_vector_pop_first(&recv_channel->data);
	wakeup_queue_wakeup_first(&recv_channel->send_queue);
	return 0;
}


#if NEED_BROADCAST

int
coro_bus_broadcast(struct coro_bus *bus, unsigned data)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)data;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_try_broadcast(struct coro_bus *bus, unsigned data)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)data;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

#endif

#if NEED_BATCH

int
coro_bus_send_v(struct coro_bus *bus, int channel, const unsigned *data, unsigned count)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)count;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_try_send_v(struct coro_bus *bus, int channel, const unsigned *data, unsigned count)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)count;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_recv_v(struct coro_bus *bus, int channel, unsigned *data, unsigned capacity)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)capacity;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_try_recv_v(struct coro_bus *bus, int channel, unsigned *data, unsigned capacity)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)capacity;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

#endif
