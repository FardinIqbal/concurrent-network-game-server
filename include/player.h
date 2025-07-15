/**
 * === DO NOT MODIFY THIS FILE ===
 * If you need some other prototypes or constants in a header, please put them
 * in another header file.
 *
 * When we grade, we will be replacing this file with our own copy.
 * You have been warned.
 * === DO NOT MODIFY THIS FILE ===
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "protocol.h"
#include "maze.h"

/*
 * The player module mantains a mapping from avatars to player state for
 * the players currently logged in to the game.
 */

/*
 * Structure that defines the state of a player in the Maze War game.
 *
 * You will have to give a complete structure definition in player.c.
 * The precise contents are up to you.  Be sure that all the operations
 * that might be called concurrently are thread-safe.  Note that, due to
 * the pattern of calls made between the maze module and the player module,
 * it is necessary that any mutex used to protect a PLAYER object
 * have the PTHREAD_MUTEX_RECURSIVE attribute set.  This makes the mutex
 * recursive, which means that it can be locked again by a thread that
 * already holds the lock, without a deadlock occurring.  The reason
 * PLAYER objects have to have recursive mutexes is because functions on
 * PLAYER objects may result in calls on the maze, and these in turn may
 * result in an "up call" to player_update_view() to update the player's view.
 * Unless a recursive mutex is used, such an up call will deadlock when
 * the attempt is made to reaquire the PLAYER mutex that is already held.
 */
typedef struct player PLAYER;

/*
 * Initialize the players module.
 * This must be called before doing calling any other functions in this
 * module.
 */
void player_init(void);

/*
 * Finalize the players module.
 * This should be called when the players module is no longer required.
 */
void player_fini(void);

/*
 * Attempt to log in a player with a specified avatar.
 *
 * @param clientfd  The file descriptor of the connection to the client.
 * @param avatar  The avatar desired for the player.
 * @param name  The player's name, which is copied before being saved.
 * @return A pointer to a PLAYER object, in case of success, otherwise NULL.
 *
 * The login can fail if the specified avatar is already in use.
 * If the login succeeds then a mapping is recorded from the specified avatar
 * to a PLAYER object that is created for this client and returned as the
 * result of the call.  The returned PLAYER object has a reference count equal
 * to one.  This reference should be "owned" by the thread that is servicing
 * this client, and it should not be released until the client has logged out.
 */
PLAYER *player_login(int clientfd, OBJECT avatar, char *name);

/*
 * Log out a player.
 *
 * @param player  The player to be logged out.
 *
 * The specified player is removed from the maze and from the players map
 * and a SCORE packet with a score of -1 is sent to the client to cause the
 * player's score to be removed from the scoreboard area.
 * This function "consumes" one reference to the PLAYER object by calling
 * player_unref().  This will have the effect of causing the PLAYER object
 * to be freed as soon as any references to it currently held by other threads
 * have been released.
 */
void player_logout(PLAYER *player);

/*
 * Reset a player to a random location in the maze.
 *
 * @param player  The player to be reset.
 *
 * The player's avatar is removed from its existing location and re-placed
 * at a randomly selected location in the maze.  If the re-placement operation
 * fails, the client connection is ungracefully shut down in order to force
 * termination of service.  If the re-placement operation succeeds, then
 * the player's scoreboard is refreshed by sending all other player's scores,
 * and all other players' scoreboards are updated with this player's score.
 */
void player_reset(PLAYER *player);

/*
 * Get the state object for the player with a specified avatar.
 *
 * @param avatar  The avatar of the player whose state is to be retrieved.
 * @return the PLAYER object containing the state of the player with the
 * specified avatar, if this avatar is currently in use, otherwise NULL
 * is returned.
 *
 * The reference count on any PLAYER object that is returned is incremented,
 * to account for the additional pointer to that object which has been
 * created.  It is the caller's responsibility to call player_unref() to
 * release this reference count when the pointer is no longer required.
 */
PLAYER *player_get(unsigned char avatar);

/*
 * Increase the reference count on a player by one.
 *
 * @param player  The PLAYER whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The PLAYER object that was passed as a parameter.
 */
PLAYER *player_ref(PLAYER *player, char *why);

/*
 * Decrease the reference count on a player by one.
 *
 * @param player  The PLAYER whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 *
 * If after decrementing, the reference count has reached zero, then the
 * player and its contents are freed.
 */
void player_unref(PLAYER *player, char *why);

/*
 * Send a packet to the client for a player.
 *
 * @param player  The PLAYER object corresponding to the client who should
 * receive the packet.
 * @param pkt  The packet to be sent.
 * @param data  Data payload to be sent, or NULL if none.
 *
 * Once a client has connected and successfully logged in, this function
 * should be used to send packets to the client, as opposed to the lower-level
 * proto_send_packet() function.  The reason for this is that the present
 * function will lock the player mutex before calling proto_send_packet().
 * The fact that the mutex is locked before sending means that multiple
 * threads can safely call this function to send to the client, and these
 * calls will be serialized by the mutex.  Note that when a change to the
 * state of the maze results from the actions of one player, then that
 * then that player's thread will attempt to update the views of all players.
 * Since this can happen concurrently, we need to synchronize access to
 * the network connection to that client.
 *
 * NOTE: This function will lock the mutex associated with the PLAYER
 * object passed, for the duration of the call.  Since PLAYER mutexes are
 * recursive, it is OK for a thread to call this function while holding
 * a lock on the same PLAYER object.  However, no other locks should be
 * held at the time of call, otherwise there is a risk of deadlock.
 */
