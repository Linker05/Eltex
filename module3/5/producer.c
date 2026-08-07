#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <fcntl.h>
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

static void producer() {
    shm = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0644);
    if (shm == -1) {
        perror("create shared memory");
        exit(EXIT_FAILURE);
    }
    ftruncate(shm, SHM_SIZE);
    sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    if (sem == SEM_FAILED) {
        perror("create semaphore");
        exit(EXIT_FAILURE);
    }
    attached = sem_open(SEM_ATTACHED_NAME, O_CREAT | O_EXCL, 0644, 0);
    if (attached == SEM_FAILED) {
        perror("create attached counter");
        exit(EXIT_FAILURE);
    }
    void *shared = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    printf("Starting of data generation...\n");
    generate_data(shared);
    printf("Data generated. Waiting for consumers...\n");
    wait_processing(shared);
    printf("Data consumed. Waiting for detaching memory...\n");
    wait_for_mem();
    TRY_IO_OR_FAIL(munmap(shared, SHM_SIZE), "unmap");
    TRY_IO_OR_FAIL(sem_close(sem), "sem close");
}

void cleanup() {
    sem_unlink(SEM_NAME);
    sem_unlink(SEM_ATTACHED_NAME);
    shm_unlink(SHM_NAME);
}

void int_handle(int code) {
    cleanup();
}

int main(const int argc, const char **argv) {
    atexit(cleanup);
    signal(SIGINT, int_handle);

    srand(time(NULL));

    producer();

    return EXIT_SUCCESS;
}
