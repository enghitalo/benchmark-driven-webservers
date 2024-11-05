/*
curl --verbose  http://127.0.0.1:8081

wrk -t16 -c512 -d60s http://127.0.0.1:8081

Running 1m test @ http://127.0.0.1:8081
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.33ms    1.08ms  15.57ms   77.72%
    Req/Sec    25.85k     1.68k   37.86k    72.07%
  24698685 requests in 1.00m, 2.83GB read
Requests/sec: 411388.60
Transfer/sec:     48.26MB
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

// Global mutex for synchronizing access to shared resources
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
void set_blocking(int fd, int blocking);
int create_server_socket();
int add_fd_to_epoll(int epoll_fd, int fd, uint32_t events);
void remove_fd_from_epoll(int epoll_fd, int fd);
void handle_accept(int server_fd, int epoll_fd);
void handle_client_closure(int client_fd);
void process_events(int epoll_fd);
void *worker_thread(void *arg);
void event_loop(int server_fd, int epoll_fd);

// Function to set a socket to non-blocking mode
void set_blocking(int fd, int blocking)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return;

    if (blocking)
    {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    else
    {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

// Function to create and configure the server socket
int create_server_socket()
{
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

    return server_fd;
}

// Function to add a file descriptor to the epoll instance
int add_fd_to_epoll(int epoll_fd, int fd, uint32_t events)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        perror("epoll_ctl: fd");
        return -1;
    }
    return 0;
}

// Function to remove a file descriptor from the epoll instance
void remove_fd_from_epoll(int epoll_fd, int fd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

// Function to handle client acceptance
void handle_accept(int server_fd, int epoll_fd)
{
    while (1)
    {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; // No more connections to accept
            perror("Accept failed");
            return;
        }

        set_blocking(client_fd, 0); // Set client socket to non-blocking

        // Locking to protect the epoll instance
        pthread_mutex_lock(&mutex);
        if (add_fd_to_epoll(epoll_fd, client_fd, EPOLLIN | EPOLLET) == -1)
        {
            close(client_fd);
        }
        pthread_mutex_unlock(&mutex);
    }
}

// Function to handle client closure
void handle_client_closure(int client_fd)
{
    // printf("Closing connection: %d\n", client_fd);
    // Locking to protect the epoll instance
    pthread_mutex_lock(&mutex);
    remove_fd_from_epoll(client_fd, client_fd);
    pthread_mutex_unlock(&mutex);
}

// Function to process events from epoll
void process_events(int epoll_fd)
{
    struct epoll_event events[MAX_CONNECTION_SIZE];
    int num_events = epoll_wait(epoll_fd, events, MAX_CONNECTION_SIZE, -1);

    for (int i = 0; i < num_events; i++)
    {
        if (events[i].events & (EPOLLHUP | EPOLLERR))
        {
            handle_client_closure(events[i].data.fd);
            continue;
        }

        if (events[i].events & EPOLLIN)
        {
            char buffer[BUFFER_SIZE] = {0};
            int bytes_read = recv(events[i].data.fd, buffer, sizeof(buffer) - 1, 0);

            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0'; // Null-terminate the buffer
                char response[BUFFER_SIZE];
                int response_length = snprintf(response, sizeof(response),
                                               "HTTP/1.1 200 OK\r\n"
                                               "Content-Type: application/json\r\n"
                                               "Content-Length: %zu\r\n"
                                               "Connection: keep-alive\r\n\r\n"
                                               "%s",
                                               RESPONSE_BODY_LENGTH, RESPONSE_BODY);

                // Send the response back to the client
                send(events[i].data.fd, response, response_length, 0);

                // Close the connection after sending the response
                handle_client_closure(events[i].data.fd);
            }
            else if (bytes_read == 0 || (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
            {
                handle_client_closure(events[i].data.fd);
            }
        }
    }
}

// Worker thread function
void *worker_thread(void *arg)
{
    int epoll_fd = *(int *)arg;
    while (1)
    {
        process_events(epoll_fd);
    }
    return NULL;
}

// Event loop for accepting clients
void event_loop(int server_fd, int epoll_fd)
{
    while (1)
    {
        handle_accept(server_fd, epoll_fd);
    }
}

int main()
{
    int server_fd = create_server_socket();
    if (server_fd < 0)
    {
        return -1;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        perror("epoll_create1 failed");
        close(server_fd);
        return -1;
    }

    // Locking to protect the epoll instance
    pthread_mutex_lock(&mutex);
    if (add_fd_to_epoll(epoll_fd, server_fd, EPOLLIN) == -1)
    {
        close(server_fd);
        close(epoll_fd);
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    pthread_mutex_unlock(&mutex);

    // Start worker threads
    pthread_t threads[MAX_THREAD_POOL_SIZE];
    for (int i = 0; i < INITIAL_THREAD_POOL_SIZE; i++)
    {
        pthread_create(&threads[i], NULL, worker_thread, &epoll_fd);
    }

    // Start the event loop for accepting clients
    event_loop(server_fd, epoll_fd);

    // Cleanup
    for (int i = 0; i < INITIAL_THREAD_POOL_SIZE; i++)
    {
        pthread_cancel(threads[i]);
        pthread_join(threads[i], NULL);
    }
    pthread_mutex_destroy(&mutex);
    close(epoll_fd);
    close(server_fd);
    return 0;
}
