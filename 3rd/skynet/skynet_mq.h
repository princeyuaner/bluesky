#ifndef SKYNET_MESSAGE_QUEUE_H
#define SKYNET_MESSAGE_QUEUE_H

#include <stdlib.h>
#include <stdint.h>
#include <bluesky_message.h>
#include <spinlock.h>

// type is encoding in skynet_message.sz high 8bit
#define MESSAGE_TYPE_MASK (SIZE_MAX >> 8)
#define MESSAGE_TYPE_SHIFT ((sizeof(size_t) - 1) * 8)

struct message_queue
{
    struct spinlock lock;
    int cap;
    int head;
    int tail;
    int release;
    int in_global;
    int overload;
    int overload_threshold;
    struct bluesky_message *queue;
};

struct message_queue *skynet_mq_create();
void skynet_mq_mark_release(struct message_queue *q);

typedef void (*message_drop)(struct bluesky_message *, void *);

void skynet_mq_release(struct message_queue *q, message_drop drop_func, void *ud);

// 0 for success
int skynet_mq_pop(struct message_queue *q, struct bluesky_message *message);
int skynet_mq_top(struct message_queue *q, struct bluesky_message *message);
void skynet_mq_push(struct message_queue *q, struct bluesky_message *message);

// return the length of message queue, for debug
int skynet_mq_length(struct message_queue *q);
int skynet_mq_overload(struct message_queue *q);

#endif
