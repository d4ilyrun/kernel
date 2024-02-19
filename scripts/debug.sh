#!/usr/bin/env bash

# Start a GDB session connected to our kernel.
# You can also pass in symbols to set breakpoints on as arguments.
#
# example: ./debug.sh kernel_main mmu_init

# Absolute path to this script
SCRIPT=$(readlink -f "$0")
SCRIPTDIR=$(dirname "$SCRIPT")
GITROOT="$SCRIPTDIR/.."

if ! command -v gdb > /dev/null; then
    echo "[ERROR] GDB not found in path" >&2
    exit 127
fi

cd "$GITROOT" || exit

BREAKPOINTS=()
for symbol in "$@"; do
    BREAKPOINTS+=("-ex" "break $symbol")
done

if [ ! -d "build" ]; then
    echo "[INFO] build dir not found, setting up meson project"
    meson setup build --cross-file "scripts/meson_cross.ini" --reconfigure -Dbuildtype=debug
fi

echo "[INFO] Starting a debugging session"
ninja -C build qemu-server || exit
gdb --symbol ./build/kernel/kernel.sym \
    -iex "set pagination of" \
    -iex "target remote localhost:1234" \
    "${BREAKPOINTS[@]}" \
    -ex "continue"

echo "[INFO] Terminating debugging session"
pkill qemu
