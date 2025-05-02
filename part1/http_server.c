#define _GNU_SOURCE

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "http.h"

#define BUFSIZE 512
#define LISTEN_QUEUE_LEN 5

int keep_going = 1;

void handle_sigint(int signo) {
    keep_going = 0;
}

int main(int argc, char **argv) {
    // Install SIGINT handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    // First argument is directory to serve, second is port
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }

    const char *serve_dir = argv[1];
    const char *port = argv[2];

    // Socket Setup
    //  Step 1: Set up address info
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;          // IPv4
    hints.ai_socktype = SOCK_STREAM;    // TCP
    hints.ai_flags = AI_PASSIVE;        // For binding to server

    // Bind specifically to localhost
    int ret_val = getaddrinfo("127.0.0.1", port, &hints, &res);
    if (ret_val != 0) {
        printf("getaddrinfo failed: %s\n", gai_strerror(ret_val));
        return 1;
    }

    // Step 2: Create socket
    int sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock_fd == -1) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    // Step 3: Bind socket to port
    if (bind(sock_fd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("bind");
        close(sock_fd);
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);    // no longer needed

    // Step 4: Listen on the socket
    if (listen(sock_fd, LISTEN_QUEUE_LEN) == -1) {
        perror("listen");
        close(sock_fd);
        return 1;
    }
    
    // You can now enter a loop to accept and handle connections...
    while (keep_going) {
        // Main Server Loop
        struct sockaddr_storage client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int conn_fd = accept(sock_fd, (struct sockaddr *) &client_addr, &addr_size);

        if (conn_fd == -1) {
            if (errno == EINTR) {
                // Interrupted by SIGINT, break loop to shutdown
                break;
            }
            perror("accept");
            continue;
        }

        char resource_name[BUFSIZE];

        // Step 1: Read HTTP request
        if (read_http_request(conn_fd, resource_name) == -1) {
            perror("read_http_request");
            close(conn_fd);
            continue;
        }

        // Step 2: Build full file path
        char full_path[BUFSIZE * 2];
        snprintf(full_path, sizeof(full_path), "%s%s", serve_dir, resource_name);

        // Step 3: Write HTTP response
        if (write_http_response(conn_fd, full_path) == -1) {
            perror("write_http_response");
            close(conn_fd);
            close(sock_fd);
            return 1;
        }

        // Step 4: Close the connection socket
        close(conn_fd);
    }

    // Cleanup
    close(sock_fd);
    return 0;
}
