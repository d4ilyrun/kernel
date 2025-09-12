/*
 * Driver for ATA devices.
 *
 * ## TODO
 *
 * - Support ATAPI devices
 * - Support LBA48 accesses
 * - Use DMA for read/write operations
 * - Use interrupts instead of polling for read/write requests
 */

#define LOG_DOMAIN "ata"

#include <kernel/cpu.h>
#include <kernel/devices/block.h>
#include <kernel/devices/pci.h>
#include <kernel/devices/timer.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/spinlock.h>
#include <kernel/waitqueue.h>
#include <kernel/worker.h>

#include <utils/bits.h>

struct ata_bus {
    uint16_t io;      /* Register I/O ports range start */
    uint16_t control; /* Control/Status regsiter I/O ports range start */
    int irq;
    spinlock_t lock;
    struct ata_drive *current;
    struct ata_drive *master;
    struct ata_drive *slave;
    uint8_t control_cache;
};

struct ata_drive {
    struct block_device blkdev;
    struct ata_bus *bus;
};

static inline struct ata_drive *to_ata(struct block_device *blkdev)
{
    return container_of(blkdev, struct ata_drive, blkdev);
}

/*
 * Register offsets into drive->io range.
 */
#define ATA_DATA 0
#define ATA_ERROR 1    // Read-only
#define ATA_FEATURES 1 // Write-only
#define ATA_SECTOR_COUNT 2
#define ATA_LBA_LOW 3
#define ATA_LBA_MID 4
#define ATA_LBA_HIGH 5
#define ATA_DRIVE_SELECT 6
#define   ATA_DRIVE_SELECT_LBA_NUMBER 0xF
#define   ATA_DRIVE_SELECT_SLAVE BIT(4)
#define   ATA_DRIVE_SELECT_LBA BIT(6)
#define ATA_STATUS 7 // Read-only
#define   ATA_STATUS_ERR BIT(0)
#define   ATA_STATUS_DRQ BIT(3)
#define   ATA_STATUS_BSY BIT(7)
#define ATA_COMMAND 7 // Write-only
#define   ATA_COMMAND_READ_SECTORS 0x20
#define   ATA_COMMAND_WRITE_SECTORS 0x30
#define   ATA_COMMAND_WRITE_FLUSH 0xE7
#define   ATA_COMMAND_IDENTIFY 0xEC

/*
 * Register offsets into drive->control range.
 */
#define ATA_ALTERNATE_STATUS 0
#define ATA_DEVICE_CONTROL 0
#define   ATA_DEVICE_CONTROL_NIEN BIT(1) /* Disable interrupts */
#define   ATA_DEVICE_CONTROL_SRST BIT(2)

struct ata_identify_response {
    uint16_t __reserved0[60];
    uint32_t lba28_sectors;
    uint16_t __reserved1[20];
    uint16_t supported_features[3];
    uint16_t enabled_features[3];
    uint16_t __reserved2[168];
};

#define ATA_SECTOR_SIZE 512

static_assert(sizeof(struct ata_identify_response) == ATA_SECTOR_SIZE);

static inline void __ata_drive_select(const struct ata_bus *bus, bool slave)
{
    uint8_t select;

    select = inb(bus->io + ATA_DRIVE_SELECT);
    if (slave)
        select |= ATA_DRIVE_SELECT_SLAVE;
    else
        select &= ~ATA_DRIVE_SELECT_SLAVE;
    outb(bus->io + ATA_DRIVE_SELECT, select);

    /*
     * When switching drives on a bus, the newly selected drive may take a while
     * to respond and push its status on the bus. It is advised to poll the
     * status register 15 times and only pay attention to the last value.
     */
    for (size_t i = 0; i < 15; ++i)
        inb(bus->control + ATA_ALTERNATE_STATUS);
}

static inline void ata_drive_select(struct ata_drive *drive)
{
    struct ata_bus *bus = drive->bus;

    if (drive != bus->current)
        __ata_drive_select(bus, drive == bus->slave);

    bus->current = drive;
}

/** Send a command to the bus. */
static inline void ata_bus_command(const struct ata_bus *bus, uint8_t command)
{
    outb(bus->io + ATA_COMMAND, command);
}

static inline u8 ata_bus_status(const struct ata_bus *bus)
{
    /*
     * Read alternate status to avoid involuntarily masking interrupts.
     */
    return inb(bus->control + ATA_ALTERNATE_STATUS);
}

static inline void ata_bus_control_set(struct ata_bus *bus, uint8_t mask)
{
    bus->control_cache |= mask;
    outb(bus->control + ATA_DEVICE_CONTROL, bus->control_cache);
}

static inline void ata_bus_control_mask(struct ata_bus *bus, uint8_t mask)
{
    bus->control_cache &= ~mask;
    outb(bus->control + ATA_DEVICE_CONTROL, bus->control_cache);
}

static inline void
ata_bus_read_pio_sector(const struct ata_bus *bus, void *buffer)
{
    insw(bus->io + ATA_DATA, buffer, ATA_SECTOR_SIZE);
}

