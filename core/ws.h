/* ws.h — minimal RFC6455 WebSocket server subset for the telemetry stream.
 *
 * Single client, localhost only (127.0.0.1). Server frames are sent UNMASKED as
 * unfragmented binary messages (opcode 0x2). Zero external deps: WSAStartup via
 * Winsock2, with a self-contained SHA-1 + base64 for the Upgrade handshake.
 *
 * This is a PURE OBSERVER sink for the sim: data flows core -> client only. The
 * only client->server bytes honored are WebSocket control frames (CLOSE/PING);
 * any data payload from the client is drained and ignored. Nothing read here may
 * touch vehicle state (project directive: renderer is an observer).
 *
 * Wall-clock (QueryPerformanceCounter/Sleep) does NOT live here — pacing is the
 * serve loop's job in main.c. This module only does sockets + framing.
 *
 * Windows-only (Winsock2). Guarded so a non-Windows build simply omits it.
 */
#ifndef BL_WS_H
#define BL_WS_H

#include <stddef.h>

#ifdef _WIN32

/* Bring up Winsock, create a listening socket bound to 127.0.0.1:port, and block
 * in accept() until exactly one client connects and completes the HTTP Upgrade
 * handshake. Returns 0 on success, non-zero on failure (prints a diagnostic). */
int  ws_serve_init(unsigned short port);

/* Send one unfragmented binary message (opcode 0x2, FIN=1, unmasked) carrying the
 * given payload. Handles 7 / 16 / 64-bit payload-length encoding automatically.
 * Returns 0 on success, non-zero if the socket errored (treat as client gone). */
int  ws_send_binary(const void* ptr, size_t len);

/* Non-blocking service of the client->server direction: reply PONG to any PING,
 * and detect a CLOSE. Returns 1 if the peer has requested/observed close (caller
 * should stop streaming), 0 otherwise. Never blocks the 125 Hz emit loop. */
int  ws_poll_client(void);

/* Graceful teardown: send CLOSE if still open, closesocket() both sockets, and
 * WSACleanup(). Safe to call multiple times. */
void ws_close(void);

/* Mode 2 INTERACTIVE inbound (canon §M2, opted into by main.c's --interactive). OFF by
 * default: a client data frame is drained and IGNORED (the pure-observer contract above,
 * byte-identical). When enabled, ws_poll_client stashes the last client binary/text
 * data-frame payload (already unmasked); ws_take_inbound copies + clears it. ws.c stays
 * protocol-agnostic — main.c interprets the bytes as a BlCmd. */
void ws_set_interactive(int on);
int  ws_take_inbound(void* out, int cap);   /* bytes copied into out (0 if none) */

#else  /* non-Windows: sockets not implemented — stubs so the tree still builds */
static inline int  ws_serve_init(unsigned short port){ (void)port; return -1; }
static inline int  ws_send_binary(const void* p, size_t n){ (void)p; (void)n; return -1; }
static inline int  ws_poll_client(void){ return 1; }
static inline void ws_close(void){ }
static inline void ws_set_interactive(int on){ (void)on; }
static inline int  ws_take_inbound(void* out, int cap){ (void)out; (void)cap; return 0; }
#endif /* _WIN32 */

#endif /* BL_WS_H */
