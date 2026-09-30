/* phone.c \u2014 LAN phone/tablet control (HTTP, token-authenticated)
 *
 * Binds 0.0.0.0:8777 on this machine's LAN.  Every request must carry the
 * pairing token (query param `token` or header `X-Token`).  The token is
 * random (BCryptGenRandom), generated once and kept in the app data folder
 * \u2014 never hardcoded, never transmitted anywhere.  The server thread only
 * parses and queues actions; all real work happens on the UI thread.
 *
 * Endpoints:
 *   GET  /api/state        full app state (JSON)
 *   GET  /api/games        game catalog + looks
 *   GET  /api/presets      saved preset names
 *   GET  /api/devices      recently connected devices
 *   POST /api/look         apply colour look (partial updates OK)
 *   POST /api/preset       apply saved preset by name
 *   POST /api/game         apply a game's look (game + look index)
 *   POST /api/crosshair    set crosshair config (partial updates OK)
 *   POST /api/hdr          toggle HDR on/off
 *   POST /api/reset        reset all changes
 */
#include "common.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>

/* pending action queue (phone thread -> UI thread); PhoneAct + PH_* from common.h */

static PhoneAct g_queue[8];
static int g_qhead, g_qcount;
static CRITICAL_SECTION g_cs;

static SOCKET g_listen = INVALID_SOCKET;
static HANDLE g_thread;
static int    g_running;
static char   g_token[33];
static wchar_t g_token_w[33];

static void token_sync_w(void)
{
    MultiByteToWideChar(CP_UTF8, 0, g_token, -1, g_token_w, 32);
    g_token_w[32] = 0;
}

static char   g_dev[4][32];   /* recent device IPs */
static int    g_ndev;

static void queue_act(const PhoneAct *a)
{
    EnterCriticalSection(&g_cs);
    if (g_qcount < 8) {
        g_queue[g_qhead] = *a;
        g_qhead = (g_qhead + 1) % 8;
        g_qcount++;
    }
    LeaveCriticalSection(&g_cs);
}

/* UI thread drains this */
int Phone_Pending(PhoneAct *out)
{
    EnterCriticalSection(&g_cs);
    int have = 0;
    if (g_qcount > 0) {
        *out = g_queue[(g_qhead - g_qcount + 8) % 8];
        g_qcount--;
        have = 1;
    }
    LeaveCriticalSection(&g_cs);
    return have;
}

/* ---------------- token ---------------- */

static void gen_token(void)
{
    BYTE rb[16];
    HCRYPTPROV hp = 0;
    if (!CryptAcquireContextW(&hp, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) ||
        !CryptGenRandom(hp, 16, rb)) {
        for (int i = 0; i < 16; i++) rb[i] = (BYTE)(rand() & 0xFF);
    }
    if (hp) CryptReleaseContext(hp, 0);
    for (int i = 0; i < 16; i++)
        sprintf(g_token + i * 2, "%02x", rb[i]);
    g_token[32] = 0;
    token_sync_w();
}

static void token_load_or_gen(void)
{
    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s\\phonetoken.txt", g_appdata);
    FILE *f = _wfopen(path, L"r");
    if (f) {
        char t[40] = { 0 };
        if (fgets(t, sizeof t, f) && strlen(t) == 32) {
            int ok = 1;
            for (int i = 0; i < 32; i++)
                if (isxdigit((unsigned char)t[i]) == 0) { ok = 0; break; }
            if (ok) { lstrcpynA(g_token, t, 33); token_sync_w(); fclose(f); return; }
        }
        fclose(f);
    }
    gen_token();
    f = _wfopen(path, L"w");
    if (f) { fputs(g_token, f); fclose(f); }
}

const wchar_t *Phone_Token(void) { return g_token_w; }

static int token_ok(const char *t)
{
    if (!t || strlen(t) != 32) return 0;
    unsigned long a = 0, b = 0;
    for (int i = 0; i < 32; i++)
        a = (a * 31u) + (unsigned long)(unsigned char)t[i];
    for (int i = 0; i < 32; i++)
        b = (b * 31u) + (unsigned long)(unsigned char)g_token[i];
    return a == b;   /* fixed-length, no early exit */
}

