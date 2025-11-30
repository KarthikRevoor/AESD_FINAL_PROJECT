#!/bin/bash
set -e
TOPDIR=$(dirname "$(realpath "$0")")

# Use external tree (mnet_external)
export BR2_EXTERNAL="${TOPDIR}/mnet_external"
BUILDROOT_DIR="${TOPDIR}/buildroot"

echo "[INFO] Cleaning MNET, BME280, and LCD_1602 build directories..."
make -C "${BUILDROOT_DIR}" mnet-dirclean || true
make -C "${BUILDROOT_DIR}" bme280-dirclean || true
make -C "${BUILDROOT_DIR}" lcd_1602-dirclean || true

# Step 1: Apply default config only if .config doesn’t exist
if [ ! -f "${BUILDROOT_DIR}/.config" ]; then
    echo "[INFO] No existing Buildroot config found. Applying default defconfig..."
    make -C "${BUILDROOT_DIR}" raspberrypi4_64_defconfig
fi

# Step 2: Ensure MNET stays enabled
if ! grep -q "BR2_PACKAGE_MNET=y" "${BUILDROOT_DIR}/.config" 2>/dev/null; then
    echo "[INFO] Enabling MNET external package..."
    echo "BR2_PACKAGE_MNET=y" >> "${BUILDROOT_DIR}/.config"
    make -C "${BUILDROOT_DIR}" olddefconfig
fi

# Step 3: Ensure BME280 stays enabled
if ! grep -q "BR2_PACKAGE_BME280=y" "${BUILDROOT_DIR}/.config" 2>/dev/null; then
    echo "[INFO] Enabling BME280 driver external package..."
    echo "BR2_PACKAGE_BME280=y" >> "${BUILDROOT_DIR}/.config"
    make -C "${BUILDROOT_DIR}" olddefconfig
fi

# Step 4: Ensure LCD_1602 userspace app stays enabled
if ! grep -q "BR2_PACKAGE_LCD_1602=y" "${BUILDROOT_DIR}/.config" 2>/dev/null; then
    echo "[INFO] Enabling LCD_1602 userspace app..."
    echo "BR2_PACKAGE_LCD_1602=y" >> "${BUILDROOT_DIR}/.config"
    make -C "${BUILDROOT_DIR}" olddefconfig
fi

# Step 5: Finalize config
make -C "${BUILDROOT_DIR}" olddefconfig

# Step 6: Build
echo "[INFO] Starting Buildroot build..."
make -C "${BUILDROOT_DIR}" -j"$(nproc)"

echo "[INFO] Build completed successfully!"

