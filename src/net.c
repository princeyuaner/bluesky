#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#ifndef _WIN32
#include <netinet/in.h>
#ifdef _XOPEN_SOURCE_EXTENDED
#include <arpa/inet.h>
#endif
#include <sys/socket.h>
#endif

#include <event2/bufferevent.h>
#include <event2/bufferevent_struct.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include <stdlib.h>
#include <Python.h>
#include <bluesky_malloc.h>

struct socket_server
{
    int recv_fd;
    int send_fd;
};

struct request_listen
{
    int port;
};

struct request_accept
{
    int fd;
};

struct request_send
{
    char *data;
};

struct request_package
{
    uint8_t header[8]; // 6 bytes dummy
    union
    {
        char buffer[256];
        struct request_listen listen;
        struct request_accept accept;
        struct request_send send;
    } u;
    uint8_t dummy[256];
};

static struct socket_server *SOCKET_SERVER = NULL;
static struct socket_server *SOCKET_RESPONSE_SERVER = NULL;
static struct event_base *base = NULL;
static struct event_base *main_loop_base = NULL;
static struct bufferevent *client_bev = NULL;

static void pipe_read(int, short, void *);
static void signal_cb(evutil_socket_t, short, void *);
static void do_listen(struct request_listen *listen);
static PyObject *my_callback = NULL;
static PyObject *my_recvdata = NULL;

void *socket_init(void *args)
{

    struct event *ev;

    base = event_base_new();
    if (!base)
    {
        fprintf(stderr, "Could not initialize libevent!\n");
        return NULL;
    }

    ev = event_new(base, SOCKET_SERVER->recv_fd, EV_READ | EV_PERSIST, pipe_read, NULL);
    event_add(ev, NULL);

    event_base_dispatch(base);
    event_base_free(base);
    return NULL;
}

void init()
{
    if (SOCKET_SERVER)
    {
        return;
    }
    int fd[2];
    if (pipe(fd))
    {
        return;
    }
    SOCKET_SERVER = malloc(sizeof(*SOCKET_SERVER));
    SOCKET_SERVER->recv_fd = fd[0];
    SOCKET_SERVER->send_fd = fd[1];
    pthread_t sokcet_t;
    pthread_create(&sokcet_t, NULL, socket_init, NULL);
}

static void
pipe_read(int fd, short which, void *args)
{
    uint8_t buffer[256];
    uint8_t header[2];
    int num = read(fd, header, sizeof(header));
    if (num < 0)
    {
        return;
    }
    int type = header[0];
    int len = header[1];
    num = read(fd, buffer, len);
    if (num < 0)
    {
        return;
    }

    switch (type)
    {
    case 'L':
        printf("do_listen\n");
        do_listen((struct request_listen *)buffer);
        break;
    case 'S':
        bufferevent_write(client_bev, ((struct request_send *)buffer)->data, strlen(((struct request_send *)buffer)->data));
        bufferevent_enable(client_bev, EV_WRITE);
        break;
    default:
        return;
    };
}

static void
response_read(int fd, short which, void *args)
{
    uint8_t buffer[256];
    uint8_t header[2];
    int num = read(fd, header, sizeof(header));
    if (num < 0)
    {
        return;
    }
    int type = header[0];
    int len = header[1];
    num = read(fd, buffer, len);
    if (num < 0)
    {
        return;
    }

    switch (type)
    {
    case 'A':
    {
        printf("accept\n");
        PyObject *arglist = Py_BuildValue("(i)", ((struct request_accept *)buffer)->fd);
        PyObject_CallObject(my_callback, arglist);
        Py_DECREF(arglist);
        break;
    }
    case 'S':
    {
        PyObject *arglist = Py_BuildValue("(s)", ((struct request_send *)buffer)->data);
        PyObject_CallObject(my_recvdata, arglist);
        Py_DECREF(arglist);
        break;
    }
    default:
        return;
    };
}

static void
send_request(struct request_package *request, char type, int len)
{
    request->header[6] = (uint8_t)type;
    request->header[7] = (uint8_t)len;
    const char *req = (const char *)request + offsetof(struct request_package, header[6]);
    for (;;)
    {
        ssize_t n = write(SOCKET_SERVER->send_fd, req, len + 2);
        if (n < 0)
        {
            continue;
        }
        assert(n == len + 2);
        return;
    }
}

static void
send_response(struct request_package *request, char type, int len)
{
    request->header[6] = (uint8_t)type;
    request->header[7] = (uint8_t)len;
    const char *req = (const char *)request + offsetof(struct request_package, header[6]);
    for (;;)
    {
        ssize_t n = write(SOCKET_RESPONSE_SERVER->send_fd, req, len + 2);
        if (n < 0)
        {
            continue;
        }
        assert(n == len + 2);
        return;
    }
}

static void
signal_cb(evutil_socket_t sig, short events, void *user_data)
{
    struct event_base *base = user_data;
    struct timeval delay = {2, 0};

    printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");

    event_base_loopexit(base, &delay);
}

static void
conn_readcb(struct bufferevent *bev, void *arg)
{
    char buf[100];
    bzero(buf, sizeof(buf));
    size_t size = bufferevent_read(bev, buf, sizeof(buf));
    printf("receive data:%s, size:%d\n", buf, (int)size);
    // int len = atoi(buf);
    // printf("长度:%d\n",len);
    // char * datas = malloc(len);
    // bufferevent_read(bev,datas,len);

    struct request_package request;
    request.u.send.data = malloc(sizeof(buf));
    memcpy(request.u.send.data, &buf, sizeof(buf));
    send_response(&request, 'S', sizeof(request.u.send));
}

