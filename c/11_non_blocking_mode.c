/*
wrk -t16 -c512 -d10s http://127.0.0.1:8081

Running 10s test @ http://127.0.0.1:8081
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     8.58ms   13.28ms 147.27ms   88.40%
    Req/Sec     1.35k     2.01k    9.69k    85.17%
  201931 requests in 10.12s, 22.72MB read
  Socket errors: connect 0, read 1137, write 0, timeout 0
Requests/sec:  19961.76
Transfer/sec:      2.25MB
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
#include <arpa/inet.h>
#include <sys/epoll.h>

#define PORT 8081
#define BUFFER_SIZE 140
#define RESPONSE_BODY "{\"message\": \"Hello, world!\"}"
#define RESPONSE_BODY_LENGTH (sizeof(RESPONSE_BODY) - 1)
#define MAX_CONNECTION_SIZE 512
#define INITIAL_THREAD_POOL_SIZE 8
#define MAX_THREAD_POOL_SIZE 16

// Worker thread function
void *worker_thread(void *arg)
{
    int epoll_fd = *(int *)arg;
    struct epoll_event events[MAX_CONNECTION_SIZE];

    while (1)
    {
        int num_events;

        num_events = epoll_wait(epoll_fd, events, MAX_CONNECTION_SIZE, -1);

        for (int i = 0; i < num_events; i++)
        {
            if (events[i].events & (EPOLLHUP | EPOLLERR))
            {
                close(events[i].data.fd);
                continue;
            }

            if (events[i].events & EPOLLIN)
            {
                char buffer[BUFFER_SIZE] = {0};
                int bytes_read;

                while ((bytes_read = recv(events[i].data.fd, buffer, sizeof(buffer) - 1, 0)) > 0)
                {
                    buffer[bytes_read] = '\0';

                    char response[BUFFER_SIZE];
                    int response_length = snprintf(response, sizeof(response),
                                                   "HTTP/1.1 200 OK\r\n"
                                                   "Content-Type: application/json\r\n"
                                                   "Content-Length: %zu\r\n"
                                                   "Connection: close\r\n\r\n"
                                                   "%s",
                                                   RESPONSE_BODY_LENGTH, RESPONSE_BODY);
                    send(events[i].data.fd, response, response_length, 0);
                }

                if (bytes_read < 0)
                {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        continue;
                    }
                }
                else if (bytes_read == 0)
                {
                    close(events[i].data.fd);
                    continue;
                }
            }
        }
    }
    return NULL;
}

int main()
{
    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd < 0)
    {
        perror("Socket creation failed");
        return -1;
    }

    // Bind to the specified port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, MAX_CONNECTION_SIZE) < 0)
    {
        perror("Listen failed");
        close(server_fd);
        return -1;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        perror("epoll_create1 failed");
        close(server_fd);
        return -1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    int num_threads = INITIAL_THREAD_POOL_SIZE;
    pthread_t threads[MAX_THREAD_POOL_SIZE];
    for (int i = 0; i < num_threads; i++)
    {
        pthread_create(&threads[i], NULL, worker_thread, &epoll_fd);
    }

    // Main loop to accept clients
    while (1)
    {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd >= 0)
        {
            fcntl(client_fd, F_SETFL, O_NONBLOCK); // Set client socket to non-blocking
            ev.events = EPOLLIN | EPOLLET;         // Edge-triggered
            ev.data.fd = client_fd;
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
        }
        else if (errno != EWOULDBLOCK)
        {
            perror("Accept failed");
            break;
        }
    }

    // Cleanup (this code will likely never be reached in the current form)
    for (int i = 0; i < num_threads; i++)
    {
        pthread_cancel(threads[i]);
    }
    close(server_fd);
    return 0;
}
