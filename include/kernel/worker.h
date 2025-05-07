/**
 * @brief Worker thread
 *
 * @defgroup worker Worker thread
 * @ingroup scheduling
 *
 * # Worker thread
 *
 * A worker thread is a wrapper around a regular kernel thread, designed so
 * that other threads can wait until it completes before continuing to execute.
 *
 * It can be particularily useful in order to delay the execution of a costly
 * function inside an interrupt handler. The kernel thread would be created
 * and listed, but its execution would be delayed until interrupts are
 * re-enabled (e.g. network packet RX codepath after copying it in memory).
 *
 * Another usecase is to use it as as a synchronisation point between threads.
 * An example for that can be seen in the network layer. When sending an IP
 * packet to a destination never seen before, we first need to find what the
 * destination's MAC address is. To do this we'd need to perform an ARP request,
 * and wait until it completes before filling the Ethernet frame.
 *
 * Using this API, this would look something like this:
 *
 * void send_ip_packet(packet)
 * {
 *      fill_ip_header(packet);
 *
 *      if (!destination_seen) {
 *           DECLARE_WORKER(worker);
 *           worker_start(&worker, do_arp_request, packet);
 *           worker_wait(&worker);
 *      }
 *
 *      fill_ethernet_header(packet);
 *      packet_send();
 * }
 */

#ifndef KERNEL_WORKER_H
#define KERNEL_WORKER_H

#include <kernel/process.h>
#include <kernel/waitqueue.h>

/** Worker thread */
struct worker {
    struct waitqueue queue;  /// The processes waiting for this thread to finish
    bool done;               /// Whether the thread has finished
    thread_entry_t function; /// Worker thread's entrypoint
    void *data;              /// Passed as an argument to @ref function
};

/** Default init value */
#define WORKER_INIT         \
    ((struct worker){       \
        INIT_WAITQUEUE(.queue), \
        .done = true,       \
    })

/** Initialize a worker */
#define INIT_WORKER(_worker) _worker = WORKER_INIT

/** Declare and initialize a worker */
#define DECLARE_WORKER(_worker) struct worker _worker = WORKER_INIT

/** Execute a function inside a worker thread */
void worker_start(struct worker *, thread_entry_t, void *);

/** Wait until the worker is done executing */
void worker_wait(struct worker *);

#endif /* KERNEL_WORKER_H */
