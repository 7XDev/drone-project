/**
 * @file    crazydrone.h
 * @brief   CrazyDrone — C library for controlling Crazyflie / ESP-Drone
 *          over CRTP (UDP/WiFi).
 *
 * Features:
 *   • Commander: send roll/pitch/yaw/thrust setpoints
 *   • High-level flight: takeoff, land, hover, move, circle, flip, figure-8
 *   • CRTP log subsystem: read gyro, accelerometer, attitude, battery
 *   • Speed modes: slow / normal / sport
 *   • Terminal I/O: raw keyboard input for interactive programs
 *   • Safety: Ctrl-C emergency stop, auto-land helpers
 *
 * Compatible with:
 *   - Crazyflie firmware  (https://github.com/bitcraze/crazyflie-firmware)
 *   - ESP-Drone firmware  (https://github.com/espressif/esp-drone)
 *
 * Build: compile lib/crazydrone.c alongside your program and link with -lm
 */

#ifndef CRAZYDRONE_H
#define CRAZYDRONE_H

/* Ensure POSIX APIs available (nanosleep, clock_gettime, etc.) */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration defaults
 * ═══════════════════════════════════════════════════════════════════════════ */

#define CD_DEFAULT_IP        "192.168.43.42"
#define CD_DEFAULT_PORT      2390
#define CD_DEFAULT_RATE_HZ   50       /* Packet send rate (Hz) */
#define CD_DEFAULT_CAM_PORT  81
#define CD_DEFAULT_CAM_STREAM_PATH "/stream"
#define CD_DEFAULT_CAM_SNAPSHOT_PATH "/capture"

/* ── Thrust constants (uint16, 0–65535) ──────────────────────────────────── */
#define CD_THRUST_OFF        0        /* Motors off / disarm */
#define CD_THRUST_MIN        13500    /* Minimum spinning thrust */
#define CD_THRUST_HOVER      45000    /* ~hover — TUNE FOR YOUR DRONE */
#define CD_THRUST_MAX        55000    /* Safe max (keep headroom) */
#define CD_THRUST_FULL       65535    /* Absolute maximum */
#define CD_THRUST_STEP       2000    /* Per-keypress increment */

/* ── Attitude limits (degrees / degrees-per-second) ──────────────────────── */
#define CD_ROLL_LIMIT        30.0f
#define CD_PITCH_LIMIT       30.0f
#define CD_YAW_RATE_LIMIT    200.0f

/* ── Speed modes ─────────────────────────────────────────────────────────── */
#define CD_SPEED_SLOW        0
#define CD_SPEED_NORMAL      1
#define CD_SPEED_SPORT       2

/* ── Key codes returned by cd_getkey() ───────────────────────────────────── */
#define CD_KEY_NONE          (-1)
#define CD_KEY_UP            1000
#define CD_KEY_DOWN          1001
#define CD_KEY_LEFT          1002
#define CD_KEY_RIGHT         1003

/* ── CRTP port definitions ───────────────────────────────────────────────── */
#define CD_CRTP_PORT_CONSOLE     0
#define CD_CRTP_PORT_PARAM       2
#define CD_CRTP_PORT_COMMANDER   3
#define CD_CRTP_PORT_MEM         4
#define CD_CRTP_PORT_LOG         5
#define CD_CRTP_PORT_LOCALIZATION 6
#define CD_CRTP_PORT_GENERIC     7
#define CD_CRTP_PORT_PLATFORM    13
#define CD_CRTP_PORT_LINK        15

/* ── CRTP header macros ──────────────────────────────────────────────────── */
#define CD_CRTP_HEADER(port, ch) ((uint8_t)((((port) & 0x0F) << 4) | (0x03 << 2) | ((ch) & 0x03)))
#define CD_CRTP_PORT(hdr)        (((hdr) >> 4) & 0x0F)
#define CD_CRTP_CHANNEL(hdr)     ((hdr) & 0x03)

/* ── CRTP log variable types ─────────────────────────────────────────────── */
#define CD_LOG_UINT8         1
#define CD_LOG_UINT16        2
#define CD_LOG_UINT32        3
#define CD_LOG_INT8          4
#define CD_LOG_INT16         5
#define CD_LOG_INT32         6
#define CD_LOG_FLOAT         7
#define CD_LOG_FP16          8

