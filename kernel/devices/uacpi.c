/**
 * @brief uACPI - Kernel API implementation
 *
 * uACPI relies on kernel-specific API to do things like mapping/unmapping
 * memory, writing/reading to/from IO, PCI config space, and many more things.
 * It is our job to implement these function in order to use the library.
 *
 * https://github.com/UltraOS/uACPI?tab=readme-ov-file#3-implement-kernel-api
 */

#include <kernel/cpu.h>
#include <kernel/devices/timer.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>
#include <kernel/vmm.h>

#include <libalgo/queue.h>
#include <uacpi/kernel_api.h>
#include <utils/macro.h>
#include <utils/map.h>
#include <utils/math.h>

#include <stdarg.h>
#include <string.h>

#ifndef UACPI_FORMATTED_LOGGING
#error UACPI_FORMATTED_LOGGING must be defined
#endif

void *uacpi_kernel_alloc(uacpi_size size)
{
    return kmalloc(size, KMALLOC_DEFAULT);
}

void *uacpi_kernel_calloc(uacpi_size count, uacpi_size size)
{
    return kcalloc(count, size, KMALLOC_DEFAULT);
}

void uacpi_kernel_free(void *mem)
{
    kfree(mem);
}

void *uacpi_kernel_map(uacpi_phys_addr physical, uacpi_size len)
{
    // Avoid problems when the area spans over 2 different pageframes
    len += physical & __align_mask(physical, PAGE_SIZE);

    vaddr_t virtual = vmm_allocate(&kernel_vmm, 0, len, VMA_READ | VMA_WRITE);
    if (!virtual)
        return UACPI_NULL;

    paddr_t pageframe = align_down(physical, PAGE_SIZE);

    for (uacpi_size off = 0; off < len; off += PAGE_SIZE) {
        if (!mmu_map(virtual + off, pageframe + off, PROT_READ | PROT_WRITE)) {
            vmm_free(&kernel_vmm, virtual, len);
            return UACPI_NULL;
        }
    }

    return (void *)virtual + (physical - pageframe);
}

void uacpi_kernel_unmap(void *addr, uacpi_size len)
{
    for (uacpi_size off = 0; off < len; off += PAGE_SIZE) {
        mmu_unmap((vaddr_t)addr + off);
    }

    vmm_free(&kernel_vmm, (vaddr_t)addr, len);
}

uacpi_status uacpi_kernel_raw_memory_read(uacpi_phys_addr address,
                                          uacpi_u8 byte_width,
                                          uacpi_u64 *out_value)
{
    uacpi_status status = UACPI_STATUS_OK;

    void *ptr = uacpi_kernel_map(address, byte_width);
    if (ptr == NULL)
        return UACPI_STATUS_OUT_OF_MEMORY;

    switch (byte_width) {
    case 1:
        *out_value = *(volatile u8 *)ptr;
        break;
    case 2:
        *out_value = *(volatile u16 *)ptr;
        break;
    case 4:
        *out_value = *(volatile u32 *)ptr;
        break;
    case 8:
        *out_value = *(volatile u64 *)ptr;
        break;
    default:
        status = UACPI_STATUS_INVALID_ARGUMENT;
        break;
    }

    uacpi_kernel_unmap(ptr, byte_width);

    return status;
}

uacpi_status uacpi_kernel_raw_memory_write(uacpi_phys_addr address,
                                           uacpi_u8 byte_width,
                                           uacpi_u64 in_value)
{
    uacpi_status status = UACPI_STATUS_OK;

    void *ptr = uacpi_kernel_map(address, byte_width);
    if (ptr == NULL)
        return UACPI_STATUS_OUT_OF_MEMORY;

    switch (byte_width) {
    case 1:
        *(volatile u8 *)ptr = in_value;
        break;
    case 2:
        *(volatile u16 *)ptr = in_value;
        break;
    case 4:
        *(volatile u32 *)ptr = in_value;
        break;
    case 8:
        *(volatile u64 *)ptr = in_value;
        break;
    default:
        status = UACPI_STATUS_INVALID_ARGUMENT;
        break;
    }

    uacpi_kernel_unmap(ptr, byte_width);

    return status;
}

