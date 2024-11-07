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
#include <signal.h>

#define PORT 8082
#define BUFFER_SIZE 140
#define RESPONSE_BODY "{\"message\": \"Hello, world!\"}"
#define RESPONSE_BODY_LENGTH (sizeof(RESPONSE_BODY) - 1)
#define MAX_CONNECTION_SIZE 512
#define MAX_THREAD_POOL_SIZE 16

// Struct to hold server information
struct server
{
    int server_socket;
    int epoll_fd;
    pthread_mutex_t lock_flag;
    int has_clients;
    pthread_t threads[MAX_THREAD_POOL_SIZE];
};

// Function prototypes
void set_blocking(int fd, int blocking);
int create_server_socket();
int add_fd_to_epoll(int epoll_fd, int fd, uint32_t events);
void remove_fd_from_epoll(int epoll_fd, int fd);
void handle_accept(struct server *srv);
void handle_client_closure(struct server *srv, int client_fd);
void process_events(struct server *srv);
void *worker_thread(void *arg);
void event_loop(struct server *srv);
void cleanup(int signum, struct server *srv);

// Função para configurar o modo não-bloqueante de um socket
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

    // Enable SO_REUSEPORT option
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt(SO_REUSEPORT) failed");
        close(server_fd);
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

// Adicionar o descritor de arquivo ao epoll
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

// Remover descritor do epoll
void remove_fd_from_epoll(int epoll_fd, int fd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

// Função para aceitar conexões de clientes
void handle_accept(struct server *srv)
{
    while (1)
    {
        //   event_loop(srv.server_socket, srv.epoll_fd);
        int client_fd = accept(srv->server_socket, NULL, NULL);
        if (client_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break; // No more connections to accept
            }
            perror("Accept failed");
            return;
        }

        set_blocking(client_fd, 0); // Set client socket to non-blocking

        // Locking to protect the epoll instance
        pthread_mutex_lock(&srv->lock_flag);
        if (add_fd_to_epoll(srv->epoll_fd, client_fd, EPOLLIN | EPOLLET) == -1)
        {
            close(client_fd);
        }
        pthread_mutex_unlock(&srv->lock_flag); // Release the lock_flag
    }
}

// Function to handle client closure
void handle_client_closure(struct server *srv, int client_fd)
{
    // printf("Closing connection: %d\n", client_fd);
    // Locking to protect the epoll instance
    pthread_mutex_lock(&srv->lock_flag);
    remove_fd_from_epoll(client_fd, client_fd);
    // close(client_fd);
    pthread_mutex_unlock(&srv->lock_flag); // Release the lock_flag
}

// Processar eventos com epoll
void process_events(struct server *srv)
{
    struct epoll_event events[MAX_CONNECTION_SIZE];
    int num_events = epoll_wait(srv->epoll_fd, events, MAX_CONNECTION_SIZE, -1);

    for (int i = 0; i < num_events; i++)
    {
        if (events[i].events & (EPOLLHUP | EPOLLERR))
        {
            handle_client_closure(srv, events[i].data.fd);
            continue;
        }

        if (events[i].events & EPOLLIN)
        {
            char *request_buffer = malloc(BUFFER_SIZE);
            if (!request_buffer)
            {
                perror("malloc failed");
                continue;
            }
            memset(request_buffer, 0, BUFFER_SIZE);
            int bytes_read = recv(events[i].data.fd, request_buffer, BUFFER_SIZE - 1, 0);

            if (bytes_read > 0)
            {
                request_buffer[bytes_read] = '\0'; // Null-terminate the request_buffer
                char *response_buffer = malloc(BUFFER_SIZE);
                if (!response_buffer)
                {
                    perror("malloc failed");
                    free(request_buffer);
                    continue;
                }
                int response_length = snprintf(response_buffer, BUFFER_SIZE,
                                               "HTTP/1.1 200 OK\r\n"
                                               "Content-Type: application/json\r\n"
                                               "Content-Length: %zu\r\n"
                                               "Connection: keep-alive\r\n\r\n"
                                               "%s",
                                               RESPONSE_BODY_LENGTH, RESPONSE_BODY);

                send(events[i].data.fd, response_buffer, response_length, 0);

                // Close the connection after sending the response_buffer
                handle_client_closure(srv, events[i].data.fd);
                free(response_buffer);
            }
            else if (bytes_read == 0 || (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
            {
                handle_client_closure(srv, events[i].data.fd);
            }
            free(request_buffer);
        }
    }
}

// Worker thread function
void *worker_thread(void *arg)
{
    struct server *srv = (struct server *)arg;
    while (1)
    {
        process_events(srv);
    }
    return NULL;
}

// Loop principal para aceitar clientes
void event_loop(struct server *srv)
{
    while (1)
    {
        handle_accept(srv);
    }
}

// // Função de cleanup
// void cleanup(int signum, struct server *srv)
// {
//     printf("Terminating...\n");

//     for (int i = 0; i < MAX_THREAD_POOL_SIZE; i++)
//     {
//         pthread_cancel(srv->threads[i]);
//         pthread_join(srv->threads[i], NULL);
//     }

//     close(srv->epoll_fd);
//     close(srv->server_socket);
//     pthread_mutex_destroy(&srv->lock_flag);
//     exit(0);
// }

int main()
{
    struct server srv;
    memset(&srv, 0, sizeof(srv));

    srv.server_socket = create_server_socket();
    if (srv.server_socket < 0)
    {
        return -1;
    }

    srv.epoll_fd = epoll_create1(0);
    if (srv.epoll_fd < 0)
    {
        perror("epoll_create1 failed");
        close(srv.server_socket);
        return -1;
    }

    // Locking to protect the epoll instance
    pthread_mutex_lock(&srv.lock_flag);
    if (add_fd_to_epoll(srv.epoll_fd, srv.server_socket, EPOLLIN) == -1)
    {
        close(srv.server_socket);
        close(srv.epoll_fd);
        pthread_mutex_unlock(&srv.lock_flag); // Release the lock_flag
        return -1;
    }
    pthread_mutex_unlock(&srv.lock_flag); // Release the lock_flag

    // Initialize the mutex
    pthread_mutex_init(&srv.lock_flag, NULL);

    // Start worker threads
    for (int i = 0; i < MAX_THREAD_POOL_SIZE; i++)
    {
        pthread_create(&srv.threads[i], NULL, worker_thread, &srv);
    }

    // // Configura o manipulador de sinal com acesso à estrutura do servidor
    // struct sigaction sa;
    // sa.sa_handler = (void (*)(int))cleanup;
    // sigemptyset(&sa.sa_mask);
    // sa.sa_flags = 0;
    // sigaction(SIGINT, &sa, NULL);
    // sigaction(SIGTERM, &sa, NULL);

    event_loop(&srv);
    return 0;
}