/* ── CRTP log protocol commands ──────────────────────────────────────────── */
/* TOC channel (ch 0) */
#define CD_LOG_TOC_GET_ITEM_V2   2
#define CD_LOG_TOC_GET_INFO_V2   3
/* Control channel (ch 1) */
#define CD_LOG_CTRL_CREATE_BLOCK     0
#define CD_LOG_CTRL_DELETE_BLOCK     2
#define CD_LOG_CTRL_START_BLOCK      3
#define CD_LOG_CTRL_STOP_BLOCK       4
#define CD_LOG_CTRL_RESET            5
#define CD_LOG_CTRL_CREATE_BLOCK_V2  6

/* ── Log block IDs (assigned by library) ─────────────────────────────────── */
#define CD_LOG_BLOCK_GYRO    1
#define CD_LOG_BLOCK_STAB    2
#define CD_LOG_BLOCK_ACCEL   3
#define CD_LOG_BLOCK_BATTERY 4

/* ═══════════════════════════════════════════════════════════════════════════
 * Types
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Log Table-of-Contents entry (one variable in the firmware's TOC). */
typedef struct {
    char     group[32];
    char     name[32];
    uint8_t  type;        /* CD_LOG_* type code */
    uint16_t id;          /* TOC index */
} CdTocEntry;

/** Snapshot of IMU / sensor data from the drone. */
typedef struct {
    float    gyro_x, gyro_y, gyro_z;      /* deg/s   */
    float    accel_x, accel_y, accel_z;    /* g       */
    float    roll, pitch, yaw;             /* deg     */
    float    battery_v;                     /* volts   */
    uint32_t timestamp_ms;                  /* host ms */
    int      gyro_valid;                    /* 1 if ever received */
    int      stab_valid;
    int      accel_valid;
} CdImuData;

/** CRTP commander packet layout (packed, 15 bytes). */
typedef struct __attribute__((packed)) {
    uint8_t  header;
    float    roll;
    float    pitch;
    float    yaw;
    uint16_t thrust;
} CdCrtpCmd;

/** Wire packet = CRTP command + checksum (16 bytes). */
typedef struct __attribute__((packed)) {
    CdCrtpCmd crtp;
    uint8_t   cksum;
} CdWirePacket;

/** Speed mode parameters. */
typedef struct {
    float    roll_limit;     /* degrees */
    float    pitch_limit;    /* degrees */
    float    yaw_limit;      /* deg/s   */
    float    roll_step;      /* per keypress */
    float    pitch_step;
    uint16_t thrust_step;
} CdSpeedMode;

/* Maximum cached TOC entries */
#define CD_MAX_TOC_ENTRIES 512

/** Main drone handle — owns socket and all state. */
typedef struct {
    /* ── Connection ───────────────────────────────────────────────────── */
    int              sock;
    struct sockaddr_in drone_addr;
    char             ip[64];
    int              port;
    int              connected;
    int              send_rate_hz;
    volatile int     running;      /* cleared on SIGINT */

    /* ── Current setpoint state ───────────────────────────────────────── */
    float            cur_roll;
    float            cur_pitch;
    float            cur_yaw_rate;
    uint16_t         cur_thrust;

    /* ── Speed mode ───────────────────────────────────────────────────── */
    int              speed_mode;   /* CD_SPEED_* */
    CdSpeedMode      speeds[3];   /* slow, normal, sport */

    /* ── Log TOC cache ────────────────────────────────────────────────── */
    CdTocEntry       log_toc[CD_MAX_TOC_ENTRIES];
    int              log_toc_count;
    int              log_toc_loaded;

    /* ── Log block variable IDs ───────────────────────────────────────── */
    uint16_t         gyro_ids[3];    /* gyro.x, y, z */
    uint16_t         stab_ids[3];    /* stabilizer.roll, pitch, yaw */
    uint16_t         accel_ids[3];   /* acc.x, y, z */
    uint16_t         battery_id;     /* pm.vbat */
    uint8_t          gyro_types[3];
    uint8_t          stab_types[3];
    uint8_t          accel_types[3];
    uint8_t          battery_type;
    int              gyro_block_ok;
    int              stab_block_ok;
    int              accel_block_ok;
    int              battery_block_ok;

    /* ── Latest sensor data ───────────────────────────────────────────── */
    CdImuData        imu;

    /* ── Camera endpoint config (ESP32-CAM HTTP) ─────────────────────── */
    int              cam_port;
    char             cam_stream_path[64];
    char             cam_snapshot_path[64];
} CrazyDrone;

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Connection & lifecycle
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Create drone handle.  Call cd_destroy() when done. */
CrazyDrone *cd_create(const char *ip, int port);

