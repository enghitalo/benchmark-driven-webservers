// Load Balancing approach:

/*
clear && wrk -t16 -c512 -d10s http://127.0.0.1:8080 

Running 10s test @ http://127.0.0.1:8080
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     3.06ms    1.40ms  32.95ms   73.10%
    Req/Sec     7.83k   592.49     9.62k    72.38%
  1254256 requests in 10.08s, 0.00B read
  Socket errors: connect 0, read 1254256, write 0, timeout 0
Requests/sec: 124464.81
Transfer/sec:       0.00B
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BACKLOG 512
#define NUM_WORKERS 16

const char *response = "HTTP/1.1 200 OK\r\n"
                       "Date: Wed, 23 Oct 2024 12:00:00 GMT\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: 27\r\n"
                       "Connection: keep-alive\r\n\r\n"
                       "{\r\n  \"message\": \"Hello, world!\"\r\n}";

int server_fd;
pthread_t thread_ids[NUM_WORKERS];
pthread_mutex_t lock;
pthread_cond_t condition;
int client_queue[BACKLOG];
int queue_front = 0, queue_rear = 0, queue_size = 0;

// Function to enqueue a client connection
void enqueue_client(int client_fd)
{
    pthread_mutex_lock(&lock);
    client_queue[queue_rear] = client_fd;
    queue_rear = (queue_rear + 1) % BACKLOG;
    queue_size++;
    pthread_cond_signal(&condition);
    pthread_mutex_unlock(&lock);
}

// Function to dequeue a client connection
int dequeue_client()
{
    pthread_mutex_lock(&lock);
    while (queue_size == 0)
    {
        pthread_cond_wait(&condition, &lock);
    }
    int client_fd = client_queue[queue_front];
    queue_front = (queue_front + 1) % BACKLOG;
    queue_size--;
    pthread_mutex_unlock(&lock);
    return client_fd;
}

// Worker thread function
void *worker_thread(void *arg)
{
    while (1)
    {
        int client_fd = dequeue_client();
        char buffer[1024] = {0};

        // Read request
        read(client_fd, buffer, 1024);

        // Send response
        write(client_fd, response, strlen(response));

        // Close connection
        close(client_fd);
    }
    return NULL;
}

int main()
{
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
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

    // Initialize mutex and condition variable
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&condition, NULL);

    // Create worker threads
    for (int i = 0; i < NUM_WORKERS; i++)
    {
        pthread_create(&thread_ids[i], NULL, worker_thread, NULL);
    }

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

    // Cleanup
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&condition);
    close(server_fd);

    return 0;
}
