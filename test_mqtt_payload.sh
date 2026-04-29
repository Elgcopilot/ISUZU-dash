#!/bin/bash
# Capture one MQTT message to verify sensor data
echo "Subscribing to MQTT topic for 10 seconds..."
echo "Looking for gy_x, gy_y, gy_z (gyroscope) and mag_x, mag_y, mag_z (magnetometer)..."
echo ""

timeout 10 mosquitto_sub -h elg-platform.trueddns.com -p 37597 -u s63 -P 57b468d811f8 -t /isuzu/omr/1 -C 1 2>/dev/null | python3 -m json.tool 2>/dev/null | grep -E "(gy_|mag_|\"x\"|\"y\"|\"z\")" | head -20

if [ $? -ne 0 ]; then
    echo "mosquitto_sub not available or connection failed"
    echo ""
    echo "Checking if MQTT is publishing from service logs:"
    sudo journalctl -u isuzu_mfd.service -n 5 --no-pager 2>/dev/null | grep -i mqtt || echo "No MQTT logs in service"
fi
