/**
 * @file protocol.c
 * @brief Implementation of packet transmission and reception for MazeWar.
 *
 * This module provides functions to send and receive packets (with optional payloads)
 * over a TCP connection, as specified in the MazeWar protocol.
 * All multi-byte fields are converted to network byte order before transmission.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>

#include "protocol.h"
#include "debug.h"  // Enable debug() output when compiled with -DDEBUG

/**
 * @brief Write all bytes from a buffer to a file descriptor.
 *
 * This helper function ensures that exactly @p count bytes from @p buf are written to @p fd,
 * handling short writes that can occur with system calls.
 *
 * @param fd    File descriptor to write to.
 * @param buf   Pointer to buffer to write.
 * @param count Number of bytes to write.
 * @return 0 on success, -1 on error.
 */
static int write_all(int fd, const void *buf, size_t count) {
    const char *ptr = buf;
    while (count > 0) {
        ssize_t n = write(fd, ptr, count);
        if (n <= 0) {
            debug("write_all failed: %s", strerror(errno));
            return -1;
        }
        ptr += n;
        count -= n;
    }
    return 0;
}

/**
 * @brief Read exactly @p count bytes from a file descriptor into a buffer.
 *
 * This function ensures that exactly @p count bytes are read from the file descriptor @p fd
 * into the buffer @p buf. It handles short reads and transient interruptions due to signals.
 *
 * If the system call is interrupted by a signal (e.g., SIGUSR1), it retries the read
 * instead of treating it as an error. This allows signal-based interruption (e.g. laser hits)
 * to be processed correctly in multithreaded client service threads.
 *
 * @param fd    File descriptor to read from.
 * @param buf   Pointer to buffer to fill with read data.
 * @param count Number of bytes to read.
 * @return 0 on success (all bytes read), -1 on error or EOF.
 */
static int read_all(int fd, void *buf, size_t count) {
    char *ptr = buf;

    while (count > 0) {
        ssize_t n = read(fd, ptr, count);

        if (n < 0) {
            if (errno == EINTR) {
                // Interrupted by signal (e.g., SIGUSR1) — retry read
                debug("read_all: interrupted by signal, retrying...");
                continue;
            }
            // Unrecoverable read error
            debug("read_all failed: %s", strerror(errno));
            return -1;
        }

        if (n == 0) {
            // EOF — client disconnected
            debug("read_all: unexpected EOF from fd=%d", fd);
            return -1;
        }

        ptr += n;
        count -= n;
    }

    return 0;
}



/**
 * @brief Send a MazeWar protocol packet (with optional payload) over a file descriptor.
 *
 * The function fills in the timestamp fields, converts all multi-byte fields to network byte order,
 * and sends the fixed-size header followed by the payload (if any).
 *
 * @param fd    File descriptor on which to send the packet.
 * @param pkt   Pointer to the packet header (fields in host byte order).
 * @param data  Pointer to payload data, or NULL if none.
 * @return 0 on success, -1 on error.
 */
int proto_send_packet(int fd, MZW_PACKET *pkt, void *data) {
    if (!pkt) return -1;

    // Add current timestamp using CLOCK_MONOTONIC to match professor's output style
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        debug("clock_gettime failed: %s", strerror(errno));
        return -1;
    }
    pkt->timestamp_sec = ts.tv_sec;
    pkt->timestamp_nsec = ts.tv_nsec;

    MZW_PACKET copy = *pkt;

    // Convert multi-byte fields to network byte order
    copy.size = htons(copy.size);
    copy.timestamp_sec = htonl(copy.timestamp_sec);
    copy.timestamp_nsec = htonl(copy.timestamp_nsec);

    debug("Sending packet: type=%d, size=%u, p1=%d, p2=%d, p3=%d",
          pkt->type, pkt->size, pkt->param1, pkt->param2, pkt->param3);

    // Send the fixed-size header
    if (write_all(fd, &copy, sizeof(MZW_PACKET)) < 0) {
        debug("Failed to send packet header");
        return -1;
    }

    // Send the payload, if any
    if (pkt->size > 0 && data != NULL) {
        if (write_all(fd, data, pkt->size) < 0) {
            debug("Failed to send packet payload");
            return -1;
        }
    }

    return 0;
}

/**
 * @brief Receive a MazeWar protocol packet (and optional payload) from a file descriptor.
 *
 * The function reads the fixed-size header, converts all multi-byte fields to host byte order,
 * and if a payload is present, allocates memory and reads it into @p *datap.
 * The caller is responsible for freeing the payload buffer if @p *datap is non-NULL.
 *
 * @param fd    File descriptor from which to receive the packet.
 * @param pkt   Pointer to storage for the fixed-size packet header (fields in host byte order).
 * @param datap Pointer to a variable to store the payload pointer. Set to NULL if no payload.
 * @return 0 on success, -1 on error.
 */
int proto_recv_packet(int fd, MZW_PACKET *pkt, void **datap) {
    if (!pkt || !datap) return -1;

    MZW_PACKET net_pkt;

    // Read the fixed-size header
    if (read_all(fd, &net_pkt, sizeof(MZW_PACKET)) < 0) {
        debug("Failed to receive packet header");
        return -1;
    }

    // Convert multi-byte fields from network to host byte order
    pkt->type = net_pkt.type;
    pkt->param1 = net_pkt.param1;
    pkt->param2 = net_pkt.param2;
    pkt->param3 = net_pkt.param3;
    pkt->size = ntohs(net_pkt.size);
    pkt->timestamp_sec = ntohl(net_pkt.timestamp_sec);
    pkt->timestamp_nsec = ntohl(net_pkt.timestamp_nsec);

    debug("Received packet: type=%d, size=%u, p1=%d, p2=%d, p3=%d",
          pkt->type, pkt->size, pkt->param1, pkt->param2, pkt->param3);

    // If there is a payload, allocate buffer and read it
    if (pkt->size > 0) {
        *datap = malloc(pkt->size);
        if (!*datap) {
            debug("malloc failed for payload");
            return -1;
        }

        if (read_all(fd, *datap, pkt->size) < 0) {
            debug("Failed to receive packet payload");
            free(*datap);
            *datap = NULL;
            return -1;
        }
    } else {
        *datap = NULL;
    }

    return 0;
}