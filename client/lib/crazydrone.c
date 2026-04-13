/**
 * @file    crazydrone.c
 * @brief   CrazyDrone library implementation — CRTP over UDP (WiFi)
 *
 * Implements:
 *   • UDP socket management + CRTP packet framing with checksum
 *   • Commander setpoints (roll/pitch/yaw/thrust)
 *   • High-level flight primitives (arm, takeoff, land, hover, circle, …)
 *   • CRTP Log subsystem (TOC scan, block create/start/stop, data parsing)
 *   • Terminal raw mode for interactive keyboard control
 *
 * Wire format  (ESP-Drone / Crazyflie over UDP):
 *   [CRTP header (1)] [payload (0-30)] [checksum (1)]
 *   checksum = byte-sum of all preceding bytes (uint8, wraps)
 *
 * Compatible with crazyflie-firmware log/param protocol V2.
 */

#define _POSIX_C_SOURCE 199309L

#include "crazydrone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

/* strnlen may need _GNU_SOURCE on some systems */
static size_t cd_strnlen(const char *s, size_t maxlen) {
    const char *p = memchr(s, '\0', maxlen);
    return p ? (size_t)(p - s) : maxlen;
}
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal globals
 * ═══════════════════════════════════════════════════════════════════════════ */

static volatile sig_atomic_t g_cd_running = 1;
static struct termios        g_orig_termios;
static int                   g_raw_mode = 0;
static int                   g_signal_installed = 0;

static int log_type_size(uint8_t type)
{
    type = (uint8_t)(type & 0x0F);
    switch (type) {
        case CD_LOG_UINT8:
        case CD_LOG_INT8:
            return 1;
        case CD_LOG_UINT16:
        case CD_LOG_INT16:
        case CD_LOG_FP16:
            return 2;
        case CD_LOG_UINT32:
        case CD_LOG_INT32:
        case CD_LOG_FLOAT:
            return 4;
        default:
            return 0;
    }
}

static float log_fp16_to_float(uint16_t h)
{
    uint16_t sign = (uint16_t)((h >> 15) & 0x1);
    uint16_t exp = (uint16_t)((h >> 10) & 0x1F);
    uint16_t frac = (uint16_t)(h & 0x03FF);

    if (exp == 0) {
        if (frac == 0) return sign ? -0.0f : 0.0f;
        float v = ldexpf((float)frac, -24);
        return sign ? -v : v;
    }

    if (exp == 31) {
        if (frac == 0) return sign ? -INFINITY : INFINITY;
        return NAN;
    }

    float m = 1.0f + ((float)frac / 1024.0f);
    float v = ldexpf(m, (int)exp - 15);
    return sign ? -v : v;
}

