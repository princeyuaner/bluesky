#ifndef MESSAGE_H
#define MESSAGE_H

enum MESSAGE_TYPE
{
	RECV_DATA,
	ACCEPTED,
	WRITE_DATA,
	TIME_OUT,
	CONNECTED,
};

struct bluesky_message
{
	enum MESSAGE_TYPE type;
	void *data;
	size_t sz;
};

struct accept_message
{
	int id;
	char *addr;
	int port;
};

struct recv_data_message
{
	int id;
	char *data;
};

struct write_data_message
{
	char *data;
};

struct connect_message
{
	size_t port;
	char *addr;
};

struct timer_message
{
	uint32_t timer_id;
};

struct connected_message
{
	int id;
};

#endif