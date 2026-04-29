#!/bin/bash
# Monitor MQTT messages to see actual sensor data values
echo "Monitoring MQTT messages for 15 seconds..."
echo "Looking for mag_x, mag_y, mag_z, gy_x, gy_y, gy_z values..."
echo ""

# Use mosquitto_sub to listen to MQTT topic
timeout 15 mosquitto_sub -h elg-platform.trueddns.com -p 37597 -u s63 -P 57b468d811f8 -t /isuzu/omr/1 -C 2 | jq -r '.mag_x, .mag_y, .mag_z, .gy_x, .gy_y, .gy_z' 2>/dev/null || echo "mosquitto_sub not available, checking logs instead..."

if [ $? -ne 0 ]; then
    echo "Fallback: Checking service logs for sensor initialization..."
    sudo journalctl -u isuzu_mfd.service -n 50 --no-pager | grep -iE "(mmc|mag|gyro|ism330)" || echo "No sensor logs found"
fi