/** Destroy handle and close socket. */
void cd_destroy(CrazyDrone *drone);

/** Verify link by sending CRTP ping.  Returns 1 on success. */
int cd_connect(CrazyDrone *drone, int timeout_ms);

/** Returns 1 if successfully connected. */
int cd_is_connected(CrazyDrone *drone);

/** Returns 0 after SIGINT (Ctrl-C) is caught. */
int cd_running(CrazyDrone *drone);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Raw CRTP
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Compute CRTP checksum (byte-sum, uint8 wrap). */
uint8_t cd_cksum(const void *data, size_t len);

/** Send raw CRTP packet (header built from port/channel, cksum appended). */
int cd_crtp_send(CrazyDrone *drone, uint8_t port, uint8_t channel,
                 const uint8_t *payload, size_t len);

/** Send CRTP and wait for matching response.  Returns payload length or -1. */
int cd_crtp_send_recv(CrazyDrone *drone, uint8_t port, uint8_t channel,
                      const uint8_t *send_data, size_t send_len,
                      uint8_t *recv_data, size_t recv_max,
                      int timeout_ms);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Commander (low-level setpoints)
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Send one CRTP commander setpoint. */
int cd_send_setpoint(CrazyDrone *drone, float roll, float pitch,
                     float yaw_rate, uint16_t thrust);

/** Send thrust=0 to stop all motors immediately. */
int cd_stop_motors(CrazyDrone *drone);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — High-level flight primitives
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Arm: send thrust=0 for duration_ms to unlock ESC safety. */
int cd_arm(CrazyDrone *drone, int duration_ms);

/** Takeoff: ramp thrust from 0 to target over duration_ms. */
int cd_takeoff(CrazyDrone *drone, uint16_t target_thrust, int duration_ms);

/** Land: ramp from current_thrust to CD_THRUST_OFF over duration_ms. */
int cd_land(CrazyDrone *drone, uint16_t from_thrust, int duration_ms);

/** Hold constant setpoint for duration_ms. */
int cd_hover(CrazyDrone *drone, float roll, float pitch, float yaw_rate,
             uint16_t thrust, int duration_ms);

/** Linear thrust ramp from → to over duration_ms. */
int cd_ramp_thrust(CrazyDrone *drone, uint16_t from, uint16_t to,
                   int duration_ms);

/** Send 1 second of thrust=0 packets — guaranteed motor kill. */
void cd_emergency_stop(CrazyDrone *drone);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Movement helpers (single-setpoint, call in a loop)
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Move forward (negative pitch). */
int cd_move_forward(CrazyDrone *drone, float pitch_deg, uint16_t thrust);
/** Move backward (positive pitch). */
int cd_move_backward(CrazyDrone *drone, float pitch_deg, uint16_t thrust);
/** Strafe left (negative roll). */
int cd_move_left(CrazyDrone *drone, float roll_deg, uint16_t thrust);
/** Strafe right (positive roll). */
int cd_move_right(CrazyDrone *drone, float roll_deg, uint16_t thrust);
/** Rotate (yaw). Positive = counter-clockwise. */
int cd_rotate_ccw(CrazyDrone *drone, float yaw_rate, uint16_t thrust);
/** Rotate clockwise. */
int cd_rotate_cw(CrazyDrone *drone, float yaw_rate, uint16_t thrust);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Pattern flight (continuous, blocking)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Fly in a circle: constant forward pitch + constant yaw rate.
 * @param pitch_deg   Forward pitch angle (higher = faster, wider circle)
 * @param yaw_rate    Turn rate deg/s (higher = tighter circle)
 * @param thrust      Hover thrust for the circle
 * @param duration_ms How long to fly the circle (0 = forever until !cd_running)
 */
int cd_fly_circle(CrazyDrone *drone, float pitch_deg, float yaw_rate,
                  uint16_t thrust, int duration_ms);

/**
 * Figure-8: two opposing half-circles joined together.
 * @param loops       Number of full figure-8s (0 = forever)
 */
int cd_fly_figure8(CrazyDrone *drone, float pitch_deg, float yaw_rate,
                   uint16_t thrust, int loops);

/**
 * Quick flip attempt (DANGEROUS — drone may crash).
 * Sends extreme attitude briefly then levels out.
 * @param axis  'r' = roll flip, 'p' = pitch flip
 */
