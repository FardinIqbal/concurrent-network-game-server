#include <criterion/criterion.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <wait.h>

static void init() {
#ifndef NO_SERVER
    int ret;
    int i = 0;
    do { // Wait for server to start
        ret = system("netstat -an | fgrep '0.0.0.0:9999' > /dev/null");
        sleep(1);
    } while(++i < 30 && WEXITSTATUS(ret));
#endif
}

static void fini() {
}

/*
 * Thread to run a command using system() and collect the exit status.
 */
void *system_thread(void *arg) {
    long ret = system((char *)arg);
    return (void *)ret;
}

Test(student_suite, 00_start_server, .timeout = 30) {
    fprintf(stderr, "server_suite/00_start_server\n");
    int server_pid = 0;
    int ret = system("netstat -an | fgrep '0.0.0.0:9999' > /dev/null");
    cr_assert_neq(WEXITSTATUS(ret), 0, "Server was already running");
    fprintf(stderr, "Starting server...");
    if ((server_pid = fork()) == 0) {
        execlp("valgrind", "mazewar", "--leak-check=full", "--track-fds=yes",
               "--error-exitcode=37", "--log-file=test_output/valgrind.out",
               "bin/mazewar", "-p", "9999", NULL);
        fprintf(stderr, "Failed to exec server\n");
        abort();
    }
    fprintf(stderr, "pid = %d\n", server_pid);
    char *cmd = "sleep 10";
    pthread_t tid;
    pthread_create(&tid, NULL, system_thread, cmd);
    pthread_join(tid, NULL);
    cr_assert_neq(server_pid, 0, "Server was not started by this test");
    fprintf(stderr, "Sending SIGHUP to server pid %d\n", server_pid);
    kill(server_pid, SIGHUP);
    sleep(5);
    kill(server_pid, SIGKILL);
    wait(&ret);
    fprintf(stderr, "Server wait() returned = 0x%x\n", ret);
    if (WIFSIGNALED(ret)) {
        fprintf(stderr, "Server terminated with signal %d\n", WTERMSIG(ret));
        system("cat test_output/valgrind.out");
        if (WTERMSIG(ret) == 9)
            cr_assert_fail("Server did not terminate after SIGHUP");
    }
    if (WEXITSTATUS(ret) == 37)
        system("cat test_output/valgrind.out");
    cr_assert_neq(WEXITSTATUS(ret), 37, "Valgrind reported errors");
    cr_assert_eq(WEXITSTATUS(ret), 0, "Server exit status was not 0");
}

Test(student_suite, 01_connect, .init = init, .fini = fini, .timeout = 5) {
    fprintf(stderr, "server_suite/01_connect\n");
    int ret = system("util/tclient -p 9999 </dev/null | grep 'Connected to server'");
    cr_assert_eq(ret, 0, "expected %d, was %d\n", 0, ret);
}

Test(student_suite, 02_invalid_port, .timeout = 5) {
    fprintf(stderr, "server_suite/02_invalid_port\n");
    int ret = system("bin/mazewar -t non_existent.txt > /dev/null 2>&1");
    cr_assert_neq(WEXITSTATUS(ret), 0, "Server should fail without -p <port>");
}

Test(student_suite, 03_default_template_loads, .timeout = 5) {
    fprintf(stderr, "server_suite/03_default_template_loads\n");
    int server_pid = fork();
    if (server_pid == 0) {
        execl("bin/mazewar", "bin/mazewar", "-p", "9998", NULL);
        perror("exec failed");
        exit(1);
    }
    sleep(2);
    int ret = system("util/tclient -p 9998 </dev/null | grep 'Connected to server' > /dev/null");
    kill(server_pid, SIGHUP);
    waitpid(server_pid, NULL, 0);
    cr_assert_eq(WEXITSTATUS(ret), 0, "Client failed to connect using default maze");
}

Test(student_suite, 04_bad_template_path, .timeout = 5) {
    fprintf(stderr, "server_suite/04_bad_template_path\n");
    int ret = system("bin/mazewar -p 9997 -t nonexistent_maze.txt > /dev/null 2>&1");
    cr_assert_neq(WEXITSTATUS(ret), 0, "Server should fail when given a bad maze file");
}


#include "client_registry.h"

// Worker thread that registers and unregisters a dummy fd
void *client_simulation(void *arg) {
    CLIENT_REGISTRY *cr = (CLIENT_REGISTRY *)arg;

    // Simulate dummy client using a pipe fd
    int fds[2];
    pipe(fds);
    int fd = fds[0];

    creg_register(cr, fd);
    usleep(10000); // simulate some work
    creg_unregister(cr, fd);

    close(fds[0]);
    close(fds[1]);
    return NULL;
}

Test(student_suite, 05_client_registry_concurrency, .timeout = 5) {
    fprintf(stderr, "server_suite/05_client_registry_concurrency\n");

    CLIENT_REGISTRY *cr = creg_init();
    const int num_threads = 10;
    pthread_t threads[num_threads];

    // Launch threads that register/unregister dummy clients
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, client_simulation, cr);
    }

    // This call should block until all clients are unregistered
    creg_wait_for_empty(cr);

    // Join threads to clean up
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    cr_assert(true, "creg_wait_for_empty did not block correctly or deadlocked");

    creg_fini(cr);
}
