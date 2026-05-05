/**
 * @file    controller_control.c
 * @brief   Interactive drone flight with PS4/gamepad controller
 *
 * TODO: Implement yaw control on different buttons (L2/R2 reserved for arm safety)
 *
 * Controls:
 *   Left Joystick (vertical)  — Height control (up=UP, down=DOWN)
 *   Right Joystick (vertical) — Forward / Backward (pitch)
 *   Right Joystick (horizontal) — Left / Right (roll)
 *   L1 button                 — Yaw left (in-place rotation)
 *   R1 button                 — Yaw right (in-place rotation)
 *   L2 + R2 triggers (both)   — REQUIRED to fly
 *   Release one L2/R2         — SMOOTH AUTO-LANDING
 *   Release both L2 + R2 (hold 1s) — EMERGENCY STOP (motors cut)
 *   Triangle (Y)             — Emergency stop (or hold >2s)
 *   Circle (B)               — Cycle thrust limit (30k → 40k → 50k → 65k)
 *   X (Xbox) / A             — Forward flip
 *   Square (X) / X           — Backward flip
 *   Options / Menu           — Cinematic stunt
 *   Touchpad / Back          — Toggle test mode (print inputs only)
 *   PS / Xbox button         — Quit
 *
 * Test Mode:
 *   When enabled, shows stick/button inputs without sending control commands
 *   Press Touchpad/Back button to toggle
 *
 * Build:  (use the project Makefile)
 * Run:    ./examples/controller_control
 *
 * Setup:
 *   1. Connect PS4 controller via Bluetooth or USB
 *   2. ls /dev/input/js* to find controller device
 *   3. Run this program
 *
 * SAFETY: Keep far from people. Ensure clear space above drone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../lib/crazydrone.h"

/* ── Joystick constants ──────────────────────────────────────────────────── */
#define JOYSTICK_DEVICE "/dev/input/js0"
#define JOYSTICK_DEADZONE 6000        /* Deadzone for analog sticks - increased to handle stick drift */
#define JOYSTICK_MAX 32767
#define JOYSTICK_NEUTRAL_THRESHOLD 0.15f  /* Additional filter: ignore commands < 15% stick */
#define JOYSTICK_COMMAND_MIN_THRESHOLD 0.25f  /* Need at least 25% to produce output command */

/* PS4 Controller button/axis mapping */
#define AXIS_LEFT_VERTICAL    1       /* Left stick Y (0=up, 1=down) */
#define AXIS_RIGHT_VERTICAL   4       /* Right stick Y (4=up, 5=down) */
#define AXIS_RIGHT_HORIZONTAL 3       /* Right stick X (left=neg, right=pos) */
#define AXIS_L2_TRIGGER       2       /* L2 (0 to 32767) */
#define AXIS_R2_TRIGGER       5       /* R2 (0 to 32767) */

#define BUTTON_X               0      /* X / Square */
#define BUTTON_CIRCLE          1      /* Circle / A */
#define BUTTON_TRIANGLE        2      /* Triangle / Y */
#define BUTTON_SQUARE          3      /* Square / X */
#define BUTTON_L1              4      /* L1 (back left button) */
#define BUTTON_R1              5      /* R1 (back right button) */
#define BUTTON_L2              6      /* L2 (lower left trigger) */
#define BUTTON_R2              7      /* R2 (lower right trigger) */
#define BUTTON_SHARE           8      /* Share / Select */
#define BUTTON_OPTIONS         9      /* Options / Start */
#define BUTTON_L3              10     /* Left stick press */
#define BUTTON_R3              11     /* Right stick press */
#define BUTTON_PS              12     /* PS / Xbox button */
#define BUTTON_TOUCHPAD        13     /* Touchpad / Back */

/* ── State ───────────────────────────────────────────────────────────────── */

static float    g_roll     = 0.0f;
static float    g_pitch    = 0.0f;
static float    g_yaw_rate = 0.0f;
static uint16_t g_thrust   = 0;
static uint16_t g_hover_thrust = CD_THRUST_OFF;
static int      g_armed    = 0;
static int      g_test_mode = 0;      /* 1 = show inputs, don't control */
static int      g_debug_gyro = 0;
static int      g_thrust_level = 0;   /* 0=30k, 1=40k, 2=50k, 3=65k */

/* Landing state */
static int      g_landing = 0;        /* 1 = smooth landing in progress */
static uint32_t g_landing_start_ms = 0;
static int      g_landing_duration_ms = 0;
static int16_t  g_prev_trigger_l2 = 0;  /* Track previous trigger state for edge detection */
static int16_t  g_prev_trigger_r2 = 0;
static int      g_triggers_were_both_pressed = 0;  /* Track if triggers were both pressed */
static uint32_t g_one_trigger_released_ms = 0;  /* Timestamp when one trigger first released */
static uint32_t g_both_triggers_released_ms = 0;  /* Timestamp when BOTH triggers released */
#define TRIGGER_GRACE_PERIOD_MS 500   /* Grace period to distinguish single vs both release */
#define EMERGENCY_STOP_TIMEOUT_MS 150 /* Time both must be released to trigger emergency stop (1 sec) */

/* Joystick state */
static int16_t  g_stick_left_y = 0;   /* Left stick vertical (height) */
static int16_t  g_stick_right_y = 0;  /* Right stick vertical (pitch) */
static int16_t  g_stick_right_x = 0;  /* Right stick horizontal (roll) */
static int16_t  g_trigger_l2 = 0;     /* L2 trigger (0-32767) */
static int16_t  g_trigger_r2 = 0;     /* R2 trigger (0-32767) */
static int      g_button_l1 = 0;      /* L1 button pressed */
static int      g_button_r1 = 0;      /* R1 button pressed */

static const char *speed_names[] = { "SLOW", "NORMAL", "SPORT" };
static const char *thrust_names[] = { "30K", "40K", "50K", "65K" };
static const uint16_t thrust_levels[] = { 30000, 40000, 50000, 65000 };

/* Control tuning (shared with wasd_control) */
#define CTRL_BRAKE_MS              160
#define CTRL_BRAKE_FACTOR          0.45f

#define CTRL_TRANSLATION_CMD_SLOW_DEG     14.0f
#define CTRL_TRANSLATION_CMD_NORMAL_DEG   20.0f
#define CTRL_TRANSLATION_CMD_SPORT_DEG    25.0f

#define CTRL_YAW_CMD_SLOW_DPS             130.0f
#define CTRL_YAW_CMD_NORMAL_DPS           175.0f
#define CTRL_YAW_CMD_SPORT_DPS            200.0f

#define CTRL_INPUT_HOLD_SLOW_MS           240
#define CTRL_INPUT_HOLD_NORMAL_MS         260
#define CTRL_INPUT_HOLD_SPORT_MS          280

#define STAB_ROLL_KP               0.36f
#define STAB_PITCH_KP              0.36f
#define STAB_ROLL_KD               0.085f
#define STAB_PITCH_KD              0.085f
#define STAB_ACCEL_XY_K            0.70f
#define STAB_ROLL_KI               0.06f
#define STAB_PITCH_KI              0.06f
#define STAB_I_LIMIT_DEG           1.8f
#define STAB_ACCEL_DEADBAND_G      0.02f
#define STAB_ANGLE_DEADBAND_DEG    0.30f
#define STAB_GYRO_DEADBAND_DPS     1.5f
#define STAB_IMU_FILTER_ALPHA      0.25f
#define STAB_TRIM_LIMIT_DEG        3.5f

#define CTRL_TAKEOFF_BOOST_THRUST  32000
#define CTRL_TAKEOFF_SETTLE_THRUST 28000
#define CTRL_TAKEOFF_BOOST_MS        420

#define CTRL_THRUST_RATE_SLOW_PER_SEC    7000
#define CTRL_THRUST_RATE_NORMAL_PER_SEC  12000
#define CTRL_THRUST_RATE_SPORT_PER_SEC   22000

#define CTRL_THRUST_RATE_SLOW_PER_SEC    7000
#define CTRL_THRUST_RATE_NORMAL_PER_SEC  12000
#define CTRL_THRUST_RATE_SPORT_PER_SEC   22000

#define IMU_LOG_PERIOD_MS          100
#define IMU_STALE_TIMEOUT_MS       1200
#define IMU_RESTART_INTERVAL_MS    1500

#define FLIP_MIN_THRUST            30000
#define FLIP_EXTRA_BOOST           11000
#define FLIP_MAX_THRUST            54000
#define FLIP_BOOST_MS              320

