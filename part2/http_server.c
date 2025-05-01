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
connection_queue_t conn_queue;    // Global connection queue

void handle_sigint(int signo) {
    keep_going = 0;
    connection_queue_shutdown(&conn_queue);
}

// Worker thread function
void *worker_thread(void *arg) {
    while (1) {
        int client_fd = connection_queue_dequeue(&conn_queue);
        if (client_fd == -1) {
            break;    // Queue shutdown
        }

        char resource_name[BUFSIZE];

        // Step 1: Read HTTP request
        if (read_http_request(client_fd, resource_name) == -1) {
            close(client_fd);
            continue;
        }

        // Step 2: Build full file path
        char full_path[BUFSIZE * 2];
        snprintf(full_path, sizeof(full_path), "%s%s", serve_dir, resource_name);

        // Step 3: Write HTTP response
        write_http_response(client_fd, full_path);

        // Step 4: Close connection
        close(client_fd);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }

    serve_dir = argv[1];
    const char *port = argv[2];

    // Install signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    // Step 1: Block all signals before thread creation
    sigset_t fullset, oldset;
    sigfillset(&fullset);
    pthread_sigmask(SIG_BLOCK, &fullset, &oldset);  // Block signals in main thread

    // Initialize connection queue
    if (connection_queue_init(&conn_queue) == -1) {
        perror("Failed to initialize connection queue");
        return 1;
    }

    // Create thread pool (signals blocked)
    pthread_t threads[N_THREADS];
    for (int i = 0; i < N_THREADS; ++i) {
        if (pthread_create(&threads[i], NULL, worker_thread, NULL) != 0) {
            perror("Failed to create thread");
            return 1;
        }
    }

    // Step 2: Restore main thread signal mask
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);

    // Set up socket
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

    int listen_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_fd == -1) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(listen_fd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("bind");
        close(listen_fd);
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);

    if (listen(listen_fd, LISTEN_QUEUE_LEN) == -1) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    // Accept loop
    while (keep_going) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EINTR)
                break;    // Graceful exit
            perror("accept");
            continue;
        }

        if (connection_queue_enqueue(&conn_queue, client_fd) == -1) {
            close(client_fd);    // Drop if queue shut down
        }
    }

    close(listen_fd);

    // Wait for worker threads to finish
    for (int i = 0; i < N_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    connection_queue_free(&conn_queue);
    return 0;
}
