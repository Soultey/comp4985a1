#include <arpa/inet.h>
#include <ndbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define TEN 10
#define FIVE 5
#define HTML_FILE "/Users/alex/Documents/comp4981a1/comp4985a1/index.html"
#define RWMODE 0666
#define HUNDO 100

#ifndef SOCK_CLOEXEC
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-macros"
    #define SOCK_CLOEXEC 0
    #pragma GCC diagnostic pop
#endif

// Function prototypes
void start_server(const char *server_ip, int server_port) __attribute__((noreturn));
int  create_server_socket(const char *server_ip, int server_port);
void accept_client_connections(int server_socket) __attribute__((noreturn));
void handle_client(int client_socket);
void handle_post_request(int client_socket, const char *buffer);
void send_response(int client_socket, const char *response);
void send_file(int client_socket);
void retrieve_data_and_send_response(int client_socket, const char *username);

// Start the server
void start_server(const char *server_ip, int server_port)
{
    int server_socket = create_server_socket(server_ip, server_port);
    printf("Server listening on port %d...\n", server_port);
    accept_client_connections(server_socket);
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

// Accept client connections
void accept_client_connections(int server_socket)
{
    while(1)
    {
        int                client_socket;
        struct sockaddr_in client_address;
        socklen_t          client_address_len;

        client_address_len = sizeof(client_address);

        // Accept a client connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len);
        if(client_socket == -1)
        {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        printf("Client connected\n");

        // Handle client request
        handle_client(client_socket);
    }
}

// Handle client request
void handle_client(int client_socket)
{
    char    buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    bytes_received = read(client_socket, buffer, sizeof(buffer) - 1);

    if(bytes_received <= 0)
    {
        // Handle read error or closed socket
        printf("Nothing received from client\n");
        close(client_socket);
        return;
    }

    // Null-terminate the received data
    buffer[bytes_received] = '\0';

    // Print the received HTTP request
    printf("Received HTTP request:\n%s\n", buffer);

    // Check if it's a GET request
    if(strstr(buffer, "GET") == buffer)
    {
        // Check if the request is for retrieving data from the database
        if(strstr(buffer, "/retrieve/") != NULL)
        {
            // Extract the username from the URL
            const char *username_start = strstr(buffer, "/retrieve/") + strlen("/retrieve/");
            char        username[HUNDO];
            sscanf(username_start, "%99[^ ]", username);

            // Call a function to retrieve data from the database based on the username
            retrieve_data_and_send_response(client_socket, username);
        }
        else
        {
            // Handle regular GET request
            send_file(client_socket);
        }
    }
    else if(strstr(buffer, "POST") == buffer)
    {
        handle_post_request(client_socket, buffer);
    }
    else if(strstr(buffer, "HEAD") == buffer)
    {
        // Handle HEAD request
    }
    else
    {
        // Unsupported HTTP method, send a 501 Not Implemented response
        const char *not_implemented_response = "HTTP/1.0 501 Not Implemented\r\n\r\n";
        ssize_t     bytes_sent               = write(client_socket, not_implemented_response, strlen(not_implemented_response));
        if(bytes_sent == -1)
        {
            perror("Error sending response to client");
        }
    }

    // Close the client socket
    close(client_socket);
}

void retrieve_data_and_send_response(int client_socket, const char *username)
{
    DBM  *db;
    datum key;
    datum value;
    char *username_copy = NULL;

    // Open the NDBM database
    db = dbm_open("database.db", O_RDONLY, RWMODE);

    if(!db)
    {
        perror("Error opening database");
        send_response(client_socket, "HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return;
    }

    // Allocate memory for the username copy (include space for null terminator)
    username_copy = (char *)malloc(strlen(username) + 1);
    if(username_copy == NULL)
    {
        perror("Error allocating memory");
        send_response(client_socket, "HTTP/1.0 500 Internal Server Error\r\n\r\n");
        dbm_close(db);
        return;
    }

    // Copy the username to avoid accessing the original string after potential deallocation
    strcpy(username_copy, username);

    // Lookup the username in the database
    key.dptr  = username_copy;    // Use the copy
    key.dsize = strlen(username_copy);
    value     = dbm_fetch(db, key);

    // Check if the username was found
    if(value.dptr)
    {
        // Calculate the length of the response message
        size_t response_length = strlen("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nUsername: ") + strlen(username) + strlen("\nPassword: ") + strlen((char *)value.dptr) + 1;

        // Allocate memory for the response
        char *response = (char *)malloc(response_length);
        if(response == NULL)
        {
            perror("Error allocating memory");
            send_response(client_socket, "HTTP/1.0 500 Internal Server Error\r\n\r\n");
            return;
        }

        // Construct the response message
        snprintf(response, response_length, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nUsername: %s\nPassword: %s\n", username, (char *)value.dptr);

        // Send the response
        send_response(client_socket, response);

        // Free dynamically allocated memory
        free(response);
    }
    else
    {
        // Username not found
        send_response(client_socket, "HTTP/1.0 404 Not Found\r\n\r\n");
    }

    dbm_close(db);

    // Free the allocated memory for the username copy
    free(username_copy);
}

void handle_post_request(int client_socket, const char *buffer)
{
    // Declare variables at the beginning
    char username[HUNDO];
    char password[HUNDO];

    // Extract POST data
    char *post_data_start = strstr(buffer, "\r\n\r\n");
    if(post_data_start != NULL)
    {
        DBM  *db;
        datum key;
        datum value;

        const char response_header[] = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n";

        post_data_start += 4;    // Move past the "\r\n\r\n"
        printf("Received POST data:\n%s\n", post_data_start);

        // Parse POST data to extract parameters
        sscanf(post_data_start, "username=%99[^&]&password=%99s", username, password);

        // Open the NDBM database
        db = dbm_open("database.db", O_CREAT | O_RDWR, RWMODE);
        if(!db)
        {
            perror("Error opening database");
            send_response(client_socket, "HTTP/1.0 500 Internal Server Error\r\n\r\n");
            return;
        }

        // Store the username and password in the database
        key.dptr    = username;
        key.dsize   = strlen(username);
        value.dptr  = password;
        value.dsize = strlen(password);

        if(dbm_store(db, key, value, DBM_REPLACE) != 0)
        {
            perror("Error storing data in database");
            send_response(client_socket, "HTTP/1.0 500 Internal Server Error\r\n\r\n");
            dbm_close(db);
            return;
        }

        dbm_close(db);

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
        close(client_socket);
        return;
    }

    // Open the HTML file
    html_file = fopen(HTML_FILE, "re");    // Use "r" instead of "re"
    if(html_file == NULL)
    {
        perror("Error opening HTML file");
        close(client_socket);
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
            close(client_socket);
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
