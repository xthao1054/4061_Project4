#include "connection_queue.h"

#include <stdio.h>
#include <string.h>

/*
I took a lot of this from lab 12
*/

int connection_queue_init(connection_queue_t *queue) {
    // TODO Not yet implemented
    queue->length = 0;
    queue->read_idx = 0;
    queue->write_idx = 0;
    queue->shutdown = 0;

    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        perror("pthread_mutex_init");
        return -1;
    }

    if (pthread_cond_init(&queue->queue_full, NULL) != 0) {
        perror("pthread_cond_init (queue_full)");
        return -1;
    }

    if (pthread_cond_init(&queue->queue_empty, NULL) != 0) {
        perror("pthread_cond_init (queue_empty)");
        return -1;
    }

    return 0;
}

int connection_queue_enqueue(connection_queue_t *queue, int connection_fd) {
    // TODO Not yet implemented
    pthread_mutex_lock(&queue->lock);

    while (queue->length == CAPACITY && !queue->shutdown) {
        pthread_cond_wait(&queue->queue_full, &queue->lock);
    }

    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    queue->client_fds[queue->write_idx] = connection_fd;
    queue->write_idx = (queue->write_idx + 1) % CAPACITY;

    queue->length++;

    pthread_cond_signal(&queue->queue_empty);
    pthread_mutex_unlock(&queue->lock);

    return 0;
}

int connection_queue_dequeue(connection_queue_t *queue) {
    // TODO Not yet implemented
    pthread_mutex_lock(&queue->lock);

    while (queue->length == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->queue_empty, &queue->lock);
    }

    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    int connection_fd = queue->client_fds[queue->read_idx];
    queue->read_idx = (queue->read_idx + 1) % CAPACITY;
    queue->length--;

    pthread_cond_signal(&queue->queue_full);
    pthread_mutex_unlock(&queue->lock);

    return 0;
}

int connection_queue_shutdown(connection_queue_t *queue) {
    // TODO Not yet implemented
    pthread_mutex_lock(&queue->lock);
    queue->shutdown = 1;

    pthread_cond_broadcast(&queue->queue_empty);
    pthread_cond_broadcast(&queue->queue_full);
    pthread_mutex_unlock(&queue->lock);
    return 0;
}

int connection_queue_free(connection_queue_t *queue) {
    // TODO Not yet implemented
    pthread_mutex_destroy(&queue->lock);
    pthread_cond_destroy(&queue->queue_empty);
    pthread_cond_destroy(&queue->queue_full);
    return 0;
}
