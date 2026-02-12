#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT "9000"
#define REGISTER_TOKEN "REGISTER"

#define SCREEN_COLS 40
#define SCREEN_ROWS 24
#define SCREEN_BYTES (SCREEN_COLS * SCREEN_ROWS)
#define PACKET_DATA 8
#define PACKET_COUNT (SCREEN_BYTES / PACKET_DATA)
#define PACKET_SIZE (2 + PACKET_DATA)
#define RETURN_WINDOW 256
#define RETURN_MAX_PAYLOAD 255

static void usage(const char* prog)
{
    fprintf(stderr, "Usage: %s [--port <port>] [--duplicates] [--reorder]\n", prog);
}

static int is_register_packet(const uint8_t* buf, size_t len)
{
    size_t tok_len = strlen(REGISTER_TOKEN);
    if (len != tok_len) {
        return 0;
    }
    return memcmp(buf, REGISTER_TOKEN, tok_len) == 0;
}

static void build_lorem(uint8_t* dst, size_t len)
{
    const char* seed =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
        "incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis "
        "nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. "
        "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu "
        "fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum. Sed ut perspiciatis unde "
        "omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam "
        "rem aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto "
        "beatae vitae dicta sunt explicabo. Nemo enim ipsam voluptatem quia voluptas sit "
        "aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos qui ratione "
        "voluptatem sequi nesciunt. Neque porro quisquam est, qui dolorem ipsum quia dolor "
        "sit amet, consectetur, adipisci velit, sed quia non numquam eius modi tempora "
        "incidunt ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima "
        "veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut "
        "aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit qui in ea "
        "voluptate velit esse quam nihil molestiae consequatur, vel illum qui dolorem eum "
        "fugiat quo voluptas nulla pariatur?";
    size_t seed_len = strlen(seed);
    size_t start = (size_t)(rand() % seed_len);
    size_t i;

    while (start < seed_len && seed[start] != ' ') {
        start++;
    }
    while (start < seed_len && seed[start] == ' ') {
        start++;
    }
    if (start >= seed_len) {
        start = 0;
    }

    for (i = 0; i < len; ++i) {
        dst[i] = (uint8_t)seed[(start + i) % seed_len];
    }
}

static uint8_t ascii_to_atascii(uint8_t ch)
{
    ch &= 0x7F;
    if (ch >= 'a' && ch <= 'z') {
        ch = (uint8_t)(ch - 0x20);
    }
    if (ch < 0x20 || ch >= 0x7F) {
        ch = (uint8_t)' ';
    }
    return ch;
}


