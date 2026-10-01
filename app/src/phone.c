/* PlexusX — Secure LAN Phone Remote Control.
 *
 * Scope (deliberately tiny, and the whole reason it can be exposed safely):
 *   the server answers GET on a fixed command set that can ONLY move the
 *   color-engine sliders, switch the active game profile, toggle the color
 *   engine and toggle the desktop crosshair.  There is no shell, no file
 *   access, no arbitrary command, no configuration write, no upload.
 *
 * Authentication:
 *   1. the PC shows a random 4-digit pairing PIN;
 *   2. GET /pair?pin=NNNN trades the PIN for a 128-bit random session token;
 *   3. every other request must carry ?t=<token> — the PIN itself is never
 *      accepted again and never appears in the polling traffic;
 *   4. five wrong PINs arm a 30-second lockout (no brute force);
 *   5. tokens expire after 8 hours idle; at most 8 live sessions.
 *
 * A phone never becomes a remote control for the machine: only the enumerated
 * display commands exist, and any other path answers 404.
 */
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include "common.h"

#define PHONE_MAX_SESSIONS 8
#define PHONE_PIN_TRIES    5
#define PHONE_LOCKOUT_MS   30000u
#define PHONE_SESSION_TTL  28800000u   /* 8 h idle */
#define PHONE_REQ_MAX      2048

static HANDLE         g_thread = NULL;
static SOCKET         g_srv = INVALID_SOCKET;
static volatile LONG  g_stop = 0;
static wchar_t        g_url[128];
static Look           g_last_look;
static int            g_have_last = 0;
static int            g_pin = 1337;
static volatile LONG  g_client_count = 0;   /* live connections, for the UI    */

/* ---------------- pairing state ---------------- */
typedef struct PhoneSession {
    char     token[33];
    unsigned long long last_ms;
    unsigned long long born_ms;
    int      used;
} PhoneSession;

static PhoneSession      g_sessions[PHONE_MAX_SESSIONS];
static int               g_fail_count = 0;
static unsigned long long g_lock_until = 0;

static unsigned long long phone_now(void) { return (unsigned long long)GetTickCount64(); }

/* xorshift128+ style generator seeded from several fast, unpredictable sources.
 * Good enough for a LAN pair-token that also requires the on-screen PIN. */
static unsigned long long g_rng_s0 = 0, g_rng_s1 = 0;

static void phone_rng_seed(void)
{
    LARGE_INTEGER pc;
    QueryPerformanceCounter(&pc);
    g_rng_s0 = (unsigned long long)pc.QuadPart ^ ((unsigned long long)GetTickCount64() << 21);
    g_rng_s1 = ((unsigned long long)GetCurrentProcessId() << 32) ^
               ((unsigned long long)(ULONG_PTR)&g_srv) ^ GetCurrentThreadId();
    if (!g_rng_s0) g_rng_s0 = 0x9E3779B97F4A7C15ULL;
    if (!g_rng_s1) g_rng_s1 = 0xBF58476D1CE4E5B9ULL;
}

static unsigned long long phone_rand(void)
{
    unsigned long long x = g_rng_s0, y = g_rng_s1;
    g_rng_s0 = y;
    x ^= x << 23;
    g_rng_s1 = x ^ y ^ (x >> 17) ^ (y >> 26);
    return g_rng_s1 + y;
}

static void phone_hex32(char out[33])
{
    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < 4; i++) {
        unsigned long long v = phone_rand();
        for (int j = 0; j < 8; j++)
            out[i * 8 + j] = hexd[(v >> ((7 - j) * 8)) & 0xF];
    }
    out[32] = 0;
}

static void generate_pin(void)
{
    phone_rng_seed();
    g_pin = 1000 + (int)(phone_rand() % 9000ULL);
}

int  Phone_GetPin(void) { return g_pin; }

void Phone_RegeneratePin(void)
{
    generate_pin();
    memset(g_sessions, 0, sizeof g_sessions);   /* old tokens die with the PIN */
    Eng_Log("phone", "pairing PIN regenerated; all sessions revoked");
}

int  Phone_GetClientCount(void) { return (int)InterlockedCompareExchange(&g_client_count, 0, 0); }
int  Phone_IsRunning(void) { return g_thread != NULL; }

