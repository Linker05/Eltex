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
static int stop_requested = 0;

static void handle_int(int code) {
    stop_requested = 1;
}

static void send_message(const char *topic, const char *message) {
    msgbuf buf = {
        .mtype = SEND_TYPE
    };
    SendMsg msg = (SendMsg) {};
    strcpy(msg.topic, topic);
    strcpy(msg.message, message);\
    memcpy(&buf.mtext, &msg, sizeof(SendMsg));

    TRY_IO_OR_FAIL(msgsnd(broker_queue, &buf, sizeof(SendMsg), 0), "send msg");
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
}

static void send_hello() {
    msgbuf buf = {
        .mtype = HELLO_TYPE
    };

    const HelloMsg msg = (HelloMsg) {
        .pid = getpid(),
        .input = 0
    };
    memcpy(&buf.mtext, &msg, sizeof(HelloMsg));

    TRY_IO_OR_FAIL(msgsnd(broker_queue, &buf, sizeof(HelloMsg), 0), "send hello");
}

void publisher(const key_t broker_key, const char *topic) {
    signal(SIGINT, handle_int);
    broker_queue = msgget(broker_key, 0);
    if (broker_queue == -1) {
        perror("connect broker queue");
        exit(EXIT_FAILURE);
    }
    send_hello();
    while (1) {
        errno = 0;
        char message[MAX_MESSAGE_SIZE + 1] = {};

        printf("Message: ");
        message[0] = '\0';
        int length = 0;
        const int matched = scanf("%" STR(MAX_MESSAGE_SIZE) "[^\n]%*c%n", message, &length);

        if (stop_requested)
            break;

        if (matched < 1) {
            getchar();
            continue;
        }
        if (length == 0) {
            continue;
        }

        send_message(topic, message);
    }
    disconnect();
}