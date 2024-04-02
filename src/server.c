#include <server.h>
#include <Python.h>
#include <skynet_mq.h>
#include <jemalloc.h>
#include <pthread.h>
#include <network.h>
#include <bluesky_timer.h>

void create_bluesky_server()
{
    BLUE_SKYSERVER = je_malloc(sizeof(*BLUE_SKYSERVER));
    BLUE_SKYSERVER->queue = skynet_mq_create();
    if (pthread_mutex_init(&BLUE_SKYSERVER->mutex, NULL))
    {
        exit(1);
    }
    if (pthread_cond_init(&BLUE_SKYSERVER->cond, NULL))
    {
        exit(1);
    }
}

static PyObject *py_start(PyObject *self, PyObject *args)
{
    create_bluesky_server();
    while (true)
    {
        struct bluesky_message msg;
        int ret = skynet_mq_pop(BLUE_SKYSERVER->queue, &msg);
        if (ret == 0)
        {
            if (msg.type == RECV_DATA)
            {
                struct recv_data_message *recv_data_msg = (struct recv_data_message *)msg.data;
                PyObject *arglist = Py_BuildValue("(is)", recv_data_msg->id, recv_data_msg->data);
                PyObject_CallObject(get_socket_server()->data_recv_cb, arglist);
                je_free(recv_data_msg->data);
                Py_DECREF(arglist);
            }
            if (msg.type == ACCEPTED)
            {
                struct accept_message *ac_msg = (struct accept_message *)msg.data;
                printf("accept1 id:%d\n", ac_msg->id);
                PyObject *arglist = Py_BuildValue("(i)", ac_msg->id);
                PyObject_CallObject(get_socket_server()->accept_cb, arglist);
                Py_DECREF(arglist);
                je_free(ac_msg);
            }
            if (msg.type == TIME_OUT)
            {
                struct timer_message *timer_msg = (struct timer_message *)msg.data;
                printf("timeout id:%d\n", timer_msg->timer_id);
                struct timer_cb_node *cb_node = get_timer_cb_node(timer_msg->timer_id);
                if (cb_node != NULL)
                {
                    printf("timeout1 id:%d\n", timer_msg->timer_id);
                    PyObject_CallObject(cb_node->cb, NULL);
                    if (cb_node->cycle)
                    {
                    }
                }
                je_free(timer_msg);
            }
        }
        else
        {
            if (pthread_mutex_lock(&BLUE_SKYSERVER->mutex) == 0)
            {
                pthread_cond_wait(&BLUE_SKYSERVER->cond, &BLUE_SKYSERVER->mutex);
                if (pthread_mutex_unlock(&BLUE_SKYSERVER->mutex))
                {
                    exit(1);
                }
            }
        }
    }

    return Py_None;
}

static PyMethodDef server_methods[] = {
    {"start", (PyCFunction)py_start, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef server_module =
    {
        PyModuleDef_HEAD_INIT,
        "server",                                                                /* name of module */
        "usage: Combinations.uniqueCombinations(lstSortableItems, comboSize)\n", /* module documentation, may be NULL */
        -1,                                                                      /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
        server_methods};

PyObject *PyInit_server()
{
    return PyModule_Create(&server_module);
};