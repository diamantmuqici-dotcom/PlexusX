/* PlexusX — New Systems Test Suite v2.2
 *
 * Tests for the premium edition new modules:
 *   core/version.h
 *   core/runtime_status.h
 *   core/app_state.h
 *   core/event_bus.h
 *   security/security.h
 *
 * Builds: gcc -std=c11 -Wall -Werror -O2 -o /tmp/test_new tests/test_new_systems.c app/src/games/game_profile.c -Iapp/src -lm && /tmp/test_new
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../app/src/core/version.h"
#include "../app/src/core/runtime_status.h"
#include "../app/src/core/app_state.h"
#include "../app/src/core/event_bus.h"
#include "../app/src/security/security.h"
#include "../app/src/color/color_runtime_state.h"
#include "../app/src/display/display_capabilities.h"
#include "../app/src/display/display_state.h"
#include "../app/src/color/applied_color_state.h"
#include "../app/src/diagnostics/diagnostics.h"

static int g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { fprintf(stderr, "\n[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } } while(0)
#define CHECK_NEAR(a,b,eps) do { double a__=(double)(a), b__=(double)(b); g_checks++; if (!(fabs(a__-b__) > (double)(eps))) {} else { fprintf(stderr, "\n[FAIL] %s:%d: %s ~= %s (%.9g vs %.9g)\n", __FILE__, __LINE__, #a, #b, a__, b__, (double)(eps)); exit(1);} } while(0)

/* ---------------- version ---------------- */
static void test_version(void)
{
    CHECK(PX_VERSION_MAJOR == 2);
    CHECK(PX_VERSION_MINOR == 2);
    CHECK(PX_VERSION_PATCH == 0);
    CHECK(strcmp(PX_VERSION_STRING, "2.2.0") == 0);
    CHECK(px_version_cmp(2,2,0, 2,1,9) > 0);
    CHECK(px_version_cmp(2,2,0, 2,2,0) == 0);
    CHECK(px_version_cmp(1,9,9, 2,0,0) < 0);
    CHECK(px_version_is_at_least(2,0,0) == 1);
    CHECK(px_version_is_at_least(2,3,0) == 0);
    CHECK(px_version_is_at_least(2,2,0) == 1);
    printf("  [PASS] Version centralized, semver compare\n");
}

