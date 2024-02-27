#ifndef MESSAGE_H
#define MESSAGE_H

#include <define.h>

struct bluesky_message
{
	message_type type;
	void *data;
	size_t sz;
};

#endif