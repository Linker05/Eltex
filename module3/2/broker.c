#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/msg.h>

#include "shared.h"

typedef struct SubscriptionList SubscriptionList;
typedef struct ClientList ClientList;

typedef struct SubscriptionList {
    pid_t subscriber;
    char topic[MAX_TOPIC_SIZE];
    SubscriptionList *next;
} SubscriptionList;

typedef struct ClientList {
    pid_t pid;
    key_t output;
    ClientList *next;
} ClientList;

static SubscriptionList *subscriptions = NULL;
static ClientList *clients = NULL;

static int stop_requested = 0;
static int broker_queue = -1;

static ClientList *get_client(const pid_t pid) {
    ClientList *current = clients;
    while (current && current->pid != pid) {
        current = current->next;
    }
    return current;
}

static void add_client(const HelloMsg msg) {
    printf("Connected %d\n", msg.pid);
    ClientList **place = &clients;
    while (*place) {
        place = &(*place)->next;
    }
    *place = malloc(sizeof(ClientList));
    if (!*place) {
        return;
    }
    **place = (ClientList) {
        .pid = msg.pid,
        .output = msg.input,
        .next = NULL
    };
}

static void remove_subscriptions_for(const pid_t pid) {
    SubscriptionList **place = &subscriptions;
    while (*place) {
        if ((*place)->subscriber == pid) {
            SubscriptionList *tmp = *place;
            *place = tmp->next;
            free(tmp);
        } else {
            place = &(*place)->next;
        }
    }
}

static void disconnect_client(const DisconnectMsg msg) {
    printf("Disconnected %d\n", msg.pid);
    ClientList **place = &clients;
    while (*place) {
        if ((*place)->pid == msg.pid) {
            ClientList *tmp = *place;
            if (tmp->output != 0) {
                const int queue = msgget(tmp->output, 0);
                const msgbuf buf = {
                    .mtype = DISCONNECT_ACK_TYPE,
                    .mtext = {}
                };
                // Send if client has its own input channel (it is a listener)
                TRY_IO_OR_FAIL(msgsnd(queue, &buf, 0, 0), "send disconnect");
            }
            *place = tmp->next;
            free(tmp);
            remove_subscriptions_for(msg.pid);
            return;
        }
        place = &(*place)->next;
    }
}

static void add_subscription(const SubscribeMsg msg) {
    printf("Subscribed %d to %s\n", msg.pid, msg.topic);
    SubscriptionList **place = &subscriptions;
    while (*place) {
        if (msg.pid == (*place)->subscriber && strcmp((*place)->topic, msg. topic) == 0) {
            return;
        }
        place = &(*place)->next;
    }
    *place = malloc(sizeof(SubscriptionList));
    if (!*place) {
        return;
    }
    **place = (SubscriptionList) {
        .subscriber = msg.pid,
        .next = NULL
    };
    strcpy((*place)->topic, msg.topic);
}

static void send_message_impl(const SendMsg msg, const key_t key) {
    const int client = msgget(key, 0);
    if (client == -1) {
        perror("connect to client");
        exit(EXIT_FAILURE);
    }
    msgbuf buf = {};
    buf.mtype = SEND_TYPE;
    memcpy(buf.mtext, &msg, sizeof(SendMsg));
    TRY_IO_OR_FAIL(msgsnd(client, &buf, sizeof(SendMsg), 0), "send msg");
}

static void send_message(const SendMsg msg) {
    const SubscriptionList *sub = subscriptions;
    while (sub) {
        if (strcmp(sub->topic, msg. topic) == 0) {
            const ClientList *client = get_client(sub->subscriber);
            send_message_impl(msg, client->output);
        }
        sub = sub->next;
    }
}

static void handle_int(int code) {
    stop_requested = 1;
}

static void cleanup() {
    msgctl(broker_queue, IPC_RMID, NULL);
    broker_queue = -1;
}

static void signal_disconnect_and_wait() {
    const ClientList *client = clients;
    while (client) {
        kill(client->pid, SIGINT);
        client = client->next;
    }
    msgbuf buf = {};
    while (clients) {
        TRY_IO_OR_FAIL(msgrcv(broker_queue, &buf, sizeof(MsgUnion), 0, 0), "wait for disconnect");
        if (buf.mtype != DISCONNECT_TYPE)
            continue;
        const DisconnectMsg msg = *(DisconnectMsg*)buf.mtext;
        disconnect_client(msg);
    }
}

void broker(const key_t input_key) {
    signal(SIGINT, handle_int);
    broker_queue = msgget(input_key, IPC_CREAT | IPC_EXCL | 0640);
    if (broker_queue == -1) {
        perror("create broker queue");
        exit(EXIT_FAILURE);
    }
    atexit(cleanup);
    msgbuf buf = {};
    while (1) {
        errno = 0;
        while (msgrcv(broker_queue, &buf, sizeof(MsgUnion), 0, IPC_NOWAIT) != -1) {
            if (stop_requested) {
                break;
            }
            switch (buf.mtype) {
                case HELLO_TYPE: {
                    const HelloMsg msg = *(HelloMsg*)buf.mtext;
                    add_client(msg);
                    break;
                }
                case DISCONNECT_TYPE: {
                    const DisconnectMsg msg = *(DisconnectMsg*)buf.mtext;
                    disconnect_client(msg);
                    break;
                }
                case SUBSCRIBE_TYPE: {
                    const SubscribeMsg msg = *(SubscribeMsg*)buf.mtext;
                    add_subscription(msg);
                    break;
                }
                case SEND_TYPE: {
                    const SendMsg msg = *(SendMsg*)buf.mtext;
                    send_message(msg);
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
    printf("Stop scheduled, all messages except disconnects will be ignored\n");
    signal_disconnect_and_wait();
}