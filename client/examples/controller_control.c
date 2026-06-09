/**
 * @file    controller_control.c
 * @brief   Multi-drone parallel flight controller with network discovery
 *
 * This program discovers all ESP-Drones on the local network that are
 * connected to the "dronewifi" SSID, then controls ALL of them in parallel
 * using a single gamepad or keyboard input.
 *
 * Features:
 *   • Network discovery: finds all drones via UDP broadcast on port 2391
 *   • Parallel control: sends identical setpoints to every drone
 *   • Console GUI: displays discovered drone count + status for each
 *   • Gamepad/PS4 controller support
 *   • Keyboard fallback (WASD)
 *
 * Controls:
 *   Left Joystick (vertical)  — Height control
 *   Right Joystick (vertical) — Forward / Backward (pitch)
 *   Right Joystick (horizontal) — Left / Right (roll)
 *   L1 button                 — Yaw left
 *   R1 button                 — Yaw right
 *   L2 + R2 triggers (both)   — REQUIRED to fly
 *   Triangle (Y)              — Arm/Disarm toggle
 *   Circle (B)                — Cycle thrust limit
 *   PS / Xbox button          — Quit
 *
 * Build:  make
 * Run:    ./examples/controller_control
 *
 * WiFi: All drones must be connected to SSID "dronewifi" with password "12345678"
 *       The client machine must also be on the same network.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>
#include "../lib/crazydrone.h"

/* ── Multi-drone configuration ──────────────────────────────────────────── */
#define MAX_DRONES              16
#define ALL_DRONES_CTRL_RATE_HZ 50   /* Control update rate per drone */

/* ── Joystick constants ──────────────────────────────────────────────────── */
#define JOYSTICK_DEVICE "/dev/input/js0"
#define JOYSTICK_DEADZONE 6000
#define JOYSTICK_MAX 32767
#define JOYSTICK_NEUTRAL_THRESHOLD 0.15f
#define JOYSTICK_COMMAND_MIN_THRESHOLD 0.25f

/* PS4 Controller button/axis mapping */
#define AXIS_LEFT_VERTICAL    1
#define AXIS_RIGHT_VERTICAL   4
#define AXIS_RIGHT_HORIZONTAL 3
#define AXIS_L2_TRIGGER       2
#define AXIS_R2_TRIGGER       5

#define BUTTON_X               0
#define BUTTON_CIRCLE          1
#define BUTTON_TRIANGLE        2
#define BUTTON_SQUARE          3
#define BUTTON_L1              4
#define BUTTON_R1              5
#define BUTTON_L2              6
#define BUTTON_R2              7
#define BUTTON_SHARE           8
#define BUTTON_OPTIONS         9
#define BUTTON_L3              10
#define BUTTON_R3              11
#define BUTTON_PS              12
#define BUTTON_TOUCHPAD        13

/* ── State ───────────────────────────────────────────────────────────────── */
static float    g_roll     = 0.0f;
static float    g_pitch    = 0.0f;
static float    g_yaw_rate = 0.0f;
static uint16_t g_thrust   = 0;
static uint16_t g_hover_thrust = CD_THRUST_OFF;
static int      g_armed    = 0;
static int      g_test_mode = 0;
static int      g_thrust_level = 0;

/* Landing state */
static int      g_landing = 0;
static uint32_t g_landing_start_ms = 0;
static int      g_landing_duration_ms = 0;
static int16_t  g_prev_trigger_l2 = 0;
static int16_t  g_prev_trigger_r2 = 0;
static int      g_triggers_were_both_pressed = 0;
static uint32_t g_one_trigger_released_ms = 0;
static uint32_t g_both_triggers_released_ms = 0;
#define TRIGGER_GRACE_PERIOD_MS 500
#define EMERGENCY_STOP_TIMEOUT_MS 150

/* Drive state */
static int16_t  g_stick_left_y = 0;
static int16_t  g_stick_right_y = 0;
static int16_t  g_stick_right_x = 0;
static int16_t  g_trigger_l2 = 0;
static int16_t  g_trigger_r2 = 0;
static int      g_button_l1 = 0;
static int      g_button_r1 = 0;

