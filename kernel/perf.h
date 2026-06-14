#ifndef PERF_H
#define PERF_H

#include "stdint.h"
#include "stddef.h"

/*
 * Performance Monitoring Subsystem
 *
 * Provides kernel-level profiling and tracing capabilities:
 *   - Cycle counter via RDTSC
 *   - Per-function profiling (avg/min/max time, call count)
 *   - System call tracing
 *   - Memory allocation tracking
 *   - Context switch counting
 *   - Per-IRQ interrupt statistics
 *   - Statistics export via proc-like interface
 *
 * The perf subsystem operates in two modes:
 *   - Stopped: no overhead, counters are zeroed
 *   - Running: all counters are active, data is collected
 *
 * Controls:
 *   perf_start() / perf_stop() / perf_reset()
 *   perf_profile_enter(id) / perf_profile_exit(id)  -- instrument functions
 *   perf_syscall_trace(syscall_nr, args, ret)       -- trace syscalls
 *   perf_alloc_track(size) / perf_free_track(size)  -- track memory
 *   perf_ctxsw()                                    -- count context switches
 *   perf_irq_count(irq)                             -- count IRQs
 */

#define PERF_MAX_PROFILES   256     /* max function profiles */
#define PERF_MAX_SYSCALLS   256     /* syscall numbers 0..255 */
#define PERF_MAX_IRQS       256     /* IRQ numbers 0..255 */

/* Per-function profile entry. */
typedef struct {
	char        name[64];       /* function name */
	uint32_t    call_count;     /* number of calls */
	uint64_t    total_cycles;   /* total cycles spent */
	uint64_t    min_cycles;     /* minimum cycles per call */
	uint64_t    max_cycles;     /* maximum cycles per call */
	uint64_t    entry_time;     /* temporary: RDTSC at entry (for active call) */
	uint8_t     active;         /* 1 = currently profiling */
} perf_profile_t;

/* System call trace entry. */
typedef struct {
	uint32_t    syscall_nr;
	uint32_t    arg0, arg1, arg2, arg3, arg4;
	uint32_t    retval;
	uint64_t    timestamp;
	uint32_t    pid;
} perf_syscall_trace_t;

/* Memory allocation statistics. */
typedef struct {
	uint64_t    bytes_allocated;    /* total bytes allocated */
	uint64_t    bytes_freed;        /* total bytes freed */
	uint64_t    current_usage;      /* current bytes in use */
	uint32_t    alloc_count;        /* number of kmalloc calls */
	uint32_t    free_count;         /* number of kfree calls */
	uint32_t    peak_usage;         /* peak bytes in use */
} perf_memstat_t;

/* Overall performance statistics. */
typedef struct {
	uint64_t    context_switches;   /* total context switches */
	uint64_t    irq_counts[PERF_MAX_IRQS]; /* per-IRQ counter */
	uint64_t    total_cycles;       /* total cycles since start */
	uint64_t    start_tsc;          /* TSC at perf_start() */
	uint32_t    syscall_count;      /* total syscalls */
	uint8_t     running;            /* 1 = perf is active */
} perf_stats_t;

/* ---- Public API ------------------------------------------------- */

/*
 * perf_init - initialise the performance monitoring subsystem.
 * Called once at boot.
 */
void perf_init(void);

/*
 * perf_start - start performance monitoring.
 * Clears all counters and begins collection.
 */
void perf_start(void);

/*
 * perf_stop - stop performance monitoring.
 * Counters are frozen but not cleared.
 */
void perf_stop(void);

/*
 * perf_reset - reset all counters to zero.
 * Can be called while running or stopped.
 */
void perf_reset(void);

/*
 * perf_tick - periodic tick handler for time-based stats.
 * Called from the timer ISR.
 */
void perf_tick(void);

/*
 * perf_read_tsc - read the CPU timestamp counter.
 * Returns the current 64-bit TSC value.
 */
uint64_t perf_read_tsc(void);

/*
 * perf_profile_enter - record entry into a profiled function.
 * Returns a profile ID to pass to perf_profile_exit().
 * On first call, registers the function name.
 */
int perf_profile_enter(const char *name);

/*
 * perf_profile_exit - record exit from a profiled function.
 * 'id' is the ID returned by perf_profile_enter().
 */
void perf_profile_exit(int id);

/*
 * perf_syscall_trace - trace a system call invocation.
 */
void perf_syscall_trace(uint32_t syscall_nr,
			uint32_t arg0, uint32_t arg1, uint32_t arg2,
			uint32_t arg3, uint32_t arg4, uint32_t retval);

/*
 * perf_alloc_track - track a memory allocation.
 */
void perf_alloc_track(uint32_t size);

/*
 * perf_free_track - track a memory deallocation.
 */
void perf_free_track(uint32_t size);

/*
 * perf_ctxsw - count a context switch.
 */
void perf_ctxsw(void);

/*
 * perf_irq_count - count an interrupt.
 */
void perf_irq_count(uint8_t irq);

/*
 * perf_get_stats - get a snapshot of current statistics.
 * Returns a pointer to the internal stats structure (read-only).
 */
const perf_stats_t *perf_get_stats(void);

/*
 * perf_get_memstat - get memory allocation statistics.
 */
const perf_memstat_t *perf_get_memstat(void);

/*
 * perf_get_profile - get a profile entry by ID.
 * Returns NULL if ID is invalid.
 */
const perf_profile_t *perf_get_profile(int id);

/*
 * perf_profile_count - return the number of registered profiles.
 */
int perf_profile_count(void);

/*
 * perf_print_stats - print all statistics to serial port.
 */
void perf_print_stats(void);

#endif /* PERF_H */