#!/bin/bash
# RawAccel Linux Uninstaller
set -e

echo "=== RawAccel Linux Uninstaller ==="
echo ""

if [[ $EUID -ne 0 ]]; then
    echo "ERROR: Please run as root: sudo $0"
    exit 1
fi

# Stop and disable service
if systemctl is-active --quiet rawaccel 2>/dev/null; then
    echo "[1/4] Stopping service..."
    systemctl stop rawaccel
fi
if systemctl is-enabled --quiet rawaccel 2>/dev/null; then
    echo "      Disabling service..."
    systemctl disable rawaccel
fi

# Remove service file
echo "[2/4] Removing service file..."
rm -f /etc/systemd/system/rawaccel.service
systemctl daemon-reload

# Remove binaries
echo "[3/4] Removing binaries..."
rm -f /usr/local/bin/rawaccel-daemon
rm -f /usr/local/bin/rawaccel-cli
rm -f /usr/local/bin/rawaccel-gui

# Remove system files
echo "[4/4] Removing system files..."
rm -f /etc/udev/rules.d/99-rawaccel.rules
rm -f /etc/modules-load.d/rawaccel.conf
rm -f /usr/share/applications/rawaccel.desktop
rm -f /usr/share/polkit-1/actions/org.rawaccel.policy
rm -f /usr/share/polkit-1/rules.d/49-rawaccel.rules
udevadm control --reload-rules 2>/dev/null || true

# Keep /etc/rawaccel/settings.json so config is not lost
echo ""
echo "=== Uninstall complete! ==="
echo ""
echo "NOTE: /etc/rawaccel/settings.json was kept (your config)."
echo "      To remove it too: sudo rm -rf /etc/rawaccel"
echo ""
echo "NOTE: User config ~/.config/rawaccel/ was not touched."
