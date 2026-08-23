#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <libgen.h>

#include "shared.h"

int sockfd = -1;
int stop_requested = 0;
const char *client_name = NULL;

static void print_prompt() {
    printf("\nMessage: ");
    fflush(stdout);
}

static void save_file(const FileMsg *msg) {
    // Download file to working directory
    FILE *file = fopen(msg->filename, "w");
    if (!file) {
        printf("\nCannot accept file");
        return;
    }
    if (fwrite(msg->data, sizeof(char), msg->filesize, file) != msg->filesize) {
        printf("\nError during file write");
        fclose(file);
        return;
    }
    fclose(file);
}

static void read_messages() {
    Message msg = {};

    const int err = read_message(sockfd, &msg);
    if (err == 0) {
        stop_requested = 1;
        return;
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
            printf("\nClient %s connected", msg.content.hello.client_name);
            break;
        }
        case TEXT: {
            printf("\n[%s]: %s", msg.content.data.client_name, msg.content.data.text);
            break;
        }
        case SEND_FILE: {
            printf("\nReceived file %s from %s", msg.content.file.filename, msg.content.file.client_name);
            save_file(&msg.content.file);
            break;
        }
        case DISCONNECT: {
            printf("\nClient %s disconnected", msg.content.disconnect.client_name);
            break;
        }
        case STOP: {
            printf("\nServer stopped");
            stop_requested = 1;
            break;
        }
        default: {
            CERROR("invalid message type");
            exit(EXIT_FAILURE);
        }
    }
}

static void process_slash(char *command, Message *msg) {
    const char *cmd = &command[1];
    char *args = NULL;
    for (int i = 1; i < strlen(command); i++) {
        if (command[i] == ' ') {
            command[i] = '\0';
            args = &command[i+1];
            break;
        }
    }
    if (strcmp(cmd, "file") == 0) {
        // Send file
        FILE *file = fopen(args, "r");
        if (!file) {
            printf("\nCannot open file");
            return;
        }
        fseek(file, 0, SEEK_END);
        const long int size = ftell(file);
        if (size > MAX_FILE_LENGTH) {
            printf("\nFile too large");
            return;
        }
        fseek(file, 0, SEEK_SET);
        msg->type = SEND_FILE;
        msg->content.file.filesize = size;
        const char *base = basename(args);
        strcpy(msg->content.file.client_name, client_name);
        strcpy(msg->content.file.filename, base);
        if (fread(msg->content.file.data, sizeof(char), MAX_FILE_LENGTH, file) != size) {
            printf("\nError during file read");
            fclose(file);
            return;
        }

        TRY_IO_OR_FAIL(send(sockfd, msg, sizeof(Message), 0), "send file");
        return;
    }
    printf("\nUnknown slash command");
}

static void send_message() {
    Message msg = {};
    msg.type = TEXT;
    strcpy(msg.content.data.client_name, client_name);

    char text[MAX_TEXT_LENGTH + 1];

    if (!fgets(text, MAX_TEXT_LENGTH, stdin)) {
        CERROR("input error");
        exit(EXIT_FAILURE);
    }
    const size_t len = strlen(text);
    if (text[len - 1] != '\0') {
        text[len - 1] = '\0';
    }

    if (text[0] == '\0') {
        return;
    }

    if (strncmp(text, "/", 1) == 0) {
        // Process slash command
        process_slash(text, &msg);
    } else {
        // Send simple message
        strcpy(msg.content.data.text, text);
        TRY_IO_OR_FAIL(send(sockfd, &msg, sizeof(Message), 0), "send message");
    }
}

static void start_session() {
    Message msg = {};
    msg.type = HELLO;
    strcpy(msg.content.hello.client_name, client_name);

    TRY_IO_OR_FAIL(send(sockfd, &msg, sizeof(Message), 0), "send hello");
}

static void close_session() {
    Message msg = {};
    msg.type = DISCONNECT;
    strcpy(msg.content.disconnect.client_name, client_name);

    TRY_IO_OR_FAIL(send(sockfd, &msg, sizeof(Message), 0), "send disconnect");
}

static void client() {
    fd_set inputs;
    const int maxfd = STDIN_FILENO > sockfd ? STDIN_FILENO : sockfd;
    FD_ZERO(&inputs);
    FD_SET(sockfd, &inputs);
    FD_SET(STDIN_FILENO, &inputs);
    start_session();
    while (1) {
        fd_set polled = inputs;
        errno = 0;
        print_prompt();
        const int result = select(maxfd + 1, &polled, NULL, NULL, NULL);
        if (result == -1) {
            if (errno == EINTR && stop_requested) {
                break;
            }
            perror("select");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(STDIN_FILENO, &polled)) {
            // User input
            send_message();
        }
        if (FD_ISSET(sockfd, &polled)) {
            // Messages from network
            read_messages();
        }

        if (stop_requested) {
            break;
        }
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
    if (argc != 3) {
        CERROR("Usage: program <username> <server>");
        exit(EXIT_SUCCESS);
    }
    client_name = argv[1];
    struct in_addr server_address = {};
    if (inet_pton(AF_INET, argv[2], &server_address) <= 0) {
        CERROR("Invalid server ip");
        exit(EXIT_FAILURE);
    }

    const struct sockaddr_in server = {
        .sin_addr = server_address.s_addr,
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_zero = {}
    };

    TRY_IO_OR_FAIL(sockfd = socket(AF_INET, SOCK_STREAM, 0), "create socket");
    TRY_IO_OR_FAIL(connect(sockfd, (struct sockaddr*)&server, sizeof(struct sockaddr_in)), "connect");

    signal(SIGINT, int_handle);
    atexit(cleanup);

    client();

    return EXIT_SUCCESS;
}