#define STUNT_MIN_THRUST           32000
#define STUNT_CLIMB_PITCH_DEG      -3.0f
#define STUNT_DIVE_PITCH_DEG      -28.0f
#define STUNT_CATCH_PITCH_DEG       8.0f
#define STUNT_TURN_YAW_RATE_DPS    150.0f
#define STUNT_TURN_SETTLE_MS        140
#define STUNT_CLIMB_MS             1900
#define STUNT_DIVE_MS               420
#define STUNT_CATCH_MS              750
#define STUNT_CLIMB_BOOST         13000
#define STUNT_CATCH_BOOST         11000
#define STUNT_MAX_THRUST          56000

/* ── Battery constants (Crazyflie 1S LiPo) ──────────────────────────────── */
#define BATTERY_FULL_V             4.2f   /* Fully charged voltage */
#define BATTERY_EMPTY_V            3.0f   /* Minimum safe voltage */
#define BATTERY_CAPACITY_MAH       400    /* Typical Crazyflie battery capacity */
#define BATTERY_NOMINAL_VOLTAGE    3.7f   /* Nominal voltage (1S LiPo) */

static int battery_voltage_to_percent(float voltage);

/* ── Web dashboard (camera + telemetry) ─────────────────────────────────── */
#define WEB_SERVER_PORT            8080
#define WEB_ACCEPT_TIMEOUT_MS      300

typedef struct {
    uint32_t ts_ms;
    int armed;
    int landing;
    int test_mode;
    int speed_mode;
    char speed_name[16];
    uint16_t thrust;
    uint16_t hover_thrust;
    uint16_t max_thrust;
    float roll_cmd;
    float pitch_cmd;
    float yaw_rate_cmd;
    float imu_roll;
    float imu_pitch;
    float imu_yaw;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    int imu_valid;
    int gyro_valid;
    float battery_v;
    int battery_percent;
    int cam_ok;
    char camera_stream_url[256];
    char camera_snapshot_url[256];
} WebTelemetry;

static volatile int g_web_server_running = 0;
static int g_web_listen_fd = -1;
static pthread_t g_web_thread;
static pthread_mutex_t g_web_mutex = PTHREAD_MUTEX_INITIALIZER;
static WebTelemetry g_web_telemetry;

