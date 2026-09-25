#ifdef _WIN32

#include "platform.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <stdlib.h>
#if !defined(__GNUC__) && !defined(__clang__)
#include <intrin.h>
#endif

static DWORD saved_input_mode;
static DWORD saved_output_mode;
static UINT saved_input_cp;
static UINT saved_output_cp;
static int term_ready;
static int socket_ready;

static int socket_error(void) {
    return WSAGetLastError();
}

double pk_now_ms(void) {
    LARGE_INTEGER frequency, counter;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter))
        return (double)GetTickCount64();
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

void pk_sleep_ms(unsigned ms) {
    Sleep(ms);
}

int pk_term_init(void) {
    if (term_ready) return 1;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD input_mode, output_mode;
    if (!GetConsoleMode(input, &input_mode) || !GetConsoleMode(output, &output_mode))
        return 0;
    saved_input_cp = GetConsoleCP();
    saved_output_cp = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    saved_input_mode = input_mode;
    saved_output_mode = output_mode;
    input_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    input_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(input, input_mode) || !SetConsoleMode(output, output_mode)) {
        SetConsoleMode(input, saved_input_mode);
        SetConsoleMode(output, saved_output_mode);
        SetConsoleCP(saved_input_cp);
        SetConsoleOutputCP(saved_output_cp);
        return 0;
    }
    term_ready = 1;
    return 1;
}

void pk_term_shutdown(void) {
    if (!term_ready) return;
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), saved_input_mode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), saved_output_mode);
    SetConsoleCP(saved_input_cp);
    SetConsoleOutputCP(saved_output_cp);
    term_ready = 0;
}

int pk_term_is_tty(void) {
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) &&
           GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode);
}

int pk_term_size(int *cols, int *rows) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) return -1;
    *cols = info.srWindow.Right - info.srWindow.Left + 1;
    *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    return 0;
}

int pk_term_read(unsigned char *buf, int cap, int timeout_ms) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD wait = timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms;
    DWORD available = 0;
    if (WaitForSingleObject(input, wait) != WAIT_OBJECT_0) return 0;
    if (!PeekNamedPipe(input, 0, 0, 0, &available, 0)) {
        DWORD read;
        if (!ReadFile(input, buf, (DWORD)cap, &read, 0)) return -1;
        return (int)read;
    }
    if (!available) return 0;
    if ((DWORD)cap < available) available = (DWORD)cap;
    DWORD read;
    if (!ReadFile(input, buf, available, &read, 0)) return -1;
    return (int)read;
}

int pk_socket_init(void) {
    WSADATA data;
    if (socket_ready) return 0;
    if (WSAStartup(MAKEWORD(2, 2), &data)) return -1;
    socket_ready = 1;
    return 0;
}

void pk_socket_cleanup(void) {
    if (!socket_ready) return;
    WSACleanup();
    socket_ready = 0;
}

pk_socket_t pk_socket_create(int family, int type, int protocol) {
    return socket(family, type, protocol);
}

int pk_socket_connect(pk_socket_t fd, const void *addr, size_t addrlen) {
    if (!connect(fd, (const struct sockaddr *)addr, (int)addrlen)) return 0;
    int error = socket_error();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ? 1 : -1;
}

int pk_socket_wait(pk_socket_t fd, int events, int timeout_ms) {
    WSAPOLLFD pf;
    pf.fd = fd;
    pf.events = 0;
    if (events & PK_POLL_IN) pf.events |= POLLRDNORM;
    if (events & PK_POLL_OUT) pf.events |= POLLWRNORM;
    pf.revents = 0;
    int r = WSAPoll(&pf, 1, timeout_ms);
    return r < 0 ? -1 : r > 0;
}

int pk_socket_error(pk_socket_t fd) {
    int error = 0;
    int length = sizeof error;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&error, &length) < 0)
        return socket_error();
    return error;
}

int pk_socket_nonblock(pk_socket_t fd, int enabled) {
    u_long mode = enabled ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &mode) ? -1 : 0;
}

int pk_socket_set_timeout(pk_socket_t fd, int seconds) {
    DWORD timeout = (DWORD)seconds * 1000u;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout)) return -1;
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof timeout) ? -1 : 0;
}

int pk_socket_send(pk_socket_t fd, const void *buf, size_t len) {
    int r = send(fd, (const char *)buf, (int)len, 0);
    return r == SOCKET_ERROR ? -1 : r;
}

int pk_socket_recv(pk_socket_t fd, void *buf, size_t len) {
    int r = recv(fd, (char *)buf, (int)len, 0);
    return r == SOCKET_ERROR ? -1 : r;
}

int pk_socket_would_block(void) {
    int error = socket_error();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}

int pk_socket_close(pk_socket_t fd) {
    return closesocket(fd);
}

struct pk_addrinfo {
    ADDRINFOA *item;
    ADDRINFOA *result;
    struct pk_addrinfo *next;
};

int pk_resolve(const char *host, const char *service, pk_addrinfo **out) {
    ADDRINFOA hints;
    ADDRINFOA *result = 0;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, service, &hints, &result)) return -1;
    pk_addrinfo *list = 0;
    pk_addrinfo **tail = 0;
    for (ADDRINFOA *item = result; item; item = item->ai_next) {
        pk_addrinfo *node = (pk_addrinfo *)calloc(1, sizeof *node);
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
    ADDRINFOA *result = list ? list->result : 0;
    while (list) {
        pk_addrinfo *next = list->next;
        free(list);
        list = next;
    }
    if (result) freeaddrinfo(result);
}

int pk_random(void *buf, size_t len) {
    if (BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG))
        return -1;
    return 0;
}

int pk_tls_ca_path(char *buf, size_t cap) {
    DWORD length = GetEnvironmentVariableA("PEEK_CA_BUNDLE", buf, (DWORD)cap);
    if (!length || length >= cap) return 0;
    buf[length] = 0;
    return 1;
}

int pk_has_avx2(void) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_cpu_supports("avx2");
#else
    int regs[4];
    __cpuid(regs, 1);
    if (!(regs[2] & (1 << 28))) return 0;
    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 5)) != 0;
#endif
}

#endif
