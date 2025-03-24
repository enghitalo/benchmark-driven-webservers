/*
curl --verbose  http://127.0.0.1:8081

wrk -t16 -c512 -d60s http://127.0.0.1:8081

Running 1m test @ http://127.0.0.1:8081
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.06ms    1.31ms  25.53ms   85.13%
    Req/Sec    33.37k     6.47k   60.49k    79.34%
  31890362 requests in 1.00m, 1.54GB read
Requests/sec: 530632.51
Transfer/sec:     26.31MB
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

void set_blocking(int fd, int blocking)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl F_GETFL failed");
        return;
    }
    if (blocking)
    {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    else
    {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

void close_socket(int fd)
{
    close(fd);
}

int create_server_socket(int port)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

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

int add_fd_to_epoll(int epoll_fd, int fd, uint32_t events)
{
    struct epoll_event ev = {
        .events = events,
        .data.fd = fd};
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }
    return 0;
}

void remove_fd_from_epoll(int epoll_fd, int fd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

void handle_accept_loop(Server *server)
{
    while (1)
    {
        int client_fd = accept(server->socket_fd, NULL, NULL);
        if (client_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            perror("Accept failed");
            return;
        }

        int epoll_fd = server->epoll_fds[client_fd % max_thread_pool_size];
        if (add_fd_to_epoll(epoll_fd, client_fd, EPOLLIN | EPOLLET) == -1)
        {
            close_socket(client_fd);
        }
    }
}

// Define the arg_struct to hold the arguments for the thread function
struct arg_struct
{
    Server *server;
    int epoll_fd;
};

void *process_events(void *arguments)
{
    struct arg_struct *args = (struct arg_struct *)arguments;
    Server *server = args->server;
    int epoll_fd = args->epoll_fd;

    while (1)
    {
        struct epoll_event events[max_connection_size];
        int num_events = epoll_wait(epoll_fd, events, max_connection_size, -1);

        for (int i = 0; i < num_events; i++)
        {
            if (events[i].events & (EPOLLHUP | EPOLLERR))
            {
                remove_fd_from_epoll(epoll_fd, events[i].data.fd);
                continue;
            }

            if (events[i].events & EPOLLIN)
            {
                unsigned char request_buffer[140];
                int bytes_read = recv(events[i].data.fd, request_buffer, 140 - 1, 0);

                if (bytes_read > 0)
                {
                    // Process request and get response
                    // This would call server->request_handler in a real implementation
                    char *response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
                    send(events[i].data.fd, response, strlen(response), 0);
                }
                else if (bytes_read == 0 || (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
                {
                    remove_fd_from_epoll(epoll_fd, events[i].data.fd);
                }
            }
        }
    }
    pthread_exit(NULL);
    return NULL;
}

void server_run(Server *server)
{
    server->socket_fd = create_server_socket(server->port);
    if (server->socket_fd < 0)
    {
        return;
    }

    for (int i = 0; i < max_thread_pool_size; i++)
    {
        server->epoll_fds[i] = epoll_create1(0);
        if (server->epoll_fds[i] < 0)
        {
            perror("epoll_create1 failed");
            close_socket(server->socket_fd);
            exit(1);
        }

        if (add_fd_to_epoll(server->epoll_fds[i], server->socket_fd, EPOLLIN) == -1)
        {
            close_socket(server->socket_fd);
            close_socket(server->epoll_fds[i]);
            return;
        }

        struct arg_struct args;
        args.server = server;
        args.epoll_fd = server->epoll_fds[i];

        if (pthread_create(&(server->threads[i]), NULL, &process_events, (void *)&args) != 0)
        {
            perror("Thread creation failed");
            close_socket(server->socket_fd);
            close_socket(server->epoll_fds[i]);
            exit(EXIT_FAILURE);
        }
    }

    printf("listening on http://localhost:%d/\n", server->port);
    handle_accept_loop(server);
}

int main()
{
    Server server = {
        .port = 8081,
        .socket_fd = -1, // Socket file descriptor initialized to -1 because it is not created yet
        .request_handler = NULL};

    server_run(&server);
    return 0;
}