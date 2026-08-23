#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <mqueue.h>

#include "shared.h"
#include "cli.h"

void handle_int(int code) {
    stop_requested = 1;
}

int main(const int argc, const char **argv) {
    signal(SIGINT, handle_int);

    const struct mq_attr attrs = (struct mq_attr) {
        .mq_curmsgs = 0,
        .mq_flags = 0,
        .mq_msgsize = sizeof(DriverReply),
        .mq_maxmsg = 10
    };
    TRY_IO_OR_FAIL(cli_queue = mq_open(CLI_QUEUE_NAME, O_RDWR | O_CREAT | O_EXCL, 0644, &attrs), "create cli queue");
    cli();
    TRY_IO_OR_FAIL(mq_close(cli_queue), "close cli queue");

    return EXIT_SUCCESS;
}
