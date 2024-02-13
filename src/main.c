#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define TEN 10
#define FIVE 5
#define HTML_FILE "../index.html"
#define HUNDO 100

#ifndef SOCK_CLOEXEC
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-macros"
    #define SOCK_CLOEXEC 0
    #pragma GCC diagnostic pop
#endif

struct ThreadArgs
{
    int client_socket;
};

// Function prototypes
void  start_server(const char *server_ip, int server_port) __attribute__((noreturn));
int   create_server_socket(const char *server_ip, int server_port);
void *client_handler(void *args);
void  send_head_response(int client_socket);
void  handle_post_request(int client_socket, const char *buffer);
void  send_response(int client_socket, const char *response);
void  send_file(int client_socket);

// Start the server
void start_server(const char *server_ip, int server_port)
{
    int server_socket = create_server_socket(server_ip, server_port);
    printf("Server listening on port %d...\n", server_port);

    while(1)
    {
        struct sockaddr_in client_address;
        struct ThreadArgs *args;

        socklen_t client_address_len = sizeof(client_address);
        int       client_socket      = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len);
        if(client_socket == -1)
        {
            perror("Accept failed");
            continue;
        }
        printf("Client connected\n");

        // Create a new thread to handle the client connection

        args = malloc(sizeof(struct ThreadArgs));
        if(args == NULL)
        {
            perror("Memory allocation failed");
            close(client_socket);
            continue;
        }
        args->client_socket = client_socket;

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, args);
        pthread_detach(tid);
    }
}

// Create and bind server socket
int create_server_socket(const char *server_ip, int server_port)
{
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
    server_address.sin_port        = htons((uint16_t)server_port);
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

    return server_socket;
}

// Client handler thread function
void *client_handler(void *args)
{
    char    buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int     client_socket;

    const struct ThreadArgs *threadArgs;

    threadArgs = (struct ThreadArgs *)args;

    client_socket = threadArgs->client_socket;
    free(args);    // Free the memory allocated for the argument

    bytes_received = read(client_socket, buffer, sizeof(buffer) - 1);

    if(bytes_received <= 0)
    {
        // Handle read error or closed socket
        printf("Nothing received from client\n");
        close(client_socket);
        return NULL;
    }

    // Null-terminate the received data
    buffer[bytes_received] = '\0';

    // Print the received HTTP request
    printf("Received HTTP request:\n%s\n", buffer);

    // Check if it's a GET or HEAD request
    if(strstr(buffer, "GET") == buffer || strstr(buffer, "HEAD") == buffer)
    {
        // Check if the request is for retrieving data from the database
        if(strstr(buffer, "/retrieve/") != NULL)
        {
            // Extract the username from the URL
            const char *username_start = strstr(buffer, "/retrieve/") + strlen("/retrieve/");
            char        username[HUNDO];
            sscanf(username_start, "%99[^ ]", username);

            // Call a function to retrieve data from the database based on the username
        }
        else
        {
            // Handle regular GET or HEAD request
            if(strstr(buffer, "HEAD") == buffer)
            {
                send_head_response(client_socket);    // Send only headers for HEAD requests
            }
            else
            {
                send_file(client_socket);    // Send file content for GET requests
            }
        }
    }
    else if(strstr(buffer, "POST") == buffer)
    {
        handle_post_request(client_socket, buffer);
    }
    else
    {
        // Unsupported HTTP method, send a 501 Not Implemented response
        const char *not_implemented_response = "HTTP/1.0 501 Not Implemented\r\n\r\n";
        ssize_t     bytes_sent;

        bytes_sent = write(client_socket, not_implemented_response, strlen(not_implemented_response));
        if(bytes_sent == -1)
        {
            perror("Error sending response to client");
        }
    }

    // Close the client socket
    close(client_socket);
    return NULL;
}

void send_head_response(int client_socket)
{
    FILE   *html_file = fopen(HTML_FILE, "re");
    long    content_length;
    char    response_header[BUFFER_SIZE];
    ssize_t bytes_sent;

    if(html_file == NULL)
    {
        perror("Error opening HTML file");
        return;
    }

    fseek(html_file, 0, SEEK_END);
    content_length = ftell(html_file);
    fseek(html_file, 0, SEEK_SET);

    fclose(html_file);

    snprintf(response_header, sizeof(response_header), "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", content_length);

    bytes_sent = write(client_socket, response_header, strlen(response_header));
    if(bytes_sent == -1)
    {
        perror("Error sending response to client");
        return;
    }

    // printf("Response sent:\n%s", response_header);
}

void handle_post_request(int client_socket, const char *buffer)
{
    // Extract POST data
    char *post_data_start = strstr(buffer, "\r\n\r\n");
    if(post_data_start != NULL)
    {
        // Declare variables at the beginning
        char username[HUNDO];
        char password[HUNDO];

        const char response_header[] = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n";

        post_data_start += 4;    // Move past the "\r\n\r\n"
        printf("Received POST data:\n%s\n", post_data_start);

        // Parse POST data to extract parameters
        sscanf(post_data_start, "username=%99[^&]&password=%99s", username, password);

        // Send confirmation response
        send_response(client_socket, response_header);
    }
    else
    {
        // Malformed POST request, send a 400 Bad Request response
        const char bad_request_response[] = "HTTP/1.0 400 Bad Request\r\n\r\n";
        send_response(client_socket, bad_request_response);
    }
}

void send_response(int client_socket, const char *response)
{
    ssize_t bytes_sent = write(client_socket, response, strlen(response));
    if(bytes_sent == -1)
    {
        perror("Error sending response to client");
    }
}

void send_file(int client_socket)
{
    const char response_header[] = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n";
    ssize_t    bytes_sent        = write(client_socket, response_header, strlen(response_header));
    char       html_buffer[BUFFER_SIZE];
    size_t     html_bytes_read;    // Change ssize_t to size_t
    FILE      *html_file;

    if(bytes_sent == -1)
    {
        perror("Error sending response to client");
        return;
    }

    // Open the HTML file
    html_file = fopen(HTML_FILE, "re");    // Use "r" instead of "re"
    if(html_file == NULL)
    {
        perror("Error opening HTML file");
        return;
    }

    // Read and send the HTML file contents
    while((html_bytes_read = fread(html_buffer, 1, sizeof(html_buffer), html_file)) > 0)
    {
        bytes_sent = write(client_socket, html_buffer, html_bytes_read);
        if(bytes_sent == -1)
        {
            perror("Error sending response to client");
            fclose(html_file);
            return;
        }
    }

    fclose(html_file);
}

// Main function
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
