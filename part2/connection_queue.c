#include "connection_queue.h"

#include <stdio.h>
#include <string.h>

/*
I took a lot of this from lab 12
*/

// Initialize the connection queue structure and its synchronization primitives
int connection_queue_init(connection_queue_t *queue) {
    // Start with an empty queue and default indices
    queue->length = 0;
    queue->read_idx = 0;
    queue->write_idx = 0;
    queue->shutdown = 0;

    // Initialize the mutex to protect the queue
    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        perror("pthread_mutex_init");
        return -1;
    }

    // Initialize the condition variable used when the queue is full
    if (pthread_cond_init(&queue->queue_full, NULL) != 0) {
        perror("pthread_cond_init (queue_full)");
        return -1;
    }

    // Initialize the condition variable used when the queue is empty
    if (pthread_cond_init(&queue->queue_empty, NULL) != 0) {
        perror("pthread_cond_init (queue_empty)");
        return -1;
    }

    return 0;
}

// Add a new client connection to the queue
int connection_queue_enqueue(connection_queue_t *queue, int connection_fd) {
    pthread_mutex_lock(&queue->lock);

    // If the queue is full, wait until space becomes available or shutdown is signaled
    while (queue->length == CAPACITY && !queue->shutdown) {
        pthread_cond_wait(&queue->queue_full, &queue->lock);
    }

    // If shutdown was triggered, exit early
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    // Add the connection to the circular buffer
    queue->client_fds[queue->write_idx] = connection_fd;
    queue->write_idx = (queue->write_idx + 1) % CAPACITY;
    queue->length++;

    // Signal a waiting thread that an item is available
    pthread_cond_signal(&queue->queue_empty);
    pthread_mutex_unlock(&queue->lock);

    return 0;
}

// Remove and return a client connection from the queue
int connection_queue_dequeue(connection_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);

    // If the queue is empty, wait until an item becomes available or shutdown is signaled
    while (queue->length == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->queue_empty, &queue->lock);
    }

    // If shutdown was triggered, exit early
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    // Retrieve a connection from the circular buffer
    int connection_fd = queue->client_fds[queue->read_idx];
    queue->read_idx = (queue->read_idx + 1) % CAPACITY;
    queue->length--;

    // Signal a waiting thread that space is available
    pthread_cond_signal(&queue->queue_full);
    pthread_mutex_unlock(&queue->lock);

    return connection_fd;
}

// Signal all threads to stop processing and shut down the queue
int connection_queue_shutdown(connection_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);

    // Set shutdown flag so threads can exit gracefully
    queue->shutdown = 1;

    // Wake up all threads waiting on either condition variable
    pthread_cond_broadcast(&queue->queue_empty);
    pthread_cond_broadcast(&queue->queue_full);

    pthread_mutex_unlock(&queue->lock);
    return 0;
}

// Clean up and release resources used by the queue
int connection_queue_free(connection_queue_t *queue) {
    pthread_mutex_destroy(&queue->lock);
    pthread_cond_destroy(&queue->queue_empty);
    pthread_cond_destroy(&queue->queue_full);
    return 0;
}
