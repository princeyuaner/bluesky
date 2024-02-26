#include <network.h>
#include <skynet_mq.h>

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
    bzero(buf, sizeof(buf));
    bufferevent_read(bev, buf,totalLen);
    printf("receive data:%s, size:%d\n", buf,(int)totalLen);
}

static void conn_writecb(struct bufferevent *bev, void *arg)
{
    return;
}

static void listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
                        struct sockaddr *sa, int socklen, void *user_data)
{
    struct event_base *base = user_data;
    printf("accept fd:%d", fd);
    struct bufferevent *clientBev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!clientBev)
    {
        printf("Error constructing bufferevent!");
        event_base_loopbreak(base);
        return;
    }
    bufferevent_setcb(clientBev, conn_readcb, conn_writecb, conn_eventcb, NULL);
    bufferevent_enable(clientBev, EV_READ);
    struct socket *s = je_malloc(sizeof(*s));
    s->fd = fd;
    s->clientBev = clientBev;
    SOCKET_SERVER->slot[fd] = s;
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
    case 'S':
        {
            struct request_send * send = (struct request_send *)buffer;
            struct socket *socket =  SOCKET_SERVER->slot[send->fd];
            if(socket != NULL)
            {
                bufferevent_enable(socket->clientBev, EV_WRITE);
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
    
    pthread_t sokcet_t;
    pthread_create(&sokcet_t, NULL, new_socket_event, NULL);
    Py_RETURN_NONE;
}

static PyObject *py_start(PyObject *self, PyObject *args)
{
    while (true)
    {
        /* code */
    }
    
    return Py_None;
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

static PyMethodDef network_methods[] = {
    {"init", (PyCFunction)network_init, METH_NOARGS, NULL},
    {"listen", (PyCFunction)py_listen, METH_VARARGS, NULL},
    {"start", (PyCFunction)py_start, METH_NOARGS, NULL},
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