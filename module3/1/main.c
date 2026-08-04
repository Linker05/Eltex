#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/wait.h>

#define BUFFER_SIZE 1024
#define MARKER_MAX_SIZE 9
#define FILENAME_MAX_SIZE 512

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
// For fork() children - exit with _exit()
#define TRY_IO_OR_FAIL_NO_CLEANUP(operation, failure_msg) TRY_IO_OR_FAIL_IMPL(operation, failure_msg, _exit)

static void writer(const int rd, const int wd) {
    DEBUG_ONLY (
        FILE *log = fopen("writer.log", "w"); 
        setvbuf(log, NULL, _IONBF, 0);
    )

    char buffer[BUFFER_SIZE] = {};
    DEBUG_LOG(log, "handshake write\n"); 
    TRY_IO_OR_FAIL_NO_CLEANUP(write(wd, "ready", 6), "handshake write");

    while (1) {
        DEBUG_LOG(log, "marker wait\n"); 
        char marker[MARKER_MAX_SIZE] = {};
        TRY_IO_OR_FAIL_NO_CLEANUP(read(rd, marker, 9), "read marker");
        if (strcmp(marker, "end") == 0) {
            _exit(EXIT_SUCCESS);
        }

        size_t name_length = 0;
        char filename[FILENAME_MAX_SIZE] = {};
        __off_t file_size = 0;
        DEBUG_LOG(log, "name length wait\n"); 
        TRY_IO_OR_FAIL_NO_CLEANUP(read(rd, &name_length, sizeof(size_t)), "read file size");
        DEBUG_LOG(log, "name wait\n"); 

        TRY_IO_OR_FAIL_NO_CLEANUP(read(rd, filename, name_length), "read file name");
        DEBUG_LOG(log, "size wait\n");

        TRY_IO_OR_FAIL_NO_CLEANUP(read(rd, &file_size, sizeof(__off_t)), "read file size");
        DEBUG_LOG(log, "file size: %ld\n", file_size);

        char output_file[FILENAME_MAX_SIZE + 5] = {};
        strcpy(output_file, filename);
        strcat(output_file, ".copy");
        const int output = open(output_file, O_WRONLY | O_CREAT, 0644);
        if (output == -1) {
            perror(output_file);
            _exit(EXIT_FAILURE);
        }

        __off_t counter = 0;
        while (counter < file_size) {
            DEBUG_LOG(log, "data size wait\n"); 
            ssize_t data_size = 0;
            TRY_IO_OR_FAIL_NO_CLEANUP(read(rd, &data_size, sizeof(ssize_t)), filename);

            DEBUG_LOG(log, "data wait %ld\n", data_size); 
            TRY_IO_OR_FAIL_NO_CLEANUP(read(rd, buffer, data_size), filename);
            DEBUG_LOG(log, "read data %ld\n", data_size); 

            counter += data_size;
            TRY_IO_OR_FAIL_NO_CLEANUP(write(output, buffer, data_size), output_file);
        }
        close(output);
    }

    DEBUG_ONLY(
        fclose(log);
    )
}

static void reader(const int rd, const int wd, const char **files) {
    DEBUG_ONLY(
        FILE *log = fopen("reader.log", "w");
        setvbuf(log, NULL, _IONBF, 0);
    )

    char buffer[BUFFER_SIZE] = {};
    DEBUG_LOG(log, "wait handshake\n"); 
    TRY_IO_OR_FAIL(read(rd, buffer, 1024), "read handshake");
    if (strcmp(buffer, "ready") != 0) {
        perror("child fail");
        exit(EXIT_FAILURE);
    }

    const char **fptr = files;
    while (*fptr) {
        const int file = open(*fptr, O_RDONLY);
        if (file == -1) {
            perror(*fptr);
            exit(EXIT_FAILURE);
        }

        DEBUG_LOG(log, "write continue marker\n"); 
        TRY_IO_OR_FAIL(write(wd, "continue", MARKER_MAX_SIZE), "write continue marker");

        const size_t len = strlen(*fptr);
        DEBUG_LOG(log, "write name length\n"); 
        TRY_IO_OR_FAIL(write(wd, &len, sizeof(size_t)), "write name length");

        DEBUG_LOG(log, "write file name\n");
        TRY_IO_OR_FAIL(write(wd, *fptr, len), "write file name");

        const __off_t size = lseek(file, 0, SEEK_END);
        DEBUG_LOG(log, "write size\n");
        TRY_IO_OR_FAIL(write(wd, &size, sizeof(__off_t)), "write file size");

        lseek(file, 0, SEEK_SET);
        ssize_t result = 0;
        DEBUG_LOG(log, "read file\n");
        while ((result = read(file, buffer, BUFFER_SIZE))) {
            if (result == -1) {
                perror(*fptr);
                exit(EXIT_FAILURE);
            }

            DEBUG_LOG(log, "write data size: %ld\n", result);
            TRY_IO_OR_FAIL(write(wd, &result, sizeof(ssize_t)), "write data size");

            DEBUG_LOG(log, "write data\n");
            TRY_IO_OR_FAIL(write(wd, buffer, result), "write data");
        }
        close(file);
        fptr++;
    }

    DEBUG_LOG(log, "write end marker\n");
    TRY_IO_OR_FAIL(write(wd, "end", 4), "write end marker");

    DEBUG_ONLY (
        fclose(log);
    )
}

