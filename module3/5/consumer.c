#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>

#include "shared.h"

static void consumer() {
    shm = shm_open(SHM_NAME, O_RDWR, 0);
    if (shm == -1) {
        perror("connect shared memory");
        exit(EXIT_FAILURE);
    }
    sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("connect semaphore");
        exit(EXIT_FAILURE);
    }
    attached = sem_open(SEM_ATTACHED_NAME, 0);
    if (attached == SEM_FAILED) {
        perror("connect attached counter");
        exit(EXIT_FAILURE);
    }
    void *shared = attach();
    if (shared == MAP_FAILED) {
        perror("mmap");
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
    TRY_IO_OR_FAIL(sem_close(sem), "sem close");
}

int main(const int argc, const char **argv) {
    consumer();

    return EXIT_SUCCESS;
}
