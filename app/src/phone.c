/* phone.c — tiny LAN control server: open http://<your-ip>:8777 on your phone */
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include "common.h"

static HANDLE   g_thread;
static SOCKET   g_srv = INVALID_SOCKET;
static volatile LONG g_stop;
static wchar_t  g_url[128];
static Look     g_last;
static int      g_have_last;

static void build_url(void)
{
    char hn[256] = { 0 };
    gethostname(hn, sizeof hn);
    struct hostent *he = gethostbyname(hn);
    char ip[64] = "127.0.0.1";
    if (he)
        for (char **a = he->h_addr_list; *a; a++) {
            unsigned char *q = (unsigned char *)*a;
            if (q[0] == 127) continue;
            wsprintfA(ip, "%d.%d.%d.%d", q[0], q[1], q[2], q[3]);
            break;
        }
    wchar_t wip[64];
    MultiByteToWideChar(CP_ACP, 0, ip, -1, wip, 64);
    wsprintfW(g_url, L"http://%s:%d", wip, CX_PORT);
}

wchar_t *Phone_Summary(void) { return g_url; }

void Phone_SetLook(const Look *lk)
{
    g_last = *lk;
    g_have_last = 1;
}

static const char PAGE[] =
"<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>ChromaX phone</title><style>"
"body{background:#0c0c10;color:#f2f2f5;font:16px/1.4 system-ui,sans-serif;margin:0;padding:18px}"
"h1{font-size:18px;letter-spacing:.12em;text-transform:uppercase;color:#c6ff3d;margin:0 0 4px}"
".sub{color:#8a8a96;font-size:13px;margin-bottom:18px}"
".card{background:#16161d;border:1px solid #26262f;border-radius:14px;padding:16px;margin-bottom:14px}"
"label{display:flex;justify-content:space-between;font-size:13px;color:#8a8a96;margin:14px 0 6px}"
"label b{color:#f2f2f5;font-variant-numeric:tabular-nums}"
"input[type=range]{width:100%;accent-color:#c6ff3d}"
"button{background:#c6ff3d;color:#0c0c10;border:0;border-radius:10px;padding:12px 18px;font-weight:700;font-size:15px;width:100%}"
".on{background:#1d1d26;color:#f2f2f5;border:1px solid #34343f}"
"</style><h1>ChromaX</h1><div class=sub>phone control · same Wi-Fi only</div>"
"<div class=card>"
"<label>Saturation <b id=vsat>150%</b></label><input id=sat type=range min=100 max=300 value=150>"
"<label>Brightness <b id=vbri>0%</b></label><input id=bri type=range min=-100 max=100 value=0>"
"<label>Contrast <b id=vcon>0%</b></label><input id=con type=range min=-100 max=100 value=0>"
"<label>Temperature <b id=vtemp>0%</b></label><input id=temp type=range min=-100 max=100 value=0>"
"<label>Gamma <b id=vgam>1.00</b></label><input id=gamma type=range min=40 max=240 value=100>"
"</div>"
"<button id=on class=on>Look: ON</button>"
"<script>"
"const ids=['sat','bri','con','temp','gamma'];"
"async function sync(){try{const j=await(await fetch('/status')).json();"
"for(const k of ids){if(j[k]!==undefined){const el=document.getElementById(k);el.value=j[k];}"
"document.getElementById('v'+(k==='gamma'?'gam':k==='temp'?'temp':k)).textContent="
"(k==='gamma'? (j.gamma/100).toFixed(2) : j[k]+'%');}"
"on.textContent='Look: '+(j.enabled?'ON':'OFF');on.className=j.enabled?'':'on';}catch(e){}}"
"for(const k of ids){const el=document.getElementById(k);"
"el.oninput=()=>{document.getElementById('v'+(k==='gamma'?'gam':k==='temp'?'temp':k)).textContent="
"(k==='gamma'? (el.value/100).toFixed(2) : el.value+'%');"
"fetch('/set?k='+k+'&v='+el.value)};}"
"on.onclick=async()=>{const j=await(await fetch('/status')).json();"
"fetch('/set?k=on&v='+(j.enabled?0:1));setTimeout(sync,150)};"
"sync();setInterval(sync,3000);</script>";

static void handle_client(SOCKET c)
{
    char req[2048];
    int r = recv(c, req, sizeof req - 1, 0);
    if (r <= 0) { closesocket(c); return; }
    req[r] = 0;

    const char *body = PAGE;
    int blen = (int)sizeof PAGE - 1;
    const char *ctype = "text/html; charset=utf-8";
    char json[512];

    if (strncmp(req, "GET /set?", 9) == 0) {
        char k[16] = { 0 };
        double v = 0;
        const char *q = strstr(req, "k=");
        if (q && sscanf(q, "k=%15[^& ]&v=%lf", k, &v) == 2)
            PostMessageW(g_hwnd, WM_APP_LOOK, (WPARAM)(unsigned char)k[0],
                         (LPARAM)(int)v);
        body = "{\"ok\":1}"; blen = 8; ctype = "application/json";
    } else if (strncmp(req, "GET /status", 11) == 0) {
        if (!g_have_last) { g_last.enabled = 1; g_last.sat = 150; g_last.gamma = 1.f; }
        blen = wsprintfA(json,
            "{\"sat\":%d,\"bri\":%d,\"con\":%d,\"temp\":%d,\"hue\":%d,\"gamma\":%d,\"enabled\":%d}",
            (int)g_last.sat, (int)g_last.bri, (int)g_last.con, (int)g_last.temp,
            (int)g_last.hue, (int)(g_last.gamma * 100), g_last.enabled ? 1 : 0);
        body = json; ctype = "application/json";
    }

    char hdr[256];
    wsprintfA(hdr,
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Connection: close\r\nCache-Control: no-store\r\n\r\n",
        ctype, blen);
    send(c, hdr, (int)strlen(hdr), 0);
    send(c, body, blen, 0);
    closesocket(c);
}

static DWORD WINAPI server_thread(LPVOID arg)
{
    (void)arg;
    for (;;) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(g_srv, &rf);
        struct timeval tv = { 1, 0 };
        int sel = select(0, &rf, NULL, NULL, &tv);
        if (InterlockedCompareExchange(&g_stop, 0, 0)) break;
        if (sel <= 0) continue;
        SOCKET c = accept(g_srv, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        struct timeval rt = { 2, 0 };
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&rt, sizeof rt);
        handle_client(c);
    }
    return 0;
}

int Phone_Start(void)
{
    if (g_thread) return 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return -1;
    g_srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_srv == INVALID_SOCKET) return -1;
    BOOL yes = 1;
    setsockopt(g_srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(CX_PORT);
    if (bind(g_srv, (struct sockaddr *)&a, sizeof a) == SOCKET_ERROR) {
        closesocket(g_srv); g_srv = INVALID_SOCKET; return -1;
    }
    if (listen(g_srv, 4) == SOCKET_ERROR) {
        closesocket(g_srv); g_srv = INVALID_SOCKET; return -1;
    }
    g_stop = 0;
    build_url();
    g_thread = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    return 0;
}

void Phone_Stop(void)
{
    if (!g_thread) return;
    InterlockedExchange(&g_stop, 1);
    closesocket(g_srv);
    g_srv = INVALID_SOCKET;
    WaitForSingleObject(g_thread, 2000);
    CloseHandle(g_thread);
    g_thread = NULL;
    WSACleanup();
}