static void build_url(void)
{
    char hn[256] = { 0 };
    gethostname(hn, sizeof hn);
    {
        struct addrinfo hints, *res = NULL;
        char ipsum[64] = "127.0.0.1";
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(hn, NULL, &hints, &res) == 0 && res) {
            for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
                struct sockaddr_in *sa = (struct sockaddr_in *)ai->ai_addr;
                unsigned char *q = (unsigned char *)&sa->sin_addr;
                if (q[0] == 127) continue;
                wsprintfA(ipsum, "%d.%d.%d.%d", q[0], q[1], q[2], q[3]);
                break;
            }
            freeaddrinfo(res);
        }
        {
            wchar_t wip[64];
            MultiByteToWideChar(CP_ACP, 0, ipsum, -1, wip, 64);
            wsprintfW(g_url, L"http://%s:%d", wip, PX_PORT);
        }
    }
}

wchar_t *Phone_SummaryUrl(void) { return g_url; }

/* Kept for main.c: a cheap cache the UI can read without touching the engine. */
void Phone_SetLook(const Look *lk)
{
    if (lk) {
        g_last_look = *lk;
        g_have_last = 1;
    }
}

void Phone_PushProfileChange(const wchar_t *name) { (void)name; }

/* ---------------- sessions ---------------- */
static int phone_session_valid(const char *token)
{
    unsigned long long now = phone_now();
    if (!token || strlen(token) != 32) return 0;
    for (int i = 0; i < PHONE_MAX_SESSIONS; i++) {
        if (!g_sessions[i].used) continue;
        if (strcmp(g_sessions[i].token, token) != 0) continue;
        if (now - g_sessions[i].last_ms > PHONE_SESSION_TTL) {
            g_sessions[i].used = 0;
            return 0;
        }
        g_sessions[i].last_ms = now;
        return 1;
    }
    return 0;
}

static const char *phone_new_session(void)
{
    unsigned long long now = phone_now();
    int slot = -1;
    for (int i = 0; i < PHONE_MAX_SESSIONS; i++) {
        if (!g_sessions[i].used) { slot = i; break; }
        if (slot < 0 || g_sessions[i].last_ms < g_sessions[slot].last_ms) slot = i;
    }
    if (slot < 0) return "";
    phone_hex32(g_sessions[slot].token);
    g_sessions[slot].used = 1;
    g_sessions[slot].born_ms = now;
    g_sessions[slot].last_ms = now;
    return g_sessions[slot].token;
}

static int phone_pair(const char *pin)
{
    static char expected[8];
    unsigned long long now = phone_now();
    if (now < g_lock_until) return -2;                 /* locked out */
    wsprintfA(expected, "%04d", g_pin);
    if (!pin || strcmp(pin, expected) != 0) {
        if (++g_fail_count >= PHONE_PIN_TRIES) {
            g_fail_count = 0;
            g_lock_until = now + PHONE_LOCKOUT_MS;
            Eng_Log("phone", "pairing PIN locked for %u ms after %d wrong tries",
                    (unsigned)PHONE_LOCKOUT_MS, PHONE_PIN_TRIES);
        }
        return -1;
    }
    g_fail_count = 0;
    return 0;
}

/* ---------------- tiny query-string helpers ---------------- */
static int qs_get(const char *req, const char *key, char *out, int cap)
{
    const char *p = req;
    size_t klen = strlen(key);
    out[0] = 0;
    while ((p = strstr(p, key)) != NULL) {
        const char *q = p + klen;
        if (*q == '=') {
            int n = 0;
            q++;
            while (*q && *q != '&' && *q != ' ' && *q != '\r' && *q != '\n' && n + 1 < cap)
                out[n++] = *q++;
            out[n] = 0;
            return 1;
        }
        p += klen;
    }
    return 0;
}

static int qs_int(const char *req, const char *key, int dflt)
{
    char tmp[32];
    if (!qs_get(req, key, tmp, sizeof tmp) || !tmp[0]) return dflt;
    return atoi(tmp);
}

