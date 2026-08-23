#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>

#include "shared.h"

static void generate_data(void *shared) {
    const char *end = (char*)shared + SHM_SIZE;
    char *ptr = shared;
    Data *last = NULL;

    TRY_IO_OR_FAIL(lock(), "lock generate");
    while (ptr < end) {
        const long remain = end - ptr;
        int count = 0;
        const int for_data = (int)(remain - sizeof(Data));
        if (for_data > 0)
            count = (int)(rand() % (for_data / sizeof(int)) + 1);
        if (count == 0) {
            if (for_data >= (int) sizeof(int))
                continue;
            if (!last)
                return;
            last->next_offset = 0;
            break;
        }
        const int size = (int)(sizeof(Data) + count * sizeof(int));
        Data *data = malloc(size);
        data->count = count;
        data->next_offset = size;
        for (int i = 0; i < data->count; i++) {
            data->numbers[i] = rand();
        }
        memcpy(ptr, data, size);
        free(data);
        last = (Data*)ptr;
        ptr += size;
    }
    if (last)
        last->next_offset = 0;
    TRY_IO_OR_FAIL(unlock(), "unlock generate");
}

static void wait_processing(const void *shared) {
    int end = 0;
    while (!end) {
        sleep(1);
        TRY_IO_OR_FAIL(lock(), "lock wait");
        const void *ptr = shared;
        const Data *current = ptr;
        end = 1;
        while (current->next_offset != 0) {
            if (current->count != 0) {
                end = 0;
                break;
            }
            ptr += current->next_offset;
            current = ptr;
        }
        TRY_IO_OR_FAIL(unlock(), "unlock wait");
    }
}

static void producer(const key_t key) {
    shm = shmget(key, SHM_SIZE, IPC_CREAT | IPC_EXCL | 0644);
    if (shm == -1) {
        perror("create shared memory");
        exit(EXIT_FAILURE);
    }
    sem = semget(key, 2, IPC_CREAT | IPC_EXCL | 0644);
    if (sem == -1) {
        perror("create semaphore");
        exit(EXIT_FAILURE);
    }
    void *shared = shmat(shm, NULL, 0);
    if (shared == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    printf("Starting of data generation...\n");
    generate_data(shared);
    printf("Data generated. Waiting for consumers...\n");
    wait_processing(shared);
    printf("Data consumed. Waiting for detaching memory...\n");
    wait_for_mem();
    TRY_IO_OR_FAIL(shmdt(shared), "unmap");
}

void cleanup() {
    shmctl(shm, IPC_RMID, NULL);
    semctl(sem, 0, IPC_RMID);
    unlink(KEY_FILE);
}

void int_handle(int code) {
    cleanup();
}

int main(const int argc, const char **argv) {
    FILE *pid_fd = fopen(KEY_FILE, "wx");
    if (!pid_fd) {
        if (errno == EEXIST) {
            printf("Key file already exists\n");
            exit(EXIT_SUCCESS);
        }
        perror("create key");
        exit(EXIT_FAILURE);
    }
    atexit(cleanup);
    signal(SIGINT, int_handle);
    TRY_IO_OR_FAIL(fclose(pid_fd), "close key fd");

    srand(time(NULL));

    const key_t key = ftok(KEY_FILE, 1);
    producer(key);

    return EXIT_SUCCESS;
}
