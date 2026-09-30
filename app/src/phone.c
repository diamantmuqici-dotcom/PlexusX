/* PlexusX — Secure LAN Phone Remote Control
 * Local Wi-Fi only, random pairing PIN, zero cloud dependencies.
 */
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include "common.h"

static HANDLE         g_thread = NULL;
static SOCKET         g_srv = INVALID_SOCKET;
static volatile LONG  g_stop = 0;
static wchar_t        g_url[128];
static Look           g_last_look;
static int            g_have_last = 0;
static int            g_pin = 1337;
static int            g_client_count = 0;

static void generate_pin(void)
{
    /* Generate 4-digit PIN between 1000 and 9999 */
    LARGE_INTEGER pc;
    QueryPerformanceCounter(&pc);
    srand((unsigned int)(pc.LowPart ^ GetCurrentProcessId()));
    g_pin = 1000 + (rand() % 9000);
}

int  Phone_GetPin(void) { return g_pin; }
void Phone_RegeneratePin(void) { generate_pin(); }
int  Phone_GetClientCount(void) { return g_client_count; }
int  Phone_IsRunning(void) { return g_thread != NULL; }

static void build_url(void)
{
    char hn[256] = { 0 };
    gethostname(hn, sizeof hn);
    struct hostent *he = gethostbyname(hn);
    char ip[64] = "127.0.0.1";
    if (he) {
        for (char **a = he->h_addr_list; *a; a++) {
            unsigned char *q = (unsigned char *)*a;
            if (q[0] == 127) continue;
            wsprintfA(ip, "%d.%d.%d.%d", q[0], q[1], q[2], q[3]);
            break;
        }
    }
    wchar_t wip[64];
    MultiByteToWideChar(CP_ACP, 0, ip, -1, wip, 64);
    wsprintfW(g_url, L"http://%s:%d", wip, PX_PORT);
}

wchar_t *Phone_SummaryUrl(void) { return g_url; }

void Phone_SetLook(const Look *lk)
{
    if (lk) {
        g_last_look = *lk;
        g_have_last = 1;
    }
}

void Phone_PushProfileChange(const wchar_t *name)
{
    (void)name;
}

