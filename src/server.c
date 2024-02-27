#include <server.h>
#include <Python.h>
#include <skynet_mq.h>
#include <jemalloc.h>
#include <pthread.h>
#include <network.h>

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
                PyObject *arglist = Py_BuildValue("(s)", msg.data);
                PyObject_CallObject(get_socket_server()->data_recv_cb, arglist);
                Py_DECREF(arglist);
            }
            if (msg.type == ACCEPTED)
            {
                struct accept_message *ac_msg = (struct accept_message *)msg.data;
                PyObject *arglist = Py_BuildValue("(is)", ac_msg->fd, &ac_msg->addr);
                PyObject_CallObject(get_socket_server()->accept_cb, arglist);
                Py_DECREF(arglist);
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