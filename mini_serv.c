#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
#include <fcntl.h>

void receive_clients(void);
void remove_client(int id);

typedef struct client
{
	int id;
	int fd;
	char *buf;
	struct client *next;

} client;

client *client_list = NULL;
fd_set fd_all;
fd_set fd_write;
fd_set fd_read;
int sockfd;

void my_printf(char *str, int fd)
{
	if (str && fd >= 0)
		write(fd, str, strlen(str));
}

void clean_all()
{
	client *ptr = client_list;

	while (ptr)
	{
		remove_client(ptr->id);
		ptr = client_list;
	}

	if (sockfd >= 0)
		close(sockfd);
}

int fatal() // Bazooka :D
{
	clean_all();
	my_printf("Fatal error\n", 2);
	exit(1);
}

// Returns -1 in case of error, 0 if no message, 1 in case of message
int extract_message(char **buf, char **msg)
{
	char *newbuf;
	int i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				fatal();
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

// Return NULL in case of failure. Frees buf but not add.
char *str_join(char *buf, char *add)
{
	char *newbuf;
	int len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		fatal();
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

// Sends a message to everyone on the server excepts the sender
void broadcast_message(char *message, client *sender)
{
	client *ptr = client_list;

	if (message == NULL)
		return;

	while (ptr)
	{
		if (ptr != sender && FD_ISSET(ptr->fd, &fd_write)) // On ne renvoie pas le message de quelqu'un à lui-même et On envoie le message a ce client que si il est pret a le recevoir
		{

			my_printf("Sending message\n", 1);

			send(ptr->fd, message, strlen(message), 0);
		}
		ptr = ptr->next;
	}
}

void add_client(int fd)
{
	client *new_client;
	client *ptr = client_list;
	static int id = 0;

	if (fd < 0)
		return;

	// Ici on créé un nouveau client pour l'ajouter a la liste des structures clients
	new_client = malloc(sizeof(client) * 1);
	if (new_client == NULL)
		fatal();
	new_client->id = id++;
	new_client->fd = fd;
	// Add client fd to the set of fd to listen to
	FD_SET(fd, &fd_all);
	new_client->buf = NULL;
	new_client->next = NULL;

	// Tell everyone that a nex client has arrived
	char welcome_msg[100];
	sprintf(welcome_msg, "server: client %d just arrived\n", new_client->id);
	broadcast_message(welcome_msg, new_client);

	// If there is no one yet, client_list becomes the new_client
	if (ptr == NULL)
	{
		client_list = new_client;

		my_printf("New client!\n", 1);

		return;
	}

	// We go to the end of the user list to add the new one
	while (ptr->next)
		ptr = ptr->next;
	ptr->next = new_client;

	my_printf("New client!\n", 1);
}

void remove_client(int id)
{
	client *ptr = client_list;
	client *ptr_prev = NULL;

	// Find the client to remove
	while (ptr && ptr->next && ptr->id != id)
	{
		ptr_prev = ptr;
		ptr = ptr->next;
	}

	// If client not found, do nothing
	if (ptr == NULL || ptr->id != id)
		return;

	if (ptr_prev) // If client has a previous element, reattach properly the clients without the one to remove (A-B-C => remove B => A--C)
		ptr_prev->next = ptr->next;
	else // If we delete the first element of client_list, we want it to be properly set.
		client_list = ptr->next;

	// Tell everyone that client disconnected
	char goodbye_msg[100];
	sprintf(goodbye_msg, "server: client %d just left\n", ptr->id);
	broadcast_message(goodbye_msg, ptr);

	// Remove client socket fd from the set of fd to listen to
	FD_CLR(ptr->fd, &fd_all);
	// Close its socket fd
	close(ptr->fd);
	free(ptr->buf);
	free(ptr);

	my_printf("Client left.\n", 1);
}

int find_maxfd(int sockfd)
{
	client *ptr = client_list;
	int maxfd = sockfd;

	// If there is no client, the maxfd is the one of the listening server ocket
	if (ptr == NULL)
		return sockfd;

	while (ptr)
	{
		if (ptr->fd > maxfd)
			maxfd = ptr->fd;
		ptr = ptr->next;
	}

	return maxfd;
}

int receive_message(client *client)
{
	int len = 0;
	int ret = 0;
	char buffer[4096 + 1];

	my_printf("in receive message\n", 1);

	// While there are characters to read, add them to the client's buffer
	while ((ret = recv(client->fd, buffer, 4096, 0)) > 0)
	{
		len += ret;
		buffer[ret] = 0;
		char *new_client_buffer = str_join(client->buf, buffer);
		client->buf = new_client_buffer;
	}
	if (len == 0 && ret == -1) // Indicates that an error occured
	{
		my_printf("error\n", 1);
		return (-1);
	}

	my_printf("Client told that: \n", 1);
	my_printf(client->buf, 1);

	return (len);
}

// Receives the message from clients and sends it back to the other
void receive_clients()
{
	client *ptr = client_list;
	int ret;
	char *msg;

	while (ptr)
	{
		if (FD_ISSET(ptr->fd, &fd_read))
		{
			ret = receive_message(ptr);
			if (ret == 0) // Client disconnection
			{

				my_printf("Client disconnection detected.\n", 1);

				client *to_remove = ptr;
				remove_client(to_remove->id);
				ptr = client_list;
			}
			else if (ret > 0)
			{
				// Read message from client buffer
				while ((ret = extract_message(&(ptr->buf), &msg)) == 1)
				{

					my_printf("Extracting message\n", 1);

					char *str = malloc(sizeof(char) * (strlen(msg) + 100)); // +100 pour ajouter le préfixe 'client %d: ...'
					if (str == NULL)
						fatal();
					sprintf(str, "client %d: %s", ptr->id, msg);
					broadcast_message(str, ptr);
					free(msg);
					free(str);
				}
				if (ret == -1)
					fatal();
			}
		}
		if (ptr)
			ptr = ptr->next;
	}
}

int main(int ac, char **av)
{

	// Check if arguments are given
	if (ac < 2)
	{
		my_printf("Wrong number of arguments\n", 2);
		return (1);
	}
	int port = atoi(av[1]);

	int connfd, maxfd;
	struct sockaddr_in servaddr;

	// socket create and verification
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd == -1) // socket creation failed
		fatal();

	// assign IP and PORT
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(2130706433); // 127.0.0.1
	servaddr.sin_port = htons(port);

	// Binding newly created socket to given IP and PORT and verification
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		fatal();

	// Socket start listening ofr maximum 10 incoming connection requests
	if (listen(sockfd, 10) != 0)
		fatal();

	// Initialize the set of fd to listen to, adding the listening socket that awaits for incoming connection requests
	FD_ZERO(&fd_all);
	FD_SET(sockfd, &fd_all);
	maxfd = sockfd;

	while (1)
	{
		fd_read = fd_all;
		fd_write = fd_all;
		maxfd = find_maxfd(sockfd);

		if (select(maxfd + 1, &fd_read, &fd_write, NULL, NULL) < 0) // If there is an error, just continue without reading fd that time.
			continue;
		else if (FD_ISSET(sockfd, &fd_read)) // If there is a connection attempt
		{

			connfd = accept(sockfd, NULL, NULL);
			if (connfd >= 0)
			{
				fcntl(connfd, F_SETFL, O_NONBLOCK);
				add_client(connfd);
			}
		}
		else // Clients may be talking
		{
			receive_clients();
		}
	}

	clean_all();
	return (0);
}
