/**
 * === DO NOT MODIFY THIS FILE ===
 * If you need some other prototypes or constants in a header, please put them
 * in another header file.
 *
 * When we grade, we will be replacing this file with our own copy.
 * You have been warned.
 * === DO NOT MODIFY THIS FILE ===
 */
#ifndef MAZE_H
#define MAZE_H

#include <ctype.h>

/*
 * A "maze" is a rectangular array of cells, each element of which can be blank
 * or contain a single object.  Objects are either player avatars or solid objects;
 * i.e. walls.  Though the code is intended to run with mazes that are arbitrarily
 * populated, the scheme for representing rendering views is more or less based on
 * the assumption that mazes consist of "corridors", which are one unit in width
 * with walls on each side, connected by "doors", which are gaps in the walls.
 * It is not clear how well the display will appear if a maze contains open spaces
 * that are larger than one unit in width.
 */

/*
 * The following type is used for maze cell contents.
 */
typedef unsigned char OBJECT;

/*
 * The following macros should be used to classify the contents of a maze cell.
 */

#define IS_EMPTY(c) (c == ' ')
#define IS_AVATAR(c) (isupper(c))
#define IS_WALL(c) (!IS_EMPTY(c) && !IS_AVATAR(c) && (c) > ' ' && (c) < 'A')

/* Constant used to fill in blank space in a maze cell, e.g. when an avatar is moved. */
#define EMPTY (' ')

/*
 * Compass directions.  These define the possible "gaze" directions of a player in
 * the maze, and they are also the possible directions in which a player can move.
 * The following (row, column) increments should be used to move one unit in each
 * direction:  NORTH = (-1, 0), WEST = (0, -1), SOUTH = (1, 0), EAST = (0, 1)
 */
typedef enum { NORTH, WEST, SOUTH, EAST } DIRECTION;
#define NUM_DIRECTIONS (EAST+1)

/*
 * The following macros should be used to compute the direction that results
 * from rotating or reversing a direction.
 */
#define TURN_LEFT(d) ((d) == EAST ? NORTH : (d)+1)
#define TURN_RIGHT(d) ((d) == NORTH ? EAST : (d)-1)
#define REVERSE(d) ((d) < 2 ? (d)+2 : (d)-2)

/*
 * A "view" represents what can be seen in the maze looking from a particular
 * position, gazing in a particular direction.  A view is represented as a
 * Dx3 array, where the parameter D is the "depth" of the view and it
 * corresponds to the distance from the viewer location.  A view is calculated
 * for a particular location simply by starting with that location and extracting
 * the Dx3 "patch" of the maze contents in a particular gaze direction, as suggested
 * by the following diagram, which represents a view of depth 10 seen by player X,
 * in which the player sees a solid corridor wall (*) on the left, a corridor
 * wall (%) with two doors on the right, a solid wall (@) at the end of the
 * corridor, and the avatar A of another player halfway down the corridor.
 *
 *     0123456789
 *   0 *********@   
 *   1 X    A   @   --> Direction of gaze
 *   2 %%% %%% %@
 *
 * In accessing a view, the first subscript corresponds to the distance from the
 * origin of the view (which is at [0][1]).  The second subscript has the
 * following meaning:
 *   0 left wall -  The value represents a feature of the left-hand wall of
 *                  the corridor defined by the direction of gaze.  The possible
 *                  features are either solid wall or door.
 *   1 corridor -   The value represents an object in the corridor defined
 *                  by the direction of gaze.  Objects can be avatars of players
 *                  or a wall at the end of the corridor.
 *   2 right wall - The value represents a feature of the right-hand wall of
 *                  the corridor defined by the direction of gaze.  The possible
 *		    features are either solid wall or door.
 *
 * When a view is calculated for a certain position and direction of gaze, the
 * depth of the returned view might not be the maximum possible depth.  This will
 * occur, for example, if the boundary of the maze is encountered at a distance
 * from the origin location that is less than the maximum view depth.
 *
 * The following macros should be used when accessing a view.
 */
#define VIEW_DEPTH 16    // Maximum view depth

#define LEFT_WALL 0
#define CORRIDOR 1
#define RIGHT_WALL 2
#define VIEW_WIDTH 3    // Width of a view

