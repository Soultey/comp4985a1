#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// #include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>

#ifndef SOCK_CLOEXEC
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-macros"
    #define SOCK_CLOEXEC 0
    #pragma GCC diagnostic pop
#endif

#define BUFFER_SIZE 4096
#define RESPONSE_BUFFER_SIZE 4096
#define MAX_CLIENTS 10
#define MAX_METHOD_LEN 10
#define MAX_PATH_LEN 255
#define MAX_PROTOCOL_LEN 10

struct HttpRequest
{
    char method[MAX_METHOD_LEN];
    char path[MAX_PATH_LEN];
    char protocol[MAX_PROTOCOL_LEN];
};

struct p101_env
{
    pthread_mutex_t client_list_mutex;
    int             exit_flag;
    int             client_sockets[MAX_CLIENTS];
    int             num_clients;
};

// struct p101_error
//{
//     int code;
//     const char *message;
// };

// struct p101_error initialize_error(int code, const char *message)
//{
//     struct p101_error error;
//     error.code = code;
//     error.message = message;
//     return error;
// }
//
// void print_error(const struct p101_error *err)
//{
//     fprintf(stderr, "Error %d: %s\n", err->code, err->message);
// }

void parse_http_request(const char *request, struct HttpRequest *parsed_request)
{
    if(sscanf(request, "%9s %255s %9s", parsed_request->method, parsed_request->path, parsed_request->protocol) != 3)
    {
        fprintf(stderr, "Error parsing HTTP request\n");
    }
    else
    {
        printf("Parsed HTTP request: Method=%s, Path=%s, Protocol=%s\n", parsed_request->method, parsed_request->path, parsed_request->protocol);
    }
}

void generate_http_response(int client_socket, const char *content)
{
    char response[RESPONSE_BUFFER_SIZE];
    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n%s", strlen(content), content);
    send(client_socket, response, strlen(response), 0);
}

void handle_http_get_request(int client_fd, const char *path)
{
    printf("Handling GET request for path: %s\n", path);

    // Send a simple HTML response
    const char *html_content = "<html><body><h1>Hello, World!</h1></body></html>";
    generate_http_response(client_fd, html_content);

    // If the requested path is the root, set it to index.html
    if(strcmp(path, "/") == 0)
    {
        path = "index.html";
    }

    printf("Serving file: %s\n", path);
    // Open the requested file
    int file_fd = open(path, O_RDONLY | O_CLOEXEC);
    if(file_fd != -1)
    {
        struct stat file_stat;
        fstat(file_fd, &file_stat);

        // Read the file content
        char    buffer[BUFFER_SIZE];
        ssize_t bytes_read;

        // Send HTTP response headers using generate_http_response
        generate_http_response(client_fd, "");    // Empty content for now

        // Send file content
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

void handle_http_post_request(int client_fd)
{
    // Example: Process POST data and send a response
    const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHello POST!";
    send(client_fd, response, strlen(response), 0);
}

void handle_http_request(int client_fd, const char *request)
{
    struct HttpRequest parsed_request;
    parse_http_request(request, &parsed_request);

    printf("Request: %s %s %s\n", parsed_request.method, parsed_request.path, parsed_request.protocol);
    printf("Method: %s\n", parsed_request.method);
    printf("Path: %s\n", parsed_request.path);
    printf("Protocol: %s\n", parsed_request.protocol);

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
        const char *response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, response, strlen(response), 0);
    }
}

void cleanup(struct p101_env *env, int server_fd);

void *handle_connection(void *arg)
{
    struct p101_env *env = (struct p101_env *)arg;
    char             buffer[BUFFER_SIZE];
    ssize_t          bytes_received;

    // Extract client_fd from arg
    int client_fd = *(int *)arg;

    printf("Handling connection for client FD: %d\n", client_fd);

    while((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0';    // Ensure null-terminated string

        // Print the received data
        printf("Received data from client %d: %s\n", client_fd, buffer);

        pthread_mutex_lock(&env->client_list_mutex);
        // Handle the HTTP request
        handle_http_request(client_fd, buffer);
        pthread_mutex_unlock(&env->client_list_mutex);
    }

    pthread_mutex_lock(&env->client_list_mutex);
    // Close the socket and perform cleanup
    close(client_fd);
    printf("Client %d disconnected.\n", client_fd);
    pthread_mutex_unlock(&env->client_list_mutex);

    return NULL;
}

void cleanup(struct p101_env *env, int server_fd)
{
    close(server_fd);

    pthread_mutex_destroy(&env->client_list_mutex);

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

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(8080);

    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(server_fd == -1)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    if(fcntl(server_fd, F_SETFD, FD_CLOEXEC) == -1)
    {
        perror("Error setting close-on-exec flag");
        cleanup(&env, server_fd);
        exit(EXIT_FAILURE);
    }

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
    printf("Server is running. Open your web browser and visit http://localhost:8080/\n");

    while(!env.exit_flag)
    {
        struct sockaddr_storage client_addr;
        socklen_t               client_addr_len = sizeof(client_addr);
        int                     client_fd       = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if(client_fd == -1)
        {
            perror("Error accepting connection");
            cleanup(&env, server_fd);
            continue;    // Skip to the next iteration
        }

        // Add the validation check for client_fd
        if(client_fd != -1 < MAX_CLIENTS)
        {
            pthread_mutex_lock(&env.client_list_mutex);
            env.client_sockets[env.num_clients++] = client_fd;
            pthread_mutex_unlock(&env.client_list_mutex);

            pthread_t thread;
            pthread_create(&thread, NULL, handle_connection, (void *)&client_fd);
            pthread_detach(thread);
        }
        else
        {
            // Connection failed or max clients reached, reject connection
            close(client_fd);
        }

        printf("Number of clients: %d\n", env.num_clients);
    }

    cleanup(&env, server_fd);

    return EXIT_SUCCESS;
}
