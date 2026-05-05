# ESP32-S2-Drone Project

## Build Status
- **FIXED**: Linker DRAM overflow (11160 bytes over limit)
- **Solution**: Reduced task stack sizes to recover memory
- **Current**: Stabilizer = 8 * 512 = 4096 bytes, System = 6 * 512 = 3072 bytes
- **Build**: Successful, flashed successfully

## Build/Flash Commands
```bash
make build     # Compile only
make flash   # Build and flash (runs build automatically)
make monitor # View serial output (115200 baud) - may fail in container
```

## Common ESP32-S2 Issues
1. **Linker DRAM overflow**: BSS section exceeds 320KB
   - Solution: Reduce stack sizes, disable unused features
2. **Stack overflow in stabilizer**: Floating-point math needs more stack
   - Common crash during IMU/PID initialization
3. **Single-core priority conflicts**: Task timing more critical
4. **Serial monitor fails**: Use external adapter or `screen /dev/ttyUSB0 115200`

## Serial Debugging
- Baud: 115200
- Watch for: Guru Meditation, stack overflow, panic, TaskWatchdog
- Common crash: stabilizerTask, sensorsTask, IMU init

## Key Files
- `components/config/include/config.h` - Stack sizes
- `sdkconfig` - ESP-IDF config
- `sdkconfig.defaults.esp32s2` - ESP32-S2 defaults

## Fix History
- Initial: STABILIZER_TASK_STACKSIZE=32768 (32KB), SYSTEM=10*512 (5KB) - OVERFLOW
- Fix #1: Reduced to 6*512 (3KB each) - BUILT
- Fix #2: Increased stabilizer to 8*512 (4KB) - current