// Circular Queue with Mutex and Atomic Operations

/*
clear && wrk -t16 -c512 -d60s http://127.0.0.1:8080

Running 1m test @ http://127.0.0.1:8080
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.76ms    1.15ms  17.82ms   71.10%
    Req/Sec     8.53k   612.66    10.46k    68.76%
  8154737 requests in 1.00m, 0.90GB read
Requests/sec: 135678.87
Transfer/sec:     15.27MB
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>

#define PORT 8080
#define INITIAL_THREAD_POOL_SIZE 8
#define MAX_THREAD_POOL_SIZE 16
#define MAX_CONNECTION_SIZE 512
#define MAX_WAITING_QUEUE_SIZE 1024
#define BACKLOG 512
#define BUFFER_SIZE 140
#define RESPONSE_BODY "{\"message\": \"Hello, world!\"}"
#define RESPONSE_BODY_LENGTH (sizeof(RESPONSE_BODY) - 1)

int server_fd;
pthread_t *thread_ids;
pthread_mutex_t lock;
int client_queue[MAX_WAITING_QUEUE_SIZE];
int queue_front = 0, queue_rear = 0;
atomic_int queue_size = 0;  // Use atomic int for queue size
atomic_int has_clients = 0; // Flag to indicate if there are clients
atomic_int current_thread_pool_size = INITIAL_THREAD_POOL_SIZE;

// Function to enqueue a client connection
void enqueue_client(int client_fd)
{
    pthread_mutex_lock(&lock);
    if (atomic_load(&queue_size) < MAX_WAITING_QUEUE_SIZE)
    {
        client_queue[queue_rear] = client_fd;
        queue_rear = (queue_rear + 1) % MAX_WAITING_QUEUE_SIZE;
        atomic_fetch_add(&queue_size, 1);
        atomic_store(&has_clients, 1); // Set flag indicating clients are present
    }
    else
    {
        close(client_fd); // Reject connection if queue is full
    }
    pthread_mutex_unlock(&lock);
}

// Function to dequeue a client connection
int dequeue_client()
{
    int client_fd;

    while (1)
    {
        pthread_mutex_lock(&lock);
        if (atomic_load(&queue_size) > 0)
        {
            client_fd = client_queue[queue_front];
            queue_front = (queue_front + 1) % MAX_WAITING_QUEUE_SIZE;
            atomic_fetch_sub(&queue_size, 1);
            if (atomic_load(&queue_size) == 0)
            {
                atomic_store(&has_clients, 0); // Clear flag if no clients
            }
            pthread_mutex_unlock(&lock);
            break;
        }
        pthread_mutex_unlock(&lock);
        usleep(100); // Reduce busy-waiting
    }
    return client_fd;
}

// Worker thread function
void *worker_thread(void *arg)
{
    while (1)
    {
        int client_fd = dequeue_client();
        char buffer[BUFFER_SIZE] = {0};

        // Read request
        if (read(client_fd, buffer, sizeof(buffer) - 1) < 0)
        {
            perror("read failed");
            close(client_fd);
            continue;
        }

        // Write and send response
        char response[BUFFER_SIZE];
        int response_length = snprintf(response, sizeof(response),
                                       "HTTP/1.1 200 OK\r\n"
                                       "Content-Type: application/json\r\n"
                                       "Content-Length: %zu\r\n"
                                       "Connection: close\r\n\r\n"
                                       "%s",
                                       RESPONSE_BODY_LENGTH, RESPONSE_BODY);

        if (write(client_fd, response, response_length) < 0)
        {
            perror("write failed");
        }
        close(client_fd);
    }
    return NULL;
}

void adjust_thread_pool_size()
{
    int queue_len = atomic_load(&queue_size);
    int pool_size = atomic_load(&current_thread_pool_size);

    if (queue_len > (MAX_WAITING_QUEUE_SIZE / 2) && pool_size < MAX_THREAD_POOL_SIZE)
    {
        // printf("Increasing thread pool size\n");
        int new_size = pool_size + 2;
        new_size = new_size > MAX_THREAD_POOL_SIZE ? MAX_THREAD_POOL_SIZE : new_size;
        for (int i = pool_size; i < new_size; i++)
        {
            pthread_create(&thread_ids[i], NULL, worker_thread, NULL);
        }
        atomic_store(&current_thread_pool_size, new_size);
    }
    else if (queue_len < (MAX_WAITING_QUEUE_SIZE / 4) && pool_size > INITIAL_THREAD_POOL_SIZE)
    {
        // printf("Decreasing thread pool size\n");
        int new_size = pool_size - 2;
        new_size = new_size < INITIAL_THREAD_POOL_SIZE ? INITIAL_THREAD_POOL_SIZE : new_size;
        atomic_store(&current_thread_pool_size, new_size);
    }
}

void *monitor_thread(void *arg)
{
    while (1)
    {
        adjust_thread_pool_size();
        sleep(1);
    }
    return NULL;
}

int main()
{
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    thread_ids = malloc(MAX_THREAD_POOL_SIZE * sizeof(pthread_t));
    pthread_t monitor_tid;
    atomic_int stop_flag = 0; // Flag to signal threads to stop

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
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

    // Bind server socket
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Initialize mutex
    pthread_mutex_init(&lock, NULL);

    // Create worker threads
    for (int i = 0; i < INITIAL_THREAD_POOL_SIZE; i++)
    {
        pthread_create(&thread_ids[i], NULL, worker_thread, &stop_flag);
    }
    pthread_create(&monitor_tid, NULL, monitor_thread, &stop_flag);

    while (1)
    {
        // Accept new connection
        int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_fd < 0)
        {
            perror("accept failed");
            continue;
        }

        // Enqueue the client connection
        enqueue_client(client_fd);
    }

    // Signal threads to stop
    atomic_store(&stop_flag, 1);

    // Cancel and detach worker threads
    for (int i = 0; i < atomic_load(&current_thread_pool_size); i++)
    {
        pthread_cancel(thread_ids[i]);
        pthread_detach(thread_ids[i]); // Free the resources of the thread
    }

    // Cancel and detach monitor thread
    pthread_cancel(monitor_tid);
    pthread_detach(monitor_tid); // Free the resources of the thread

    // Cleanup
    pthread_mutex_destroy(&lock);
    close(server_fd);
    free(thread_ids);

    return 0;
}
