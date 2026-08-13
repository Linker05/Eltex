#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include "shared.h"

int sockfd = -1;
int stop_requested = 0;

typedef struct ConnectionList ConnectionList;
typedef struct ConnectionList {
    struct sockaddr_in address;
    uint32_t counter;
    ConnectionList *next;
} ConnectionList;

ConnectionList *connections = NULL;

static int try_connection(const Packet *packet, const ConnectionList *current) {
    return current->address.sin_port == packet->udp->source && current->address.sin_addr.s_addr == packet->ip->saddr;
}

// Add client connection
static int add_connection(const Packet *hello) {
    ConnectionList **place = &connections;
    while (*place) {
        ConnectionList *current = *place;
        if (try_connection(hello, current)) {
            return -1;
        }
        place = &current->next;
    }
    *place = malloc(sizeof(ConnectionList));
    **place = (ConnectionList) {
        .address = {
            .sin_family = AF_INET,
            .sin_port = hello->udp->source,
            .sin_addr = {
                .s_addr = hello->ip->saddr
            },
            .sin_zero = {}
        },
        .next = NULL,
        .counter = 0
    };
    return 0;
}

// Remove client connection from list
static int remove_connection(const Packet *disconnect) {
    ConnectionList **place = &connections;
    while (*place) {
        ConnectionList *current = *place;
        if (try_connection(disconnect, current)) {
            *place = current->next;
            free(current);
            return 0;
        }
        place = &current->next;
    }
    return -1;
}

static ConnectionList *get_connection(const Packet *packet) {
    ConnectionList *current = connections;
    while (current) {
        if (try_connection(packet, current)) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

static void send_to_connection(const ConnectionList* connection, const Message *msg) {
    errno = 0;
    const ssize_t result = send_message(sockfd, msg, &connection->address, SERVER_PORT);
    if (result == -1) {
        perror("send");
        exit(EXIT_FAILURE);
    }
}

// Send to all client
static void send_to_clients(const Message *msg) {
    const ConnectionList *current = connections;
    while (current) {
        send_to_connection(current, msg);
        current = current->next;
    }
}

static void reply(ConnectionList* connection, const Message* request) {
    connection->counter++;
    Message reply = {
        .type = ECHO_REPLY,
        .content = {
            .reply = {
                .text = "",
                .seq_number = connection->counter
            }
        }
    };
    strcpy(reply.content.reply.text, request->content.request.text);
    send_to_connection(connection, &reply);
}

static void server() {
    while (1) {
        Packet pkt = {};
        while (1) {
            const ssize_t result = get_message(sockfd, &pkt);
            if (stop_requested)
                break;
            if (result == -1) {
                perror("read");
                exit(EXIT_FAILURE);
            }
            if (pkt.udp->dest == htons(SERVER_PORT)) {
                break;
            }
        }
        if (stop_requested)
            break;

        const Message *msg = pkt.data;
        switch (msg->type) {
            case HELLO: {
                printf("Received hello\n");
                add_connection(&pkt);
                break;
            }
            case ECHO_REQUEST: {
                printf("Received echo request\n");
                ConnectionList *connection = get_connection(&pkt);
                if (!connection)
                    break;
                reply(connection, msg);
                break;
            }
            case DISCONNECT: {
                printf("Received disconnect\n");
                remove_connection(&pkt);
                break;
            }
            default: {
                break;
            }
        }
    }

    const Message msg = {
        .type = STOP,
        .content = {}
    };
    send_to_clients(&msg);
}

static void int_handle(int code) {
    stop_requested = 1;
}

static void cleanup() {
    close(sockfd);
}

int main(const int argc, const char **argv) {
    TRY_IO_OR_FAIL(sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP), "create socket");

    const struct sigaction sa = {
        .sa_flags = 0,
        .sa_handler = int_handle
    };
    sigaction(SIGINT, &sa, NULL);
    atexit(cleanup);

    server();

    return EXIT_SUCCESS;
}