static const char *speed_names[] = { "SLOW", "NORMAL", "SPORT" };
static const char *thrust_names[] = { "30K", "40K", "50K", "65K" };
static const uint16_t thrust_levels[] = { 30000, 40000, 50000, 65000 };

/* ── Battery constants ──────────────────────────────────────────────────── */
#define BATTERY_FULL_V             4.2f
#define BATTERY_EMPTY_V            3.0f

static int battery_voltage_to_percent(float voltage)
{
    if (voltage >= BATTERY_FULL_V) return 100;
    if (voltage <= BATTERY_EMPTY_V) return 0;
    float percent = ((voltage - BATTERY_EMPTY_V) / (BATTERY_FULL_V - BATTERY_EMPTY_V)) * 100.0f;
    return (int)(percent + 0.5f);
}

static const char *battery_indicator(int percent)
{
    if (percent >= 60) return "🔋";
    if (percent >= 20) return "🪫";
    return "🪫";
}

/* ── Control parameters ─────────────────────────────────────────────────── */
#define CTRL_TRANSLATION_CMD_DEG         20.0f
#define CTRL_YAW_CMD_DPS                 175.0f
#define CTRL_THRUST_RATE_PER_SEC         12000
#define CD_THRUST_STEP                   2000

/* ── Multi-drone global state ───────────────────────────────────────────── */
static int              g_drone_count = 0;
static CrazyDrone      *g_drones[MAX_DRONES] = { NULL };
static CdDiscoveredDrone g_discovered[MAX_DRONES];

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
    return pressed_now && !was_pressed;
}

static void read_joystick_events(int fd)
{
    struct js_event event;
    while (read(fd, &event, sizeof(event)) == sizeof(event)) {
        if (event.type == JS_EVENT_AXIS) {
            switch (event.number) {
            case AXIS_LEFT_VERTICAL:   g_stick_left_y = event.value; break;
            case AXIS_RIGHT_VERTICAL:  g_stick_right_y = event.value; break;
            case AXIS_RIGHT_HORIZONTAL: g_stick_right_x = event.value; break;
            case AXIS_L2_TRIGGER:      g_trigger_l2 = event.value; break;
            case AXIS_R2_TRIGGER:      g_trigger_r2 = event.value; break;
            default: break;
            }
        } else if (event.type == JS_EVENT_BUTTON) {
            if (event.number < 16) button_state[event.number] = event.value;
            switch (event.number) {
            case BUTTON_L1: g_button_l1 = event.value; break;
            case BUTTON_R1: g_button_r1 = event.value; break;
            default: break;
            }
        }
    }
}

/* ── Normalize axis helper ───────────────────────────────────────────────── */
static float normalize_axis(int16_t value)
{
    if (value > -JOYSTICK_DEADZONE && value < JOYSTICK_DEADZONE) return 0.0f;
    return (float)value / (float)JOYSTICK_MAX;
}

/* ── Send commands to ALL drones ──────────────────────────────────────────── */
static void send_to_all_drones(float roll, float pitch, float yaw_rate, uint16_t thrust)
{
    for (int i = 0; i < g_drone_count; i++) {
        if (g_drones[i] && g_drones[i]->connected) {
            cd_send_setpoint(g_drones[i], roll, pitch, yaw_rate, thrust);
        }
    }
}

static void emergency_stop_all(void)
{
    for (int i = 0; i < g_drone_count; i++) {
        if (g_drones[i]) {
            cd_emergency_stop(g_drones[i]);
        }
    }
}

static void arm_all(int duration_ms)
{
    for (int i = 0; i < g_drone_count; i++) {
        if (g_drones[i] && g_drones[i]->connected) {
            cd_arm(g_drones[i], duration_ms);
        }
    }
}

static void land_all(uint16_t from_thrust, int duration_ms)
{
    for (int i = 0; i < g_drone_count; i++) {
        if (g_drones[i] && g_drones[i]->connected) {
            cd_land(g_drones[i], from_thrust, duration_ms);
        }
    }
}

