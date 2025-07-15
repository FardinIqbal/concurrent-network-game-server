/**
 * @file player.c
 * @brief Thread-safe player management for the MazeWar server.
 *
 * This module manages player login/logout, state, communication,
 * view updates, scoring, and game events like laser hits.
 * Thread safety is ensured via recursive mutexes.
 * All PLAYER operations are reference-counted for memory safety.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "player.h"
#include "protocol.h"
#include "maze.h"
#include "debug.h"

#define MAX_PLAYERS 256

/**
 * @struct player
 * @brief Internal representation of a MazeWar player.
 *
 * This structure holds all server-side state for a logged-in MazeWar player.
 * All mutable fields must be accessed under the player's recursive mutex.
 * The `laser_hit` field is marked volatile and updated asynchronously by the SIGUSR1 signal handler.
 */
struct player {
    pthread_mutex_t mutex;        /**< Recursive mutex for player state. */
    int ref_count;                /**< Reference count for lifetime management. */
    int client_fd;                /**< Socket descriptor for this player. */
    OBJECT avatar;                /**< Character representing the player in the maze. */
    char *name;                   /**< Player name (malloc-allocated). */
    int row, col;                 /**< Current maze coordinates. */
    DIRECTION dir;                /**< Current gaze direction (NORTH, EAST, etc). */
    int score;                    /**< Player's score. */

    volatile sig_atomic_t laser_hit;  /**< Set to 1 when hit by a laser (signal-safe). */

    pthread_t thread_id;          /**< Thread ID servicing this player (for SIGUSR1). */
    char last_view[VIEW_DEPTH][VIEW_WIDTH]; /**< Cached view sent to client. */
    int view_valid_depth;         /**< Valid depth of cached view; -1 = no valid view. */
};



/// Global player map and its mutex.
static PLAYER *player_map[MAX_PLAYERS];
static pthread_mutex_t map_mutex = PTHREAD_MUTEX_INITIALIZER;
// Thread-local pointer to the PLAYER object for the current thread
__thread PLAYER *this_player = NULL;


/**
 * @brief Signal handler for SIGUSR1 (laser hit).
 *
 * When a player is hit by a laser, their thread receives a SIGUSR1 signal.
 * This handler sets the thread-local player's `laser_hit` flag so that
 * `player_check_for_laser_hit()` can detect it on the next loop iteration.
 */
static void handle_sigusr1(int sig __attribute__((unused))) {
    debug("SIGUSR1 handler: thread %lu, this_player = %p", pthread_self(), (void *)this_player);
    if (this_player != NULL) {
        this_player->laser_hit = 1;
        debug("SIGUSR1 handler: laser_hit set for %s[%c]", this_player->name, this_player->avatar);
    } else {
        debug("SIGUSR1 handler: this_player was NULL");
    }
}



/**
 * @brief Initialize the player module and install signal handler.
 */
void player_init(void) {
    struct sigaction sa = {0};
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    memset(player_map, 0, sizeof(player_map));
    debug("player_init: player module initialized.");
}

/**
 * @brief Finalize the player module, releasing all player resources.
 */
void player_fini(void) {
    pthread_mutex_lock(&map_mutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (player_map[i]) {
            debug("player_fini: unref player %p for avatar %d", player_map[i], i);
            player_unref(player_map[i], "player_fini");
        }
    }
    pthread_mutex_unlock(&map_mutex);
    debug("player_fini: player module finalized.");
}

/**
 * @brief Attempt to log in a player with a specified avatar.
 *
 * This function creates and initializes a PLAYER object, assigns it an avatar,
 * places it randomly into the maze, and registers it into the global player map.
 * Also sets up per-thread tracking (`this_player`) for SIGUSR1 hit detection.
 *
 * @param clientfd File descriptor of the client's socket connection.
 * @param avatar   Desired avatar character for the player.
 * @param name     User's name (copied internally).
 * @return Pointer to PLAYER object on success, or NULL on failure.
 */
