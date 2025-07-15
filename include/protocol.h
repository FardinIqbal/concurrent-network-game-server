/**
 * === DO NOT MODIFY THIS FILE ===
 * If you need some other prototypes or constants in a header, please put them
 * in another header file.
 *
 * When we grade, we will be replacing this file with our own copy.
 * You have been warned.
 * === DO NOT MODIFY THIS FILE ===
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <netinet/in.h>
#include <arpa/inet.h>

/*
 * The "Maze War" game protocol.
 *
 * This header file specifies the format of communication between the
 * Maze War server and its clients.  We will use the term "packet" to refer
 * to a single message sent at the protocol level.  A full-duplex,
 * stream-based (i.e. TCP) connection is used between a client and the
 * server.  Communication is effected by the client and server sending
 * "packets" to each other over this connection.  Each packet consists
 * of a fixed-length header, with fields in network byte order,
 * followed by an optional payload whose length is specified in the header.
 *
 * The following are the packet types in the protocol:
 *
 * Client-to-server requests:
 *   LOGIN:   Log a user into the game
 *            (sends user name, desired avatar/UID)
 *   MOVE:    Move within the maze (possibilities: forward, back)
 *   TURN:    Rotate the direction of gaze (possibilities: left, right)
 *   FIRE:    Fire laser
 *   REFRESH: Ask for the view to be refreshed
 *   SEND:    Send a chat message
 *
 * 
 * Server-to-client responses:
 *   READY:   Sent in response to a successful login
 *   INUSE:   Avatar/UID is in use -- login unsuccessful
 *   CLEAR:   Clear all objects from the current view
 *   SHOW:    Show an object in the view
 *            (contains object, direction, distance)
 *   ALERT:   Alert the user to an event that has occurred
 *   SCORE:   Update the scoreboard
 *            (contains avatar, score)
 *   CHAT:    Chat message from another user
 */

/*
 * Packet types.
 */
typedef enum {
    /* Unused */
    MZW_NO_PKT,
    /* Client-to-server */
    MZW_LOGIN_PKT, MZW_MOVE_PKT, MZW_TURN_PKT, MZW_FIRE_PKT, MZW_REFRESH_PKT,
    MZW_SEND_PKT,
    /* Server-to-client */
    MZW_READY_PKT, MZW_INUSE_PKT, MZW_CLEAR_PKT, MZW_SHOW_PKT,MZW_ALERT_PKT,
    MZW_SCORE_PKT, MZW_CHAT_PKT
} MZW_PACKET_TYPE;

/*
 * Structure of a packet.
 */
typedef struct mzw_packet {
    uint8_t type;		   // Type of the packet
    int8_t param1;                 // Generic parameter field 1
    int8_t param2;                 // Generic parameter field 2
    int8_t param3;                 // Generic parameter field 3
    uint16_t size;                 // Payload size (zero if no payload)
    uint32_t timestamp_sec;        // Seconds field of time packet was sent
    uint32_t timestamp_nsec;       // Nanoseconds field of time packet was sent
} MZW_PACKET;

/*
 * Object types (for 'show' packet).
 */
typedef enum {
    MZW_NO_OBJ, MZW_PLAYER, MZW_WALL, MZW_DOOR
} MZW_OBJECT_TYPE;

/*
 * Send a packet, which consists of a fixed-size header followed by an
 * optional associated data payload.
 *
 * @param fd  The file descriptor on which packet is to be sent.
 * @param pkt  The fixed-size packet header, with multi-byte fields
 *   in host byte order
 * @param data  The data payload, or NULL, if there is none.
 * @return  zero in case of successful transmission, nonzero otherwise.
 *   In the latter case, errno is set to indicate the error.
 *
 * Multi-byte fields in the packet header are converted to network byte
 * order before sending.  The structure passed to this function may be
 * modified as a result of this conversion process.
 */
int proto_send_packet(int fd, MZW_PACKET *pkt, void *data);

/*
 * Receive a packet, blocking until one is available.
 *
 * @param fd  The file descriptor from which the packet is to be received.
 * @param pkt  Pointer to caller-supplied storage for the fixed-size
 *   portion of the packet.
 * @param datap  Pointer to a variable into which to store a pointer to any
 *   payload received.
 * @return  zero in case of successful reception, nonzero otherwise.  In the
 *   latter case, errno is set to indicate the error.
 *
 * The returned structure has its multi-byte fields in host byte order.
 * If the returned payload pointer is non-NULL, then the caller has the
 * responsibility of freeing that storage.
 */
int proto_recv_packet(int fd, MZW_PACKET *pkt, void **datap);

#endif
