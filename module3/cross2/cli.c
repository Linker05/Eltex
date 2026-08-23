#include <sys/timerfd.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <errno.h>
#include <sys/epoll.h>
#include <signal.h>

#include "cli.h"

#include "shared.h"
#include "driver.h"

static int drivers[MAX_DRIVERS] = {};
static int drivers_count = 0;

static int check_pid(const int pid) {
    for (int i = 0; i < drivers_count; i++)
        if (drivers[i] == pid)
            return 1;
    return 0;
}

static void disconnect_driver(const int pid) {
    int i = 0;
    for (i = 0; i < drivers_count; i++)
        if (drivers[i] == pid)
            break;
    if (i == drivers_count) {
        CERROR("driver not connected");
        exit(EXIT_FAILURE);
    }

    char queue_name[12];
    sprintf(queue_name, "/%d", pid);
    mqd_t queue = -1;
    const DriverCommand command = {
        .type = STOP
    };

    TRY_IO_OR_FAIL(queue = mq_open(queue_name, O_WRONLY), "open queue");
    TRY_IO_OR_FAIL(mq_send(queue, (const char*) &command, sizeof(DriverCommand), EXIT_PRIORITY), "send disconnect");
    TRY_IO_OR_FAIL(mq_close(queue), "close queue");

    for (; i < drivers_count - 1; i++)
        drivers[i] = drivers[i+1];
    drivers_count--;
}

static void connect_driver(const int pid) {
    if (check_pid(pid)) {
        CERROR("driver already connected");
        exit(EXIT_FAILURE);
    }
    if (drivers_count == MAX_DRIVERS) {
        CERROR("max count of drivers");
        exit(EXIT_FAILURE);
    }
    drivers[drivers_count] = pid;
    drivers_count++;
}

static void read_reply_from(const int pid, DriverReply *reply) {
    while (1) {
        unsigned int prio = 0;
        TRY_IO_OR_FAIL(mq_receive(cli_queue, (char*) reply, sizeof(DriverReply), &prio), "receive reply");
        if (reply->pid == pid)
            return;
    }
}

static int send_command(const int pid, const DriverCommand *command, DriverReply *reply) {
    if (!check_pid(pid)) {
        printf("Invalid driver PID\n");
        return -1;
    }
    char queue_name[12];
    sprintf(queue_name, "/%d", pid);
    mqd_t queue = -1;
    TRY_IO_OR_FAIL(queue = mq_open(queue_name, O_WRONLY), "open queue");
    TRY_IO_OR_FAIL(mq_send(queue, (const char*) command, sizeof(DriverCommand), CONTROL_FLOW_PRIORITY), "send command");
    read_reply_from(pid, reply);
    TRY_IO_OR_FAIL(mq_close(queue), "close queue");
    return 0;
}

static void send_task(const int pid, const long time) {
    DriverReply reply;
    const DriverCommand command =  (DriverCommand) {
        .type = SEND_TASK,
        .argument = time
    };
    if (send_command(pid, &command, &reply) != -1)
        if (reply.type == REPLY_ERROR)
            printf("Driver %d is busy. Reply: %s %lu\n", pid, reply.status.text, reply.status.timer);
}

static void get_status(const int pid) {
    DriverReply reply;
    const DriverCommand command =  (DriverCommand) {
        .type = GET_STATUS
    };
    if (send_command(pid, &command, &reply) != -1) {
        printf("PID: %d - Status: %s", pid, reply.status.text);
        if (reply.status.timer != 0)
            printf(" %lu", reply.status.timer);
        printf("\n");
    }
}

static void get_drivers() {
    printf("Drivers list:\n");
    for (int i = 0; i < drivers_count; i++) {
        get_status(drivers[i]);
    }
}

static void cleanup() {
    while (drivers_count > 0) {
        disconnect_driver(drivers[0]);
    }
    mq_unlink(CLI_QUEUE_NAME);
}

void cli() {
    atexit(cleanup);
    while (1) {
        char buffer[MAX_CMD_LENGTH + 1] = "";
        printf("Command> ");
        const int scanned = scanf("%" STR(MAX_CMD_LENGTH) "[^\n]%*c", buffer);
        if (stop_requested)
            break;
        if (scanned < 1) {
            getchar();
            continue;
        }

        const size_t len = strlen(buffer);
        for (int i = 0; i < len; i++)
            if (buffer[i] == ' ')
                buffer[i] = '\0';
        const char *cmd = buffer;
        const char *arg1 = cmd + strlen(cmd) + 1;
        const char *arg2 = arg1 + strlen(arg1) + 1;

        if (strcmp(cmd, "create_driver") == 0) {
            const int result = fork();
            if (result != 0) {
                connect_driver(result);
                printf("Driver PID: %d\n", result);
                continue;
            }
            signal(SIGINT, SIG_IGN);
            driver();
            _exit(EXIT_SUCCESS);
        }

        if (strcmp(cmd, "send_task") == 0) {
            if (arg1[0] == '\0') {
                printf("PID required\n");
                continue;
            }
            if (arg2[0] == '\0' || arg2 == arg1) {
                printf("Task timer required\n");
                continue;
            }
            char *end = NULL;
            errno = 0;
            const long pid = strtol(arg1, &end, 10);
            if (errno != 0 || *end != '\0') {
                printf("Invalid pid\n");
                continue;
            }
            errno = 0;
            const long time = strtol(arg2, &end, 10);
            if (errno != 0|| *end != '\0') {
                printf("Invalid timer\n");
                continue;
            }
            send_task((int)pid, time);
        } else if (strcmp(cmd, "get_status") == 0) {
            if (arg1[0] == '\0') {
                printf("PID required\n");
                continue;
            }
            char *end = NULL;
            errno = 0;
            const long pid = strtol(arg1, &end, 10);
            if (errno != 0 || *end != '\0') {
                printf("Invalid pid\n");
                continue;
            }
            get_status((int)pid);

        } else if (strcmp(cmd, "get_drivers") == 0) {
            get_drivers();
        } else {
            printf("Unknown command\n");
        }
    }
}
