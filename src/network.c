#include <network.h>
#include <skynet_mq.h>
#include <server.h>
#include <pthread.h>
#include <message.h>

static struct socket_server *SOCKET_SERVER = NULL;
static struct event_base *SOCKET_BASE = NULL;

static void conn_eventcb(struct bufferevent *bev, short events, void *user_data)
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

static void conn_readcb(struct bufferevent *bev, void *arg)
{
    struct evbuffer *input = bufferevent_get_input(bev);
    size_t totalLen = evbuffer_get_length(input);
    char buf[totalLen];
    bufferevent_read(bev, buf, totalLen);
    printf("receive data:%s, size1:%d\n", buf, (int)totalLen);
    struct bluesky_message smsg;
    smsg.type = RECV_DATA;
    struct recv_data_message recv_data_msg;
    recv_data_msg.fd = bev->ev_read.ev_fd;
    recv_data_msg.data = buf;
    smsg.data = &recv_data_msg;
    skynet_mq_push(BLUE_SKYSERVER->queue, &smsg);
    pthread_cond_signal(&BLUE_SKYSERVER->cond);
}

static void conn_writecb(struct bufferevent *bev, void *arg)
{
    struct socket *s = SOCKET_SERVER->slot[bev->ev_read.ev_fd];
    struct bluesky_message message;
    if (skynet_mq_top(s->message_queue, &message) == 1)
    {
        // 没有数据要发送，取消监听
        bufferevent_disable(bev, EV_WRITE);
    }
    else
    {
        int result = bufferevent_write(bev, message.data, strlen(message.data));
        printf("写数据 %s %d %d\n", (char *)message.data, (int)strlen(message.data), result);
        if (result == 0)
        {
            printf("写数据成功\n");
            skynet_mq_pop(s->message_queue, &message);
            // if (skynet_mq_length(s->message_queue) == 0)
            // {
            //     // 没有数据要发送，取消监听
            //     bufferevent_disable(bev, EV_WRITE);
            // }
        }
    }
}

static void listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
                        struct sockaddr *sa, int socklen, void *user_data)
{
    struct event_base *base = user_data;
    printf("accept fd:%d\n", fd);
    struct bufferevent *client_bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!client_bev)
    {
        printf("Error constructing bufferevent!");
        event_base_loopbreak(base);
        return;
    }
    bufferevent_setcb(client_bev, conn_readcb, conn_writecb, conn_eventcb, NULL);
    bufferevent_enable(client_bev, EV_READ);
    struct socket *s = je_malloc(sizeof(*s));
    s->fd = fd;
    s->client_bev = client_bev;
    s->message_queue = skynet_mq_create();
    SOCKET_SERVER->slot[fd] = s;
    struct bluesky_message smsg;
    smsg.type = ACCEPTED;
    struct accept_message ac_msg;
    ac_msg.fd = fd;
    smsg.data = &ac_msg;
    printf("地址1%p\n", &ac_msg);
    skynet_mq_push(BLUE_SKYSERVER->queue, &smsg);
    pthread_cond_signal(&BLUE_SKYSERVER->cond);
}

static void do_listen(struct request_listen *listen)
{
    struct sockaddr_in sin = {0};
    struct evconnlistener *listener;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(listen->port);

    listener = evconnlistener_new_bind(SOCKET_BASE, listener_cb, (void *)SOCKET_BASE,
                                       LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
                                       (struct sockaddr *)&sin,
                                       sizeof(sin));

    if (!listener)
    {
        printf("Could not create a listener!\n");
        return;
    }
}

static void pipe_read(int fd, short which, void *args)
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
    case 'W':
    {
        struct request_send *send = (struct request_send *)buffer;
        struct socket *socket = SOCKET_SERVER->slot[send->fd];
        printf("增加写事件:%d\n", send->fd);
        if (socket != NULL)
        {
            bufferevent_enable(socket->client_bev, EV_WRITE);
        }
        break;
    }
    default:
        return;
    };
}

