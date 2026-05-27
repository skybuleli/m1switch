#!/bin/bash
# ── M1Switch E2E Test — 一键构建脚本 ────────────────────────
# 设置 devkitPro 环境变量，构建所有测试 NRO。
# 用法: ./build_all.sh [clean]
#───────────────────────────────────────────────────────────────

set -euo pipefail

# 设置 devkitPro 路径
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"

# 添加工具链到 PATH
export PATH="$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== M1Switch E2E Test NRO Builder ==="
echo "DEVKITPRO=$DEVKITPRO"
echo ""

# 检查工具链
if [ ! -x "$DEVKITA64/bin/aarch64-none-elf-gcc" ]; then
    echo "ERROR: devkitA64 compiler not found at $DEVKITA64/bin/aarch64-none-elf-gcc"
    exit 1
fi

# 清理
if [ "${1:-}" = "clean" ]; then
    echo "Cleaning..."
    rm -rf "$BUILD_DIR"
    for dir in "$SCRIPT_DIR"/l0_* "$SCRIPT_DIR"/l1_*; do
        [ -d "$dir" ] && rm -rf "$dir/build" "$dir"/*.nro "$dir"/*.elf "$dir"/*.map 2>/dev/null || true
    done
    echo "Done."
    exit 0
fi

# 构建
FAILED=0
BUILT=0
mkdir -p "$BUILD_DIR"

for dir in "$SCRIPT_DIR"/l0_*; do
    test_name="$(basename "$dir")"
    if [ ! -f "$dir/Makefile" ]; then
        echo "  [SKIP] $test_name (no Makefile)"
        continue
    fi

    echo -n "  [BUILD] $test_name ... "
    LOG="/tmp/nro_build_$$.log"
    if make -C "$dir" all DEVKITPRO="$DEVKITPRO" > "$LOG" 2>&1; then
        cp "$dir/build/$test_name.nro" "$BUILD_DIR/"
        echo "OK"
        BUILT=$((BUILT + 1))
    else
        echo "FAIL"
        tail -5 "$LOG" | sed 's/^/    /'
        FAILED=$((FAILED + 1))
    fi
done

# L1 tests: not yet ready (need libnx service fixes)
# For now just list them
echo ""
echo "=== L1 Tests (libnx-based, need service fixes) ==="
for dir in "$SCRIPT_DIR"/l1_*; do
    test_name="$(basename "$dir")"
    echo "  $test_name — pending (libnx AM/applet init)"
done

echo ""
echo "=== Summary ==="
echo "  Built:   $BUILT"
echo "  Failed:  $FAILED"
echo "  Output:  $BUILD_DIR/"
echo ""

[ "$FAILED" -gt 0 ] && exit 1
echo "All L0 tests built successfully!"
