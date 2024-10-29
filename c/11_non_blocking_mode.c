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
#include <sys/select.h>

#define MAX_CLIENTS 512
#define QUEUE_SIZE 512
#define PORT 8081
#define BUFFER_SIZE 140
#define RESPONSE_BODY "{\"message\": \"Hello, world!\"}"
#define RESPONSE_BODY_LENGTH (sizeof(RESPONSE_BODY) - 1)

// Client connection queue
int client_queue[QUEUE_SIZE];
int front = 0, rear = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

// Function to enqueue a client connection
void enqueue_client(int client_fd)
{
    pthread_mutex_lock(&queue_mutex);
    while ((rear + 1) % QUEUE_SIZE == front)
    { // Queue is full
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    client_queue[rear] = client_fd;
    rear = (rear + 1) % QUEUE_SIZE;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}

// Function to dequeue a client connection
int dequeue_client()
{
    pthread_mutex_lock(&queue_mutex);
    while (front == rear)
    { // Queue is empty
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    int client_fd = client_queue[front];
    front = (front + 1) % QUEUE_SIZE;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
    return client_fd;
}

// Worker thread function
void *worker_thread(void *arg)
{
    while (1)
    {
        int client_fd = dequeue_client();

        // Handle client connection (non-blocking)
        char buffer[BUFFER_SIZE] = {0};
        int bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);

        if (bytes_read > 0)
        {
            // Process the received data
            char response[BUFFER_SIZE];
            int response_length = snprintf(response, sizeof(response),
                                           "HTTP/1.1 200 OK\r\n"
                                           "Content-Type: application/json\r\n"
                                           "Content-Length: %zu\r\n"
                                           "Connection: close\r\n\r\n"
                                           "%s",
                                           RESPONSE_BODY_LENGTH, RESPONSE_BODY);

            send(client_fd, response, response_length, 0);
            close(client_fd);
        }
        else if (bytes_read == 0 || (bytes_read < 0 && errno != EAGAIN))
        {
            // Connection closed or error
            close(client_fd);
        }
    }
    return NULL;
}

// Function to adjust thread pool size
void adjust_thread_pool_size()
{
    // Example: dynamically adjust thread pool based on load
}

// Monitor thread function
void *monitor_thread(void *arg)
{
    while (1)
    {
        // Implement logic to monitor server state and adjust thread pool size
        adjust_thread_pool_size();
        sleep(5); // Adjust as necessary
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
    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("Listen failed");
        close(server_fd);
        return -1;
    }

    // Create worker threads
    int num_threads = 4; // Adjust as needed
    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; i++)
    {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    // Create monitor thread
    pthread_t monitor;
    pthread_create(&monitor, NULL, monitor_thread, NULL);

    // Main loop to accept clients
    fd_set read_fds;
    int max_fd = server_fd;

    while (1)
    {
        // Clear the set and add the server socket
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        // Wait for an activity on one of the sockets
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            break;
        }

        // Check if there is a new connection on the server socket
        if (FD_ISSET(server_fd, &read_fds))
        {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd >= 0)
            {
                fcntl(client_fd, F_SETFL, O_NONBLOCK); // Set client socket to non-blocking
                enqueue_client(client_fd);
                if (client_fd > max_fd)
                {
                    max_fd = client_fd;
                }
            }
            else if (errno != EWOULDBLOCK)
            {
                perror("Accept failed");
            }
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
