#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 50000
#define MAX_NAME_LENGTH 127
#define MAX_TEXT_LENGTH 2047
#define VM_BROADCAST "192.168.174.255"

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

typedef enum MsgType {
    HELLO,
    DATA,
    DISCONNECT
} MsgType;

typedef struct HelloMsg {
    char client_name[MAX_NAME_LENGTH + 1];
} HelloMsg;

typedef struct DataMsg {
    char client_name[MAX_NAME_LENGTH + 1];
    char text[MAX_TEXT_LENGTH + 1];
} DataMsg;

typedef struct DisconnectMsg {
    char client_name[MAX_NAME_LENGTH + 1];
} DisconnectMsg;

typedef union MsgUnion {
    HelloMsg hello;
    DataMsg data;
    DisconnectMsg disconnect;
} MsgUnion;

typedef struct Message {
    MsgType type;
    MsgUnion content;
} Message;

int sockfd = -1;
int stop_requested = 0;
const char *client_name = NULL;
struct sockaddr_in broadcast = {};

static void print_prompt() {
    printf("\nMessage (empty to just receive): ");
    fflush(stdout);
}

static void read_messages() {
    Message msg = {};
    struct sockaddr_in inet = {};
    socklen_t address_length = sizeof(struct sockaddr_in);
    TRY_IO_OR_FAIL(recvfrom(sockfd, &msg, sizeof(Message), 0,
        (struct sockaddr*) &inet, &address_length), "get message");

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &inet.sin_addr, ip, INET_ADDRSTRLEN);

    switch (msg.type) {
        case HELLO: {
            printf("\nClient %s connected at %s:%d", msg.content.hello.client_name, ip, ntohs(inet.sin_port));
            break;
        }
        case DATA: {
            printf("\n[%s]: %s", msg.content.data.client_name, msg.content.data.text);
            break;
        }
        case DISCONNECT: {
            printf("\nClient %s disconnected", msg.content.disconnect.client_name);
            break;
        }
        default: {
            CERROR("invalid message type");
            exit(EXIT_FAILURE);
        }
    }

    print_prompt();
}

static void send_message() {
    Message msg = {};
    msg.type = DATA;
    strcpy(msg.content.data.client_name, client_name);

    if (!fgets(msg.content.data.text, MAX_TEXT_LENGTH, stdin)) {
        CERROR("input error");
        exit(EXIT_FAILURE);
    }
    const size_t len = strlen(msg.content.data.text);
    if (msg.content.data.text[len - 1] != '\0') {
        msg.content.data.text[len - 1] = '\0';
    }

    if (msg.content.data.text[0] == '\0') {
        print_prompt();
        return;
    }

    TRY_IO_OR_FAIL(sendto(sockfd, &msg, sizeof(Message), 0,
        (const struct sockaddr*) &broadcast, sizeof(struct sockaddr_in)), "send message");
}

static void start_session() {
    Message msg = {};
    msg.type = HELLO;
    strcpy(msg.content.hello.client_name, client_name);

    TRY_IO_OR_FAIL(sendto(sockfd, &msg, sizeof(Message), 0,
        (const struct sockaddr*) &broadcast, sizeof(struct sockaddr_in)), "send hello");
}

static void close_session() {
    Message msg = {};
    msg.type = DISCONNECT;
    strcpy(msg.content.disconnect.client_name, client_name);

    TRY_IO_OR_FAIL(sendto(sockfd, &msg, sizeof(Message), 0,
        (const struct sockaddr*) &broadcast, sizeof(struct sockaddr_in)), "send message");
}

static void messenger() {
    fd_set inputs;
    const int maxfd = STDIN_FILENO > sockfd ? STDIN_FILENO : sockfd;
    start_session();
    while (1) {
        FD_ZERO(&inputs);
        FD_SET(sockfd, &inputs);
        FD_SET(STDIN_FILENO, &inputs);
        errno = 0;
        const int result = select(maxfd + 1, &inputs, NULL, NULL, NULL);
        if (result == -1 && errno != EINTR) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        if (stop_requested) {
            close_session();
            return;
        }

        if (FD_ISSET(STDIN_FILENO, &inputs)) {
            send_message();
        }
        if (FD_ISSET(sockfd, &inputs)) {
            read_messages();
        }
    }
}

static void int_handle(int code) {
    stop_requested = 1;
}

static void cleanup() {
    close(sockfd);
}

int main(const int argc, const char **argv) {
    if (argc != 2) {
        CERROR("Usage: program <username>");
        exit(EXIT_SUCCESS);
    }
    client_name = argv[1];
    const int enable = 1;
    TRY_IO_OR_FAIL(sockfd = socket(AF_INET, SOCK_DGRAM, 0), "create socket");
    TRY_IO_OR_FAIL(setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(int)), "enable broadcast");

    const struct sockaddr_in bind_address = {
        .sin_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_zero = {}
    };
    TRY_IO_OR_FAIL(bind(sockfd, (const struct sockaddr*) &bind_address,
        sizeof(struct sockaddr_in)), "bind socket");

    signal(SIGINT, int_handle);
    atexit(cleanup);

    broadcast = (struct sockaddr_in) {
        .sin_addr = inet_addr(VM_BROADCAST),
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_zero = {}
    };

    messenger();

    return EXIT_SUCCESS;
}
