#ifndef INC_5_SHARED_H
#define INC_5_SHARED_H

#include <semaphore.h>

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
#define SHM_NAME "/shm"
#define SEM_NAME "/shm_write"
#define SEM_ATTACHED_NAME "/shm_attached"

static int shm = -1;
static sem_t *sem = NULL;
static sem_t *attached = NULL;

typedef struct Data {
    int count;
    long next_offset;
    int numbers[];
} Data;

static int lock() {
    return sem_wait(sem);
}

static int unlock() {
    return sem_post(sem);
}

static void *attach() {
    if (sem_post(attached) == -1)
        return MAP_FAILED;
    return mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
}

static int detach(void *shared) {
    if (sem_trywait(attached) == -1)
        return -1;
    return munmap(shared, SHM_SIZE);
}

static int wait_for_mem() {
    int value = 1;
    while (value != 0) {
        if(sem_getvalue(attached, &value) == -1)
            return -1;
        sleep(1);
    }
    return 0;
}

#endif
