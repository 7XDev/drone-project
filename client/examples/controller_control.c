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
 *   L1 / R1 buttons          — Sideways movement (strafe left/right)
 *   L2 + R2 triggers (both)  — REQUIRED to fly; release either = EMERGENCY STOP
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
        
        /* L1/R1: Strafe sideways */
        float strafe_cmd = 0.0f;
        if (g_button_l1) {
            strafe_cmd = -translation_cmd_deg;
        } else if (g_button_r1) {
            strafe_cmd = translation_cmd_deg;
        }

        /* ────── Handle analog inputs with exponential curves ────────── */

        /* Left stick Y: Height (up=UP, down=DOWN) */
        float left_y_norm = normalize_axis(g_stick_left_y);
        if (g_armed && fabsf(left_y_norm) > JOYSTICK_NEUTRAL_THRESHOLD) {
            /* Exponential curve for height control */
            float exp_height = apply_exponential_curve(left_y_norm, 2.0f);
            
            const CdSpeedMode *spd = cd_get_speed(drone);
            uint16_t step = spd ? spd->thrust_step : CD_THRUST_STEP;
            
            if (exp_height < 0.0f) {
                /* Stick up = increase height */
                if (g_hover_thrust == CD_THRUST_OFF) {
                    g_hover_thrust = CTRL_TAKEOFF_SETTLE_THRUST;
                } else {
                    g_hover_thrust = cd_clamp_thrust((int)g_hover_thrust + (int)step);
                }
            } else if (exp_height > 0.0f) {
                /* Stick down = decrease height */
                g_hover_thrust = cd_clamp_thrust((int)g_hover_thrust - (int)step);
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

        /* Right stick X: Left/Right (roll) + L1/R1 strafe */
        float right_x_norm = normalize_axis(g_stick_right_x);
        float cmd_roll = 0.0f;
        if (fabsf(right_x_norm) > JOYSTICK_COMMAND_MIN_THRESHOLD) {
            float exp_roll = apply_exponential_curve(right_x_norm, 1.8f);
            cmd_roll = exp_roll * translation_cmd_deg;
        }
        
        /* If L1 or R1 pressed, use strafe instead */
        if (g_button_l1 || g_button_r1) {
            cmd_roll = strafe_cmd;
        }

        /* Triggers L2/R2: YAW DISABLED (reserved for arm safety) */
        /* TODO: Implement yaw on different buttons when needed */
        float cmd_yaw = 0.0f;

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

        /* Check if both L2 and R2 triggers are pressed (required to fly) */
        int both_triggers_pressed = (g_trigger_l2 > JOYSTICK_DEADZONE) && (g_trigger_r2 > JOYSTICK_DEADZONE);
        
        if (g_armed && !both_triggers_pressed) {
            /* Emergency shutdown: both triggers released */
            g_armed = 0;
            g_thrust = 0;
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
            printf("\r[EMERGENCY] Both triggers released! Motors stopped!\n");
        }

        if (g_armed && both_triggers_pressed) {
            g_thrust = g_hover_thrust;
        } else {
            g_thrust = 0;
        }

        /* Send setpoint if armed AND not in test mode */
        if (g_armed && !g_test_mode) {
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
    close(js_fd);

    printf("\nGoodbye!\n");

    cd_destroy(drone);
    return EXIT_SUCCESS;
}
