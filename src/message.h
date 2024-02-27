#ifndef MESSAGE_H
#define MESSAGE_H

enum MESSAGE_TYPE
{
	RECV_DATA,
	ACCEPTED,
};

struct bluesky_message
{
	enum MESSAGE_TYPE type;
	void *data;
	size_t sz;
};

struct accept_message
{
	int fd;
	void *addr;
	int port;
};

#endif