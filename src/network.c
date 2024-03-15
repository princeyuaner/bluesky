#include <network.h>
#include <skynet_mq.h>
#include <server.h>
#include <pthread.h>
#include <message.h>
#include <stddef.h>

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
    else if (events & BEV_EVENT_CONNECTED)
    {
        printf("服务器已连接1\n");
        return;
    }
    /* None of the other events can happen here, since we haven't enabled
     * timeouts */
    bufferevent_free(bev);
}

static void conn_readcb(struct bufferevent *bev, void *arg)
{
    struct socket *s = (struct socket *)arg;
    struct evbuffer *input = bufferevent_get_input(bev);
    size_t totalLen = evbuffer_get_length(input);
    char *buf = je_malloc(sizeof(*buf) * totalLen);
    bufferevent_read(bev, buf, totalLen);
    printf("receive data:%s, size1:%d id:%d\n", buf, (int)totalLen, s->id);
    struct bluesky_message smsg;
    smsg.type = RECV_DATA;
    struct recv_data_message recv_data_msg;
    recv_data_msg.id = s->id;
    recv_data_msg.data = buf;
    smsg.data = &recv_data_msg;
    skynet_mq_push(BLUE_SKYSERVER->queue, &smsg);
    pthread_cond_signal(&BLUE_SKYSERVER->cond);
}

static void conn_writecb(struct bufferevent *bev, void *arg)
{
    struct socket *s = (struct socket *)arg;
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
    struct bufferevent *client_bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!client_bev)
    {
        printf("Error constructing bufferevent!");
        event_base_loopbreak(base);
        return;
    }
    int id = make_id(SOCKET_SERVER);
    struct socket *s = &SOCKET_SERVER->slot[HASH_ID(id)];
    printf("accept fd:%d id:%d\n", fd, id);
    bufferevent_setcb(client_bev, conn_readcb, conn_writecb, conn_eventcb, s);
    printf("accept fd:%d id:%d\n", fd, id);
    bufferevent_enable(client_bev, EV_READ);
    printf("accept fd:%d id:%d\n", fd, id);
    s->fd = fd;
    s->id = id;
    s->client_bev = client_bev;
    struct bluesky_message smsg;
    smsg.type = ACCEPTED;
    struct accept_message *ac_msg = je_malloc(sizeof(*ac_msg));
    ac_msg->id = s->id;
    smsg.data = ac_msg;
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

static void do_connect(struct request_connect *connect)
{
    struct bufferevent *bev;
    bev = bufferevent_socket_new(SOCKET_BASE, -1, BEV_OPT_CLOSE_ON_FREE);

    // 连接服务器
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(connect->port);
    evutil_inet_pton(AF_INET, connect->addr, &serv.sin_addr.s_addr);
    bufferevent_socket_connect(bev, (struct sockaddr *)&serv, sizeof(serv));

    // 设置回调
    bufferevent_setcb(bev, conn_readcb, conn_writecb, conn_eventcb, &connect->id);
    bufferevent_enable(bev, EV_READ | EV_PERSIST);
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
        struct socket *socket = &SOCKET_SERVER->slot[HASH_ID(send->id)];
        printf("增加写事件:%d\n", send->id);
        if (socket != NULL)
        {
            bufferevent_enable(socket->client_bev, EV_WRITE);
        }
        break;
    }
    case 'C':
    {
        printf("do_connect\n");
        do_connect((struct request_connect *)buffer);
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

static PyObject *network_listen(PyObject *self, PyObject *args)
{
    printf("network_listen\n");
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
    int id;
    char *data;
    if (!PyArg_ParseTuple(args, "is", &id, &data))
    {
        Py_RETURN_NONE;
    }
    struct socket *s = &SOCKET_SERVER->slot[HASH_ID(id)];
    // 将待发送的数据写入socket队列
    struct bluesky_message smsg;
    smsg.type = WRITE_DATA;
    smsg.data = data;
    skynet_mq_push(s->message_queue, &smsg);
    // 推送管道提醒
    struct request_package request;
    request.u.send.id = id;
    send_request(&request, 'W', sizeof(request.u.send));
    Py_RETURN_NONE;
}

static PyObject *network_connect(PyObject *self, PyObject *args)
{
    printf("network_connect\n");
    char *addr;
    int port;
    if (!PyArg_ParseTuple(args, "is", &addr, &port))
    {
        Py_RETURN_NONE;
    }
    int id = make_id(SOCKET_SERVER);
    struct request_package request;
    request.u.connect.port = port;
    request.u.connect.addr = addr;
    request.u.connect.id = id;
    send_request(&request, 'C', sizeof(request.u.send));
    PyObject *ret = Py_BuildValue("(i)", id);
    return ret;
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
    SOCKET_SERVER->id = 0;

    for (int i = 0; i < MAX_SOCKET; i++)
    {
        struct socket *s = &SOCKET_SERVER->slot[i];
        s->type = SOCKET_TYPE_INVALID;
        s->message_queue = skynet_mq_create();
    }
    return true;
}

struct socket_server *get_socket_server()
{
    return SOCKET_SERVER;
}

int make_id(struct socket_server *ss)
{
    for (int i = 0; i < MAX_SOCKET; i++)
    {
        int id = __sync_add_and_fetch(&(ss->id), 1);
        printf("make_id %d\n", id);
        if (id < 0)
        {
            id = __sync_and_and_fetch(&(ss->id), 0x7fffffff);
        }
        struct socket *s = &ss->slot[HASH_ID(id)];
        if (__sync_bool_compare_and_swap(&s->type, SOCKET_TYPE_INVALID, SOCKET_TYPE_RESERVE))
        {
            s->id = id;
            s->fd = -1;
            return id;
        }
    }
    return -1;
}

static PyMethodDef network_methods[] = {
    {"init", (PyCFunction)network_init, METH_VARARGS, NULL},
    {"listen", (PyCFunction)network_listen, METH_VARARGS, NULL},
    {"write_data", (PyCFunction)network_write_data, METH_VARARGS, NULL},
    {"connect", (PyCFunction)network_connect, METH_VARARGS, NULL},
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