static void web_send_http_response(int fd, int code, const char *status,
                                   const char *content_type, const char *body)
{
    if (!status || !content_type || !body) return;

    size_t body_len = strlen(body);
    char header[512];
    int hdr_len = snprintf(header, sizeof(header),
                           "HTTP/1.1 %d %s\r\n"
                           "Content-Type: %s\r\n"
                           "Content-Length: %zu\r\n"
                           "Cache-Control: no-store\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           code, status, content_type, body_len);
    if (hdr_len > 0) {
        (void)write(fd, header, (size_t)hdr_len);
    }
    (void)write(fd, body, body_len);
}

static void web_send_not_found(int fd)
{
    web_send_http_response(fd, 404, "Not Found", "text/plain; charset=utf-8", "404");
}

static void web_send_dashboard_html(int fd)
{
    WebTelemetry snap;
    pthread_mutex_lock(&g_web_mutex);
    snap = g_web_telemetry;
    pthread_mutex_unlock(&g_web_mutex);

    char page[16384];
    int n = snprintf(page, sizeof(page),
        "<!doctype html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"utf-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        "  <title>ESP32 Drone Dashboard</title>\n"
        "  <style>\n"
        "    :root{--bg:#0b1020;--panel:#141a2f;--panel2:#1b2340;--text:#e8ecff;--muted:#95a2d6;--ok:#27d17f;--warn:#ffb020;--danger:#ff5470;--acc:#4aa8ff;}\n"
        "    *{box-sizing:border-box}body{margin:0;font-family:Inter,system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:radial-gradient(circle at 10%% 10%%,#1b2340,#0b1020 55%%);color:var(--text)}\n"
        "    .wrap{max-width:1280px;margin:0 auto;padding:18px}.head{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}.title{font-size:22px;font-weight:700}.sub{color:var(--muted);font-size:13px}\n"
        "    .grid{display:grid;grid-template-columns:2.1fr 1.2fr;gap:14px}.panel{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid #283155;border-radius:14px;padding:14px;box-shadow:0 8px 26px rgba(0,0,0,.35)}\n"
        "    .cam{width:100%%;aspect-ratio:16/9;border-radius:10px;border:1px solid #2d3967;background:#03060f;object-fit:cover}.badge{padding:5px 10px;border-radius:999px;font-size:12px;font-weight:700}.ok{background:rgba(39,209,127,.16);color:var(--ok)}.bad{background:rgba(255,84,112,.18);color:var(--danger)}\n"
        "    .kpis{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-top:12px}.kpi{background:rgba(255,255,255,.03);border:1px solid #2a355f;border-radius:10px;padding:10px}.kpi .l{font-size:12px;color:var(--muted)}.kpi .v{font-size:22px;font-weight:700}\n"
        "    .meter{height:9px;background:#0b1228;border:1px solid #2b3868;border-radius:999px;overflow:hidden}.fill{height:100%%;background:linear-gradient(90deg,#4aa8ff,#2ee9d2);width:0%%;transition:width .14s linear}\n"
        "    .inst{display:grid;grid-template-columns:1fr 1fr;gap:12px}.horizon{position:relative;height:210px;border-radius:12px;overflow:hidden;border:1px solid #2a355f;background:#081126}.sky{position:absolute;inset:-40%% -20%% 50%% -20%%;background:linear-gradient(#4aa8ff,#89cbff)}.ground{position:absolute;inset:50%% -20%% -40%% -20%%;background:linear-gradient(#8a5d34,#5b3a22)}\n"
        "    .att{position:absolute;inset:0;transform:translateY(0px) rotate(0deg);transition:transform .14s linear}.cross{position:absolute;left:50%%;top:50%%;width:120px;height:2px;background:#fff;transform:translate(-50%%,-50%%)}.cross:before,.cross:after{content:\"\";position:absolute;top:-6px;width:2px;height:14px;background:#fff}.cross:before{left:0}.cross:after{right:0}\n"
        "    .ring{position:absolute;inset:12px;border:2px solid rgba(255,255,255,.22);border-radius:50%%}.yawv{height:210px;display:flex;align-items:center;justify-content:center;position:relative}.compass{width:180px;height:180px;border:2px solid #2f3e71;border-radius:50%%;position:relative;background:radial-gradient(circle,#182244,#0d1530)}\n"
        "    .needle{position:absolute;left:50%%;top:50%%;width:4px;height:72px;background:linear-gradient(var(--acc),#fff);transform-origin:center 64px;transform:translate(-50%%,-90%%) rotate(0deg);border-radius:4px;transition:transform .14s linear}.center{position:absolute;left:50%%;top:50%%;width:12px;height:12px;border-radius:50%%;background:#fff;transform:translate(-50%%,-50%%)}\n"
        "    .foot{margin-top:10px;color:var(--muted);font-size:12px}\n"
        "    @media (max-width:980px){.grid{grid-template-columns:1fr}.inst{grid-template-columns:1fr}}\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div class=\"wrap\">\n"
        "    <div class=\"head\">\n"
        "      <div><div class=\"title\">ESP32 Drone Flight Dashboard</div><div class=\"sub\">Live camera + controls + flight instruments</div></div>\n"
        "      <div id=\"statusBadge\" class=\"badge bad\">DISARMED</div>\n"
        "    </div>\n"
        "    <div class=\"grid\">\n"
        "      <div class=\"panel\">\n"
        "        <img id=\"cam\" class=\"cam\" src=\"%s\" alt=\"ESP32-CAM Stream\">\n"
        "        <div class=\"kpis\">\n"
        "          <div class=\"kpi\"><div class=\"l\">Speed mode</div><div class=\"v\" id=\"mode\">%s</div></div>\n"
        "          <div class=\"kpi\"><div class=\"l\">Thrust</div><div class=\"v\" id=\"thrust\">0</div><div class=\"meter\"><div id=\"thrustFill\" class=\"fill\"></div></div></div>\n"
        "          <div class=\"kpi\"><div class=\"l\">Battery</div><div class=\"v\" id=\"battery\">--%%</div><div class=\"meter\"><div id=\"batteryFill\" class=\"fill\"></div></div></div>\n"
        "        </div>\n"
        "      </div>\n"
        "      <div class=\"panel\">\n"
        "        <div class=\"inst\">\n"
        "          <div class=\"horizon\">\n"
        "            <div id=\"att\" class=\"att\"><div class=\"sky\"></div><div class=\"ground\"></div></div>\n"
        "            <div class=\"ring\"></div><div class=\"cross\"></div>\n"
        "          </div>\n"
        "          <div class=\"yawv\">\n"
        "            <div class=\"compass\"><div id=\"needle\" class=\"needle\"></div><div class=\"center\"></div></div>\n"
        "          </div>\n"
        "        </div>\n"
        "        <div class=\"kpis\" style=\"margin-top:12px\">\n"
        "          <div class=\"kpi\"><div class=\"l\">Roll</div><div class=\"v\" id=\"roll\">0.0°</div></div>\n"
        "          <div class=\"kpi\"><div class=\"l\">Pitch</div><div class=\"v\" id=\"pitch\">0.0°</div></div>\n"
        "          <div class=\"kpi\"><div class=\"l\">Yaw rate</div><div class=\"v\" id=\"yaw\">0.0°/s</div></div>\n"
        "        </div>\n"
        "      </div>\n"
        "    </div>\n"
        "    <div class=\"foot\">Dashboard API: <code>/api/telemetry</code> | Camera stream: <code id=\"camUrlTxt\">%s</code></div>\n"
        "  </div>\n"
        "  <script>\n"
        "    const fmt=(v,d=1)=>Number.isFinite(v)?v.toFixed(d):'--';\n"
        "    async function tick(){\n"
        "      try{\n"
        "        const r=await fetch('/api/telemetry',{cache:'no-store'});\n"
        "        if(!r.ok) return;\n"
        "        const t=await r.json();\n"
        "        document.getElementById('statusBadge').textContent=t.armed?'ARMED':'DISARMED';\n"
        "        document.getElementById('statusBadge').className='badge '+(t.armed?'ok':'bad');\n"
        "        document.getElementById('mode').textContent=t.speed_name;\n"
        "        document.getElementById('thrust').textContent=t.thrust+' / '+t.max_thrust;\n"
        "        const thrPct=t.max_thrust>0?Math.max(0,Math.min(100,(t.thrust*100/t.max_thrust))):0;\n"
        "        document.getElementById('thrustFill').style.width=thrPct+'%%';\n"
        "        document.getElementById('battery').textContent=t.battery_percent+'%% ('+fmt(t.battery_v,2)+'V)';\n"
        "        document.getElementById('batteryFill').style.width=Math.max(0,Math.min(100,t.battery_percent))+'%%';\n"
        "        document.getElementById('roll').textContent=fmt(t.imu_roll,1)+'°';\n"
        "        document.getElementById('pitch').textContent=fmt(t.imu_pitch,1)+'°';\n"
        "        document.getElementById('yaw').textContent=fmt(t.yaw_rate_cmd,1)+'°/s';\n"
        "        document.getElementById('att').style.transform='translateY('+(t.imu_pitch*1.6)+'px) rotate('+(t.imu_roll)+'deg)';\n"
        "        document.getElementById('needle').style.transform='translate(-50%%,-90%%) rotate('+(t.imu_yaw)+'deg)';\n"
        "        if(t.camera_stream_url){\n"
        "          const cam=document.getElementById('cam');\n"
        "          if(cam.src!==t.camera_stream_url) cam.src=t.camera_stream_url;\n"
        "          document.getElementById('camUrlTxt').textContent=t.camera_stream_url;\n"
        "        }\n"
        "      }catch(e){}\n"
        "    }\n"
        "    setInterval(tick,180);\n"
        "    tick();\n"
        "  </script>\n"
        "</body>\n"
        "</html>\n",
        snap.camera_stream_url,
        snap.speed_name,
        snap.camera_stream_url);

    if (n <= 0 || n >= (int)sizeof(page)) {
        web_send_http_response(fd, 500, "Internal Server Error", "text/plain; charset=utf-8", "page rendering failed");
        return;
    }

    web_send_http_response(fd, 200, "OK", "text/html; charset=utf-8", page);
}

static void web_send_telemetry_json(int fd)
{
    WebTelemetry snap;
    pthread_mutex_lock(&g_web_mutex);
    snap = g_web_telemetry;
    pthread_mutex_unlock(&g_web_mutex);

    char json[2048];
    int n = snprintf(json, sizeof(json),
                     "{"
                     "\"ts_ms\":%u,"
                     "\"armed\":%d,"
                     "\"landing\":%d,"
                     "\"test_mode\":%d,"
                     "\"speed_mode\":%d,"
                     "\"speed_name\":\"%s\","
                     "\"thrust\":%u,"
                     "\"hover_thrust\":%u,"
                     "\"max_thrust\":%u,"
                     "\"roll_cmd\":%.3f,"
                     "\"pitch_cmd\":%.3f,"
                     "\"yaw_rate_cmd\":%.3f,"
                     "\"imu_roll\":%.3f,"
                     "\"imu_pitch\":%.3f,"
                     "\"imu_yaw\":%.3f,"
                     "\"gyro_x\":%.3f,"
                     "\"gyro_y\":%.3f,"
                     "\"gyro_z\":%.3f,"
                     "\"imu_valid\":%d,"
                     "\"gyro_valid\":%d,"
                     "\"battery_v\":%.3f,"
                     "\"battery_percent\":%d,"
                     "\"cam_ok\":%d,"
                     "\"camera_stream_url\":\"%s\","
                     "\"camera_snapshot_url\":\"%s\""
                     "}",
                     snap.ts_ms,
                     snap.armed,
                     snap.landing,
                     snap.test_mode,
                     snap.speed_mode,
                     snap.speed_name,
                     snap.thrust,
                     snap.hover_thrust,
                     snap.max_thrust,
                     snap.roll_cmd,
                     snap.pitch_cmd,
                     snap.yaw_rate_cmd,
                     snap.imu_roll,
                     snap.imu_pitch,
                     snap.imu_yaw,
                     snap.gyro_x,
                     snap.gyro_y,
                     snap.gyro_z,
                     snap.imu_valid,
                     snap.gyro_valid,
                     snap.battery_v,
                     snap.battery_percent,
                     snap.cam_ok,
                     snap.camera_stream_url,
                     snap.camera_snapshot_url);

    if (n <= 0 || n >= (int)sizeof(json)) {
        web_send_http_response(fd, 500, "Internal Server Error", "application/json", "{\"error\":\"json overflow\"}");
        return;
    }

    web_send_http_response(fd, 200, "OK", "application/json", json);
}

static void web_handle_client(int fd)
{
    char req[2048];
    ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0) return;
    req[n] = '\0';

    char method[8] = {0};
    char path[256] = {0};
    if (sscanf(req, "%7s %255s", method, path) != 2) {
        web_send_http_response(fd, 400, "Bad Request", "text/plain; charset=utf-8", "bad request");
        return;
    }

    if (strcmp(method, "GET") != 0) {
        web_send_http_response(fd, 405, "Method Not Allowed", "text/plain; charset=utf-8", "method not allowed");
        return;
    }

    if (strcmp(path, "/") == 0) {
        web_send_dashboard_html(fd);
    } else if (strcmp(path, "/api/telemetry") == 0) {
        web_send_telemetry_json(fd);
    } else if (strcmp(path, "/health") == 0) {
        web_send_http_response(fd, 200, "OK", "text/plain; charset=utf-8", "ok");
    } else {
        web_send_not_found(fd);
    }
}

static void *web_server_thread_main(void *arg)
{
    (void)arg;
    while (g_web_server_running) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);

        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(g_web_listen_fd, &rset);
        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = WEB_ACCEPT_TIMEOUT_MS * 1000
        };

        int sel = select(g_web_listen_fd + 1, &rset, NULL, NULL, &tv);
        if (sel <= 0 || !FD_ISSET(g_web_listen_fd, &rset)) {
            continue;
        }

        int cfd = accept(g_web_listen_fd, (struct sockaddr *)&cli, &cli_len);
        if (cfd < 0) {
            continue;
        }

        web_handle_client(cfd);
        close(cfd);
    }
    return NULL;
}

static int web_server_start(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("web socket");
        return -1;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(WEB_SERVER_PORT);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("web bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 8) < 0) {
        perror("web listen");
        close(fd);
        return -1;
    }

    g_web_listen_fd = fd;
    g_web_server_running = 1;
    if (pthread_create(&g_web_thread, NULL, web_server_thread_main, NULL) != 0) {
        perror("web pthread_create");
        g_web_server_running = 0;
        close(g_web_listen_fd);
        g_web_listen_fd = -1;
        return -1;
    }
    return 0;
}

static void web_server_stop(void)
{
    if (!g_web_server_running) return;
    g_web_server_running = 0;

    if (g_web_listen_fd >= 0) {
        shutdown(g_web_listen_fd, SHUT_RDWR);
        close(g_web_listen_fd);
        g_web_listen_fd = -1;
    }

    pthread_join(g_web_thread, NULL);
}

