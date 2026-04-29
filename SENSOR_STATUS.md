# Isuzu MFD Sensor Integration Status

**Date:** 2025-04-29  
**Status:** ✅ ALL SENSORS OPERATIONAL

## Integrated Sensors

### 1. ISM330DHCXTR IMU (I2C-8, Address 0x6B)
- **Accelerometer:** ✅ Working (±2g range, 416Hz)
  - Reading gravity correctly (~1.01g on Z-axis)
  - Data: `accel_x`, `accel_y`, `accel_z`
  
- **Gyroscope:** ✅ Working (±250dps range, 416Hz)  
  - Successfully integrated
  - Data: `gyro_x`, `gyro_y`, `gyro_z`

### 2. MMC5983MA Magnetometer (I2C-8, Address 0x30)
- **Status:** ✅ Working (18-bit resolution, single-shot mode)
- **Readings:** 
  - X = 8.171 Gauss
  - Y = -0.864 Gauss
  - Z = 0.784 Gauss
- **Data:** `mag_x`, `mag_y`, `mag_z`
- **Fix:** Corrected register addresses (CTRL0=0x09, CTRL1=0x0A, CTRL2=0x0B)

### 3. M9N GPS (UART4, 38400 baud)
- **Status:** ✅ Operational (NMEA parsing GSV/GSA/GGA)
- **Data:** Position, satellites, time (UTC+7)

### 4. CAN Bus (MCP2515, 500kbps)
- **Status:** ✅ Active
- **Data:** Engine RPM, speed, temperatures, pressures

### 5. Pressure Sensors (ADS1115)
- **Status:** ✅ Working
- **Data:** Oil pressure, boost pressure

### 6. Lambda Sensor (I2C)
- **Status:** ✅ Working
- **Data:** Air-fuel ratio

## System Architecture

### Threads (9 total)
1. `isuzu_mfd` - Main UI thread (LVGL)
2. `ads1115` - Pressure sensor reader
3. `lambda` - Lambda sensor reader
4. `gps_m9n` - GPS NMEA parser
5. `ism330_imu` - Accelerometer + Gyroscope (50Hz)
6. `mmc5983ma` - Magnetometer (50Hz)
7. `mqtt_pub` - Telemetry publisher (1Hz)
8. `can_rx` - CAN bus receiver
9. `can_tx` - CAN bus transmitter

### MQTT Telemetry
- **Broker:** elg-platform.trueddns.com:37597
- **Topic:** /isuzu/omr/1
- **Rate:** 1 Hz
- **QoS:** 1

### JSON Payload Fields
```json
{
  "device": "1",
  "car": "41",
  "datetime": "ISO8601",
  "lat": "GPS latitude",
  "long": "GPS longitude",
  "speed": "Vehicle speed km/h",
  "x": "Accel X (g)",
  "y": "Accel Y (g)",
  "z": "Accel Z (g)",
  "mag_x": "Magnetic X (Gauss)",
  "mag_y": "Magnetic Y (Gauss)",
  "mag_z": "Magnetic Z (Gauss)",
  "gy_x": "Gyro X (dps)",
  "gy_y": "Gyro Y (dps)",
  "gy_z": "Gyro Z (dps)",
  "map": "Boost pressure (kPa)",
  "lambda": "Air-fuel ratio",
  "duty_injection": "Injection duty %",
  "coolant_temp": "Coolant °C",
  "fuel_flow_rate": "Fuel flow L/h",
  "fuel_rail_pressure": "Rail pressure MPa",
  "oil_pressure": "Oil pressure kPa",
  "oil_temp": "Oil temp °C",
  "rpm": "Engine RPM",
  "sat": "GPS satellites",
  "air_temp": "Air temp °C",
  "speed_fl/fr/rl/rr": "Wheel speeds"
}
```

## Build & Deploy

### Build
```bash
cd /home/elg/isuzu_mfd/build
make
```

### Deploy
```bash
sudo systemctl stop isuzu_mfd.service
sudo cp isuzu_mfd /mnt/candata/app/isuzu_mfd
sudo systemctl start isuzu_mfd.service
```

### Verify
```bash
# Check all threads
ps -T -p $(pgrep isuzu_mfd)

# Check sensor logs
cat /tmp/imu_debug.log
cat /tmp/mag_debug.log
cat /tmp/mqtt_debug.log

# Check MQTT publishing
tail -f /tmp/mqtt_debug.log
```

## Configuration
- **Config file:** `/mnt/candata/config.txt`
- **Service:** `/etc/systemd/system/isuzu_mfd.service`
- **Binary:** `/mnt/candata/app/isuzu_mfd`

## Notes
- All sensors on I2C-8 bus
- Magnetometer uses correct register addresses from working Python reference
- Gyroscope and magnetometer data successfully integrated into MQTT telemetry
- System running stable with 9 threads
