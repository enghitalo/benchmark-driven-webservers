// threaded_http_server.c since it describes the functionality of the code, which is a multi-threaded HTTP server.

/*
clear && wrk -t16 -c512 -d30s http://127.0.0.1:8080

Running 30s test @ http://127.0.0.1:8080
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.95ms   17.85ms 417.64ms   98.09%
    Req/Sec     8.25k     1.96k   12.94k    69.18%
  3937570 requests in 30.10s, 443.11MB read
Requests/sec: 130817.20
Transfer/sec:     14.72MB
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>

#define PORT 8080
#define BUFFER_SIZE 140
#define RESPONSE_BODY "{\"message\": \"Hello, world!\"}"
#define RESPONSE_BODY_LENGTH (sizeof(RESPONSE_BODY) - 1)
#define THREAD_POOL_SIZE 16
#define QUEUE_SIZE 512

typedef struct
{
    int client_fd;
} client_data_t;

client_data_t *queue[QUEUE_SIZE];
int queue_start = 0;
int queue_end = 0;
sem_t queue_semaphore;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client(void *arg)
{
    while (1)
    {
        sem_wait(&queue_semaphore);
        pthread_mutex_lock(&queue_mutex);
        client_data_t *client_data = queue[queue_start];
        queue_start = (queue_start + 1) % QUEUE_SIZE;
        pthread_mutex_unlock(&queue_mutex);

        char buffer[BUFFER_SIZE];
        ssize_t bytes_read = read(client_data->client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read < 0)
        {
            perror("read failed");
            close(client_data->client_fd);
            free(client_data);
            continue;
        }
        buffer[bytes_read] = '\0'; // Null-terminate the buffer

        // printf("Received request:\n%s\n", buffer);

        char response[BUFFER_SIZE];
        int response_length = snprintf(response, sizeof(response),
                                       "HTTP/1.1 200 OK\r\n"
                                       "Content-Type: application/json\r\n"
                                       "Content-Length: %zu\r\n"
                                       "Connection: close\r\n\r\n"
                                       "%s",
                                       RESPONSE_BODY_LENGTH, RESPONSE_BODY);

        write(client_data->client_fd, response, response_length);
        close(client_data->client_fd);
        free(client_data);
    }
    return NULL;
}

int main()
{
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    sem_init(&queue_semaphore, 0, 0);

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Bind and listen
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 128) < 0)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    pthread_t thread_pool[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
    {
        pthread_create(&thread_pool[i], NULL, handle_client, NULL);
    }

    while (1)
    {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        // int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
        {
            perror("accept failed");
            continue;
        }

        client_data_t *client_data = malloc(sizeof(client_data_t));
        client_data->client_fd = client_fd;

        pthread_mutex_lock(&queue_mutex);
        queue[queue_end] = client_data;
        queue_end = (queue_end + 1) % QUEUE_SIZE;
        pthread_mutex_unlock(&queue_mutex);
        sem_post(&queue_semaphore);
    }

    close(server_fd);
    return 0;
}
