#!/bin/bash
# ── M1Switch E2E Test — 一键构建脚本 ────────────────────────
# 设置 devkitPro 环境变量，构建所有测试 NRO。
# 用法: ./build_all.sh [clean]
#───────────────────────────────────────────────────────────────

set -euo pipefail

# 设置 devkitPro 路径
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export LIBNX="${LIBNX:-$DEVKITPRO/libnx}"

# 添加工具链到 PATH
export PATH="$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== M1Switch E2E Test NRO Builder ==="
echo "DEVKITPRO=$DEVKITPRO"
echo "DEVKITA64=$DEVKITA64"
echo "CC=$DEVKITA64/bin/aarch64-none-elf-gcc"
echo ""

# 检查工具链
if [ ! -x "$DEVKITA64/bin/aarch64-none-elf-gcc" ]; then
    echo "ERROR: devkitA64 compiler not found at $DEVKITA64/bin/aarch64-none-elf-gcc"
    echo "Set DEVKITPRO to the correct path."
    exit 1
fi

# 清理
if [ "${1:-}" = "clean" ]; then
    echo "Cleaning..."
    rm -rf "$BUILD_DIR"
    for dir in "$SCRIPT_DIR"/l0_* "$SCRIPT_DIR"/l1_*; do
        if [ -d "$dir" ]; then
            (cd "$dir" && rm -rf build *.nro *.elf *.map 2>/dev/null || true)
        fi
    done
    echo "Done."
    exit 0
fi

# 构建
FAILED=0
BUILT=0
SKIPPED=0

mkdir -p "$BUILD_DIR"

for dir in "$SCRIPT_DIR"/l0_* "$SCRIPT_DIR"/l1_*; do
    test_name="$(basename "$dir")"
    if [ ! -f "$dir/Makefile" ]; then
        echo "  [SKIP] $test_name (no Makefile)"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    echo -n "  [BUILD] $test_name ... "
    if (cd "$dir" && make --no-print-directory all 2>&1) > /tmp/nro_build_$$.log; then
        # 复制产物
        if [ -f "$dir/build/$test_name.nro" ]; then
            cp "$dir/build/$test_name.nro" "$BUILD_DIR/"
            echo "OK -> $BUILD_DIR/$test_name.nro"
            BUILT=$((BUILT + 1))
        elif [ -f "$dir/$test_name.nro" ]; then
            cp "$dir/$test_name.nro" "$BUILD_DIR/"
            echo "OK -> $BUILD_DIR/$test_name.nro"
            BUILT=$((BUILT + 1))
        else
            echo "OK (no NRO found at $dir/build/...)"
            BUILT=$((BUILT + 1))
        fi
    else
        echo "FAIL"
        cat /tmp/nro_build_$$.log | tail -20
        FAILED=$((FAILED + 1))
    fi
done

rm -f /tmp/nro_build_$$.log

echo ""
echo "=== Summary ==="
echo "  Built:   $BUILT"
echo "  Failed:  $FAILED"
echo "  Skipped: $SKIPPED"
echo "  Output:  $BUILD_DIR/"
echo ""

if [ "$FAILED" -gt 0 ]; then
    echo "Some tests FAILED. Check the logs above."
    exit 1
fi

echo "All tests built successfully!"
