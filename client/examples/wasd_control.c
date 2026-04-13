/**
 * @file    wasd_control.c
 * @brief   Interactive drone flight with keyboard control
 *
 * Controls:
 *   W / S        — Forward / Backward  (pitch)
 *   A / D        — Left / Right        (roll)
 *   Arrow Up/Dn  — Increase / Decrease thrust (altitude)
 *   Arrow L/R    — Rotate left / right  (yaw)
 *   1 / 2 / 3 / 4 — Speed modes (+4 = forward speed test)
 *   SPACE        — Emergency stop (kills motors instantly)
 *   E            — Arm / Disarm toggle
 *   X            — Forward flip (salto) with upward boost
 *   C            — Backward flip (salto) with upward boost
 *   O            — Cinematic forward climb + dive + catch
 *   G            — Toggle gyro debug view
 *   Q            — Quit (auto-land first if flying)
 *
 * Build:  (use the project Makefile)
 * Run:    ./examples/wasd_control
 *
 * SAFETY: Keep far from people. Ensure clear space above drone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/crazydrone.h"

/* ── State ───────────────────────────────────────────────────────────────── */

static float    g_roll     = 0.0f;
static float    g_pitch    = 0.0f;
static float    g_yaw_rate = 0.0f;
static uint16_t g_thrust   = 0;
static uint16_t g_hover_thrust = CD_THRUST_OFF;
static int      g_armed    = 0;
static int      g_debug_gyro = 0;
static int      g_mode4_speedtest = 0;

static const char *speed_names[] = { "SLOW", "NORMAL", "SPORT" };

/* Fixed, balanced control tuning */
#define CTRL_BRAKE_MS              160
#define CTRL_BRAKE_FACTOR          0.45f

#define CTRL_TRANSLATION_CMD_SLOW_DEG     14.0f
#define CTRL_TRANSLATION_CMD_NORMAL_DEG   20.0f
#define CTRL_TRANSLATION_CMD_SPORT_DEG    25.0f
#define CTRL_FORWARD_CMD_MODE4_DEG         60.0f

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
/* Requested mode mapping:
 * - mode 1: slower
 * - mode 2: old mode 1 feel
 * - mode 3: old mode 2 feel
 */
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

/* ── Display ─────────────────────────────────────────────────────────────── */

