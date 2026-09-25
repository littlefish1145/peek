#ifndef _WIN32

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

static struct termios saved_term;
static int term_ready;

double pk_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

void pk_sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, 0);
}

int pk_term_init(void) {
    if (term_ready) return 1;
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_term)) return 0;
    struct termios raw = saved_term;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw)) return 0;
    term_ready = 1;
    return 1;
}

void pk_term_shutdown(void) {
    if (!term_ready) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_term);
    term_ready = 0;
}

int pk_term_is_tty(void) {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

int pk_term_size(int *cols, int *rows) {
    struct winsize ws;
    int c = 0, r = 0;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        c = ws.ws_col;
        r = ws.ws_row;
    }
    if (c <= 0 && ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        c = ws.ws_col;
        r = ws.ws_row;
    }
    if (c <= 0 || r <= 0) return -1;
    *cols = c;
    *rows = r;
    return 0;
}

int pk_term_read(unsigned char *buf, int cap, int timeout_ms) {
    if (!buf || cap <= 0) return -1;
    if (timeout_ms >= 0) {
        fd_set rs;
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        FD_ZERO(&rs);
        FD_SET(STDIN_FILENO, &rs);
        int r = select(STDIN_FILENO + 1, &rs, 0, 0, &tv);
        if (r <= 0) return r ? -1 : 0;
    }
    ssize_t r = read(STDIN_FILENO, buf, (size_t)cap);
    return r < 0 ? -1 : (int)r;
}

int pk_socket_init(void) {
    return 0;
}

void pk_socket_cleanup(void) {
}

pk_socket_t pk_socket_create(int family, int type, int protocol) {
    return socket(family, type, protocol);
}

int pk_socket_connect(pk_socket_t fd, const void *addr, size_t addrlen) {
    int r = connect(fd, (const struct sockaddr *)addr, (socklen_t)addrlen);
    if (!r) return 0;
    return errno == EINPROGRESS ? 1 : -1;
}

int pk_socket_wait(pk_socket_t fd, int events, int timeout_ms) {
    struct pollfd pf;
    pf.fd = fd;
    pf.events = 0;
    pf.revents = 0;
    if (events & PK_POLL_IN) pf.events |= POLLIN;
    if (events & PK_POLL_OUT) pf.events |= POLLOUT;
    int r = poll(&pf, 1, timeout_ms);
    return r < 0 ? -1 : r > 0;
}

int pk_socket_error(pk_socket_t fd) {
    int error = 0;
    socklen_t len = sizeof error;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) return errno;
    return error;
}

int pk_socket_nonblock(pk_socket_t fd, int enabled) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (enabled) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) < 0 ? -1 : 0;
}

int pk_socket_set_timeout(pk_socket_t fd, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0) return -1;
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) < 0 ? -1 : 0;
}

int pk_socket_send(pk_socket_t fd, const void *buf, size_t len) {
    ssize_t r = send(fd, (const char *)buf, len, 0);
    return r < 0 ? -1 : (int)r;
}

int pk_socket_recv(pk_socket_t fd, void *buf, size_t len) {
    ssize_t r = recv(fd, (char *)buf, len, 0);
    return r < 0 ? -1 : (int)r;
}

int pk_socket_would_block(void) {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

int pk_socket_close(pk_socket_t fd) {
    return close(fd);
}

struct pk_addrinfo {
    struct addrinfo *item;
    struct addrinfo *result;
    struct pk_addrinfo *next;
};

int pk_resolve(const char *host, const char *service, pk_addrinfo **out) {
    struct addrinfo hint;
    struct addrinfo *result = 0;
    memset(&hint, 0, sizeof hint);
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, service, &hint, &result)) return -1;
    pk_addrinfo *list = 0;
    pk_addrinfo **tail = 0;
    for (struct addrinfo *item = result; item; item = item->ai_next) {
        pk_addrinfo *node = calloc(1, sizeof *node);
        if (!node) {
            pk_resolve_free(list);
            freeaddrinfo(result);
            return -1;
        }
        node->item = item;
        if (tail) *tail = node;
        else list = node;
        tail = &node->next;
    }
    if (list) list->result = result;
    *out = list;
    return 0;
}

pk_addrinfo *pk_addrinfo_first(const pk_addrinfo *list) {
    return (pk_addrinfo *)list;
}

pk_addrinfo *pk_addrinfo_next(const pk_addrinfo *item) {
    return item ? item->next : 0;
}

int pk_addrinfo_family(const pk_addrinfo *item) { return item->item->ai_family; }
int pk_addrinfo_type(const pk_addrinfo *item) { return item->item->ai_socktype; }
int pk_addrinfo_protocol(const pk_addrinfo *item) { return item->item->ai_protocol; }
const void *pk_addrinfo_address(const pk_addrinfo *item) { return item->item->ai_addr; }
size_t pk_addrinfo_length(const pk_addrinfo *item) { return (size_t)item->item->ai_addrlen; }

void pk_resolve_free(pk_addrinfo *list) {
    struct addrinfo *result = list ? list->result : 0;
    while (list) {
        pk_addrinfo *next = list->next;
        free(list);
        list = next;
    }
    if (result) freeaddrinfo(result);
}

int pk_random(void *buf, size_t len) {
    unsigned char *out = buf;
    size_t got = 0;
#if defined(__linux__)
    while (got < len) {
        ssize_t r = getrandom(out + got, len - got, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        got += (size_t)r;
    }
    if (got == len) return 0;
#endif
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    while (got < len) {
        ssize_t r = read(fd, out + got, len - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if (!r) {
            close(fd);
            return -1;
        }
        got += (size_t)r;
    }
    close(fd);
    return 0;
}

int pk_tls_ca_path(char *buf, size_t cap) {
    const char *paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/ssl/cert.pem"
    };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        if (access(paths[i], R_OK)) continue;
        snprintf(buf, cap, "%s", paths[i]);
        return 1;
    }
    return 0;
}

int pk_has_avx2(void) {
#if defined(__x86_64__) || defined(__i386__)
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_cpu_supports("avx2");
#else
    return 0;
#endif
#else
    return 0;
#endif
}

#endif
