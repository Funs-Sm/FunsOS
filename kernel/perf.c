/*
 * kernel/perf.c - Performance Monitoring Subsystem
 *
 * A lightweight kernel profiling framework that tracks:
 *   - Function call counts and cycle counts (RDTSC-based)
 *   - System call tracing with arguments and return values
 *   - Memory allocation/free tracking
 *   - Context switch counting
 *   - Per-IRQ interrupt statistics
 *
 * All counters are updated atomically where possible.  Function
 * profiling uses a simple linear search for name-to-ID mapping,
 * which is acceptable for the small number of profiled functions
 * (typically < 256).
 *
 * RDTSC is used for cycle counting.  On modern x86 CPUs, the TSC
 * is invariant (constant rate regardless of P-state), which makes
 * it suitable for profiling even with frequency scaling.
 *
 * The perf subsystem is designed to be called from interrupt context
 * (timer ISR, syscall handler, etc.) and therefore does not use
 * any blocking operations.
 */

#include "perf.h"
#include "string.h"
#include "kheap.h"
#include "klog.h"
#include "serial.h"
#include "process.h"
#include "sched.h"

/* ------------------------------------------------------------------
 *  Global state
 * ------------------------------------------------------------------ */
static perf_stats_t   perf_stats;
static perf_memstat_t perf_memstat;
static perf_profile_t perf_profiles[PERF_MAX_PROFILES];
static int            perf_profile_count_static = 0;

/* ------------------------------------------------------------------
 *  perf_read_tsc - read the CPU timestamp counter
 * ------------------------------------------------------------------ */
