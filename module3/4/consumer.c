#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>

#include "shared.h"

static void consumer(const key_t key) {
    shm = shmget(key, SHM_SIZE, 0);
    if (shm == -1) {
        perror("get shared memory");
        exit(EXIT_FAILURE);
    }
    sem = semget(key, 1, 0);
    if (sem == -1) {
        perror("get semaphore");
        exit(EXIT_FAILURE);
    }
    void *shared = attach();
    if (shared == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    void *ptr = shared;
    Data *current = ptr;
    do {
        sleep(1);
        TRY_IO_OR_FAIL(lock(), "lock process");
        while (current->next_offset != 0 && current->count == 0) {
            ptr += current->next_offset;
            current = ptr;
        }
        if (current->count != 0) {
            int min = INT_MAX;
            int max = INT_MIN;
            for (int i = 0; i < current->count; i++) {
                if (min > current->numbers[i])
                    min = current->numbers[i];
                if (max < current->numbers[i])
                    max = current->numbers[i];
            }
            printf("Min: %d\t Max: %d\n", min, max);
            current->count = 0;
        }
        TRY_IO_OR_FAIL(unlock(), "unlock process");
    } while (current->next_offset != 0);

    TRY_IO_OR_FAIL(detach(shared), "unmap");
}

int main(const int argc, const char **argv) {
    FILE *pid_fd = fopen(KEY_FILE, "r");
    if (!pid_fd) {
        perror("producer not running");
        exit(EXIT_FAILURE);
    }
    TRY_IO_OR_FAIL(fclose(pid_fd), "close key fd");

    const key_t key = ftok(KEY_FILE, 1);
    consumer(key);

    return EXIT_SUCCESS;
}
