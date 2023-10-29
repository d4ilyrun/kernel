#!/usr/bin/env bash

if [ $# -lt 2 ]; then
    echo "Usage: ./generate_iso.sh <kernel> <isofile>" >&2
    exit 1
fi

KERNEL_BIN="$1"
KERNEL_ISO="$2"

# Assert that the given kernel binary is multiboot compliant.
# args:
#   $1 - path to the kernel executable
function check_multiboot()
{
    if ! grub-file --is-x86-multiboot "$1"; then
        echo "error: The provided kernel executable is not multiboot compliant ($1)" >&2
        exit 2
    fi
}

function generate_iso()
{
    local iso_dir

    iso_dir="$(mktemp -d)"

    echo "Generating $KERNEL_ISO from $KERNEL_BIN ..."

    # Create a minimal boot partition for grub to be able to generate an iso file
    mkdir -p "$iso_dir/boot/grub"
    cp "$KERNEL_BIN" "$iso_dir/boot/$(basename "$KERNEL_BIN")"

    # Add a custom multiboot entry for grub to be able to boot our kernel
    cat <<EOF > "$iso_dir/boot/grub/grub.cfg"
menuentry "Kernel - ${KERNEL_BIN%.*}" {
    multiboot /boot/$(basename "$KERNEL_BIN")
}
EOF

    grub-mkrescue -o "$KERNEL_ISO" "$iso_dir"
}

# Split the binary file into two separate parts:
# - The raw binary, stripped from any unneeded debug symbol
# - The symbol definitions, used by GDB
#
# This step is necessary, as unneeded symbols would make the
# resulting binary not multiboot compliant.
function split_debug_symbols()
{
    local symbols
    local kernel

    kernel="$1"
    symbols="${kernel%.*}.sym"

    echo "Moving debug symbols into $symbols"
	objcopy --only-keep-debug "$kernel" "$symbols"
	objcopy --strip-unneeded "$kernel"
}

split_debug_symbols "$KERNEL_BIN"
check_multiboot "$KERNEL_BIN"
generate_iso
