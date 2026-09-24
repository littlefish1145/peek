#ifndef WS_H
#define WS_H

#include <stddef.h>

typedef struct Ws Ws;

enum { WS_CONNECTING = 0, WS_OPEN = 1, WS_CLOSING = 2, WS_CLOSED = 3 };

typedef struct {
    void (*on_open)(Ws *);
    void (*on_msg)(Ws *, const char *data, size_t n, int binary);
    void (*on_close)(Ws *, int code, const char *reason);
    void (*on_err)(Ws *, const char *what);
} WsHooks;

Ws *ws_connect(const char *url, const WsHooks *hooks, void *ud);
void ws_poll(void);
int ws_active(void);
int ws_state(Ws *);
int ws_send(Ws *, const char *data, size_t n, int binary);
void ws_close(Ws *, int code, const char *reason);
void ws_free(Ws *);
void *ws_ud(Ws *);
const char *ws_url(Ws *);
const char *ws_protocol(Ws *);

#endif
