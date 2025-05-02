#define _GNU_SOURCE

#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "connection_queue.h"
#include "http.h"

#define BUFSIZE 512
#define LISTEN_QUEUE_LEN 5
#define N_THREADS 5
#define HOST "127.0.0.1"

int keep_going = 1;
const char *serve_dir;
// Thread-safe queue for client connections
connection_queue_t conn_queue;

// Handle SIGINT to shut down the server
void handle_sigint(int signo) {
    keep_going = 0;
    connection_queue_shutdown(&conn_queue);
}

// Worker thread function to process one client connection at a time
void *worker_thread(void *arg) {
    int client_fd;
    // Keep working while connections are available in the queue
    while ((client_fd = connection_queue_dequeue(&conn_queue)) != -1) {
        char resource_name[BUFSIZE];

        // Try to read an HTTP request from the client
        if (read_http_request(client_fd, resource_name) == -1) {
            // If invalid request, close the connection
            fprintf(stderr, "Error: Failed to read HTTP request\n");
            close(client_fd);
        } else {
            // Build the full path to the requested file
            char full_path[BUFSIZE * 2];
            if (snprintf(full_path, sizeof(full_path), "%s%s", serve_dir, resource_name) >=
                sizeof(full_path)) {
                fprintf(stderr, "Error: Full path too long, closing connection\n");
                close(client_fd);
                continue;
            }

            // Send the HTTP response with the requested file
            write_http_response(client_fd, full_path);

            // Close connection
            close(client_fd);
        }
    }

    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }

    // Set the directory to serve files from
    serve_dir = argv[1];
    // Port to listen on
    const char *port = argv[2];

    // Block all signals before starting worker threads
    sigset_t fullset, oldset;
    sigfillset(&fullset);
    if (pthread_sigmask(SIG_BLOCK, &fullset, &oldset) != 0) {
        perror("pthread_sigmask");
        return 1;
    }

    // Set up SIGINT handler for graceful shutdown
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    // Initialize the connection queue
    if (connection_queue_init(&conn_queue) == -1) {
        perror("Failed to initialize connection queue");
        return 1;
    }

    // Start worker threads
    pthread_t threads[N_THREADS];
    for (int i = 0; i < N_THREADS; ++i) {
        if (pthread_create(&threads[i], NULL, worker_thread, NULL) != 0) {
            perror("Failed to create thread");
            // Clean up already created threads
            for (int j = 0; j < i; ++j) {
                pthread_cancel(threads[j]);
                pthread_join(threads[j], NULL);
            }
            connection_queue_free(&conn_queue);
            return 1;
        }
    }

    // Restore signal mask in main thread
    if (pthread_sigmask(SIG_SETMASK, &oldset, NULL) != 0) {
        perror("pthread_sigmask restore");
        return 1;
    }

    // Set up server socket using getaddrinfo
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int ret_val = getaddrinfo(HOST, port, &hints, &res);
    if (ret_val != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret_val));
        return 1;
    }

    // Create the listening socket
    int listen_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_fd == -1) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    // Set socket options to allow address reuse
    int optval = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(listen_fd);
        freeaddrinfo(res);
        return 1;
    }

    // Bind the socket to the specified address and port
    if (bind(listen_fd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("bind");
        close(listen_fd);
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);

    // Start listening for incoming connections
    if (listen(listen_fd, LISTEN_QUEUE_LEN) == -1) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    // Main accept loop
    while (keep_going == 1) {
        int client_fd = accept(listen_fd, NULL, NULL);
        int should_continue = 1;

        // Check for error or signal interruption
        if (client_fd == -1) {
            if (errno == EINTR || !keep_going) {
                // Stop accepting new connections if interrupted
                should_continue = 0;
                keep_going = 0;
            } else {
                // Other errors: log and continue
                perror("accept");
                should_continue = 1;
            }
        }

        // Add client socket to the queue for workers to handle
        if (should_continue && client_fd != -1) {
            if (connection_queue_enqueue(&conn_queue, client_fd) == -1) {
                // Drop the connection if queue is shut down
                fprintf(stderr, "Failed to enqueue client connection\n");
                close(client_fd);
            }
        }
    }

    // Close listening socket
    close(listen_fd);

    // Wait for all worker threads to finish
    for (int i = 0; i < N_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    // Free resources used by the connection queue
    connection_queue_free(&conn_queue);
    return 0;
}