static float log_value_to_float(const uint8_t *data, uint8_t type)
{
    type = (uint8_t)(type & 0x0F);
    switch (type) {
        case CD_LOG_UINT8: {
            return (float)data[0];
        }
        case CD_LOG_UINT16: {
            uint16_t v;
            memcpy(&v, data, sizeof(v));
            return (float)v;
        }
        case CD_LOG_UINT32: {
            uint32_t v;
            memcpy(&v, data, sizeof(v));
            return (float)v;
        }
        case CD_LOG_INT8: {
            int8_t v;
            memcpy(&v, data, sizeof(v));
            return (float)v;
        }
        case CD_LOG_INT16: {
            int16_t v;
            memcpy(&v, data, sizeof(v));
            return (float)v;
        }
        case CD_LOG_INT32: {
            int32_t v;
            memcpy(&v, data, sizeof(v));
            return (float)v;
        }
        case CD_LOG_FLOAT: {
            float v;
            memcpy(&v, data, sizeof(v));
            return v;
        }
        case CD_LOG_FP16: {
            uint16_t v;
            memcpy(&v, data, sizeof(v));
            return log_fp16_to_float(v);
        }
        default:
            return 0.0f;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Signal handler
 * ═══════════════════════════════════════════════════════════════════════════ */

static void cd_signal_handler(int sig)
{
    (void)sig;
    g_cd_running = 0;
}

static void install_signal_handler(void)
{
    if (!g_signal_installed) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = cd_signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        g_signal_installed = 1;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Utility
 * ═══════════════════════════════════════════════════════════════════════════ */

void cd_sleep_ms(int ms)
{
    struct timespec ts = {
        .tv_sec  = ms / 1000,
        .tv_nsec = (ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);
}

uint32_t cd_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

float cd_clampf(float val, float lo, float hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

uint16_t cd_clamp_thrust(int val)
{
    if (val < 0) return 0;
    if (val > 65535) return 65535;
    return (uint16_t)val;
}

uint8_t cd_cksum(const void *data, size_t len)
{
    const unsigned char *p = data;
    uint8_t ck = 0;
    for (size_t i = 0; i < len; i++)
        ck += p[i];
    return ck;
}

void cd_print_banner(const char *title, const char *ip, int port)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║  %-46s  ║\n", title);
    printf("║  Target: %-16s  Port: %-5d       ║\n", ip, port);
    printf("╚══════════════════════════════════════════════════╝\n");
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Connection / lifecycle
 * ═══════════════════════════════════════════════════════════════════════════ */

CrazyDrone *cd_create(const char *ip, int port)
{
    install_signal_handler();

    CrazyDrone *d = calloc(1, sizeof(CrazyDrone));
    if (!d) { perror("calloc"); return NULL; }

    strncpy(d->ip, ip ? ip : CD_DEFAULT_IP, sizeof(d->ip) - 1);
    d->port         = port > 0 ? port : CD_DEFAULT_PORT;
    d->send_rate_hz = CD_DEFAULT_RATE_HZ;
    d->running      = 1;
    d->speed_mode   = CD_SPEED_NORMAL;

    /* Initialize speed mode presets */
    d->speeds[CD_SPEED_SLOW]   = (CdSpeedMode){ 10.0f, 10.0f,  60.0f, 3.0f, 3.0f, 1000 };
    d->speeds[CD_SPEED_NORMAL] = (CdSpeedMode){ 20.0f, 20.0f, 120.0f, 5.0f, 5.0f, 2000 };
    d->speeds[CD_SPEED_SPORT]  = (CdSpeedMode){ 30.0f, 30.0f, 200.0f, 8.0f, 8.0f, 3000 };

    /* Create UDP socket */
    d->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (d->sock < 0) {
        perror("[CD] socket");
        free(d);
        return NULL;
    }

    int yes = 1;
    setsockopt(d->sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    /* Bind local port (same as drone port so ACKs come back) */
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port        = htons((uint16_t)d->port);

    if (bind(d->sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        /* Non-fatal: OS will assign ephemeral port */
        fprintf(stderr, "[CD] bind port %d failed (continuing): %s\n",
                d->port, strerror(errno));
    }

    /* Set up drone destination address */
    memset(&d->drone_addr, 0, sizeof(d->drone_addr));
    d->drone_addr.sin_family = AF_INET;
    d->drone_addr.sin_port   = htons((uint16_t)d->port);
    if (inet_pton(AF_INET, d->ip, &d->drone_addr.sin_addr) <= 0) {
        fprintf(stderr, "[CD] Invalid IP: %s\n", d->ip);
        close(d->sock);
        free(d);
        return NULL;
    }

    return d;
}

void cd_destroy(CrazyDrone *drone)
{
    if (!drone) return;
    if (drone->sock >= 0) close(drone->sock);
    free(drone);
}

int cd_running(CrazyDrone *drone)
{
    return g_cd_running && (drone ? drone->running : 0);
}

int cd_is_connected(CrazyDrone *drone)
{
    return drone ? drone->connected : 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Raw CRTP send / receive
 * ═══════════════════════════════════════════════════════════════════════════ */

int cd_crtp_send(CrazyDrone *drone, uint8_t port, uint8_t channel,
                 const uint8_t *payload, size_t len)
{
    if (!drone || drone->sock < 0) return -1;
    if (len > 30) return -1;  /* CRTP max payload */

    uint8_t buf[64];
    buf[0] = CD_CRTP_HEADER(port, channel);
    if (len > 0 && payload)
        memcpy(buf + 1, payload, len);
    size_t total = 1 + len;
    buf[total] = cd_cksum(buf, total);
    total++;

    ssize_t n = sendto(drone->sock, buf, total, 0,
                       (struct sockaddr *)&drone->drone_addr,
                       sizeof(drone->drone_addr));
    return (n > 0) ? 0 : -1;
}

/**
 * Dispatch an incoming packet to the appropriate handler.
 * Called internally whenever we receive a packet.
 */
static void dispatch_packet(CrazyDrone *drone, const uint8_t *buf, int len)
{
    if (len < 2) return;

    uint8_t port = CD_CRTP_PORT(buf[0]);
    uint8_t chan = CD_CRTP_CHANNEL(buf[0]);

    /* Log data on port 5, channel 2 */
    if (port == CD_CRTP_PORT_LOG && chan == 2) {
        /* Payload starts at buf[1], checksum at buf[len-1] */
        const uint8_t *data = buf + 1;
        int dlen = len - 2;  /* minus header and checksum */

        if (dlen < 4) return;  /* need block_id + 3-byte timestamp */

        uint8_t block_id = data[0];
        /* uint32_t ts = data[1] | (data[2] << 8) | (data[3] << 16); */
        const uint8_t *vals = data + 4;
        int vlen = dlen - 4;

        if (block_id == CD_LOG_BLOCK_GYRO && drone->gyro_block_ok) {
            int s0 = log_type_size(drone->gyro_types[0]);
            int s1 = log_type_size(drone->gyro_types[1]);
            int s2 = log_type_size(drone->gyro_types[2]);
            if (s0 > 0 && s1 > 0 && s2 > 0 && vlen >= (s0 + s1 + s2)) {
                int off = 0;
                drone->imu.gyro_x = log_value_to_float(vals + off, drone->gyro_types[0]); off += s0;
                drone->imu.gyro_y = log_value_to_float(vals + off, drone->gyro_types[1]); off += s1;
                drone->imu.gyro_z = log_value_to_float(vals + off, drone->gyro_types[2]);
                drone->imu.gyro_valid = 1;
            }
        }
        else if (block_id == CD_LOG_BLOCK_STAB && drone->stab_block_ok) {
            int s0 = log_type_size(drone->stab_types[0]);
            int s1 = log_type_size(drone->stab_types[1]);
            int s2 = log_type_size(drone->stab_types[2]);
            if (s0 > 0 && s1 > 0 && s2 > 0 && vlen >= (s0 + s1 + s2)) {
                int off = 0;
                drone->imu.roll  = log_value_to_float(vals + off, drone->stab_types[0]); off += s0;
                drone->imu.pitch = log_value_to_float(vals + off, drone->stab_types[1]); off += s1;
                drone->imu.yaw   = log_value_to_float(vals + off, drone->stab_types[2]);
                drone->imu.stab_valid = 1;
            }
        }
        else if (block_id == CD_LOG_BLOCK_ACCEL && drone->accel_block_ok) {
            int s0 = log_type_size(drone->accel_types[0]);
            int s1 = log_type_size(drone->accel_types[1]);
            int s2 = log_type_size(drone->accel_types[2]);
            if (s0 > 0 && s1 > 0 && s2 > 0 && vlen >= (s0 + s1 + s2)) {
                int off = 0;
                drone->imu.accel_x = log_value_to_float(vals + off, drone->accel_types[0]); off += s0;
                drone->imu.accel_y = log_value_to_float(vals + off, drone->accel_types[1]); off += s1;
                drone->imu.accel_z = log_value_to_float(vals + off, drone->accel_types[2]);
                drone->imu.accel_valid = 1;
            }
        }
        else if (block_id == CD_LOG_BLOCK_BATTERY && drone->battery_block_ok) {
            int s0 = log_type_size(drone->battery_type);
            if (s0 > 0 && vlen >= s0) {
                drone->imu.battery_v = log_value_to_float(vals, drone->battery_type);
            }
        }

        drone->imu.timestamp_ms = cd_now_ms();
    }
}

/**
 * Receive with timeout using select().
 * Returns bytes received or -1 on timeout/error.
 */
static ssize_t recv_timeout(CrazyDrone *drone, uint8_t *buf, size_t maxlen,
                            int timeout_ms)
{
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(drone->sock, &rset);

    int r = select(drone->sock + 1, &rset, NULL, NULL, &tv);
    if (r <= 0) return -1;

    return recv(drone->sock, buf, maxlen, 0);
}

int cd_crtp_send_recv(CrazyDrone *drone, uint8_t port, uint8_t channel,
                      const uint8_t *send_data, size_t send_len,
                      uint8_t *recv_data, size_t recv_max,
                      int timeout_ms)
{
    if (!drone) return -1;

    int retries = 3;
    for (int attempt = 0; attempt < retries; attempt++) {
        /* Send */
        if (cd_crtp_send(drone, port, channel, send_data, send_len) < 0)
            return -1;

        /* Wait for matching response */
        uint32_t deadline = cd_now_ms() + (uint32_t)timeout_ms;
        while (cd_now_ms() < deadline) {
            if (!g_cd_running) return -1;

            uint8_t rbuf[64];
            ssize_t n = recv_timeout(drone, rbuf, sizeof(rbuf), 50);
            if (n <= 1) continue;

            uint8_t rport = CD_CRTP_PORT(rbuf[0]);
            uint8_t rchan = CD_CRTP_CHANNEL(rbuf[0]);

            if (rport == port && rchan == channel) {
                /* Matched!  Copy payload (skip header, skip checksum) */
                int plen = (int)n - 2;
                if (plen < 0) plen = 0;
                if ((size_t)plen > recv_max) plen = (int)recv_max;
                if (plen > 0)
                    memcpy(recv_data, rbuf + 1, (size_t)plen);
                return plen;
            }

            /* Not our response — dispatch and keep waiting */
            dispatch_packet(drone, rbuf, (int)n);
        }
    }
    return -1;  /* all retries exhausted */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Connection verification
 * ═══════════════════════════════════════════════════════════════════════════ */

int cd_connect(CrazyDrone *drone, int timeout_ms)
{
    if (!drone) return 0;
    if (timeout_ms <= 0) timeout_ms = 5000;

    printf("[CD] Pinging drone at %s:%d …\n", drone->ip, drone->port);

    /* Drain old data */
    {
        uint8_t junk[64];
        while (recv(drone->sock, junk, sizeof(junk), MSG_DONTWAIT) > 0) {}
    }

    uint32_t deadline = cd_now_ms() + (uint32_t)timeout_ms;

    int tries = 0;
    while (cd_now_ms() < deadline && g_cd_running) {
        tries++;

        /* CRTP link-layer ping: header=0xFF (port 15) */
        uint8_t ping[2];
        ping[0] = 0xFF;
        ping[1] = cd_cksum(ping, 1);
        sendto(drone->sock, ping, sizeof(ping), 0,
               (struct sockaddr *)&drone->drone_addr,
               sizeof(drone->drone_addr));

        uint8_t rbuf[64];
        ssize_t n = recv_timeout(drone, rbuf, sizeof(rbuf), 200);
        if (n > 0) {
            printf("[CD] Drone replied (%zd bytes) — connected!\n", n);
            drone->connected = 1;
            return 1;
        }

        if ((tries % 3) == 0) {
            uint8_t cmd_info = CD_LOG_TOC_GET_INFO_V2;
            uint8_t resp[16];
            int rn = cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 0,
                                       &cmd_info, 1, resp, sizeof(resp), 250);
            if (rn >= 3) {
                printf("[CD] Drone replied to log TOC query — connected!\n");
                drone->connected = 1;
                return 1;
            }
        }
    }

    fprintf(stderr, "[CD] No reply within %d ms.\n", timeout_ms);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Commander — setpoints
 * ═══════════════════════════════════════════════════════════════════════════ */

int cd_send_setpoint(CrazyDrone *drone, float roll, float pitch,
                     float yaw_rate, uint16_t thrust)
{
    if (!drone || drone->sock < 0) return -1;

    CdWirePacket pkt;
    pkt.crtp.header = CD_CRTP_HEADER(CD_CRTP_PORT_COMMANDER, 0);
    pkt.crtp.roll   = roll;
    pkt.crtp.pitch  = pitch;
    pkt.crtp.yaw    = yaw_rate;
    pkt.crtp.thrust = thrust;
    pkt.cksum       = cd_cksum(&pkt.crtp, sizeof(pkt.crtp));

    drone->cur_roll     = roll;
    drone->cur_pitch    = pitch;
    drone->cur_yaw_rate = yaw_rate;
    drone->cur_thrust   = thrust;

    ssize_t n = sendto(drone->sock, &pkt, sizeof(pkt), 0,
                       (struct sockaddr *)&drone->drone_addr,
                       sizeof(drone->drone_addr));
    return (n > 0) ? 0 : -1;
}

int cd_stop_motors(CrazyDrone *drone)
{
    return cd_send_setpoint(drone, 0, 0, 0, CD_THRUST_OFF);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * High-level flight
 * ═══════════════════════════════════════════════════════════════════════════ */

int cd_arm(CrazyDrone *drone, int duration_ms)
{
    if (!drone) return -1;
    int period = 1000 / drone->send_rate_hz;
    int packets = duration_ms / period;

    printf("[CD] ARM — sending thrust=0 for %d ms\n", duration_ms);

    for (int i = 0; i < packets; i++) {
        if (!cd_running(drone)) return -1;
        cd_send_setpoint(drone, 0, 0, 0, CD_THRUST_OFF);
        cd_sleep_ms(period);
    }
    return 0;
}

int cd_takeoff(CrazyDrone *drone, uint16_t target_thrust, int duration_ms)
{
    printf("[CD] TAKEOFF — ramping to thrust=%u over %d ms\n",
           target_thrust, duration_ms);
    return cd_ramp_thrust(drone, CD_THRUST_OFF, target_thrust, duration_ms);
}

int cd_land(CrazyDrone *drone, uint16_t from_thrust, int duration_ms)
{
    printf("[CD] LAND — ramping from %u to 0 over %d ms\n",
           from_thrust, duration_ms);
    int ret = cd_ramp_thrust(drone, from_thrust, CD_THRUST_OFF, duration_ms);
    /* Send a few zero-thrust packets to make sure motors stop */
    for (int i = 0; i < 20; i++) {
        cd_send_setpoint(drone, 0, 0, 0, CD_THRUST_OFF);
        cd_sleep_ms(20);
    }
    return ret;
}

int cd_hover(CrazyDrone *drone, float roll, float pitch, float yaw_rate,
             uint16_t thrust, int duration_ms)
{
    if (!drone) return -1;
    int period = 1000 / drone->send_rate_hz;
    int packets = duration_ms / period;

    for (int i = 0; i < packets; i++) {
        if (!cd_running(drone)) return -1;
        cd_send_setpoint(drone, roll, pitch, yaw_rate, thrust);
        cd_poll(drone);
        cd_sleep_ms(period);
    }
    return 0;
}

int cd_ramp_thrust(CrazyDrone *drone, uint16_t from, uint16_t to,
                   int duration_ms)
{
    if (!drone) return -1;
    int period = 1000 / drone->send_rate_hz;
    int packets = duration_ms / period;
    if (packets < 1) packets = 1;

    for (int i = 0; i < packets; i++) {
        if (!cd_running(drone)) return -1;
        float t = (packets > 1) ? (float)i / (float)(packets - 1) : 1.0f;
        uint16_t thr = (uint16_t)((float)from + t * ((float)to - (float)from));
        cd_send_setpoint(drone, 0, 0, 0, thr);
        cd_poll(drone);
        cd_sleep_ms(period);
    }
    return 0;
}

void cd_emergency_stop(CrazyDrone *drone)
{
    if (!drone) return;
    printf("[CD] *** EMERGENCY STOP ***\n");
    for (int i = 0; i < 50; i++) {
        cd_send_setpoint(drone, 0, 0, 0, CD_THRUST_OFF);
        cd_sleep_ms(20);
    }
    printf("[CD] Motors stopped.\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Movement helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

int cd_move_forward(CrazyDrone *drone, float pitch_deg, uint16_t thrust)
{
    /* Crazyflie: negative pitch = forward */
    return cd_send_setpoint(drone, 0, -fabsf(pitch_deg), 0, thrust);
}

int cd_move_backward(CrazyDrone *drone, float pitch_deg, uint16_t thrust)
{
    return cd_send_setpoint(drone, 0, fabsf(pitch_deg), 0, thrust);
}

int cd_move_left(CrazyDrone *drone, float roll_deg, uint16_t thrust)
{
    /* Negative roll = left */
    return cd_send_setpoint(drone, -fabsf(roll_deg), 0, 0, thrust);
}

int cd_move_right(CrazyDrone *drone, float roll_deg, uint16_t thrust)
{
    return cd_send_setpoint(drone, fabsf(roll_deg), 0, 0, thrust);
}

int cd_rotate_ccw(CrazyDrone *drone, float yaw_rate, uint16_t thrust)
{
    return cd_send_setpoint(drone, 0, 0, fabsf(yaw_rate), thrust);
}

int cd_rotate_cw(CrazyDrone *drone, float yaw_rate, uint16_t thrust)
{
    return cd_send_setpoint(drone, 0, 0, -fabsf(yaw_rate), thrust);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Pattern flight
 * ═══════════════════════════════════════════════════════════════════════════ */

int cd_fly_circle(CrazyDrone *drone, float pitch_deg, float yaw_rate,
                  uint16_t thrust, int duration_ms)
{
    if (!drone) return -1;
    int period = 1000 / drone->send_rate_hz;

    printf("[CD] Circle flight: pitch=%.1f° yaw=%.1f°/s thrust=%u\n",
           pitch_deg, yaw_rate, thrust);

    uint32_t start = cd_now_ms();

    while (cd_running(drone)) {
        if (duration_ms > 0 && (int)(cd_now_ms() - start) >= duration_ms)
            break;

        /* Forward pitch + constant yaw = circle */
        cd_send_setpoint(drone, 0, -fabsf(pitch_deg), yaw_rate, thrust);
        cd_poll(drone);
        cd_sleep_ms(period);
    }
    return 0;
}

int cd_fly_figure8(CrazyDrone *drone, float pitch_deg, float yaw_rate,
                   uint16_t thrust, int loops)
{
    if (!drone) return -1;
    int half_circle_ms = (int)(360.0f / fabsf(yaw_rate) * 1000.0f);

    printf("[CD] Figure-8: pitch=%.1f° yaw=%.1f°/s  half-circle=%dms\n",
           pitch_deg, yaw_rate, half_circle_ms);

    int count = 0;
    while (cd_running(drone)) {
        /* Left half-circle */
        cd_fly_circle(drone, pitch_deg, fabsf(yaw_rate), thrust, half_circle_ms);
        if (!cd_running(drone)) break;

        /* Right half-circle */
        cd_fly_circle(drone, pitch_deg, -fabsf(yaw_rate), thrust, half_circle_ms);
        if (!cd_running(drone)) break;

        count++;
        if (loops > 0 && count >= loops) break;
    }
    return 0;
}

int cd_flip(CrazyDrone *drone, char axis, uint16_t thrust)
{
    if (!drone) return -1;

    printf("[CD] ⚠ FLIP (%c-axis) — DANGEROUS!\n", axis);

    int period = 1000 / drone->send_rate_hz;
    if (period < 1) period = 10;

    uint16_t boost = cd_clamp_thrust((int)thrust + 11000);
    if (boost > CD_THRUST_MAX) boost = CD_THRUST_MAX;

    /* Pre-boost: go upward before rotation */
    cd_hover(drone, 0, 0, 0, boost, 320);

    float roll_cmd = 0.0f;
    float pitch_cmd = 0.0f;

    switch (axis) {
        case 'r': roll_cmd = 80.0f; break;    /* right roll flip */
        case 'R': roll_cmd = -80.0f; break;   /* left roll flip */
        case 'p': pitch_cmd = -80.0f; break;  /* forward flip */
        case 'P': pitch_cmd = 80.0f; break;   /* backward flip */
        default:  pitch_cmd = -80.0f; break;
    }

    int flip_ms = 280;
    int packets = flip_ms / period;
    if (packets < 1) packets = 1;
    for (int i = 0; i < packets && cd_running(drone); i++) {
        cd_send_setpoint(drone, roll_cmd, pitch_cmd, 0, boost);
        cd_sleep_ms(period);
    }

    /* Counter pulse to stop residual rotation and catch */
    int brake_ms = 180;
    packets = brake_ms / period;
    if (packets < 1) packets = 1;
    for (int i = 0; i < packets && cd_running(drone); i++) {
        cd_send_setpoint(drone, -0.45f * roll_cmd, -0.45f * pitch_cmd, 0, thrust);
        cd_sleep_ms(period);
    }

    /* Recovery: level out */
    cd_hover(drone, 0, 0, 0, thrust, 600);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Speed modes
 * ═══════════════════════════════════════════════════════════════════════════ */

void cd_set_speed_mode(CrazyDrone *drone, int mode)
{
    if (drone && mode >= 0 && mode <= 2)
        drone->speed_mode = mode;
}

const CdSpeedMode *cd_get_speed(CrazyDrone *drone)
{
    if (!drone) return NULL;
    return &drone->speeds[drone->speed_mode];
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CRTP Log system
 * ═══════════════════════════════════════════════════════════════════════════ */

int cd_log_reset(CrazyDrone *drone)
{
    uint8_t cmd = CD_LOG_CTRL_RESET;
    uint8_t resp[8];
    int n = cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 1,
                              &cmd, 1, resp, sizeof(resp), 350);
    if (n < 0) {
        n = cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 1,
                              &cmd, 1, resp, sizeof(resp), 500);
    }
    if (n >= 0) {
        printf("[CD] Log blocks reset.\n");
        drone->gyro_block_ok  = 0;
        drone->stab_block_ok  = 0;
        drone->accel_block_ok = 0;
        return 0;
    }
    printf("[CD] Log reset ACK not received (continuing).\n");
    return 0;
}

/**
 * Scan the log TOC: get count, then fetch every entry.
 */
static int scan_log_toc(CrazyDrone *drone)
{
    if (drone->log_toc_loaded) return 0;

    printf("[CD] Scanning log TOC…\n");

    /* 1. Get TOC info (V2): cmd=3 → response: [cmd, count_lo, count_hi, crc…] */
    uint8_t cmd_info = CD_LOG_TOC_GET_INFO_V2;
    uint8_t resp[64];
    int n = cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 0,
                              &cmd_info, 1, resp, sizeof(resp), 1000);
    if (n < 3) {
        fprintf(stderr, "[CD] Failed to get log TOC info.\n");
        return -1;
    }

    uint16_t count = (uint16_t)(resp[1] | (resp[2] << 8));
    printf("[CD] Log TOC has %u entries.\n", count);

    if (count > CD_MAX_TOC_ENTRIES)
        count = CD_MAX_TOC_ENTRIES;
    drone->log_toc_count = count;

    /* 2. Fetch each entry */
    for (uint16_t i = 0; i < count; i++) {
        if (!g_cd_running) return -1;

        if (i % 50 == 0)
            printf("[CD]   Scanning %u / %u …\n", i, count);

        uint8_t cmd_item[3] = {
            CD_LOG_TOC_GET_ITEM_V2,
            (uint8_t)(i & 0xFF),
            (uint8_t)(i >> 8)
        };

        n = cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 0,
                              cmd_item, 3, resp, sizeof(resp), 300);
        if (n < 5) {
            /* Retry once */
            n = cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 0,
                                  cmd_item, 3, resp, sizeof(resp), 500);
        }
        if (n < 5) {
            /* Skip this entry */
            drone->log_toc[i].group[0] = '\0';
            drone->log_toc[i].name[0]  = '\0';
            continue;
        }

        /* Response: [cmd, id_lo, id_hi, type, group\0name\0] */
        drone->log_toc[i].id   = (uint16_t)(resp[1] | (resp[2] << 8));
        drone->log_toc[i].type = (uint8_t)(resp[3] & 0x0F);

        /* Parse group\0name\0 strings */
        const char *s = (const char *)&resp[4];
        int remaining = n - 4;
        if (remaining > 0) {
            int glen = (int)cd_strnlen(s, (size_t)remaining);
            int cplen = glen < 31 ? glen : 31;
            memcpy(drone->log_toc[i].group, s, (size_t)cplen);
            drone->log_toc[i].group[cplen] = '\0';
            if (glen + 1 < remaining) {
                const char *nm = s + glen + 1;
                int nlen = (int)cd_strnlen(nm, (size_t)(remaining - glen - 1));
                int cnlen = nlen < 31 ? nlen : 31;
                memcpy(drone->log_toc[i].name, nm, (size_t)cnlen);
                drone->log_toc[i].name[cnlen] = '\0';
            }
        }
    }

    drone->log_toc_loaded = 1;
    printf("[CD] Log TOC scan complete.\n");
    return 0;
}

int cd_log_find_var(CrazyDrone *drone, const char *group, const char *name)
{
    if (!drone) return -1;

    /* Scan TOC if not already loaded */
    if (!drone->log_toc_loaded) {
        if (scan_log_toc(drone) < 0) return -1;
    }

    for (int i = 0; i < drone->log_toc_count; i++) {
        if (strcmp(drone->log_toc[i].group, group) == 0 &&
            strcmp(drone->log_toc[i].name, name) == 0)
        {
            return (int)drone->log_toc[i].id;
        }
    }
    return -1;
}

int cd_log_create_block(CrazyDrone *drone, uint8_t block_id,
                        const uint16_t *var_ids, const uint8_t *var_types,
                        int count)
{
    if (!drone || count < 1 || count > 8) return -1;

    /* CREATE_BLOCK_V2 payload (official client behavior):
     * [cmd=6, block_id, {storage_fetch_type, id_lo, id_hi}*N]
     */
    uint8_t payload[64];
    payload[0] = CD_LOG_CTRL_CREATE_BLOCK_V2;
    payload[1] = block_id;
    int pos = 2;
    for (int i = 0; i < count; i++) {
        payload[pos++] = (uint8_t)(var_types[i] & 0x0F);
        payload[pos++] = (uint8_t)(var_ids[i] & 0xFF);
        payload[pos++] = (uint8_t)(var_ids[i] >> 8);
    }

    uint8_t resp[8];
    int n = cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 1,
                              payload, (size_t)pos, resp, sizeof(resp), 500);
    if (n >= 3 && resp[0] == CD_LOG_CTRL_CREATE_BLOCK_V2 && resp[2] == 0) {
        return 0;  /* success */
    }

    /* Fallback for older/variant firmwares: CREATE_BLOCK with explicit types
     * [cmd=0, block_id, {type, id_lo, id_hi}*N]
     */
    payload[0] = CD_LOG_CTRL_CREATE_BLOCK;
    pos = 2;
    for (int i = 0; i < count; i++) {
        payload[pos++] = (uint8_t)(var_types[i] & 0x0F);
        payload[pos++] = (uint8_t)(var_ids[i] & 0xFF);
        payload[pos++] = (uint8_t)(var_ids[i] >> 8);
    }

    n = cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 1,
                          payload, (size_t)pos, resp, sizeof(resp), 500);
    if (n >= 3 && resp[0] == CD_LOG_CTRL_CREATE_BLOCK && resp[2] == 0) {
        return 0;
    }

    if (n >= 3) {
        fprintf(stderr, "[CD] Log block %u create error: %u\n", block_id, resp[2]);
    }
    return -1;
}

int cd_log_start(CrazyDrone *drone, uint8_t block_id, uint16_t period_ms)
{
    /* Period in multiples of 10ms */
    uint8_t period = (uint8_t)(period_ms / 10);
    if (period < 1) period = 1;

    uint8_t payload[3] = { CD_LOG_CTRL_START_BLOCK, block_id, period };
    uint8_t resp[8];
    int n = cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 1,
                              payload, 3, resp, sizeof(resp), 500);
    if (n >= 3 && resp[2] == 0) return 0;
    return -1;
}

int cd_log_stop(CrazyDrone *drone, uint8_t block_id)
{
    uint8_t payload[2] = { CD_LOG_CTRL_STOP_BLOCK, block_id };
    uint8_t resp[8];
    cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 1,
                      payload, 2, resp, sizeof(resp), 500);
    return 0;
}

int cd_log_delete(CrazyDrone *drone, uint8_t block_id)
{
    uint8_t payload[2] = { CD_LOG_CTRL_DELETE_BLOCK, block_id };
    uint8_t resp[8];
    cd_crtp_send_recv(drone, CD_CRTP_PORT_LOG, 1,
                      payload, 2, resp, sizeof(resp), 500);
    return 0;
}

/**
 * Helper: find 3 log variables and create a log block for them.
 */
static int setup_log_block_1f(CrazyDrone *drone, uint8_t block_id,
                               const char *group, const char *name,
                               uint16_t *id_out, uint8_t *type_out)
{
    int id = -1;
    uint8_t type = CD_LOG_FLOAT;

    if (!drone->log_toc_loaded) {
        if (scan_log_toc(drone) < 0) return -1;
    }

    for (int i = 0; i < drone->log_toc_count; i++) {
        CdTocEntry *e = &drone->log_toc[i];
        if (strcmp(e->group, group) == 0 && strcmp(e->name, name) == 0) {
            id = (int)e->id;
            type = e->type;
            break;
        }
    }

    if (id < 0) {
        fprintf(stderr, "[CD] Could not find %s.%s in TOC.\n", group, name);
        return -1;
    }

    *id_out = (uint16_t)id;
    *type_out = type;

    uint16_t ids[1] = { *id_out };
    uint8_t types[1] = { *type_out };

    printf("[CD] Creating log block %u: %s.%s(%u)\n",
           block_id, group, name, ids[0]);

    if (cd_log_create_block(drone, block_id, ids, types, 1) < 0) {
        fprintf(stderr, "[CD] Failed to create log block %u.\n", block_id);
        return -1;
    }
    return 0;
}

static int setup_log_block_3f(CrazyDrone *drone, uint8_t block_id,
                               const char *group,
                               const char *n0, const char *n1, const char *n2,
                               uint16_t ids_out[3], uint8_t types_out[3])
{
    int id0 = -1, id1 = -1, id2 = -1;
    uint8_t t0 = CD_LOG_FLOAT, t1 = CD_LOG_FLOAT, t2 = CD_LOG_FLOAT;

    if (!drone->log_toc_loaded) {
        if (scan_log_toc(drone) < 0) return -1;
    }

    for (int i = 0; i < drone->log_toc_count; i++) {
        CdTocEntry *e = &drone->log_toc[i];
        if (strcmp(e->group, group) != 0) continue;
        if (strcmp(e->name, n0) == 0) { id0 = (int)e->id; t0 = e->type; }
        else if (strcmp(e->name, n1) == 0) { id1 = (int)e->id; t1 = e->type; }
        else if (strcmp(e->name, n2) == 0) { id2 = (int)e->id; t2 = e->type; }
    }

    if (id0 < 0 || id1 < 0 || id2 < 0) {
        fprintf(stderr, "[CD] Could not find %s.{%s,%s,%s} in TOC.\n",
                group, n0, n1, n2);
        return -1;
    }

    ids_out[0] = (uint16_t)id0;
    ids_out[1] = (uint16_t)id1;
    ids_out[2] = (uint16_t)id2;
    types_out[0] = t0;
    types_out[1] = t1;
    types_out[2] = t2;

    uint16_t ids[3] = { ids_out[0], ids_out[1], ids_out[2] };
    uint8_t types[3] = { types_out[0], types_out[1], types_out[2] };

    printf("[CD] Creating log block %u: %s.{%s(%u),%s(%u),%s(%u)}\n",
           block_id, group, n0, ids[0], n1, ids[1], n2, ids[2]);

    if (cd_log_create_block(drone, block_id, ids, types, 3) < 0) {
        fprintf(stderr, "[CD] Failed to create log block %u.\n", block_id);
        return -1;
    }
    return 0;
}

int cd_setup_imu_logging(CrazyDrone *drone)
{
    if (!drone) return -1;

    /* Reset any old log blocks */
    cd_log_reset(drone);
    cd_sleep_ms(100);

    /* Scan TOC */
    if (scan_log_toc(drone) < 0) {
        fprintf(stderr, "[CD] TOC scan failed — IMU logging unavailable.\n");
        return -1;
    }

    int ok = 0;

    /* Block 1: gyro.x, gyro.y, gyro.z */
    if (setup_log_block_3f(drone, CD_LOG_BLOCK_GYRO,
                           "gyro", "x", "y", "z",
                           drone->gyro_ids, drone->gyro_types) == 0)
    {
        if (cd_log_start(drone, CD_LOG_BLOCK_GYRO, 100) == 0) {
            drone->gyro_block_ok = 1;
            printf("[CD] Gyro logging started (100ms).\n");
            ok++;
        }
    }

    cd_sleep_ms(50);

    /* Block 2: stabilizer.roll, stabilizer.pitch, stabilizer.yaw */
    if (setup_log_block_3f(drone, CD_LOG_BLOCK_STAB,
                           "stabilizer", "roll", "pitch", "yaw",
                           drone->stab_ids, drone->stab_types) == 0)
    {
        if (cd_log_start(drone, CD_LOG_BLOCK_STAB, 100) == 0) {
            drone->stab_block_ok = 1;
            printf("[CD] Stabilizer logging started (100ms).\n");
            ok++;
        }
    }

    cd_sleep_ms(50);

    /* Block 3: acc.x, acc.y, acc.z */
    if (setup_log_block_3f(drone, CD_LOG_BLOCK_ACCEL,
                           "acc", "x", "y", "z",
                           drone->accel_ids, drone->accel_types) == 0)
    {
        if (cd_log_start(drone, CD_LOG_BLOCK_ACCEL, 100) == 0) {
            drone->accel_block_ok = 1;
            printf("[CD] Accel logging started (100ms).\n");
            ok++;
        }
    }

    cd_sleep_ms(50);

    /* Block 4: pm.vbat (battery voltage) */
    if (setup_log_block_1f(drone, CD_LOG_BLOCK_BATTERY,
                           "pm", "vbat",
                           &drone->battery_id, &drone->battery_type) == 0)
    {
        if (cd_log_start(drone, CD_LOG_BLOCK_BATTERY, 500) == 0) {
            drone->battery_block_ok = 1;
            printf("[CD] Battery logging started (500ms).\n");
            ok++;
        }
    }

    if (ok < 3) {
        fprintf(stderr, "[CD] Warning: Some log blocks could not be started (%d/4).\n", ok);
    }

    printf("[CD] IMU logging ready (%d/4 blocks active).\n", ok);
    return (ok >= 3) ? 0 : -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Polling — process incoming packets
 * ═══════════════════════════════════════════════════════════════════════════ */

int cd_poll(CrazyDrone *drone)
{
    if (!drone || drone->sock < 0) return 0;

    int count = 0;
    uint8_t buf[64];

    for (;;) {
        ssize_t n = recv(drone->sock, buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0) break;
        dispatch_packet(drone, buf, (int)n);
        count++;
    }
    return count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IMU data getters
 * ═══════════════════════════════════════════════════════════════════════════ */

int cd_get_gyro(CrazyDrone *drone, float *gx, float *gy, float *gz)
{
    if (!drone || !drone->imu.gyro_valid) return -1;
    if (gx) *gx = drone->imu.gyro_x;
    if (gy) *gy = drone->imu.gyro_y;
    if (gz) *gz = drone->imu.gyro_z;
    return 0;
}

int cd_get_attitude(CrazyDrone *drone, float *roll, float *pitch, float *yaw)
{
    if (!drone || !drone->imu.stab_valid) return -1;
    if (roll)  *roll  = drone->imu.roll;
    if (pitch) *pitch = drone->imu.pitch;
    if (yaw)   *yaw   = drone->imu.yaw;
    return 0;
}

int cd_get_accel(CrazyDrone *drone, float *ax, float *ay, float *az)
{
    if (!drone || !drone->imu.accel_valid) return -1;
    if (ax) *ax = drone->imu.accel_x;
    if (ay) *ay = drone->imu.accel_y;
    if (az) *az = drone->imu.accel_z;
    return 0;
}

const CdImuData *cd_get_imu(CrazyDrone *drone)
{
    return drone ? &drone->imu : NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Terminal I/O — raw keyboard
 * ═══════════════════════════════════════════════════════════════════════════ */

void cd_term_raw(void)
{
    if (g_raw_mode) return;

    tcgetattr(STDIN_FILENO, &g_orig_termios);
    atexit(cd_term_restore);

    struct termios raw = g_orig_termios;
    raw.c_lflag   &= (tcflag_t)~(ECHO | ICANON);  /* No echo, no line buffer */
    raw.c_cc[VMIN]  = 0;   /* Non-blocking */
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_raw_mode = 1;
}

void cd_term_restore(void)
{
    if (g_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_mode = 0;
    }
}

int cd_kbhit(void)
{
    struct timeval tv = { 0, 0 };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

int cd_getkey(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
        return CD_KEY_NONE;

    if (c == 0x1B) {
        /* Escape sequence: try to read '[' */
        unsigned char c2;
        if (read(STDIN_FILENO, &c2, 1) != 1) return 0x1B;
        if (c2 == '[') {
            unsigned char c3;
            if (read(STDIN_FILENO, &c3, 1) != 1) return 0x1B;
            switch (c3) {
                case 'A': return CD_KEY_UP;
                case 'B': return CD_KEY_DOWN;
                case 'C': return CD_KEY_RIGHT;
                case 'D': return CD_KEY_LEFT;
                default:  return 0x1B;
            }
        }
        return 0x1B;
    }

    return (int)c;
}
