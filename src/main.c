#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 4096
#define RESPONSE_BUFFER_SIZE 4096
#define MAX_CLIENTS 10
#define MAX_METHOD_LEN 10
#define MAX_PATH_LEN 255
#define MAX_PROTOCOL_LEN 10

// Struct for representing an HTTP request
struct HttpRequest
{
    char method[MAX_METHOD_LEN];
    char path[MAX_PATH_LEN];
    char protocol[MAX_PROTOCOL_LEN];
};

// Struct for the server environment
struct p101_env
{
    pthread_mutex_t client_list_mutex;
    int             exit_flag;
    int             client_sockets[MAX_CLIENTS];
    int             num_clients;
};

// Struct for handling errors (you may define it based on your assignment requirements)
struct p101_error
{
    int         code;       // Error code indicating the type of error
    const char *message;    // Error message providing more details
};

// Function to initialize an error structure
struct p101_error initialize_error(int code, const char *message)
{
    struct p101_error error;
    error.code    = code;
    error.message = message;
    return error;
}

// Function to print an error
void print_error(const struct p101_error *err)
{
    fprintf(stderr, "Error %d: %s\n", err->code, err->message);
}

// Modify parse_http_request to accept a pointer to struct
void parse_http_request(const char *request, struct HttpRequest *parsed_request)
{
    sscanf(request, "%s %s %s", parsed_request->method, parsed_request->path, parsed_request->protocol);
}

// Function to generate HTTP response
void generate_http_response(int client_socket, const char *content)
{
    char response[RESPONSE_BUFFER_SIZE];
    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s", strlen(content), content);
    send(client_socket, response, strlen(response), 0);
}

// Function to handle HTTP GET request
void handle_http_get_request(int client_fd, const char *path)
{
    // Example: Serve the content of the requested file
    int file_fd = open(path, O_RDONLY | O_CLOEXEC);
    if(file_fd != -1)
    {
        struct stat file_stat;
        fstat(file_fd, &file_stat);

        // Send HTTP response headers
        dprintf(client_fd, "HTTP/1.1 200 OK\r\nContent-Length: %lld\r\n\r\n", file_stat.st_size);

        // Send file content
        char    buffer[BUFFER_SIZE];
        ssize_t bytes_read;

        while((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0)
        {
            send(client_fd, buffer, (size_t)bytes_read, 0);
        }

        close(file_fd);
    }
    else
    {
        // File not found
        const char *response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, response, strlen(response), 0);
    }
}

// Function to handle HTTP POST request
void handle_http_post_request(int client_fd)
{
    // Example: Process POST data and send a response
    const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHello POST!";
    send(client_fd, response, strlen(response), 0);
}

// Function to handle an HTTP request
void handle_http_request(int client_fd, const char *request)
{
    // Parse HTTP request
    struct HttpRequest parsed_request;
    parse_http_request(request, &parsed_request);

    // Print the parsed HTTP request details
    printf("Request: %s %s %s\n", parsed_request.method, parsed_request.path, parsed_request.protocol);
    printf("Method: %s\n", parsed_request.method);
    printf("Path: %s\n", parsed_request.path);
    printf("Protocol: %s\n", parsed_request.protocol);

    // Handle different HTTP methods
    if(strcmp(parsed_request.method, "GET") == 0)
    {
        handle_http_get_request(client_fd, parsed_request.path);
    }
    else if(strcmp(parsed_request.method, "POST") == 0)
    {
        handle_http_post_request(client_fd);
    }
    else
    {
        // Unsupported method
        const char *response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, response, strlen(response), 0);
    }
}

void cleanup(struct p101_env *env, int server_fd);

// Function to handle a client connection
void *handle_connection(void *arg)
{
    struct p101_env *env = (struct p101_env *)arg;
    char             buffer[BUFFER_SIZE];
    ssize_t          bytes_received;

    // Extract client_fd from arg
    int client_fd = *((int *)arg);

    while((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0';    // Ensure null-terminated string
        pthread_mutex_lock(&env->client_list_mutex);
        // Handle the HTTP request
        handle_http_request(client_fd, buffer);
        pthread_mutex_unlock(&env->client_list_mutex);
    }

    pthread_mutex_lock(&env->client_list_mutex);
    // Close the socket and perform cleanup
    close(client_fd);
    pthread_mutex_unlock(&env->client_list_mutex);

    return NULL;
}

void cleanup(struct p101_env *env, int server_fd)
{
    close(server_fd);

    pthread_mutex_destroy(&env->client_list_mutex);

    // Close remaining client connections
    for(int i = 0; i < env->num_clients; ++i)
    {
        close(env->client_sockets[i]);
    }
}

int main(void)
{
    struct p101_env env;
    env.exit_flag   = 0;
    env.num_clients = 0;
    pthread_mutex_init(&env.client_list_mutex, NULL);

    struct sockaddr_in server_addr;    // Use sockaddr_in for IPv4
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(8080);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if(server_fd == -1)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

#ifdef SOCK_CLOEXEC
    // Use SOCK_CLOEXEC if available
    int flags = SOCK_CLOEXEC;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags)) == -1)
    {
        perror("Error setting SO_REUSEADDR");
        cleanup(&env, server_fd);
        exit(EXIT_FAILURE);
    }
#else
    // Fall back to fcntl for setting FD_CLOEXEC
    if(fcntl(server_fd, F_SETFD, FD_CLOEXEC) == -1)
    {
        perror("Error setting close-on-exec flag");
        cleanup(&env, server_fd);
        exit(EXIT_FAILURE);
    }
#endif

    if(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Error binding socket");
        cleanup(&env, server_fd);
        exit(EXIT_FAILURE);
    }

    if(listen(server_fd, SOMAXCONN) == -1)
    {
        perror("Error listening on socket");
        cleanup(&env, server_fd);
        exit(EXIT_FAILURE);
    }

    while(!env.exit_flag)
    {
        struct sockaddr_storage client_addr;
        socklen_t               client_addr_len = sizeof(client_addr);
        int                     client_fd       = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if(env.num_clients < MAX_CLIENTS)
        {
            pthread_mutex_lock(&env.client_list_mutex);
            env.client_sockets[env.num_clients++] = client_fd;
            pthread_mutex_unlock(&env.client_list_mutex);

            pthread_t thread;
            pthread_create(&thread, NULL, handle_connection, &env);
            pthread_detach(thread);
        }
        else
        {
            // Max clients reached, reject connection
            close(client_fd);
        }
    }

    // Perform cleanup on graceful shutdown or errors
    cleanup(&env, server_fd);

    return EXIT_SUCCESS;
}
