#ifndef PEEK_PLATFORM_H
#define PEEK_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
typedef uintptr_t pk_socket_t;
#define PK_INVALID_SOCKET ((pk_socket_t)~(pk_socket_t)0)
#else
typedef int pk_socket_t;
#define PK_INVALID_SOCKET (-1)
#endif

enum {
    PK_POLL_IN = 1,
    PK_POLL_OUT = 2
};

typedef struct pk_addrinfo pk_addrinfo;

double pk_now_ms(void);
void pk_sleep_ms(unsigned ms);

int pk_term_init(void);
void pk_term_shutdown(void);
int pk_term_is_tty(void);
int pk_term_size(int *cols, int *rows);
int pk_term_read(unsigned char *buf, int cap, int timeout_ms);

int pk_socket_init(void);
void pk_socket_cleanup(void);
pk_socket_t pk_socket_create(int family, int type, int protocol);
int pk_socket_connect(pk_socket_t fd, const void *addr, size_t addrlen);
int pk_socket_wait(pk_socket_t fd, int events, int timeout_ms);
int pk_socket_error(pk_socket_t fd);
int pk_socket_nonblock(pk_socket_t fd, int enabled);
int pk_socket_set_timeout(pk_socket_t fd, int seconds);
int pk_socket_send(pk_socket_t fd, const void *buf, size_t len);
int pk_socket_recv(pk_socket_t fd, void *buf, size_t len);
int pk_socket_would_block(void);
int pk_socket_close(pk_socket_t fd);

int pk_resolve(const char *host, const char *service, pk_addrinfo **out);
pk_addrinfo *pk_addrinfo_first(const pk_addrinfo *list);
pk_addrinfo *pk_addrinfo_next(const pk_addrinfo *item);
int pk_addrinfo_family(const pk_addrinfo *item);
int pk_addrinfo_type(const pk_addrinfo *item);
int pk_addrinfo_protocol(const pk_addrinfo *item);
const void *pk_addrinfo_address(const pk_addrinfo *item);
size_t pk_addrinfo_length(const pk_addrinfo *item);
void pk_resolve_free(pk_addrinfo *list);

int pk_random(void *buf, size_t len);
int pk_tls_ca_path(char *buf, size_t cap);
int pk_has_avx2(void);

static inline unsigned pk_ctz64(uint64_t x) {
    unsigned n = 0;
    while (!(x & 1u)) {
        x >>= 1;
        n++;
    }
    return n;
}

#endif
