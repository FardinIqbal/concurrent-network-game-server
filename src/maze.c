/**
 * @file maze.c
 * @brief Thread-safe maze module for MazeWar server.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <ctype.h>
#include <time.h>

#include "maze.h"
#include "debug.h"

static char **maze = NULL;
static int maze_rows = 0;
static int maze_cols = 0;
static pthread_mutex_t maze_mutex;

/**
 * @brief Initialize the maze from a template.
 *
 * This function initializes the global maze grid based on a NULL-terminated
 * array of strings (each row). It allocates internal memory, determines dimensions,
 * and sets up the maze mutex for thread-safe operations. It also seeds the
 * random number generator for future randomized placement (e.g., respawning).
 *
 * @param template A NULL-terminated array of strings representing the maze template.
 */
void maze_init(char **template) {
    if (!template || !template[0]) {
        error("maze_init: invalid or empty template");
        return;
    }

    // Initialize maze mutex for thread safety
    pthread_mutex_init(&maze_mutex, NULL);

    // Seed random number generator for randomized respawns
    srand(time(NULL));

    // Determine maze dimensions
    maze_rows = 0;
    maze_cols = strlen(template[0]);  // Assume all rows same length
    while (template[maze_rows] != NULL) {
        maze_rows++;
    }

    // Allocate and copy maze content
    maze = calloc(maze_rows, sizeof(char *));
    if (!maze) {
        error("maze_init: memory allocation failed for maze rows");
        return;
    }

    for (int i = 0; i < maze_rows; i++) {
        maze[i] = strdup(template[i]);
        if (!maze[i]) {
            error("maze_init: memory allocation failed for row %d", i);
            // Not freeing earlier rows for simplicity here; can add cleanup if desired
            return;
        }
    }

    debug("maze_init: Maze initialized (%d rows × %d cols)", maze_rows, maze_cols);
}


/**
 * Free maze memory and destroy mutex.
 */
void maze_fini() {
    pthread_mutex_lock(&maze_mutex);
    for (int i = 0; i < maze_rows; i++) {
        free(maze[i]);
    }
    free(maze);
    maze = NULL;
    pthread_mutex_unlock(&maze_mutex);
    pthread_mutex_destroy(&maze_mutex);
}

int maze_get_rows() {
    return maze_rows;
}

int maze_get_cols() {
    return maze_cols;
}

/**
 * @brief Attempt to place a player's avatar at a specific location in the maze.
 *
 * This function sets the specified cell of the maze to contain the given avatar,
 * only if the cell is currently unoccupied (i.e., contains a space character).
 * The operation is protected by a mutex to ensure thread safety.
 *
 * @param avatar The avatar character to place (typically an uppercase letter).
 * @param row    The row index in the maze.
 * @param col    The column index in the maze.
 * @return 0 on success, -1 if the cell is out of bounds or not empty.
 */
int maze_set_player(OBJECT avatar, int row, int col) {
    pthread_mutex_lock(&maze_mutex);

    // Validate bounds
    if (row < 0 || row >= maze_rows || col < 0 || col >= maze_cols) {
        debug("maze_set_player: Out of bounds placement [%d, %d] for %c", row, col, avatar);
        pthread_mutex_unlock(&maze_mutex);
        return -1;
    }

    // Ensure the cell is empty
    if (!IS_EMPTY(maze[row][col])) {
        debug("maze_set_player: Cell [%d, %d] is not empty (contains '%c')",
              row, col, maze[row][col]);
        pthread_mutex_unlock(&maze_mutex);
        return -1;
    }

    // Perform the placement
    maze[row][col] = avatar;
    debug("maze_set_player: Placed %c at [%d, %d]", avatar, row, col);

    pthread_mutex_unlock(&maze_mutex);
    return 0;
}


void maze_remove_player(OBJECT avatar, int row, int col) {
    pthread_mutex_lock(&maze_mutex);
    if (maze[row][col] == avatar) {
        maze[row][col] = EMPTY;
    }
    pthread_mutex_unlock(&maze_mutex);
}

/**
 * @brief Place a player's avatar at a random unoccupied location in the maze.
 *
 * Attempts up to 1000 times to randomly find a valid position (i.e., empty cell)
 * and place the avatar there. On success, updates the output row and column pointers.
 *
 * This function must be called with unplaced avatar — it does not remove previous positions.
 *
 * @param avatar The avatar character to place (e.g., 'A', 'B', etc.).
 * @param rowp Pointer to store the selected row (optional).
 * @param colp Pointer to store the selected col (optional).
 * @return 0 on success, -1 on failure after max_attempts.
 */