int cd_flip(CrazyDrone *drone, char axis, uint16_t thrust);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Speed modes
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Set speed mode: CD_SPEED_SLOW, CD_SPEED_NORMAL, CD_SPEED_SPORT. */
void cd_set_speed_mode(CrazyDrone *drone, int mode);

/** Get current speed mode config. */
const CdSpeedMode *cd_get_speed(CrazyDrone *drone);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — CRTP Log system (read gyro, accel, attitude, battery)
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Reset all log blocks on the drone. */
int cd_log_reset(CrazyDrone *drone);

/**
 * Scan the log TOC and set up logging for gyro, stabilizer, accel.
 * Can take 5-30 seconds depending on WiFi quality.
 * Call this BEFORE arming.
 */
int cd_setup_imu_logging(CrazyDrone *drone);

/**
 * Scan TOC to find a specific variable.
 * @return variable ID, or -1 if not found.
 */
int cd_log_find_var(CrazyDrone *drone, const char *group, const char *name);

/** Create a log block with up to 8 variables. */
int cd_log_create_block(CrazyDrone *drone, uint8_t block_id,
                        const uint16_t *var_ids, const uint8_t *var_types,
                        int count);

/** Start a log block.  period_ms rounded to nearest 10ms. */
int cd_log_start(CrazyDrone *drone, uint8_t block_id, uint16_t period_ms);

/** Stop a log block. */
int cd_log_stop(CrazyDrone *drone, uint8_t block_id);

/** Delete a log block. */
int cd_log_delete(CrazyDrone *drone, uint8_t block_id);

/**
 * Poll for incoming packets (non-blocking).
 * Call this frequently during flight to update sensor data.
 * Returns number of packets processed.
 */
int cd_poll(CrazyDrone *drone);

/** Get latest gyro reading.  Returns 0 if data valid, -1 if no data yet. */
int cd_get_gyro(CrazyDrone *drone, float *gx, float *gy, float *gz);

/** Get latest stabilizer attitude.  Returns 0 if valid. */
int cd_get_attitude(CrazyDrone *drone, float *roll, float *pitch, float *yaw);

/** Get latest accelerometer.  Returns 0 if valid. */
int cd_get_accel(CrazyDrone *drone, float *ax, float *ay, float *az);

/** Get full IMU data snapshot. */
const CdImuData *cd_get_imu(CrazyDrone *drone);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Camera endpoint helpers (ESP32-CAM on the drone)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Configure camera HTTP endpoint.
 * @param cam_port      Camera TCP port (typically 81)
 * @param stream_path   MJPEG stream path (typically "/stream")
 * @param snapshot_path JPEG snapshot path (typically "/capture")
 */
void cd_camera_set_endpoint(CrazyDrone *drone, int cam_port,
                            const char *stream_path, const char *snapshot_path);

/** Build camera stream URL into out buffer. Returns 0 on success. */
int cd_camera_get_stream_url(CrazyDrone *drone, char *out, size_t out_len);

/** Build camera snapshot URL into out buffer. Returns 0 on success. */
int cd_camera_get_snapshot_url(CrazyDrone *drone, char *out, size_t out_len);

/**
 * Probe camera endpoint by opening TCP connection and issuing HTTP request.
 * Returns 1 if endpoint responds with an HTTP status line, otherwise 0.
 */
int cd_camera_probe(CrazyDrone *drone, int timeout_ms);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Terminal I/O (raw keyboard for interactive programs)
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Switch terminal to raw mode (no echo, non-blocking reads). */
void cd_term_raw(void);

/** Restore original terminal settings. */
void cd_term_restore(void);

/** Non-blocking key check.  Returns 1 if a key is available. */
int cd_kbhit(void);

/**
 * Read one key (non-blocking).  Handles escape sequences for arrow keys.
 * Returns CD_KEY_*, ASCII char, or CD_KEY_NONE.
 */
int cd_getkey(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Utility
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Sleep for ms milliseconds. */
void cd_sleep_ms(int ms);

/** Monotonic timestamp in milliseconds. */
uint32_t cd_now_ms(void);

/** Clamp a float to [min, max]. */
float cd_clampf(float val, float lo, float hi);

/** Clamp thrust to [0, 65535]. */
uint16_t cd_clamp_thrust(int val);

/** Print a nice banner. */
void cd_print_banner(const char *title, const char *ip, int port);

#ifdef __cplusplus
}
#endif

#endif /* CRAZYDRONE_H */