/* ---------------- runtime status 10-state ---------------- */
static void test_runtime_status(void)
{
    /* Names */
    CHECK(strcmp(PxRt_Name(PX_RT_READY), "READY") == 0);
    CHECK(strcmp(PxRt_Name(PX_RT_APPLYING), "APPLYING") == 0);
    CHECK(strcmp(PxRt_Name(PX_RT_APPLIED), "APPLIED") == 0);
    CHECK(strcmp(PxRt_Name(PX_RT_LIMITED), "LIMITED") == 0);
    CHECK(strcmp(PxRt_Name(PX_RT_HDR), "HDR") == 0);
    CHECK(strcmp(PxRt_Name(PX_RT_EXCLUSIVE_FULLSCREEN), "EXCLUSIVE_FULLSCREEN") == 0);
    CHECK(strcmp(PxRt_Name(PX_RT_UNSUPPORTED), "UNSUPPORTED") == 0);
    CHECK(strcmp(PxRt_Name(PX_RT_ERROR), "ERROR") == 0);
    CHECK(strcmp(PxRt_Name(PX_RT_RESTORING), "RESTORING") == 0);
    CHECK(strcmp(PxRt_Name(PX_RT_SAFE_MODE), "SAFE_MODE") == 0);

    /* Short labels */
    CHECK(PxRt_ShortLabel(PX_RT_APPLIED)[0] != 0);
    CHECK(PxRt_ShortLabel(PX_RT_HDR)[0] != 0);

    /* Explain + Hint non-empty for all 10 */
    for (int i = 0; i < PX_RT_COUNT; i++) {
        CHECK(PxRt_Explain((PxRuntimeStatus)i)[0] != 0);
        CHECK(PxRt_Hint((PxRuntimeStatus)i)[0] != 0);
    }

    /* Tone + Severity */
    CHECK(PxRt_Tone(PX_RT_APPLIED) == 1);
    CHECK(PxRt_Tone(PX_RT_ERROR) == 3);
    CHECK(PxRt_Tone(PX_RT_HDR) == 2);
    CHECK(PxRt_Severity(PX_RT_APPLIED) == PX_RT_SEV_OK);
    CHECK(PxRt_Severity(PX_RT_ERROR) == PX_RT_SEV_ERROR);

    /* Derivation priority: safe_mode > restoring > error > hdr > exclusive > limited > unsupported > applying > applied > ready */
    {
        PxEffectiveState eff;
        AppliedColorState app;
        MonitorInfo mon;
        PxRuntimeState rt;
        PxRuntimeInput in;
        Look lk = LOOK_NEUTRAL_INIT;
        memset(&eff, 0, sizeof eff);
        memset(&app, 0, sizeof app);
        memset(&mon, 0, sizeof mon);
        memset(&in, 0, sizeof in);
        PxAS_Init(&app);
        eff.status = PX_STATUS_ACTIVE;
        eff.engine_enabled = 1;
        eff.linear_deliverable = 1;
        eff.curves_deliverable = 1;
        eff.linear_live = 1;
        eff.curves_live = 1;
        eff.requested = lk;
        eff.effective = lk;
        eff.applied = lk;

        in.eff = &eff;
        in.applied = &app;
        in.monitor = &mon;
        in.hdr_state = PX_HDR_DISABLED;
        in.pres_mode = PX_PRES_WINDOWED;
        in.game_active = 0;
        in.restoring = 0;
        in.safe_mode = 0;
        in.mag_available = 1;
        in.gamma_available = 1;

        PxRuntime_Derive(&in, &rt);
        CHECK(rt.status == PX_RT_APPLIED);

        /* HDR takes precedence over applied */
        in.hdr_state = PX_HDR_ENABLED;
        PxRuntime_Derive(&in, &rt);
        CHECK(rt.status == PX_RT_HDR);
        in.hdr_state = PX_HDR_DISABLED;

        /* Exclusive fullscreen with game */
        in.pres_mode = PX_PRES_FULLSCREEN_SURFACE;
        in.game_active = 1;
        PxRuntime_Derive(&in, &rt);
        CHECK(rt.status == PX_RT_EXCLUSIVE_FULLSCREEN);
        in.pres_mode = PX_PRES_WINDOWED;
        in.game_active = 0;

        /* Error */
        eff.status = PX_STATUS_FAILED;
        PxRuntime_Derive(&in, &rt);
        CHECK(rt.status == PX_RT_ERROR);
        eff.status = PX_STATUS_ACTIVE;

        /* Restoring */
        in.restoring = 1;
        PxRuntime_Derive(&in, &rt);
        CHECK(rt.status == PX_RT_RESTORING);
        in.restoring = 0;

        /* Safe mode top priority */
        in.safe_mode = 1;
        in.restoring = 1;
        eff.status = PX_STATUS_FAILED;
        in.hdr_state = PX_HDR_ENABLED;
        PxRuntime_Derive(&in, &rt);
        CHECK(rt.status == PX_RT_SAFE_MODE);
        in.safe_mode = 0;
        in.restoring = 0;
        eff.status = PX_STATUS_ACTIVE;
        in.hdr_state = PX_HDR_DISABLED;

        /* Unsupported */
        in.mag_available = 0;
        in.gamma_available = 0;
        eff.status = PX_STATUS_DISABLED;
        PxRuntime_Derive(&in, &rt);
        CHECK(rt.status == PX_RT_UNSUPPORTED);
    }

    /* JSON */
    {
        PxRuntimeState rt;
        char buf[1024];
        memset(&rt, 0, sizeof rt);
        rt.status = PX_RT_APPLIED;
        rt.reason = "test";
        rt.hint = "hint";
        rt.tone = 1;
        int n = PxRt_ToJson(&rt, buf, sizeof buf);
        CHECK(n > 0);
        CHECK(strstr(buf, "APPLIED") != NULL);
    }

    printf("  [PASS] Runtime status 10-state derivation priority + JSON\n");
}

/* ---------------- app state ---------------- */
static void test_app_state(void)
{
    PxAppState s;
    PxAppState_Init(&s);
    CHECK(s.phase == PX_APP_BOOT);
    CHECK(strcmp(s.version, "2.2.0") == 0);
    CHECK(s.flags == 0);
    CHECK(PxAppState_HasTelemetry(&s) == 0);
    CHECK(PxAppState_TelemetryPolicy()[0] != 0);

    PxAppState_SetPhase(&s, PX_APP_LOADING, 100);
    CHECK(s.phase == PX_APP_LOADING);
    CHECK(s.log.count > 0);

    PxAppState_SetFlag(&s, PX_APP_FLAG_FIRST_RUN, 1, 200);
    CHECK(PxAppState_HasFlag(&s, PX_APP_FLAG_FIRST_RUN) == 1);
    PxAppState_SetFlag(&s, PX_APP_FLAG_FIRST_RUN, 0, 300);
    CHECK(PxAppState_HasFlag(&s, PX_APP_FLAG_FIRST_RUN) == 0);

    PxAppState_OnApply(&s, 500);
    CHECK(s.last_apply_ms == 500);
    CHECK(s.reassert_count == 1);

    CHECK(PxAppState_IsReady(&s) == 0);
    PxAppState_SetPhase(&s, PX_APP_READY, 600);
    CHECK(PxAppState_IsReady(&s) == 1);

    PxAppState_SetPhase(&s, PX_APP_SAFE_MODE, 700);
    CHECK(PxAppState_IsSafeMode(&s) == 1);

    printf("  [PASS] App state phase machine + flags + telemetry assertion\n");
}

/* ---------------- event bus ---------------- */
static int g_ev_count = 0;
static PxEventType g_last_type = PX_EV_NONE;
static void test_handler(const PxEvent *e, void *ud)
{
    (void)ud;
    g_ev_count++;
    g_last_type = e->type;
}

