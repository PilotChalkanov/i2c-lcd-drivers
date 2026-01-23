#!/bin/bash

# Test script for lcd1602 driver probe and remove

set -e

DRIVER_DIR="../driver"

echo "===== Testing lcd1602 Driver Probe and Remove ====="
echo ""

# Test 1: Load the module
echo "TEST 1: Loading the driver module..."
echo "---"
cd "${DRIVER_DIR}"
bash lcd1602_load.sh
LOAD_RESULT=$?

if [ $LOAD_RESULT -eq 0 ]; then
    echo "✓ Driver loaded successfully"

    echo ""
    echo "Verifying device files:"
    echo "---"
    ls -la /dev/lcd1602 2>/dev/null || echo "/dev/lcd1602 not found"
    echo ""
    echo "I2C device registration:"
    ls -la /sys/bus/i2c/devices/ 2>/dev/null || echo "No I2C devices found"
    echo ""
    echo "Module directory:"
    ls -la /sys/module/lcd1602/ 2>/dev/null || echo "Module sysfs not found"
else
    echo "✗ Failed to load driver"
    exit 1
fi

echo ""
echo "Press Enter to continue with unload test..."
read -r

# Test 2: Unload the module
echo ""
echo "TEST 2: Unloading the driver module..."
echo "---"
bash lcd1602_unload.sh
UNLOAD_RESULT=$?

if [ $UNLOAD_RESULT -eq 0 ]; then
    echo "✓ Driver unloaded successfully"
else
    echo "✗ Failed to unload driver"
    exit 1
fi

echo ""
echo "===== All Tests Passed ====="
