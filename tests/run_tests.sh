#!/bin/bash
# RawAccel Linux — Test çalıştırıcı
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BIN="$ROOT/build-manual/test_accel"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -I$ROOT/include -I$ROOT/src"

echo "=== RawAccel Linux Birim Testleri ==="
echo "Derleniyor..."

# config.cpp ayrı derleme birimi olarak derlenir (M2: ODR sorununu önler)
$CXX $CXXFLAGS \
    "$ROOT/tests/test_accel.cpp" \
    "$ROOT/src/config.cpp" \
    -o "$BIN"

echo "Çalıştırılıyor..."
echo ""
# Forward any CLI args (e.g. --filter, --list, --quiet) to the test binary
"$BIN" "$@"

# ── CLI davranış kapıları (P83: create-preset 256-char senkronu) ──────────────
CLI="$ROOT/build-manual/rawaccel-cli"
if [ -x "$CLI" ]; then
    TMPCFG=$(mktemp)
    rm -f "$TMPCFG"   # P42: var olan bos config uzerine yazilmaz; dosya yokken seed olusur
    TMPN256=$(mktemp)
    TMPN257=$(mktemp)
    python3 - "$TMPN256" "$TMPN257" <<'PY'
import sys
open(sys.argv[1], 'w').write('a' * (256))
open(sys.argv[2], 'w').write('a' * (257))
PY
    # Geçerli bir seed config olustur (bos dosya uzerine P42 reddeder)
    set +e
    "$CLI" -c "$TMPCFG" --no-daemon create-preset office seed >/dev/null 2>&1
    SRC=$?
    set -e
    if [ $SRC -ne 0 ]; then
        echo "FAIL: seed config baslatilamadi (rc=$SRC)"
        exit 1
    fi
    # 256 char: kabul (create-preset siniri MAX_NAME_LEN = 256)
    set +e
    OUT=$("$CLI" -c "$TMPCFG" --no-daemon create-preset cs2 "$(cat "$TMPN256")" 2>&1)
    RC=$?
    set -e
    if [ $RC -ne 0 ] || ! echo "$OUT" | grep -qE "Created profile|updated"; then
        echo "FAIL: 256-char create-preset rejected (rc=$RC): $OUT"
        exit 1
    fi
    # 257 char: reddedilmeli ("too long")
    set +e
    OUT=$("$CLI" -c "$TMPCFG" --no-daemon create-preset cs2 "$(cat "$TMPN257")" 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -qiE "too long"; then
        echo "FAIL: 257-char create-preset accepted (rc=$RC): $OUT"
        exit 1
    fi
    echo "CLI create-preset ad kapısı: 256 OK, 257 red ✓ (P83)"
    rm -f "$TMPCFG" "$TMPN256" "$TMPN257"

    # ── P99: arity + -c "" kapıları ───────────────────────────────────────────
    # Ekstra argümanlar sessizce yutulmuyor; hepsi rc=1 + usage. "-c ''" reel
    # config'e düşüp onu değiştirmemeli (P74 BULGU-2 sınıfı).
    TMPA=$(mktemp)
    rm -f "$TMPA"
    set +e
    OUT=$("$CLI" -c "$TMPA" --no-daemon create pro >/dev/null 2>&1; "$CLI" -c "$TMPA" --no-daemon set pro BONUS 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "takes at most"; then
        echo "FAIL: P99 extra-arg set rejected (rc=$RC): $OUT"
        exit 1
    fi
    ACTIVE=$("$CLI" -c "$TMPA" --no-daemon list | sed -n 's/^Active profile: //p')
    if [ "$ACTIVE" != "default" ]; then
        echo "FAIL: P99 extra-arg set mutated config (active=$ACTIVE)"
        exit 1
    fi
    set +e
    OUT=$("$CLI" -c "$TMPA" --no-daemon create-preset cs2 a b c 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "takes at most"; then
        echo "FAIL: P99 extra-arg create-preset rejected (rc=$RC): $OUT"
        exit 1
    fi
    set +e
    OUT=$("$CLI" -c "" --no-daemon status 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "non-empty path"; then
        echo "FAIL: P99 -c '' not rejected (rc=$RC): $OUT"
        exit 1
    fi
    echo "CLI P99 arity/-c\"\" kapısı: ekstra-arg red + -c\"\" red ✓"
    rm -f "$TMPA"
fi