/* ---------------- phone page ---------------- */
static const char PAGE[] =
"<!doctype html><html lang='en'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
"<title>PlexusX Remote</title><style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:#0c0c10;color:#f2f2f8;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;padding:16px;max-width:480px;margin:0 auto;line-height:1.45}"
"header{display:flex;justify-content:space-between;align-items:center;padding-bottom:14px;border-bottom:1px solid #23233a;margin-bottom:16px}"
".brand{font-size:18px;font-weight:800;letter-spacing:1px;color:#c6ff3d}"
".brand span{background:#161622;border:1px solid #2a2a3e;color:#8a8a9a;font-size:11px;font-weight:600;padding:3px 9px;border-radius:6px;margin-left:8px}"
".card{background:#16161e;border:1px solid #24243a;border-radius:14px;padding:16px;margin-bottom:14px}"
".card-title{font-size:12px;font-weight:700;letter-spacing:.08em;color:#7e7e92;text-transform:uppercase;margin-bottom:12px}"
".row{display:flex;justify-content:space-between;font-size:13px;color:#9e9eb0;margin-bottom:6px}"
".val{font-weight:700;color:#f0f0f5;font-variant-numeric:tabular-nums}"
".slider-row{margin-bottom:16px}"
"input[type=range]{width:100%;height:6px;border-radius:3px;background:#242438;accent-color:#c6ff3d;-webkit-appearance:none;outline:none}"
"input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:#c6ff3d;cursor:pointer}"
".btn-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
".btn{background:#1c1c28;border:1px solid #2e2e46;color:#f0f0f5;padding:12px;border-radius:10px;font-size:14px;font-weight:600;text-align:center;cursor:pointer}"
".btn:active{transform:scale(.97)}"
".btn-accent{background:#c6ff3d;color:#0c0c10;border:none;font-weight:800}"
".btn-wide{grid-column:span 2}"
".pill{display:inline-block;font-size:11px;font-weight:700;padding:3px 9px;border-radius:99px;background:#20202e;border:1px solid #2e2e46}"
".ok{color:#4fe378;border-color:#26543a}.warn{color:#ffc43d;border-color:#5a4a1c}.bad{color:#ff5050;border-color:#5a2222}.info{color:#4fe3ff;border-color:#1e4a5a}.dim{color:#9e9eb0}"
"#pinBox{text-align:center;padding:28px 14px}"
"#pinBox input{width:170px;padding:12px;font-size:26px;letter-spacing:8px;text-align:center;background:#1a1a26;border:2px solid #34344a;color:#c6ff3d;border-radius:10px;margin:16px 0;outline:none}"
"select{width:100%;padding:10px;background:#1a1a26;border:1px solid #34344a;color:#f0f0f5;border-radius:10px;font-size:14px}"
"</style></head><body>"
"<div id='pinBox'>"
"  <h2 style='color:#c6ff3d'>PlexusX Remote</h2>"
"  <p style='color:#8e8ea0;font-size:13px'>Enter the 4-digit pairing PIN shown on the PC. The PIN is exchanged for a session; it is never reused.</p>"
"  <input id='pinIn' type='tel' maxlength='4' inputmode='numeric' placeholder='----'>"
"  <br><button class='btn btn-accent' style='width:180px' onclick='pair()'>PAIR</button>"
"  <p id='pinErr' style='color:#ff8080;font-size:12px;margin-top:12px'></p>"
"</div>"
"<div id='app' style='display:none'>"
"  <header><div class='brand'>PLEXUS<b style='color:#fff'>X</b><span>LAN REMOTE</span></div>"
"    <div id='stat' class='pill dim'>CONNECTING</div></header>"
"  <div class='card'>"
"    <div class='card-title'>Pipeline</div>"
"    <div class='row'><span>Color engine</span><span id='engine' class='val'>-</span></div>"
"    <div class='row'><span>Status</span><span id='status' class='val'>-</span></div>"
"    <div class='row'><span>Game</span><span id='game' class='val'>-</span></div>"
"    <div class='row'><span>Profile</span><span id='profile' class='val'>-</span></div>"
"    <div class='row'><span>Display</span><span id='display' class='val'>-</span></div>"
"    <div class='btn-grid' style='margin-top:12px'>"
"      <div class='btn' id='engineBtn' onclick='toggleEngine()'>Engine ON/OFF</div>"
"      <div class='btn' onclick='toggleXh()'>Crosshair</div>"
"    </div>"
"  </div>"
"  <div class='card'>"
"    <div class='card-title'>Color</div>"
"    <div class='slider-row'><div class='row'><span>Saturation</span><span class='val' id='v_sat'>-</span></div><input id='sat' type='range' min='0' max='300'></div>"
"    <div class='slider-row'><div class='row'><span>Vibrance</span><span class='val' id='v_vib'>-</span></div><input id='vib' type='range' min='0' max='300'></div>"
"    <div class='slider-row'><div class='row'><span>Brightness</span><span class='val' id='v_bri'>-</span></div><input id='bri' type='range' min='0' max='200'></div>"
"    <div class='slider-row'><div class='row'><span>Contrast</span><span class='val' id='v_con'>-</span></div><input id='con' type='range' min='0' max='200'></div>"
"    <div class='slider-row'><div class='row'><span>Gamma</span><span class='val' id='v_gam'>-</span></div><input id='gam' type='range' min='40' max='250'></div>"
"    <div class='slider-row'><div class='row'><span>Temperature</span><span class='val' id='v_tem'>-</span></div><input id='tem' type='range' min='3000' max='10000' step='100'></div>"
"  </div>"
"  <div class='card'>"
"    <div class='card-title'>Game profile</div>"
"    <select id='games' onchange='applyProfile()'></select>"
"  </div>"
"  <div class='card'>"
"    <div class='card-title'>Presets</div>"
"    <div class='btn-grid'>"
"      <div class='btn' onclick='preset(\"comp\")'>Competitive</div>"
"      <div class='btn' onclick='preset(\"vib\")'>Max Vibrance</div>"
"      <div class='btn' onclick='preset(\"night\")'>Night Ops</div>"
"      <div class='btn' onclick='preset(\"nat\")'>Natural</div>"
"      <div class='btn btn-wide' onclick='preset(\"reset\")'>Reset All Display Defaults</div>"
"    </div>"
"  </div>"
"</div>"
"<script>"
"let tok=sessionStorage.getItem('px_tok')||'';"
"const ids=['sat','vib','bri','con','gam','tem'];"
"const map={sat:'s',vib:'v',bri:'b',con:'c',gam:'g',tem:'t'};"
"function err(m){document.getElementById('pinErr').textContent=m;}"
"async function pair(){"
"  const v=document.getElementById('pinIn').value.trim();"
"  if(v.length!==4){err('Enter the 4-digit PIN');return;}"
"  try{const r=await fetch('/pair?pin='+encodeURIComponent(v));"
"    const j=await r.json();"
"    if(j.token){tok=j.token;sessionStorage.setItem('px_tok',tok);showApp();}"
"    else if(r.status===429){err('Too many attempts. Wait 30 seconds.');}"
"    else err('Wrong PIN.');"
"  }catch(e){err('Network error: '+e);}"
"}"
"function showApp(){document.getElementById('pinBox').style.display='none';document.getElementById('app').style.display='block';sync();loadGames();setInterval(sync,3000);}"
"async function sync(){"
"  try{"
"    const r=await fetch('/status?t='+tok);"
"    if(r.status!==200){tok='';sessionStorage.removeItem('px_tok');location.reload();return;}"
"    const j=await r.json();"
"    const st=document.getElementById('stat');"
"    st.textContent=j.status;j=j;"
"    st.className='pill '+((j.status==='ACTIVE')?'ok':(j.status==='LIMITED'||j.status==='PASSTHROUGH')?'warn':(j.status==='FAILED')?'bad':'info');"
"    document.getElementById('engine').textContent=j.enabled?'ENABLED':'DISABLED';"
"    document.getElementById('game').textContent=j.game||'Desktop';"
"    document.getElementById('profile').textContent=j.profile||'-';"
"    document.getElementById('display').textContent=j.display||'-';"
"    const vals={sat:j.sat,vib:j.vib,bri:j.bri,con:j.con,gam:j.gamma,tem:j.temp};"
"    for(const k of ids){const el=document.getElementById(k);if(document.activeElement!==el)el.value=vals[k];"
"      document.getElementById('v_'+k).textContent=(k==='gam')?(j.gamma/100).toFixed(2):(k==='tem')?j.temp+'K':vals[k]+'%';}"
"  }catch(e){}"
"}"
"for(const k of ids){const el=document.getElementById(k);"
"  el.oninput=()=>{document.getElementById('v_'+k).textContent=(k==='gam')?(el.value/100).toFixed(2):(k==='tem')?el.value+'K':el.value+'%';};"
"  el.onchange=()=>fetch('/set?t='+tok+'&k='+map[k]+'&v='+el.value);"
"}"
"async function loadGames(){"
"  try{const r=await fetch('/games?t='+tok);const j=await r.json();"
"    const sel=document.getElementById('games');sel.innerHTML='';"
"    for(const g of j.games){const o=document.createElement('option');o.value=g.i;o.textContent=g.name+(g.enabled?'':' (disabled)');if(g.active)o.selected=true;sel.appendChild(o);}"
"  }catch(e){}"
"}"
"function applyProfile(){const i=document.getElementById('games').value;fetch('/profile?t='+tok+'&i='+i);}"
"function toggleEngine(){fetch('/engine?t='+tok+'&on='+(document.getElementById('engine').textContent==='ENABLED'?0:1));}"
"function toggleXh(){fetch('/toggle_xh?t='+tok);}"
"function preset(p){fetch('/preset?t='+tok+'&p='+p);setTimeout(sync,150);}"
"if(tok)showApp();"
"</script></body></html>";