PLAYER *player_login(int clientfd, OBJECT avatar, char *name) {
    pthread_mutex_lock(&map_mutex);

    if (player_map[avatar]) {
        pthread_mutex_unlock(&map_mutex);
        debug("player_login: Avatar %c already in use", avatar);
        return NULL;
    }

    PLAYER *player = calloc(1, sizeof(PLAYER));
    if (!player) {
        pthread_mutex_unlock(&map_mutex);
        error("player_login: Memory allocation failed");
        return NULL;
    }

    // Initialize recursive mutex for safe reentrant locking
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&player->mutex, &attr);

    player->ref_count = 1;
    player->client_fd = clientfd;
    player->avatar = avatar;
    player->name = strdup(name ? name : "Anonymous");
    player->dir = NORTH;
    player->view_valid_depth = -1;

    // Attempt to place the avatar randomly into the maze
    if (maze_set_player_random(avatar, &player->row, &player->col) != 0) {
        debug("player_login: Failed to place avatar %c in maze", avatar);
        free(player->name);
        pthread_mutex_destroy(&player->mutex);
        free(player);
        pthread_mutex_unlock(&map_mutex);
        return NULL;
    }

    // Record the thread ID and set thread-local SIGUSR1 player context
    player->thread_id = pthread_self();
    this_player = player;
    debug("player_login: this_player set to %p for thread %lu", (void *)player, player->thread_id);

    // Register player in global map
    player_map[avatar] = player;

    pthread_mutex_unlock(&map_mutex);
    success("player_login: %s[%c] logged in", player->name, avatar);
    return player;
}




/**
 * @brief Log out a player and remove from the map and maze.
 * @param player Player to log out.
 */
void player_logout(PLAYER *player) {
    pthread_mutex_lock(&map_mutex);
    if (player_map[player->avatar] == player)
        player_map[player->avatar] = NULL;
    pthread_mutex_unlock(&map_mutex);

    maze_remove_player(player->avatar, player->row, player->col);

    // Notify client to remove score from scoreboard
    MZW_PACKET pkt = { .type = MZW_SCORE_PKT, .param1 = player->avatar, .param2 = -1 };
    player_send_packet(player, &pkt, NULL);

    debug("player_logout: Player %s[%c] logged out", player->name, player->avatar);
    player_unref(player, "logout");
}

/**
 * @brief Get a reference to the player object for a given avatar.
 * @param avatar Avatar character.
 * @return PLAYER* with incremented ref count, or NULL if not found.
 */
PLAYER *player_get(unsigned char avatar) {
    pthread_mutex_lock(&map_mutex);
    PLAYER *p = player_map[avatar];
    if (p) player_ref(p, "get");
    pthread_mutex_unlock(&map_mutex);
    return p;
}

/**
 * @brief Increment reference count on player.
 * @param player Player to reference.
 * @param why    Reason for ref (for debug).
 * @return PLAYER* (same as input).
 */
PLAYER *player_ref(PLAYER *player, char *why) {
    pthread_mutex_lock(&player->mutex);
    player->ref_count++;
    debug("player_ref: %p -> %d (%s)", player, player->ref_count, why);
    pthread_mutex_unlock(&player->mutex);
    return player;
}

/**
 * @brief Decrement reference count and free player if zero.
 * @param player Player to unref.
 * @param why    Reason for unref (for debug).
 */
void player_unref(PLAYER *player, char *why) {
    pthread_mutex_lock(&player->mutex);
    player->ref_count--;
    debug("player_unref: %p -> %d (%s)", player, player->ref_count, why);

    if (player->ref_count == 0) {
        free(player->name);
        pthread_mutex_unlock(&player->mutex);
        pthread_mutex_destroy(&player->mutex);
        free(player);
        debug("player_unref: Freed player object");
        return;
    }
    pthread_mutex_unlock(&player->mutex);
}

