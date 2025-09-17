# Kernel

This is a mere hobby kernel for 32-bit x86, to learn about OS/Kernel development.

I'm  adding features in no particular order, just implementing stuff I want to learn
more about and what seems more fun at the moment.

## Dependencies

* Make
* mtools (to generate an iso file)
* QEMU

### Nix

If you're using a the `Nix` package manager, a development environment is ready for you to use inside the flake.

```bash
$ nix develop .#kernel
```

> [!WARNING]
>
> This requires flakes support

## Building

```bash
$ # Use our custom toolchain for building
$ export CROSS_COMPILE=toolchain/opt/i686-dailyrun/bin/i686-dailyrun-
$ export ARCH=i686               # The architecture's name
$ make iso      # builds the kernel.iso file
$ make qemu     # boots up qemu using the iso file
```

### Toolchain

If you do not already have an available `i686-elf` toolchain, a custom GCC target for this kernel (i686-dailyrun)
is available for you to build inside the `toolchain` directory. Note that using this toolchain is required when
building userland executables destined to be used with this kernel.

```bash
$ make gcc TARGET=i686-dailyrun ARCH= CROSS_COMPILE= GCC_CONFIGURE_FLAGS='--with-newlib'
$ make libc ARCH=i686 CROSS_COMPILE=
```

## Testing

I don't know how I can test kernel code, so all tests are performed manually for the time being ...

```bash
$ make qemu-server    # boots up a qemu server open for GDB connections
$ gdb -x .gdbinit build/kernel/kernel.sym
```

You can also run unit-tests on our libraries:
```bash
$ make tests
```

## Documentation

I try to keep the codebase well-documented. You can generate the full doc using doxygen:
```bash
$ make docs # Requires you to also pull the git submodules
```

## Things I'd like to learn

- Memory management
    - [X] Paging
    - [X] Copy-on-Write
    - [X] Page cache
    - [ ] Shared pages
    - [ ] Guard pages
- Drivers
    - [X] Timer
    - [ ] Mouse
    - [ ] Loadable kernel modules
- Scheduling
    - [X] Multitasking
    - [X] Semaphores
    - [ ] Priority
    - [ ] Multiprocessor
- Networking
    - [X] Ethernet
    - [ ] IP
        - [X] ARP
        - [ ] IP segmentation
    - [X] Berkley sockets
    - [ ] TCP
    - [ ] UDP
    - [ ] NTP
    - [ ] NAT
- Filesystems
    - [X] VFS
    - [ ] ext2
    - [ ] NFS
- Porting
    - [X] Load/Execute ELF programs
    - [ ] Porting an already existing program (Can it run DOOM?)
    - [ ] Dynamic ELF relocation
- Misc
    - [ ] IPC
    - [ ] SMP
    - [ ] POSIX syscalls compatibility
