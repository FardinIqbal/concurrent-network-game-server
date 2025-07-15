# MazeWar Server – Multi-Threaded Network Game Server in C

## Overview

MazeWar Server is a high-performance, modular game server written in C that supports multiple concurrent players navigating a shared maze environment. It handles real-time avatar movement, chat messaging, and laser-based combat using a custom binary network protocol. The server is designed for robustness, scalability, and concurrency correctness, with a focus on clean multithreaded architecture and low-level systems programming.

## Features

* POSIX-compliant multi-threaded server design using pthreads
* TCP socket handling with graceful connection management
* Custom binary protocol for client-server communication
* Thread-safe client registry with blocking shutdown support
* Dynamic maze representation with real-time avatar updates
* Reference-counted player management with signal-based laser hit detection
* Fine-grained synchronization using mutexes, semaphores, and recursive locks
* Real-time chat broadcast to all connected players
* Incremental and full-view rendering logic for client updates
* Clean shutdown via SIGHUP and SIGUSR1 signal handling
* Defensive coding practices and extensive runtime validation

## Technologies

* C (C99)
* POSIX Threads (pthreads)
* BSD Sockets API (TCP/IP)
* Mutexes & Semaphores
* Signal Handling
* Memory Management
* Valgrind (Leak Detection)
* Criterion (Unit Testing)

## Architecture

The MazeWar Server is composed of the following core components:

* **Main Server Loop**: Accepts client connections and spawns dedicated service threads.
* **Client Service Threads**: Handle all communication with individual clients and dispatch game logic based on received packets.
* **Client Registry**: Tracks active client connections with support for graceful shutdown coordination.
* **Protocol Module**: Encodes and decodes structured packets for inter-process communication over the network.
* **Maze Module**: Maintains a concurrent, lock-protected maze data structure and handles avatar placement, movement, and collisions.
* **Player Module**: Manages the lifecycle of player objects, handles login, scorekeeping, laser interactions, and view updates.


## Build Instructions

Compile in release mode:

```
make
```

Compile in debug mode with verbose logging:

```
make debug
```

## Running the Server

To start the server on a specific port:

```
./mazewar -p 3333
```

To use a custom maze template:

```
./mazewar -p 3333 -t path/to/template.txt
```

Clients can then connect using the provided graphical or text client:

```
./util/gclient -p 3333 -u Username -a A
./util/tclient -p 3333
```

## Testing

Unit tests and integration tests are located in the `tests/` directory. To run them:

```
make run-tests
```

Memory and concurrency correctness should be validated using:

```
valgrind ./mazewar
```

Custom stress tests can simulate concurrent logins, movements, chat, and combat using scripted text clients.

## Notable Design Decisions

* Recursive mutexes are used for player objects to support nested lock acquisition during self-referential updates.
* The server uses reference counting to prevent premature deallocation of shared player state across threads.
* View updates are optimized using incremental rendering to reduce network I/O.
* SIGUSR1 signals interrupt blocked threads on laser hit, triggering asynchronous recovery without polling.

## Possible Extensions

* Move to event-driven architecture (e.g., epoll) for improved scalability
* Add persistent user state and score tracking
* Replace ASCII maze with graphical rendering over WebSockets
* TLS support for secure client connections
* Rate limiting, error metrics, and observability improvements

## License

This project is licensed under the MIT License or academic license terms as applicable.