int maze_set_player_random(OBJECT avatar, int *rowp, int *colp) {
    const int max_attempts = 1000;

    for (int i = 0; i < max_attempts; i++) {
        int r = rand() % maze_get_rows();
        int c = rand() % maze_get_cols();

        debug("maze_set_player_random: Trying to place %c at [%d,%d] (attempt %d)", avatar, r, c, i + 1);

        if (maze_set_player(avatar, r, c) == 0) {
            if (rowp) *rowp = r;
            if (colp) *colp = c;

            debug("maze_set_player_random: SUCCESS placing %c at [%d,%d]", avatar, r, c);
            return 0;
        }
    }

    debug("maze_set_player_random: FAILED to place %c after %d attempts", avatar, max_attempts);
    return -1;
}


int maze_move(int row, int col, int dir) {
    int drow[] = { -1, 0, 1, 0 };
    int dcol[] = { 0, -1, 0, 1 };

    pthread_mutex_lock(&maze_mutex);

    if (row < 0 || row >= maze_rows || col < 0 || col >= maze_cols || !IS_AVATAR(maze[row][col])) {
        pthread_mutex_unlock(&maze_mutex);
        return -1;
    }

    int new_row = row + drow[dir];
    int new_col = col + dcol[dir];

    if (new_row < 0 || new_row >= maze_rows || new_col < 0 || new_col >= maze_cols ||
        !IS_EMPTY(maze[new_row][new_col])) {
        pthread_mutex_unlock(&maze_mutex);
        return -1;
    }

    maze[new_row][new_col] = maze[row][col];
    maze[row][col] = EMPTY;

    pthread_mutex_unlock(&maze_mutex);
    return 0;
}

OBJECT maze_find_target(int row, int col, DIRECTION dir) {
    int drow[] = { -1, 0, 1, 0 };
    int dcol[] = { 0, -1, 0, 1 };

    pthread_mutex_lock(&maze_mutex);

    while (row >= 0 && row < maze_rows && col >= 0 && col < maze_cols) {
        row += drow[dir];
        col += dcol[dir];

        if (row < 0 || row >= maze_rows || col < 0 || col >= maze_cols) break;

        if (!IS_EMPTY(maze[row][col])) {
            OBJECT result = maze[row][col];
            pthread_mutex_unlock(&maze_mutex);
            return IS_AVATAR(result) ? result : EMPTY;
        }
    }

    pthread_mutex_unlock(&maze_mutex);
    return EMPTY;
}

int maze_get_view(VIEW *view, int row, int col, DIRECTION gaze, int depth) {
    int drow[] = { -1, 0, 1, 0 };
    int dcol[] = { 0, -1, 0, 1 };

    int lrow[] = { 0, -1, 0, 1 };
    int lcol[] = { -1, 0, 1, 0 };

    pthread_mutex_lock(&maze_mutex);

    int actual_depth = 0;
    for (int d = 0; d < depth; d++) {
        int r = row + d * drow[gaze];
        int c = col + d * dcol[gaze];

        if (r < 0 || r >= maze_rows || c < 0 || c >= maze_cols) break;

        (*view)[d][CORRIDOR] = maze[r][c];

        int rl = r + lrow[gaze];
        int cl = c + lcol[gaze];
        (*view)[d][LEFT_WALL] = (rl < 0 || rl >= maze_rows || cl < 0 || cl >= maze_cols)
                                ? '*' : maze[rl][cl];

        int rr = r - lrow[gaze];
        int cr = c - lcol[gaze];
        (*view)[d][RIGHT_WALL] = (rr < 0 || rr >= maze_rows || cr < 0 || cr >= maze_cols)
                                 ? '*' : maze[rr][cr];

        actual_depth++;
    }

    pthread_mutex_unlock(&maze_mutex);
    return actual_depth;
}

void show_view(VIEW *view, int depth) {
    fprintf(stderr, "View:\n");
    for (int i = 0; i < depth; i++) {
        fprintf(stderr, "%c %c %c\n", (*view)[i][LEFT_WALL], (*view)[i][CORRIDOR], (*view)[i][RIGHT_WALL]);
    }
}

void show_maze() {
    pthread_mutex_lock(&maze_mutex);
    fprintf(stderr, "Current Maze State:\n");
    for (int i = 0; i < maze_rows; i++) {
        fprintf(stderr, "%s\n", maze[i]);
    }
    pthread_mutex_unlock(&maze_mutex);
}
