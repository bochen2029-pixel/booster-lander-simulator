/* ws.c — minimal RFC6455 WebSocket server subset (single client, localhost).
 *
 * Scope (deliberately tiny, canon ~250 lines):
 *   - WSAStartup, listen on 127.0.0.1:port, accept ONE client.
 *   - HTTP Upgrade handshake: Sec-WebSocket-Accept = base64(SHA1(key + GUID)).
 *   - ws_send_binary: one unmasked binary message, 7/16/64-bit length paths.
 *   - ws_poll_client: non-blocking; PING->PONG, CLOSE detected, data ignored.
 *   - ws_close: graceful CLOSE + teardown.
 *
 * SHA-1 and base64 are hand-rolled below (public-domain style) so core keeps its
 * zero-external-dependency rule. Only <winsock2.h>/<ws2tcpip.h> + ws2_32.lib.
 */
#ifdef _WIN32

#include "ws.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")  /* belt-and-suspenders; CMake also links it */

/* ------------------------------------------------------------------ state ---- */
static SOCKET g_listen = INVALID_SOCKET;
static SOCKET g_client = INVALID_SOCKET;
static int    g_wsa_up = 0;   /* WSAStartup succeeded */
static int    g_closed = 0;   /* CLOSE has been sent/observed */

/* ------------------------------------------------------------------ SHA-1 ---- */
/* Minimal SHA-1 (FIPS 180-1). Produces a 20-byte digest. */
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t n; } SHA1;

static uint32_t rol32(uint32_t x, int c){ return (x << c) | (x >> (32 - c)); }

