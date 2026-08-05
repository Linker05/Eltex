#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/msg.h>

#include "shared.h"

static int broker_queue = -1;
static int input_queue = -1;
static int stop_requested = 0;

static void handle_int(int code) {
    stop_requested = 1;
}

static void disconnect() {
    msgbuf buf = {
        .mtype = DISCONNECT_TYPE
    };

    const DisconnectMsg msg = (DisconnectMsg) {
        .pid = getpid()
    };
    memcpy(&buf.mtext, &msg, sizeof(DisconnectMsg));

    TRY_IO_OR_FAIL(msgsnd(broker_queue, &buf, sizeof(DisconnectMsg), 0), "send disconnect");
    // Wait for ack to close input
    TRY_IO_OR_FAIL(msgrcv(input_queue, &buf, 0, DISCONNECT_ACK_TYPE, 0), "receive disconnect ack");
}

static void send_hello(const key_t queue_key) {
    msgbuf buf = {
        .mtype = HELLO_TYPE
    };

    const HelloMsg msg = (HelloMsg) {
        .pid = getpid(),
        .input = queue_key
    };
    memcpy(&buf.mtext, &msg, sizeof(HelloMsg));

    TRY_IO_OR_FAIL(msgsnd(broker_queue, &buf, sizeof(HelloMsg), 0), "send hello");
}

static void subscribe(const int topics_count, const char **topics) {
    msgbuf buf = {
        .mtype = SUBSCRIBE_TYPE
    };
    const pid_t pid = getpid();

    for (int i = 0; i < topics_count; i++) {
        SubscribeMsg message = {
            .pid = pid
        };
        strcpy(message.topic, topics[i]);
        memcpy(&buf.mtext, &message, sizeof(SubscribeMsg));
        TRY_IO_OR_FAIL(msgsnd(broker_queue, &buf, sizeof(SubscribeMsg), 0), "send subscribe");
    }
}

static void cleanup() {
    msgctl(input_queue, IPC_RMID, NULL);
    input_queue = -1;
}

void listener(const key_t broker_key, const int topics_count, const char **topics, const key_t input_key) {
    signal(SIGINT, handle_int);
    broker_queue = msgget(broker_key, 0);
    if (broker_queue == -1) {
        perror("connect broker queue");
        exit(EXIT_FAILURE);
    }
    input_queue = msgget(input_key, IPC_CREAT | IPC_EXCL | 0640);
    if (input_queue == -1) {
        perror("create input queue");
        exit(EXIT_FAILURE);
    }
    atexit(cleanup);
    msgbuf buf = {};

    send_hello(input_key);
    subscribe(topics_count, topics);
    while (1) {
        errno = 0;
        while (msgrcv(input_queue, &buf, sizeof(MsgUnion), 0, IPC_NOWAIT) != -1) {
            if (stop_requested)
                break;
            switch (buf.mtype) {
                case SEND_TYPE: {
                    SendMsg msg = *(SendMsg*)buf.mtext;
                    printf("[%s]: %s\n", msg.topic, msg.message);
                    break;
                }
                default: {
                    perror("unknown message type");
                    exit(EXIT_FAILURE);
                }
            }
        }
        if (stop_requested)
            break;
        if (errno != ENOMSG) {
            perror("receive msg");
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }
    disconnect();
}