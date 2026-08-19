#ifndef CROSS2_SHARED_H
#define CROSS2_SHARED_H

#include <stdio.h>
#include <unistd.h>
#include <mqueue.h>
#include <stdlib.h>

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
// For fork() children - exit with _exit()
#define TRY_IO_OR_FAIL_NO_CLEANUP(operation, failure_msg) TRY_IO_OR_FAIL_IMPL(operation, failure_msg, _exit)

#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

#define MAX_CMD_LENGTH 1023

#define CLI_QUEUE_NAME "/cli"
#define MAX_DRIVERS 1024

#define CONTROL_FLOW_PRIORITY 1
#define EXIT_PRIORITY 10

#define AVAILABLE_STATUS "Available"
#define BUSY_STATUS "Busy"

typedef struct Status {
    char text[12];
    long timer;
} Status;

typedef enum CommandType {
    SEND_TASK,
    GET_STATUS,
    STOP
} CommandType;

typedef struct DriverCommand {
    CommandType type;
    long argument;
} DriverCommand;

typedef enum ReplyType {
    REPLY_SUCCESS,
    REPLY_ERROR
} ReplyType;

typedef struct DriverReply {
    int pid;
    ReplyType type;
    Status status;
} DriverReply;

extern mqd_t cli_queue;
extern int stop_requested;

#endif //CROSS2_SHARED_H