/* ---------------- status JSON (authoritative engine state) ---------------- */
static int phone_status_json(char *out, int cap)
{
    const Look *req = Eng_GetRequested();
    const PxEffectiveState *eff = Eng_Effective();
    const PxGameDisplayState *gs = Prof_GameState();
    Profile *ap = Prof_Get(Prof_ActiveIndex());
    ModeInfo mode;
    MonitorInfo *mi;
    char game[PX_GAME_EXE_LEN + 1];
    int i;

    Modes_Current(&mode);
    mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());
    for (i = 0; i < PX_GAME_EXE_LEN && gs->exe[i]; i++) game[i] = (char)(gs->exe[i] & 0xFF);
    game[i] = 0;

    {
        char pname[96] = "-";
        if (ap) {
            int n = 0;
            for (; ap->name[n] && n < 90; n++) pname[n] = (char)(ap->name[n] & 0xFF);
            pname[n] = 0;
        }
        return _snprintf(out, (size_t)cap,
            "{\"ok\":1,\"enabled\":%d,\"status\":\"%s\",\"reason\":\"%s\","
            "\"sat\":%.1f,\"vib\":%.1f,\"bri\":%.1f,\"con\":%.1f,\"gamma\":%d,\"temp\":%.0f,"
            "\"tint\":%.1f,\"hue\":%.1f,\"clarity\":%.1f,\"shadows\":%.1f,\"highlights\":%.1f,"
            "\"game\":\"%s\",\"detected\":%d,\"profile\":\"%s\",\"profileIndex\":%d,"
            "\"display\":\"%dx%d@%d\",\"monitor\":\"%s\",\"hdr\":\"%s\",\"crosshair\":%d,"
            "\"profiles\":%d,\"gameOutput\":\"%s\"}",
            req->enabled, PxStatus_Id(eff->status), eff->reason,
            req->sat, req->vibrance, req->bri, req->con, (int)(req->gamma * 100 + 0.5f), req->temp,
            req->tint, req->hue, req->clarity, req->shadows, req->highlights,
            game, gs->detected, pname, Prof_ActiveIndex(),
            mode.w, mode.h, mode.hz, mi ? "primary-or-selected" : "-",
            PxHdr_Name(PxHdr_StateOf(mi)), Xh_IsActive() ? 1 : 0,
            Prof_Count(), px_gameout_name(gs->game_output));
    }
}

