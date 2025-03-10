# Kernel

This is a mere hobby kernel for 32-bit x86, to learn about OS/Kernel development.

I'm learning from [osdev](www.osdev.org), adding features in no particular order, just implementing
stuff I want to learn more about.

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
$ export CROSS_COMPILE=i686-elf- # The target architecturea toolchain's prefix
$ export ARCH=i686               # The architecture's name
$ make iso      # builds the kernel.iso file
$ make qemu     # boots up qemu using the iso file
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

- [X] Interrupts
- Memory management
    - [X] Paging
    - [X] VMM (virtual)
    - [ ] CoW
    - [ ] Guard pages
- Drivers
    - [X] Timer
    - [ ] Mouse
    - [ ] Networking
    - [ ] Loadable kernel modules
- Scheduling
    - [X] Multitasking
    - [ ] Priority
    - [ ] Multiprocessor
- Filesystems
    - [ ] ext2
    - [X] VFS
- Userland
    - [X] jumping to userland
    - [X] syscalls
- Porting
    - [ ] Load/Execute ELF programs
    - [ ] Porting an already existing program (Can it run DOOM?)
    - [ ] Dynamic ELF relocation
- [ ] IPC
- [ ] SMP