/* ---------------- Phone Remote HTML/CSS/JS Page ---------------- */
static const char PAGE[] =
"<!doctype html><html lang='en'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
"<title>PlexusX Mobile Remote</title><style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:#0a0a0f;color:#f0f0f5;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;padding:16px;max-width:480px;margin:0 auto;line-height:1.4}"
"header{display:flex;justify-content:space-between;align-items:center;padding-bottom:14px;border-bottom:1px solid #232330;margin-bottom:16px}"
".brand{font-size:18px;font-weight:800;letter-spacing:1px;color:#c6ff3d;display:flex;align-items:center;gap:8px}"
".brand span{background:#161622;border:1px solid #2a2a3e;color:#8a8a9a;font-size:11px;font-weight:600;padding:2px 8px;border-radius:6px}"
".card{background:#14141e;border:1px solid #242434;border-radius:14px;padding:16px;margin-bottom:14px}"
".card-title{font-size:12px;font-weight:700;letter-spacing:0.08em;color:#7e7e92;text-transform:uppercase;margin-bottom:12px}"
".slider-row{margin-bottom:16px}"
".slider-head{display:flex;justify-content:space-between;font-size:13px;color:#9e9eb0;margin-bottom:6px}"
".slider-val{font-weight:700;color:#f0f0f5;font-variant-numeric:tabular-nums}"
"input[type=range]{width:100%;height:6px;border-radius:3px;background:#242434;accent-color:#c6ff3d;-webkit-appearance:none;outline:none}"
"input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:#c6ff3d;cursor:pointer}"
".btn-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px}"
".btn{background:#1a1a26;border:1px solid #2e2e42;color:#f0f0f5;padding:12px;border-radius:10px;font-size:14px;font-weight:600;text-align:center;cursor:pointer;transition:all 0.15s}"
".btn:active{transform:scale(0.97)}"
".btn-accent{background:#c6ff3d;color:#0a0a0f;border:none;font-weight:700}"
".btn-wide{grid-column:span 2}"
"#authBox{text-align:center;padding:32px 16px}"
"#authBox input{width:160px;padding:12px;font-size:24px;letter-spacing:6px;text-align:center;background:#1a1a26;border:2px solid #34344a;color:#c6ff3d;border-radius:10px;margin:16px 0;outline:none}"
"#authBox input:focus{border-color:#c6ff3d}"
".badge-active{color:#c6ff3d;font-size:12px;font-weight:700}"
"</style></head><body>"
"<div id='authBox'>"
"  <h2 style='color:#c6ff3d;margin-bottom:6px'>PlexusX Remote</h2>"
"  <p style='color:#8e8ea0;font-size:13px'>Enter 4-digit pairing PIN shown on your PC:</p>"
"  <input id='pinIn' type='text' maxlength='4' placeholder='••••' autofocus pattern='[0-9]*'>"
"  <br><button class='btn btn-accent' style='width:160px' onclick='tryAuth()'>CONNECT</button>"
"</div>"
"<div id='mainApp' style='display:none'>"
"  <header>"
"    <div class='brand'>PLEXUS<b>X</b> <span>LAN REMOTE</span></div>"
"    <div id='stat' class='badge-active'>● CONNECTED</div>"
"  </header>"
"  <div class='card'>"
"    <div class='card-title'>Display Color Engine</div>"
"    <div class='slider-row'>"
"      <div class='slider-head'><span>Saturation</span><span id='vsat' class='slider-val'>150%</span></div>"
"      <input id='sat' type='range' min='0' max='300' value='150'>"
"    </div>"
"    <div class='slider-row'>"
"      <div class='slider-head'><span>Vibrance</span><span id='vvib' class='slider-val'>120%</span></div>"
"      <input id='vib' type='range' min='0' max='300' value='120'>"
"    </div>"
"    <div class='slider-row'>"
"      <div class='slider-head'><span>Brightness</span><span id='vbri' class='slider-val'>100%</span></div>"
"      <input id='bri' type='range' min='0' max='200' value='100'>"
"    </div>"
"    <div class='slider-row'>"
"      <div class='slider-head'><span>Contrast</span><span id='vcon' class='slider-val'>100%</span></div>"
"      <input id='con' type='range' min='0' max='200' value='100'>"
"    </div>"
"    <div class='slider-row'>"
"      <div class='slider-head'><span>GPU Gamma</span><span id='vgam' class='slider-val'>1.00</span></div>"
"      <input id='gamma' type='range' min='40' max='250' value='100'>"
"    </div>"
"    <div class='slider-row'>"
"      <div class='slider-head'><span>Temperature</span><span id='vtemp' class='slider-val'>6500K</span></div>"
"      <input id='temp' type='range' min='3000' max='10000' value='6500' step='100'>"
"    </div>"
"  </div>"
"  <div class='card'>"
"    <div class='card-title'>Quick Presets</div>"
"    <div class='btn-grid'>"
"      <div class='btn' onclick='preset(\"comp\")'>Competitive</div>"
"      <div class='btn' onclick='preset(\"vib\")'>Ultra 300%</div>"
"      <div class='btn' onclick='preset(\"night\")'>Night Ops</div>"
"      <div class='btn' onclick='preset(\"nat\")'>Natural</div>"
"      <div class='btn btn-accent btn-wide' onclick='preset(\"reset\")'>Reset All Display Defaults</div>"
"    </div>"
"  </div>"
"  <div class='card'>"
"    <div class='card-title'>Tools</div>"
"    <div class='btn-grid'>"
"      <div class='btn btn-wide' id='xhBtn' onclick='toggleXh()'>Toggle Crosshair Overlay</div>"
"    </div>"
"  </div>"
"</div>"
"<script>"
"let gPin=localStorage.getItem('px_pin')||'';"
"const ids=['sat','vib','bri','con','gamma','temp'];"
"function tryAuth(){"
"  const v=document.getElementById('pinIn').value;"
"  if(v.length===4){gPin=v;localStorage.setItem('px_pin',v);checkAuth();}"
"}"
"async function checkAuth(){"
"  try{"
"    const r=await fetch('/status?pin='+gPin);"
"    if(r.status===200){"
"      document.getElementById('authBox').style.display='none';"
"      document.getElementById('mainApp').style.display='block';"
"      sync();"
"    }else{alert('Invalid PIN code. Check PC screen.');}"
"  }catch(e){console.error(e);}"
"}"
"if(gPin)checkAuth();"
"async function sync(){"
"  try{"
"    const j=await (await fetch('/status?pin='+gPin)).json();"
"    for(const k of ids){"
"      if(j[k]!==undefined){"
"        const el=document.getElementById(k);"
"        el.value=j[k];"
"        let disp=j[k]+'%';"
"        if(k==='gamma')disp=(j.gamma/100).toFixed(2);"
"        if(k==='temp')disp=j.temp+'K';"
"        document.getElementById('v'+k.slice(0,3)).textContent=disp;"
"      }"
"    }"
"  }catch(e){}"
"}"
"for(const k of ids){"
"  const el=document.getElementById(k);"
"  el.oninput=()=>{"
"    let disp=el.value+'%';"
"    if(k==='gamma')disp=(el.value/100).toFixed(2);"
"    if(k==='temp')disp=el.value+'K';"
"    document.getElementById('v'+k.slice(0,3)).textContent=disp;"
"    fetch('/set?pin='+gPin+'&k='+k+'&v='+el.value);"
"  };"
"}"
"function preset(p){fetch('/preset?pin='+gPin+'&p='+p);setTimeout(sync,100);}"
"function toggleXh(){fetch('/toggle_xh?pin='+gPin);}"
"setInterval(sync,3000);"
"</script></body></html>";

