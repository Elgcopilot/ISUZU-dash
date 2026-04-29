#!/bin/bash
# Quick test to verify magnetometer and gyroscope data
echo "Testing MMC5983MA and ISM330DHCXTR sensors..."
echo ""

# Test magnetometer on I2C-8 at 0x30
echo "1. Testing MMC5983MA Magnetometer (0x30):"
sudo i2cget -y 8 0x30 0x2F 2>/dev/null
if [ $? -eq 0 ]; then
    PRODUCT_ID=$(sudo i2cget -y 8 0x30 0x2F 2>/dev/null)
    echo "   Product ID: $PRODUCT_ID (expected 0x30)"
    if [ "$PRODUCT_ID" = "0x30" ]; then
        echo "   ✓ MMC5983MA detected successfully"
    else
        echo "   ✗ Product ID mismatch"
    fi
else
    echo "   ✗ Failed to communicate with MMC5983MA"
fi

echo ""
echo "2. Testing ISM330DHCXTR IMU (0x6B):"
sudo i2cget -y 8 0x6b 0x0F 2>/dev/null
if [ $? -eq 0 ]; then
    WHO_AM_I=$(sudo i2cget -y 8 0x6b 0x0F 2>/dev/null)
    echo "   WHO_AM_I: $WHO_AM_I (expected 0x6b)"
    if [ "$WHO_AM_I" = "0x6b" ]; then
        echo "   ✓ ISM330DHCXTR detected successfully"
    else
        echo "   ✗ WHO_AM_I mismatch"
    fi
else
    echo "   ✗ Failed to communicate with ISM330DHCXTR"
fi

echo ""
echo "3. Reading raw magnetometer data (X0, Y0, Z0):"
X0=$(sudo i2cget -y 8 0x30 0x00 2>/dev/null)
Y0=$(sudo i2cget -y 8 0x30 0x02 2>/dev/null)
Z0=$(sudo i2cget -y 8 0x30 0x04 2>/dev/null)
echo "   X0: $X0, Y0: $Y0, Z0: $Z0"

echo ""
echo "4. Reading raw gyroscope data (X_L, Y_L, Z_L):"
GX=$(sudo i2cget -y 8 0x6b 0x22 2>/dev/null)
GY=$(sudo i2cget -y 8 0x6b 0x24 2>/dev/null)
GZ=$(sudo i2cget -y 8 0x6b 0x26 2>/dev/null)
echo "   Gyro X_L: $GX, Y_L: $GY, Z_L: $GZ"

echo ""
echo "Sensor test complete!"
