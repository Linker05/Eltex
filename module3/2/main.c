#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>

#include "shared.h"

typedef enum Mode {
    BROKER,
    LISTENER,
    PUBLISHER
} Mode;

static Mode mode = -1;
static char pid_file[256] = {};

extern void broker(key_t input);
extern void publisher(key_t broker_key, const char *topic);
extern void listener(key_t broker_key, int topics_count, const char **topics, key_t input_key);

static int is_flag(const char *arg) {
    return strncmp(arg, "-", 1) == 0;
}

static void remove_pid() {
    unlink(pid_file);
}

static void create_pid() {
    FILE *pid_fd = fopen(pid_file, "wx");
    if (!pid_fd) {
        if (errno == EEXIST) {
            printf("PID file already exists\n");
            exit(EXIT_SUCCESS);
        }
        perror("create pid");
        exit(EXIT_FAILURE);
    }
    atexit(remove_pid);
    TRY_IO_OR_FAIL(fprintf(pid_fd, "%d", getpid()), "write pid");
    TRY_IO_OR_FAIL(fclose(pid_fd), "close pid fd");
}

void run(const int topics_count, const char **topics) {
    switch (mode) {
        case PUBLISHER: {
            const key_t broker_key = ftok(BROKER_PID_FILE, 1);
            publisher(broker_key, topics[0]);
            break;
        }
        case LISTENER: {
            const key_t broker_key = ftok(BROKER_PID_FILE, 1);
            sprintf(pid_file, LISTENER_PID_FILE_FORMAT, getpid());
            create_pid();
            const key_t input_key = ftok(pid_file, 1);
            listener(broker_key, topics_count, topics, input_key);
            break;
        }
        case BROKER: {
            strcpy(pid_file, BROKER_PID_FILE);
            create_pid();
            const key_t broker_key = ftok(BROKER_PID_FILE, 1);
            broker(broker_key);
            break;
        }
        default: {
            CERROR("unknown mode");
            exit(EXIT_FAILURE);
        }
    }
}

int main(const int argc, const char **argv) {
    if (argc < 2) {
        printf("Usage: program -<b|p|s> [topics...]\n");
        exit(EXIT_SUCCESS);
    }
    if (!is_flag(argv[1])) {
        CERROR("first argument must be a flag b, p or s\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "-p") == 0) {
        // Publisher mode
        mode = PUBLISHER;
    } else if (strcmp(argv[1], "-s") == 0) {
        // Listener mode
        mode = LISTENER;
    } else if (strcmp(argv[1], "-b") == 0) {
        // Broker mode
        mode = BROKER;
    } else {
        // Unknown flag
        CERROR("invalid flag");
        exit(EXIT_FAILURE);
    }

    if (mode == BROKER && argc != 2 ||
        mode == LISTENER && argc < 3 ||
        mode == PUBLISHER && argc != 3) {
        CERROR("invalid args count for mode");
        exit(EXIT_FAILURE);
    }

    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '\0' || is_flag(argv[i])) {
            CERROR("invalid parameter");
            exit(EXIT_FAILURE);
        }
    }

    run(argc - 2, &argv[2]);

    return EXIT_SUCCESS;
}