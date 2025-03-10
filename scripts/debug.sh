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

echo "[INFO] Starting a debugging session"
make qemu-server || exit
gdb --symbol ./build/kernel/kernel.sym \
    -iex "set pagination of" \
    -iex "target remote localhost:1234" \
    "${BREAKPOINTS[@]}" \
    -x "${GITROOT}/.gdbinit" \
    -ex "continue"

echo "[INFO] Terminating debugging session"
pkill qemu