void *new_socket_event(void *args)
{

    struct event *ev;

    SOCKET_BASE = event_base_new();
    if (!SOCKET_BASE)
    {
        fprintf(stderr, "Could not initialize libevent!\n");
        return NULL;
    }

    ev = event_new(SOCKET_BASE, SOCKET_SERVER->recv_fd, EV_READ | EV_PERSIST, pipe_read, NULL);
    event_add(ev, NULL);

    event_base_dispatch(SOCKET_BASE);
    event_base_free(SOCKET_BASE);
    return NULL;
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

static PyObject *py_listen(PyObject *self, PyObject *args)
{
    printf("py_listen\n");
    char *ip;
    int port;
    if (!PyArg_ParseTuple(args, "si", &ip, &port))
    {
        Py_RETURN_NONE;
    }
    struct request_package request;
    request.u.listen.port = port;
    send_request(&request, 'L', sizeof(request.u.listen));
    Py_RETURN_NONE;
}

static PyObject *network_init(PyObject *self, PyObject *args)
{
    if (SOCKET_SERVER)
    {
        Py_RETURN_NONE;
    }

    if (create_socket_server() == false)
    {
        Py_RETURN_NONE;
    }

    PyObject *accept_cb;
    PyObject *disconnect_cd;
    PyObject *data_recv_cb;

    if (PyArg_ParseTuple(args, "OOO", &accept_cb, &disconnect_cd, &data_recv_cb))
    {
        if (!PyCallable_Check(accept_cb))
        {
            PyErr_SetString(PyExc_TypeError, "accept_cb must be callable");
            Py_RETURN_NONE;
        }
        if (!PyCallable_Check(disconnect_cd))
        {
            PyErr_SetString(PyExc_TypeError, "disconnect_cd must be callable");
            Py_RETURN_NONE;
        }
        if (!PyCallable_Check(data_recv_cb))
        {
            PyErr_SetString(PyExc_TypeError, "data_recv_cb must be callable");
            Py_RETURN_NONE;
        }
        Py_XINCREF(accept_cb);                /* Add a reference to new callback */
        SOCKET_SERVER->accept_cb = accept_cb; /* Remember new callback */

        Py_XINCREF(disconnect_cd);                    /* Add a reference to new callback */
        SOCKET_SERVER->disconnect_cb = disconnect_cd; /* Remember new callback */

        Py_XINCREF(data_recv_cb);                   /* Add a reference to new callback */
        SOCKET_SERVER->data_recv_cb = data_recv_cb; /* Remember new callback */
    }

    pthread_t sokcet_t;
    pthread_create(&sokcet_t, NULL, new_socket_event, NULL);
    Py_RETURN_NONE;
}

static PyObject *network_write_data(PyObject *self, PyObject *args)
{
    printf("network_write_data\n");
    int fd;
    char *data;
    if (!PyArg_ParseTuple(args, "is", &fd, &data))
    {
        Py_RETURN_NONE;
    }
    struct socket *s = SOCKET_SERVER->slot[fd];
    // 将待发送的数据写入socket队列
    struct bluesky_message smsg;
    smsg.type = WRITE_DATA;
    smsg.data = data;
    skynet_mq_push(s->message_queue, &smsg);
    // 推送管道提醒
    struct request_package request;
    request.u.send.fd = fd;
    send_request(&request, 'W', sizeof(request.u.send));
    Py_RETURN_NONE;
}

bool create_socket_server()
{
    int fd[2];
    if (pipe(fd))
    {
        return false;
    }
    SOCKET_SERVER = je_malloc(sizeof(*SOCKET_SERVER));
    SOCKET_SERVER->recv_fd = fd[0];
    SOCKET_SERVER->send_fd = fd[1];
    return true;
}

struct socket_server *get_socket_server()
{
    return SOCKET_SERVER;
}

static PyMethodDef network_methods[] = {
    {"init", (PyCFunction)network_init, METH_VARARGS, NULL},
    {"listen", (PyCFunction)py_listen, METH_VARARGS, NULL},
    {"write_data", (PyCFunction)network_write_data, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef network_module =
    {
        PyModuleDef_HEAD_INIT,
        "network",                                                               /* name of module */
        "usage: Combinations.uniqueCombinations(lstSortableItems, comboSize)\n", /* module documentation, may be NULL */
        -1,                                                                      /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
        network_methods};

PyObject *PyInit_network()
{
    return PyModule_Create(&network_module);
};