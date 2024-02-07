#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

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

// Forward declarations
struct HttpRequest;
struct p101_env;
void  parse_http_request(const char *request, struct HttpRequest *parsed_request);
void  generate_http_response(int client_socket, const char *content);
void  handle_http_get_request(int client_fd, const char *path);
void  handle_http_post_request(int client_fd);
void  handle_http_request(int client_fd, const char *request);
void  cleanup(struct p101_env *env, int server_fd);
void *handle_connection(void *arg);

// HTTP request structure
struct HttpRequest
{
    char method[MAX_METHOD_LEN];
    char path[MAX_PATH_LEN];
    char protocol[MAX_PROTOCOL_LEN];
};

// Server environment structure
struct p101_env
{
    pthread_mutex_t client_list_mutex;
    int             exit_flag;
    int             client_sockets[MAX_CLIENTS];
    int             num_clients;
};

// Parse HTTP request function
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

// Generate HTTP response function
void generate_http_response(int client_socket, const char *content)
{
    char response[RESPONSE_BUFFER_SIZE];
    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n%s", strlen(content), content);
    send(client_socket, response, strlen(response), 0);
}

// Handle HTTP GET request function
void handle_http_get_request(int client_fd, const char *path)
{
    printf("Handling GET request for path: %s\n", path);

    // Send a simple HTML response
    //    const char *html_content = "<html><body><h1>Hello, World!</h1></body></html>";
    //    generate_http_response(client_fd, html_content);

    if(strcmp(path, "/") == 0)
    {
        // path = "comp4985a1/src/index.html";
        //   path = "./src/index.html";
        path = "index.html"; //needs to be in the build directory
    }

    printf("Serving file: %s\n", path);

    int file_fd = open(path, O_RDONLY | O_CLOEXEC);

    if(file_fd != -1)
    {
        struct stat file_stat;
        fstat(file_fd, &file_stat);

        char    buffer[BUFFER_SIZE];
        ssize_t bytes_read;

        // generate_http_response(client_fd, "");    // Empty content for now

        FILE *file = fopen(path, "re");
        if(file != NULL)
        {
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            fseek(file, 0, SEEK_SET);

            // cast the file_size to size_t
            char *content = (char *)malloc((size_t)file_size + 1);
            if(content == NULL)
            {
                // memory allocation
                perror("Error: Memory allocation failed");
                fclose(file);      // close file before leaving
                close(file_fd);    // close file d
                return;            // Exit function
            }
            fread(content, 1, (size_t)file_size, file);
            content[file_size] = '\0';

            fclose(file);

            // Generate HTTP response with the content of index.html
            generate_http_response(client_fd, content);

            free(content);
        }
        else
        {
            // Handle error opening file
            perror("Error: Unable to open file");
        }

        while((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0)
        {
            send(client_fd, buffer, (size_t)bytes_read, 0);
        }

        close(file_fd);
    }
    else
    {
        perror("Error: Unable to open file");
        const char *response = "HTTP/1.1 404 Not Found\r\nContent-Length: 15\r\n\r\n404 Not Found\n";
        send(client_fd, response, strlen(response), 0);
    }
}

// Handle HTTP POST request function
void handle_http_post_request(int client_fd)
{
    const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHello POST!";
    send(client_fd, response, strlen(response), 0);
}

// Handle HTTP request function
void handle_http_request(int client_fd, const char *request)
{
    struct HttpRequest parsed_request;
    parse_http_request(request, &parsed_request);

    printf("Request: %s %s %s\n", parsed_request.method, parsed_request.path, parsed_request.protocol);

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

// Handle connection function
void *handle_connection(void *arg)
{
    struct p101_env *env = (struct p101_env *)arg;
    char             buffer[BUFFER_SIZE];
    ssize_t          bytes_received;
    int              client_fd = *(int *)arg;

    printf("Handling connection for client FD: %d\n", client_fd);

    while((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0';

        pthread_mutex_lock(&env->client_list_mutex);
        handle_http_request(client_fd, buffer);
        pthread_mutex_unlock(&env->client_list_mutex);
    }

    pthread_mutex_lock(&env->client_list_mutex);
    close(client_fd);
    printf("Client %d disconnected.\n", client_fd);
    pthread_mutex_unlock(&env->client_list_mutex);

    return NULL;
}

// Cleanup function
void cleanup(struct p101_env *env, int server_fd)
{
    close(server_fd);
    pthread_mutex_destroy(&env->client_list_mutex);

    for(int i = 0; i < env->num_clients; ++i)
    {
        close(env->client_sockets[i]);
    }
}

// Main function
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
            continue;
        }

        if(env.num_clients < MAX_CLIENTS)
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
            close(client_fd);
        }

        printf("Number of clients: %d\n", env.num_clients);
    }

    cleanup(&env, server_fd);

    return EXIT_SUCCESS;
}
