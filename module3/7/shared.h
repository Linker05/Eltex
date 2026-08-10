#ifndef INC_7_SHARED_H
#define INC_7_SHARED_H

#define PORT 50000
#define MAX_NAME_LENGTH 127
#define MAX_FILENAME_LENGTH 127
#define MAX_TEXT_LENGTH 2047
#define MAX_FILE_LENGTH 102400
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
    TEXT,
    SEND_FILE,
    DISCONNECT,
    STOP
} MsgType;

typedef struct HelloMsg {
    char client_name[MAX_NAME_LENGTH + 1];
} HelloMsg;

typedef struct TextMsg {
    char client_name[MAX_NAME_LENGTH + 1];
    char text[MAX_TEXT_LENGTH + 1];
} TextMsg;

typedef struct FileMsg {
    char client_name[MAX_NAME_LENGTH + 1];
    long int filesize;
    char filename[MAX_FILENAME_LENGTH + 1];
    char data[MAX_FILE_LENGTH + 1];
} FileMsg;

typedef struct DisconnectMsg {
    char client_name[MAX_NAME_LENGTH + 1];
} DisconnectMsg;

typedef union MsgUnion {
    HelloMsg hello;
    TextMsg data;
    FileMsg file;
    DisconnectMsg disconnect;
} MsgUnion;

typedef struct Message {
    MsgType type;
    MsgUnion content;
} Message;

static int read_message(const int fd, Message *msg) {
    char *ptr = (char*) msg;
    ssize_t received = 0;
    ssize_t total = 0;
    while (total < sizeof(Message)) {
        received = recv(fd, ptr, sizeof(Message) - total, 0);
        if (received == -1) {
            return -1;
        }
        if (received == 0) {
            return 0;
        }
        total += received;
        ptr += received;
    }
    return 1;
}

#endif
