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
    // First argument is directory to serve, second is port
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }
    // Uncomment the lines below to use these definitions:
    const char *serve_dir = argv[1];
    const char *port = argv[2];

    // TODO Complete the rest of this function

    // Socket Setup
    /*
    I have no idea if this works I just copied lecture slidees :p

    */
    //  Step 1: Set up address info
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;          // IPv4
    hints.ai_socktype = SOCK_STREAM;    // TCP
    hints.ai_flags = AI_PASSIVE;        // For binding (server)

    int ret_val = getaddrinfo(NULL, port, &hints, &res);
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

    // Optional: set SO_REUSEADDR
    int optval = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

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

    printf("Server is listening on port %s and serving directory %s\n", port, serve_dir);

    // You can now enter a loop to accept and handle connections...
    while (keep_going) {
        // main server loop would go here
    }

    // Cleanup
    close(sock_fd);
    return 0;
}
