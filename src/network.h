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

struct socket_server
{
    int recv_fd; //接收管道
    int send_fd; //发送管道
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

PyObject *PyInit_network();