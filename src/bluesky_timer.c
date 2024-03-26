#include <bluesky_timer.h>
#include <jemalloc.h>

#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static struct timer *TIMER = NULL;

static void systime(uint32_t *sec, uint32_t *cs)
{
    struct timespec ti;
    clock_gettime(CLOCK_REALTIME, &ti);
    *sec = (uint32_t)ti.tv_sec;
    *cs = (uint32_t)(ti.tv_nsec / 10000000);
}

static uint64_t gettime()
{
    uint64_t t;
    struct timespec ti;
    clock_gettime(CLOCK_MONOTONIC, &ti);
    t = (uint64_t)ti.tv_sec * 100;
    t += ti.tv_nsec / 10000000;
    return t;
}

static struct timer *create_timer()
{
    struct timer *t = je_malloc(sizeof(*t));
    memset(t, 0, sizeof(*t));
    return t;
}

void init_timer()
{
    TIMER = create_timer();
    uint32_t current = 0;
    systime(&TIMER->starttime, &current);
    TIMER->current = current;
    TIMER->current_point = gettime();
}

uint32_t make_timer_id(struct timer* T)
{
    return (T->id)++;
}


static inline void add_to_list(struct timer_list* list, struct timer_node* node) {
    list->tail->next = node;
    list->tail = node;
    node->next = 0;
}

static void add_node(struct timer* T, struct timer_node* node)
{
    uint32_t expire_time = node->expire_time;
    uint32_t current_time = T->time;
    uint32_t mask = TIMER_SLOT;
    for (int i = 0; i < 4; i++)
    {
        if ((expire_time | (mask - 1)) == (current_time | (mask - 1)))
        {
            add_to_list(&T->timer[i][expire_time & (mask - 1)], node);
            break;
        }
        mask <<= TIMER_SLOT_SHIFT;
    }
}

static void timer_add(struct timer* T, int interval)
{
    struct timer_node* node = (struct timer_node*)je_malloc(sizeof(*node));

    SPIN_LOCK(T);

    node->expire_time = interval + T->time;
    add_node(T, node);

    SPIN_UNLOCK(T);
}

static void timer_cb_add(struct timer* T, uint32_t start, uint32_t interval, bool cycle)
{
    struct timer_cb_node* node = (struct timer_cb_node*)je_malloc(sizeof(*node));
    memset(node, 0, sizeof(*node));
    node->id = make_timer_id(T);
    node->interval = interval;
    node->start = start;
    node->cycle = cycle;
    uint32_t slot = node->id & (TIMER_CB_SLOT - 1);
    struct timer_cb_list slotList = T->timer_cb[slot];
    if (slotList.head == NULL)
    {
        slotList.head = node;
        slotList.tail = node;
    }
    else
    {
        node->last = slotList.tail;
        slotList.tail->next = node;
        slotList.tail = node;
    }
}

static PyObject* timer_once(PyObject* self, PyObject* args)
{
    PyObject* timer_cb;
    uint32_t interval;

    if (PyArg_ParseTuple(args, "iO", &interval, &timer_cb))
    {
        timer_cb_add(TIMER, 0, interval, false);
        timer_add(TIMER, interval);
    }
    Py_RETURN_NONE;
}

static PyObject* timer_cycle(PyObject* self, PyObject* args)
{
    PyObject* timer_cb;
    uint32_t start;
    uint32_t interval;

    if (PyArg_ParseTuple(args, "iiO",&start, &interval, &timer_cb))
    {
        timer_cb_add(TIMER, start, interval, true);
        timer_add(TIMER, interval);
    }
    Py_RETURN_NONE;
}

static PyObject* timer_cancel(PyObject* self, PyObject* args)
{
    uint32_t id;
    if (PyArg_ParseTuple(args, "i", &id))
    {
        uint32_t slot = id & (TIMER_CB_SLOT - 1);
        struct timer_cb_list slotList = TIMER->timer_cb[slot];
        struct timer_cb_node* node = slotList.head;
        while (node != NULL)
        {
            if (node->id == id)
            {
                if (node->last != NULL)
                {
                    node->last->next = node->next;
                }
                else
                {
                    slotList.head = node->next;
                }
                if (node->next != NULL)
                {
                    node->next->last = node->last;
                }
                else
                {
                    slotList.tail = node->last;
                }
                je_free(node);
                break;
            }
            node = node->next;
        }
    }
    Py_RETURN_NONE;
}

static PyMethodDef timer_methods[] = {
    {"timerOnce", (PyCFunction)timer_once, METH_VARARGS, NULL},
    {"timerCycle", (PyCFunction)timer_cycle, METH_VARARGS, NULL},
    {"cancel", (PyCFunction)timer_cancel, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL} };

static struct PyModuleDef timer_module =
{
    PyModuleDef_HEAD_INIT,
    "timer",                                                               /* name of module */
    "usage: Combinations.uniqueCombinations(lstSortableItems, comboSize)\n", /* module documentation, may be NULL */
    -1,                                                                      /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
    timer_methods };

PyObject* PyInit_timer()
{
    return PyModule_Create(&timer_module);
};