/* ---------------- tiny HTTP ---------------- */

static int read_all(SOCKET s, char *buf, int max)
{
    int total = 0;
    while (total < max - 1) {
        int n = recv(s, buf + total, max - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        char *hdrEnd = strstr(buf, "\r\n\r\n");
        if (hdrEnd) {
            int len = 0;
            for (char *pe = buf; pe < hdrEnd; ) {
                if (_strnicmp(pe, "Content-Length:", 15) == 0) {
                    pe += 15;
                    len = atoi(pe);
                    break;
                }
                pe = strchr(pe, '\n');
                if (!pe) break;
                pe++;
            }
            int hdrlen = (int)(hdrEnd - buf + 4);
            if (total >= hdrlen + len) break;
        } else if (total > 4096) break;
    }
    buf[total] = 0;
    return total;
}

static void send_http(SOCKET s, int code, const char *ctype, const char *body)
{
    const char *msg = code == 200 ? "OK" : (code == 400 ? "Bad Request" :
                      (code == 401 ? "Unauthorized" : "Not Found"));
    char hdr[160];
    int n = sprintf(hdr, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                   "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
                    code, msg, ctype, (int)strlen(body));
    (void)send(s, hdr, n, 0);
    (void)send(s, body, (int)strlen(body), 0);
}

static char *look_to_json(const CxLook *l)
{
    char *buf = (char *)malloc(700);
    sprintf(buf,
        "{\"saturation\":%.2f,\"vibrance\":%.2f,\"brightness\":%.2f,\"contrast\":%.2f,"
        "\"gamma\":%.3f,\"temperature\":%.1f,\"tint\":%.1f,\"red\":%.1f,\"green\":%.1f,"
        "\"blue\":%.1f,\"shadows\":%.1f,\"highlights\":%.1f,\"black\":%.1f,\"white\":%.1f,"
        "\"sharpness\":%.1f,\"clarity\":%.1f,\"intensity\":%.1f,\"hue\":%.1f,"
        "\"dehaze\":%.1f,\"enabled\":%d}",
        l->sat, l->vibrance, l->brightness, l->contrast, l->gamma, l->temperature,
        l->tint, l->red, l->green, l->blue, l->shadows, l->highlights, l->blacklevel,
        l->whitepoint, l->sharpness, l->clarity, l->intensity, l->hue, l->dehaze,
        l->enabled);
    return buf;
}

static void esc_into(char *dst, int dz, const char *s)
{
    int o = 0;
    for (const char *c = s; *c && o < dz - 1; c++) {
        if (*c == '"' || *c == '\\') { dst[o++] = '\\'; if (o >= dz - 1) break; }
        dst[o++] = *c;
    }
    dst[o] = 0;
}

static void handle_conn(SOCKET s)
{
    char buf[16384];
    int n = read_all(s, buf, sizeof buf);
    if (n < 4) { closesocket(s); return; }

    char method[8] = { 0 }, path[300] = { 0 };
    char *sp1 = strchr(buf, ' ');
    if (!sp1) { closesocket(s); return; }
    *sp1 = 0;
    lstrcpynA(method, buf, 8);
    char *p2 = sp1 + 1;
    char *sp2 = strchr(p2, ' ');
    if (sp2) {
        int plen = (int)(sp2 - p2);
        if (plen > 299) plen = 299;
        memcpy(path, p2, plen);
        path[plen] = 0;
    }

    char *body = strstr(buf, "\r\n\r\n");
    body = body ? body + 4 : "";

    char token[40] = { 0 };
    char *q = strchr(path, '?');
    if (q) {
        char *tk = strstr(q, "token=");
        if (tk) {
            tk += 6;
            for (int i = 0; i < 32 && tk[i] && tk[i] != '&' && tk[i] != ' '; i++)
                token[i] = tk[i];
        }
    }
    if (!token[0]) {
        char *ht = strstr(buf, "X-Token:");
        if (ht) {
            ht += 8;
            while (*ht == ' ') ht++;
            for (int i = 0; i < 32 && *ht && *ht != '\r' && *ht != '\n'; i++)
                token[i] = *ht++;
        }
    }

    if (!token_ok(token)) {
        send_http(s, 401, "application/json", "{\"error\":\"bad token\"}");
        closesocket(s);
        return;
    }

    /* ---------------- GET ---------------- */
    if (lstrcmpA(method, "GET") == 0) {
        if (lstrcmpA(path, "/api/state") == 0) {
            char *look = look_to_json(&g_look);
            char monjson[1024] = { 0 };
            int off = 0;
            for (int i = 0; i < MonCount() && i < 4 && off < 900; i++) {
                const MonInfo *m = MonGet(i);
                char nm[96] = { 0 };
                esc_into(nm, sizeof nm, Main_Utf16ToUtf8Alloc(m->name));
                off += sprintf(monjson + off, "%s{\"name\":\"%s\",\"res\":\"%dx%d\",\"hz\":%d}",
                               i ? "," : "", nm, m->res_w, m->res_h, m->hz);
            }
            char resp2[4096];
            sprintf(resp2,
                "{\"app\":\"ChromaX\",\"version\":\"2.0.0\",\"gaming\":%d,\"look\":%s,"
                "\"crosshair\":{\"on\":%d,\"shape\":%d,\"size\":%d,\"opacity\":%d},"
                "\"monitors\":[%s]}",
                Main_GamingMode(), look, g_xh.on, g_xh.shape, g_xh.size, g_xh.opacity,
                monjson);
            free(look);
            send_http(s, 200, "application/json", resp2);
        } else if (lstrcmpA(path, "/api/games") == 0) {
            char resp[16384];
            int off = sprintf(resp, "[");
            for (int i = 0; i < CxGames_Count() && off < 15000; i++) {
                const CxGame *g = CxGames_Get(i);
                off += sprintf(resp + off, "%s{\"name\":\"%s\",\"looks\":[",
                               i ? "," : "", g->name);
                for (int k = 0; k < g->nlooks; k++)
                    off += sprintf(resp + off, "%s\"%s\"", k ? "," : "", g->looks[k].name);
                off += sprintf(resp + off, "]}");
            }
            resp[off] = ']';
            resp[off + 1] = 0;
            send_http(s, 200, "application/json", resp);
        } else if (lstrcmpA(path, "/api/presets") == 0) {
            char resp[4096] = "[\"";
            int off = (int)strlen(resp);
            int first = 1;
            WIN32_FIND_DATAW fd;
            wchar_t pat[MAX_PATH];
            wsprintfW(pat, L"%s\\presets\\*.json", g_appdata);
            HANDLE h = FindFirstFileW(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    char nb[96] = { 0 };
                    if (wcslen(fd.cFileName) > 5) {
                        wchar_t base[64];
                        lstrcpynW(base, fd.cFileName, 58);
                        base[58] = 0;              /* strip ".json" */
                        WideCharToMultiByte(CP_UTF8, 0, base, -1, nb, 95, NULL, NULL);
                    }
                    if (!nb[0]) continue;
                    if (!first) { off += sprintf(resp + off, "\",\""); }
                    off += sprintf(resp + off, "%s", nb);
                    first = 0;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (!first) { off += sprintf(resp + off, "\"]"); }
            else { resp[1] = ']'; resp[2] = 0; }
            send_http(s, 200, "application/json", resp);
        } else if (lstrcmpA(path, "/api/devices") == 0) {
            char resp[512] = "{\"devices\":[";
            int off = (int)strlen(resp);
            for (int i = 0; i < g_ndev; i++)
                off += sprintf(resp + off, "%s\"%s\"", i ? "," : "", g_dev[i]);
            off += sprintf(resp + off, "]}");
            send_http(s, 200, "application/json", resp);
        } else if (lstrcmpA(path, "/") == 0) {
            send_http(s, 200, "text/plain",
                      "ChromaX phone control. Try /api/state?token=...");
        } else {
            send_http(s, 404, "application/json", "{\"error\":\"not found\"}");
        }
        closesocket(s);
        return;
    }

    /* ---------------- POST ---------------- */
    if (lstrcmpA(method, "POST") == 0) {
        CxJson *j = NULL;
        char err[96] = { 0 };
        if (CxJson_Parse(body, strlen(body), &j, err, sizeof err) != 0 || !j) {
            send_http(s, 400, "application/json", "{\"error\":\"bad json\"}");
            closesocket(s);
            return;
        }

        if (lstrcmpA(path, "/api/look") == 0) {
            PhoneAct a;
            memset(&a, 0, sizeof a);
            a.action = PH_LOOK;
            a.look = g_look;
            struct { const char *k; int id; } m[] = {
                { "saturation", CXLOOK_SAT }, { "vibrance", CXLOOK_VIBRANCE },
                { "brightness", CXLOOK_BRIGHTNESS }, { "contrast", CXLOOK_CONTRAST },
                { "gamma", CXLOOK_GAMMA }, { "temperature", CXLOOK_TEMPERATURE },
                { "tint", CXLOOK_TINT }, { "red", CXLOOK_RED }, { "green", CXLOOK_GREEN },
                { "blue", CXLOOK_BLUE }, { "shadows", CXLOOK_SHADOWS },
                { "highlights", CXLOOK_HIGHLIGHTS }, { "black", CXLOOK_BLACK },
                { "white", CXLOOK_WHITE }, { "sharpness", CXLOOK_SHARP },
                { "clarity", CXLOOK_CLARITY }, { "intensity", CXLOOK_INTENSITY },
                { "hue", CXLOOK_HUE }, { "dehaze", CXLOOK_DEHAZE },
            };
            for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++)
                if (CxJson_IsNum(j, m[i].k))
                    CxLook_Set(&a.look, m[i].id, (float)CxJson_GetNum(j, m[i].k, 100.0));
            a.look.enabled = CxJson_GetBool(j, "enabled", 1);
            CxLook_Clamp(&a.look);
            CxJson_Free(j);
            queue_act(&a);
            send_http(s, 200, "application/json", "{\"ok\":true}");
        } else if (lstrcmpA(path, "/api/preset") == 0) {
            const char *name = CxJson_GetStr(j, "name", "");
            CxJson_Free(j);
            if (strlen(name) < 1 || strlen(name) > 150) {
                send_http(s, 400, "application/json", "{\"error\":\"name required\"}");
            } else {
                PhoneAct a;
                memset(&a, 0, sizeof a);
                a.action = PH_PRESET;
                lstrcpynA(a.name, name, sizeof a.name);
                queue_act(&a);
                send_http(s, 200, "application/json", "{\"ok\":true}");
            }
        } else if (lstrcmpA(path, "/api/game") == 0) {
            int gi = (int)CxJson_GetNum(j, "game", -1);
            int li = (int)CxJson_GetNum(j, "look", 0);
            CxJson_Free(j);
            if (gi < 0 || gi >= CxGames_Count() || li < 0) {
                send_http(s, 400, "application/json", "{\"error\":\"bad game/look index\"}");
            } else {
                PhoneAct a;
                memset(&a, 0, sizeof a);
                a.action = PH_GAME;
                a.a = gi; a.b = li;
                queue_act(&a);
                send_http(s, 200, "application/json", "{\"ok\":true}");
            }
        } else if (lstrcmpA(path, "/api/crosshair") == 0) {
            PhoneAct a;
            memset(&a, 0, sizeof a);
            a.action = PH_XH;
            a.xh = g_xh;
            struct { const char *k; int off; } mm[] = {
                { "on", offsetof(XhCfg, on) },
                { "shape", offsetof(XhCfg, shape) },
                { "size", offsetof(XhCfg, size) },
                { "gap", offsetof(XhCfg, gap) },
                { "thick", offsetof(XhCfg, thick) },
                { "opacity", offsetof(XhCfg, opacity) },
                { "outline", offsetof(XhCfg, outline) },
                { "dot", offsetof(XhCfg, dot) },
                { "rotation", offsetof(XhCfg, rotation) },
                { "monitor", offsetof(XhCfg, monitor) },
            };
            int *base = (int *)&a.xh;
            for (unsigned i = 0; i < sizeof mm / sizeof mm[0]; i++)
                if (CxJson_IsNum(j, mm[i].k))
                    base[mm[i].off / 4] = (int)CxJson_GetNum(j, mm[i].k, 0.0);
            CxJson_Free(j);
            queue_act(&a);
            send_http(s, 200, "application/json", "{\"ok\":true}");
        } else if (lstrcmpA(path, "/api/hdr") == 0) {
            PhoneAct a;
            memset(&a, 0, sizeof a);
            a.action = PH_HDR;
            a.a = CxJson_GetBool(j, "on", 0);
            CxJson_Free(j);
            queue_act(&a);
            send_http(s, 200, "application/json", "{\"ok\":true}");
        } else if (lstrcmpA(path, "/api/reset") == 0) {
            CxJson_Free(j);
            PhoneAct a;
            memset(&a, 0, sizeof a);
            a.action = PH_RESET;
            queue_act(&a);
            send_http(s, 200, "application/json", "{\"ok\":true}");
        } else {
            CxJson_Free(j);
            send_http(s, 404, "application/json", "{\"error\":\"not found\"}");
        }
        closesocket(s);
        return;
    }

    send_http(s, 404, "application/json", "{\"error\":\"bad method\"}");
    closesocket(s);
}

static void note_device(const char *ip)
{
    if (!ip[0]) return;
    for (int i = 0; i < g_ndev; i++)
        if (lstrcmpA(g_dev[i], ip) == 0) return;
    if (g_ndev < 4) {
        lstrcpynA(g_dev[g_ndev], ip, 32);
        g_ndev++;
    }
}

static DWORD WINAPI thread_main(LPVOID unused)
{
    (void)unused;
    while (g_running) {
        struct sockaddr_in addr;
        int len = sizeof addr;
        SOCKET c = accept(g_listen, (struct sockaddr *)&addr, &len);
        if (c == INVALID_SOCKET) {
            if (!g_running) break;
            Sleep(50);
            continue;
        }
        char ip[32] = { 0 };
        const char *d = inet_ntoa(addr.sin_addr);
        if (d) lstrcpynA(ip, d, 32);
        note_device(ip);
        handle_conn(c);
    }
    return 0;
}

wchar_t *Phone_Summary(void)
{
    wchar_t *out = (wchar_t *)malloc(96 * sizeof(wchar_t));
    char ip[32] = { 0 };
    char host[64] = { 0 };
    if (gethostname(host, 64) == 0) {
        struct hostent *he = gethostbyname(host);
        if (he && he->h_addr_list[0])
            memcpy(ip, he->h_addr_list[0], 4);
    }
    if (!ip[0]) strcpy(ip, "127.0.0.1");
    wsprintfW(out, L"http://%hs:%d", ip, CX_PORT);
    return out;
}

int Phone_Start(void)
{
    if (g_running) return 1;
    InitializeCriticalSection(&g_cs);
    g_qhead = g_qcount = 0;
    g_ndev = 0;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;

    token_load_or_gen();

    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) return 0;

    int reuse = 1;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof reuse);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(CX_PORT);
    if (bind(g_listen, (struct sockaddr *)&a, sizeof a) == SOCKET_ERROR) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return 0;
    }
    if (listen(g_listen, 8) == SOCKET_ERROR) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return 0;
    }
    DWORD to = 800;
    setsockopt(g_listen, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);

    g_running = 1;
    g_thread = CreateThread(NULL, 0, thread_main, NULL, 0, NULL);
    return g_thread ? 1 : 0;
}

void Phone_Stop(void)
{
    if (!g_running) return;
    g_running = 0;
    if (g_thread) {
        SOCKET t = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (t != INVALID_SOCKET) {
            struct sockaddr_in a;
            memset(&a, 0, sizeof a);
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = INADDR_ANY;
            a.sin_port = htons(CX_PORT);
            (void)connect(t, (struct sockaddr *)&a, sizeof a);
            closesocket(t);
        }
        WaitForSingleObject(g_thread, 1500);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_listen != INVALID_SOCKET) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }
    WSACleanup();
}

int Phone_Running(void) { return g_running; }

int Phone_DeviceCount(void) { return g_ndev; }
int Phone_DeviceIp(int i, wchar_t *out, int sz)
{
    if (i < 0 || i >= g_ndev) return 0;
    MultiByteToWideChar(CP_UTF8, 0, g_dev[i], -1, out, sz);
    return 1;
}