static int phone_games_json(char *out, int cap)
{
    int o = 0, n = Prof_Count();
    o += _snprintf(out + o, (size_t)(cap - o), "{\"ok\":1,\"active\":%d,\"games\":[", Prof_ActiveIndex());
    for (int i = 0; i < n && o < cap - 160; i++) {
        Profile *p = Prof_Get(i);
        char nm[64];
        int k = 0;
        if (!p) continue;
        for (; p->name[k] && k < 60; k++) nm[k] = (char)(p->name[k] & 0xFF);
        nm[k] = 0;
        o += _snprintf(out + o, (size_t)(cap - o), "%s{\"i\":%d,\"name\":\"%s\",\"enabled\":%d,\"active\":%d}",
                       i ? "," : "", i, nm, p->enabled ? 1 : 0, i == Prof_ActiveIndex() ? 1 : 0);
    }
    o += _snprintf(out + o, (size_t)(cap - o), "]}");
    return o;
}

/* ---------------- request handling ---------------- */
static void handle_client(SOCKET c)
{
    char *req = (char *)malloc(PHONE_REQ_MAX);
    const char *body = PAGE;
    int blen;
    const char *ctype = "text/html; charset=utf-8";
    int status_code = 200;
    char json[1024];
    char token[64];
    char pinbuf[16];
    int authorised = 0;

    InterlockedIncrement(&g_client_count);
    if (!req) { closesocket(c); InterlockedDecrement(&g_client_count); return; }
    {
        int r = recv(c, req, PHONE_REQ_MAX - 1, 0);
        if (r <= 0) { free(req); closesocket(c); InterlockedDecrement(&g_client_count); return; }
        req[r] = 0;
    }

    token[0] = 0;
    qs_get(req, "t", token, sizeof token);
    authorised = phone_session_valid(token);

    if (strncmp(req, "GET /pair", 9) == 0) {
        qs_get(req, "pin", pinbuf, sizeof pinbuf);
        {
            int pr = phone_pair(pinbuf);
            if (pr == -2) {
                body = "{\"error\":\"locked\",\"retry_ms\":30000}";
                status_code = 429;
            } else if (pr == -1) {
                body = "{\"error\":\"bad_pin\"}";
                status_code = 401;
            } else {
                const char *tk = phone_new_session();
                blen = _snprintf(json, sizeof json, "{\"ok\":1,\"token\":\"%s\",\"ttl_ms\":%u}", tk, PHONE_SESSION_TTL);
                body = json;
                status_code = 200;
                Eng_Log("phone", "device paired");
            }
        }
        ctype = "application/json";
    } else if (!authorised) {
        body = "{\"error\":\"unauthorized\"}";
        status_code = 401;
        ctype = "application/json";
    } else if (strncmp(req, "GET /status", 11) == 0) {
        phone_status_json(json, sizeof json);
        body = json;
        ctype = "application/json";
    } else if (strncmp(req, "GET /games", 10) == 0) {
        phone_games_json(json, sizeof json);
        body = json;
        ctype = "application/json";
    } else if (strncmp(req, "GET /set?", 9) == 0) {
        char key[8];
        int v = qs_int(req, "v", 0);
        qs_get(req, "k", key, sizeof key);
        if (key[0] && key[1] == 0) {
            char k = key[0];
            if (k == 's' || k == 'v' || k == 'b' || k == 'c' || k == 'g' || k == 't' ||
                k == 'n' || k == 'h') {
                PostMessageW(g_hwnd, WM_APP_LOOK, (WPARAM)(unsigned char)k, (LPARAM)v);
                body = "{\"ok\":1}";
            } else {
                body = "{\"error\":\"unknown_key\"}";
                status_code = 400;
            }
        } else {
            body = "{\"error\":\"bad_request\"}";
            status_code = 400;
        }
        ctype = "application/json";
    } else if (strncmp(req, "GET /profile", 12) == 0) {
        int i = qs_int(req, "i", -1);
        if (i >= 0 && i < Prof_Count()) {
            PostMessageW(g_hwnd, WM_APP_PROFILE, (WPARAM)i, 0);
            body = "{\"ok\":1}";
        } else {
            body = "{\"error\":\"bad_profile\"}";
            status_code = 400;
        }
        ctype = "application/json";
    } else if (strncmp(req, "GET /engine", 11) == 0) {
        int on = qs_int(req, "on", -1);
        if (on == 0 || on == 1) {
            PostMessageW(g_hwnd, WM_APP_LOOK, (WPARAM)(unsigned char)'x', (LPARAM)on);
            body = "{\"ok\":1}";
        } else {
            body = "{\"error\":\"bad_request\"}";
            status_code = 400;
        }
        ctype = "application/json";
    } else if (strncmp(req, "GET /preset", 11) == 0) {
        char p[16];
        qs_get(req, "p", p, sizeof p);
        if (strcmp(p, "comp") == 0) PostMessageW(g_hwnd, WM_COMMAND, ID_B_HOME_COMPETITIVE, 0);
        else if (strcmp(p, "vib") == 0) PostMessageW(g_hwnd, WM_COMMAND, ID_B_HOME_MAX_VIB, 0);
        else if (strcmp(p, "night") == 0) PostMessageW(g_hwnd, WM_COMMAND, ID_B_HOME_NIGHT_VIS, 0);
        else if (strcmp(p, "nat") == 0) PostMessageW(g_hwnd, WM_COMMAND, ID_B_HOME_NATURAL, 0);
        else if (strcmp(p, "reset") == 0) PostMessageW(g_hwnd, WM_COMMAND, ID_B_RESET_COLOR, 0);
        else {
            body = "{\"error\":\"unknown_preset\"}";
            status_code = 400;
            ctype = "application/json";
            goto respond;
        }
        body = "{\"ok\":1}";
        ctype = "application/json";
    } else if (strncmp(req, "GET /toggle_xh", 14) == 0) {
        PostMessageW(g_hwnd, WM_COMMAND, ID_B_XH_TOGGLE_REMOTE, 0);
        body = "{\"ok\":1}";
        ctype = "application/json";
    } else {
        body = "{\"error\":\"not_found\"}";
        status_code = 404;
        ctype = "application/json";
    }

respond:
    blen = (int)strlen(body);
    {
        char hdr[256];
        _snprintf(hdr, sizeof hdr,
                  "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                  "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                  "Referrer-Policy: no-referrer\r\nConnection: close\r\n\r\n",
                  status_code,
                  status_code == 200 ? "OK" : status_code == 401 ? "Unauthorized"
                                    : status_code == 429 ? "Too Many Requests"
                                    : status_code == 404 ? "Not Found" : "Bad Request",
                  ctype, blen);
        send(c, hdr, (int)strlen(hdr), 0);
    }
    send(c, body, blen, 0);
    free(req);
    closesocket(c);
    InterlockedDecrement(&g_client_count);
}

