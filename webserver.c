#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <signal.h>

#define PORT 8080
#define SERVER "127.0.0.1"
#define BUFFER_SIZE 1024

// Flag to indicate if program termination is initiated by Ctrl+C
volatile sig_atomic_t sigint_received = 0;

// Function to handle Ctrl+C signal
void handle_sigint(int sig)
{
    sigint_received = 1; // Set flag to indicate Ctrl+C received
    printf("\nCaught Ctrl+C. Exiting program...\n");
}

int main()
{
    int server_socket_fd, client_socket_fd, client_address_len;
    struct sockaddr_in server_address, client_address;
    char buffer[BUFFER_SIZE];

    // Criação do socket
    server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd < 0)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Configurando a estrutura do servidor/socket
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    server_address.sin_addr.s_addr = inet_addr(SERVER);
    // server_address.sin_addr.s_addr = INADDR_ANY;

    // Bind do socket ao endereço e porta
    if (bind(server_socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("Erro binding socket at address and port");
        exit(EXIT_FAILURE);
    }

    // Listen para conexões
    //   listen(server_socket_fd, 5);
    if (listen(server_socket_fd, SOMAXCONN) == -1)
    {
        perror("Error waiting for connections");
        exit(EXIT_FAILURE);
    }

    printf("Waiting for conections at %d...\n", PORT);

    // Aceitação de conexão
    client_address_len = sizeof(client_address);
    client_socket_fd = accept(server_socket_fd, (struct sockaddr *)&client_address, &client_address_len);
    if (client_socket_fd < 0)
    {
        perror("Error accepting conection");
        exit(EXIT_FAILURE);
    }

    printf("Client connected: %s\n", inet_ntoa(client_address.sin_addr));

    // Register signal handler for SIGINT (Ctrl+C)
    signal(SIGINT, handle_sigint);

    while (1)
    { // Loop to keep connection open
        // Check if Ctrl+C signal was received
        if (sigint_received == 1)
        {
            break;
        }

        // Receive message from client
        ssize_t received_bytes = recv(client_socket_fd, buffer, sizeof(buffer), 0);
        if (received_bytes <= 0)
        {
            perror("Error receiving message");
            break;
        }

        buffer[received_bytes] = '\0';
        // printf("Client message: %s\n", buffer);

        // Send response to client
        const char *message = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nFoo";
        send(client_socket_fd, message, strlen(message), 0);
    }

    // Close sockets only after loop exits (due to error or Ctrl+C)
    close(client_socket_fd);
    close(server_socket_fd);

    return 0;
}
