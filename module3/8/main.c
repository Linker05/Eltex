#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <linux/ip.h>
#include <linux/udp.h>

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

#define FILTER_MSG 1
#define FILTER_DNS 2
#define FILTER_ALL (FILTER_DNS | FILTER_MSG)

#define MAX_UDP_MTU 655535
#define MSG_PORT 50000
#define DNS_PORT 53

#define MIN_SQUASH 5
#define LINE_LENGTH 16

typedef struct Packet {
    struct iphdr *ip;
    struct udphdr *udp;
    uint8_t *content;
} Packet;

int sockfd = -1;
int stop_requested = 0;
int filter = 0;

// Print hexdecimal data squashing more than MIN_SQUASH similar bytes
static void print_squashed(const unsigned char squashed_char, const int squashed) {
    if (squashed > MIN_SQUASH) {
        printf("%02x(%d times)", squashed_char, squashed);
    } else {
        for (int i = 0; i < squashed; i++)
            printf("%02x ", squashed_char);
    }
}

// Print ASCII data squashing more than MIN_SQUASH similar characters (unprintable data = .)
static void print_squashed_char(unsigned char squashed_char, const int squashed) {
    if (squashed <= 0)
        return;
    if (squashed_char < 32 || squashed_char >= 127) {
        squashed_char = '.';
    }

    if (squashed > MIN_SQUASH) {
        printf("%c(%d times)", squashed_char, squashed);
    } else {
        for (int i = 0; i < squashed; i++)
            printf("%c", squashed_char);
    }
}

static void print_message(const Packet *pkt) {
    const struct iphdr *ip = pkt->ip;
    const struct udphdr *udp = pkt->udp;
    const uint8_t *content = pkt->content;
    char saddr[INET_ADDRSTRLEN];
    char daddr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip->saddr, saddr, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &ip->daddr, daddr, INET_ADDRSTRLEN);
    printf(""
        "Src: %s:%d\n"
        "Dst: %s:%d\n"
        "Len: %d\n"
        "Content (HEX):\n", saddr, ntohs(udp->source), daddr, ntohs(udp->dest), ntohs(udp->len));
    const int data_length = ntohs(udp->len) - 8;
    int squashed = 0;
    int squashed_char = -1;
    int line = 0;

    for (int i = 0; i < data_length; i++) {
        if (content[i] != squashed_char) {
            print_squashed((char)squashed_char, squashed);
            line += squashed;
            if (line > LINE_LENGTH) {
                printf("\n");
                line = 0;
            }
            squashed = 0;
            squashed_char = (unsigned char) content[i];
        }
        squashed++;
    }
    print_squashed((char) squashed_char, squashed);

    squashed = 0;
    squashed_char = -1;
    printf("\nContent (ASCII):\n");
    for (int i = 0; i < data_length; i++) {
        if (content[i] != squashed_char) {
            print_squashed_char((char)squashed_char, squashed);
            line += squashed;
            squashed = 0;
            squashed_char = content[i];
        }
        squashed++;
    }
    print_squashed_char((char) squashed_char, squashed);
    printf("\n\n");
}

static void int_handle(int code) {
    stop_requested = 1;
}

static void cleanup() {
    close(sockfd);
}

static void sniffer() {
    struct timespec start;
    timespec_get(&start, TIME_UTC);
    struct timespec current;
    char buffer[MAX_UDP_MTU];
    Packet packet = {};
    int i = 0;
    while (1) {
        const ssize_t result = recvfrom(sockfd, buffer, MAX_UDP_MTU, MSG_DONTWAIT, NULL, NULL);
        if (stop_requested) {
            break;
        }
        if (result == 0) {
            break;
        }
        if (result == -1) {
            if (errno == EAGAIN) {
                sleep(1);
                continue;
            }
            perror("recvfrom");
            exit(EXIT_FAILURE);
        }
        i++;
        timespec_get(&current, TIME_UTC);
        packet.ip = (struct iphdr*) buffer;
        packet.udp = (struct udphdr*) ((char*) packet.ip + packet.ip->ihl * 4);
        packet.content = (uint8_t*) packet.udp + 8;

        char proto[8] = "";

        if ((filter & FILTER_MSG) && ((ntohs(packet.udp->source) == MSG_PORT) || (ntohs(packet.udp->dest) == MSG_PORT))) {
            strcpy(proto, "MESSAGE");
        } else if ((filter & FILTER_DNS) && ((ntohs(packet.udp->source) == DNS_PORT) || (ntohs(packet.udp->dest) == DNS_PORT))) {
            strcpy(proto, "DNS");
        } else if (filter == FILTER_ALL) {
            strcpy(proto, "OTHER");
        }

        if (proto[0] == '\0')
            continue;

        const double elapsed = (double)(current.tv_sec - start.tv_sec) + ((double)(current.tv_nsec - start.tv_nsec) / 1000000000.0);

        printf("=== Packet #%d [ %s ] - time: %lf ===\n", i, proto, elapsed);
        print_message(&packet);
    }
}

int main(const int argc, const char **argv) {
    if (argc != 2) {
        CERROR("Usage: program <msg|dns|all>");
        exit(EXIT_SUCCESS);
    }
    const char *filter_name = argv[1];
    if (strcmp(filter_name, "msg") == 0) {
        filter = FILTER_MSG;
    } else if (strcmp(filter_name, "dns") == 0) {
        filter = FILTER_DNS;
    } else if (strcmp(filter_name, "all") == 0) {
        filter = FILTER_ALL;
    } else {
        CERROR("invalid filter");
        exit(EXIT_FAILURE);
    }
    TRY_IO_OR_FAIL(sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP), "create socket");

    signal(SIGINT, int_handle);
    atexit(cleanup);

    sniffer();

    return EXIT_SUCCESS;
}
