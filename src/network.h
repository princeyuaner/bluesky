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
#include <Python.h>
#include <jemalloc.h>
#include <spinlock.h>

#define MAX_SOCKET (1 << 16)

struct socket
{
    int fd;
    struct spinlock dw_lock;
    const void *dw_buffer;
    struct bufferevent *client_bev;
};

struct socket_server
{
    int recv_fd;                     // 接收管道
    int send_fd;                     // 发送管道
    struct socket *slot[MAX_SOCKET]; // socket列表
    PyObject *accept_cb;             // 连接回调
    PyObject *disconnect_cb;         // 断开连接回调
    PyObject *data_recv_cb;          // 接收数据回调
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
    int fd;
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

bool create_socket_server();
struct socket_server *get_socket_server();

PyObject *PyInit_network();