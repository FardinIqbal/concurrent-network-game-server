/**
 * @file client_registry.c
 * @brief Thread-safe registry for tracking active client connections in the MazeWar server.
 *
 * This module keeps track of all connected clients by storing their file descriptors (fd),
 * which are unique integer IDs assigned to each network connection by the operating system.
 *
 * What this module does:
 * - Keeps a list of all clients currently connected to the server.
 * - Lets new client threads "register" their connection when they join.
 * - Lets client threads "unregister" their connection when they leave.
 * - Allows the main thread to wait until all clients have disconnected.
 * - Can shut down all client connections (e.g., during server termination).
 *
 * Why this is needed:
 * - The server handles multiple clients at the same time using threads.
 * - Threads must not interfere with each other (race conditions).
 * - We use a mutex (lock) and semaphore to make this system safe and predictable.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include "client_registry.h"
#include "debug.h"  // Enables debug logging

#define MAX_CLIENTS 128  // Maximum number of clients the server can track at once

/**
 * Internal structure to track all active clients.
 * This is shared across threads, so it uses synchronization tools.
 */
struct client_registry {
    int client_fds[MAX_CLIENTS];   // Array that stores each client's socket fd (or -1 if unused)
    int count;                     // Total number of currently connected clients
    pthread_mutex_t mutex;        // Lock to protect shared access to the array and count
    sem_t empty;                  // Used to notify when all clients have disconnected
};

// Global pointer to the main client registry instance (set in main.c)
CLIENT_REGISTRY *client_registry = NULL;

/**
 * Create and initialize a new client registry.
 * Sets all client slots to "empty" (-1) and prepares the locking mechanisms.
 *
 * @return Pointer to a new CLIENT_REGISTRY, or NULL if memory allocation fails.
 */
CLIENT_REGISTRY *creg_init() {
    CLIENT_REGISTRY *cr = malloc(sizeof(CLIENT_REGISTRY));
    if (cr == NULL) {
        debug("creg_init: Failed to allocate memory for client registry.");
        return NULL;
    }

    // Mark all fd slots as unused (-1 means "no client here")
    memset(cr->client_fds, -1, sizeof(cr->client_fds));
    cr->count = 0;

    // Initialize lock and signal
    pthread_mutex_init(&cr->mutex, NULL);
    sem_init(&cr->empty, 0, 0);

    debug("creg_init: Client registry initialized.");
    return cr;
}

/**
 * Destroy the client registry and clean up resources.
 * Should only be called when no clients are connected.
 *
 * @param cr The client registry to destroy.
 */
void creg_fini(CLIENT_REGISTRY *cr) {
    if (cr == NULL) return;

    pthread_mutex_destroy(&cr->mutex);
    sem_destroy(&cr->empty);
    free(cr);

    debug("creg_fini: Client registry finalized and memory freed.");
}

/**
 * Register a new client.
 * Adds the client's file descriptor to the registry and increments the client count.
 * Safe to call from multiple threads at once.
 *
 * @param cr Pointer to the client registry.
 * @param fd The file descriptor for the new client.
 */
void creg_register(CLIENT_REGISTRY *cr, int fd) {
    pthread_mutex_lock(&cr->mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cr->client_fds[i] == -1) {
            cr->client_fds[i] = fd;
            cr->count++;
            debug("creg_register: Registered fd=%d (count=%d)", fd, cr->count);
            break;
        }
    }

    pthread_mutex_unlock(&cr->mutex);
}

/**
 * Unregister a client.
 * Removes the client's file descriptor and decrements the client count.
 * If no clients are left, signals the main thread that the registry is now empty.
 *
 * @param cr Pointer to the client registry.
 * @param fd The file descriptor of the client to remove.
 */
void creg_unregister(CLIENT_REGISTRY *cr, int fd) {
    pthread_mutex_lock(&cr->mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cr->client_fds[i] == fd) {
            cr->client_fds[i] = -1;
            cr->count--;
            debug("creg_unregister: Unregistered fd=%d (count=%d)", fd, cr->count);
            break;
        }
    }

    if (cr->count == 0) {
        debug("creg_unregister: No more clients connected, signaling empty.");
        sem_post(&cr->empty);
    }

    pthread_mutex_unlock(&cr->mutex);
}

/**
 * Block the calling thread until all clients have disconnected.
 * Usually used by the main thread to wait before shutdown.
 *
 * @param cr Pointer to the client registry.
 */
void creg_wait_for_empty(CLIENT_REGISTRY *cr) {
    pthread_mutex_lock(&cr->mutex);
    int empty = (cr->count == 0);
    pthread_mutex_unlock(&cr->mutex);

    if (!empty) {
        debug("creg_wait_for_empty: Waiting for all clients to disconnect...");
        sem_wait(&cr->empty);  // Will block until creg_unregister posts to the semaphore
        debug("creg_wait_for_empty: All clients have disconnected.");
    } else {
        debug("creg_wait_for_empty: No clients connected, skipping wait.");
    }
}

/**
 * Gracefully shut down all active clients.
 * This will "cut off" incoming communication from every client socket.
 * Each client thread will detect the shutdown and begin exiting.
 *
 * @param cr Pointer to the client registry.
 */
void creg_shutdown_all(CLIENT_REGISTRY *cr) {
    pthread_mutex_lock(&cr->mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cr->client_fds[i] != -1) {
            debug("creg_shutdown_all: Shutting down fd=%d", cr->client_fds[i]);
            shutdown(cr->client_fds[i], SHUT_RD);  // Disable read-side of the connection
        }
    }

    pthread_mutex_unlock(&cr->mutex);
    debug("creg_shutdown_all: All client fds shut down.");
}
