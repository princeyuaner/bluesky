#include <bluesky_timer.h>
#include <jemalloc.h>
#include <message.h>
#include <server.h>
#include <skynet_mq.h>

#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static struct timer *TIMER = NULL;
static void move_list(struct timer* T, int level, int idx);
static inline void dispatch_list(struct timer_node* current);

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

static inline struct timer_node* clear_list(struct timer_list* list)
{
    struct timer_node* ret = list->head;
    list->head = NULL;
    list->tail = NULL;
    return ret;
}

static inline void execute_timer(struct timer* T)
{
    uint32_t idx = T->time & TIMER_SLOT_MASK;
    if (T->timer[0][idx].head)
    {
        struct timer_node* current = clear_list(&T->timer[0][idx]);
        SPIN_UNLOCK(T);
        dispatch_list(current);
        SPIN_LOCK(T);
    }
}

static void shift_timer(struct timer* T)
{
    uint32_t ct = ++T->time;
    if (ct == 0)
    {
        move_list(T, TIMER_TOTAL_LEVEL, 0);
    }
    else
    {
        int mask = TIMER_SLOT;
        uint32_t time = ct >> TIMER_SLOT_SHIFT;
        int i = 1;
        while ((ct & (mask - 1)) == 0)
        {
            int idx = time & TIMER_SLOT_MASK;
            if (idx != 0) {
                move_list(T, i, idx);
                break;
            }
            mask <<= TIMER_SLOT_SHIFT;
            time >>= TIMER_SLOT_SHIFT;
            ++i;
        }
    }
}


static void update_timer(struct timer* T) {
    SPIN_LOCK(T);
    execute_timer(TIMER);
    shift_timer(TIMER);
    execute_timer(TIMER);
    SPIN_UNLOCK(T);
}

void update_time()
{
    uint64_t cp = gettime();
    if (cp < TIMER->current_point)
    {
        TIMER->current_point = cp;
    }
    else if (cp > TIMER->current_point)
    {
        uint32_t diff = cp - TIMER->current_point;
        TIMER->current_point = cp;
        TIMER->current += diff;
        for (uint32_t i = 0; i < diff; i++)
        {
            update_timer(TIMER);
        }
    }
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
    if (expire_time - current_time <= TIMER_SLOT)
    {   
        //time为0时情况特殊，单独拿出来判断
        add_to_list(&T->timer[0][expire_time & (mask - 1)], node);
    }
    else
    {
        for (int i = 1; i < 4; i++)
        {
            if ((expire_time | (mask - 1)) == (current_time | (mask - 1)))
            {
                add_to_list(&T->timer[i][expire_time & (mask - 1)], node);
                break;
            }
            mask <<= TIMER_SLOT_SHIFT;
        }
    }
}

static void timer_add(struct timer* T, int interval,uint32_t timer_id)
{
    struct timer_node* node = (struct timer_node*)je_malloc(sizeof(*node));

    SPIN_LOCK(T);

    node->expire_time = interval + T->time;
    node->timer_id = timer_id;
    add_node(T, node);

    SPIN_UNLOCK(T);
}


static void move_list(struct timer* T, int level, int idx) {
    struct timer_node* current = clear_list(&T->timer[level][idx]);
    while (current) {
        struct timer_node* temp = current->next;
        add_node(T, current);
        current = temp;
    }
}

static uint32_t timer_cb_add(struct timer* T, uint32_t start, uint32_t interval, bool cycle, PyObject* cb)
{
    struct timer_cb_node* node = (struct timer_cb_node*)je_malloc(sizeof(*node));
    memset(node, 0, sizeof(*node));
    node->id = make_timer_id(T);
    node->interval = interval;
    node->start = start;
    node->cycle = cycle;
    node->cb = cb;
    Py_XINCREF(cb);
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
    return node->id;
}



static inline void dispatch_list(struct timer_node* current) 
{
    while (current)
    {
        struct bluesky_message smsg;
        smsg.type = ACCEPTED;
        struct timer_message* timer_msg = je_malloc(sizeof(*timer_msg));
        timer_msg->timer_id = current->timer_id;
        smsg.data = timer_msg;
        skynet_mq_push(BLUE_SKYSERVER->queue, &smsg);
        current = current->next;
        
    }
    pthread_cond_signal(&BLUE_SKYSERVER->cond);
}

static PyObject* timer_once(PyObject* self, PyObject* args)
{
    PyObject* timer_cb;
    uint32_t interval;

    if (PyArg_ParseTuple(args, "iO", &interval, &timer_cb))
    {
        uint32_t timer_id =  timer_cb_add(TIMER, 0, interval, false,timer_cb);
        timer_add(TIMER, interval,timer_id);
        PyObject* arglist = Py_BuildValue("(i)", timer_id);
        return arglist;
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
        uint32_t timer_id = timer_cb_add(TIMER, start, interval, true, timer_cb);
        timer_add(TIMER, interval, timer_id);
        PyObject* arglist = Py_BuildValue("(i)", timer_id);
        return arglist;
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
                Py_XDECREF(node->cb);
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