static inline void
ata_bus_write_pio_sector(const struct ata_bus *bus, const void *buffer)
{
    size_t word_count = ATA_SECTOR_SIZE / sizeof(uint16_t);

    /*
     * There needs to be a very small delay between writes. The delay induced
     * by the loop's branching should suffice.
     */
    for (size_t i = 0; i < word_count; ++i)
        outw(bus->io + ATA_DATA, ((uint16_t *)buffer)[i]);
}

/*
 * Set the logical base address for the next transfer.
 */
static void ata_bus_set_lba(const struct ata_bus *bus, uint64_t lba)
{
    uint8_t select;

    outb(bus->io + ATA_LBA_LOW, lba);
    outb(bus->io + ATA_LBA_MID, lba >> 8);
    outb(bus->io + ATA_LBA_HIGH, lba >> 16);

    select = inb(bus->io + ATA_DRIVE_SELECT);
    select &= ~ATA_DRIVE_SELECT_LBA_NUMBER;
    select |= (lba >> 24) & ATA_DRIVE_SELECT_LBA_NUMBER;
    outb(bus->io + ATA_DRIVE_SELECT, select);
}

/*
 * Perform a software reset on an ATA bus.
 */
static void ata_bus_reset(struct ata_bus *bus)
{
    ata_bus_control_set(bus, ATA_DEVICE_CONTROL_SRST);
    timer_delay_ms(1); /* Only 5us should be necessary here */
    ata_bus_control_mask(bus, ATA_DEVICE_CONTROL_SRST);

    /* The master drive on the bus is automatically selected. */
    bus->current = bus->master;

    ata_bus_control_mask(bus, 0xff);
}

static bool ata_bus_status_poll(const struct ata_bus *bus, uint8_t mask)
{
    int retry_count = 100;

    do {
        if (ata_bus_status(bus) & mask)
            return false;
        retry_count -= 1;
        timer_wait_ms(1);
    } while (retry_count > 0);

    return true;
}

/**
 * Wait for the bus's current operation to finish.
 *
 * @return True if the operation did not finish in time.
 */
static inline bool ata_bus_wait_ready(const struct ata_bus *bus)
{
    int retry_count = 100;

    do {
        if (!(ata_bus_status(bus) & ATA_STATUS_BSY))
            return false;
        retry_count -= 1;
        timer_wait_ms(1);
    } while (retry_count > 0);

    return true;
}

static void ata_bus_request_read_sectors(struct ata_bus *bus,
                                         struct block_io_request *request)
{
    for (blkcnt_t count = 0; count < request->count; ++count) {
        ata_bus_command(bus, ATA_COMMAND_READ_SECTORS);
        if (ata_bus_wait_ready(bus)) {
            log_warn("read_sectors @ %08lx: bus is busy",
                     request->offset + count * ATA_SECTOR_SIZE);
            break;
        }
        ata_bus_read_pio_sector(bus, request->buf + count * ATA_SECTOR_SIZE);
    }

    spinlock_release(&bus->lock);
}

static void ata_bus_request_write_sectors(struct ata_bus *bus,
                                          struct block_io_request *request)
{
    for (blkcnt_t count = 0; count < request->count; ++count) {
        ata_bus_command(bus, ATA_COMMAND_WRITE_SECTORS);
        ata_bus_write_pio_sector(bus, request->buf + count * ATA_SECTOR_SIZE);
        if (ata_bus_wait_ready(bus)) {
            log_warn("write_sectors @ %08lx: bus is busy",
                     request->offset + count * ATA_SECTOR_SIZE);
            break;
        }
        ata_bus_command(bus, ATA_COMMAND_WRITE_FLUSH);
    }

    spinlock_release(&bus->lock);
}

static error_t
ata_request(struct block_device *blkdev, struct block_io_request *request)
{
    struct ata_drive *drive = to_ata(blkdev);
    struct ata_bus *bus = drive->bus;

    /* This lock is released after the last request has been finished. */
    spinlock_acquire(&bus->lock);

    ata_drive_select(drive);
    if (ata_bus_wait_ready(bus)) {
        log_warn("bus not ready before request");
        return E_BUSY;
    }

    outb(bus->io + ATA_SECTOR_COUNT, request->count);
    ata_bus_set_lba(bus, request->offset / ATA_SECTOR_SIZE);

    switch (request->type) {
    case BLOCK_IO_REQUEST_READ:
        ata_bus_request_read_sectors(bus, request);
        break;
    case BLOCK_IO_REQUEST_WRITE:
        ata_bus_request_write_sectors(bus, request);
        break;
    }

    return E_SUCCESS;
}

static const struct block_device_ops ata_block_device_ops = {
    .request = ata_request,
};

/*
 * Generate a unique name for an ATA device.
 */
static const char *ata_drive_next_name(void)
{
    static const char *ata_drive_names[] = {"hd0", "hd1", "hd2", "hd3"};
    static size_t index = 0;

    if (index >= ARRAY_SIZE(ata_drive_names)) {
        log_warn("too many ATA drives, using generic name");
        return "hd";
    }

    return ata_drive_names[index++];
}

