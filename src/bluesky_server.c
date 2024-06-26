#include <bluesky_server.h>
#include <Python.h>
#include <skynet_mq.h>
#include <jemalloc.h>
#include <pthread.h>
#include <bluesky_network.h>
#include <bluesky_timer.h>
#include <signal.h>
#include <stdio.h>

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

void signal_handler(int s)
{
    exit(1);
}

void reg_signal()
{
    signal(SIGINT, signal_handler);
    signal(SIGSTOP, signal_handler);
    signal(SIGTSTP, signal_handler);
}

static PyObject *py_start(PyObject *self, PyObject *args)
{
    reg_signal();
    create_bluesky_server();
    while (true)
    {
        struct bluesky_message msg;
        int ret = skynet_mq_pop(BLUE_SKYSERVER->queue, &msg);
        if (ret == 0)
        {
            switch (msg.type)
            {
            case RECV_DATA:
            {
                struct recv_data_message *recv_data_msg = (struct recv_data_message *)msg.data;
                PyObject *arglist = Py_BuildValue("(is)", recv_data_msg->id, recv_data_msg->data);
                PyObject_CallObject(get_socket_server()->data_recv_cb, arglist);
                je_free(recv_data_msg->data);
                Py_DECREF(arglist);
                break;
            }
            case ACCEPTED:
            {
                struct accept_message *ac_msg = (struct accept_message *)msg.data;
                printf("accept1 id:%d\n", ac_msg->id);
                PyObject *arglist = Py_BuildValue("(i)", ac_msg->id);
                PyObject_CallObject(get_socket_server()->accept_cb, arglist);
                Py_DECREF(arglist);
                je_free(ac_msg);
                break;
            }
            case TIME_OUT:
            {
                struct timer_message *timer_msg = (struct timer_message *)msg.data;
                printf("timeout id:%d\n", timer_msg->timer_id);
                timer_timeout(timer_msg->timer_id);
                je_free(timer_msg);
                break;
            }
            case CONNECTED:
            {
                struct connected_message *connected_message = (struct connected_message *)msg.data;
                printf("connected_message id:%d\n", connected_message->id);
                PyObject *arglist = Py_BuildValue("(i)", connected_message->id);
                PyObject_CallObject(get_socket_server()->connect_cb, arglist);
                Py_DECREF(arglist);
                je_free(connected_message);
                break;
            }
            case DISCONNECT:
            {
                struct disconnect_message *disconnect_message = (struct disconnect_message *)msg.data;
                printf("disconnect_message id:%d\n", disconnect_message->id);
                PyObject *arglist = Py_BuildValue("(i)", disconnect_message->id);
                PyObject_CallObject(get_socket_server()->disconnect_cb, arglist);
                Py_DECREF(arglist);
                je_free(disconnect_message);
                break;
            }
            case WRITE_DATA:
            {
                break;
            }
            };
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