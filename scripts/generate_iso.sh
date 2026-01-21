#!/usr/bin/env bash

if [ $# -lt 3 ]; then
    echo "Usage: ./generate_iso.sh <isofile> <kernel> <initramfs>" >&2
    exit 1
fi

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

KERNEL_ISO="$1"
KERNEL_BIN="$2"
INITRAMFS="$3"

# Assert that the given kernel binary is multiboot compliant.
# args:
#   $1 - path to the kernel executable
function check_multiboot()
{
    if ! grub-file --is-x86-multiboot2 "$1"; then
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
    touch "$iso_dir/boot/initramfs.tar" # In case it is not present, at least have an empty initramfs

    # Align the size of the initramfs onto that of a page (required by initrd).
    echo cp "${INITRAMFS}"  "$iso_dir/boot/initramfs.tar"
    cp "$INITRAMFS"  "$iso_dir/boot/initramfs.tar"
    truncate -s %4096 "$iso_dir/boot/initramfs.tar"

    # Add a custom multiboot entry for grub to be able to boot our kernel
    cat <<EOF > "$iso_dir/boot/grub/grub.cfg"
set timeout=0
menuentry "Kernel - ${KERNEL_BIN%.*}" {
    multiboot2 /boot/$(basename "$KERNEL_BIN")
    module2 /boot/initramfs.tar
}
EOF

    grub-mkrescue -o "$KERNEL_ISO" "$iso_dir"
}

if ! "$SCRIPT_DIR"/extract_kernel_symbols.py "$KERNEL_BIN" 131072; then
    echo "Failed to process kernel binary files. Stopping"
    exit 1
fi

check_multiboot "$KERNEL_BIN"
generate_iso
