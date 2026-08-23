#include <sys/timerfd.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <errno.h>
#include <sys/epoll.h>

#include "driver.h"

#include "shared.h"

static int timer = 0;
static Status status = {
    .text = AVAILABLE_STATUS,
    .timer = 0
};


static void update_time() {
    struct itimerspec time = {};
    TRY_IO_OR_FAIL_NO_CLEANUP(timerfd_gettime(timer, &time), "get time");
    status.timer = time.it_value.tv_sec;
}

static void set_task(const DriverCommand *command) {
    const pid_t pid = getpid();
    DriverReply reply = {
        .pid = pid,
        .type = REPLY_SUCCESS,
        .status = status
    };
    if (strcmp(status.text, BUSY_STATUS) == 0) {
        update_time();
        reply.status = status;
        TRY_IO_OR_FAIL_NO_CLEANUP(mq_send(cli_queue, (const char*) &reply, sizeof(DriverReply), CONTROL_FLOW_PRIORITY),
            "send error reply");
        return;
    }

    const struct itimerspec time = (struct itimerspec) {
        .it_value = {
            .tv_sec = command->argument,
            .tv_nsec = 0
        },
        .it_interval = {}
    };

    status = (Status) {
        .text = BUSY_STATUS,
        .timer = command->argument
    };
    reply.status = status;

    TRY_IO_OR_FAIL_NO_CLEANUP(timerfd_settime(timer, 0, &time, NULL), "set timer");
    TRY_IO_OR_FAIL_NO_CLEANUP(mq_send(cli_queue, (const char*) &reply, sizeof(DriverReply), CONTROL_FLOW_PRIORITY),
        "send success reply");
}

static void check_status() {
    const pid_t pid = getpid();
    DriverReply reply = {
        .pid = pid,
        .type = REPLY_SUCCESS,
        .status = status
    };

    if (strcmp(status.text, BUSY_STATUS) == 0) {
        update_time();
        reply.status = status;
    }

    TRY_IO_OR_FAIL_NO_CLEANUP(mq_send(cli_queue, (const char*) &reply, sizeof(DriverReply), CONTROL_FLOW_PRIORITY),
        "send reply");
}

void driver() {
    const pid_t pid = getpid();
    char queue_name[12];
    sprintf(queue_name, "/%d", pid);
    mqd_t queue = 0;

    const struct mq_attr attrs = (struct mq_attr) {
        .mq_curmsgs = 0,
        .mq_flags = 0,
        .mq_msgsize = sizeof(DriverCommand),
        .mq_maxmsg = 10
    };
    TRY_IO_OR_FAIL_NO_CLEANUP(timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC), "create timer");
    TRY_IO_OR_FAIL_NO_CLEANUP(queue = mq_open(queue_name, O_RDWR | O_CREAT | O_EXCL, 0644, &attrs), "create queue");

    int epoll_fd = 0;
    TRY_IO_OR_FAIL_NO_CLEANUP(epoll_fd = epoll_create1(0), "create epoll");

    struct epoll_event timer_event = {
        .events = EPOLLIN,
        .data.fd = timer
    };
    struct epoll_event command_event = {
        .events = EPOLLIN,
        .data.fd = queue
    };
    TRY_IO_OR_FAIL_NO_CLEANUP(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer, &timer_event), "register timer");
    TRY_IO_OR_FAIL_NO_CLEANUP(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, queue, &command_event), "register commands");

    struct epoll_event events[2];

    while (1) {
        if (stop_requested)
            break;

        errno = 0;
        const int n = epoll_wait(epoll_fd, events, 2, -1);
        if (n == -1) {
            if (errno == EINTR)
                continue;
            perror("epoll");
            _exit(EXIT_FAILURE);
        }

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == queue) {
                DriverCommand command;
                unsigned int prio = 0;
                TRY_IO_OR_FAIL_NO_CLEANUP(mq_receive(queue, (char*) &command, sizeof(DriverCommand), &prio), "receive");
                if (command.type == SEND_TASK) {
                    set_task(&command);
                }
                else if (command.type == GET_STATUS) {
                    check_status();
                }
                else if (command.type == STOP) {
                    stop_requested = 1;
                    break;
                }
                else {
                    CERROR("invalid command type");
                    _exit(EXIT_FAILURE);
                }
            } else if (events[i].data.fd == timer) {
                status = (Status) {
                    .text = AVAILABLE_STATUS,
                    .timer = 0
                };
            }
        }
    }
    TRY_IO_OR_FAIL_NO_CLEANUP(mq_close(cli_queue), "close cli queue");
    TRY_IO_OR_FAIL_NO_CLEANUP(mq_close(queue), "close queue");
    TRY_IO_OR_FAIL_NO_CLEANUP(mq_unlink(queue_name), "unlink queue");
}
