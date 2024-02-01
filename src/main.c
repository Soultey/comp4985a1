#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void          *server_input_thread(void *arg);
static void           add_client_socket(int client_sockfd);
static void           remove_client_socket(int client_sockfd);
static void           broadcast_to_clients(const char *message);
static void          *receive_messages(void *arg);
static void           setup_signal_handler(void);
static void           sigint_handler(int signum);
static void           parse_arguments(int argc, char *argv[], char **ip_address, char **port);
static void           handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, in_port_t *port);
static in_port_t      parse_in_port_t(const char *binary_name, const char *port_str);
_Noreturn static void usage(const char *program_name, const char *message);
static void           convert_address(const char *address, struct sockaddr_storage *addr);
static int            socket_create(int domain, int type, int protocol);
static void           socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void           start_listening(int server_fd);
static int            socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);
static void           handle_connection(int client_sockfd);
static void           socket_close(int sockfd);
static void           socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void          *thread_func(void *arg);
// static void           broadcast_to_other_clients(const char *message, int sender_sockfd);

#define BASE_TEN 10
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t exit_flag = 0;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int client_sockets[MAX_CLIENTS];
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int num_clients = 0;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static pthread_mutex_t client_list_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[])
{
    int                     opt;
    int                     c_flag   = 0;
    int                     a_flag   = 0;
    char                   *address  = NULL;
    char                   *port_str = NULL;
    in_port_t               port;
    int                     sockfd;
    struct sockaddr_storage addr;
    pthread_t               recv_thread;
    pthread_t               server_thread;
    opterr = 0;

    // Parse command line options
    while((opt = getopt(argc, argv, ":ac")) != -1)
    {
        switch(opt)
        {
            case 'a':
                a_flag = 1;
                break;
            case 'c':
                c_flag = 1;
                break;
            default:
                fprintf(stderr, "Invalid option or missing argument.\n");
                usage(argv[0], "Invalid usage");
        }
    }

    parse_arguments(argc, argv, &address, &port_str);
    handle_arguments(argv[0], address, port_str, &port);

    if(c_flag && argc - optind == 2)
    {
        // Client code
        convert_address(address, &addr);
        sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
        socket_connect(sockfd, &addr, port);

        if(pthread_create(&recv_thread, NULL, receive_messages, (void *)&sockfd) != 0)
        {
            perror("Failed to create receive thread");
            exit(EXIT_FAILURE);
        }

        while(1)
        {
            char message[BUFFER_SIZE];
            if(fgets(message, BUFFER_SIZE, stdin) == NULL)
            {
                printf("Client exiting due to EOF\n");
                break;    // EOF or error
            }
            if(write(sockfd, message, strlen(message)) < 0)
            {
                perror("write error");
                break;
            }
        }

        socket_close(sockfd);
        pthread_join(recv_thread, NULL);
    }
    else if(a_flag && argc - optind == 2)
    {
        // Server code
        convert_address(address, &addr);
        sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
        socket_bind(sockfd, &addr, port);
        start_listening(sockfd);
        setup_signal_handler();

        if(pthread_create(&server_thread, NULL, server_input_thread, NULL) != 0)
        {
            perror("Failed to create server input thread");
            exit(EXIT_FAILURE);
        }

        while(!exit_flag)
        {
            int                     client_sockfd;
            struct sockaddr_storage client_addr;
            socklen_t               client_addr_len = sizeof(client_addr);
            int                    *new_sock;
            pthread_t               thread_id;

            client_sockfd = socket_accept_connection(sockfd, &client_addr, &client_addr_len);

            if(client_sockfd == -1)
            {
                if(exit_flag)
                {
                    break;
                }
                continue;
            }

            add_client_socket(client_sockfd);
            new_sock = malloc(sizeof(int));
            if(!new_sock)
            {
                perror("Failed to allocate memory for new socket");
                close(client_sockfd);
                continue;
            }

            *new_sock = client_sockfd;

            if(pthread_create(&thread_id, NULL, thread_func, (void *)new_sock) != 0)
            {
                perror("Failed to create thread");
                // Consider freeing new_sock and closing client_sockfd here if thread creation fails.
            }
        }

        pthread_join(server_thread, NULL);
        socket_close(sockfd);
    }
    else
    {
        fprintf(stderr, "Usage: %s -flag <IP Address> <Port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

static void parse_arguments(int argc, char *argv[], char **ip_address, char **port)
{
    if(argc - optind == 2)
    {
        *ip_address = argv[optind];
        *port       = argv[optind + 1];
    }
    else
    {
        usage(argv[0], "Incorrect number of arguments");
    }
}

static void handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, in_port_t *port)
{
    if(ip_address == NULL)
    {
        usage(binary_name, "The IP address is required.");
    }

    if(port_str == NULL)
    {
        usage(binary_name, "The port is required.");
    }

    *port = parse_in_port_t(binary_name, port_str);
}

in_port_t parse_in_port_t(const char *binary_name, const char *str)
{
    char     *endptr;
    uintmax_t parsed_value;
    errno        = 0;
    parsed_value = strtoumax(str, &endptr, BASE_TEN);

    if(errno != 0)
    {
        perror("Error parsing in_port_t");
        exit(EXIT_FAILURE);
    }

    // Check if there are any non-numeric characters in the input string
    if(*endptr != '\0')
    {
        usage(binary_name, "Invalid characters in input.");
    }

    // Check if the parsed value is within the valid range for in_port_t
    if(parsed_value > UINT16_MAX)
    {
        usage(binary_name, "in_port_t value out of range.");
    }

    return (in_port_t)parsed_value;
}

_Noreturn static void usage(const char *program_name, const char *message)
{
    if(message)
    {
        fprintf(stderr, "%s\n", message);
    }
    fprintf(stderr, "Usage: %s -a <IP Address> <Port> (for server mode)\n", program_name);
    fprintf(stderr, "       %s -c <IP Address> <Port> (for client mode)\n", program_name);
    exit(1);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void sigint_handler(int signum)
{
    if(signum == SIGINT || signum == SIGTSTP)
    {
        exit_flag = 1;
    }
}

#pragma GCC diagnostic pop

static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;
    }
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

static int socket_create(int domain, int type, int protocol)
{
    int sockfd;
    sockfd = socket(domain, type, protocol);

    if(sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    socklen_t addr_len;
    void     *vaddr;
    in_port_t net_port;
    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;
        ipv4_addr           = (struct sockaddr_in *)addr;
        addr_len            = sizeof(*ipv4_addr);
        ipv4_addr->sin_port = net_port;
        vaddr               = (void *)&(((struct sockaddr_in *)addr)->sin_addr);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;
        ipv6_addr            = (struct sockaddr_in6 *)addr;
        addr_len             = sizeof(*ipv6_addr);
        ipv6_addr->sin6_port = net_port;
        vaddr                = (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr);
    }
    else
    {
        fprintf(stderr,
                "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: "
                "%d\n",
                addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(inet_ntop(addr->ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Binding to: %s:%u\n", addr_str, port);

    if(bind(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        perror("Binding failed");
        fprintf(stderr, "Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    printf("Bound to socket: %s:%u\n", addr_str, port);
}

static void start_listening(int server_fd)
{
    if(listen(server_fd, 1) == -1)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");
}

static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len)
{
    int  client_fd;
    char client_host[NI_MAXHOST];
    char client_service[NI_MAXSERV];
    errno     = 0;
    client_fd = accept(server_fd, (struct sockaddr *)client_addr, client_addr_len);

    if(client_fd == -1)
    {
        if(errno != EINTR)
        {
            perror("accept failed");
        }
        return -1;
    }

    if(getnameinfo((struct sockaddr *)client_addr, *client_addr_len, client_host, NI_MAXHOST, client_service, NI_MAXSERV, 0) == 0)
    {
        printf("Accepted a new connection from %s:%s\n", client_host, client_service);
    }
    else
    {
        printf("Unable to get client information\n");
    }

    return client_fd;
}

static void setup_signal_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = sigint_handler;
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if(sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1 || sigaction(SIGTSTP, &sa, NULL) == -1)
    {
        perror("Error setting up signal handler");
        exit(EXIT_FAILURE);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

static void handle_connection(int client_sockfd)
{
    char    buffer[BUFFER_SIZE];
    ssize_t nread;

    while((nread = read(client_sockfd, buffer, sizeof(buffer) - 1)) > 0)
    {
        buffer[nread] = '\0';

        //    broadcast_to_other_clients(buffer, client_sockfd);
        printf("Received: %s\n", buffer);

        ssize_t nwritten = 0;
        size_t  to_write = (size_t)nread;

        while(nwritten < nread)
        {
            ssize_t nw = write(client_sockfd, buffer + nwritten, to_write);
            if(nw < 0)
            {
                if(errno != EINTR)
                {
                    perror("write error");
                    break;
                }
            }
            else
            {
                nwritten += nw;
                to_write -= (size_t)nw;
            }
        }
    }

    if(nread == -1)
    {
        perror("read error");
    }

    remove_client_socket(client_sockfd);
    close(client_sockfd);
}

#pragma GCC diagnostic pop

static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    in_port_t net_port;
    socklen_t addr_len;

    if(inet_ntop(addr->ss_family, addr->ss_family == AF_INET ? (void *)&(((struct sockaddr_in *)addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr), addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to: %s:%u\n", addr_str, port);
    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;
        ipv4_addr           = (struct sockaddr_in *)addr;
        ipv4_addr->sin_port = net_port;
        addr_len            = sizeof(struct sockaddr_in);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;
        ipv6_addr            = (struct sockaddr_in6 *)addr;
        ipv6_addr->sin6_port = net_port;
        addr_len             = sizeof(struct sockaddr_in6);
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(connect(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        const char *msg;

        msg = strerror(errno);
        fprintf(stderr, "Error: connect (%d): %s\n", errno, msg);
        exit(EXIT_FAILURE);
    }

    printf("Connected to: %s:%u\n", addr_str, port);
}

static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

static void *thread_func(void *arg)
{
    int sock = *((int *)arg);
    free(arg);    // Ensure to free the dynamically allocated memory
    handle_connection(sock);
    pthread_exit(NULL);    // Use pthread_exit for clean thread termination
}

static void *receive_messages(void *arg)
{
    int  sockfd = *((int *)arg);
    char buffer[BUFFER_SIZE];

    while(1)
    {
        ssize_t nread = read(sockfd, buffer, BUFFER_SIZE - 1);
        if(nread > 0)
        {
            buffer[nread] = '\0';
            printf("%s", buffer);    // Print messages received from server
        }
        else if(nread == 0)
        {
            printf("Connection closed by server.\n");
            break;
        }
        else
        {
            perror("read error");
            break;
        }
    }

    return NULL;
}

// Function to add client socket to the list
static void add_client_socket(int client_sockfd)
{
    pthread_mutex_lock(&client_list_mutex);

    if(num_clients < MAX_CLIENTS)
    {
        client_sockets[num_clients++] = client_sockfd;
    }

    pthread_mutex_unlock(&client_list_mutex);
}

// Function to remove a client socket from the list
static void remove_client_socket(int client_sockfd)
{
    pthread_mutex_lock(&client_list_mutex);

    for(int i = 0; i < num_clients; i++)
    {
        if(client_sockets[i] == client_sockfd)
        {
            client_sockets[i] = client_sockets[num_clients - 1];
            num_clients--;
            break;
        }
    }

    pthread_mutex_unlock(&client_list_mutex);
}

// Function to send a message to all clients
static void broadcast_to_clients(const char *message)
{
    pthread_mutex_lock(&client_list_mutex);

    for(int i = 0; i < num_clients; i++)
    {
        write(client_sockets[i], message, strlen(message));
    }

    pthread_mutex_unlock(&client_list_mutex);
}

static void *server_input_thread(void *arg)
{
    char buffer[BUFFER_SIZE];
    (void)arg;

    while(fgets(buffer, BUFFER_SIZE, stdin) != NULL)
    {
        broadcast_to_clients(buffer);
    }

    printf("Server exiting due to EOF\n");
    exit_flag = 1;

    return NULL;
}
