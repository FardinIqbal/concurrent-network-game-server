/**
 * @file main.c
 * @brief Entry point for the MazeWar multi-threaded server.
 *
 * This file sets up a TCP server that listens for MazeWar client connections.
 * Each client is handled by a dedicated service thread. The server can be
 * gracefully terminated via a SIGHUP signal.
 *
 * Responsibilities:
 * - Parse command-line arguments.
 * - Load maze template from file or use default.
 * - Initialize modules: client registry, maze, player.
 * - Create TCP socket and accept incoming clients.
 * - Spawn a thread per client using mzw_client_service().
 * - Handle SIGHUP to shut down cleanly.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "client_registry.h"
#include "maze.h"
#include "player.h"
#include "debug.h"
#include "server.h"

static void terminate(int status);
static void handle_sighup(int sig);

extern CLIENT_REGISTRY *client_registry;

static char *default_maze[] = {
    "******************************",
    "***** %%%%%%%%% &&&&&&&&&&& **",
    "***** %%%%%%%%%        $$$$  *",
    "*           $$$$$$ $$$$$$$$$ *",
    "*##########                  *",
    "*########## @@@@@@@@@@@@@@@@@*",
    "*           @@@@@@@@@@@@@@@@@*",
    "******************************",
    NULL
};

static int listenfd;  // Listening socket for the server

int main(int argc, char *argv[]) {
    int opt, port = -1;
    char *template_file = NULL;

    // Parse command-line arguments: -p <port> [-t <template_file>]
    while ((opt = getopt(argc, argv, "p:t:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 't':
                template_file = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s -p <port> [-t <template_file>]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (port <= 0) {
        fprintf(stderr, "Error: You must specify a valid port using -p <port>\n");
        exit(EXIT_FAILURE);
    }

    // Install SIGHUP handler to trigger graceful shutdown
    struct sigaction sa;
    sa.sa_handler = handle_sighup;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGHUP, &sa, NULL);

    // Initialize global modules
    client_registry = creg_init();

    if (template_file) {
        FILE *fp = fopen(template_file, "r");
        if (!fp) {
            perror("Error opening maze template");
            exit(EXIT_FAILURE);
        }

        char **lines = calloc(64, sizeof(char *));
        size_t cap = 64, len = 0;
        char buffer[256];

        while (fgets(buffer, sizeof(buffer), fp)) {
            buffer[strcspn(buffer, "\n")] = '\0';  // Strip newline
            lines[len++] = strdup(buffer);
            if (len >= cap) {
                cap *= 2;
                lines = realloc(lines, cap * sizeof(char *));
            }
        }
        lines[len] = NULL;
        fclose(fp);

        maze_init(lines);
        for (size_t i = 0; i < len; i++) free(lines[i]);
        free(lines);
    } else {
        maze_init(default_maze);
    }

    player_init();
    debug_show_maze = 1;  // Enable maze display after each action (DEBUG mode)

    // Create listening socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, 32) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Accept client connections and spawn handler threads
    while (1) {
        int *clientfd = malloc(sizeof(int));
        if (!clientfd) continue;

        *clientfd = accept(listenfd, NULL, NULL);
        if (*clientfd < 0) {
            free(clientfd);
            continue;
        }

        debug("Accepted client fd=%d", *clientfd);

        pthread_t tid;
        if (pthread_create(&tid, NULL, mzw_client_service, clientfd) != 0) {
            perror("pthread_create");
            close(*clientfd);
            free(clientfd);
            continue;
        }
        pthread_detach(tid);  // Reap thread automatically on exit
    }

    // Should never reach here
    terminate(EXIT_SUCCESS);
}

/**
 * @brief Signal handler for SIGHUP.
 * Triggers a clean server shutdown with status code 0.
 */
static void handle_sighup(int sig) {
    (void)sig;
    terminate(EXIT_SUCCESS);
}

/**
 * @brief Cleanly shuts down the MazeWar server and all active threads.
 * Closes listening socket, shuts down all clients, finalizes all modules.
 * @param status The exit code to terminate the program with.
 */
static void terminate(int status) {
    if (listenfd > 0) close(listenfd);

    creg_shutdown_all(client_registry);
    debug("Waiting for service threads to terminate...");
    creg_wait_for_empty(client_registry);
    debug("All service threads terminated.");

    creg_fini(client_registry);
    player_fini();
    maze_fini();

    debug("MazeWar server terminating");
    exit(status);
}
