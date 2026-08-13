#ifndef INC_7_SHARED_H
#define INC_7_SHARED_H

#include <memory.h>
#include <linux/ip.h>

#define SERVER_PORT 50000
#define CLIENT_PORT 40000
#define MAX_TEXT_LENGTH 2047

#define MAX_UDP_MTU 65535

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
    ECHO_REQUEST,
    ECHO_REPLY,
    DISCONNECT,
    STOP
} MsgType;

typedef struct EchoRequestMsg {
    char text[MAX_TEXT_LENGTH + 1];
} EchoRequestMsg;

typedef struct EchoReplyMsg {
    char text[MAX_TEXT_LENGTH + 1];
    uint32_t seq_number;
} EchoReplyMsg;

typedef union MsgUnion {
    EchoRequestMsg request;
    EchoReplyMsg reply;
} MsgUnion;

typedef struct Message {
    MsgType type;
    MsgUnion content;
} Message;

typedef struct Packet {
    struct iphdr *ip;
    struct udphdr *udp;
    Message *data;
    uint8_t buffer[MAX_UDP_MTU];
} Packet;

static ssize_t send_message(const int sockfd, const Message *msg, const struct sockaddr_in *dst, const uint16_t src_port) {
    char buffer[sizeof(Message) + 8] = {};
    *(struct udphdr*)buffer = (struct udphdr) {
        .source = htons(src_port),
        .dest = dst->sin_port,
        .len = sizeof(Message) + 8,
        .check = 0
    };
    Message *data = (Message*) &buffer[8];
    memcpy(data, msg, sizeof(Message));
    errno = 0;
    return sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*) dst, sizeof(struct sockaddr_in));
}

static ssize_t get_message(const int sockfd, Packet *packet) {
    uint8_t *buffer = packet->buffer;
    errno = 0;
    const ssize_t result = recvfrom(sockfd, buffer, MAX_UDP_MTU, 0, NULL, NULL);
    packet->ip = (struct iphdr*) buffer;
    packet->udp = (struct udphdr*) ((char*)packet->ip + 4 * packet->ip->ihl);
    packet->data = (Message*) ((uint8_t*)packet->udp + 8);
    return result;
}

#endif