/* ── Console GUI ────────────────────────────────────────────────────────── */
static void print_status(void)
{
    uint32_t now = cd_now_ms();

    printf("\033[H\033[J");

    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║         MULTI-DRONE PARALLEL FLIGHT            ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Drones found: %-2d                            ║\n", g_drone_count);
    printf("║  Status: %-8s   Ctrl Rate: %d Hz              ║\n",
           g_armed ? "ARMED" : "DISARMED", ALL_DRONES_CTRL_RATE_HZ);
    if (g_test_mode) {
        printf("║  ⚠ TEST MODE: Commands shown but NOT sent     ║\n");
    }
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Thrust: %5u / %-4s  (Max: %u)          ║\n",
           g_thrust, thrust_names[g_thrust_level], thrust_levels[g_thrust_level]);
    printf("║  Roll: %+7.1f°  Pitch: %+7.1f°  Yaw: %+6.1f°/s ║\n",
           g_roll, g_pitch, g_yaw_rate);
    printf("╠══════════════════════════════════════════════════╣\n");

    /* Show each drone */
    printf("║  #  Drone ID           IP               Status ║\n");
    printf("║  ═══════════════════════════════════════════════ ║\n");
    for (int i = 0; i < g_drone_count; i++) {
        const char *status = "---";
        if (g_drones[i]) {
            if (g_drones[i]->connected) status = "CONN";
            else status = "WAIT";
        }
        printf("║  %-2d %-20s %-15s %-5s ║\n",
               i + 1,
               g_discovered[i].id,
               g_discovered[i].ip,
               status);
    }

    printf("╠══════════════════════════════════════════════════╣\n");

    /* Joystick display */
    float left_y_norm = normalize_axis(g_stick_left_y);
    float right_x_norm = normalize_axis(g_stick_right_x);
    float right_y_norm = normalize_axis(g_stick_right_y);
    float l2_norm = (float)g_trigger_l2 / JOYSTICK_MAX;
    float r2_norm = (float)g_trigger_r2 / JOYSTICK_MAX;

    printf("║  L-Stick Y: %+6.2f  R-Stick X: %+6.2f  Y: %+6.2f ║\n",
           left_y_norm, right_x_norm, right_y_norm);
    printf("║  Triggers: L2=%+6.2f  R2=%+6.2f  L1=%d  R1=%d ║\n",
           l2_norm, r2_norm, g_button_l1, g_button_r1);

    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Controls:                                      ║\n");
    printf("║  L-Stick Y = Height   R-Stick X/Y = Movement   ║\n");
    printf("║  L1/R1 = Yaw          L2+R2 = Enable Flight    ║\n");
    printf("║  △(Y) = Arm/Disarm    ○(B) = Cycle Thrust     ║\n");
    printf("║  PS/Xbox = Quit                                ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");

    fflush(stdout);
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n🔍 Scanning for drones on network...\n");
    printf("   (Make sure all drones are connected to SSID 'dronewifi')\n\n");

    /* ── Step 1: Discover all drones on the network ─────────────────────── */
    memset(g_discovered, 0, sizeof(g_discovered));
    g_drone_count = cd_discover_drones(g_discovered, MAX_DRONES, CD_DISCOVERY_TIMEOUT_MS);

    if (g_drone_count == 0) {
        fprintf(stderr, "No drones found on the network.\n");
        fprintf(stderr, "Make sure drones are powered on and connected to 'dronewifi'.\n");
        fprintf(stderr, "Also ensure this computer is on the same network.\n");
        return EXIT_FAILURE;
    }

    printf("\n🎯 Found %d drone(s) on the network!\n\n", g_drone_count);

    /* ── Step 2: Connect to each drone ──────────────────────────────────── */
    for (int i = 0; i < g_drone_count; i++) {
        printf("Connecting to %s (%s)...\n", g_discovered[i].id, g_discovered[i].ip);

        g_drones[i] = cd_create(g_discovered[i].ip, g_discovered[i].port);
        if (!g_drones[i]) {
            fprintf(stderr, "  Failed to create drone handle for %s\n", g_discovered[i].ip);
            continue;
        }

        g_drones[i]->send_rate_hz = ALL_DRONES_CTRL_RATE_HZ;
        cd_set_speed_mode(g_drones[i], CD_SPEED_NORMAL);

        if (cd_connect(g_drones[i], 2000)) {
            printf("  ✅ Connected!\n");
        } else {
            printf("  ⚠️  Could not verify connection. Continuing anyway.\n");
        }
    }

    cd_sleep_ms(1000);

    /* ── Step 3: Open joystick (optional) ────────────────────────────────── */
    int js_fd = open_joystick(JOYSTICK_DEVICE);
    if (js_fd < 0) {
        fprintf(stderr, "\nNo joystick found at %s\n", JOYSTICK_DEVICE);
        fprintf(stderr, "Controls: Use keyboard instead.\n");
        fprintf(stderr, "  W/S = height up/down\n");
        fprintf(stderr, "  A/D = yaw left/right\n");
        fprintf(stderr, "  Up/Down = forward/backward\n");
        fprintf(stderr, "  Left/Right = roll left/right\n");
        fprintf(stderr, "  Space = arm/disarm\n");
        fprintf(stderr, "  Q = quit\n");
        fprintf(stderr, "  TAB = cycle thrust\n");
    }

    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║  ✅ %d Drone(s) ready!                        ║\n", g_drone_count);
    printf("║  Press Triangle(Y) or Space to ARM all drones  ║\n");
    printf("║  then hold L2+R2 (or Enter) to enable flight.  ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");
    cd_sleep_ms(2000);

    /* ── Step 4: Main control loop ──────────────────────────────────────── */
    uint32_t last_display = 0;
    int period = 1000 / ALL_DRONES_CTRL_RATE_HZ;
    uint32_t triangle_held_time = 0;

    while (cd_running(g_drones[0] ? g_drones[0] : NULL)) {
        uint32_t now = cd_now_ms();

        /* Poll all drones for incoming data */
        for (int i = 0; i < g_drone_count; i++) {
            if (g_drones[i]) cd_poll(g_drones[i]);
        }

        /* Read joystick events */
        if (js_fd >= 0) read_joystick_events(js_fd);

        /* ────── Handle triangle/arm button ─────────────────────────── */
        int arm_button = 0;
        if (js_fd >= 0) {
            arm_button = button_pressed(BUTTON_TRIANGLE);
        }

        if (arm_button) {
            if (triangle_held_time == 0) triangle_held_time = now;
        } else {
            if (triangle_held_time > 0) {
                uint32_t held_ms = now - triangle_held_time;
                if (held_ms > 200 && held_ms <= 2000) {
                    g_armed = !g_armed;
                    if (g_armed) {
                        printf("\r[ARM] Arming %d drones...\n", g_drone_count);
                        arm_all(1000);
                        g_hover_thrust = CD_THRUST_OFF;
                        g_thrust = CD_THRUST_OFF;
                        print_status();
                    } else {
                        g_landing = 0;
                        g_thrust = 0;
                        g_hover_thrust = CD_THRUST_OFF;
                        g_roll = 0; g_pitch = 0; g_yaw_rate = 0;
                    }
                }
                triangle_held_time = 0;
            }
        }

        /* Circle button: cycle thrust */
        if (js_fd >= 0 && button_just_pressed(BUTTON_CIRCLE)) {
            g_thrust_level = (g_thrust_level + 1) % 4;
            printf("\r[THRUST LEVEL] Max thrust set to %s (%u)\n",
                   thrust_names[g_thrust_level], thrust_levels[g_thrust_level]);
        }

        /* PS button: quit */
        if (js_fd >= 0 && button_just_pressed(BUTTON_PS)) {
            printf("\r[QUIT] Quitting...\n");
            if (g_armed && g_thrust > 0) {
                land_all(g_thrust, 3000);
            }
            break;
        }

        /* ────── Handle analog inputs ──────────────────────────────── */
        float left_y_norm = normalize_axis(g_stick_left_y);
        if (g_armed && !g_landing && fabsf(left_y_norm) > JOYSTICK_NEUTRAL_THRESHOLD) {
            if (left_y_norm < 0.0f) {
                /* Stick up = increase height */
                if (g_hover_thrust == CD_THRUST_OFF) {
                    g_hover_thrust = 28000;
                } else {
                    g_hover_thrust = cd_clamp_thrust((int)g_hover_thrust + CD_THRUST_STEP);
                }
            } else {
                g_hover_thrust = cd_clamp_thrust((int)g_hover_thrust - CD_THRUST_STEP);
            }
            if (g_hover_thrust > thrust_levels[g_thrust_level])
                g_hover_thrust = thrust_levels[g_thrust_level];
        }

        /* Right stick Y: Forward/Backward (pitch) */
        float right_y_norm = normalize_axis(g_stick_right_y);
        float cmd_pitch = 0.0f;
        if (fabsf(right_y_norm) > JOYSTICK_COMMAND_MIN_THRESHOLD)
            cmd_pitch = right_y_norm * CTRL_TRANSLATION_CMD_DEG;

        /* Right stick X: Left/Right (roll) */
        float right_x_norm = normalize_axis(g_stick_right_x);
        float cmd_roll = 0.0f;
        if (fabsf(right_x_norm) > JOYSTICK_COMMAND_MIN_THRESHOLD)
            cmd_roll = right_x_norm * CTRL_TRANSLATION_CMD_DEG;

        /* L1/R1: Yaw */
        float cmd_yaw = 0.0f;
        if (g_button_l1) cmd_yaw = -CTRL_YAW_CMD_DPS;
        else if (g_button_r1) cmd_yaw = CTRL_YAW_CMD_DPS;

        /* Check trigger states */
        int l2_pressed = (g_trigger_l2 > JOYSTICK_DEADZONE);
        int r2_pressed = (g_trigger_r2 > JOYSTICK_DEADZONE);
        int both_triggers_pressed = l2_pressed && r2_pressed;

        int l2_released = (g_prev_trigger_l2 > JOYSTICK_DEADZONE) && !l2_pressed;
        int r2_released = (g_prev_trigger_r2 > JOYSTICK_DEADZONE) && !r2_pressed;

        g_prev_trigger_l2 = g_trigger_l2;
        g_prev_trigger_r2 = g_trigger_r2;

        if (both_triggers_pressed) {
            g_triggers_were_both_pressed = 1;
            g_both_triggers_released_ms = 0;
        }

        /* Handle landing state */
        if (g_armed && g_landing && g_landing_start_ms > 0) {
            uint32_t landing_elapsed = now - g_landing_start_ms;
            if (landing_elapsed >= (uint32_t)g_landing_duration_ms) {
                g_landing = 0;
                g_armed = 0;
                g_thrust = 0;
                g_hover_thrust = CD_THRUST_OFF;
                g_roll = 0; g_pitch = 0; g_yaw_rate = 0;
                printf("\r[LAND] All drones landed!\n");
            }
        } else if (g_armed && g_triggers_were_both_pressed && !l2_pressed && !r2_pressed && g_both_triggers_released_ms == 0) {
            g_both_triggers_released_ms = now;
            g_one_trigger_released_ms = 0;
        } else if (g_both_triggers_released_ms > 0 && both_triggers_pressed) {
            g_both_triggers_released_ms = 0;
        } else if (g_both_triggers_released_ms > 0 && (now - g_both_triggers_released_ms) >= EMERGENCY_STOP_TIMEOUT_MS) {
            g_triggers_were_both_pressed = 0;
            g_both_triggers_released_ms = 0;
            g_armed = 0; g_thrust = 0; g_landing = 0;
            g_hover_thrust = CD_THRUST_OFF;
            g_roll = 0; g_pitch = 0; g_yaw_rate = 0;
            emergency_stop_all();
            printf("\r[EMERGENCY] All drones stopped!\n");
        } else if (g_armed && !g_landing && (l2_released || r2_released) && !both_triggers_pressed && g_both_triggers_released_ms == 0) {
            if (g_triggers_were_both_pressed && g_one_trigger_released_ms == 0)
                g_one_trigger_released_ms = now;
        } else if (g_one_trigger_released_ms > 0 && both_triggers_pressed) {
            g_one_trigger_released_ms = 0;
        } else if (g_one_trigger_released_ms > 0 && (now - g_one_trigger_released_ms) >= TRIGGER_GRACE_PERIOD_MS) {
            if (!g_landing && g_armed) {
                uint16_t land_from = g_hover_thrust;
                g_landing_duration_ms = 2500;
                g_landing_start_ms = now;
                g_landing = 1;
                g_one_trigger_released_ms = 0;
                g_triggers_were_both_pressed = 0;
                printf("\r[LAND] Landing %d drones from thrust=%u...\n", g_drone_count, land_from);
                land_all(land_from, g_landing_duration_ms);
            }
        }

        /* Assign thrust */
        if (g_armed && !g_landing && both_triggers_pressed) {
            g_thrust = g_hover_thrust;
        } else if (!g_armed || g_landing) {
            g_thrust = 0;
        }

        if (!g_landing) {
            g_yaw_rate = cmd_yaw;
        }

        g_roll = cd_clampf(cmd_roll, -CD_ROLL_LIMIT, CD_ROLL_LIMIT);
        g_pitch = cd_clampf(cmd_pitch, -CD_PITCH_LIMIT, CD_PITCH_LIMIT);
        g_yaw_rate = cd_clampf(g_yaw_rate, -CD_YAW_RATE_LIMIT, CD_YAW_RATE_LIMIT);

        /* Send setpoints to ALL drones */
        if (g_armed && !g_test_mode && !g_landing) {
            send_to_all_drones(g_roll, g_pitch, g_yaw_rate, g_thrust);
        } else if (!g_armed) {
            send_to_all_drones(0, 0, 0, CD_THRUST_OFF);
        }

        /* Update display */
        if (now - last_display >= 100) {
            print_status();
            last_display = now;
        }

        /* Keyboard fallback (non-blocking) */
        if (js_fd < 0) {
            fd_set rset;
            struct timeval tv = { 0, 0 };
            FD_ZERO(&rset);
            FD_SET(STDIN_FILENO, &rset);
            if (select(STDIN_FILENO + 1, &rset, NULL, NULL, &tv) > 0) {
                char key = 0;
                if (read(STDIN_FILENO, &key, 1) == 1) {
                    switch (key) {
                    case 'w': case 'W':
                        g_hover_thrust = cd_clamp_thrust((int)g_hover_thrust + CD_THRUST_STEP * 2);
                        if (g_hover_thrust > thrust_levels[g_thrust_level])
                            g_hover_thrust = thrust_levels[g_thrust_level];
                        break;
                    case 's': case 'S':
                        g_hover_thrust = cd_clamp_thrust((int)g_hover_thrust - CD_THRUST_STEP * 2);
                        break;
                    case 'a': case 'A': g_yaw_rate = -CTRL_YAW_CMD_DPS; break;
                    case 'd': case 'D': g_yaw_rate = CTRL_YAW_CMD_DPS; break;
                    case ' ': 
                        g_armed = !g_armed;
                        if (g_armed) {
                            arm_all(1000);
                            g_hover_thrust = 28000;
                        }
                        break;
                    case '\t':
                        g_thrust_level = (g_thrust_level + 1) % 4;
                        break;
                    case 'q': case 'Q': goto done;
                    case '\n': case '\r':
                        /* Enter = enable flight (like triggers) */
                        if (g_armed && !g_landing) {
                            g_thrust = g_hover_thrust;
                        }
                        break;
                    default: break;
                    }
                    if (g_armed && !g_landing) {
                        send_to_all_drones(g_roll, g_pitch, g_yaw_rate, g_thrust);
                    }
                }
            }
        }

        cd_sleep_ms(period);
    }

done:
    /* Emergency stop all drones */
    printf("\nStopping all drones...\n");
    emergency_stop_all();

    /* Cleanup */
    for (int i = 0; i < g_drone_count; i++) {
        if (g_drones[i]) cd_destroy(g_drones[i]);
    }
    if (js_fd >= 0) close(js_fd);

    printf("\n✅ All drones stopped. Goodbye!\n");
    return EXIT_SUCCESS;
}