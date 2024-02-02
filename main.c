#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define TEN 10
#define BIG 655356
#define FIVE 5

#ifndef SOCK_CLOEXEC
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-macros"
    #define SOCK_CLOEXEC 0
    #pragma GCC diagnostic pop
#endif

void forker(const char *command, int client_socket);

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
        ssize_t bytes_sent;

        const char *response = "HTTP/1.0 200 OK\r\nContent-Length: 12\r\n\r\nHello, World!";

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
        bytes_received = read(client_socket, buffer,
                              sizeof(buffer) - 1);    // space for null-terminator
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
        // prints the string from client and iterates using  %.*s
        printf("Received data from client: %.*s\n", (int)bytes_received, buffer);

        // incase fedora is giving linux-like problems
        setenv("PATH",
               "/usr/local/bin:/usr/local/sbin:/sbin:/root:/opt/idafree-7.0:/bin:/"
               "usr/bin",
               1);

        // Call the forker function to execute the received command
        forker(buffer, client_socket);    // buffer contains the received command

        // Send the response to the client

        bytes_sent = write(client_socket, response, strlen(response));
        if(bytes_sent == -1)
        {
            handle_client(client_socket);
        }
        // Receive data from client
        bytes_received = read(client_socket, buffer, sizeof(buffer) - 1);
        if(bytes_received > 0)
        {
            buffer[bytes_received] = '\0';    // Null-terminate the string
        }
        else
        {
            // Handle read error or closed socket
            printf("Nothing received from client\n");
            close(client_socket);
            return 0;
        }

        // Print the received HTTP request
        printf("Received HTTP request:\n%s\n", buffer);

        // TODO: Implement parsing of HTTP requests and generating appropriate responses

        // TODO: Implement handling of GET, HEAD, and POST requests

        // TODO: Implement NDBM storage with POST requests

        // Respond to the client

        write(client_socket, response, strlen(response));

        // Close the client socket
        close(client_socket);
    }
}

void forker(const char *command, int client_socket)
{
    // Fork a new process
    pid_t pid = fork();

    if(pid == -1)
    {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }
    else if(pid == 0)
    {
        // Child process
        // Execute the command (you might want to implement this part)
        printf("Executing command: %s\n", command);
        execl("/bin/sh", "sh", "-c", command, NULL);
        perror("Exec failed");
        exit(EXIT_FAILURE);
    }
    else
    {
        // Parent process
        // Do any cleanup or additional processing if needed
        // For example, you might want to wait for the child process to complete
        waitpid(pid, NULL, 0);

        // Close the client socket in the parent process
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
