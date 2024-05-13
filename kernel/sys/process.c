#include <kernel/process.h>

process_t kernel_startup_process = {
    .name = "kstartup",
    .pid = 0,
};