/**
 * @brief Thread-safe send of a packet to the player's client.
 * @param player Player to send to.
 * @param pkt    Packet to send.
 * @param data   Optional payload.
 * @return 0 on success, nonzero on error.
 */
int player_send_packet(PLAYER *player, MZW_PACKET *pkt, void *data) {
    pthread_mutex_lock(&player->mutex);
    int rc = proto_send_packet(player->client_fd, pkt, data);
    pthread_mutex_unlock(&player->mutex);
    return rc;
}

/**
 * @brief Get current location and direction for a player.
 * @param player Player to query.
 * @param rowp   [out] Row index.
 * @param colp   [out] Column index.
 * @param dirp   [out] Direction.
 * @return 0 if valid, nonzero otherwise.
 */
int player_get_location(PLAYER *player, int *rowp, int *colp, int *dirp) {
    pthread_mutex_lock(&player->mutex);
    *rowp = player->row;
    *colp = player->col;
    *dirp = player->dir;
    pthread_mutex_unlock(&player->mutex);
    return 0;
}

/**
 * @brief Move the player's avatar forward or backward.
 * @param player Player to move.
 * @param sign   1 for forward, -1 for backward.
 * @return 0 on success, nonzero on error.
 */
int player_move(PLAYER *player, int sign) {
    pthread_mutex_lock(&player->mutex);
    DIRECTION move_dir = (sign == -1) ? REVERSE(player->dir) : player->dir;
    int rc = maze_move(player->row, player->col, move_dir);
    if (rc == 0) {
        // Update coordinates on successful move
        if (move_dir == NORTH) player->row--;
        else if (move_dir == SOUTH) player->row++;
        else if (move_dir == WEST)  player->col--;
        else if (move_dir == EAST)  player->col++;
    }
    pthread_mutex_unlock(&player->mutex);

    // Update all player views after move
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (player_map[i]) player_update_view(player_map[i]);

    debug("player_move: Player %p moved %s", player, (sign == 1) ? "forward" : "backward");
    return rc;
}

/**
 * @brief Rotate the player's gaze.
 * @param player Player to rotate.
 * @param dir    1 for CCW, -1 for CW.
 */
void player_rotate(PLAYER *player, int dir) {
    pthread_mutex_lock(&player->mutex);
    player->dir = (dir == 1) ? TURN_LEFT(player->dir) : TURN_RIGHT(player->dir);
    player_invalidate_view(player);
    pthread_mutex_unlock(&player->mutex);

    player_update_view(player);
    debug("player_rotate: Player %p rotated %s", player, (dir == 1) ? "CCW" : "CW");
}

/**
 * @brief Invalidate the player's cached view.
 * @param player Player whose view is invalidated.
 */
void player_invalidate_view(PLAYER *player) {
    player->view_valid_depth = -1;
    debug("player_invalidate_view: Player %p view invalidated", player);
}

/**
 * @brief Update the player's view based on current maze state.
 * @param player Player to update.
 */
void player_update_view(PLAYER *player) {
    char view[VIEW_DEPTH][VIEW_WIDTH];
    pthread_mutex_lock(&player->mutex);
    int depth = maze_get_view((VIEW *)view, player->row, player->col, player->dir, VIEW_DEPTH);

    if (player->view_valid_depth < 0) {
        // Full update: send CLEAR then SHOW for all cells
        MZW_PACKET clear = { .type = MZW_CLEAR_PKT };
        player_send_packet(player, &clear, NULL);

        for (int d = 0; d < depth; d++) {
            for (int x = 0; x < VIEW_WIDTH; x++) {
                MZW_PACKET show = {
                    .type = MZW_SHOW_PKT,
                    .param1 = view[d][x],
                    .param2 = x,
                    .param3 = d
                };
                player_send_packet(player, &show, NULL);
            }
        }
    } else {
        // Incremental update: only send changed cells
        for (int d = 0; d < depth; d++) {
            for (int x = 0; x < VIEW_WIDTH; x++) {
                if (view[d][x] != player->last_view[d][x]) {
                    MZW_PACKET show = {
                        .type = MZW_SHOW_PKT,
                        .param1 = view[d][x],
                        .param2 = x,
                        .param3 = d
                    };
                    player_send_packet(player, &show, NULL);
                }
            }
        }
    }
    memcpy(player->last_view, view, sizeof(view));
    player->view_valid_depth = depth;
    pthread_mutex_unlock(&player->mutex);
    debug("player_update_view: Player %p view updated", player);
}

