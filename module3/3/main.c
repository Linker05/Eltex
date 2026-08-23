#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/wait.h>
#include <mqueue.h>

#define HANDSHAKE_PRIORITY 20
#define STOP_PRIORITY 10
#define MSG_PRIORITY 1
#define MAX_MSG_LENGTH 2048

#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

// Macro for errors not using errno
#define CERROR(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

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

static int is_stop_initiator = 0;
static int stop_requested = 0;
static int is_session_up = 0;
static int is_master = 0;

// Wait for other client to stop
static void wait_for_stop_ack(const mqd_t rd, char *buffer) {
    unsigned int priority = 0;
    errno = 0;
    // While queue is empty
    while (errno == 0 || errno == EAGAIN) {
        sleep(1);
        errno = 0;
        while (mq_receive(rd, buffer, MAX_MSG_LENGTH + 1, &priority) != -1) {
            // Ignore messages that not stop
            if (priority == STOP_PRIORITY) {
                return;
            }
        }
    }
}

static void read_messages(const mqd_t rd, char *buffer) {
    unsigned int priority = 0;
    errno = 0;
    while (mq_receive(rd, buffer, MAX_MSG_LENGTH + 1, &priority) != -1) {
        if (priority == MSG_PRIORITY) {
            // If message - print it
            printf("Received: %s\n", buffer);
        } else if (priority == STOP_PRIORITY) {
            // If stop signal - raise a flag
            stop_requested = 1;
        }

        if (stop_requested) {
            break;
        }
    }
}

static void start_session(const mqd_t rd, const mqd_t wd, char *buffer) {
    unsigned int priority = 0;
    TRY_IO_OR_FAIL(mq_send(wd, "HELLO", 6, HANDSHAKE_PRIORITY), "send handshake");
    errno = 0;
    while (errno == 0 || errno == EAGAIN) {
        sleep(1);
        errno = 0;
        // Stop handshake if stop requested
        if (stop_requested) {
            return;
        }
        while (mq_receive(rd, buffer, MAX_MSG_LENGTH + 1, &priority) != -1) {
            // If first message not handshake it is an error
            if (priority != HANDSHAKE_PRIORITY) {
                CERROR("read handshake");
                exit(EXIT_FAILURE);
            }
            is_session_up = 1;
            return;
        }
    }
}

static void close_session(const mqd_t rd, const mqd_t wd, char *buffer) {
    // Send stop signal to other client
    TRY_IO_OR_FAIL(mq_send(wd, "STOP", 5, STOP_PRIORITY), "send stop");
    // If we are master client, wait for other side to stop before deleting queues
    if (is_stop_initiator && is_master) {
        wait_for_stop_ack(rd, buffer);
    }
    is_session_up = 0;
}

static void messenger(const mqd_t rd, const mqd_t wd) {
    char buffer[MAX_MSG_LENGTH + 1] = {};
    printf("Waiting for other side...\n");
    start_session(rd, wd, buffer);
    while (1) {
        read_messages(rd, buffer);
        if (stop_requested && is_session_up) {
            close_session(rd, wd, buffer);
            return;
        }
        if (errno != EAGAIN) {
            perror("receiving msg");
            exit(EXIT_FAILURE);
        }
        printf("Message (empty to just receive): ");
        buffer[0] = '\0';
        int length = 0;
        const int matched = scanf("%" STR(MAX_MSG_LENGTH) "[^\n]%*c%n", buffer, &length);
        if (matched < 1) {
            getchar();
            continue;
        }
        if (length == 0) {
            continue;
        }
        TRY_IO_OR_FAIL(mq_send(wd, buffer, length + 1, MSG_PRIORITY), "send msg");
    }
}

static void int_handle(int code) {
    stop_requested = 1;
    is_stop_initiator = 1;
}

int main(const int argc, const char **argv) {
    if (argc != 2 || argv[1][0] == '\0') {
        printf("Usage: program <queue name>\n");
        return EXIT_SUCCESS;
    }
    const size_t name_length = strlen(argv[1]);
    char *queue_file1 = calloc(sizeof(char), name_length + 4);
    char *queue_file2 = calloc(sizeof(char), name_length + 4);
    queue_file1[0] = '/';
    queue_file2[0] = '/';
    strcat(queue_file1, argv[1]);
    strcat(queue_file2, argv[1]);
    strcat(queue_file1, "_1");
    strcat(queue_file2, "_2");

    mqd_t q1 = mq_open(queue_file1, O_WRONLY);

    signal(SIGINT, int_handle);

    if (q1 == -1) {
        // Queues not exists, we are first client
        is_master = 1;
        struct mq_attr attr = (struct mq_attr) {
            .mq_curmsgs = 0,
            .mq_flags = 0,
            .mq_msgsize = MAX_MSG_LENGTH + 1,
            .mq_maxmsg = 10
        };
        q1 = mq_open(queue_file1, O_RDONLY | O_CREAT | O_NONBLOCK, 0644, &attr);
        const mqd_t q2 = mq_open(queue_file2, O_WRONLY | O_CREAT, 0644, &attr);
        if (q1 == -1 || q2 == -1) {
            perror("create queues");
            exit(EXIT_FAILURE);
        }

        printf("It is a master client\n");
        messenger(q1, q2);

        mq_close(q1);
        mq_close(q2);
        mq_unlink(queue_file1);
        mq_unlink(queue_file2);
    } else {
        // Queues exists, we are second client
        is_master = 0;
        const mqd_t q2 = mq_open(queue_file2, O_RDONLY| O_NONBLOCK);
        if (q2 == -1) {
            perror("open queues");
            exit(EXIT_FAILURE);
        }

        printf("It is a slave client\n");
        messenger(q2, q1);

        mq_close(q1);
        mq_close(q2);
    }

    free(queue_file1);
    free(queue_file2);

    return EXIT_SUCCESS;
}