static void web_update_telemetry(CrazyDrone *drone)
{
    static uint32_t last_cam_probe_ms = 0;
    static int cam_probe_ok = 0;

    const CdImuData *imu = cd_get_imu(drone);
    WebTelemetry t;
    memset(&t, 0, sizeof(t));

    t.ts_ms = cd_now_ms();
    t.armed = g_armed;
    t.landing = g_landing;
    t.test_mode = g_test_mode;
    t.speed_mode = drone->speed_mode;
    snprintf(t.speed_name, sizeof(t.speed_name), "%s", speed_names[drone->speed_mode]);
    t.thrust = g_thrust;
    t.hover_thrust = g_hover_thrust;
    t.max_thrust = thrust_levels[g_thrust_level];
    t.roll_cmd = g_roll;
    t.pitch_cmd = g_pitch;
    t.yaw_rate_cmd = g_yaw_rate;
    t.imu_roll = imu->roll;
    t.imu_pitch = imu->pitch;
    t.imu_yaw = imu->yaw;
    t.gyro_x = imu->gyro_x;
    t.gyro_y = imu->gyro_y;
    t.gyro_z = imu->gyro_z;
    t.imu_valid = imu->stab_valid;
    t.gyro_valid = imu->gyro_valid;
    t.battery_v = imu->battery_v;
    t.battery_percent = battery_voltage_to_percent(imu->battery_v);
    if (t.ts_ms - last_cam_probe_ms > 2500) {
        cam_probe_ok = cd_camera_probe(drone, 250);
        last_cam_probe_ms = t.ts_ms;
    }
    t.cam_ok = cam_probe_ok;
    cd_camera_get_stream_url(drone, t.camera_stream_url, sizeof(t.camera_stream_url));
    cd_camera_get_snapshot_url(drone, t.camera_snapshot_url, sizeof(t.camera_snapshot_url));

    pthread_mutex_lock(&g_web_mutex);
    g_web_telemetry = t;
    pthread_mutex_unlock(&g_web_mutex);
}

/* ── Exponential curve helper ───────────────────────────────────────────── */

/**
 * Convert normalized joystick value (-1 to 1) to exponential command.
 * Uses exponential curve for smoother control at low values.
 * Higher values = faster exponential growth.
 */
static float apply_exponential_curve(float normalized, float exponent)
{
    if (normalized < -1.0f) normalized = -1.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    
    float sign = (normalized < 0.0f) ? -1.0f : 1.0f;
    float abs_val = fabsf(normalized);
    
    /* Exponential curve: y = x^e */
    float result = powf(abs_val, exponent) * sign;
    return result;
}

/**
 * Map joystick axis value to normalized value with deadzone
 */
static float normalize_axis(int16_t value)
{
    if (value > -JOYSTICK_DEADZONE && value < JOYSTICK_DEADZONE) {
        return 0.0f;
    }
    return (float)value / (float)JOYSTICK_MAX;
}

/**
 * Calculate smooth landing duration based on current thrust.
 * Higher thrust requires longer landing time for smooth descent.
 * At 23k (hover): ~2.5 seconds, at high altitude scales up proportionally.
 */
static int calculate_landing_duration_ms(uint16_t current_thrust)
{
    /* Base assumption: 23k is hover thrust, take ~2.5s to land from there */
    #define HOVER_THRUST_REF 23000
    #define BASE_LANDING_MS  2500
    
    if (current_thrust <= HOVER_THRUST_REF) {
        return BASE_LANDING_MS;
    }
    
    /* For higher altitudes, scale landing time proportionally */
    /* Each 1000 units above hover adds ~100ms */
    uint16_t thrust_above_hover = current_thrust - HOVER_THRUST_REF;
    int extra_time_ms = (thrust_above_hover / 1000) * 100;
    int total_landing_ms = BASE_LANDING_MS + extra_time_ms;
    
    /* Cap at 8 seconds max for very high altitude */
    if (total_landing_ms > 8000) {
        total_landing_ms = 8000;
    }
    
    return total_landing_ms;
}

/**
 * Calculate diminishing thrust step based on current altitude.
 * As the drone goes higher, thrust increments become smaller for finer control.
 * Returns a factor (0.2 to 1.0) to multiply the base step size.
 */
static float calculate_thrust_step_factor(uint16_t current_thrust, uint16_t max_thrust)
{
    if (max_thrust == 0) return 1.0f;
    
    float thrust_ratio = (float)current_thrust / (float)max_thrust;
    
    /* Linear interpolation: at 0% altitude use 100%, at 100% altitude use 20% */
    float factor = 1.0f - (thrust_ratio * 0.8f);
    
    /* Minimum factor of 0.2 to ensure some increment always available */
    if (factor < 0.2f) factor = 0.2f;
    
    return factor;
}

/**
 * Convert battery voltage to percentage (0-100%).
 * Uses linear mapping between BATTERY_EMPTY_V and BATTERY_FULL_V.
 */
static int battery_voltage_to_percent(float voltage)
{
    if (voltage >= BATTERY_FULL_V) {
        return 100;
    }
    if (voltage <= BATTERY_EMPTY_V) {
        return 0;
    }
    
    float percent = ((voltage - BATTERY_EMPTY_V) / (BATTERY_FULL_V - BATTERY_EMPTY_V)) * 100.0f;
    return (int)(percent + 0.5f);  /* Round to nearest int */
}

/**
 * Get battery status indicator symbol based on percentage.
 */
static const char *battery_indicator(int percent)
{
    if (percent >= 80) return "🔋";
    if (percent >= 60) return "🔋";
    if (percent >= 40) return "🔋";
    if (percent >= 20) return "🪫";
    return "🪫";
}

/* ── Joystick I/O ────────────────────────────────────────────────────────── */

static int open_joystick(const char *device)
{
    int fd = open(device, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open joystick");
        return -1;
    }
    return fd;
}

/* Button state tracking for edge detection */
static int button_state[16] = {0};
static int button_prev_state[16] = {0};

static int button_pressed(int button_num)
{
    return button_state[button_num];
}

static int button_just_pressed(int button_num)
{
    int pressed_now = button_state[button_num];
    int was_pressed = button_prev_state[button_num];
    button_prev_state[button_num] = pressed_now;
    return pressed_now && !was_pressed;  /* Now pressed, was not pressed */
}

static void read_joystick_events(int fd)
{
    struct js_event event;
    
    while (read(fd, &event, sizeof(event)) == sizeof(event)) {
        if (event.type == JS_EVENT_AXIS) {
            switch (event.number) {
            case AXIS_LEFT_VERTICAL:
                g_stick_left_y = event.value;
                break;
            case AXIS_RIGHT_VERTICAL:
                g_stick_right_y = event.value;
                break;
            case AXIS_RIGHT_HORIZONTAL:
                g_stick_right_x = event.value;
                break;
            case AXIS_L2_TRIGGER:
                g_trigger_l2 = event.value;
                break;
            case AXIS_R2_TRIGGER:
                g_trigger_r2 = event.value;
                break;
            default:
                break;
            }
        } else if (event.type == JS_EVENT_BUTTON) {
            if (event.number < 16) {
                button_state[event.number] = event.value;
            }
            switch (event.number) {
            case BUTTON_L1:
                g_button_l1 = event.value;
                break;
            case BUTTON_R1:
                g_button_r1 = event.value;
                break;
            default:
                break;
            }
        }
    }
}

/* ── Display ─────────────────────────────────────────────────────────────── */

