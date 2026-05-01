#!/bin/bash
# kde-fix-accel.sh — Disable KDE Plasma mouse acceleration for RawAccel
#
# KDE applies its own libinput acceleration curve on top of your mouse input.
# When RawAccel is running, this causes double-acceleration (two curves stacked).
# This script sets Pointer Acceleration to "Flat" (profile=1) in kwinrc
# and reloads KWin input settings immediately — no logout required.
#
# Usage:
#   bash scripts/kde-fix-accel.sh          # fix + reload
#   bash scripts/kde-fix-accel.sh --check  # check current state only
#   bash scripts/kde-fix-accel.sh --undo   # restore adaptive acceleration

set -euo pipefail

# ── Config file location ──────────────────────────────────────────────────────
KWINRC="${XDG_CONFIG_HOME:-$HOME/.config}/kwinrc"

# ── Helpers ───────────────────────────────────────────────────────────────────

die() { echo "ERROR: $*" >&2; exit 1; }

check_kde() {
    local desktop="${XDG_CURRENT_DESKTOP:-}"
    local session="${DESKTOP_SESSION:-}"
    if [[ "$desktop" == *KDE* || "$desktop" == *plasma* ||
          "$session" == *plasma* || "$session" == *kde* ||
          -f "$KWINRC" ]]; then
        return 0
    fi
    return 1
}

get_current_profile() {
    # Returns the PointerAccelerationProfile value from [Libinput] section, or "" if not set.
    awk '/^\[Libinput\]/{found=1; next} /^\[/{if(found) exit} found && /^PointerAccelerationProfile=/{print $0; exit}' \
        "$KWINRC" 2>/dev/null | cut -d= -f2
}

write_kwinrc() {
    local profile="$1"  # 1 = flat, 2 = adaptive
    local accel="$2"    # 0 for flat, -0.5 or 0 for adaptive

    # Create backup
    [[ -f "$KWINRC" ]] && cp "$KWINRC" "$KWINRC.rawaccel-backup.$(date +%Y%m%d-%H%M%S)"

    # Use python3 for clean INI manipulation (preserves comments/sections)
    if command -v python3 &>/dev/null; then
        python3 - "$KWINRC" "$profile" "$accel" << 'PYEOF'
import sys, os, configparser

kwinrc, profile, accel = sys.argv[1], sys.argv[2], sys.argv[3]
cfg = configparser.RawConfigParser()
cfg.optionxform = str  # preserve key case

if os.path.exists(kwinrc):
    cfg.read(kwinrc)

if "Libinput" not in cfg:
    cfg["Libinput"] = {}
cfg["Libinput"]["PointerAccelerationProfile"] = profile
cfg["Libinput"]["PointerAcceleration"] = accel

tmp = kwinrc + ".tmp"
with open(tmp, "w") as f:
    cfg.write(f, space_around_delimiters=False)
os.rename(tmp, kwinrc)
print(f"  Updated {kwinrc}")
PYEOF
    else
        # Fallback: manual INI editing with sed + append
        if grep -q '^\[Libinput\]' "$KWINRC" 2>/dev/null; then
            sed -i "/^\[Libinput\]/,/^\[/{
                s/^PointerAccelerationProfile=.*/PointerAccelerationProfile=$profile/
                s/^PointerAcceleration=.*/PointerAcceleration=$accel/
            }" "$KWINRC"
            # If keys not present in section, append them
            if ! grep -A20 '^\[Libinput\]' "$KWINRC" | grep -q '^PointerAccelerationProfile='; then
                sed -i "/^\[Libinput\]/a PointerAccelerationProfile=$profile" "$KWINRC"
            fi
            if ! grep -A20 '^\[Libinput\]' "$KWINRC" | grep -q '^PointerAcceleration='; then
                sed -i "/^\[Libinput\]/a PointerAcceleration=$accel" "$KWINRC"
            fi
        else
            printf '\n[Libinput]\nPointerAccelerationProfile=%s\nPointerAcceleration=%s\n' \
                "$profile" "$accel" >> "$KWINRC"
        fi
    fi
}

reload_kwin() {
    echo "  Reloading KWin input settings..."
    local reloaded=0
    for cmd in qdbus6 qdbus; do
        if command -v "$cmd" &>/dev/null; then
            if "$cmd" org.kde.KWin /KWin reconfigure 2>/dev/null; then
                echo "  ✓ KWin reconfigured via $cmd."
                reloaded=1
                break
            fi
        fi
    done
    if [[ $reloaded -eq 0 ]]; then
        echo "  ℹ Could not reach KWin D-Bus. Changes will apply on next KWin start."
        echo "    (If running under Wayland, log out and back in.)"
    fi
}

# ── Main ──────────────────────────────────────────────────────────────────────

MODE="${1:---fix}"

case "$MODE" in
--check)
    echo "=== KDE Acceleration State Check ==="
    if ! check_kde; then
        echo "  Not a KDE session (kwinrc not found). Nothing to do."
        exit 0
    fi
    PROFILE=$(get_current_profile)
    echo "  kwinrc: $KWINRC"
    echo "  PointerAccelerationProfile: '${PROFILE:-not set}'"
    if [[ "$PROFILE" == "1" ]]; then
        echo "  ✓ Flat (disabled) — correct for RawAccel."
        exit 0
    else
        echo "  ⚠ NOT flat — double-acceleration will occur with RawAccel!"
        echo "  Run: bash scripts/kde-fix-accel.sh"
        exit 1
    fi
    ;;

--undo)
    echo "=== Restoring KDE adaptive acceleration ==="
    if ! check_kde; then
        echo "  Not a KDE session. Nothing to do."; exit 0
    fi
    write_kwinrc "2" "-0.5"
    reload_kwin
    echo "  ✓ Restored adaptive acceleration (profile=2, accel=-0.5)."
    echo "  Note: adjust the exact value in System Settings → Input Devices → Mouse."
    ;;

--fix|*)
    echo "=== KDE RawAccel Fix: Disable Pointer Acceleration ==="
    if ! check_kde; then
        echo "  Not a KDE session (XDG_CURRENT_DESKTOP=$XDG_CURRENT_DESKTOP)."
        echo "  This script is only needed on KDE Plasma. Exiting."
        exit 0
    fi

    PROFILE=$(get_current_profile)
    if [[ "$PROFILE" == "1" ]]; then
        echo "  ✓ Pointer acceleration is already Flat. No change needed."
        exit 0
    fi

    echo "  Current profile: '${PROFILE:-not set}'"
    echo "  Setting PointerAccelerationProfile=1 (Flat) in: $KWINRC"
    write_kwinrc "1" "0"
    reload_kwin
    echo ""
    echo "  ✓ Done. KDE will no longer apply an extra acceleration curve."
    echo "  RawAccel is now the sole acceleration provider."
    echo ""
    echo "  To verify: bash scripts/kde-fix-accel.sh --check"
    echo "  To undo:   bash scripts/kde-fix-accel.sh --undo"
    ;;
esac
