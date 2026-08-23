#ifndef INC_4_SHARED_H
#define INC_4_SHARED_H

// do-while to protect macro from usage with else block
#define TRY_IO_OR_FAIL_IMPL(operation, failure_msg, exit_handler) \
    do { \
        if ((operation) == -1) { \
            perror(failure_msg); \
            exit_handler(EXIT_FAILURE); \
        } \
    } while (0)
// For main processes - exit with exit()
#define TRY_IO_OR_FAIL(operation, failure_msg) TRY_IO_OR_FAIL_IMPL(operation, failure_msg, exit)

#define MAX_TOPIC_SIZE 256
#define MAX_MESSAGE_SIZE 1024

#define BROKER_PID_FILE "/tmp/broker.pid"
#define LISTENER_PID_FILE_FORMAT "/tmp/listener%d.pid"

#define DISCONNECT_ACK_TYPE 5
#define HELLO_TYPE 4
#define DISCONNECT_TYPE 3
#define SUBSCRIBE_TYPE 2
#define SEND_TYPE 1

#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

// Macro for errors not using errno
#define CERROR(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

#define SHM_SIZE 1024
#define KEY_FILE "/tmp/shared_memory.run"

static int shm = -1;
static int sem = -1;

typedef struct Data {
    int count;
    long next_offset;
    int numbers[];
} Data;

static struct sembuf attach_cmd[1] = {
    {1, 1, SEM_UNDO}
};
static struct sembuf detach_cmd[1] = {
    {1, -1, SEM_UNDO}
};
static struct sembuf free_wait_cmd[1] = {
    {1, 0 ,0}
};
static struct sembuf lock_cmd[2] = {
    {0, 0, 0},
    {0, 1, SEM_UNDO}
};
static struct sembuf unlock_cmd[1] = {
    {0, -1, SEM_UNDO}
};

static int lock() {
    return semop(sem, lock_cmd, 2);
}

static int unlock() {
    return semop(sem, unlock_cmd, 1);
}

static void *attach() {
    if (semop(sem, attach_cmd, 1) == -1)
        return (void*)-1;
    return shmat(shm, NULL, 0);
}

static int detach(const void *mem) {
    if (shmdt(mem) == -1)
        return -1;
    return semop(sem, detach_cmd, 1);
}

static int wait_for_mem() {
    return semop(sem, free_wait_cmd, 1);
}

#endif