static void print_status(CrazyDrone *drone)
{
    const CdImuData *imu = cd_get_imu(drone);
    uint32_t now = cd_now_ms();
    const char *mode_name = speed_names[drone->speed_mode];

    /* Move cursor to top-left and clear screen */
    printf("\033[H\033[J");

    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║         Controller Drone Flight                ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Status: %-8s   Speed: %-8s              ║\n",
           g_armed ? "ARMED" : "DISARMED",
           mode_name);
    if (g_test_mode) {
        printf("║  ⚠️  TEST MODE: Inputs shown, drone NOT controlled  ║\n");
    }
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Thrust: %5u / %u (Max: %s)              ║\n", 
           g_thrust, thrust_levels[g_thrust_level], thrust_names[g_thrust_level]);
    printf("║  Roll:   %+7.1f°    Pitch: %+7.1f°             ║\n",
           g_roll, g_pitch);
    printf("║  Yaw:    %+7.1f°/s                              ║\n", g_yaw_rate);
    printf("╠══════════════════════════════════════════════════╣\n");
    
    /* Battery display */
    int battery_percent = battery_voltage_to_percent(imu->battery_v);
    const char *batt_indicator = battery_indicator(battery_percent);
    if (drone->battery_block_ok && imu->battery_v > 0.1f) {
        printf("║  Battery: %s %3d%% (%4.2fV) %dmAh nominal       ║\n",
               batt_indicator, battery_percent, imu->battery_v, BATTERY_CAPACITY_MAH);
    } else {
        printf("║  Battery: -- --%% (--.-V) %dmAh nominal              ║\n", BATTERY_CAPACITY_MAH);
    }
    printf("╠══════════════════════════════════════════════════╣\n");

    /* Joystick input display */
    float left_y_norm = normalize_axis(g_stick_left_y);
    float right_x_norm = normalize_axis(g_stick_right_x);
    float right_y_norm = normalize_axis(g_stick_right_y);
    float l2_norm = (float)g_trigger_l2 / JOYSTICK_MAX;
    float r2_norm = (float)g_trigger_r2 / JOYSTICK_MAX;

    printf("║  Left Stick (height):                           ║\n");
    printf("║    Y: %+6.2f                                    ║\n", left_y_norm);
    printf("║  Right Stick (movement):                        ║\n");
    printf("║    X: %+6.2f  Y: %+6.2f                        ║\n", right_x_norm, right_y_norm);
    printf("║  Triggers: L2=%+6.2f  R2=%+6.2f               ║\n", l2_norm, r2_norm);
    printf("║  Back Buttons: L1=%d  R1=%d                      ║\n", g_button_l1, g_button_r1);

    printf("╠══════════════════════════════════════════════════╣\n");

    if (imu->stab_valid) {
        printf("║  IMU Roll:  %+7.1f°  Pitch: %+7.1f°            ║\n",
               imu->roll, imu->pitch);
        printf("║  IMU Yaw:   %+7.1f°                             ║\n",
               imu->yaw);
    } else {
        printf("║  IMU: (no data)                                 ║\n");
        printf("║                                                 ║\n");
    }

    if (imu->gyro_valid) {
        printf("║  Gyro: %+6.1f  %+6.1f  %+6.1f °/s              ║\n",
               imu->gyro_x, imu->gyro_y, imu->gyro_z);
    } else {
        printf("║  Gyro: (no data)                                ║\n");
    }

    printf("║  Log blocks G/S/A: %c / %c / %c                  ║\n",
           drone->gyro_block_ok ? 'Y' : 'N',
           drone->stab_block_ok ? 'Y' : 'N',
           drone->accel_block_ok ? 'Y' : 'N');

    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  L Stick Y = Height    R Stick X/Y = Movement   ║\n");
    printf("║  L1/R1 = Strafe        L2/R2 = Yaw Rotation     ║\n");
    printf("║  △/Y = Arm/Disarm      ✕/A = Fwd Flip           ║\n");
    printf("║  ○/B = Cycle Max Thrust □/X = Bwd Flip          ║\n");
    printf("║  Menu/Start = Stunt    Touchpad/Back = Test     ║\n");
    printf("║  PS/Xbox = Quit        △/Y held = Emergency     ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");

    if (g_debug_gyro) {
        uint32_t age_ms = (imu->timestamp_ms > 0 && now >= imu->timestamp_ms)
                   ? (now - imu->timestamp_ms)
                   : 0;
        float gyro_mag = sqrtf(imu->gyro_x * imu->gyro_x +
                      imu->gyro_y * imu->gyro_y +
                      imu->gyro_z * imu->gyro_z);

        printf("\n[GYRO DEBUG] valid=%d  stab_valid=%d  accel_valid=%d\n",
            imu->gyro_valid, imu->stab_valid, imu->accel_valid);
        printf("[GYRO DEBUG] blocks: gyro=%d stab=%d accel=%d\n",
            drone->gyro_block_ok, drone->stab_block_ok, drone->accel_block_ok);
        printf("[GYRO DEBUG] gx=%+8.3f  gy=%+8.3f  gz=%+8.3f  deg/s\n",
            imu->gyro_x, imu->gyro_y, imu->gyro_z);
        printf("[GYRO DEBUG] |g|=%8.3f deg/s  sample_age=%ums\n",
            gyro_mag, age_ms);
    }

    fflush(stdout);
}

static void ensure_imu_logging_fresh(CrazyDrone *drone)
{
    static uint32_t last_restart_ms = 0;
    static int stale_recovery_count = 0;

    const CdImuData *imu = cd_get_imu(drone);
    uint32_t now = cd_now_ms();

    uint32_t age_ms = (imu->timestamp_ms > 0 && now >= imu->timestamp_ms)
                        ? (now - imu->timestamp_ms)
                        : 0;

    int stale = 0;
    if (imu->timestamp_ms == 0) {
        stale = 1;
    } else if (age_ms > IMU_STALE_TIMEOUT_MS) {
        stale = 1;
    }

    if (!stale) {
        return;
    }

    if (now - last_restart_ms < IMU_RESTART_INTERVAL_MS) {
        return;
    }
    last_restart_ms = now;

    if (stale_recovery_count < 3) {
        if (drone->gyro_block_ok) {
            cd_log_stop(drone, CD_LOG_BLOCK_GYRO);
            cd_log_start(drone, CD_LOG_BLOCK_GYRO, IMU_LOG_PERIOD_MS);
        }
        if (drone->stab_block_ok) {
            cd_log_stop(drone, CD_LOG_BLOCK_STAB);
            cd_log_start(drone, CD_LOG_BLOCK_STAB, IMU_LOG_PERIOD_MS);
        }
        if (drone->accel_block_ok) {
            cd_log_stop(drone, CD_LOG_BLOCK_ACCEL);
            cd_log_start(drone, CD_LOG_BLOCK_ACCEL, IMU_LOG_PERIOD_MS);
        }
        stale_recovery_count++;
    } else {
        cd_setup_imu_logging(drone);
        stale_recovery_count = 0;
    }
}

static void do_upward_salto_flip(CrazyDrone *drone)
{
    if (!g_armed) {
        printf("\r[FLIP] Arm the drone first.\n");
        return;
    }

    if (g_hover_thrust < FLIP_MIN_THRUST) {
        printf("\r[FLIP] Increase altitude first.\n");
        return;
    }

    uint16_t saved_hover = g_hover_thrust;
    uint16_t flip_thrust = cd_clamp_thrust((int)saved_hover + FLIP_EXTRA_BOOST);
    if (flip_thrust > FLIP_MAX_THRUST) {
        flip_thrust = FLIP_MAX_THRUST;
    }

    printf("\r[FLIP] Upward boost + forward salto...\n");

    g_roll = 0.0f;
    g_pitch = 0.0f;
    g_yaw_rate = 0.0f;
    g_thrust = flip_thrust;

    cd_hover(drone, 0.0f, 0.0f, 0.0f, flip_thrust, FLIP_BOOST_MS);
    cd_flip(drone, 'p', flip_thrust);

    g_roll = 0.0f;
    g_pitch = 0.0f;
    g_yaw_rate = 0.0f;
    g_hover_thrust = saved_hover;
    g_thrust = g_hover_thrust;
}

static void do_upward_backward_salto_flip(CrazyDrone *drone)
{
    if (!g_armed) {
        printf("\r[FLIP] Arm the drone first.\n");
        return;
    }

    if (g_hover_thrust < FLIP_MIN_THRUST) {
        printf("\r[FLIP] Increase altitude first.\n");
        return;
    }

    uint16_t saved_hover = g_hover_thrust;
    uint16_t flip_thrust = cd_clamp_thrust((int)saved_hover + FLIP_EXTRA_BOOST);
    if (flip_thrust > FLIP_MAX_THRUST) {
        flip_thrust = FLIP_MAX_THRUST;
    }

    printf("\r[FLIP] Upward boost + backward salto...\n");

    g_roll = 0.0f;
    g_pitch = 0.0f;
    g_yaw_rate = 0.0f;
    g_thrust = flip_thrust;

    cd_hover(drone, 0.0f, 0.0f, 0.0f, flip_thrust, FLIP_BOOST_MS);
    cd_flip(drone, 'P', flip_thrust);

    g_roll = 0.0f;
    g_pitch = 0.0f;
    g_yaw_rate = 0.0f;
    g_hover_thrust = saved_hover;
    g_thrust = g_hover_thrust;
}



