// Adaptive Event Handling approach
/*
wrk -t16 -c256 -d10s http://127.0.0.1:8080 

Running 10s test @ http://127.0.0.1:8080
  16 threads and 256 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.66ms   16.02ms 416.36ms   98.37%
    Req/Sec     7.20k     1.58k   10.70k    68.10%
  1149879 requests in 10.09s, 0.00B read
  Socket errors: connect 0, read 1149879, write 0, timeout 0
Requests/sec: 113941.44
Transfer/sec:       0.00B
*/
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdatomic.h>
#include <stdbool.h>

#define RESPONSE "HTTP/1.1 200 OK\r\nDate: Wed, 23 Oct 2024 12:00:00 GMT\r\nContent-Type: application/json\r\nContent-Length: 27\r\nConnection: keep-alive\r\n\r\n{\r\n  \"message\": \"Hello, world!\"\r\n}"
#define RESPONSE_LEN (sizeof(RESPONSE) - 1)

#define INITIAL_THREAD_POOL_SIZE 16
#define MAX_THREAD_POOL_SIZE 32
#define MAX_QUEUE_SIZE 512
#define HIGH_LOAD_THRESHOLD 400
#define LOW_LOAD_THRESHOLD 100

typedef struct
{
    int client_fd;
} Task;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t load_mutex = PTHREAD_MUTEX_INITIALIZER;

Task task_queue[MAX_QUEUE_SIZE];
int queue_front = 0, queue_back = 0;
atomic_int current_load = 0;
atomic_bool server_running = true;

pthread_t thread_pool[MAX_THREAD_POOL_SIZE];
int thread_pool_size = INITIAL_THREAD_POOL_SIZE;

void *worker_thread(void *arg);
void enqueue_task(int client_fd);
Task dequeue_task();
void adjust_thread_pool();

int main()
{
    struct sockaddr_in server_addr;
    int server_fd, client_fd;

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Bind server socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, 128) < 0)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Create worker threads
    for (int i = 0; i < thread_pool_size; i++)
    {
        pthread_create(&thread_pool[i], NULL, worker_thread, NULL);
    }

    while (server_running)
    {
        // Accept new connection
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        // Add the new task to the queue
        enqueue_task(client_fd);
        adjust_thread_pool();
    }

    // Clean up
    close(server_fd);
    server_running = false;
    pthread_cond_broadcast(&queue_cond); // Wake up all threads to exit
    for (int i = 0; i < thread_pool_size; i++)
    {
        pthread_join(thread_pool[i], NULL);
    }

    return 0;
}

void enqueue_task(int client_fd)
{
    pthread_mutex_lock(&queue_mutex);
    // Simple queue management for demonstration, could use ring buffer logic
    if ((queue_back + 1) % MAX_QUEUE_SIZE == queue_front)
    {
        // fprintf(stderr, "Task queue is full!\n");
        close(client_fd);
    }
    else
    {
        task_queue[queue_back].client_fd = client_fd;
        queue_back = (queue_back + 1) % MAX_QUEUE_SIZE;
        atomic_fetch_add(&current_load, 1);
        pthread_cond_signal(&queue_cond);
    }
    pthread_mutex_unlock(&queue_mutex);
}

Task dequeue_task()
{
    pthread_mutex_lock(&queue_mutex);
    while (queue_front == queue_back && server_running)
    {
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }

    Task task = {.client_fd = -1};
    if (queue_front != queue_back)
    {
        task = task_queue[queue_front];
        queue_front = (queue_front + 1) % MAX_QUEUE_SIZE;
        atomic_fetch_sub(&current_load, 1);
    }
    pthread_mutex_unlock(&queue_mutex);
    return task;
}

void *worker_thread(void *arg)
{
    while (server_running)
    {
        Task task = dequeue_task();
        if (task.client_fd == -1)
            continue;

        // Handle connection
        char buffer[1024];
        read(task.client_fd, buffer, sizeof(buffer));

        // Send HTTP response
        write(task.client_fd, RESPONSE, RESPONSE_LEN);

        // Close connection
        close(task.client_fd);
    }
    return NULL;
}

void adjust_thread_pool()
{
    pthread_mutex_lock(&load_mutex);
    if (current_load >= HIGH_LOAD_THRESHOLD && thread_pool_size < MAX_THREAD_POOL_SIZE)
    {
        // Increase thread pool size
        thread_pool_size++;
        pthread_create(&thread_pool[thread_pool_size - 1], NULL, worker_thread, NULL);
    }
    else if (current_load < LOW_LOAD_THRESHOLD && thread_pool_size > INITIAL_THREAD_POOL_SIZE)
    {
        // Decrease thread pool size
        thread_pool_size--;
    }
    pthread_mutex_unlock(&load_mutex);
}