int player_send_packet(PLAYER *player, MZW_PACKET *pkt, void *data);

/*
 * Get the current maze location and gaze direction for a player.
 *
 * @param player  The player whose information is to be accessed.
 * @param rowp  Pointer to a variable to receive the row index.
 * @param colp  Pointer to a variable to receive the column index.
 * @param dirp  Pointer to a variable to receive the gaze direction.
 * @return 0 if the player currently has a valid location in the maze,
 * otherwise nonzero.
 */
int player_get_location(PLAYER *player, int *rowp, int *colp, int *dirp);

/*
 * Attempt to move the player's avatar in the maze one unit of distance
 * forward or back with respect to the current direction of gaze.
 *
 * @param player  The player whose avatar is to be moved.
 * @param sign  1 for "forward" motion, which is in the direction of gaze,
 * and -1 for "backward" motion in the direction opposite to the gaze.
 * @return zero if the movement succeeds, nonzero otherwise.
 *
 * This function finds the current location of the player and calculates
 * the direction of motion, then calls maze_move() to actually carry out
 * the motion.
 */
int player_move(PLAYER *player, int dir);

/*
 * Rotate the player's gaze 90 degrees in the specified direction.
 *
 * @param player  The player whose direction of gaze is to be rotated.
 * @param dir  The direction of rotation: 1 means counter-clockwise
 * and -1 means clockwise.
 *
 * As a rotation of the gaze is likely to cause a complete change in
 * the view, this function invalidates the current player view in order
 * to force a full update.  Note that since the maze is not involved
 * in this operation, it is necessary to call player_update_view()
 * to cause the player's view to be updated after the rotation.
 */
void player_rotate(PLAYER *player, int dir);

/*
 * Fire the player's laser in the direction of gaze.
 *
 * @param player  The player whose laser is to be fired.
 *
 * The laser will score a hit on the first avatar, if any, encountered
 * along the corridor.  If a hit is scored, the thread serving the player
 * who has been hit will be notified by sending a SIGUSR1 signal to the
 * thread serving that player.  In addition the score of the player who
 * fired the laser will be incremented by one and all clients will be
 * notified of the new score.
 */
void player_fire_laser(PLAYER *player);

/*
 * Invalidate the current view of an player.
 *
 * @param the player whose view is to be invalidated.
 *
 * In order to avoid having to transmit the full view seen by a player
 * each time a small change occurs (such as the appearance of another player's
 * avatar in an otherwise unchanged scene), the view used for a preceding
 * update is saved to permit the next update to be performed using only
 * differences between the two views, rather than the complete view.
 * However, some operations are likely to or certain to completely change
 * a player's view.  In such cases, we want to invalidate any preceding
 * view in order to force the next update to be a full one, rather than
 * a differential one.  The purpose of this function is to perform such
 * invalidation.
 */
void player_invalidate_view(PLAYER *player);

/*
 * Update a player's view to correspond to the current maze contents and
 * direction of gaze.
 *
 * @param player  The player whose view is to be updated.
 *
 * This function will query the maze for the view to be shown to the player,
 * and then it will perform either an incremental or a full view update,
 * depending on whether there is a valid previous view for the player.
 * To avoid deadlock between multiple threads calling this function
 * concurrently, the player mutex should be locked and held while querying
 * the maze for the view to be shown.  Once the view to be shown has
 * been obtained, then the update is performed by sending a series of
 * packets to the client.  A full update is performed by sending a CLEAR
 * packet, followed by a SHOW packet for every cell in the view.
 * An incremental update is performed by just sending SHOW packets for
 * those cells in the view that have changed since the previous update.
 * Note that in an incremental update care must be taken if the depths of
 * the old and new views are different.
 */
void player_update_view(PLAYER *player);

/*
 * Check whether a player has received any laser hits and, if so,
 * respond appropriately.
 *
 * @param player  The player to be checked for laser hits.
 *
 * The server thread handling a particular player will be notified about
 * a laser hit by receiving a SIGUSR1 signal.  A handler for this signal
 * should be installed in order to record the occurrence of the laser hit
 * in the state for the player who took the hit.  The present function
 * should be called just before each attempt is made to read the next
 * packet from the client connection.  This will ensure that any hits
 * that have occurred are noticed by the player that took the hits
 * in a prompt fashion.
 */
void player_check_for_laser_hit(PLAYER *player);

/*
 * Broadcast a chat message to all players.
 *
 * @param player  The player sending the chat message.
 * @param msg  The message to be sent (not null-terminated).
 * @param len  The length of the message.
 */
void player_send_chat(PLAYER *player, char *msg, size_t len);

#endif