static void handle_client(SOCKET c)
{
    char req[2048];
    int r = recv(c, req, sizeof req - 1, 0);
    if (r <= 0) { closesocket(c); return; }
    req[r] = 0;

    const char *body = PAGE;
    int blen = (int)sizeof PAGE - 1;
    const char *ctype = "text/html; charset=utf-8";
    int status_code = 200;
    char json[512];

    /* PIN verification */
    char pin_str[16] = { 0 };
    char expected_pin[16];
    wsprintfA(expected_pin, "%04d", g_pin);
    const char *p_pin = strstr(req, "pin=");
    if (p_pin) sscanf(p_pin, "pin=%15[^& \r\n]", pin_str);

    int auth_ok = (strcmp(pin_str, expected_pin) == 0);

    if (strncmp(req, "GET /status", 11) == 0) {
        if (!auth_ok) {
            body = "{\"error\":\"unauthorized\"}";
            blen = (int)strlen(body);
            ctype = "application/json";
            status_code = 401;
        } else {
            if (!g_have_last) {
                g_last_look.enabled = 1;
                g_last_look.sat = 150;
                g_last_look.vibrance = 120;
                g_last_look.bri = 100;
                g_last_look.con = 100;
                g_last_look.gamma = 1.0f;
                g_last_look.temp = 6500;
            }
            blen = wsprintfA(json,
                "{\"sat\":%d,\"vib\":%d,\"bri\":%d,\"con\":%d,\"gamma\":%d,\"temp\":%d,\"enabled\":%d}",
                (int)g_last_look.sat, (int)g_last_look.vibrance,
                (int)g_last_look.bri, (int)g_last_look.con,
                (int)(g_last_look.gamma * 100), (int)g_last_look.temp,
                g_last_look.enabled ? 1 : 0);
            body = json;
            ctype = "application/json";
        }
    } else if (strncmp(req, "GET /set?", 9) == 0) {
        if (!auth_ok) {
            body = "{\"error\":\"unauthorized\"}";
            blen = (int)strlen(body);
            ctype = "application/json";
            status_code = 401;
        } else {
            char k[16] = { 0 };
            double v = 0;
            const char *q = strstr(req, "k=");
            if (q && sscanf(q, "k=%15[^& ]&v=%lf", k, &v) == 2) {
                PostMessageW(g_hwnd, WM_APP_LOOK, (WPARAM)(unsigned char)k[0], (LPARAM)(int)v);
            }
            body = "{\"ok\":1}";
            blen = 8;
            ctype = "application/json";
        }
    } else if (strncmp(req, "GET /preset?", 12) == 0) {
        if (auth_ok) {
            char p[16] = { 0 };
            const char *qp = strstr(req, "p=");
            if (qp && sscanf(qp, "p=%15[^& ]", p) == 1) {
                if (strcmp(p, "comp") == 0) PostMessageW(g_hwnd, WM_COMMAND, ID_B_HOME_COMPETITIVE, 0);
                else if (strcmp(p, "vib") == 0) PostMessageW(g_hwnd, WM_COMMAND, ID_B_HOME_MAX_VIB, 0);
                else if (strcmp(p, "night") == 0) PostMessageW(g_hwnd, WM_COMMAND, ID_B_HOME_NIGHT_VIS, 0);
                else if (strcmp(p, "nat") == 0) PostMessageW(g_hwnd, WM_COMMAND, ID_B_HOME_NATURAL, 0);
                else if (strcmp(p, "reset") == 0) PostMessageW(g_hwnd, WM_COMMAND, ID_B_RESET_COLOR, 0);
            }
        }
        body = "{\"ok\":1}"; blen = 8; ctype = "application/json";
    } else if (strncmp(req, "GET /toggle_xh", 14) == 0) {
        if (auth_ok) Xh_Toggle();
        body = "{\"ok\":1}"; blen = 8; ctype = "application/json";
    }

    char hdr[256];
    wsprintfA(hdr,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Connection: close\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
        status_code, (status_code == 200 ? "OK" : "Unauthorized"), ctype, blen);
    send(c, hdr, (int)strlen(hdr), 0);
    send(c, body, blen, 0);
    closesocket(c);
}

static DWORD WINAPI server_thread(LPVOID arg)
{
    (void)arg;
    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(g_srv, &rf);
        struct timeval tv = { 1, 0 };
        int sel = select(0, &rf, NULL, NULL, &tv);
        if (sel <= 0) continue;

        SOCKET c = accept(g_srv, NULL, NULL);
        if (c == INVALID_SOCKET) continue;

        g_client_count = 1;
        struct timeval rt = { 2, 0 };
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&rt, sizeof rt);
        handle_client(c);
    }
    return 0;
}

int Phone_Start(void)
{
    if (g_thread) return 0;
    generate_pin();

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
    a.sin_port = htons(PX_PORT);

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
    g_client_count = 0;
    WSACleanup();
}