/**
 * @brief Fire the player's laser in the direction of gaze.
 *
 * This function searches in the current direction of gaze to find the first
 * avatar (if any) in the laser's path. If a player is hit, the victim's thread
 * is notified via SIGUSR1, the shooter's score is incremented, and a SCORE
 * packet is broadcast to all clients. The victim will detect the signal,
 * process the hit, and eventually respawn.
 *
 * @param player The player firing the laser.
 */
void player_fire_laser(PLAYER *player) {
    // Lock to safely access firing player's direction and position
    pthread_mutex_lock(&player->mutex);
    OBJECT target = maze_find_target(player->row, player->col, player->dir);
    pthread_mutex_unlock(&player->mutex);

    if (!IS_AVATAR(target)) {
        debug("player_fire_laser: No avatar hit");
        return;
    }

    // Lookup victim's player object
    PLAYER *victim = player_get(target);
    if (!victim) {
        debug("player_fire_laser: Target avatar not found in player_map");
        return;
    }

    // Tag the victim and signal their thread to interrupt blocking recv
    pthread_mutex_lock(&victim->mutex);
    victim->laser_hit = 1;
    pthread_mutex_unlock(&victim->mutex);

    // Send SIGUSR1 to victim's actual thread
    pthread_kill(victim->thread_id, SIGUSR1);

    // Increment shooter's score
    pthread_mutex_lock(&player->mutex);
    player->score++;
    int score = player->score;
    pthread_mutex_unlock(&player->mutex);

    // Broadcast updated score
    MZW_PACKET pkt = {
        .type = MZW_SCORE_PKT,
        .param1 = player->avatar,
        .param2 = score
    };

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (player_map[i]) {
            player_send_packet(player_map[i], &pkt, NULL);
        }
    }

    // Release victim player reference
    player_unref(victim, "fire_laser");
    debug("player_fire_laser: %c hit %c; new score=%d",
          player->avatar, target, score);
}



/**
 * @brief Check whether a player has been hit by a laser and handle consequences.
 *
 * This function should be called at the top of the client service loop.
 * If the player has been marked as hit (via SIGUSR1), this function will:
 *  - Clear the hit flag
 *  - Remove the player from the maze
 *  - Broadcast updated views to all clients
 *  - Send an ALERT packet to the hit client
 *  - Sleep for 3 seconds (purgatory)
 *  - Respawn the player at a new location and update scoreboards
 *
 * @param player Pointer to the PLAYER object being serviced.
 */
void player_check_for_laser_hit(PLAYER *player) {
    debug("player_check_for_laser_hit: Checking hit status for player %s[%c]",
          player->name, player->avatar);

    // Step 1: Check and clear the laser hit flag safely
    pthread_mutex_lock(&player->mutex);
    int hit = player->laser_hit;
    if (hit) {
        player->laser_hit = 0;
        debug("player_check_for_laser_hit: Laser hit detected for %s[%c]",
              player->name, player->avatar);
    }
    pthread_mutex_unlock(&player->mutex);

    if (!hit) {
        debug("player_check_for_laser_hit: No hit to process for %s[%c]",
              player->name, player->avatar);
        return;
    }

    // Step 2: Remove the player from their current location in the maze
    pthread_mutex_lock(&player->mutex);
    int row = player->row;
    int col = player->col;
    pthread_mutex_unlock(&player->mutex);

    maze_remove_player(player->avatar, row, col);
    debug("player_check_for_laser_hit: Removed %s[%c] from maze location [%d,%d]",
          player->name, player->avatar, row, col);

    // Step 3: Update all player views to reflect removal
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (player_map[i]) {
            player_update_view(player_map[i]);
        }
    }

    // Step 4: Send ALERT packet to this player
    MZW_PACKET alert = { .type = MZW_ALERT_PKT };
    player_send_packet(player, &alert, NULL);
    debug("player_check_for_laser_hit: Sent ALERT to %s[%c]",
          player->name, player->avatar);

    // Step 5: Sleep for purgatory duration
    sleep(3);

    // Step 6: Reset player (new location, zero score, scoreboard/view updates)
    player_reset(player);
    debug("player_check_for_laser_hit: Respawned %s[%c] successfully",
          player->name, player->avatar);
}



