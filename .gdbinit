# Connect to qemu's gdbserver
target remote :1234

# Change layout
layout reg

# Continue execution until entering the kernel
break kernel_main
continue