static void do_cinematic_climb_dive(CrazyDrone *drone)
{
    if (!g_armed) {
        printf("\r[STUNT] Arm the drone first.\n");
        return;
    }

    if (g_hover_thrust < STUNT_MIN_THRUST) {
        printf("\r[STUNT] Increase altitude first.\n");
        return;
    }

    uint16_t saved_hover = g_hover_thrust;
    uint16_t climb_thrust = cd_clamp_thrust((int)saved_hover + STUNT_CLIMB_BOOST);
    if (climb_thrust > STUNT_MAX_THRUST) climb_thrust = STUNT_MAX_THRUST;

    uint16_t catch_thrust = cd_clamp_thrust((int)saved_hover + STUNT_CATCH_BOOST);
    if (catch_thrust > STUNT_MAX_THRUST) catch_thrust = STUNT_MAX_THRUST;

    printf("\r[STUNT] Forward climb -> 180 turn -> nose dive -> catch...\n");

    g_roll = 0.0f;
    g_pitch = 0.0f;
    g_yaw_rate = 0.0f;
    g_thrust = climb_thrust;

    cd_hover(drone, 0.0f, STUNT_CLIMB_PITCH_DEG, 0.0f, climb_thrust, STUNT_CLIMB_MS);

    int turn_ms = (int)((180.0f / STUNT_TURN_YAW_RATE_DPS) * 1000.0f);
    cd_hover(drone, 0.0f, 0.0f, STUNT_TURN_YAW_RATE_DPS, climb_thrust, turn_ms);
    cd_hover(drone, 0.0f, 0.0f, 0.0f, climb_thrust, STUNT_TURN_SETTLE_MS);

    cd_flip(drone, 'p', climb_thrust);
    cd_hover(drone, 0.0f, STUNT_DIVE_PITCH_DEG, 0.0f, saved_hover, STUNT_DIVE_MS);

    cd_hover(drone, 0.0f, STUNT_CATCH_PITCH_DEG, 0.0f, catch_thrust, STUNT_CATCH_MS);
    cd_hover(drone, 0.0f, 0.0f, 0.0f, saved_hover, 350);

    g_roll = 0.0f;
    g_pitch = 0.0f;
    g_yaw_rate = 0.0f;
    g_hover_thrust = saved_hover;
    g_thrust = g_hover_thrust;
}

/* ─── Main ──────────────────────────────────────────────────────────────── */

