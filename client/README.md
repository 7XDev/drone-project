# CrazyDrone — C Library for Crazyflie / ESP-Drone Control

**CrazyDrone** is a lightweight C library for controlling Crazyflie and ESP-Drone quadcopters over **CRTP** (Crazyflie Real Time Protocol) via UDP/WiFi. It provides both low-level commander setpoints and high-level flight primitives with an intuitive API.

## Features

- ✈️ **Commander Control**: Send roll, pitch, yaw, and thrust setpoints at 50 Hz
- 🚁 **High-Level Flight Primitives**: Takeoff, land, hover, move, circle, figure-8, flip
- 📊 **CRTP Log Subsystem**: Read gyroscope, accelerometer, attitude, and battery data in real-time
- 🎮 **Interactive Control**: Raw keyboard input and gamepad support
- ⚡ **Speed Modes**: Slow, Normal, and Sport flight characteristics
- 🛑 **Safety**: Emergency stop, Ctrl-C handling, auto-land helpers
- 🔌 **Wide Compatibility**: Works with Crazyflie 2.x and ESP-Drone firmware

## Library Overview

The `lib/` directory contains:

- **`crazydrone.h`** — Public API header with all function declarations and constants
- **`crazydrone.c`** — Complete implementation (~1200 lines)

### Supported Hardware

