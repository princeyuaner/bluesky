#include "skynet_mq.h"
#include "spinlock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <jemalloc.h>

#define DEFAULT_QUEUE_SIZE 64
#define MAX_GLOBAL_MQ 0x10000

// 0 means mq is not in global mq.
// 1 means mq is in global mq , or the message is dispatching.

#define MQ_IN_GLOBAL 1
#define MQ_OVERLOAD 1024

struct message_queue *
skynet_mq_create()
{
	struct message_queue *q = je_malloc(sizeof(*q));
	q->cap = DEFAULT_QUEUE_SIZE;
	q->head = 0;
	q->tail = 0;
	SPIN_INIT(q)
	// When the queue is create (always between service create and service init) ,
	// set in_global flag to avoid push it to global queue .
	// If the service init success, skynet_context_new will call skynet_mq_push to push it to global queue.
	q->in_global = MQ_IN_GLOBAL;
	q->release = 0;
	q->overload = 0;
	q->overload_threshold = MQ_OVERLOAD;
	q->queue = je_malloc(sizeof(struct bluesky_message) * q->cap);

	return q;
}

static void
_release(struct message_queue *q)
{
	SPIN_DESTROY(q)
	je_free(q->queue);
	je_free(q);
}

int skynet_mq_length(struct message_queue *q)
{
	int head, tail, cap;

	SPIN_LOCK(q)
	head = q->head;
	tail = q->tail;
	cap = q->cap;
	SPIN_UNLOCK(q)

	if (head <= tail)
	{
		return tail - head;
	}
	return tail + cap - head;
}

int skynet_mq_overload(struct message_queue *q)
{
	if (q->overload)
	{
		int overload = q->overload;
		q->overload = 0;
		return overload;
	}
	return 0;
}

int skynet_mq_pop(struct message_queue *q, struct bluesky_message *message)
{
	int ret = 1;
	SPIN_LOCK(q)

	if (q->head != q->tail)
	{
		*message = q->queue[q->head++];
		ret = 0;
		int head = q->head;
		int tail = q->tail;
		int cap = q->cap;

		if (head >= cap)
		{
			q->head = head = 0;
		}
		int length = tail - head;
		if (length < 0)
		{
			length += cap;
		}
		while (length > q->overload_threshold)
		{
			q->overload = length;
			q->overload_threshold *= 2;
		}
	}
	else
	{
		// reset overload_threshold when queue is empty
		q->overload_threshold = MQ_OVERLOAD;
	}

	if (ret)
	{
		q->in_global = 0;
	}

	SPIN_UNLOCK(q)

	return ret;
}

int skynet_mq_top(struct message_queue *q, struct bluesky_message *message)
{
	int ret = 1;
	SPIN_LOCK(q)

	if (q->head != q->tail)
	{
		*message = q->queue[q->head++];
		ret = 0;
	}

	SPIN_UNLOCK(q)

	return ret;
}

static void
expand_queue(struct message_queue *q)
{
	struct bluesky_message *new_queue = je_malloc(sizeof(struct bluesky_message) * q->cap * 2);
	int i;
	for (i = 0; i < q->cap; i++)
	{
		new_queue[i] = q->queue[(q->head + i) % q->cap];
	}
	q->head = 0;
	q->tail = q->cap;
	q->cap *= 2;

	je_free(q->queue);
	q->queue = new_queue;
}

void skynet_mq_push(struct message_queue *q, struct bluesky_message *message)
{
	assert(message);
	SPIN_LOCK(q)

	q->queue[q->tail] = *message;
	if (++q->tail >= q->cap)
	{
		q->tail = 0;
	}

	if (q->head == q->tail)
	{
		expand_queue(q);
	}

	SPIN_UNLOCK(q)
}

void skynet_mq_mark_release(struct message_queue *q)
{
	SPIN_LOCK(q)
	assert(q->release == 0);
	q->release = 1;
	SPIN_UNLOCK(q)
}

static void
_drop_queue(struct message_queue *q, message_drop drop_func, void *ud)
{
	struct bluesky_message msg;
	while (!skynet_mq_pop(q, &msg))
	{
		drop_func(&msg, ud);
	}
	_release(q);
}

void skynet_mq_release(struct message_queue *q, message_drop drop_func, void *ud)
{
	SPIN_LOCK(q)

	if (q->release)
	{
		SPIN_UNLOCK(q)
		_drop_queue(q, drop_func, ud);
	}
}
