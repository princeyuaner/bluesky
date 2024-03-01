#ifndef MESSAGE_H
#define MESSAGE_H

enum MESSAGE_TYPE
{
	RECV_DATA,
	ACCEPTED,
	WRITE_DATA,
};

struct bluesky_message
{
	enum MESSAGE_TYPE type;
	void *data;
	size_t sz;
};

struct accept_message
{
	size_t fd;
	char *addr;
	int port;
};

struct recv_data_message
{
	size_t fd;
	char *data;
};

struct write_data_message
{
	char *data;
};

#endif