// static void
// conn_writecb(struct bufferevent *bev, void *user_data)
// {
//     struct evbuffer *output = bufferevent_get_output(bev);
//     if (evbuffer_get_length(output) == 0)
//     {
//         bufferevent_write(bev, "aaa", strlen("MESSAGE"));
//         sleep(2);
//         bufferevent_enable(bev, EV_WRITE);
//     }
// }

static void
conn_eventcb(struct bufferevent *bev, short events, void *user_data)
{
    if (events & BEV_EVENT_EOF)
    {
        printf("Connection closed.\n");
    }
    else if (events & BEV_EVENT_ERROR)
    {
        printf("Got an error on the connection: %s\n",
               strerror(errno)); /*XXX win32*/
    }
    /* None of the other events can happen here, since we haven't enabled
     * timeouts */
    bufferevent_free(bev);
}

static void
listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
            struct sockaddr *sa, int socklen, void *user_data)
{
    struct event_base *base = user_data;
    printf("listener_cb:%d", fd);
    client_bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!client_bev)
    {
        printf("Error constructing bufferevent!");
        event_base_loopbreak(base);
        return;
    }
    bufferevent_setcb(client_bev, conn_readcb, NULL, conn_eventcb, NULL);
    bufferevent_enable(client_bev, EV_READ);

    struct request_package request;
    request.u.accept.fd = (int)client_bev->ev_read.ev_fd;
    send_response(&request, 'A', sizeof(request.u.accept));
}

static void do_listen(struct request_listen *listen)
{
    struct sockaddr_in sin = {0};
    struct evconnlistener *listener;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(listen->port);

    listener = evconnlistener_new_bind(base, listener_cb, (void *)base,
                                       LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
                                       (struct sockaddr *)&sin,
                                       sizeof(sin));

    if (!listener)
    {
        printf("Could not create a listener!\n");
        return;
    }
}

static PyObject *py_init(PyObject *self, PyObject *args)
{
    printf("py_init");
    init();
    Py_RETURN_NONE;
}

static PyObject *py_listen(PyObject *self, PyObject *args)
{
    printf("py_listen");
    char *ip;
    int port;
    if (!PyArg_ParseTuple(args, "si", &ip, &port))
    {
        return NULL;
    }
    struct request_package request;
    request.u.listen.port = port;
    send_request(&request, 'L', sizeof(request.u.listen));
    Py_RETURN_NONE;
}

static PyObject *py_send(PyObject *self, PyObject *args)
{
    char *data;
    if (!PyArg_ParseTuple(args, "s", &data))
    {
        return NULL;
    }
    struct request_package request;
    request.u.send.data = data;
    send_request(&request, 'S', sizeof(request.u.send));
    Py_RETURN_NONE;
}

static PyObject *py_register(PyObject *self, PyObject *args)
{
    PyObject *result = NULL;
    PyObject *temp;
    PyObject *temprecvdata;

    if (PyArg_ParseTuple(args, "OO", &temp, &temprecvdata))
    {
        if (!PyCallable_Check(temp))
        {
            PyErr_SetString(PyExc_TypeError, "parameter must be callable");
            return NULL;
        }
        Py_XINCREF(temp);        /* Add a reference to new callback */
        Py_XDECREF(my_callback); /* Dispose of previous callback */
        my_callback = temp;      /* Remember new callback */

        Py_XINCREF(temprecvdata);   /* Add a reference to new callback */
        Py_XDECREF(my_recvdata);    /* Dispose of previous callback */
        my_recvdata = temprecvdata; /* Remember new callback */
        /* Boilerplate to return "None" */
        Py_INCREF(Py_None);
        result = Py_None;
    }
    return result;
}

static PyObject *py_start(PyObject *self, PyObject *args)
{
    if (SOCKET_RESPONSE_SERVER)
    {
        return Py_None;
    }
    int fd[2];
    if (pipe(fd))
    {
        return Py_None;
    }
    SOCKET_RESPONSE_SERVER = malloc(sizeof(*SOCKET_RESPONSE_SERVER));
    SOCKET_RESPONSE_SERVER->recv_fd = fd[0];
    SOCKET_RESPONSE_SERVER->send_fd = fd[1];

    struct event *signal_event;
    struct event *ev;

    main_loop_base = event_base_new();
    if (!main_loop_base)
    {
        fprintf(stderr, "Could not initialize libevent!\n");
        return Py_None;
    }

    signal_event = evsignal_new(main_loop_base, SIGINT, signal_cb, (void *)main_loop_base);

    if (!signal_event || event_add(signal_event, NULL) < 0)
    {
        fprintf(stderr, "Could not create/add a signal event!\n");
        return NULL;
    }

    ev = event_new(main_loop_base, SOCKET_RESPONSE_SERVER->recv_fd, EV_READ | EV_PERSIST, response_read, NULL);
    event_add(ev, NULL);

    event_base_dispatch(main_loop_base);
    event_free(signal_event);
    event_base_free(main_loop_base);
    return Py_None;
}

static PyMethodDef net_methods[] = {
    {"init", (PyCFunction)py_init, METH_NOARGS, NULL},
    {"listen", (PyCFunction)py_listen, METH_VARARGS, NULL},
    {"register", (PyCFunction)py_register, METH_VARARGS, NULL},
    {"start", (PyCFunction)py_start, METH_NOARGS, NULL},
    {"send", (PyCFunction)py_send, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef Combinations =
    {
        PyModuleDef_HEAD_INIT,
        "Combinations",                                                          /* name of module */
        "usage: Combinations.uniqueCombinations(lstSortableItems, comboSize)\n", /* module documentation, may be NULL */
        -1,                                                                      /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
        net_methods};

PyMODINIT_FUNC PyInit_net()
{
    return PyModule_Create(&Combinations);
};