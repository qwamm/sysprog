#include "chat.h"
#include "chat_client.h"
#include "msg_node.h"

#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/fcntl.h>
#include <errno.h>

#define INPUT_BUFFER_INITIAL_SIZE 4096

struct chat_client {
	/* Socket connected to the server. */
	int socket;
	/* Array of received messages. */
	struct msg_node recv_list;
	/* Output buffer. */
	struct msg_node send_list;
	/* Nickname of the client. */
	char *name;
	/* Input buffer for incoming data. */
	char *input_buffer;
	/* Current length of input buffer. */
	size_t buffer_len;
	/* Maximum number of bytes an input buffer can store. */
	size_t buffer_capacity;
};

/* IMPLEMENTED */
struct chat_client* chat_client_new(const char *name)
{
	struct chat_client *client = calloc(1, sizeof(*client));

	client->socket = -1;
	
	if (name != NULL)
	{
		size_t name_len = strlen(name);
		client->name = malloc(name_len + 2);
		memcpy(client->name, name, name_len);
		client->name[name_len] = '\n';
		client->name[name_len + 1] = '\0';
	}

	client->input_buffer = malloc(INPUT_BUFFER_INITIAL_SIZE);
	client->buffer_capacity = INPUT_BUFFER_INITIAL_SIZE;

	rlist_create(&client->recv_list.node);
	rlist_create(&client->send_list.node);

	return client;
}

/* IMPLEMENTED */
void chat_client_delete(struct chat_client *client)
{
	if (client->socket >= 0)
		close(client->socket);

	struct msg_node *iter, *tmp;

	rlist_foreach_entry_safe(iter, &client->recv_list.node, node ,tmp) {
		chat_message_delete(iter->msg);
		free(iter);
	};

	rlist_foreach_entry_safe(iter, &client->send_list.node, node ,tmp) {
		chat_message_delete(iter->msg);
		free(iter);
	};

	free(client->name);
	free(client->input_buffer);
	free(client);
}

int chat_client_connect(struct chat_client *client, const char *addr)
{
	/*
	 * 1) Use getaddrinfo() to resolve addr to struct sockaddr_in.
	 * 2) Create a client socket (function socket()).
	 * 3) Connect it by the found address (function connect()).
	 */
	/* IMPLEMENT THIS FUNCTION */
	(void)client;
	(void)addr;

	return CHAT_ERR_NOT_IMPLEMENTED;
}

struct chat_message* chat_client_pop_next(struct chat_client *client)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)client;
	return NULL;
}

int chat_client_update(struct chat_client *client, double timeout)
{
	/*
	 * The easiest way to wait for updates on a single socket with a timeout
	 * is to use poll(). Epoll is good for many sockets, poll is good for a
	 * few.
	 *
	 * You create one struct pollfd, fill it, call poll() on it, handle the
	 * events (do read/write).
	 */
	(void)client;
	(void)timeout;
	return CHAT_ERR_NOT_IMPLEMENTED;
}

int chat_client_get_descriptor(const struct chat_client *client)
{
	return client->socket;
}

int chat_client_get_events(const struct chat_client *client)
{
	/*
	 * IMPLEMENT THIS FUNCTION - add OUTPUT event if has non-empty output
	 * buffer.
	 */
	(void)client;
	return CHAT_EVENT_INPUT;
}

int chat_client_feed(struct chat_client *client, const char *msg, uint32_t msg_size)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)client;
	(void)msg;
	(void)msg_size;
	return CHAT_ERR_NOT_IMPLEMENTED;
}
