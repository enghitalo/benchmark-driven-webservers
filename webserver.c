#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>

// wrk -H 'Connection: "keep-alive"' --connections 512 --threads 16 --duration 20 --timeout 1 http://localhost:8080/
#define PORT 8080
#define SERVER "127.0.0.1"

// Flag to indicate if program termination is initiated by Ctrl+C
volatile sig_atomic_t sigint_received = 0;

void handle_request(int client_socket_fd)
{
    char response[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, World!\r\n";
    send(client_socket_fd, response, sizeof(response), 0);
}

int main()
{
    int server_socket_fd, client_socket_fd;
    struct sockaddr_in server_address, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Create socket
    if ((server_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Set up server address struct
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    // server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_addr.s_addr = inet_addr(SERVER);

    // Bind the socket to the specified address and port
    if (bind(server_socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1)
    {
        perror("Error binding socket at address and port");
        close(server_socket_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket_fd, SOMAXCONN) == -1)
    {
        perror("Error listening/waiting for connections");
        close(server_socket_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    // Loop to keep connection open
    while (1)
    {
        // Accept a connection
        if ((client_socket_fd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1)
        {
            perror("Error accepting connection");
            close(server_socket_fd);
            exit(EXIT_FAILURE);
            // continue;
        }

        // printf("Connection accepted from %s\n", inet_ntoa(client_addr.sin_addr));

        // Handle the HTTP request
        handle_request(client_socket_fd);

        // Close the client socket
        close(client_socket_fd);
    }

    // Close the server socket
    close(server_socket_fd);

    return 0;
}
