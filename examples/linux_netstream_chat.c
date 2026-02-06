#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define DEFAULT_PORT "9000"
#define REGISTER_TOKEN "REGISTER"

static struct termios orig_termios;
static int termios_active = 0;

static void restore_terminal(void)
{
    if (termios_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        termios_active = 0;
    }
}

static void on_signal(int sig)
{
    (void)sig;
    restore_terminal();
    _exit(1);
}

static int enable_raw_terminal(void)
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) {
        return -1;
    }

    raw = orig_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return -1;
    }

    termios_active = 1;
    atexit(restore_terminal);
    return 0;
}

static void usage(const char* prog)
{
    fprintf(stderr, "Usage: %s [--port <port>]\n", prog);
}

static int is_register_packet(const uint8_t* buf, size_t len)
{
    size_t tok_len = strlen(REGISTER_TOKEN);
    if (len != tok_len) {
        return 0;
    }
    return memcmp(buf, REGISTER_TOKEN, tok_len) == 0;
}

int main(int argc, char** argv)
{
    const char* port = DEFAULT_PORT;
    struct addrinfo hints;
    struct addrinfo* res = NULL;
    struct sockaddr_storage peer_addr;
    socklen_t peer_len = 0;
    int sockfd = -1;
    int rc = 0;
    int connected = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = argv[++i];
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
    if (enable_raw_terminal() != 0) {
        perror("tcsetattr");
        close(sockfd);
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGQUIT, on_signal);

    while (1) {
        fd_set rfds;
        int maxfd = sockfd;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);
        if (connected) {
            FD_SET(STDIN_FILENO, &rfds);
            if (STDIN_FILENO > maxfd) {
                maxfd = STDIN_FILENO;
            }
        }

        tv.tv_sec = 0;
        tv.tv_usec = 20000;

        rc = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        if (FD_ISSET(sockfd, &rfds)) {
            uint8_t buf[512];
            struct sockaddr_storage from;
            socklen_t from_len = sizeof(from);
            ssize_t got = recvfrom(sockfd, buf, sizeof(buf), 0,
                                   (struct sockaddr*)&from, &from_len);
            if (got > 0) {
                peer_addr = from;
                peer_len = from_len;

                if (!connected && is_register_packet(buf, (size_t)got)) {
                    connected = 1;
                    fputs("Client Connected\r\n", stdout);
                    fflush(stdout);
                    continue;
                }

                for (ssize_t i = 0; i < got; ++i) {
                    uint8_t ch = buf[i];
                    if (ch == 0x9B) {
                        fputs("\r\n", stdout);
                        continue;
                    }
                    if (ch == '\n') {
                        fputs("\r\n", stdout);
                        continue;
                    }
                    fputc((int)ch, stdout);
                }
                fflush(stdout);
            }
        }

        if (connected && FD_ISSET(STDIN_FILENO, &rfds)) {
            uint8_t ch = 0;
            ssize_t got = read(STDIN_FILENO, &ch, 1);
            if (got < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("read");
                break;
            }
            if (got == 0) {
                continue;
            }

            if (ch == '\n' || ch == '\r') {
                ch = 0x9B;
            }

            if (peer_len > 0) {
                if (sendto(sockfd, &ch, 1, 0,
                           (const struct sockaddr*)&peer_addr, peer_len) < 0) {
                    perror("sendto");
                }
            }
        }
    }

    restore_terminal();
    close(sockfd);
    return 0;
}
