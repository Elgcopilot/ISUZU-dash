# Isuzu MFD Dashboard

Embedded Linux dashboard and racing telemetry application for an Isuzu D-Max installation.

It combines an LVGL 800 × 480 dashboard, CAN vehicle telemetry, an SCX Hub controller, GPS, I2C sensors, MQTT telemetry, CSV session logging, and GPS-polygon lap timing.

## Documentation

- [Complete project documentation](PROJECT_DOCUMENTATION.md) — architecture, hardware, configuration, build, deployment, diagnostics, SCX, GPS, lap timing, CSV, and MQTT operation.
- [MQTT telemetry contract](MQTT_TELEMETRY.md) — broker configuration, topic, QoS, JSON fields, units, and backend integration guidance.

## Key features

- **CAN0:** Isuzu vehicle telemetry and OBD polling at 500 kbit/s.
- **CAN1 / SCX Hub:** page navigation from SCX buttons and RPM reporting at 500 kbit/s.
- **Sensors:** ADS1115 boost pressure, lambda input, ISM330DHCXTR IMU, MMC5983MA magnetometer, and M9N/N9M GPS.
- **Dashboard:** three LVGL pages for engine data, temperatures/GPS satellites, and racing data including lap time.
- **GPS laps:** configurable `START` and `FINISH` quadrilateral zones from `/mnt/candata/config.txt`.
- **MQTT:** JSON telemetry published to `/isuzu/omr/<device>`.
- **CSV:** one log file per application start under `/mnt/candata/data/`.

## Build

```text
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

The built executable is `build/isuzu_mfd`.

## Deploy

```text
sudo install -m 0755 build/isuzu_mfd /mnt/candata/app/isuzu_mfd
sudo systemctl restart isuzu_mfd.service
sudo systemctl status isuzu_mfd.service
```

Runtime settings are read from `/mnt/candata/config.txt`. Do not commit real MQTT passwords or other production credentials.
