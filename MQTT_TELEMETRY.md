# Isuzu Dashboard MQTT Telemetry Contract

This document describes the MQTT telemetry currently published by the Isuzu dashboard application. It is intended for backend integration.

## Transport

| Setting | Value |
| --- | --- |
| Protocol | MQTT over TCP |
| Broker address | `tcp://<SERVER>:<PORT>` |
| Credentials | Username and password from the device configuration |
| Client ID | `isuzu_device_<device>` |
| Topic | `/isuzu/omr/<device>` |
| QoS | `0` (at most once) |
| Retained | `false` |
| Encoding | UTF-8 JSON |
| Nominal publish period | 40 ms (up to 25 Hz) |
| Publish condition | MQTT connected (temporary testing mode; publishes at RPM 0) |

`<device>`, broker address, port, credentials, and car number are loaded from `/mnt/candata/config.txt`.

### Topic example

For device `6`:

```text
/isuzu/omr/6
```

## Message format

Each message is one JSON object.

```json
{
  "device": 6,
  "car": "",
  "datetime": "03/08/26 12:34:56",
  "lat": 13.756321,
  "lon": 100.501847,
  "speed": 82.50,
  "x": 0.120000,
  "y": -0.040000,
  "z": 0.000000,
  "mag_x": 0.010000,
  "mag_y": -0.020000,
  "mag_z": 0.410000,
  "gy_x": 0.050000,
  "gy_y": 0.010000,
  "gy_z": -0.020000,
  "map": 184.00,
  "lambda": 1.02,
  "even": "pt",
  "class": "isuzu-omr",
  "hr": null,
  "duty_injection": 34.50,
  "battery_voltage": 13.80,
  "throttle": 67,
  "coolant_temp": 88.0,
  "fuel_flow_rate": 8.20,
  "fuel_rail_pressure": 102.50,
  "oil_pressure": 0.00,
  "oil_temp": 96.0,
  "rpm": 3250,
  "sat": 6,
  "air_temp": 35,
  "speed_fl": 82.40,
  "speed_fr": 82.60,
  "speed_rl": 82.30,
  "speed_rr": 82.70,
  "drs": null
}
```

## Field reference

| Field | JSON type | Unit | Source / meaning |
| --- | --- | --- | --- |
| `device` | number | — | Configured device ID; also appears in topic. |
| `car` | string | — | Configured car number; can be empty. |
| `datetime` | string | `DD/MM/YY HH:MM:SS` UTC | GPS UTC time when available; otherwise the device system clock in UTC. The date is always from the device system clock in UTC. |
| `lat` | number | decimal degrees | GPS latitude. It is `0.0` until the GPS has a valid position. |
| `lon` | number | decimal degrees | GPS longitude. It is `0.0` until the GPS has a valid position. |
| `speed` | number | km/h | Average of the four wheel-speed values. Not GPS speed. |
| `x` | number | g | Lateral acceleration from the IMU. |
| `y` | number | g | Longitudinal acceleration from the IMU. |
| `z` | number | g | Currently fixed at `0.0`; vertical acceleration is not published. |
| `mag_x` | number | gauss | Magnetometer X axis. |
| `mag_y` | number | gauss | Magnetometer Y axis. |
| `mag_z` | number | gauss | Magnetometer Z axis. |
| `gy_x` | number | sensor units | Gyroscope X axis. |
| `gy_y` | number | sensor units | Gyroscope Y axis. |
| `gy_z` | number | sensor units | Gyroscope Z axis. |
| `map` | number | kPa | Boost/MAP value from the ADS1115 physical pressure sensor. |
| `lambda` | number | lambda ratio | Lambda sensor value from I2C sensor input. |
| `even` | string | — | Current literal value is `"pt"`. **This is spelled `even` in the current payload.** |
| `class` | string | — | Current literal value is `"isuzu-omr"`. |
| `hr` | null | — | Reserved; always `null`. |
| `duty_injection` | number | % | Injection duty cycle. |
| `battery_voltage` | number | V | Vehicle/control-module voltage. |
| `throttle` | number | % | Accelerator pedal position. |
| `coolant_temp` | number | °C | Engine coolant temperature. |
| `fuel_flow_rate` | number | L/h | Engine fuel rate. |
| `fuel_rail_pressure` | number | MPa | Fuel rail pressure. |
| `oil_pressure` | number | unspecified | Currently no active data source; typically `0.0`. |
| `oil_temp` | number | °C | Engine oil temperature. |
| `rpm` | number | RPM | Engine speed. |
| `sat` | number | count | GPS satellites used by the receiver. `0` means no usable GPS position solution. |
| `air_temp` | number | °C | Intake-air temperature. |
| `speed_fl` | number | km/h | Front-left wheel speed. |
| `speed_fr` | number | km/h | Front-right wheel speed. |
| `speed_rl` | number | km/h | Rear-left wheel speed. |
| `speed_rr` | number | km/h | Rear-right wheel speed. |
| `drs` | null | — | Reserved; always `null`. |

## Backend handling guidance

1. Use `device` and/or the final topic segment as the device identity.
2. Treat MQTT QoS 0 as lossy. Messages can be dropped or arrive late; do not depend on every 40 ms sample.
3. Consider a device offline when no message arrives for a backend-defined timeout, such as 5–10 seconds while the engine is expected to run.
4. Do not treat `lat`/`lon` as valid solely because the fields exist. Require `sat > 0` and apply backend-side validation such as geographic bounds and movement plausibility.
5. Store ingestion time in addition to `datetime`; it is always UTC but is not ISO 8601, and its date comes from the device system clock.
6. Preserve the exact field name `even` until a coordinated dashboard/backend schema migration changes it to `event`.
7. Treat `hr`, `drs`, and `oil_pressure` as reserved or currently unsupported fields.

## Current limitations

- MQTT publishing stops when `rpm <= 0`; the backend will not receive parked/engine-off telemetry.
- There is no schema version field yet.
- There is no explicit GPS validity boolean; use `sat` and coordinate validation.
- GPS speed, top speed, lap timing, SCX controls, and maximum boost are currently dashboard features and are not included in this MQTT payload.

## Recommended future schema changes

For a backward-compatible next version, add:

```json
{
  "schema_version": 2,
  "event": "pt",
  "gps_valid": true,
  "gps_speed_kmh": 117.6,
  "top_speed_kmh": 154.2,
  "lap": 3,
  "lap_time_s": 58.421,
  "delta_time_s": -0.213,
  "max_boost_kpa": 221.0,
  "timestamp_utc": "2026-08-03T12:34:56Z"
}
```

Do not remove or rename existing fields without coordinating a migration with backend consumers.
