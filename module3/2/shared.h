#ifndef INC_2_SHARED_H
#define INC_2_SHARED_H

#ifdef DEBUG
#define DEBUG_ONLY(...) __VA_ARGS__
#define DEBUG_LOG(fd, ...) fprintf(fd, __VA_ARGS__)
#else
#define DEBUG_ONLY(...)
#define DEBUG_LOG(fd, ...)
#endif

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

#define MAX_TOPIC_SIZE 256
#define MAX_MESSAGE_SIZE 1024

#define BROKER_PID_FILE "/tmp/broker.pid"
#define LISTENER_PID_FILE_FORMAT "/tmp/listener%d.pid"

#define DISCONNECT_ACK_TYPE 5
#define HELLO_TYPE 4
#define DISCONNECT_TYPE 3
#define SUBSCRIBE_TYPE 2
#define SEND_TYPE 1

#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

typedef struct HelloMsg {
    pid_t pid;
    key_t input;
} HelloMsg;

typedef struct SubscribeMsg {
    pid_t pid;
    char topic[MAX_TOPIC_SIZE];
} SubscribeMsg;

typedef struct SendMsg {
    char topic[MAX_TOPIC_SIZE];
    char message[MAX_MESSAGE_SIZE + 1];
} SendMsg;

typedef struct DisconnectMsg {
    pid_t pid;
} DisconnectMsg;

typedef struct MsgUnion {
    HelloMsg hello;
    SubscribeMsg subscribe;
    SendMsg send;
    DisconnectMsg disconnect;
} MsgUnion;

typedef struct msgbuf {
    long mtype;
    char mtext[sizeof(MsgUnion)];
} msgbuf;

#endif
