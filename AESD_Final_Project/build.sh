#!/bin/bash
set -e
TOPDIR=$(dirname "$(realpath "$0")")

export BR2_EXTERNAL="${TOPDIR}/mnet_external"
BUILDROOT_DIR="${TOPDIR}/buildroot"

echo "[INFO] Cleaning MNET, BME280, and LCD_1602 build directories..."
make -C "${BUILDROOT_DIR}" mnet-dirclean || true
make -C "${BUILDROOT_DIR}" bme280-dirclean || true
make -C "${BUILDROOT_DIR}" lcd_1602-dirclean || true

if [ ! -f "${BUILDROOT_DIR}/.config" ]; then
    echo "[INFO] No existing Buildroot config found. Applying default defconfig..."
    make -C "${BUILDROOT_DIR}" raspberrypi4_64_defconfig
fi

if ! grep -q "BR2_PACKAGE_MNET=y" "${BUILDROOT_DIR}/.config" 2>/dev/null; then
    echo "[INFO] Enabling MNET external package..."
    echo "BR2_PACKAGE_MNET=y" >> "${BUILDROOT_DIR}/.config"
    make -C "${BUILDROOT_DIR}" olddefconfig
fi

if ! grep -q "BR2_PACKAGE_BME280=y" "${BUILDROOT_DIR}/.config" 2>/dev/null; then
    echo "[INFO] Enabling BME280 driver external package..."
    echo "BR2_PACKAGE_BME280=y" >> "${BUILDROOT_DIR}/.config"
    make -C "${BUILDROOT_DIR}" olddefconfig
fi

if ! grep -q "BR2_PACKAGE_LCD_1602=y" "${BUILDROOT_DIR}/.config" 2>/dev/null; then
    echo "[INFO] Enabling LCD_1602 userspace app..."
    echo "BR2_PACKAGE_LCD_1602=y" >> "${BUILDROOT_DIR}/.config"
    make -C "${BUILDROOT_DIR}" olddefconfig
fi

make -C "${BUILDROOT_DIR}" olddefconfig

echo "[INFO] Starting Buildroot build..."
make -C "${BUILDROOT_DIR}" -j"$(nproc)"

echo "[INFO] Build completed successfully!"

