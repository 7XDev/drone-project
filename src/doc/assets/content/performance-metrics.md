# Performance Metrics
Here estimates about weight, thrust, electronical consumption and flight time for the drone itselft can be found.

## Electronical consumption
#wh Diclaimer
#wb The results might not represent acutal values in real world applications. Estimates are based on our research sources found on on the credits page.

### Parts powered by the battery
| Part | Idle Power Cunsumption | Ø Power Consumption | Peak Power Cusumtion |
|------|------------------------|---------------------|----------------------|
| ESP32-CAM Microcontroller | 80 mA | 100 mA | 160 mA |
| Happy Model SE 0802 Motors (16000KV, 4x) | 9.5 A | 12.5 A | 14.2 A |

### Sensors
| Part | Idle Power Cunsumption | Ø Power Consumption | Peak Power Cusumtion |
|------|------------------------|---------------------|----------------------|
| GY-521 MPU6050 | 1.5 mA | 3.9mA | - |
| TFmini-S Lidar Sensor | 0.01 mA | 181 mA | - |
| OV2640 | - | 6 mA | 10 mA |
| MLX90640ESF-BAB | - | 15 mA | 20 mA | 

### Calculations
#cal
Hover:                          9500 + 80 + 1.5 = 9581.5 mA

Operation (without sensors):    12500 + 115 + 3.9 = 12618.9 mA
Operation (with sensors):       9500 + 115 + 3.9 + 181 + 10 + 20 = 12829.9 mA

Peak:                           16000 + 160 + 3.9 + 181 + 10 + 20 = 16374.9 mA
#cal

## Conclusion
The battery can discharge `22 A` continously and `44 A` on burst. The peak drone power draw at `~16.5 A` is **well below** the continous recommended discharge value.

## Flight time

#cal

#cal