/*
 * Identify the currently selected ATA drive.
 */
static struct ata_drive *ata_drive_identify(struct ata_bus *bus)
{
    struct ata_drive *drive = NULL;
    struct ata_identify_response response;
    uint64_t sector_count;
    uint16_t lba_high;
    uint8_t lba_mid;
    uint8_t status;
    error_t err;

    lba_mid = inb(bus->io + ATA_LBA_MID);
    lba_high = inb(bus->io + ATA_LBA_HIGH);

    /*
     * Reset LBA and sector count.
     */
    outb(bus->io + ATA_SECTOR_COUNT, 0);
    ata_bus_set_lba(bus, 0);

    ata_bus_command(bus, ATA_COMMAND_IDENTIFY);
    status = ata_bus_status(bus);

    /*
     * Drive does not exist.
     */
    if (status == 0x0) {
        err = E_NODEV;
        goto identify_error_exit;
    }

    /*
     * Detect ATA device type based on its signature.
     * The signature is only valid after a reset operation.
     */
    switch (lba_mid << 8 | lba_high) {
    case 0x0000: /* PATA device */
        break;
    case 0x14EB: /* PATAPI device */
        not_implemented("PATAPI devices");
        err = E_NOT_IMPLEMENTED;
        goto identify_error_exit;
    case 0x3CC3: /* SATA device */
    default:
        log_warn("unsupported ATA signature: " FMT8 ":" FMT8, lba_mid,
                 (uint8_t)lba_high);
        err = E_NOT_SUPPORTED;
        goto identify_error_exit;
    }

    if (ata_bus_wait_ready(bus)) {
        log_warn("bus@" FMT16 ": identify: busy poll timeout", bus->io);
        err = E_BUSY;
        goto identify_error_exit;
    }

    if (ata_bus_status_poll(bus, ATA_STATUS_DRQ | ATA_STATUS_ERR)) {
        log_warn("bus@" FMT16 ": identify: ready poll timeout", bus->io);
        err = E_BUSY;
        goto identify_error_exit;
    }

    if (ata_bus_status(bus) & ATA_STATUS_ERR) {
        log_err("bus@" FMT16 ": identify: error detected", bus->io);
        log_err("  error: " FMT8, inb(bus->io + ATA_ERROR));
        err = E_INVAL;
        goto identify_error_exit;
    }

    ata_bus_read_pio_sector(bus, &response);

    sector_count = response.lba28_sectors;
    if (sector_count == 0) {
        log_err("invalid LBA28 sector count");
        err = E_INVAL;
        goto identify_error_exit;
    }

    drive = kcalloc(1, sizeof(*drive), KMALLOC_KERNEL);
    if (!drive) {
        err = E_NOMEM;
        goto identify_error_exit;
    }

    drive->bus = bus;
    drive->blkdev.block_size = ATA_SECTOR_SIZE;
    drive->blkdev.block_count = sector_count;
    drive->blkdev.ops = &ata_block_device_ops;
    device_set_name(&drive->blkdev.dev, ata_drive_next_name());

    err = block_device_register(&drive->blkdev);
    if (err) {
        log_err("failed to register block device: %s", err_to_str(err));
        goto identify_error_exit;
    }

    return drive;

identify_error_exit:
    kfree(drive);
    return PTR_ERR(err);
}

static error_t ata_bus_probe(uint16_t io, uint16_t control, int irq)
{
    struct ata_bus *bus;
    uint8_t val;

    /* Detect floating bus (no drive connected). */
    if (inb(io + ATA_STATUS) == 0xFF)
        return E_NODEV;

    bus = kcalloc(1, sizeof(*bus), KMALLOC_KERNEL);
    if (!bus)
        return E_NOMEM;

    bus->io = io;
    bus->irq = irq;
    bus->control = control;

    INIT_SPINLOCK(bus->lock);

    ata_bus_reset(bus);

    /*
     * Disable interrupts since we don't use them anyway.
     */
    ata_bus_control_set(bus, ATA_DEVICE_CONTROL_NIEN);

    /*
     * Use LBA addressing mode.
     */
    val = inb(bus->io + ATA_DRIVE_SELECT);
    val |= ATA_DRIVE_SELECT_LBA;
    outb(bus->io + ATA_DRIVE_SELECT, val);

    __ata_drive_select(bus, false);
    bus->master = ata_drive_identify(bus);
    if (IS_ERR(bus->master))
        bus->master = NULL;

    __ata_drive_select(bus, true);
    bus->slave = ata_drive_identify(bus);
    if (IS_ERR(bus->slave))
        bus->slave = NULL;

    return E_SUCCESS;
}

static error_t ata_init(void)
{
    ata_bus_probe(0x1F0, 0x3F6, 14); /* Primary bus */
    ata_bus_probe(0x170, 0x376, 15); /* Secondary bus */

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_NORMAL, ata_init);
