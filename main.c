#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FIVE 5
#define THOU 1024
#define BIG 65535
#define TEN 10
#define SIXTYFO 64

#ifndef SOCK_CLOEXEC
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-macros"
    #define SOCK_CLOEXEC 0
    #pragma GCC diagnostic pop
#endif

void forker(const char *command, int client_socket);

int find_executable(const char *program_name, char *const commandArgs[], char *result_path, size_t result_path_size);

int main(int argc, const char *argv[])
{
    long               port;
    int                server_socket;
    int                client_socket;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t          client_address_len;

    char    buffer[THOU];
    ssize_t bytes_received;

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
    server_socket = socket(AF_INET, SOCK_CLOEXEC, 0);
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

    // Close client and server sockets
    close(client_socket);
    close(server_socket);

    return 0;
}

void forker(const char *command, int client_socket)
{
    pid_t  child_pid;
    char  *command_copy;
    char **commandArgs;
    int    arg_count;
    char   executable_path[THOU];
    // Split the command_copy into tokens using strtok_r.
    char *token;
    char *saveptr;

    command_copy = strdup(command);    // Create a non-const copy of the command
    if(command_copy == NULL)
    {
        perror("strdup");
        exit(1);    // Exit with an error code.
    }

    child_pid = fork();

    if(child_pid == -1)
    {
        perror("fork");
        free(command_copy);    // Free the allocated memory
        exit(1);               // Exit with an error code.
    }
    else if(child_pid == 0)
    {
        // Child process begins

        // Redirect stdout to the client socket
        if(dup2(client_socket, STDOUT_FILENO) == -1)
        {
            perror("dup2");
            free(command_copy);    // Free the allocated memory
            exit(1);               // Exit with an error code.
        }

        token = strtok_r(command_copy, " \"", &saveptr);

        if(token == NULL)
        {
            fprintf(stderr, "Invalid command format\n");
            free(command_copy);    // Free the allocated memory
            exit(1);
        }
        commandArgs = (char **)malloc(sizeof(char *) * SIXTYFO);
        arg_count   = 0;

        // Initialize the commandArgs array to later pass into execv.
        commandArgs[arg_count] = strdup(token);
        arg_count++;

        while(token != NULL)
        {
            token = strtok_r(NULL, "  ", &saveptr);
            if(token != NULL)
            {
                commandArgs[arg_count] = strdup(token);
                arg_count++;
            }
        }

        commandArgs[arg_count] = NULL;    // Null-terminate the array.

        // Attempt to find and execute the program in the child process.

        // Checks if the command is executable
        if(find_executable(commandArgs[0], commandArgs, executable_path, sizeof(executable_path)) == 0)
        {
            // Execute the program.
            if(execv(executable_path, commandArgs) == -1)
            {
                perror("execv");
            }
        }
        else
        {
            fprintf(stderr, "Executable not found or execution failed.\n");
        }

        // Free the allocated memory before exiting
        free(command_copy);
        exit(1);    // Exit with an error code if there's an issue with execution.
    }
    free(command_copy);
}

int find_executable(const char *program_name, char *const commandArgs[], char *result_path, size_t result_path_size)
{
    char *path_env;
    char *path_copy;
    char *token;
    char *saveptr;
    int   executable_found = 0;    // Flag to track whether an executable was found.
    // Get the PATH environment variable.
    path_env = getenv("PATH");

    if(path_env == NULL)
    {
        fprintf(stderr, "PATH environment variable not found.\n");
        return -1;
    }

    // Make a copy of the PATH variable.
    path_copy = strdup(path_env);

    if(path_copy == NULL)
    {
        perror("strdup");
        return -1;
    }

    token = strtok_r(path_copy, ":", &saveptr);

    while(token != NULL)
    {
        // Construct the full path to the executable.
        snprintf(result_path, result_path_size, "%s/%s", token, program_name);

        // Try to execute the program.
        if(execv(result_path, commandArgs) != -1)
        {
            // If execv succeeds, set the flag and break out of the loop.
            executable_found = 1;
            break;
        }

        // If execv returns, it means execution failed.
        // Continue searching for the program in other directories by checking the
        // next base directory.
        token = strtok_r(NULL, ":", &saveptr);
    }

    free(path_copy);    // Free the copied PATH variable.

    if(executable_found)
    {
        // Executable found and executed successfully.
        printf("Executable found at path: %s\n", result_path);
        return 0;
    }

    // Executable not found in the PATH.
    printf("Executable not found in the PATH.\n");
    return -1;
}
