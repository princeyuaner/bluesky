#include <network.h>

static struct socket_pip *SOCKET_PIP = NULL;
static struct event_base *socket_base = NULL;
static struct bufferevent *client_bev = NULL;

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
    char buf[100];
    bzero(buf, sizeof(buf));
    size_t size = bufferevent_read(bev, buf, sizeof(buf));
    printf("receive data:%s, size:%d\n", buf, (int)size);
    // int len = atoi(buf);
    // printf("长度:%d\n",len);
    // char * datas = je_malloc(len);
    // bufferevent_read(bev,datas,len);

    struct request_package request;
    request.u.send.data = je_malloc(sizeof(buf));
    memcpy(request.u.send.data, &buf, sizeof(buf));
    // send_response(&request, 'S', sizeof(request.u.send));
}

static void listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
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

    // struct request_package request;
    // request.u.accept.fd = (int)client_bev->ev_read.ev_fd;
    // send_response(&request, 'A', sizeof(request.u.accept));
}

static void do_listen(struct request_listen *listen)
{
    struct sockaddr_in sin = {0};
    struct evconnlistener *listener;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(listen->port);

    listener = evconnlistener_new_bind(socket_base, listener_cb, (void *)socket_base,
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
        // bufferevent_write(client_bev, ((struct request_send *)buffer)->data, strlen(((struct request_send *)buffer)->data));
        // bufferevent_enable(client_bev, EV_WRITE);
        break;
    default:
        return;
    };
}

void *new_socket_event(void *args)
{

    struct event *ev;

    socket_base = event_base_new();
    if (!socket_base)
    {
        fprintf(stderr, "Could not initialize libevent!\n");
        return NULL;
    }

    ev = event_new(socket_base, SOCKET_PIP->recv_fd, EV_READ | EV_PERSIST, pipe_read, NULL);
    event_add(ev, NULL);

    event_base_dispatch(socket_base);
    event_base_free(socket_base);
    return NULL;
}

static PyObject *network_init(PyObject *self, PyObject *args)
{
    if (SOCKET_PIP)
    {
        Py_RETURN_NONE;
    }
    int fd[2];
    if (pipe(fd))
    {
        Py_RETURN_NONE;
    }
    SOCKET_PIP = je_malloc(sizeof(*SOCKET_PIP));
    SOCKET_PIP->recv_fd = fd[0];
    SOCKET_PIP->send_fd = fd[1];
    pthread_t sokcet_t;
    pthread_create(&sokcet_t, NULL, new_socket_event, NULL);
    Py_RETURN_NONE;
}

static PyMethodDef network_methods[] = {
    {"init", (PyCFunction)network_init, METH_NOARGS, NULL},
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