- [Crazyflie 2.0/2.1](https://www.bitcraze.io/) — ~10g micro drone
- [ESP-Drone](https://github.com/espressif/esp-drone) — WiFi-enabled quad with ESP32

## Getting Started

### Build

The project uses a simple Makefile. Build everything with:

```bash
make                    # Build all examples
make examples/wasd_control        # Build just WASD version
make examples/controller_control  # Build gamepad version
make clean              # Clean build artifacts
```

**Requirements:**
- GCC (C11 standard)
- `libm` (math library)
- POSIX sockets + termios (Linux/macOS)

### Connection Setup

1. **WiFi Setup:**
   - Power on drone (it broadcasts `Crazyflie-XX` or similar SSID)
   - Connect computer to drone's WiFi network
   - Default IP: `192.168.43.42`, port `2390`

2. **Run an Example:**
   ```bash
   ./examples/wasd_control        # Fly with WASD keys
   ./examples/controller_control  # Fly with PS4 controller
   ```

### Basic Usage

```c
#include "lib/crazydrone.h"

int main() {
    // Create connection to drone
    CrazyDrone *drone = cd_create("192.168.43.42", 2390);
    
    if (!cd_connect(drone, 3000)) {
        fprintf(stderr, "Failed to connect\n");
        return 1;
    }
    
    // Setup sensor logging
    cd_setup_imu_logging(drone);
    
    // Arm and take off
    cd_arm(drone, 500);
    cd_takeoff(drone, CD_THRUST_HOVER, 2000);
    
    // Simple manual control loop
    cd_term_raw();
    while (cd_running(drone)) {
        int key = cd_getkey();
        if (key == 'w') cd_move_forward(drone, 10.0f, CD_THRUST_HOVER);
        if (key == 's') cd_move_backward(drone, 10.0f, CD_THRUST_HOVER);
        if (key == 'a') cd_move_left(drone, 10.0f, CD_THRUST_HOVER);
        if (key == 'd') cd_move_right(drone, 10.0f, CD_THRUST_HOVER);
        
        // Read sensor data
        cd_poll(drone);
        float gx, gy, gz;
        cd_get_gyro(drone, &gx, &gy, &gz);
        
        cd_sleep_ms(5);
    }
    cd_term_restore();
    
    cd_land(drone, CD_THRUST_HOVER, 1500);
    cd_destroy(drone);
    return 0;
}
```

## API Highlights

### Connection & Lifecycle
- `cd_create()` / `cd_destroy()` — Allocate/free drone handle
- `cd_connect()` — Verify WiFi link with CRTP ping
- `cd_running()` — Check if Ctrl-C was pressed

### Commander (Low-Level)
- `cd_send_setpoint()` — Send raw roll/pitch/yaw/thrust
- `cd_stop_motors()` — Emergency kill all motors

### High-Level Flight
- `cd_arm()` — Unlock ESC safety
- `cd_takeoff()` / `cd_land()` / `cd_hover()`
- `cd_move_forward()`, `cd_move_left()`, `cd_rotate_ccw()`
- `cd_fly_circle()` / `cd_fly_figure8()` / `cd_flip()`

### Sensor Logging
- `cd_setup_imu_logging()` — Scan TOC and setup gyro/accel/attitude blocks
- `cd_poll()` — Non-blocking receive of incoming packets
- `cd_get_gyro()` / `cd_get_attitude()` / `cd_get_accel()`
- `cd_get_imu()` — Full sensor snapshot

### Interactive Control
- `cd_term_raw()` / `cd_term_restore()` — Raw terminal mode
- `cd_getkey()` — Non-blocking keyboard input (includes arrow keys)

## How It Works Internally

### Network & CRTP Protocol

**Wire Format (11-31 bytes):**
```
┌─ CRTP Header (1 byte) ─┬──── Payload (0-30 bytes) ────┬─ Checksum (1 byte) ─┐
│ PORT(4) PORT(4)        │ Command-specific data         │ Byte-sum uint8     │
└────────────────────────┴───────────────────────────────┴────────────────────┘
```

- **CRTP Header** encodes port (0-15) and channel (0-3)
- **Ports** map to subsystems: Commander (3), Log (5), Param (2), Link (15)
- **Checksum** is simple byte-sum (uint8 wrap) for integrity
- UDP sent to `192.168.43.42:2390` (configurable)

### Socket Management

The `CrazyDrone` handle owns a UDP socket created per connection:

```c
struct CrazyDrone {
    int sock;                    // UDP socket file descriptor
    struct sockaddr_in addr;     // Drone address
    int connected;               // 1 if ping succeeded
    volatile int running;        // Cleared on SIGINT
    // ... state...
};
```

Signal handler captures `SIGINT` (Ctrl-C) and sets `running = 0`, allowing clean shutdown.

### Commander Subsystem

**Packet Layout (16 bytes total):**
```c
struct __attribute__((packed)) CdWirePacket {
    uint8_t  header;         // CRTP header for port 3, channel 0
    float    roll;           // ±30°
    float    pitch;          // ±30°
    float    yaw_rate;       // ±200°/s
    uint16_t thrust;         // 0-65535 (0=off, ~45k=hover)
    uint8_t  cksum;          // Checksum
};
```

Packets are sent at **50 Hz** (20ms intervals). Continuous setpoints maintain flight; no response needed.

### Log Subsystem (TOC Scanning)

The **CRTP Log** port (5) uses a **Table-of-Contents** protocol to discover available sensor variables:

**Phase 1: Enumerate TOC**
1. Send `CD_LOG_TOC_GET_INFO_V2` → Get total count (e.g., 127 variables)
2. Send individual `CD_LOG_TOC_GET_ITEM_V2` requests for IDs 0, 1, 2, … 
3. Parse responses: group name (e.g., "gyro"), variable name (e.g., "x"), type, ID

**Phase 2: Find Variables**
```
TOC Entry: { group: "gyro", name: "x", type: FLOAT (7), id: 42 }
TOC Entry: { group: "stabilizer", name: "attitude.roll", type: FLOAT, id: 89 }
```

**Phase 3: Create Log Blocks**
- Collect variable IDs for a "block" (e.g., gyro.x, gyro.y, gyro.z)
- Send `CD_LOG_CTRL_CREATE_BLOCK_V2` with array of IDs
- Start with `CD_LOG_CTRL_START_BLOCK` at period (e.g., 20ms)

**Phase 4: Receive Data**
Drone streams binary log packets containing stacked variable values:
```
[header][gyro.x: float][gyro.y: float][gyro.z: float][cksum]
```

Library parses type and converts FP16/int16/int32 to floats. Cache in `CdImuData` struct.

### Type Conversions

Log variables can be `UINT8`, `INT16`, `FLOAT`, `FP16` (half-precision), etc.

FP16 (half-precision float):
```c
// Extract sign, exponent, mantissa from 16-bit value
sign = bit 15
exp = bits 14-10 (5 bits, bias-15)
frac = bits 9-0 (10 bits)

// Convert to float with ldexp() and bit manipulation
```

### Speed Modes

Three preset configurations for different flight characteristics:

| Mode | Roll / Pitch | Yaw Rate | Thrust Step |
|------|-------------|----------|-------------|
| **Slow** | ±15° | 100°/s | 500 |
| **Normal** | ±30° | 200°/s | 2000 |
| **Sport** | ±45° | 350°/s | 5000 |

Keystroke increments are divided into small steps within limits.

### Terminal Control

Uses POSIX `termios` for raw input mode:
- Disable echo and line buffering
- Use `select()` for non-blocking key reads
- Parse **escape sequences** for arrow keys: `ESC[A` → `CD_KEY_UP`
- Restore on exit via signal handler

## Controller Control Example

The `examples/controller_control.c` program provides **PS4 gamepad flight control** (~934 lines).

### Hardware Setup

1. Connect PS4 controller via Bluetooth or USB
2. Appears as `/dev/input/js0` (joystick device)
3. Open with `open()`, read `struct js_event` packets

### Input Mapping

| Input | Function |
|-------|----------|
| **Left Joystick (Y)** | Height control (up=thrust up, down=thrust down) |
| **Right Joystick (Y)** | Forward / Backward (pitch) |
| **Right Joystick (X)** | Roll left / right |
| **L1 / R1** | Strafe left / right |
| **L2 + R2** (both) | Arm safety: must hold both to fly; release either → emergency stop |
| **Triangle (Y)** | Emergency stop |
| **Circle (B)** | Cycle thrust limit (30k → 40k → 50k → 65k) |
| **X / Square** | Forward/backward flip |
| **Options** | Cinematic stunt |
| **Touchpad / Back** | Toggle test mode (show inputs, don't send commands) |
| **PS / Xbox Button** | Quit |

### Deadzone Handling

```c
JOYSTICK_DEADZONE = 6000        // Ignore stick values < ±6000 / 32767
JOYSTICK_NEUTRAL_THRESHOLD = 0.15f  // Additional filter: < 15% is zero
JOYSTICK_COMMAND_MIN_THRESHOLD = 0.25f  // Need ≥25% to produce output
```

Prevents drift and accidental input.

### Safety Mechanisms

1. **Dual-Trigger Armed Mode:** Both L2 + R2 must be held. Release either → instant emergency stop
2. **Test Mode:** Toggle with Touchpad to preview inputs without drone movement
3. **Thrust Levels:** Cycle between safe limits (30k, 40k, 50k, 65k) to prevent runaway
4. **Emergency Stop Button:** Triangle instantly zeros all motors

### State Machine

```c
g_armed      // 1 if both triggers held
g_thrust     // 0 (disarmed) or target level
g_roll       // ±pitch from sticks, clamped to speed mode limits
g_pitch      // ±roll from sticks
g_yaw_rate   // Rotation speed (not yet fully bound to buttons)
```

Loop sends setpoints at ~50 Hz via `cd_send_setpoint()`.

### Debug & Tuning

- **Test Mode:** Press Touchpad to see stick/button values in real-time without flying
- **Gyro Debug:** Toggle with console commands to watch gyroscope feedback
- **Thrust Cycling:** Easily test different hover thrust levels without recompile

---

## Typical Workflow

1. **Connect:** `cd_connect()` sends ping, waits for response
2. **Setup Logging:** `cd_setup_imu_logging()` scans TOC (5-30 seconds)
3. **Arm:** `cd_arm()` sends low-thrust packets to unlock ESC
4. **Takeoff:** `cd_takeoff()` ramps thrust smoothly upward
5. **Control Loop:** Repeatedly call `cd_send_setpoint()` + `cd_poll()` + sensor reads
6. **Land:** `cd_land()` ramps thrust down
7. **Destroy:** `cd_destroy()` closes socket, restores terminal

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| No WiFi connection | Check SSID/password, verify drone WiFi enabled, ping IP |
| `cd_connect()` timeout | Drone not broadcasting, or wrong IP/port |
| Sensors unreliable | Rerun `cd_setup_imu_logging()`, may take 30+ seconds on weak link |
| Motor control feels sluggish | Check speed mode, increase send rate (currently 50 Hz fixed) |
| Gamepad not detected | Verify `/dev/input/js0` exists, try `jstest /dev/input/js0` |
| Terminal garbled after crash | Run `reset` in shell or call `cd_term_restore()` in signal handler |

---

## Files

```
.
├── Makefile                       # Build configuration
├── README.md                      # This file
├── lib/
│   ├── crazydrone.h              # Public API header (431 lines)
│   └── crazydrone.c              # Implementation (1210 lines)
└── examples/
    ├── wasd_control.c            # WASD keyboard flight
    ├── controller_control.c       # PS4 gamepad flight (934 lines)
    └── controller_test.c         # Gamepad input logger (minimal)
```

---

## License & Attribution

This library is compatible with **Crazyflie** (Bitcraze) and **ESP-Drone** (Espressif) open-source firmware.

**See also:**
- [Crazyflie Firmware](https://github.com/bitcraze/crazyflie-firmware)
- [ESP-Drone](https://github.com/espressif/esp-drone)

---

## Notes for Developers

- **Thread Safety:** Library is single-threaded. All functions assume synchronous calls from main thread.
- **Extensibility:** CRTP port system allows adding new subsystems (param tuning, position tracking, etc.)
- **Minimal Dependencies:** Uses only POSIX APIs and math library—no external libraries needed
- **Performance:** ~20ms round-trip, 50 Hz command rate suitable for manual flight

---

**Ready to fly! 🚁**