static void print_status(CrazyDrone *drone)
{
    const CdImuData *imu = cd_get_imu(drone);
    uint32_t now = cd_now_ms();
    const char *mode_name = g_mode4_speedtest ? "SPEEDTEST" : speed_names[drone->speed_mode];

    /* Move cursor to top-left and clear screen */
    printf("\033[H\033[J");

    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║            WASD Drone Controller                ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Status: %-8s   Speed: %-8s              ║\n",
           g_armed ? "ARMED" : "DISARMED",
            mode_name);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Thrust: %5u / 65535                           ║\n", g_thrust);
    printf("║  Roll:   %+7.1f°    Pitch: %+7.1f°             ║\n",
           g_roll, g_pitch);
    printf("║  Yaw:    %+7.1f°/s                              ║\n", g_yaw_rate);
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
    printf("║  W/S = Fwd/Back   A/D = Left/Right              ║\n");
    printf("║  ↑/↓ = Climb/Descend  ←/→ = Yaw                 ║\n");
    printf("║  E = Arm/Disarm   1/2/3/4 = Speed Mode           ║\n");
    printf("║  X/C = Fwd/Bwd Flip  O = ClimbDive   Q = Quit    ║\n");
    printf("║  SPACE = Emergency Stop                          ║\n");
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
        printf("\r[FLIP] Arm the drone first (press E).\n");
        return;
    }

    if (g_hover_thrust < FLIP_MIN_THRUST) {
        printf("\r[FLIP] Increase altitude first (thrust too low).\n");
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
        printf("\r[FLIP] Arm the drone first (press E).\n");
        return;
    }

    if (g_hover_thrust < FLIP_MIN_THRUST) {
        printf("\r[FLIP] Increase altitude first (thrust too low).\n");
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
        printf("\r[STUNT] Arm the drone first (press E).\n");
        return;
    }

    if (g_hover_thrust < STUNT_MIN_THRUST) {
        printf("\r[STUNT] Increase altitude first (thrust too low).\n");
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

    /* Phase 1: fast forward climb (visual "goes up and forward") */
    cd_hover(drone, 0.0f, STUNT_CLIMB_PITCH_DEG, 0.0f, climb_thrust, STUNT_CLIMB_MS);

    /* Phase 2: at the top, rotate ~180 deg so nose points back toward pilot */
    int turn_ms = (int)((180.0f / STUNT_TURN_YAW_RATE_DPS) * 1000.0f);
    cd_hover(drone, 0.0f, 0.0f, STUNT_TURN_YAW_RATE_DPS, climb_thrust, turn_ms);
    cd_hover(drone, 0.0f, 0.0f, 0.0f, climb_thrust, STUNT_TURN_SETTLE_MS);

    /* Phase 3: snap into forward flip then nose-down dive */
    cd_flip(drone, 'p', climb_thrust);
    cd_hover(drone, 0.0f, STUNT_DIVE_PITCH_DEG, 0.0f, saved_hover, STUNT_DIVE_MS);

    /* Phase 4: catch hard and level out */
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

    cd_print_banner("WASD Drone Controller", drone->ip, drone->port);

    /* Increase control update rate to reduce input latency */
    drone->send_rate_hz = 100;
    cd_set_speed_mode(drone, CD_SPEED_SLOW);

    printf("Connecting to drone…\n");
    int connected = cd_connect(drone, 5000);
    if (!connected) {
        printf("Warning: No reply from drone. Continuing anyway.\n");
        printf("(Make sure you're connected to the drone's WiFi AP)\n\n");
    }

    /* Try to set up IMU logging */
    printf("Setting up IMU logging (this may take a moment)…\n");
    if (cd_setup_imu_logging(drone) < 0) {
        printf("Warning: IMU logging unavailable. Flying without sensor feedback.\n");
    }

    /* Enter raw terminal mode */
    cd_term_raw();

    printf("\nPress 'E' to arm the drone, then use ↑ to increase thrust.\n");
    printf("Press 'G' to toggle gyro debug info.\n");
    printf("Press 'Q' to quit.\n\n");
    cd_sleep_ms(2000);

    uint32_t last_display = 0;
    int period = 1000 / drone->send_rate_hz;

    int roll_dir = 0;
    int pitch_dir = 0;
    int yaw_dir = 0;
    uint32_t roll_until = 0;
    uint32_t pitch_until = 0;
    uint32_t yaw_until = 0;
    uint32_t roll_brake_until = 0;
    uint32_t pitch_brake_until = 0;
    float roll_brake = 0.0f;
    float pitch_brake = 0.0f;
    float prev_roll_cmd = 0.0f;
    float prev_pitch_cmd = 0.0f;
    float hold_roll_i = 0.0f;
    float hold_pitch_i = 0.0f;
    float filt_roll = 0.0f;
    float filt_pitch = 0.0f;
    float filt_gx = 0.0f;
    float filt_gy = 0.0f;
    int imu_filt_init = 0;
    uint32_t prev_imu_ts = 0;
    /* ── Main control loop ──────────────────────────────────────────────── */
    while (cd_running(drone)) {

        /* Process incoming sensor data */
        cd_poll(drone);

        /* Auto-recover IMU stream if packets stop arriving */
        ensure_imu_logging_fresh(drone);

        uint32_t now = cd_now_ms();

        int input_hold_ms = CTRL_INPUT_HOLD_NORMAL_MS;
        float translation_cmd_deg = CTRL_TRANSLATION_CMD_NORMAL_DEG;
        float forward_cmd_deg = CTRL_TRANSLATION_CMD_NORMAL_DEG;
        float yaw_cmd_dps = CTRL_YAW_CMD_NORMAL_DPS;
        if (drone->speed_mode == CD_SPEED_SLOW) {
            input_hold_ms = CTRL_INPUT_HOLD_SLOW_MS;
            translation_cmd_deg = CTRL_TRANSLATION_CMD_SLOW_DEG;
            forward_cmd_deg = CTRL_TRANSLATION_CMD_SLOW_DEG;
            yaw_cmd_dps = CTRL_YAW_CMD_SLOW_DPS;
        } else if (drone->speed_mode == CD_SPEED_SPORT) {
            input_hold_ms = CTRL_INPUT_HOLD_SPORT_MS;
            translation_cmd_deg = CTRL_TRANSLATION_CMD_SPORT_DEG;
            forward_cmd_deg = CTRL_TRANSLATION_CMD_SPORT_DEG;
            yaw_cmd_dps = CTRL_YAW_CMD_SPORT_DPS;
        }

        if (g_mode4_speedtest) {
            input_hold_ms = CTRL_INPUT_HOLD_SPORT_MS;
            translation_cmd_deg = CTRL_TRANSLATION_CMD_SPORT_DEG;
            forward_cmd_deg = CTRL_FORWARD_CMD_MODE4_DEG;
            yaw_cmd_dps = CTRL_YAW_CMD_SPORT_DPS;
        }

        /* Drain all pending keyboard input for lower latency */
        for (;;) {
            int key = cd_getkey();
            if (key == CD_KEY_NONE) break;

            switch (key) {
            /* ── Movement (balanced fixed speed) ─────────────────────── */
            case 'w': case 'W':
                pitch_dir = -1;
                pitch_until = now + (uint32_t)input_hold_ms;
                break;
            case 's': case 'S':
                pitch_dir = 1;
                pitch_until = now + (uint32_t)input_hold_ms;
                break;
            case 'a': case 'A':
                roll_dir = -1;
                roll_until = now + (uint32_t)input_hold_ms;
                break;
            case 'd': case 'D':
                roll_dir = 1;
                roll_until = now + (uint32_t)input_hold_ms;
                break;

            /* ── Thrust hold target (up/down) ────────────────────────── */
            case CD_KEY_UP:
                if (g_armed) {
                    const CdSpeedMode *spd = cd_get_speed(drone);
                    uint16_t step = spd ? spd->thrust_step : CD_THRUST_STEP;
                    if (g_hover_thrust == CD_THRUST_OFF) {
                        g_hover_thrust = CTRL_TAKEOFF_SETTLE_THRUST;
                    } else {
                        g_hover_thrust = cd_clamp_thrust((int)g_hover_thrust + (int)step);
                    }
                }
                break;
            case CD_KEY_DOWN:
                if (g_armed) {
                    const CdSpeedMode *spd = cd_get_speed(drone);
                    uint16_t step = spd ? spd->thrust_step : CD_THRUST_STEP;
                    g_hover_thrust = cd_clamp_thrust((int)g_hover_thrust - (int)step);
                }
                break;

            /* ── Yaw ──────────────────────────────────────────────────── */
            case CD_KEY_LEFT:
                yaw_dir = 1;
                yaw_until = now + (uint32_t)input_hold_ms;
                break;
            case CD_KEY_RIGHT:
                yaw_dir = -1;
                yaw_until = now + (uint32_t)input_hold_ms;
                break;

            /* ── Arm / disarm ─────────────────────────────────────────── */
            case 'e': case 'E':
                g_armed = !g_armed;
                if (g_armed) {
                    printf("\r[ARM] Arming — sending thrust=0 for 1s …\n");
                    cd_arm(drone, 1000);
                    g_hover_thrust = CD_THRUST_OFF;
                    g_thrust = CD_THRUST_OFF;
                } else {
                    g_thrust   = 0;
                    g_hover_thrust = CD_THRUST_OFF;
                    g_roll     = 0;
                    g_pitch    = 0;
                    g_yaw_rate = 0;
                    hold_roll_i = hold_pitch_i = 0.0f;
                    imu_filt_init = 0;
                    prev_imu_ts = 0;
                    roll_dir = pitch_dir = yaw_dir = 0;
                    roll_until = pitch_until = yaw_until = 0;
                    roll_brake_until = pitch_brake_until = 0;
                }
                break;

            /* ── Speed mode (display/profile only) ───────────────────── */
            case '1':
                cd_set_speed_mode(drone, CD_SPEED_SLOW);
                g_mode4_speedtest = 0;
                break;
            case '2':
                cd_set_speed_mode(drone, CD_SPEED_NORMAL);
                g_mode4_speedtest = 0;
                break;
            case '3':
                cd_set_speed_mode(drone, CD_SPEED_SPORT);
                g_mode4_speedtest = 0;
                break;
            case '4':
                g_mode4_speedtest = 1;
                cd_set_speed_mode(drone, CD_SPEED_SPORT);
                break;

            /* ── Debug ──────────────────────────────────────────────── */
            case 'g': case 'G':
                g_debug_gyro = !g_debug_gyro;
                break;

            /* ── Flip (salto) ───────────────────────────────────────── */
            case 'x': case 'X':
                do_upward_salto_flip(drone);
                break;
            case 'c': case 'C':
                do_upward_backward_salto_flip(drone);
                break;
            case 'o': case 'O':
                do_cinematic_climb_dive(drone);
                break;

            /* ── Emergency stop ───────────────────────────────────────── */
            case ' ':
                g_armed    = 0;
                g_thrust   = 0;
                g_roll     = 0;
                g_pitch    = 0;
                g_yaw_rate = 0;
                hold_roll_i = hold_pitch_i = 0.0f;
                imu_filt_init = 0;
                prev_imu_ts = 0;
                roll_dir = pitch_dir = yaw_dir = 0;
                roll_until = pitch_until = yaw_until = 0;
                roll_brake_until = pitch_brake_until = 0;
                cd_emergency_stop(drone);
                break;

            /* ── Quit ─────────────────────────────────────────────────── */
            case 'q': case 'Q':
                if (g_armed && g_thrust > 0) {
                    printf("\r[LAND] Landing before exit…\n");
                    cd_land(drone, g_thrust, 3000);
                }
                goto done;

            default:
                break;
            }
        }

        float cmd_roll = (now < roll_until) ? (float)roll_dir * translation_cmd_deg : 0.0f;
        float cmd_pitch = 0.0f;
        if (now < pitch_until) {
            if (pitch_dir < 0) {
                cmd_pitch = -(forward_cmd_deg);
            } else {
                cmd_pitch = translation_cmd_deg;
            }
        }
        float cmd_yaw = (now < yaw_until) ? (float)yaw_dir * yaw_cmd_dps : 0.0f;

        /* Brief opposite command on release to stop drift faster */
        if (fabsf(prev_roll_cmd) > 0.1f && fabsf(cmd_roll) < 0.1f) {
            roll_brake = -prev_roll_cmd * CTRL_BRAKE_FACTOR;
            roll_brake_until = now + CTRL_BRAKE_MS;
        }
        if (fabsf(prev_pitch_cmd) > 0.1f && fabsf(cmd_pitch) < 0.1f) {
            pitch_brake = -prev_pitch_cmd * CTRL_BRAKE_FACTOR;
            pitch_brake_until = now + CTRL_BRAKE_MS;
        }

        if (now >= roll_brake_until) roll_brake = 0.0f;
        if (now >= pitch_brake_until) pitch_brake = 0.0f;

        if (fabsf(cmd_roll) > 0.1f) {
            roll_brake = 0.0f;
            roll_brake_until = 0;
        }
        if (fabsf(cmd_pitch) > 0.1f) {
            pitch_brake = 0.0f;
            pitch_brake_until = 0;
        }

        g_roll = cmd_roll + roll_brake;
        g_pitch = cmd_pitch + pitch_brake;
        g_yaw_rate = cmd_yaw;

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

            if (fabsf(cmd_roll) < 0.1f) {
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
            if (fabsf(cmd_pitch) < 0.1f) {
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

        if (g_armed) {
            g_thrust = g_hover_thrust;
        } else {
            g_thrust = 0;
        }

        /* Send setpoint if armed */
        if (g_armed) {
            cd_send_setpoint(drone, g_roll, g_pitch, g_yaw_rate, g_thrust);
        } else {
            cd_send_setpoint(drone, 0, 0, 0, CD_THRUST_OFF);
        }

        /* Update display every 100ms */
        if (now - last_display >= 100) {
            print_status(drone);
            last_display = now;
        }

        cd_sleep_ms(period);
    }

done:
    /* Ensure motors are stopped */
    cd_emergency_stop(drone);
    cd_term_restore();

    printf("\nGoodbye!\n");

    cd_destroy(drone);
    return EXIT_SUCCESS;
}
