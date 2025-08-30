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

/* IMPLEMENTED */
int chat_client_connect(struct chat_client *client, const char *addr)
{
	struct addrinfo *addr_info;
	struct addrinfo filter;

	memset(&filter, 0, sizeof(filter));
	filter.ai_family = AF_INET;
	filter.ai_socktype = SOCK_STREAM;

	char *addr_dup = strdup(addr);
	char *delimeter = strrchr(addr_dup, ':');
	*delimeter = '\0';

	int rc = getaddrinfo(addr_dup, delimeter + 1, &filter, &addr_info);
	if (rc != 0) {
		free(addr_dup);
		return CHAT_ERR_SYS;
	}
	free(addr_dup);

	struct addrinfo *iter = addr_info;
	for (; iter != NULL; iter = iter->ai_next) {
		client->socket = socket(iter->ai_family, iter->ai_socktype, iter->ai_protocol);
		if (client->socket == -1)
			continue;

		rc = connect(client->socket, iter->ai_addr, iter->ai_addrlen);
		if (rc == 0)
		{
			int flags = fcntl(client->socket, F_GETFL, 0);
			fcntl(client->socket, F_SETFL, flags | O_NONBLOCK);
			freeaddrinfo(addr_info);
			return 0;
		}
		close(client->socket);
		client->socket = -1;
	}

	freeaddrinfo(addr_info);
	return CHAT_ERR_SYS;
}

/* IMPLEMENTED */
struct chat_message* chat_client_pop_next(struct chat_client *client)
{
	if (rlist_empty(&client->recv_list.node))
		return NULL;

	struct msg_node *next_recv_msg = rlist_first_entry(&client->recv_list.node, struct msg_node, node);
	rlist_del(&next_recv_msg->node);

	struct chat_message *msg = next_recv_msg;
	free(next_recv_msg);

	return msg;
}

static int chat_client_parse_input(struct chat_client *client)
{
	char *current_pos = client->input_buffer;
	size_t remaining len = buffer_len;
	while(1) {
		char *data_end = memchr(current_pos, '\n', remaining_len);
		if (data_end == NULL)
			break;

		size_t data_len = data_end - current_pos;
		size_t len_after_data = remaining_len - (data_len + 1);
		if (len_after_data == 0)
			break;

		char *author_end = memchr(data_end + 1, '\0', len_after_data);
		if (author_end == NULL)
			break;

		size_t author_len = author_end - (data_end + 1);

		struct msg_node *new_node = calloc(1, sizeof(struct msg_node));
		new_node->msg = calloc(1, sizeof(struct chat_message));
		new_node->msg->data = strdup(current_pos, data_len);
		new_node->msg->author = strdup(data_end + 1, author_len);

		rlist_add_tail(&client->recv_list.node, &new_node->node);

		int recv_data_len = author_end - current_pos + 1;

		current_pos += recv_data_len;
		remaining_len -= recv_data_len;
	}

	if (current_pos != client->input_buffer) {
		memmove(client->input_buffer, current_pos, remaining_len);
		client->input_buffer_len = remaining_len;
	}

	return 0;
}

static int chat_client_input(struct chat_client *client)
{
	while (1) {
		if (client->buffer_len == client->buffer_capacity) {
			size_t new_capacity = client->buffer_capacity*2;
			char *new_buffer = realloc(client->input_buffer, new_capacity);
			client->buffer_capacity = new_capacity;
			client->input_buffer = new_buffer;
		}

		ssize_t recv_bytes = recv(client->socket, client->input_buffer + client->buffer_len,
								 client->capacity - client->buffer_len, 0);
		if (recv_bytes > 0) {
			client->buffer_len += recv_bytes;
		}
		else if (recv_bytes == 0) {
			return CHAT_ERR_SYS;
		}
		else {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}

			return CHAT_ERR_SYS;
		}
	}

	return 0;
}

static int chat_client_output(struct chat_client *client)
{
	struct msg_node *iter, *tmp;

	rlist_foreach_entry_safe (msg_snd_iter, &client->send_list.node, node, tmp) {
			struct chat_message *cur_msg = iter->msg;
			size_t msg_len = strlen(cur_msg);

			ssize_t sent_bytes = send(client->socket, cur_msg, msg_len, 0);

			if (sent_bytes < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					return 0;
				}
				return CHAT_ERR_SYS;
			}

			if ((size_t) sent_bytes < msg_len) {
					memove(cur_msg->data, cur_msg->data + sent_bytes, msg_len - sent_bytes + 1);
			}
			chat_message_delete(cur_msg);
			free(iter);
	}

	return 0;
}

/* IMPLEMENTED */
int chat_client_update(struct chat_client *client, double timeout)
{
	if (client->socket < 0)
		return CHAT_ERR_NOT_STARTED;

	struct pollfd pfd;
	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = client->socket;
	pfd.events = chat_events_to_poll_events(chat_client_get_events(client));

	int poll_rc = poll(&pfd, 1, (int)(timeout*1000));

	if (poll_rc == -1)
		return CHAT_ERR_SYS;

	if (poll_rc == 0)
		return CHAT_ERR_TIMEOUT;

	if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
		return CHAT_ERR_SYS;
	}

	int rc = 0;
	if ((pfd.revents & POLLIN) != 0) {
		rc = chat_client_input(client);
		if (rc == 0)
			rc = chat_client_parse_input(client);
	}
	else if ((pfd.revents & POLLOUT) != 0) {
		rc = chat_client_output(client);
	}

	return rc;
}

int chat_client_get_descriptor(const struct chat_client *client)
{
	return client->socket;
}

int chat_client_get_events(const struct chat_client *client)
{
	int bit_mask = 0;

	if (!rlist_empty(&client->recv_list.node))
		bit_mask |= CHAT_EVENT_INPUT;
	if (!rlist_empty(&client->send_list.node))
		bit_mask |= CHAT_EVENT_OUTPUT;

	return bit_mask;
}

static void add_msg_to_send_list(struct chat_client *client, const char *msg, uint32_t msg_size)
{
	struct msg_node *new_node = calloc(1, sizeof(struct msg_node));
	new-node->msg = calloc(1, sizeof(struct chat_message));
	new_node->msg->data = strdup(msg, msg_size);
	rlist_add_tail(&client->send_list.node, &new_node->node);
}

/* IMPLEMENTED */
int chat_client_feed(struct chat_client *client, const char *msg, uint32_t msg_size)
{
	if (client->name != NULL)
	{
		add_msg_to_send_list(client, client->name, strlen(client->name))
		free(client->name);
		client->name = NULL;
	}
	add_msg_to_send_list(client, msg, msg_size);
	return 0;
}
