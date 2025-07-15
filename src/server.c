/**
 * @file server.c
 * @brief Thread routine for handling an individual MazeWar client connection.
 *
 * Each client is serviced by a dedicated thread running this function. The thread
 * initializes itself, registers the client with the server, and then continuously
 * processes incoming packets until the client disconnects or an error occurs.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <arpa/inet.h>

#include "server.h"
#include "protocol.h"
#include "player.h"
#include "client_registry.h"
#include "debug.h"

int debug_show_maze = 1;
extern __thread PLAYER *this_player;

/**
 * @brief Thread function to handle a connected MazeWar client.
 *
 * Responsibilities:
 * 1. Detach the thread to allow automatic cleanup.
 * 2. Extract and register the client socket with the global registry.
 * 3. Wait for incoming packets in a loop, handle each by type.
 * 4. On disconnect or error, log out the player, unregister the client, and close the socket.
 *
 * @param arg Pointer to malloc'd int (client fd). Caller must free it.
 * @return Always NULL.
 */
void *mzw_client_service(void *arg) {
    debug("Running My Version");

    // Step 1: Extract client socket file descriptor
    int client_fd = *((int *) arg);
    free(arg);

    // Step 2: Detach thread for automatic cleanup
    pthread_detach(pthread_self());
    debug("mzw_client_service: Client service thread started for fd=%d", client_fd);

    // Step 3: Register the client file descriptor in the global registry
    creg_register(client_registry, client_fd);

    PLAYER *player = NULL;
    int logged_in = 0;

    // Step 4: Main service loop
    while (1) {
        //  Process laser hit from *previous* signal before recv
        if (this_player) {
            player_check_for_laser_hit(this_player);
        }

        MZW_PACKET pkt;
        void *data = NULL;

        // Attempt to receive the next packet (may be interrupted by SIGUSR1)
        if (proto_recv_packet(client_fd, &pkt, &data) < 0) {
            debug("mzw_client_service: Disconnection or error from fd=%d", client_fd);
            break;
        }

        // Process laser hit that may have occurred during the blocking recv
        if (this_player) {
            player_check_for_laser_hit(this_player);
        }

        debug("mzw_client_service: Received packet type=%d from fd=%d", pkt.type, client_fd);

        // Step 5: Handle packet types
        switch (pkt.type) {
            case MZW_LOGIN_PKT:
                if (logged_in) {
                    debug("mzw_client_service: Ignoring duplicate LOGIN from fd=%d", client_fd);
                    break;
                }

                // Extract login info
                OBJECT avatar = pkt.param1;
                char *username = data;

                debug("mzw_client_service: Attempting login for fd=%d as '%s' (avatar=%d)",
                      client_fd, username, avatar);

                // Try logging in
                player = player_login(client_fd, avatar, username);
                this_player = player; // Explicitly set thread-local player pointer
                debug("mzw_client_service: this_player set to %p for thread %lu", (void *)player, pthread_self());

                if (!player) {
                    debug("mzw_client_service: Login failed for avatar=%d", avatar);
                    MZW_PACKET response = { .type = MZW_INUSE_PKT, .size = 0 };
                    proto_send_packet(client_fd, &response, NULL);
                    break;
                }

                // Successful login
                logged_in = 1;
                MZW_PACKET response = { .type = MZW_READY_PKT, .size = 0 };
                proto_send_packet(client_fd, &response, NULL);
                player_reset(player);
                debug("mzw_client_service: Login succeeded for '%s' (fd=%d)", username, client_fd);
                break;

            case MZW_MOVE_PKT:
                if (logged_in) {
                    debug("mzw_client_service: MOVE command from fd=%d", client_fd);
                    player_move(player, pkt.param1);
                }
                break;

            case MZW_TURN_PKT:
                if (logged_in) {
                    debug("mzw_client_service: TURN command from fd=%d", client_fd);
                    player_rotate(player, pkt.param1);
                }
                break;

            case MZW_FIRE_PKT:
                if (logged_in) {
                    debug("mzw_client_service: FIRE command from fd=%d", client_fd);
                    player_fire_laser(player);
                }
                break;

            case MZW_REFRESH_PKT:
                if (logged_in) {
                    debug("mzw_client_service: REFRESH command from fd=%d", client_fd);
                    player_invalidate_view(player);
                    player_update_view(player);
                }
                break;

            case MZW_SEND_PKT:
                if (logged_in && data != NULL) {
                    debug("mzw_client_service: SEND chat from fd=%d", client_fd);
                    player_send_chat(player, data, pkt.size);
                }
                break;

            default:
                debug("mzw_client_service: Unknown or unhandled packet type=%d from fd=%d",
                      pkt.type, client_fd);
                break;
        }

        // Always free packet payload if allocated
        if (data != NULL) {
            free(data);
        }
    }

    // Step 6: Client has disconnected or errored out — clean up
    if (player != NULL) {
        debug("mzw_client_service: Logging out player on fd=%d", client_fd);
        player_logout(player);
    }

    creg_unregister(client_registry, client_fd);
    close(client_fd);
    debug("mzw_client_service: Thread exiting for fd=%d", client_fd);
    return NULL;
}
