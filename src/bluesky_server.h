#ifndef SERVER_H
#define SERVER_H

#include <skynet_mq.h>
#include <Python.h>

struct bluesky_server
{
    struct message_queue *queue; // 消息队列
    pthread_cond_t cond;         // 条件变量
    pthread_mutex_t mutex;       // 互斥锁
};

struct bluesky_server *BLUE_SKYSERVER;

void create_bluesky_server();

PyObject *PyInit_server();

#endif