uacpi_status uacpi_kernel_raw_io_read(uacpi_io_addr address,
                                      uacpi_u8 byte_width, uacpi_u64 *out_value)
{
    switch (byte_width) {
    case 1:
        *out_value = inb(address);
        break;
    case 2:
        *out_value = inw(address);
        break;
    case 4:
        *out_value = inl(address);
        break;
    default:
        return UACPI_STATUS_INVALID_ARGUMENT;
    }

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_raw_io_write(uacpi_io_addr address,
                                       uacpi_u8 byte_width, uacpi_u64 in_value)
{
    switch (byte_width) {
    case 1:
        outb(address, in_value);
        break;
    case 2:
        outw(address, in_value);
        break;
    case 4:
        outl(address, in_value);
        break;
    default:
        return UACPI_STATUS_INVALID_ARGUMENT;
    }

    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len,
                                 uacpi_handle *out_handle)
{
    // Do not remap anything, keep the same base
    UNUSED(len);
    *out_handle = (void *)(native_t)base;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle)
{
    UNUSED(handle);
}

uacpi_status uacpi_kernel_io_read(uacpi_handle base, uacpi_size offset,
                                  uacpi_u8 byte_width, uacpi_u64 *value)
{
    // mapped 1:1 by uacpi_kernel_io_map, so we can read as with a raw address
    return uacpi_kernel_raw_io_read((uacpi_io_addr)(native_t)base + offset,
                                    byte_width, value);
}

uacpi_status uacpi_kernel_io_write(uacpi_handle base, uacpi_size offset,
                                   uacpi_u8 byte_width, uacpi_u64 value)
{
    // mapped 1:1 by uacpi_kernel_io_map, so we can write as with a raw address
    return uacpi_kernel_raw_io_write((uacpi_io_addr)(native_t)base + offset,
                                     byte_width, value);
}

uacpi_handle uacpi_kernel_create_spinlock(void)
{
    spinlock_t *spinlock = kmalloc(sizeof(*spinlock), KMALLOC_DEFAULT);
    if (spinlock == NULL) {
        log_err("uacpi", "Failed to allocate spinlock");
        return NULL;
    }

    spinlock->locked = false;

    return spinlock;
}

void uacpi_kernel_free_spinlock(uacpi_handle spinlock)
{
    kfree(spinlock);
}

uacpi_cpu_flags uacpi_kernel_spinlock_lock(uacpi_handle spinlock)
{
    spinlock_acquire(spinlock);
    return 0;
}

void uacpi_kernel_spinlock_unlock(uacpi_handle spinlock, uacpi_cpu_flags flags)
{
    UNUSED(flags);
    spinlock_release(spinlock);
}

uacpi_handle uacpi_kernel_create_mutex(void)
{
    return uacpi_kernel_create_spinlock();
}

void uacpi_kernel_free_mutex(uacpi_handle mutex)
{
    uacpi_kernel_free_spinlock(mutex);
}

uacpi_bool uacpi_kernel_acquire_mutex(uacpi_handle mutex, uacpi_u16 timeout)
{
    UNUSED(timeout);
    uacpi_kernel_spinlock_lock(mutex);
    return UACPI_TRUE;
}

void uacpi_kernel_release_mutex(uacpi_handle mutex)
{
    return uacpi_kernel_spinlock_unlock(mutex, 0);
}

uacpi_u64 uacpi_kernel_get_ticks(void)
{
    return 10 * timer_to_us(timer_gettick());
}

void uacpi_kernel_stall(uacpi_u8 usec)
{
    // TODO: timer wait microseconds
    timer_wait_ms(round_down(usec, 1000) / 1000);
}

void uacpi_kernel_sleep(uacpi_u64 msec)
{
    timer_wait_ms(msec);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void)
{
    return 0;
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type,
                                        uacpi_work_handler handler,
                                        uacpi_handle ctx)
{
    UNUSED(type);

    process_t *process = process_create("kuacpi", handler, ctx, PROC_KERNEL);
    if (process == NULL)
        return UACPI_STATUS_OUT_OF_MEMORY;

    sched_new_process(process);

    return UACPI_STATUS_OK;
}

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *format, ...)
{
    va_list parameters;
    va_start(parameters, format);
    uacpi_kernel_vlog(level, format, parameters);
    va_end(parameters);
}

