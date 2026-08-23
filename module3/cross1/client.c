#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/udp.h>

#include "shared.h"

int sockfd = -1;
struct sockaddr_in server = {};
int stop_requested = 0;

static void start_session() {
    Message msg = {};
    msg.type = HELLO;

    TRY_IO_OR_FAIL(send_message(sockfd, &msg, &server, CLIENT_PORT), "send hello");
}

static void close_session() {
    Message msg = {};
    msg.type = DISCONNECT;

    TRY_IO_OR_FAIL(send_message(sockfd, &msg, &server, CLIENT_PORT), "send disconnect");
}

static void client() {
    start_session();
    while (1) {
        errno = 0;
        Message msg = {
            .type = ECHO_REQUEST
        };

        printf("Message: ");
        msg.content.request.text[0] = '\0';
        int length = 0;
        const int matched = scanf("%" STR(MAX_TEXT_LENGTH) "[^\n]%*c%n", msg.content.request.text, &length);
        if (stop_requested) {
            break;
        }
        if (matched < 1) {
            getchar();
            continue;
        }
        if (length == 0) {
            continue;
        }

        ssize_t result = send_message(sockfd, &msg, &server, CLIENT_PORT);

        if (result == 0) {
            break;
        }
        if (result == -1) {
            perror("send");
            exit(EXIT_FAILURE);
        }

        Packet pkt = {};
        while (1) {
            result = get_message(sockfd, &pkt);
            if (result == 0) {
                break;
            }
            if (result == -1) {
                perror("read");
                exit(EXIT_FAILURE);
            }

            if (pkt.udp->dest == htons(CLIENT_PORT)) {
                break;
            }
        }
        Message *data = pkt.data;
        if (data->type == STOP)
            return;
        if (data->type == ECHO_REPLY)
            printf("Reply: %s, %u\n", data->content.reply.text, data->content.reply.seq_number);
    }
    close_session();
}

static void int_handle(int code) {
    stop_requested = 1;
}

static void cleanup() {
    close(sockfd);
}

int main(const int argc, const char **argv) {
    if (argc != 2) {
        CERROR("Usage: program <server>");
        exit(EXIT_SUCCESS);
    }
    struct in_addr server_address = {};
    if (inet_pton(AF_INET, argv[1], &server_address) <= 0) {
        CERROR("Invalid server ip");
        exit(EXIT_FAILURE);
    }

    server = (struct sockaddr_in) {
        .sin_addr = server_address.s_addr,
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
        .sin_zero = {}
    };

    TRY_IO_OR_FAIL(sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP), "create socket");

    signal(SIGINT, int_handle);
    atexit(cleanup);

    client();

    return EXIT_SUCCESS;
}
