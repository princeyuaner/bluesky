#ifndef BLUESKY_TIMER_H
#define BLUESKY_TIMER_H

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <Python.h>
#include <spinlock.h>

#define TIMER_SLOT_SHIFT  8
#define TIMER_SLOT (1 << TIMER_SLOT_SHIFT)

struct timer_node
{
    struct timer_node *next;
    uint32_t expire_time;
    uint32_t id;
};

struct timer_list
{
    struct timer_node *head;
    struct timer_node *tail;
};

struct timer
{
    struct timer_list timer[4][TIMER_SLOT];
    struct spinlock lock;
    uint64_t id;
    uint32_t time;
    uint32_t starttime;
    uint64_t current;
    uint64_t current_point;
};

void init_timer(void);

PyObject* PyInit_timer();

#endif