typedef char VIEW[][VIEW_WIDTH];

/*
 * Initialize the maze.
 *
 * @param template  The template for the maze.
 *
 * This must be called before any other operations are performed on the maze.
 * A maze template is a NULL-terminated array of strings, all of which are the same
 * length.  This defines a rectangular array of characters.  Characters represent
 * either player avatars, solid objects, or empty space, as described above.
 */
void maze_init(char **template);

/*
 * Finalize the maze.
 * This should be called when the maze is no longer required.
 */
void maze_fini();

/*
 * Get the number of rows of the maze.
 *
 * @return the number of rows of the maze.
 */
int maze_get_rows();

/*
 * Get the number of columns of the maze.
 *
 * @return the number of columns of the maze.
 */
int maze_get_cols();

/*
 * Place a player's avatar in the maze at a specified row/column location.
 *
 * @param avatar  The avatar to be placed in the maze.
 * @param row  The row in which the avatar is to be placed.
 * @param col  The column in which the avatar is to be placed.
 * @return zero if the placement was successful, nonzero otherwise.
 *
 * Unsuccessful placement will occur if the specified location in the maze
 * is not empty.
 */
int maze_set_player(OBJECT avatar, int row, int col);

/*
 * Place a player's avatar in the maze at a random unoccupied location.
 *
 * @param avatar  The avatar to be placed in the maze.
 * @param row  Pointer to a variable into which will be stored the row
 * at which the avatar was placed.
 * @param col  Pointer to a variable into which will be stored the column
 * at which the avatar was placed.
 * @return zero if the placement was successful, nonzero otherwise.
 *
 * The placement can fail if after a large number of attempts an unoccupied
 * location has not been found.
 */
int maze_set_player_random(OBJECT avatar, int *rowp, int *colp);

/*
 * Remove a specified player's avatar from a specified location in the maze.
 *
 * @param avatar  The avatar to be removed from the maze.
 * @param row  The row from which the avatar is to be removed.
 * @param col  The column from which the avatar is to be removed.
 */
void maze_remove_player(OBJECT avatar, int row, int col);

/*
 * Attempt to move a player's avatar at a specified location one unit
 * of distance in a specified direction.
 *
 * @param row  The row at which the avatar to be moved is located.
 * @param col  The column at which the avatar to be moved is located.
 * @param dir  The direction in which the avatar is to be moved.
 * @return zero if movement was successful, nonzero otherwise.
 *
 * Movement is not possible if it would cause the avatar to occupy
 * a location already occupied by some other object, or if it would
 * result in moving outside the bounds of the maze.
 */
int maze_move(int row, int col, int dir);

/*
 * Search from a specified target location in a specified direction,
 * and return the first avatar, if any, that is found.
 *
 * @param row  The starting row for the search.
 * @param col  The starting column for the search.
 * @param dir  The direction for the search.
 * @return the first avatar found, or EMPTY if the search terminated
 * without finding an avatar.
 *
 * The search terminates when a non-empty location is reached,
 * or the search would go beyond the maze boundaries.
 */
OBJECT maze_find_target(int row, int col, DIRECTION dir);

/*
 * Get the view from a specified location in the maze, with the gaze
 * in a specified direction.
 *
 * @param view  A pointer to a view of maximum depth that is to be filled
 * in as a result of the call.
 * @param row  Row of the maze that contains the view origin.
 * @param col  Column of the maze that contains the view origin.
 * @param gaze  Direction of gaze for the view.
 * @param depth  Maximum depth of the view.  This must be less than or
 * equal to the depth of the view that is passed.
 * @return the depth to which the view was filled in.
 *
 * The view array is populated with a "patch" of the maze contents,
 * as described above.  The returned value could be less than the
 * maximum depth, as described above.  Entries of the view at depths
 * greater than the returned depth should be regarded as invalid.
 */
int maze_get_view(VIEW *view, int row, int col, DIRECTION gaze, int depth);

/*
 * Print a view on stderr, for debugging.
 *
 * @param view  A pointer to the view to be shown.
 * @param depth  Depth to which the view should be shown.
 */
void show_view(VIEW *view, int depth);

/*
 * Print the maze on stderr, for debugging.
 */
void show_maze();

#endif


