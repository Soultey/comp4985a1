#include <arpa/inet.h>
// #include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// #include <ndbm.h> database header

#define BUFFER_SIZE 1024
#define TEN 10
#define FIVE 5

#ifndef SOCK_CLOEXEC
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-macros"
    #define SOCK_CLOEXEC 0
    #pragma GCC diagnostic pop
#endif

void handle_client(int client_socket);
void start_server(const char *server_ip, int server_port) __attribute__((noreturn));

void start_server(const char *server_ip, int server_port)
{
    long               port = server_port;
    int                server_socket;
    struct sockaddr_in server_address;

    // Create a socket for the server
    server_socket = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(server_socket == -1)
    {
        perror("Server socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Initialize server_address struct
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family      = AF_INET;
    server_address.sin_port        = htons((uint16_t)port);
    server_address.sin_addr.s_addr = inet_addr(server_ip);

    // Bind the server socket to the server_address
    if(bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if(listen(server_socket, FIVE) == -1)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %ld...\n", port);

    while(1)
    {
        int                client_socket;
        struct sockaddr_in client_address;
        socklen_t          client_address_len = sizeof(client_address);

        // Accept a client connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len);
        if(client_socket == -1)
        {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        printf("Client connected\n");

        handle_client(client_socket);
    }
}

void handle_client(int client_socket)
{
    const char *response = "HTTP/1.0 200 OK\r\nContent-Length: 12\r\n\r\nHello, World!";
    ssize_t     bytes_sent;

    // Receive data from client
    char    buffer[BUFFER_SIZE];
    ssize_t bytes_received = read(client_socket, buffer, sizeof(buffer) - 1);

    if(bytes_received > 0)
    {
        buffer[bytes_received] = '\0';    // Null-terminate the string
    }
    else
    {
        // Handle read error or closed socket
        printf("Nothing received from client\n");
        close(client_socket);
        return;
    }

    // Print the received HTTP request
    printf("Received HTTP request:\n%s\n", buffer);

    // Respond to the client
    bytes_sent = write(client_socket, response, strlen(response));
    if(bytes_sent == -1)
    {
        perror("Error sending response to client");
    }

    // Close the client socket
    close(client_socket);
}

int main(int argc, const char *argv[])
{
    long        port;
    const char *server_ip;

    if(argc != 3)
    {
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    server_ip = argv[1];
    port      = strtol(argv[2], NULL, TEN);

    start_server(server_ip, (int)port);
}
