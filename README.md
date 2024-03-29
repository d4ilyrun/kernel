# Kernel

This is a mere hobby kernel for 32-bit x86, to learn about OS/Kernel development.

I'm learning from [osdev](www.osdev.org), adding features in no particular order, just implementing
stuff I want to learn more about.

## Dependencies

* Meson
* Ninja
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
$ meson setup build --cross-file scripts/meson_cross.ini --reconfigure [-Dbuildtype=debug]
$ ninja -C build iso      # builds the kernel.iso file
$ ninja -C build qemu     # boots up qemu using the iso file
```

## Testing

I don't know how I can test kernel code, so all tests are performed manually for the time being ...


```bash
$ ninja -C build qemu-server    # boots up a qemu server open for GDB connections
$ gdb -x .gdbinit build/kernel/kernel.sym
```

## What's available ?

- GDT segments
- Interrupts
- Logging
- PIT & Timer

## Things I'd like to learn/implement

- [X] Interrupts
- Memory management
    - [X] Paging
    - [X] PMM (physical)
    - [ ] VMM (virtual)
- [ ] Scheduler
- [ ] Filesystem
- [ ] Syscalls
- User interaction
    - [X] Keyboard
    - [ ] Mouse
    - [ ] Screen
- [ ] Porting an already existing program (Can it run DOOM?)
- [ ] Multitasking
- [ ] Semaphores
