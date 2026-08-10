#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>

#include "shared.h"

#define MAX_CONNECTIONS 63
#define NO_EXCEPT_CLIENT (-1)

int sockfd = -1;
int stop_requested = 0;

nfds_t connections_count = 0;
struct pollfd polled[MAX_CONNECTIONS + 1] = {};

// Add client connection to polling
static int add_connection(const int fd) {
    if (connections_count == MAX_CONNECTIONS)
        return -1;
    const struct pollfd sock = {
        .fd = fd,
        .events = POLLIN,
        .revents = 0
    };
    for (int i = 1; i < connections_count + 1; i++) {
        if (polled[i].fd == fd)
            return -1;
    }
    polled[connections_count + 1] = sock;
    connections_count++;
    return 0;
}

// Remove client connection from polled list
static int remove_connection(const int fd) {
    if (connections_count == 0)
        return -1;
    int i = 0;
    for (i = 1; i < connections_count + 1; i++) {
        if (polled[i].fd == fd)
            break;
    }
    if (i == connections_count + 1)
        return -1;
    for (int j = i; i < connections_count; i++) {
        polled[j] = polled[j+1];
    }
    close(fd);
    connections_count--;
    return 0;
}

// Send to all client except client with descriptor fd
static void send_to_clients_except(const int fd, const Message *msg_ptr) {
    for (int i = 1; i < connections_count + 1; i++) {
        if (polled[i].fd == fd)
            continue;
        TRY_IO_OR_FAIL(send(polled[i].fd, msg_ptr, sizeof(Message), 0), "send");
    }
}

static void process_message(const int fd) {
    Message msg;

    const int err = read_message(fd, &msg);
    if (err == 0) {
        if (remove_connection(fd) == 0) {
            send_to_clients_except(fd, &msg);
            return;
        }
        CERROR("disconnect client");
        exit(EXIT_FAILURE);
    }
    if (err == -1) {
        if (errno == EINTR) {
            return;
        }
        perror("read message");
        exit(EXIT_FAILURE);
    }

    switch (msg.type) {
        case HELLO: {
            printf("Hello from %s\n", msg.content.hello.client_name);
            send_to_clients_except(fd, &msg);
            break;
        }
        case SEND_FILE:
        case TEXT: {
            printf("Data from %s\n", msg.content.data.client_name);
            send_to_clients_except(fd, &msg);
            break;
        }
        case DISCONNECT: {
            printf("Disconnect from %s\n", msg.content.disconnect.client_name);
            if (remove_connection(fd) == 0)
                send_to_clients_except(fd, &msg);
            break;
        }
        default: {
            CERROR("invalid message type");
            exit(EXIT_FAILURE);
        }
    }
}

static void server() {
    const struct pollfd listen_socket = {
        .fd = sockfd,
        .events = POLLIN,
        .revents = 0
    };
    polled[0] = listen_socket;
    while (1) {
        errno = 0;
        const int error = poll(polled, connections_count + 1, -1);
        if (error == -1 && errno != EINTR) {
            perror("poll");
            exit(EXIT_FAILURE);
        }
        if (stop_requested) {
            break;
        }
        if (polled[0].revents & POLLIN) {
            // New connection
            struct sockaddr_in inet = {};
            socklen_t address_length = sizeof(struct sockaddr_in);
            int client_fd;
            TRY_IO_OR_FAIL(client_fd = accept(sockfd, (struct sockaddr*) &inet, &address_length), "accept");
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &inet.sin_addr, ip, INET_ADDRSTRLEN);
            printf("Accepted connection from %s:%d\n", ip, inet.sin_port);
            add_connection(client_fd);
        }
        for (int i = 1; i < connections_count + 1; i++) {
            // Message from connected client
            if (polled[i].revents & POLLIN) {
                process_message(polled[i].fd);
            }
        }
    }

    const Message msg = {
        .type = STOP,
        .content = {}
    };
    send_to_clients_except(NO_EXCEPT_CLIENT, &msg);
}

static void int_handle(int code) {
    stop_requested = 1;
}

static void cleanup() {
    close(sockfd);
    for (int i = 1; i < connections_count + 1; i++)
        close(polled[i].fd);
}

int main(const int argc, const char **argv) {
    TRY_IO_OR_FAIL(sockfd = socket(AF_INET, SOCK_STREAM, 0), "create socket");

    const struct sockaddr_in bind_address = {
        .sin_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_zero = {}
    };
    TRY_IO_OR_FAIL(bind(sockfd, (const struct sockaddr*) &bind_address,
        sizeof(struct sockaddr_in)), "bind socket");
    TRY_IO_OR_FAIL(listen(sockfd, 5), "listen");

    signal(SIGINT, int_handle);
    atexit(cleanup);

    server();

    return EXIT_SUCCESS;
}
