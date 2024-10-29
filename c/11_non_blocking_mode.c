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
#define BACKLOG 1024

// Worker thread function
void *worker_thread(void *arg)
{
    int epoll_fd = *(int *)arg;
    struct epoll_event events[BACKLOG];

    while (1)
    {
        int num_events = epoll_wait(epoll_fd, events, BACKLOG, -1);
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
                int bytes_read = recv(events[i].data.fd, buffer, sizeof(buffer) - 1, 0);
                if (bytes_read < 0)
                {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        perror("recv error");
                    }
                    close(events[i].data.fd);
                    continue;
                }
                else if (bytes_read == 0)
                {
                    // Connection closed by the client
                    close(events[i].data.fd);
                    continue;
                }

                // Null-terminate the received data
                buffer[bytes_read] = '\0';
                printf("Received bytes_read: %d\n", bytes_read);
                printf("Received request: %s\n", buffer);

                // Prepare the HTTP response
                char response[BUFFER_SIZE];
                int response_length = snprintf(response, sizeof(response),
                                               "HTTP/1.1 200 OK\r\n"
                                               "Content-Type: application/json\r\n"
                                               "Content-Length: %zu\r\n"
                                               "Connection: close\r\n\r\n"
                                               "%s",
                                               RESPONSE_BODY_LENGTH, RESPONSE_BODY);
                send(events[i].data.fd, response, response_length, 0);

                // Close the client socket after sending the response
                close(events[i].data.fd);
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

    // Listen for incoming connections
    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("Listen failed");
        close(server_fd);
        return -1;
    }

    // Create epoll instance
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

    // Create worker threads
    int num_threads = 16; // Adjust as needed
    pthread_t threads[num_threads];
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