static int is_flag(const char *arg) {
    return strncmp(arg, "-", 1) == 0;
}

int main(const int argc, const char **argv) {
    const char *pipe_file = NULL;
    if (argc < 2) {
        printf("Usage: program [-p <pipe name>] <files to copy...>\n");
        exit(EXIT_SUCCESS);
    }
    
    const char **files = calloc(sizeof(char*), argc + 1);
    const char **fptr = files;
    for (int i = 1; i < argc; i++) {
        // Ignore empty args
        if (argv[i][0] == '\0')
            continue;
        // Files to copy
        if (!is_flag(argv[i])) {
            *fptr = argv[i];
            fptr++;
            continue;
        }
        if (strcmp(argv[i], "-p") == 0) {
            // -p flag to specify pipe name
            if (pipe_file) {
                perror("pipe name (-p) duplicate");
                exit(EXIT_FAILURE);
            }
            i++;
            if (is_flag(argv[i])) {
                perror("flag -p must have value");
                exit(EXIT_FAILURE);
            }
            pipe_file = argv[i];
        } else {
            // Unknown flah
            perror("invalid flag");
            exit(EXIT_FAILURE);
        }
    }

    int fd_to_reader[2];
    int fd_to_writer[2];

    if (pipe_file) {
        const size_t len = strlen(pipe_file);
        char *pipe_to_reader = calloc(sizeof(char), (len + 2) + 1);
        char *pipe_to_writer = calloc(sizeof(char), (len + 2) + 1);
        strcat(pipe_to_reader, pipe_file);
        strcat(pipe_to_writer, pipe_file);
        strcat(pipe_to_reader, "_r");
        strcat(pipe_to_writer, "_w");

        unlink(pipe_to_reader);
        unlink(pipe_to_writer);
        TRY_IO_OR_FAIL(mkfifo(pipe_to_reader, 0644), "mkfifo reader");
        TRY_IO_OR_FAIL(mkfifo(pipe_to_writer, 0644), "mkfifo writer");

        fd_to_reader[0] = open(pipe_to_reader, O_RDONLY | O_NONBLOCK);
        fd_to_reader[1] = open(pipe_to_reader, O_WRONLY);
        fd_to_writer[0] = open(pipe_to_writer, O_RDONLY | O_NONBLOCK);
        fd_to_writer[1] = open(pipe_to_writer, O_WRONLY);

        // Open as NONBLOCK to skip waiting for writer, but reset NONBLOCK to get blocking read() behaviour
        int flags = fcntl(fd_to_reader[0], F_GETFL, 0);
        fcntl(fd_to_reader[0], F_SETFL, flags & ~O_NONBLOCK);
        flags = fcntl(fd_to_writer[0], F_GETFL, 0);
        fcntl(fd_to_writer[0], F_SETFL, flags & ~O_NONBLOCK);

        free(pipe_to_writer);
        free(pipe_to_reader);
    } else {
        TRY_IO_OR_FAIL(pipe(fd_to_reader), "pipe reader");
        TRY_IO_OR_FAIL(pipe(fd_to_writer), "pipe writer");
    }

    const pid_t pid = fork();
    if (pid > 0) {
        close(fd_to_reader[1]);
        close(fd_to_writer[0]);

        reader(fd_to_reader[0], fd_to_writer[1], files);
        int status;
        wait(&status);
        printf("Child result %d\n", WEXITSTATUS(status));

        free(files);
        close(fd_to_reader[0]);
        close(fd_to_writer[1]);
    } else {
        free(files);
        close(fd_to_reader[0]);
        close(fd_to_writer[1]);

        writer(fd_to_writer[0], fd_to_reader[1]);

        close(fd_to_reader[1]);
        close(fd_to_writer[0]);
    }

    return EXIT_SUCCESS;
}