static void sha1_block(SHA1* s, const uint8_t* p){
    uint32_t w[80];
    for(int i=0;i<16;i++)
        w[i] = ((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|((uint32_t)p[i*4+2]<<8)|((uint32_t)p[i*4+3]);
    for(int i=16;i<80;i++) w[i] = rol32(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
    uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4];
    for(int i=0;i<80;i++){
        uint32_t f,k;
        if(i<20){ f=(b&c)|((~b)&d); k=0x5A827999u; }
        else if(i<40){ f=b^c^d; k=0x6ED9EBA1u; }
        else if(i<60){ f=(b&c)|(b&d)|(c&d); k=0x8F1BBCDCu; }
        else { f=b^c^d; k=0xCA62C1D6u; }
        uint32_t t = rol32(a,5)+f+e+k+w[i];
        e=d; d=c; c=rol32(b,30); b=a; a=t;
    }
    s->h[0]+=a; s->h[1]+=b; s->h[2]+=c; s->h[3]+=d; s->h[4]+=e;
}
static void sha1_init(SHA1* s){
    s->h[0]=0x67452301u; s->h[1]=0xEFCDAB89u; s->h[2]=0x98BADCFEu;
    s->h[3]=0x10325476u; s->h[4]=0xC3D2E1F0u; s->len=0; s->n=0;
}
static void sha1_update(SHA1* s, const void* data, size_t len){
    const uint8_t* p=(const uint8_t*)data; s->len += (uint64_t)len*8u;
    while(len){
        size_t take = 64 - s->n; if(take>len) take=len;
        memcpy(s->buf + s->n, p, take); s->n += take; p += take; len -= take;
        if(s->n==64){ sha1_block(s, s->buf); s->n=0; }
    }
}
static void sha1_final(SHA1* s, uint8_t out[20]){
    uint64_t bitlen = s->len;
    uint8_t pad = 0x80; sha1_update(s, &pad, 1);
    uint8_t z = 0x00; while(s->n != 56) sha1_update(s, &z, 1);
    uint8_t lb[8]; for(int i=0;i<8;i++) lb[i]=(uint8_t)(bitlen >> (56 - i*8));
    sha1_update(s, lb, 8);
    for(int i=0;i<5;i++){ out[i*4]=(uint8_t)(s->h[i]>>24); out[i*4+1]=(uint8_t)(s->h[i]>>16);
                          out[i*4+2]=(uint8_t)(s->h[i]>>8); out[i*4+3]=(uint8_t)s->h[i]; }
}

/* ------------------------------------------------------------------ base64 --- */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
/* Encode exactly 20 bytes -> 28 chars (incl one '=' pad). Writes NUL-terminated. */
static void base64_20(const uint8_t in[20], char out[29]){
    int oi=0;
    for(int i=0;i<18;i+=3){
        uint32_t v=((uint32_t)in[i]<<16)|((uint32_t)in[i+1]<<8)|in[i+2];
        out[oi++]=B64[(v>>18)&63]; out[oi++]=B64[(v>>12)&63];
        out[oi++]=B64[(v>>6)&63];  out[oi++]=B64[v&63];
    }
    /* last group: 2 bytes (in[18],in[19]) -> 3 chars + '=' */
    uint32_t v=((uint32_t)in[18]<<16)|((uint32_t)in[19]<<8);
    out[oi++]=B64[(v>>18)&63]; out[oi++]=B64[(v>>12)&63];
    out[oi++]=B64[(v>>6)&63];  out[oi++]='=';
    out[oi]='\0';
}

/* --------------------------------------------------------------- low-level --- */
static int send_all(SOCKET s, const uint8_t* p, size_t n){
    size_t off=0;
    while(off<n){
        int k = send(s, (const char*)(p+off), (int)(n-off), 0);
        if(k==SOCKET_ERROR) return -1;
        off += (size_t)k;
    }
    return 0;
}

/* Case-insensitive header-line locate + trimmed value copy (handshake only). */
static int header_value(const char* req, const char* name, char* out, size_t outsz){
    size_t nlen = strlen(name);
    const char* p = req;
    while(*p){
        /* compare header name at start of a line, case-insensitively */
        int match=1;
        for(size_t i=0;i<nlen;i++){
            char a=p[i], b=name[i];
            if(a>='A'&&a<='Z') a=(char)(a-'A'+'a');
            if(b>='A'&&b<='Z') b=(char)(b-'A'+'a');
            if(a!=b){ match=0; break; }
        }
        if(match && p[nlen]==':'){
            const char* v=p+nlen+1;
            while(*v==' '||*v=='\t') v++;
            size_t o=0;
            while(*v && *v!='\r' && *v!='\n' && o+1<outsz) out[o++]=*v++;
            out[o]='\0';
            return 1;
        }
        /* advance to next line */
        while(*p && *p!='\n') p++;
        if(*p=='\n') p++;
    }
    return 0;
}

/* -------------------------------------------------------------- handshake ---- */
static int do_handshake(void){
    char req[4096]; size_t total=0;
    /* Read until end of headers (\r\n\r\n). Blocking is fine here. */
    for(;;){
        if(total >= sizeof(req)-1) break;
        int k = recv(g_client, req+total, (int)(sizeof(req)-1-total), 0);
        if(k<=0) return -1;
        total += (size_t)k; req[total]='\0';
        if(strstr(req, "\r\n\r\n")) break;
    }
    char key[256];
    if(!header_value(req, "Sec-WebSocket-Key", key, sizeof(key))){
        fprintf(stderr, "ws: handshake missing Sec-WebSocket-Key\n");
        return -1;
    }
    /* accept = base64(sha1(key + magic-guid)) */
    static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SHA1 s; sha1_init(&s);
    sha1_update(&s, key, strlen(key));
    sha1_update(&s, GUID, strlen(GUID));
    uint8_t dg[20]; sha1_final(&s, dg);
    char accept[29]; base64_20(dg, accept);

    char resp[512];
    int rn = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept);
    if(rn<=0 || rn>=(int)sizeof(resp)) return -1;
    return send_all(g_client, (const uint8_t*)resp, (size_t)rn);
}

/* ------------------------------------------------------------------ public --- */
int ws_serve_init(unsigned short port){
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2,2), &wsa)!=0){ fprintf(stderr,"ws: WSAStartup failed\n"); return -1; }
    g_wsa_up=1;

    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(g_listen==INVALID_SOCKET){ fprintf(stderr,"ws: socket() failed (%d)\n", WSAGetLastError()); ws_close(); return -1; }

    BOOL yes=TRUE;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* 127.0.0.1 only */

    if(bind(g_listen,(struct sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR){
        fprintf(stderr,"ws: bind(127.0.0.1:%u) failed (%d)\n", port, WSAGetLastError()); ws_close(); return -1;
    }
    if(listen(g_listen, 1)==SOCKET_ERROR){
        fprintf(stderr,"ws: listen() failed (%d)\n", WSAGetLastError()); ws_close(); return -1;
    }
    fprintf(stderr,"ws: listening on 127.0.0.1:%u — waiting for a client...\n", port);

    g_client = accept(g_listen, NULL, NULL);   /* blocking accept of ONE client */
    if(g_client==INVALID_SOCKET){ fprintf(stderr,"ws: accept() failed (%d)\n", WSAGetLastError()); ws_close(); return -1; }

    /* stop listening once we have our single client — no lingering listener */
    closesocket(g_listen); g_listen = INVALID_SOCKET;

    if(do_handshake()!=0){ fprintf(stderr,"ws: handshake failed\n"); ws_close(); return -1; }

    /* client socket is non-blocking from here so the emit loop never stalls on read */
    u_long nb=1; ioctlsocket(g_client, FIONBIO, &nb);
    fprintf(stderr,"ws: client connected, handshake OK\n");
    return 0;
}

int ws_send_binary(const void* ptr, size_t len){
    if(g_client==INVALID_SOCKET || g_closed) return -1;
    uint8_t hdr[10]; size_t hn=0;
    hdr[0]=0x82;                     /* FIN=1, opcode=0x2 (binary) */
    if(len<126){
        hdr[1]=(uint8_t)len;         /* server frames are UNMASKED (mask bit 0) */
        hn=2;
    } else if(len<=0xFFFF){
        hdr[1]=126;
        hdr[2]=(uint8_t)((len>>8)&0xFF);
        hdr[3]=(uint8_t)(len&0xFF);
        hn=4;
    } else {
        hdr[1]=127;
        for(int i=0;i<8;i++) hdr[2+i]=(uint8_t)((uint64_t)len >> (56 - i*8));
        hn=10;
    }
    if(send_all(g_client, hdr, hn)!=0) return -1;
    if(len && send_all(g_client, (const uint8_t*)ptr, len)!=0) return -1;
    return 0;
}

/* Send a CLOSE control frame (opcode 0x8, empty payload, unmasked). */
static void send_close_frame(void){
    if(g_client==INVALID_SOCKET || g_closed) return;
    uint8_t f[2]={0x88, 0x00};
    send_all(g_client, f, 2);
    g_closed=1;
}

/* Reply to a client PING with a PONG carrying the same (already-unmasked) payload. */
static void send_pong(const uint8_t* payload, size_t n){
    if(g_client==INVALID_SOCKET) return;
    uint8_t hdr[2]; hdr[0]=0x8A; hdr[1]=(uint8_t)(n<126?n:0);
    if(n>=126) return;                 /* control frames are <=125 by spec */
    if(send_all(g_client, hdr, 2)!=0) return;
    if(n) send_all(g_client, payload, n);
}

int ws_poll_client(void){
    if(g_client==INVALID_SOCKET) return 1;
    if(g_closed) return 1;

    /* Drain whatever is buffered without blocking. We only need enough of each
     * frame header to classify it and, for control frames, read the small payload.
     * Data frames (which a pure-observer renderer must not send) are consumed and
     * ignored. */
    uint8_t b[2048];
    int k = recv(g_client, (char*)b, (int)sizeof(b), 0);
    if(k==0){ /* peer closed TCP */ g_closed=1; return 1; }
    if(k==SOCKET_ERROR){
        int e=WSAGetLastError();
        if(e==WSAEWOULDBLOCK) return 0;    /* nothing pending — normal */
        g_closed=1; return 1;              /* real error -> treat as gone */
    }

    /* Walk frames in the buffer (client->server frames are ALWAYS masked). */
    int i=0;
    while(i+2 <= k){
        uint8_t op = b[i] & 0x0F;
        int masked = (b[i+1] & 0x80) != 0;
        uint64_t plen = b[i+1] & 0x7F;
        int j = i+2;
        if(plen==126){ if(j+2>k) break; plen=((uint64_t)b[j]<<8)|b[j+1]; j+=2; }
        else if(plen==127){ if(j+8>k) break; plen=0; for(int q=0;q<8;q++) plen=(plen<<8)|b[j+q]; j+=8; }
        uint8_t mask[4]={0,0,0,0};
        if(masked){ if(j+4>k) break; for(int q=0;q<4;q++) mask[q]=b[j+q]; j+=4; }
        if((uint64_t)(j) + plen > (uint64_t)k) break;   /* partial frame; stop */

        /* unmask small control payloads in place */
        uint8_t pay[125];
        size_t pn = (plen<sizeof(pay))?(size_t)plen:sizeof(pay);
        for(size_t q=0;q<pn;q++) pay[q] = (uint8_t)(b[j+q] ^ mask[q&3]);

        if(op==0x8){ /* CLOSE */ send_close_frame(); return 1; }
        else if(op==0x9){ /* PING */ send_pong(pay, pn); }
        /* op==0xA PONG, 0x1/0x2 data, continuations: ignore (observer sink) */

        i = j + (int)plen;   /* advance past this frame */
    }
    return 0;
}

void ws_close(void){
    if(g_client!=INVALID_SOCKET){
        send_close_frame();
        closesocket(g_client);
        g_client=INVALID_SOCKET;
    }
    if(g_listen!=INVALID_SOCKET){
        closesocket(g_listen);
        g_listen=INVALID_SOCKET;
    }
    if(g_wsa_up){ WSACleanup(); g_wsa_up=0; }
}

#endif /* _WIN32 */