static DWORD WINAPI server_thread(LPVOID arg)
{
    (void)arg;
    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        fd_set rf;
        struct timeval tv = { 0, 250000 };
        FD_ZERO(&rf);
        FD_SET(g_srv, &rf);
        if (select(0, &rf, NULL, NULL, &tv) <= 0) continue;
        {
            SOCKET c = accept(g_srv, NULL, NULL);
            if (c == INVALID_SOCKET) continue;
            {
                struct timeval rt = { 2, 0 };
                setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&rt, sizeof rt);
            }
            handle_client(c);
        }
    }
    return 0;
}

int Phone_Start(void)
{
    if (g_thread) return 0;
    generate_pin();
    phone_rng_seed();

    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa)) return -1;
    }

    g_srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_srv == INVALID_SOCKET) { WSACleanup(); return -1; }

    {
        BOOL yes = 1;
        struct sockaddr_in a;
        setsockopt(g_srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);   /* LAN only; never routed publicly */
        a.sin_port = htons(PX_PORT);
        if (bind(g_srv, (struct sockaddr *)&a, sizeof a) == SOCKET_ERROR ||
            listen(g_srv, 4) == SOCKET_ERROR) {
            closesocket(g_srv);
            g_srv = INVALID_SOCKET;
            WSACleanup();
            return -1;
        }
    }

    g_stop = 0;
    build_url();
    g_thread = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    if (!g_thread) {
        closesocket(g_srv);
        g_srv = INVALID_SOCKET;
        WSACleanup();
        return -1;
    }
    Eng_Log("phone", "LAN remote listening on port %d", PX_PORT);
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
    memset(g_sessions, 0, sizeof g_sessions);
    WSACleanup();
}
