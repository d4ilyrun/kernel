#include <kernel/process.h>

process_t kernel_startup_process = {
    .name = "kstartup",
    .pid = 0,
};

process_t *current_process = &kernel_startup_process;