uint64_t perf_read_tsc(void)
{
	uint32_t lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

/* ------------------------------------------------------------------
 *  perf_init - initialise the subsystem
 * ------------------------------------------------------------------ */
void perf_init(void)
{
	memset(&perf_stats, 0, sizeof(perf_stats));
	memset(&perf_memstat, 0, sizeof(perf_memstat));

	for (int i = 0; i < PERF_MAX_PROFILES; i++) {
		memset(&perf_profiles[i], 0, sizeof(perf_profile_t));
	}

	perf_profile_count_static = 0;
	perf_stats.running = 0;

	klog_info("perf: subsystem initialised");
}

/* ------------------------------------------------------------------
 *  perf_start - start monitoring
 * ------------------------------------------------------------------ */
void perf_start(void)
{
	if (perf_stats.running) {
		klog_warn("perf_start: already running");
		return;
	}

	perf_reset();
	perf_stats.start_tsc = perf_read_tsc();
	perf_stats.running = 1;

	klog_info("perf: monitoring started");
}

/* ------------------------------------------------------------------
 *  perf_stop - stop monitoring
 * ------------------------------------------------------------------ */
void perf_stop(void)
{
	if (!perf_stats.running) {
		klog_warn("perf_stop: not running");
		return;
	}

	perf_stats.total_cycles = perf_read_tsc() - perf_stats.start_tsc;
	perf_stats.running = 0;

	klog_info("perf: monitoring stopped");
}

/* ------------------------------------------------------------------
 *  perf_reset - reset all counters
 * ------------------------------------------------------------------ */
void perf_reset(void)
{
	perf_stats.context_switches = 0;
	perf_stats.syscall_count = 0;
	perf_stats.total_cycles = 0;

	for (int i = 0; i < PERF_MAX_IRQS; i++)
		perf_stats.irq_counts[i] = 0;

	perf_memstat.bytes_allocated = 0;
	perf_memstat.bytes_freed = 0;
	perf_memstat.current_usage = 0;
	perf_memstat.alloc_count = 0;
	perf_memstat.free_count = 0;
	perf_memstat.peak_usage = 0;

	for (int i = 0; i < perf_profile_count_static; i++) {
		perf_profiles[i].call_count = 0;
		perf_profiles[i].total_cycles = 0;
		perf_profiles[i].min_cycles = (uint64_t)-1;
		perf_profiles[i].max_cycles = 0;
		perf_profiles[i].active = 0;
	}
}

/* ------------------------------------------------------------------
 *  perf_tick - periodic tick handler
 * ------------------------------------------------------------------ */
void perf_tick(void)
{
	if (!perf_stats.running)
		return;
	perf_stats.total_cycles = perf_read_tsc() - perf_stats.start_tsc;
}

/* ------------------------------------------------------------------
 *  perf_profile_enter - record entry into a profiled function
 * ------------------------------------------------------------------ */
int perf_profile_enter(const char *name)
{
	int i;

	if (!perf_stats.running || !name)
		return -1;

	for (i = 0; i < perf_profile_count_static; i++) {
		if (strcmp(perf_profiles[i].name, name) == 0) {
			perf_profiles[i].entry_time = perf_read_tsc();
			perf_profiles[i].active = 1;
			return i;
		}
	}

	if (perf_profile_count_static >= PERF_MAX_PROFILES)
		return -1;

	i = perf_profile_count_static;
	perf_profile_count_static++;

	size_t len = strlen(name);
	if (len > 63) len = 63;
	memcpy(perf_profiles[i].name, name, len);
	perf_profiles[i].name[len] = '\0';

	perf_profiles[i].call_count = 0;
	perf_profiles[i].total_cycles = 0;
	perf_profiles[i].min_cycles = (uint64_t)-1;
	perf_profiles[i].max_cycles = 0;
	perf_profiles[i].entry_time = perf_read_tsc();
	perf_profiles[i].active = 1;

	return i;
}

/* ------------------------------------------------------------------
 *  perf_profile_exit - record exit from a profiled function
 * ------------------------------------------------------------------ */
void perf_profile_exit(int id)
{
	if (!perf_stats.running || id < 0 || id >= PERF_MAX_PROFILES)
		return;

	perf_profile_t *p = &perf_profiles[id];

	if (!p->active)
		return;

	uint64_t now = perf_read_tsc();
	uint64_t elapsed = now - p->entry_time;

	p->call_count++;
	p->total_cycles += elapsed;

	if (elapsed < p->min_cycles)
		p->min_cycles = elapsed;
	if (elapsed > p->max_cycles)
		p->max_cycles = elapsed;

	p->active = 0;
}

/* ------------------------------------------------------------------
 *  perf_syscall_trace - trace a system call
 * ------------------------------------------------------------------ */
void perf_syscall_trace(uint32_t syscall_nr,
			uint32_t arg0, uint32_t arg1, uint32_t arg2,
			uint32_t arg3, uint32_t arg4, uint32_t retval)
{
	if (!perf_stats.running)
		return;

	perf_stats.syscall_count++;

	klog_debug("perf: syscall %u(args=%x,%x,%x,%x,%x) -> %x",
		syscall_nr, arg0, arg1, arg2, arg3, arg4, retval);
}

/* ------------------------------------------------------------------
 *  perf_alloc_track - track a memory allocation
 * ------------------------------------------------------------------ */
void perf_alloc_track(uint32_t size)
{
	if (!perf_stats.running)
		return;

	perf_memstat.bytes_allocated += size;
	perf_memstat.current_usage += size;
	perf_memstat.alloc_count++;

	if (perf_memstat.current_usage > perf_memstat.peak_usage)
		perf_memstat.peak_usage = perf_memstat.current_usage;
}

/* ------------------------------------------------------------------
 *  perf_free_track - track a memory deallocation
 * ------------------------------------------------------------------ */
void perf_free_track(uint32_t size)
{
	if (!perf_stats.running)
		return;

	perf_memstat.bytes_freed += size;
	perf_memstat.free_count++;

	if (perf_memstat.current_usage >= size)
		perf_memstat.current_usage -= size;
	else
		perf_memstat.current_usage = 0;
}

/* ------------------------------------------------------------------
 *  perf_ctxsw - count a context switch
 * ------------------------------------------------------------------ */
void perf_ctxsw(void)
{
	if (!perf_stats.running)
		return;
	perf_stats.context_switches++;
}

/* ------------------------------------------------------------------
 *  perf_irq_count - count an interrupt
 * ------------------------------------------------------------------ */
void perf_irq_count(uint8_t irq)
{
	if (!perf_stats.running)
		return;
	if (irq < PERF_MAX_IRQS)
		perf_stats.irq_counts[irq]++;
}

/* ------------------------------------------------------------------
 *  Getters
 * ------------------------------------------------------------------ */
const perf_stats_t *perf_get_stats(void)
{
	return &perf_stats;
}

const perf_memstat_t *perf_get_memstat(void)
{
	return &perf_memstat;
}

const perf_profile_t *perf_get_profile(int id)
{
	if (id < 0 || id >= perf_profile_count_static)
		return NULL;
	return &perf_profiles[id];
}

int perf_profile_count(void)
{
	return perf_profile_count_static;
}

/* ------------------------------------------------------------------
 *  perf_print_stats - print all statistics to serial port
 * ------------------------------------------------------------------ */
void perf_print_stats(void)
{
	if (perf_stats.running)
		perf_stats.total_cycles = perf_read_tsc() - perf_stats.start_tsc;

	serial_print(COM1, "\n========================================\n");
	serial_print(COM1, "  Performance Monitoring Statistics\n");
	serial_print(COM1, "========================================\n\n");

	serial_print(COM1, "Status: ");
	serial_print(COM1, perf_stats.running ? "RUNNING\n" : "STOPPED\n");

	serial_print(COM1, "Total cycles:  ");
	serial_printf(COM1, "0x%x%08x\n",
		(uint32_t)(perf_stats.total_cycles >> 32),
		(uint32_t)(perf_stats.total_cycles));

	serial_print(COM1, "Context switches: ");
	serial_printf(COM1, "%u\n", (uint32_t)perf_stats.context_switches);

	serial_print(COM1, "System calls:     ");
	serial_printf(COM1, "%u\n", perf_stats.syscall_count);

	/* Per-IRQ statistics. */
	int irq_active = 0;
	for (int i = 0; i < PERF_MAX_IRQS; i++) {
		if (perf_stats.irq_counts[i] > 0)
			irq_active = 1;
	}

	if (irq_active) {
		serial_print(COM1, "\n--- IRQ Statistics ---\n");
		serial_print(COM1, "IRQ   Count\n");
		serial_print(COM1, "----  ------\n");

		for (int i = 0; i < 32; i++) {
			if (perf_stats.irq_counts[i] > 0) {
				serial_printf(COM1, " %-4d  %u\n", i,
					(uint32_t)perf_stats.irq_counts[i]);
			}
		}
	}

	/* Function profile data. */
	if (perf_profile_count_static > 0) {
		serial_print(COM1, "\n--- Function Profile ---\n");
		serial_print(COM1, "Name                           Calls    Total(cyc)   Avg   Min   Max\n");
		serial_print(COM1, "-----------------------------  -------  ------------  ----- ----- -----\n");

		for (int i = 0; i < perf_profile_count_static; i++) {
			perf_profile_t *p = &perf_profiles[i];

			serial_print(COM1, p->name);
			int namelen = (int)strlen(p->name);
			for (int j = namelen; j < 30; j++)
				serial_putchar(COM1, ' ');
			serial_putchar(COM1, ' ');

			serial_printf(COM1, "%-8u", p->call_count);

			serial_printf(COM1, " %u", (uint32_t)p->total_cycles);

			uint32_t avg = 0;
			if (p->call_count > 0)
				avg = (uint32_t)(p->total_cycles / p->call_count);
			serial_printf(COM1, "       %u", avg);

			uint32_t min = (p->min_cycles == (uint64_t)-1) ? 0 : (uint32_t)p->min_cycles;
			serial_printf(COM1, " %u", min);

			serial_printf(COM1, " %u", (uint32_t)p->max_cycles);

			serial_putchar(COM1, '\n');
		}
	}

	/* Memory statistics. */
	serial_print(COM1, "\n--- Memory Statistics ---\n");
	serial_print(COM1, "Allocations:      ");
	serial_printf(COM1, "%u\n", perf_memstat.alloc_count);
	serial_print(COM1, "Frees:            ");
	serial_printf(COM1, "%u\n", perf_memstat.free_count);
	serial_print(COM1, "Bytes allocated:  ");
	serial_printf(COM1, "%u\n", (uint32_t)perf_memstat.bytes_allocated);
	serial_print(COM1, "Bytes freed:      ");
	serial_printf(COM1, "%u\n", (uint32_t)perf_memstat.bytes_freed);
	serial_print(COM1, "Current usage:    ");
	serial_printf(COM1, "%u\n", (uint32_t)perf_memstat.current_usage);
	serial_print(COM1, "Peak usage:       ");
	serial_printf(COM1, "%u\n", (uint32_t)perf_memstat.peak_usage);

	serial_print(COM1, "\n========================================\n");
}