/**
 * @brief Reset a player after being hit by a laser or logging in.
 *
 * This function handles repositioning the player after death or login.
 * It performs the following:
 *   1. Removes the player from their current position in the maze.
 *   2. Resets the player's score to 0.
 *   3. Places the player at a random unoccupied location in the maze.
 *   4. Sends all other players' scores to this player (to populate scoreboard).
 *   5. Broadcasts this player's score to all players (score reset).
 *   6. Triggers a full view update for all players.
 *
 * If maze placement fails (e.g. full maze), the function logs the error and returns
 * without closing the socket — it's the service thread's job to handle termination.
 *
 * @param player Pointer to the PLAYER object to reset.
 */
/**
 * @brief Reset a player after being hit or on login.
 *
 * Removes the player from their current maze position and respawns them
 * in a new random empty location. Updates all player views and scoreboards.
 *
 * @param player Pointer to the PLAYER object being reset.
 */
void player_reset(PLAYER *player) {
    pthread_mutex_lock(&player->mutex);

    // Step 1: Save old position and remove player from maze
    int old_row = player->row;
    int old_col = player->col;
    maze_remove_player(player->avatar, old_row, old_col);
    debug("player_reset: Removed %c from [%d,%d]", player->avatar, old_row, old_col);

    // Step 2: Place player in a new random unoccupied location
    if (maze_set_player_random(player->avatar, &player->row, &player->col) != 0) {
        error("player_reset: Failed to place %c in maze — maze may be full", player->avatar);
        pthread_mutex_unlock(&player->mutex);
        return;
    }
    debug("player_reset: Re-placed %c at [%d,%d]", player->avatar, player->row, player->col);

    // Step 3: Reset score
    player->score = 0;

    pthread_mutex_unlock(&player->mutex);

    // Step 4: Send all other scores to this player
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (player_map[i] && player_map[i] != player) {
            MZW_PACKET pkt = {
                .type = MZW_SCORE_PKT,
                .param1 = player_map[i]->avatar,
                .param2 = player_map[i]->score
            };
            player_send_packet(player, &pkt, NULL);
        }
    }

    // Step 5: Broadcast this player's score reset to all players
    MZW_PACKET pkt = {
        .type = MZW_SCORE_PKT,
        .param1 = player->avatar,
        .param2 = 0
    };
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (player_map[i]) {
            player_send_packet(player_map[i], &pkt, NULL);
        }
    }

    // Step 6: Update all player views
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (player_map[i]) {
            player_update_view(player_map[i]);
        }
    }
}




/**
 * @brief Broadcast a chat message from a player to all players.
 * @param player Player sending the message.
 * @param msg    Message data (not null-terminated).
 * @param len    Length of message.
 */
void player_send_chat(PLAYER *player, char *msg, size_t len) {
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "%s[%c] %.*s", player->name, player->avatar, (int)len, msg);
    MZW_PACKET pkt = { .type = MZW_CHAT_PKT, .size = n };

    for (int i = 0; i < MAX_PLAYERS; i++)
        if (player_map[i]) player_send_packet(player_map[i], &pkt, buf);

    debug("player_send_chat: Player %p broadcast chat", player);
}