int main(void)
{
    CrazyDrone *drone = cd_create(CD_DEFAULT_IP, CD_DEFAULT_PORT);
    if (!drone) {
        fprintf(stderr, "Failed to create drone handle.\n");
        return EXIT_FAILURE;
    }

    cd_camera_set_endpoint(drone, CD_DEFAULT_CAM_PORT,
                           CD_DEFAULT_CAM_STREAM_PATH,
                           CD_DEFAULT_CAM_SNAPSHOT_PATH);

    char cam_url[256] = {0};
    cd_camera_get_stream_url(drone, cam_url, sizeof(cam_url));

    cd_print_banner("Controller Drone Flight", drone->ip, drone->port);

    /* Increase control update rate */
    drone->send_rate_hz = 100;
    cd_set_speed_mode(drone, CD_SPEED_SLOW);

    printf("Connecting to drone…\n");
    int connected = cd_connect(drone, 5000);
    if (!connected) {
        printf("Warning: No reply from drone. Continuing anyway.\n");
        printf("(Make sure you're connected to the drone's WiFi AP)\n\n");
    }

    printf("Camera stream endpoint: %s\n", cam_url);
    printf("Camera probe: %s\n", cd_camera_probe(drone, 1200) ? "OK" : "not reachable yet");

    /* Try to set up IMU logging */
    printf("Setting up IMU logging (this may take a moment)…\n");
    if (cd_setup_imu_logging(drone) < 0) {
        printf("Warning: IMU logging unavailable. Flying without sensor feedback.\n");
    }

    /* Open joystick */
    printf("Opening joystick device: %s\n", JOYSTICK_DEVICE);
    int js_fd = open_joystick(JOYSTICK_DEVICE);
    if (js_fd < 0) {
        fprintf(stderr, "Failed to open joystick. Is controller connected?\n");
        fprintf(stderr, "Try: ls /dev/input/js*\n");
        cd_destroy(drone);
        return EXIT_FAILURE;
    }

    printf("\nController connected! Press Triangle (Y) to arm, then move sticks.\n");
    printf("Press Touchpad/Back to toggle TEST MODE.\n");
    printf("Press PS/Xbox button to quit.\n\n");

    memset(&g_web_telemetry, 0, sizeof(g_web_telemetry));
    snprintf(g_web_telemetry.speed_name, sizeof(g_web_telemetry.speed_name), "%s", speed_names[drone->speed_mode]);
    cd_camera_get_stream_url(drone, g_web_telemetry.camera_stream_url, sizeof(g_web_telemetry.camera_stream_url));
    cd_camera_get_snapshot_url(drone, g_web_telemetry.camera_snapshot_url, sizeof(g_web_telemetry.camera_snapshot_url));

    if (web_server_start() == 0) {
        printf("Web dashboard: http://127.0.0.1:%d\n", WEB_SERVER_PORT);
        printf("(Open from another device: http://<this-computer-ip>:%d)\n\n", WEB_SERVER_PORT);
    } else {
        printf("Warning: Could not start web dashboard on port %d\n\n", WEB_SERVER_PORT);
    }

    cd_sleep_ms(2000);

    uint32_t last_display = 0;
    int period = 1000 / drone->send_rate_hz;

    float filt_roll = 0.0f;
    float filt_pitch = 0.0f;
    float filt_gx = 0.0f;
    float filt_gy = 0.0f;
    int imu_filt_init = 0;
    uint32_t prev_imu_ts = 0;

    float hold_roll_i = 0.0f;
    float hold_pitch_i = 0.0f;
    float roll_brake = 0.0f;
    float pitch_brake = 0.0f;
    uint32_t roll_brake_until = 0;
    uint32_t pitch_brake_until = 0;
    float prev_roll_cmd = 0.0f;
    float prev_pitch_cmd = 0.0f;
    uint32_t triangle_held_time = 0;

    /* ── Main control loop ──────────────────────────────────────────────── */
    while (cd_running(drone)) {

        /* Process incoming sensor data */
        cd_poll(drone);

        /* Read joystick events */
        read_joystick_events(js_fd);

        /* Auto-recover IMU stream if packets stop arriving */
        ensure_imu_logging_fresh(drone);

        uint32_t now = cd_now_ms();

        /* ────── Handle button events ────────────────────────────────── */

        /* Triangle (Y) button: Arm/Disarm (quick press), or Emergency Stop (hold >2s) */
        if (button_pressed(BUTTON_TRIANGLE)) {
            if (triangle_held_time == 0) {
                triangle_held_time = now;
            }
            uint32_t held_ms = now - triangle_held_time;
            if (held_ms > 2000) {
                /* Emergency stop */
                g_armed    = 0;
                g_landing  = 0;
                g_landing_start_ms = 0;
                g_thrust   = 0;
                g_roll     = 0;
                g_pitch    = 0;
                g_yaw_rate = 0;
                hold_roll_i = hold_pitch_i = 0.0f;
                imu_filt_init = 0;
                prev_imu_ts = 0;
                roll_brake_until = 0;
                pitch_brake_until = 0;
                cd_emergency_stop(drone);
                printf("\r[EMERGENCY] Triangle held > 2s, motors stopped!\n");
                triangle_held_time = 0;
            }
        } else {
            /* Triangle released */
            if (triangle_held_time > 0) {
                uint32_t held_ms = now - triangle_held_time;
                if (held_ms > 200 && held_ms <= 2000) {
                    /* Quick press: toggle arm/disarm */
                    g_armed = !g_armed;
                    if (g_armed) {
                        printf("\r[ARM] Arming — sending thrust=0 for 1s …\n");
                        printf("\r[THRUST LEVEL] Current max: %s (%u)\n", 
                               thrust_names[g_thrust_level], thrust_levels[g_thrust_level]);
                        cd_arm(drone, 1000);
                        g_hover_thrust = CD_THRUST_OFF;
                        g_thrust = CD_THRUST_OFF;
                    } else {
                        g_landing = 0;
                        g_landing_start_ms = 0;
                        g_thrust   = 0;
                        g_hover_thrust = CD_THRUST_OFF;
                        g_roll     = 0;
                        g_pitch    = 0;
                        g_yaw_rate = 0;
                        hold_roll_i = hold_pitch_i = 0.0f;
                        imu_filt_init = 0;
                        prev_imu_ts = 0;
                        roll_brake_until = 0;
                        pitch_brake_until = 0;
                    }
                }
                triangle_held_time = 0;
            }
        }

        /* Circle (B) button: Cycle through thrust levels (30k → 40k → 50k → 65k) */
        if (button_just_pressed(BUTTON_CIRCLE)) {
            g_thrust_level = (g_thrust_level + 1) % 4;
            printf("\r[THRUST LEVEL] Max thrust set to %s (%u)\n", 
                   thrust_names[g_thrust_level], thrust_levels[g_thrust_level]);
        }

        /* X button: Forward flip */
        if (button_just_pressed(BUTTON_X)) {
            do_upward_salto_flip(drone);
        }

        /* Square button: Backward flip */
        if (button_just_pressed(BUTTON_SQUARE)) {
            do_upward_backward_salto_flip(drone);
        }

        /* Options (Start/Menu) button: Cinematic stunt */
        if (button_just_pressed(BUTTON_OPTIONS)) {
            do_cinematic_climb_dive(drone);
        }

        /* Touchpad / Back button: Toggle test mode */
        if (button_just_pressed(BUTTON_TOUCHPAD)) {
            g_test_mode = !g_test_mode;
            printf("\r[TEST MODE] %s\n", g_test_mode ? "ENABLED" : "DISABLED");
        }

        /* PS / Xbox button: Quit */
        if (button_just_pressed(BUTTON_PS)) {
            printf("\r[PS BUTTON] Quitting...\n");
            if (g_armed && g_thrust > 0) {
                printf("\r[LAND] Landing before exit…\n");
                cd_land(drone, g_thrust, 3000);
            }
            goto done;
        }

        /* Get control parameters based on speed mode */
        float translation_cmd_deg = CTRL_TRANSLATION_CMD_NORMAL_DEG;
        
        if (drone->speed_mode == CD_SPEED_SLOW) {
            translation_cmd_deg = CTRL_TRANSLATION_CMD_SLOW_DEG;
        } else if (drone->speed_mode == CD_SPEED_SPORT) {
            translation_cmd_deg = CTRL_TRANSLATION_CMD_SPORT_DEG;
        }

        /* ────── Handle button inputs ────────────────────────────────── */

        /* Triangle (Y) button: Arm/Disarm, held = emergency stop */
        
        /* L1/R1: Yaw control (L1 = left, R1 = right) */
        float cmd_yaw_button = 0.0f;
        if (g_button_l1) {
            cmd_yaw_button = -CTRL_YAW_CMD_NORMAL_DPS;  /* Yaw left */
        } else if (g_button_r1) {
            cmd_yaw_button = CTRL_YAW_CMD_NORMAL_DPS;   /* Yaw right */
        }

        /* ────── Handle analog inputs with exponential curves ────────── */

        /* Left stick Y: Height (up=UP, down=DOWN) */
        /* DISABLED during landing - only flight direction control allowed */
        float left_y_norm = normalize_axis(g_stick_left_y);
        if (g_armed && !g_landing && fabsf(left_y_norm) > JOYSTICK_NEUTRAL_THRESHOLD) {
            /* Exponential curve for height control */
            float exp_height = apply_exponential_curve(left_y_norm, 2.0f);
            
            const CdSpeedMode *spd = cd_get_speed(drone);
            uint16_t base_step = spd ? spd->thrust_step : CD_THRUST_STEP;
            
            /* Calculate diminishing step factor based on current thrust */
            float step_factor = calculate_thrust_step_factor(g_hover_thrust, thrust_levels[g_thrust_level]);
            uint16_t adjusted_step = (uint16_t)((float)base_step * step_factor);
            
            if (exp_height < 0.0f) {
                /* Stick up = increase height */
                if (g_hover_thrust == CD_THRUST_OFF) {
                    g_hover_thrust = CTRL_TAKEOFF_SETTLE_THRUST;
                } else {
                    g_hover_thrust = cd_clamp_thrust((int)g_hover_thrust + (int)adjusted_step);
                }
            } else if (exp_height > 0.0f) {
                /* Stick down = decrease height */
                g_hover_thrust = cd_clamp_thrust((int)g_hover_thrust - (int)adjusted_step);
            }
            
            /* Clamp to current thrust level max */
            if (g_hover_thrust > thrust_levels[g_thrust_level]) {
                g_hover_thrust = thrust_levels[g_thrust_level];
            }
        }

        /* Right stick Y: Forward/Backward (pitch) */
        float right_y_norm = normalize_axis(g_stick_right_y);
        float cmd_pitch = 0.0f;
        if (fabsf(right_y_norm) > JOYSTICK_COMMAND_MIN_THRESHOLD) {
            float exp_pitch = apply_exponential_curve(right_y_norm, 1.8f);
            cmd_pitch = exp_pitch * translation_cmd_deg;
        }

        /* Right stick X: Left/Right (roll) */
        float right_x_norm = normalize_axis(g_stick_right_x);
        float cmd_roll = 0.0f;
        if (fabsf(right_x_norm) > JOYSTICK_COMMAND_MIN_THRESHOLD) {
            float exp_roll = apply_exponential_curve(right_x_norm, 1.8f);
            cmd_roll = exp_roll * translation_cmd_deg;
        }

        /* Yaw control is now on L1/R1 buttons (see above) */

        /* Brief opposite command on release to stop drift faster */
        if (fabsf(prev_roll_cmd) > JOYSTICK_COMMAND_MIN_THRESHOLD && fabsf(cmd_roll) < JOYSTICK_COMMAND_MIN_THRESHOLD) {
            roll_brake = -prev_roll_cmd * CTRL_BRAKE_FACTOR;
            roll_brake_until = now + CTRL_BRAKE_MS;
        }
        if (fabsf(prev_pitch_cmd) > JOYSTICK_COMMAND_MIN_THRESHOLD && fabsf(cmd_pitch) < JOYSTICK_COMMAND_MIN_THRESHOLD) {
            pitch_brake = -prev_pitch_cmd * CTRL_BRAKE_FACTOR;
            pitch_brake_until = now + CTRL_BRAKE_MS;
        }

        if (now >= roll_brake_until) roll_brake = 0.0f;
        if (now >= pitch_brake_until) pitch_brake = 0.0f;

        if (fabsf(cmd_roll) > JOYSTICK_COMMAND_MIN_THRESHOLD) {
            roll_brake = 0.0f;
            roll_brake_until = 0;
        }
        if (fabsf(cmd_pitch) > JOYSTICK_COMMAND_MIN_THRESHOLD) {
            pitch_brake = 0.0f;
            pitch_brake_until = 0;
        }

        g_roll = cmd_roll + roll_brake;
        g_pitch = cmd_pitch + pitch_brake;
        /* NOTE: g_yaw_rate is set by trigger handler below, not here */

        /* IMU-assisted trim: keep craft level when no directional input */
        const CdImuData *imu = cd_get_imu(drone);
        uint32_t imu_age_ms = (imu->timestamp_ms > 0 && now >= imu->timestamp_ms)
                                ? (now - imu->timestamp_ms)
                                : UINT32_MAX;

        if (imu->stab_valid && imu->gyro_valid && imu_age_ms <= IMU_STALE_TIMEOUT_MS) {
            float dt = (float)period / 1000.0f;
            if (imu->timestamp_ms > 0 && prev_imu_ts > 0 && imu->timestamp_ms > prev_imu_ts) {
                dt = (float)(imu->timestamp_ms - prev_imu_ts) / 1000.0f;
            }
            dt = cd_clampf(dt, 0.01f, 0.20f);
            prev_imu_ts = imu->timestamp_ms;

            if (!imu_filt_init) {
                filt_roll = imu->roll;
                filt_pitch = imu->pitch;
                filt_gx = imu->gyro_x;
                filt_gy = imu->gyro_y;
                imu_filt_init = 1;
            } else {
                const float a = STAB_IMU_FILTER_ALPHA;
                filt_roll += a * (imu->roll - filt_roll);
                filt_pitch += a * (imu->pitch - filt_pitch);
                filt_gx += a * (imu->gyro_x - filt_gx);
                filt_gy += a * (imu->gyro_y - filt_gy);
            }

            if (fabsf(cmd_roll) < JOYSTICK_COMMAND_MIN_THRESHOLD) {
                float roll_rate = (fabsf(filt_gx) < STAB_GYRO_DEADBAND_DPS) ? 0.0f : filt_gx;
                float roll_err = -filt_roll;
                if (fabsf(roll_err) < STAB_ANGLE_DEADBAND_DEG) {
                    roll_err = 0.0f;
                }
                hold_roll_i += roll_err * STAB_ROLL_KI * dt;
                hold_roll_i = cd_clampf(hold_roll_i, -STAB_I_LIMIT_DEG, STAB_I_LIMIT_DEG);

                float roll_trim = (STAB_ROLL_KP * roll_err) - (STAB_ROLL_KD * roll_rate) + hold_roll_i;
                if (imu->accel_valid) {
                    float ay = imu->accel_y;
                    if (fabsf(ay) < STAB_ACCEL_DEADBAND_G) ay = 0.0f;
                    roll_trim += -(STAB_ACCEL_XY_K * ay);
                }
                g_roll += cd_clampf(roll_trim, -STAB_TRIM_LIMIT_DEG, STAB_TRIM_LIMIT_DEG);
            } else {
                hold_roll_i *= 0.85f;
            }
            if (fabsf(cmd_pitch) < JOYSTICK_COMMAND_MIN_THRESHOLD) {
                float pitch_rate = (fabsf(filt_gy) < STAB_GYRO_DEADBAND_DPS) ? 0.0f : filt_gy;
                float pitch_err = -filt_pitch;
                if (fabsf(pitch_err) < STAB_ANGLE_DEADBAND_DEG) {
                    pitch_err = 0.0f;
                }
                hold_pitch_i += pitch_err * STAB_PITCH_KI * dt;
                hold_pitch_i = cd_clampf(hold_pitch_i, -STAB_I_LIMIT_DEG, STAB_I_LIMIT_DEG);

                float pitch_trim = (STAB_PITCH_KP * pitch_err) - (STAB_PITCH_KD * pitch_rate) + hold_pitch_i;
                if (imu->accel_valid) {
                    float ax = imu->accel_x;
                    if (fabsf(ax) < STAB_ACCEL_DEADBAND_G) ax = 0.0f;
                    pitch_trim += (STAB_ACCEL_XY_K * ax);
                }
                g_pitch += cd_clampf(pitch_trim, -STAB_TRIM_LIMIT_DEG, STAB_TRIM_LIMIT_DEG);
            } else {
                hold_pitch_i *= 0.85f;
            }
        } else {
            hold_roll_i *= 0.95f;
            hold_pitch_i *= 0.95f;
            imu_filt_init = 0;
            prev_imu_ts = 0;
        }

        g_roll = cd_clampf(g_roll, -CD_ROLL_LIMIT, CD_ROLL_LIMIT);
        g_pitch = cd_clampf(g_pitch, -CD_PITCH_LIMIT, CD_PITCH_LIMIT);
        g_yaw_rate = cd_clampf(g_yaw_rate, -CD_YAW_RATE_LIMIT, CD_YAW_RATE_LIMIT);

        prev_roll_cmd = cmd_roll;
        prev_pitch_cmd = cmd_pitch;

        /* Check trigger states for flight control */
        int l2_pressed = (g_trigger_l2 > JOYSTICK_DEADZONE);
        int r2_pressed = (g_trigger_r2 > JOYSTICK_DEADZONE);
        int both_triggers_pressed = l2_pressed && r2_pressed;
        
        /* Detect edge: trigger went from pressed to released */
        int l2_released = (g_prev_trigger_l2 > JOYSTICK_DEADZONE) && !l2_pressed;
        int r2_released = (g_prev_trigger_r2 > JOYSTICK_DEADZONE) && !r2_pressed;
        
        /* Update previous state for next frame */
        g_prev_trigger_l2 = g_trigger_l2;
        g_prev_trigger_r2 = g_trigger_r2;
        
        /* Track if both triggers were pressed (for emergency stop detection) */
        if (both_triggers_pressed) {
            g_triggers_were_both_pressed = 1;
            g_both_triggers_released_ms = 0;  /* Reset emergency stop timer when triggers pressed */
        }
        
        /* Handle trigger release events with grace period and 1-second emergency stop timeout */
        if (g_armed && g_landing && g_landing_start_ms > 0) {
            /* Currently landing - check if landing is complete */
            uint32_t landing_elapsed = now - g_landing_start_ms;
            if (landing_elapsed >= (uint32_t)g_landing_duration_ms) {
                /* Landing complete */
                g_landing = 0;
                g_armed = 0;
                g_thrust = 0;
                g_hover_thrust = CD_THRUST_OFF;
                g_roll = 0;
                g_pitch = 0;
                g_yaw_rate = 0;
                hold_roll_i = hold_pitch_i = 0.0f;
                imu_filt_init = 0;
                prev_imu_ts = 0;
                printf("\r[LAND] Smooth landing complete!\n");
            }
        } else if (g_armed && g_triggers_were_both_pressed && !l2_pressed && !r2_pressed && g_both_triggers_released_ms == 0) {
            /* Both triggers were pressed, now BOTH fully released: start emergency stop timer */
            g_both_triggers_released_ms = now;
            g_one_trigger_released_ms = 0;
            printf("\r[EMERGENCY] Both triggers released! Hold for 1s to execute emergency stop...\n");
        } else if (g_both_triggers_released_ms > 0 && both_triggers_pressed) {
            /* Triggers pressed again during emergency stop countdown: cancel */
            g_both_triggers_released_ms = 0;
            printf("\r[EMERGENCY] Cancelled! Triggers pressed again.\n");
        } else if (g_both_triggers_released_ms > 0 && (now - g_both_triggers_released_ms) >= EMERGENCY_STOP_TIMEOUT_MS) {
            /* 1 second passed with both triggers released: execute emergency stop */
            g_triggers_were_both_pressed = 0;
            g_both_triggers_released_ms = 0;
            g_one_trigger_released_ms = 0;
            g_armed = 0;
            g_thrust = 0;
            g_landing = 0;
            g_landing_start_ms = 0;
            g_roll = 0;
            g_pitch = 0;
            g_yaw_rate = 0;
            hold_roll_i = hold_pitch_i = 0.0f;
            imu_filt_init = 0;
            prev_imu_ts = 0;
            roll_brake_until = 0;
            pitch_brake_until = 0;
            g_hover_thrust = CD_THRUST_OFF;
            cd_emergency_stop(drone);
            printf("\r[EMERGENCY] EMERGENCY STOP EXECUTED! Motors stopped!\n");
        } else if (g_armed && !g_landing && (l2_released || r2_released) && !both_triggers_pressed && g_both_triggers_released_ms == 0) {
            /* One trigger released (not both): start landing grace period */
            if (g_triggers_were_both_pressed && g_one_trigger_released_ms == 0) {
                g_one_trigger_released_ms = now;
                printf("\r[GRACE] One trigger released, waiting %dms...\n", TRIGGER_GRACE_PERIOD_MS);
            }
        } else if (g_one_trigger_released_ms > 0 && both_triggers_pressed) {
            /* User pressed both triggers again during grace period: cancel landing */
            g_one_trigger_released_ms = 0;
            printf("\r[GRACE] Recovered! Both triggers pressed again.\n");
        } else if (g_one_trigger_released_ms > 0 && (now - g_one_trigger_released_ms) >= TRIGGER_GRACE_PERIOD_MS) {
            /* Grace period expired: both triggers still not pressed, initiate landing */
            if (!g_landing && g_armed) {
                uint16_t land_from = g_hover_thrust;
                g_landing_duration_ms = calculate_landing_duration_ms(land_from);
                g_landing_start_ms = now;
                g_landing = 1;
                g_one_trigger_released_ms = 0;
                g_triggers_were_both_pressed = 0;
                printf("\r[LAND] Smooth landing initiated from %u thrust (%dms duration)\n", 
                       land_from, g_landing_duration_ms);
                cd_land(drone, land_from, g_landing_duration_ms);
            }
        }

        /* Assign thrust: flying normally, or stopped */
        if (g_armed && !g_landing && both_triggers_pressed) {
            g_thrust = g_hover_thrust;
        } else if (!g_armed || g_landing) {
            g_thrust = 0;  /* Will be controlled by landing sequence if landing */
        }

        /* Apply L1/R1 yaw control if not landing */
        if (!g_landing) {
            g_yaw_rate = cmd_yaw_button;
        }

        /* Send setpoint if armed AND not in test mode AND not landing */
        /* (Landing sequence handles its own setpoint sending) */
        if (g_armed && !g_test_mode && !g_landing) {
            cd_send_setpoint(drone, g_roll, g_pitch, g_yaw_rate, g_thrust);
        } else if (!g_armed) {
            cd_send_setpoint(drone, 0, 0, 0, CD_THRUST_OFF);
        }

        /* Update display every 100ms */
        if (now - last_display >= 100) {
            web_update_telemetry(drone);
            print_status(drone);
            last_display = now;
        }

        cd_sleep_ms(period);
    }

done:
    web_server_stop();

    /* Ensure motors are stopped */
    cd_emergency_stop(drone);
    close(js_fd);

    printf("\nGoodbye!\n");

    cd_destroy(drone);
    return EXIT_SUCCESS;
}
