/*
curl --verbose  http://127.0.0.1:8081

wrk -H 'Connection: "keep-alive"' --connections 512 --threads 16 --duration 10s --timeout 1 http://localhost:8081/

Running 10s test @ http://localhost:8081/
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.24ms    1.41ms  25.44ms   85.16%
    Req/Sec    31.61k     5.03k   51.26k    72.81%
  5059127 requests in 10.09s, 366.68MB read
Requests/sec: 501562.70
Transfer/sec:     36.35MB
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#define max_connection_size 1024
#define max_thread_pool_size 8

const unsigned char tiny_bad_request_response[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

typedef struct
{
    int port;
    int socket_fd;
    int epoll_fds[max_thread_pool_size];
    pthread_t threads[max_thread_pool_size];
    void *(*request_handler)(void *);
} Server;

struct arg_struct
{
    Server *server;
    int epoll_fd;
};

/**
 * Sets a file descriptor to non-blocking mode.
 * @param fd The file descriptor to modify.
 */
void set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl F_GETFL failed");
        return;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl F_SETFL failed");
    }
}

/**
 * Closes a socket file descriptor.
 * @param fd The file descriptor to close.
 */
void close_socket(int fd)
{
    close(fd);
}

/**
 * Creates and configures a server socket.
 * @param port The port number to bind to.
 * @return The server socket file descriptor, or -1 on failure.
 */
int create_server_socket(int port)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    set_non_blocking(server_fd); // Set server socket to non-blocking

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt SO_REUSEPORT failed");
        close_socket(server_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = {.s_addr = INADDR_ANY},
        .sin_zero = {0}};

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close_socket(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, max_connection_size) < 0)
    {
        perror("Listen failed");
        close_socket(server_fd);
        exit(EXIT_FAILURE);
    }
    return server_fd;
}

/**
 * Adds a file descriptor to an epoll instance.
 * @param epoll_fd The epoll file descriptor.
 * @param fd The file descriptor to add.
 * @param events The events to monitor (e.g., EPOLLIN).
 * @return 0 on success, -1 on failure.
 */
int add_fd_to_epoll(int epoll_fd, int fd, uint32_t events)
{
    struct epoll_event ev = {
        .events = events,
        .data.fd = fd};
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        perror("epoll_ctl");
        return -1;
    }
    return 0;
}

/**
 * Removes a file descriptor from an epoll instance.
 * @param epoll_fd The epoll file descriptor.
 * @param fd The file descriptor to remove.
 */
void remove_fd_from_epoll(int epoll_fd, int fd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

/**
 * Handles accepting new client connections in the main thread.
 * @param server Pointer to the Server struct.
 * @param main_epoll_fd The epoll instance monitoring the server socket.
 */
void handle_accept_loop(Server *server, int main_epoll_fd)
{
    static int next_worker = 0;
    struct epoll_event events[1];

    while (1)
    {
        int n = epoll_wait(main_epoll_fd, events, 1, -1);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++)
        {
            if (events[i].events & EPOLLIN)
            {
                while (1)
                {
                    int client_fd = accept(server->socket_fd, NULL, NULL);
                    if (client_fd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept");
                        continue;
                    }
                    set_non_blocking(client_fd); // Set client socket to non-blocking
                    int epoll_fd = server->epoll_fds[next_worker];
                    next_worker = (next_worker + 1) % max_thread_pool_size;
                    if (add_fd_to_epoll(epoll_fd, client_fd, EPOLLIN | EPOLLET) < 0)
                    {
                        close_socket(client_fd);
                    }
                }
            }
        }
    }
}

/**
 * Worker thread function to process client events.
 * @param arguments Pointer to the arg_struct containing server and epoll_fd.
 * @return NULL (thread runs indefinitely).
 */
void *process_events(void *arguments)
{
    struct arg_struct *args = (struct arg_struct *)arguments;
    Server *server = args->server;
    int epoll_fd = args->epoll_fd;

    struct epoll_event events[max_connection_size];
    while (1)
    {
        int num_events = epoll_wait(epoll_fd, events, max_connection_size, -1);
        if (num_events < 0)
        {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < num_events; i++)
        {
            int fd = events[i].data.fd;
            if (events[i].events & (EPOLLHUP | EPOLLERR))
            {
                remove_fd_from_epoll(epoll_fd, fd);
                close_socket(fd);
                continue;
            }

            if (events[i].events & EPOLLIN)
            {
                char request_buffer[1024];
                int bytes_read = recv(fd, request_buffer, sizeof(request_buffer), 0);
                if (bytes_read <= 0)
                {
                    // free(request_buffer);
                    if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                    {
                        remove_fd_from_epoll(epoll_fd, fd);
                        close_socket(fd);
                    }
                    continue;
                }

                const char *response;
                response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\nConnection: keep-alive\r\n\r\nHello, World!";

                ssize_t sent = send(fd, response, strlen(response), MSG_NOSIGNAL);
                if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    remove_fd_from_epoll(epoll_fd, fd);
                    close_socket(fd);
                }

            }
        }
    }
    pthread_exit(NULL);
    return NULL;
}

/**
 * Initializes and runs the server.
 * @param server Pointer to the Server struct.
 */
void server_run(Server *server)
{
    server->socket_fd = create_server_socket(server->port);
    if (server->socket_fd < 0)
    {
        return;
    }

    int main_epoll_fd = epoll_create1(0);
    if (main_epoll_fd < 0)
    {
        perror("epoll_create1");
        close_socket(server->socket_fd);
        exit(EXIT_FAILURE);
    }

    if (add_fd_to_epoll(main_epoll_fd, server->socket_fd, EPOLLIN) < 0)
    {
        close_socket(server->socket_fd);
        close_socket(main_epoll_fd);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < max_thread_pool_size; i++)
    {
        server->epoll_fds[i] = epoll_create1(0);
        if (server->epoll_fds[i] < 0)
        {
            perror("epoll_create1");
            for (int j = 0; j < i; j++)
            {
                close_socket(server->epoll_fds[j]);
            }
            close_socket(main_epoll_fd);
            close_socket(server->socket_fd);
            exit(EXIT_FAILURE);
        }

        struct arg_struct *args = malloc(sizeof(struct arg_struct));
        if (!args)
        {
            perror("malloc");
            for (int j = 0; j <= i; j++)
            {
                close_socket(server->epoll_fds[j]);
            }
            close_socket(main_epoll_fd);
            close_socket(server->socket_fd);
            exit(EXIT_FAILURE);
        }
        args->server = server;
        args->epoll_fd = server->epoll_fds[i];

        if (pthread_create(&(server->threads[i]), NULL, process_events, args) != 0)
        {
            perror("pthread_create");
            free(args);
            for (int j = 0; j <= i; j++)
            {
                close_socket(server->epoll_fds[j]);
            }
            close_socket(main_epoll_fd);
            close_socket(server->socket_fd);
            exit(EXIT_FAILURE);
        }
    }

    printf("listening on http://localhost:%d/\n", server->port);
    handle_accept_loop(server, main_epoll_fd);
}

/**
 * Main entry point of the program.
 * @return Exit status.
 */
int main()
{
    Server server = {
        .port = 8081,
        .socket_fd = -1,
        .request_handler = NULL};
    server_run(&server);
    return 0;
}