static void test_event_bus(void)
{
    PxEventBus bus;
    PxEventBus_Init(&bus);
    CHECK(bus.handler_count == 0);
    CHECK(bus.emitted == 0);

    PxEventBus_Subscribe(&bus, PX_EV_COLOR_REQUESTED, test_handler, NULL);
    CHECK(bus.handler_count == 1);

    PxEventBus_EmitSimple(&bus, PX_EV_COLOR_REQUESTED, 100, "slider");
    CHECK(g_ev_count == 1);
    CHECK(g_last_type == PX_EV_COLOR_REQUESTED);
    CHECK(bus.emitted == 1);
    CHECK(bus.history.count == 1);

    /* Any handler */
    g_ev_count = 0;
    PxEventBus_Subscribe(&bus, PX_EV_NONE, test_handler, NULL);
    PxEventBus_EmitSimple(&bus, PX_EV_GAME_ENTER, 200, "rust");
    CHECK(g_ev_count == 1); /* only ANY handler */
    CHECK(bus.history.count == 2);

    /* History ring */
    for (int i = 0; i < 70; i++) {
        PxEventBus_EmitSimple(&bus, PX_EV_DISPLAY_CHANGED, 300 + i, "test");
    }
    CHECK(bus.history.count == PX_EVENT_HISTORY_CAP);
    CHECK(bus.history.dropped > 0);

    /* Event name */
    CHECK(strcmp(PxEvent_Name(PX_EV_SAFE_RESET), "SAFE_RESET") == 0);
    CHECK(strcmp(PxEvent_Name(PX_EV_COUNT), "UNKNOWN") == 0);

    printf("  [PASS] Event bus subscribe/emit/history/any-filter\n");
}

/* ---------------- security ---------------- */
static void test_security(void)
{
    /* Finite */
    CHECK(px_sec_is_finite_d(1.0) == 1);
    CHECK(px_sec_is_finite_d(0.0/0.0) == 0);
    CHECK(px_sec_is_finite_d(1.0/0.0) == 0);
    CHECK(px_sec_is_finite_f(1.0f) == 1);
    CHECK(px_sec_is_finite_f(NAN) == 0);

    /* Clamp finite */
    CHECK(px_sec_clampf_finite(150.0f, 0, 100, 50) == 100.0f);
    CHECK(px_sec_clampf_finite(NAN, 0, 100, 50) == 50.0f);
    CHECK(px_sec_clampd_finite(1e31, 0, 100, 50) == 50.0);

    /* strlcpy */
    {
        char dst[8];
        size_t n = px_sec_strlcpy(dst, "hello world", sizeof dst);
        CHECK(n == 11);
        CHECK(strcmp(dst, "hello w") == 0);
        CHECK(dst[7] == 0);
    }

    /* Path safe */
    CHECK(px_sec_path_is_safe("config.ini") == 1);
    CHECK(px_sec_path_is_safe("../etc/passwd") == 0);
    CHECK(px_sec_path_is_safe("/etc/passwd") == 0);
    CHECK(px_sec_path_is_safe("a/../b") == 0);
    CHECK(px_sec_path_is_safe("a:b") == 0);
    CHECK(px_sec_path_is_safe("") == 0);

    /* PIN valid */
    CHECK(px_sec_pin_valid(1000) == 1);
    CHECK(px_sec_pin_valid(9999) == 1);
    CHECK(px_sec_pin_valid(999) == 0);
    CHECK(px_sec_pin_valid(10000) == 0);

    /* Token valid */
    CHECK(px_sec_token_valid("0123456789abcdef0123456789ABCDEF") == 1);
    CHECK(px_sec_token_valid("short") == 0);
    CHECK(px_sec_token_valid("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz") == 0);
    CHECK(px_sec_token_valid(NULL) == 0);

    /* File size ok */
    CHECK(px_sec_file_size_ok(100, 1024) == 1);
    CHECK(px_sec_file_size_ok(0, 1024) == 0);
    CHECK(px_sec_file_size_ok(2048, 1024) == 0);

    /* Audit */
    {
        PxSecAudit a;
        PxSec_AuditInit(&a);
        CHECK(PxSec_AuditPass(&a) == 1);
        CHECK(a.ini_parse_safe == 1);
        CHECK(a.no_injection == 1);
        CHECK(a.no_game_memory == 1);
    }

    /* Parse int */
    {
        int out = 0;
        CHECK(px_sec_parse_int("123", 0, 0, 200, &out) == 1 && out == 123);
        CHECK(px_sec_parse_int("999", 0, 0, 100, &out) == 1 && out == 100);
        CHECK(px_sec_parse_int("abc", 42, 0, 100, &out) == 0 && out == 42);
    }

    printf("  [PASS] Security helpers finite/clamp/path/PIN/token/audit\n");
}

int main(void)
{
    printf("\n=== PlexusX New Systems Test Suite v2.2 ===\n");
    test_version();
    test_runtime_status();
    test_app_state();
    test_event_bus();
    test_security();
    printf("==================================================\n");
    printf("ALL 5 NEW SYSTEMS TEST SUITES PASSED - %d checks\n\n", g_checks);
    return 0;
}
