#!/bin/bash

# Script to load the I2C LCD1602 driver module

set -e

MODULE_NAME="lcd1602"
DEVICE_NAME="lcd1602"
KERNEL_VERSION=$(uname -r)

echo "Loading ${MODULE_NAME} module..."

# Remove the module if already loaded
if lsmod | grep -q "${MODULE_NAME}"; then
    echo "Removing existing ${MODULE_NAME} module..."
    rmmod "${MODULE_NAME}"
fi

# Install the module
if [ -e /lib/modules/$kernel_ver/extra/$module.ko ]; then
    echo "Installing ${MODULE_NAME} module..."
    insmod /lib/modules/$KERNEL_VERSION/extra/${MODULE_NAME}.ko
else
    echo "Module not found in /lib/modules/$KERNEL_VERSION/extra/, attempting modprobe"
    modprobe $MODULE_NAME || exit 1
fi

# Verify the module is loaded
if lsmod | grep -q "${MODULE_NAME}"; then
    echo "${MODULE_NAME} module loaded successfully!"
else
    echo "Error: Failed to load ${MODULE_NAME} module"
    exit 1
fi

# Display module information
echo "==============================="
echo "Module information:"
lsmod | grep "${MODULE_NAME}"

echo ""
echo "Module parameters and sysfs entries:"
if [ -d "/sys/module/${MODULE_NAME}" ]; then
    ls -la "/sys/module/${MODULE_NAME}/" 2>/dev/null || echo "No sysfs entries yet"
fi

echo ""
echo "I2C device information:"
if [ -d "/sys/bus/i2c/devices" ]; then
    ls -la /sys/bus/i2c/devices/ 2>/dev/null || echo "No I2C devices found"
fi

echo ""
echo "Device file:"
if [ -e "/dev/${DEVICE_NAME}" ]; then
    ls -la "/dev/${DEVICE_NAME}"
else
    echo "/dev/${DEVICE_NAME} not found (may need to be created manually)"
fi

echo ""
echo "Kernel messages:"
dmesg | tail -5
