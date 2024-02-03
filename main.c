#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// #include <ndbm.h> database header

#define BUFFER_SIZE 1024
#define TEN 10
#define BIG 655356
#define FIVE 5
#define URI 255

#ifndef SOCK_CLOEXEC
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-macros"
    #define SOCK_CLOEXEC 0
    #pragma GCC diagnostic pop
#endif

void handle_client(int client_socket);

int main(int argc, const char *argv[])
{
    long               port;
    int                server_socket;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t          client_address_len;

    char buffer[BUFFER_SIZE];

    if(argc != 3)
    {
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Convert the string to a long using strtol
    errno = 0;    // Initialize errno before the call
    port  = strtol(argv[2], NULL, TEN);

    // range check to make sure port within bounds
    if(errno != 0 || port < 0 || port > BIG)
    {
        perror("Invalid port number");
        exit(EXIT_FAILURE);
    }

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
    server_address.sin_addr.s_addr = INADDR_ANY;

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
        int     client_socket;
        ssize_t bytes_received;

        // Parse HTTP request
        char method[TEN];
        char uri[URI];
        char http_version[TEN];

        // Accept a client connection
        client_address_len = sizeof(client_address);
        client_socket      = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len);
        if(client_socket == -1)
        {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        printf("Client connected\n");

        // Receive data from client
        bytes_received = read(client_socket, buffer, sizeof(buffer) - 1);

        if(bytes_received > 0)
        {
            buffer[bytes_received] = '\0';    // Null-terminate the string
        }
        else
        {
            // Handle read error or closed socket
            printf("Nothing received from client: %.*s\n", (int)bytes_received, buffer);
            exit(EXIT_FAILURE);
        }

        // Print the received HTTP request
        printf("Received HTTP request:\n%s\n", buffer);

        sscanf(buffer, "%9s %254s %9s", method, uri, http_version);

        // Handle different HTTP methods
        if(strcmp(method, "GET") == 0)
        {
            const char *html_response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html><body><h1>Hello, World!</h1></body></html>";
            write(client_socket, html_response, strlen(html_response));
            // Handle GET requests
            printf("Received GET request for URI: %s\n", uri);
            // TODO: Implement handling of GET requests
            // You need to check the requested resource (URI) and send an appropriate response.
        }
        //        else if(strcmp(method, "HEAD") == 0)
        //        {
        //            // Handle HEAD requests
        //            // TODO: Implement handling of HEAD requestsasdfasdfasdf
        //        }
        //        else if(strcmp(method, "POST") == 0)
        //        {
        //            // Handle POST requests
        //            // TODO: Implement handling of POST requestsasdfasdfad
        //        }
        else
        {
            // Unsupported HTTP method, send a 501 Not Implemented response
            const char *not_implemented_response = "HTTP/1.0 501 Not Implemented\r\n\r\n";
            write(client_socket, not_implemented_response, strlen(not_implemented_response));
        }

        // TODO: Implement NDBM storage with POST requests

        // Respond to the client
        // TODO: Send appropriate responses based on the HTTP method

        // Close the client socket
        close(client_socket);
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

    // TODO: Implement parsing of HTTP requests and generating appropriate responses

    // TODO: Implement handling of GET, HEAD, and POST requests

    // TODO: Implement NDBM storage with POST requests

    // Respond to the client
    bytes_sent = write(client_socket, response, strlen(response));
    if(bytes_sent == -1)
    {
        perror("Error sending response to client");
    }

    // Close the client socket
    close(client_socket);
}