int main(int argc, char** argv)
{
    const char* port = DEFAULT_PORT;
    int enable_duplicates = 0;
    int enable_reorder = 0;
    struct addrinfo hints;
    struct addrinfo* res = NULL;
    struct sockaddr_storage peer_addr;
    socklen_t peer_len = 0;
    int sockfd = -1;
    int rc;
    uint8_t lorem[SCREEN_BYTES];

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = argv[++i];
        } else if (strcmp(argv[i], "--duplicates") == 0) {
            enable_duplicates = 1;
        } else if (strcmp(argv[i], "--reorder") == 0) {
            enable_reorder = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    rc = getaddrinfo(NULL, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return 1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
        freeaddrinfo(res);
        close(sockfd);
        return 1;
    }
    freeaddrinfo(res);

    fprintf(stderr, "Listening on UDP port %s (waiting for REGISTER)\n", port);

    srand((unsigned int)time(NULL));

    int connected = 0;
    uint16_t seq_base = 0;
    unsigned long total_runs = 0;
    unsigned long total_verified = 0;
    unsigned long total_mismatch = 0;
    unsigned long total_timeouts = 0;
    unsigned long total_duplicate_sent = 0;

    for (;;) {
        while (!connected) {
            uint8_t buf[256];
            struct sockaddr_storage from;
            socklen_t from_len = sizeof(from);
            ssize_t got = recvfrom(sockfd, buf, sizeof(buf), 0,
                                   (struct sockaddr*)&from, &from_len);
            if (got < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("recvfrom");
                close(sockfd);
                return 1;
            }
            if (got == 0) {
                continue;
            }
            if (is_register_packet(buf, (size_t)got)) {
                peer_addr = from;
                peer_len = from_len;
                fprintf(stderr, "Client Connected\n");
                connected = 1;
                {
                    struct timespec ts;
                    ts.tv_sec = 0;
                    ts.tv_nsec = 20000000L;
                    nanosleep(&ts, NULL);
                }
                break;
            }
        }

        unsigned int dup_sent = 0;
        build_lorem(lorem, sizeof(lorem));
        for (size_t i = 0; i < sizeof(lorem); ++i) {
            lorem[i] = ascii_to_atascii(lorem[i]);
        }

        fprintf(stderr, "Sending %u packets%s%s...\n",
                (unsigned)PACKET_COUNT,
                enable_duplicates ? " (dups enabled)" : "",
                enable_reorder ? " (reorder enabled)" : "");
        {
            uint16_t swap_index = (uint16_t)(rand() % (PACKET_COUNT - 1));
            if (enable_reorder) {
                fprintf(stderr, "Out-of-order swap: %u then %u\n",
                        (unsigned)(seq_base + swap_index + 1),
                        (unsigned)(seq_base + swap_index));
            }
            for (uint16_t i = 0; i < PACKET_COUNT; ++i) {
                uint16_t seq = (uint16_t)(seq_base + i);
                uint8_t packet[PACKET_SIZE];
                packet[0] = (uint8_t)((seq >> 8) & 0xFF);
                packet[1] = (uint8_t)(seq & 0xFF);
                memcpy(packet + 2, lorem + (i * PACKET_DATA), PACKET_DATA);

                if (enable_reorder && i == swap_index) {
                    uint16_t seq2 = (uint16_t)(seq_base + i + 1);
                    uint8_t packet2[PACKET_SIZE];
                    packet2[0] = (uint8_t)((seq2 >> 8) & 0xFF);
                    packet2[1] = (uint8_t)(seq2 & 0xFF);
                    memcpy(packet2 + 2, lorem + ((i + 1) * PACKET_DATA), PACKET_DATA);

                    if (sendto(sockfd, packet2, sizeof(packet2), 0,
                               (const struct sockaddr*)&peer_addr, peer_len) < 0) {
                        perror("sendto");
                    }
                    {
                        struct timespec ts;
                        ts.tv_sec = 0;
                        ts.tv_nsec = 50000000L;
                        nanosleep(&ts, NULL);
                    }

                    if (sendto(sockfd, packet, sizeof(packet), 0,
                               (const struct sockaddr*)&peer_addr, peer_len) < 0) {
                        perror("sendto");
                    }
                    {
                        struct timespec ts;
                        ts.tv_sec = 0;
                        ts.tv_nsec = 50000000L;
                        nanosleep(&ts, NULL);
                    }
                    ++i;
                    continue;
                }

                if (sendto(sockfd, packet, sizeof(packet), 0,
                           (const struct sockaddr*)&peer_addr, peer_len) < 0) {
                    perror("sendto");
                }
                if (enable_duplicates && (rand() & 1) != 0) {
                    if (sendto(sockfd, packet, sizeof(packet), 0,
                               (const struct sockaddr*)&peer_addr, peer_len) < 0) {
                        perror("sendto");
                    }
                    ++dup_sent;
                }
                {
                    struct timespec ts;
                    ts.tv_sec = 0;
                    ts.tv_nsec = 50000000L;
                    nanosleep(&ts, NULL);
                }
            }
        }

        fprintf(stderr, "Sent %u packets (%u duplicates). Waiting for return data...\n",
                (unsigned)PACKET_COUNT, dup_sent);
        total_duplicate_sent += dup_sent;
        ++total_runs;
        seq_base = (uint16_t)(seq_base + PACKET_COUNT);

        uint8_t received[SCREEN_BYTES];
        unsigned int got_bytes = 0;
        uint16_t base_seq = 0;
        uint16_t next_seq = 0;
        int base_set = 0;
        uint8_t slot_present[RETURN_WINDOW];
        uint16_t slot_seq[RETURN_WINDOW];
        uint8_t slot_len[RETURN_WINDOW];
        uint8_t slot_data[RETURN_WINDOW][RETURN_MAX_PAYLOAD];
        time_t start = time(NULL);
        int timed_out = 0;

        memset(received, 0, sizeof(received));
        memset(slot_present, 0, sizeof(slot_present));

        while (got_bytes < SCREEN_BYTES) {
            fd_set rfds;
            struct timeval tv;
            int sel;

            if (time(NULL) - start > 40) {
                fprintf(stderr, "Timeout waiting for return data (%u/%u bytes).\n",
                        got_bytes, (unsigned)SCREEN_BYTES);
                fprintf(stderr, "Timeout bytes received: %u\n", got_bytes);
                ++total_timeouts;
                timed_out = 1;
                break;
            }

            FD_ZERO(&rfds);
            FD_SET(sockfd, &rfds);
            tv.tv_sec = 0;
            tv.tv_usec = 200000;

            sel = select(sockfd + 1, &rfds, NULL, NULL, &tv);
            if (sel < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("select");
                close(sockfd);
                return 1;
            }
            if (sel == 0) {
                continue;
            }

            if (FD_ISSET(sockfd, &rfds)) {
                uint8_t buf[256];
                struct sockaddr_storage from;
                socklen_t from_len = sizeof(from);
                ssize_t got = recvfrom(sockfd, buf, sizeof(buf), 0,
                                       (struct sockaddr*)&from, &from_len);
                if (got < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    perror("recvfrom");
                    close(sockfd);
                    return 1;
                }
                if (got < 2) {
                    if (is_register_packet(buf, (size_t)got)) {
                        peer_addr = from;
                        peer_len = from_len;
                        connected = 1;
                        fprintf(stderr, "Client Connected\n");
                    }
                    continue;
                }
                if (is_register_packet(buf, (size_t)got)) {
                    peer_addr = from;
                    peer_len = from_len;
                    connected = 1;
                    fprintf(stderr, "Client Connected\n");
                    continue;
                }

                {
                    uint16_t seq = (uint16_t)((buf[0] << 8) | buf[1]);
                    size_t payload_len = (size_t)got - 2;
                    uint8_t idx;

                    if (payload_len == 0) {
                        continue;
                    }
                    if (payload_len > RETURN_MAX_PAYLOAD) {
                        payload_len = RETURN_MAX_PAYLOAD;
                    }

                    if (!base_set) {
                        base_seq = seq;
                        next_seq = seq;
                        base_set = 1;
                    } else if (got_bytes == 0 && seq < base_seq) {
                        base_seq = seq;
                        next_seq = seq;
                    }

                    idx = (uint8_t)(seq & 0xFF);
                    if (slot_present[idx] && slot_seq[idx] == seq) {
                        continue;
                    }
                    slot_present[idx] = 1;
                    slot_seq[idx] = seq;
                    slot_len[idx] = (uint8_t)payload_len;
                    memcpy(slot_data[idx], buf + 2, payload_len);

                    while (slot_present[(uint8_t)(next_seq & 0xFF)] &&
                           slot_seq[(uint8_t)(next_seq & 0xFF)] == next_seq) {
                        uint8_t sidx = (uint8_t)(next_seq & 0xFF);
                        size_t remaining = SCREEN_BYTES - got_bytes;
                        size_t take = slot_len[sidx];
                        if (take > remaining) {
                            take = remaining;
                        }
                        memcpy(received + got_bytes, slot_data[sidx], take);
                        got_bytes += (unsigned int)take;
                        slot_present[sidx] = 0;
                        next_seq = (uint16_t)(next_seq + 1);
                        if (got_bytes >= SCREEN_BYTES) {
                            break;
                        }
                    }
                }
            }
        }

        if (got_bytes < SCREEN_BYTES) {
            fprintf(stderr, "Return data incomplete (%u/%u bytes). Waiting for next client...\n",
                    got_bytes, (unsigned)SCREEN_BYTES);
            if (timed_out) {
                fprintf(stderr, "Return data mismatch (incomplete at timeout).\n");
                ++total_mismatch;
                fprintf(stderr,
                        "Stats: runs=%lu verified=%lu mismatches=%lu timeouts=%lu dup_sent=%lu\n",
                        total_runs, total_verified, total_mismatch, total_timeouts,
                        total_duplicate_sent);
            }
            continue;
        }

        if (memcmp(received, lorem, sizeof(lorem)) == 0) {
            fprintf(stderr, "Return data verified (%u bytes).\n", (unsigned)sizeof(lorem));
            ++total_verified;
        } else {
            fprintf(stderr, "Return data mismatch.\n");
            ++total_mismatch;
        }

        fprintf(stderr,
                "Stats: runs=%lu verified=%lu mismatches=%lu timeouts=%lu dup_sent=%lu\n",
                total_runs, total_verified, total_mismatch, total_timeouts,
                total_duplicate_sent);
    }
}