void uacpi_kernel_vlog(uacpi_log_level level, const uacpi_char *format,
                       uacpi_va_list va_args)
{
    // TODO: Better logging system ffs ... (no raw values + log_level as anyone
    // would do!)

    // We already inject a \n by default
    size_t format_len = strlen(format);
    if (format_len > 0 && format[format_len - 1] == '\n')
        ((char *)format)[format_len - 1] = '\0';

    switch (level) {
    case UACPI_LOG_DEBUG:
        log_vlog(ANSI_DBG "DEBUG" ANSI_RESET " ", "uacpi", format, va_args);
        break;
    case UACPI_LOG_TRACE:
        log_vlog(ANSI_INFO "TRACE" ANSI_RESET " ", "uacpi", format, va_args);
        break;
    case UACPI_LOG_INFO:
        log_vlog(ANSI_INFO "INFO" ANSI_RESET "  ", "uacpi", format, va_args);
        break;
    case UACPI_LOG_WARN:
        log_vlog(ANSI_WARN "WARN" ANSI_RESET "  ", "uacpi", format, va_args);
        break;
    case UACPI_LOG_ERROR:
        log_vlog(ANSI_ERR "ERROR" ANSI_RESET " ", "uacpi", format, va_args);
        break;
    }
}

typedef struct uacpi_irq_handle {
    u8 irq;
    interrupt_handler handler;
    void *data;
} uacpi_irq_handle;

uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx,
    uacpi_handle *out_irq_handle)
{
    if (irq > IDT_LENGTH)
        return UACPI_STATUS_INVALID_ARGUMENT;

    uacpi_irq_handle *handle = kmalloc(sizeof(*handle), KMALLOC_DEFAULT);
    if (handle == NULL)
        return UACPI_STATUS_OUT_OF_MEMORY;

    handle->irq = irq;
    handle->handler = interrupts_get_handler(irq, &handle->data);

    interrupts_set_handler(irq, handler, ctx);
    *out_irq_handle = handle;

    return UACPI_STATUS_OK;
}

uacpi_status
uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler,
                                         uacpi_handle irq_handle)
{
    UNUSED(handler);

    uacpi_irq_handle *irq = irq_handle;
    interrupts_set_handler(irq->irq, irq->handler, irq->data);

    return UACPI_STATUS_OK;
}

typedef struct uacpi_kernel_event {
    uacpi_u32 semaphore; // TODO: semaphore stub ...
} uacpi_kernel_event;

uacpi_handle uacpi_kernel_create_event(void)
{
    return kcalloc(1, sizeof(uacpi_kernel_event), KMALLOC_DEFAULT);
}

void uacpi_kernel_free_event(uacpi_handle event)
{
    kfree(event);
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout)
{
    uacpi_kernel_event *event = handle;
    uacpi_u32 original = event->semaphore;

    timestamp_t start = timer_to_ms(timer_gettick());
    timestamp_t end = start + timeout;

    while (event->semaphore == original) {
        if (timer_to_ms(timer_gettick()) > end)
            return UACPI_FALSE;
    }

    return UACPI_TRUE;
}

void uacpi_kernel_signal_event(uacpi_handle handle)
{
    uacpi_kernel_event *event = handle;
    event->semaphore += 1;
}

void uacpi_kernel_reset_event(uacpi_handle handle)
{
    uacpi_kernel_event *event = handle;
    event->semaphore -= 1;
}

/* ----- UNIMPLEMENTED uACPI FUNCTIONS ----- */

#define WARN_UNIMPLEMENTED() \
    log_warn("uacpi", "Unimplemented: %s", __FUNCTION__);

uacpi_status uacpi_kernel_pci_read(uacpi_pci_address *address,
                                   uacpi_size offset, uacpi_u8 byte_width,
                                   uacpi_u64 *value)
{
    MAP(UNUSED, address, offset, byte_width, value);
    WARN_UNIMPLEMENTED();
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write(uacpi_pci_address *address,
                                    uacpi_size offset, uacpi_u8 byte_width,
                                    uacpi_u64 value)
{
    MAP(UNUSED, address, offset, byte_width, value);
    WARN_UNIMPLEMENTED();
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req)
{
    UNUSED(req);
    WARN_UNIMPLEMENTED();
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void)
{
    WARN_UNIMPLEMENTED();
    return UACPI_STATUS_UNIMPLEMENTED;
}
