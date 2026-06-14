/*
 * kernel/kdebug.c - Kernel Debugger
 *
 * An interactive kernel debugger accessible via:
 *   - Serial port (COM1, 115200 baud): always active
 *   - Keyboard shortcut (Ctrl+Shift+D): toggles debugger console
 *
 * Commands:
 *   help    - show available commands
 *   regs    - dump CPU registers
 *   mem     - hexdump memory (xxd-style)
 *   bt      - stack backtrace
 *   bp      - breakpoint management (set/list/clear)
 *   step    - single-step (enables TF flag)
 *   disasm  - disassemble instructions
 *   ps      - list processes/threads
 *   page    - walk page tables for a VA
 *   memstat - memory statistics
 *   reboot  - triple-fault reboot
 *   continue - resume execution
 *
 * When a breakpoint (#BP) or exception occurs, the debugger is entered
 * automatically.  The user can inspect state and then 'continue'.
 */

#include "stdint.h"
#include "stddef.h"
#include "string.h"
#include "kheap.h"
#include "klog.h"
#include "ksym.h"
#include "pmm.h"
#include "vmm.h"
#include "process.h"
#include "sched.h"
#include "serial.h"
#include "io.h"
#include "idt.h"
#include "irq.h"
#include "../drivers/keyboard.h"

/* ------------------------------------------------------------------
 *	Constants
 * ------------------------------------------------------------------ */
#define DBG_PORT        COM1
#define DBG_MAX_CMD     256
#define DBG_MAX_BP      64
#define DBG_MEM_WIDTH   16      /* bytes per line in hexdump */

/* ------------------------------------------------------------------
 *  Debugger state
 * ------------------------------------------------------------------ */
static volatile int dbg_enabled = 0;
static volatile int dbg_active = 0;   /* 1 = debugger shell is running */
static volatile int dbg_step = 0;     /* 1 = single-step mode active */
static volatile int dbg_serial_poll = 0;
static regs_t           *saved_regs = NULL;

/* ------------------------------------------------------------------
 *  Breakpoint table
 * ------------------------------------------------------------------ */
typedef struct {
	uint32_t addr;          /* virtual address of breakpoint */
	uint8_t  orig_byte;     /* original opcode byte (before INT3) */
	uint8_t  enabled;       /* 1 = active */
} bp_entry_t;

static bp_entry_t bp_table[DBG_MAX_BP];
static int bp_count = 0;

/* ------------------------------------------------------------------
 *  Forward declarations
 * ------------------------------------------------------------------ */
static void dbg_output(const char *str);
static void dbg_output_hex(uint32_t val);
static void dbg_output_dec(uint32_t val);
static void dbg_prompt(void);
static int  dbg_readline(char *buf, int max);
static void dbg_cmd_help(void);
static void dbg_cmd_regs(void);
static void dbg_cmd_mem(const char *args);
static void dbg_cmd_bt(void);
static void dbg_cmd_bp_set(const char *args);
static void dbg_cmd_bp_list(void);
static void dbg_cmd_bp_clear(const char *args);
static void dbg_cmd_step(void);
static void dbg_cmd_disasm(const char *args);
static void dbg_cmd_ps(void);
static void dbg_cmd_page(const char *args);
static void dbg_cmd_memstat(void);
static void dbg_cmd_reboot(void);
static void dbg_parse_command(const char *line);

/* ------------------------------------------------------------------
 *  Output helpers (write to serial and optionally to screen)
 * ------------------------------------------------------------------ */
static void dbg_output(const char *str)
{
	serial_print(DBG_PORT, str);
}

static void dbg_output_hex(uint32_t val)
{
	char hex[] = "0123456789ABCDEF";

	serial_putchar(DBG_PORT, '0');
	serial_putchar(DBG_PORT, 'x');

	int started = 0;
	for (int i = 28; i >= 0; i -= 4) {
		uint8_t nibble = (val >> i) & 0x0F;
		if (nibble || started || i == 0) {
			serial_putchar(DBG_PORT, hex[nibble]);
			started = 1;
		}
	}
}

static void dbg_output_dec(uint32_t val)
{
	char buf[12];
	int i = 0;

	if (val == 0) {
		serial_putchar(DBG_PORT, '0');
		return;
	}

	while (val > 0) {
		buf[i++] = '0' + (val % 10);
		val /= 10;
	}

	while (i > 0)
		serial_putchar(DBG_PORT, buf[--i]);
}

static void dbg_output_ptr(uint32_t val)
{
	/* Print as hex with leading zeros, no '0x' prefix. */
	char hex[] = "0123456789ABCDEF";
	for (int i = 28; i >= 0; i -= 4) {
		serial_putchar(DBG_PORT, hex[(val >> i) & 0x0F]);
	}
}

/* ------------------------------------------------------------------
 *  dbg_prompt - print the debugger prompt
 * ------------------------------------------------------------------ */
static void dbg_prompt(void)
{
	dbg_output("\nKDBG> ");
}

/* ------------------------------------------------------------------
 *  dbg_readline - read a line from serial port
 *  Returns 0 on success, -1 if no data.
 * ------------------------------------------------------------------ */
static int dbg_readline(char *buf, int max)
{
	int pos = 0;

	while (pos < max - 1) {
		/* Wait for a character on serial. */
		while (!serial_available(DBG_PORT))
			;

		char c = serial_read(DBG_PORT);

		if (c == '\r' || c == '\n') {
			/* Echo newline. */
			serial_putchar(DBG_PORT, '\r');
			serial_putchar(DBG_PORT, '\n');
			break;
		}

		if (c == '\b' || c == 0x7F) {
			if (pos > 0) {
				pos--;
				/* Erase character on remote terminal. */
				serial_putchar(DBG_PORT, '\b');
				serial_putchar(DBG_PORT, ' ');
				serial_putchar(DBG_PORT, '\b');
			}
			continue;
		}

		/* Accept printable characters. */
		if (c >= 0x20 && c <= 0x7E) {
			buf[pos++] = c;
			serial_putchar(DBG_PORT, c);
		}
	}

	buf[pos] = '\0';
	return 0;
}

/* ------------------------------------------------------------------
 *  dbg_get_segment - read a segment register via inline asm
 * ------------------------------------------------------------------ */
static uint16_t dbg_read_seg(const char *seg_name)
{
	uint16_t value;

	if (strcmp(seg_name, "cs") == 0) {
		__asm__ volatile("mov %%cs, %0" : "=r"(value));
	} else if (strcmp(seg_name, "ds") == 0) {
		__asm__ volatile("mov %%ds, %0" : "=r"(value));
	} else if (strcmp(seg_name, "es") == 0) {
		__asm__ volatile("mov %%es, %0" : "=r"(value));
	} else if (strcmp(seg_name, "fs") == 0) {
		__asm__ volatile("mov %%fs, %0" : "=r"(value));
	} else if (strcmp(seg_name, "gs") == 0) {
		__asm__ volatile("mov %%gs, %0" : "=r"(value));
	} else if (strcmp(seg_name, "ss") == 0) {
		__asm__ volatile("mov %%ss, %0" : "=r"(value));
	} else {
		value = 0;
	}

	return value;
}

static uint32_t dbg_read_cr(uint8_t n)
{
	uint32_t val = 0;

	switch (n) {
	case 0:
		__asm__ volatile("mov %%cr0, %0" : "=r"(val));
		break;
	case 2:
		__asm__ volatile("mov %%cr2, %0" : "=r"(val));
		break;
	case 3:
		__asm__ volatile("mov %%cr3, %0" : "=r"(val));
		break;
	case 4:
		/* CR4 may not exist on all CPUs; use cpuid check */
		{
			uint32_t eax_max;
			__asm__ volatile("cpuid" : "=a"(eax_max) : "a"(0x80000000));
			if (eax_max >= 0x80000001) {
				__asm__ volatile("mov %%cr4, %0" : "=r"(val));
			}
		}
		break;
	}

	return val;
}

/* ------------------------------------------------------------------
 *  dbg_cmd_help - print available commands
 * ------------------------------------------------------------------ */
static void dbg_cmd_help(void)
{
	dbg_output("\n"
		"Kernel Debugger Commands:\n"
		"  help              - Show this help\n"
		"  regs              - Dump CPU registers\n"
		"  mem  <addr> [len] - Hexdump memory (default 256 bytes)\n"
		"  bt                - Stack backtrace\n"
		"  bp set <addr>     - Set breakpoint\n"
		"  bp list           - List breakpoints\n"
		"  bp clear <addr>   - Clear breakpoint\n"
		"  step              - Single-step one instruction\n"
		"  disasm <addr> [n] - Disassemble n instructions (default 16)\n"
		"  ps                - List processes and threads\n"
		"  page <addr>       - Walk page tables for virtual address\n"
		"  memstat           - Show memory statistics\n"
		"  reboot            - Reboot the system\n"
		"  continue          - Resume execution\n"
		"  exit              - Exit debugger (same as continue)\n"
	);
}

/* ------------------------------------------------------------------
 *  dbg_cmd_regs - dump all CPU registers
 *
 *  Reads saved context from the exception frame, plus live segment
 *  and control registers.
 * ------------------------------------------------------------------ */
static void dbg_cmd_regs(void)
{
	regs_t *r = saved_regs;

	if (!r) {
		/* Read from current processor state. */
		dbg_output("\nNo saved register context available.\n");
		dbg_output("Reading live registers (may be inaccurate):\n\n");

		uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, eip, eflags;
		uint16_t cs, ds, ss, es, fs, gs;

		/* Push/pop to read EIP and EFLAGS. */
		__asm__ volatile(
			"call 1f\n"
			"1: pop %0\n"
			: "=r"(eip)
		);
		__asm__ volatile(
			"pushfl\n"
			"pop %0\n"
			: "=r"(eflags)
		);

		__asm__ volatile("mov %%eax, %0" : "=r"(eax));
		__asm__ volatile("mov %%ebx, %0" : "=r"(ebx));
		__asm__ volatile("mov %%ecx, %0" : "=r"(ecx));
		__asm__ volatile("mov %%edx, %0" : "=r"(edx));
		__asm__ volatile("mov %%esi, %0" : "=r"(esi));
		__asm__ volatile("mov %%edi, %0" : "=r"(edi));
		__asm__ volatile("mov %%ebp, %0" : "=r"(ebp));
		__asm__ volatile("mov %%esp, %0" : "=r"(esp));

		cs = dbg_read_seg("cs");
		ds = dbg_read_seg("ds");
		ss = dbg_read_seg("ss");
		es = dbg_read_seg("es");
		fs = dbg_read_seg("fs");
		gs = dbg_read_seg("gs");

		dbg_output("EAX=");   dbg_output_hex(eax);   dbg_output("  ");
		dbg_output("EBX=");   dbg_output_hex(ebx);   dbg_output("  ");
		dbg_output("ECX=");   dbg_output_hex(ecx);   dbg_output("  ");
		dbg_output("EDX=");   dbg_output_hex(edx);   dbg_output("\n");
		dbg_output("ESI=");   dbg_output_hex(esi);   dbg_output("  ");
		dbg_output("EDI=");   dbg_output_hex(edi);   dbg_output("  ");
		dbg_output("EBP=");   dbg_output_hex(ebp);   dbg_output("  ");
		dbg_output("ESP=");   dbg_output_hex(esp);   dbg_output("\n");
		dbg_output("EIP=");   dbg_output_hex(eip);   dbg_output("  ");
		dbg_output("EFLAGS="); dbg_output_hex(eflags); dbg_output("\n");
		dbg_output("CS=");    dbg_output_hex(cs);    dbg_output("  ");
		dbg_output("DS=");    dbg_output_hex(ds);    dbg_output("  ");
		dbg_output("SS=");    dbg_output_hex(ss);    dbg_output("  ");
		dbg_output("ES=");    dbg_output_hex(es);    dbg_output("  ");
		dbg_output("FS=");    dbg_output_hex(fs);    dbg_output("  ");
		dbg_output("GS=");    dbg_output_hex(gs);    dbg_output("\n");
		dbg_output("CR0=");   dbg_output_hex(dbg_read_cr(0));
		dbg_output("  CR2="); dbg_output_hex(dbg_read_cr(2));
		dbg_output("  CR3="); dbg_output_hex(dbg_read_cr(3));
		dbg_output("  CR4="); dbg_output_hex(dbg_read_cr(4));
		dbg_output("\n");

		return;
	}

	/* Print from saved exception frame. */
	dbg_output("\n--- Register Dump (from exception frame) ---\n\n");

	dbg_output("EAX="); dbg_output_hex(r->eax);
	dbg_output("  EBX="); dbg_output_hex(r->ebx);
	dbg_output("  ECX="); dbg_output_hex(r->ecx);
	dbg_output("  EDX="); dbg_output_hex(r->edx);
	dbg_output("\n");

	dbg_output("ESI="); dbg_output_hex(r->esi);
	dbg_output("  EDI="); dbg_output_hex(r->edi);
	dbg_output("  EBP="); dbg_output_hex(r->ebp);
	dbg_output("  ESP="); dbg_output_hex(r->esp_kernel);
	dbg_output("\n");

	dbg_output("EIP="); dbg_output_hex(r->eip);
	dbg_output("  EFLAGS="); dbg_output_hex(r->eflags);
	dbg_output("\n");

	dbg_output("CS=");  dbg_output_hex(r->cs);
	dbg_output("  SS=");  dbg_output_hex(r->ss);
	dbg_output("  DS=");  dbg_output_hex(dbg_read_seg("ds"));
	dbg_output("  ES=");  dbg_output_hex(dbg_read_seg("es"));
	dbg_output("\n");

	dbg_output("FS=");  dbg_output_hex(dbg_read_seg("fs"));
	dbg_output("  GS=");  dbg_output_hex(dbg_read_seg("gs"));
	dbg_output("\n");

	dbg_output("CR0="); dbg_output_hex(dbg_read_cr(0));
	dbg_output("  CR2="); dbg_output_hex(dbg_read_cr(2));
	dbg_output("  CR3="); dbg_output_hex(dbg_read_cr(3));
	dbg_output("  CR4="); dbg_output_hex(dbg_read_cr(4));
	dbg_output("\n");

	dbg_output("Int#="); dbg_output_dec(r->int_no);
	dbg_output("  ErrCode="); dbg_output_hex(r->err_code);
	dbg_output("\n");

	/* Resolve EIP to symbol. */
	char sym[64];
	uint32_t off = ksym_lookup_name(r->eip, sym, sizeof(sym));
	if (sym[0]) {
		dbg_output("Symbol: ");
		dbg_output(sym);
		if (off > 0) {
			dbg_output(" + 0x");
			dbg_output_hex(off);
		}
		dbg_output("\n");
	}
}

/* ------------------------------------------------------------------
 *  dbg_cmd_mem - hexdump memory (xxd-style)
 *
 *  Usage: mem <addr> [length]
 *  Shows hex and ASCII side-by-side, DBG_MEM_WIDTH bytes per line.
 * ------------------------------------------------------------------ */
static void dbg_cmd_mem(const char *args)
{
	uint32_t addr = 0;
	uint32_t length = 256;  /* default 256 bytes */
	int i;

	if (!args || args[0] == '\0') {
		dbg_output("\nUsage: mem <addr> [length]\n");
		return;
	}

	/* Parse hex address. */
	addr = 0;
	while (*args == ' ') args++;
	if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X'))
		args += 2;

	while (*args && *args != ' ') {
		char c = *args;
		uint8_t nibble;

		if (c >= '0' && c <= '9')
			nibble = c - '0';
		else if (c >= 'a' && c <= 'f')
			nibble = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			nibble = c - 'A' + 10;
		else
			break;

		addr = (addr << 4) | nibble;
		args++;
	}

	/* Parse optional length (decimal or hex). */
	while (*args == ' ') args++;
	if (*args) {
		length = 0;
		if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X'))
			args += 2;

		while (*args) {
			char c = *args;
			uint8_t nibble;

			if (c >= '0' && c <= '9')
				nibble = c - '0';
			else if (c >= 'a' && c <= 'f')
				nibble = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				nibble = c - 'A' + 10;
			else
				break;

			length = (length << 4) | nibble;
			args++;
		}
	}

	if (length == 0) length = 256;

	/* Align start address to 16-byte boundary for readability. */
	uint32_t start = addr & ~(uint32_t)(DBG_MEM_WIDTH - 1);
	uint32_t end = addr + length;

	dbg_output("\n");
	dbg_output("Hexdump of ");
	dbg_output_hex(addr);
	dbg_output(" (");
	dbg_output_dec(length);
	dbg_output(" bytes):\n");

	for (uint32_t offset = start; offset < end; offset += DBG_MEM_WIDTH) {
		/* Print address. */
		dbg_output_ptr(offset);
		dbg_output("  ");

		char ascii[DBG_MEM_WIDTH + 1];
		uint8_t *ptr = (uint8_t *)offset;

		/* Print hex bytes. */
		for (i = 0; i < DBG_MEM_WIDTH; i++) {
			uint32_t byte_addr = offset + i;

			if (byte_addr < addr || byte_addr >= (addr + length)) {
				dbg_output("   ");
				ascii[i] = ' ';
			} else {
				uint8_t b = ptr[i];
				char hex[] = "0123456789ABCDEF";
				serial_putchar(DBG_PORT, hex[(b >> 4) & 0x0F]);
				serial_putchar(DBG_PORT, hex[b & 0x0F]);
				serial_putchar(DBG_PORT, ' ');

				/* Printable ASCII? */
				if (b >= 0x20 && b <= 0x7E)
					ascii[i] = (char)b;
				else
					ascii[i] = '.';
			}
		}

		/* Print ASCII column. */
		dbg_output(" |");
		ascii[DBG_MEM_WIDTH] = '\0';
		dbg_output(ascii);
		dbg_output("|\n");
	}
}

/* ------------------------------------------------------------------
 *  dbg_cmd_bt - stack backtrace
 *
 *  Walks the EBP chain.  Each frame has:
 *    [EBP+0] = saved EBP (previous frame)
 *    [EBP+4] = return EIP
 *  Resolves EIP to symbol names via ksym_lookup_name.
 * ------------------------------------------------------------------ */
static void dbg_cmd_bt(void)
{
	uint32_t ebp, eip;
	uint32_t *frame;
	int depth = 0;
	char sym[64];
	uint32_t off;

	if (!saved_regs) {
		/* Read current EBP and EIP. */
		__asm__ volatile("mov %%ebp, %0" : "=r"(ebp));
		__asm__ volatile(
			"call 1f\n"
			"1: pop %0\n"
			: "=r"(eip)
		);
	} else {
		ebp = saved_regs->ebp;
		eip = saved_regs->eip;
	}

	dbg_output("\n--- Stack Backtrace ---\n");

	/* Frame 0: current/exception point. */
	dbg_output(" #0  EIP=");
	dbg_output_hex(eip);
	off = ksym_lookup_name(eip, sym, sizeof(sym));
	if (sym[0]) {
		dbg_output("  ");
		dbg_output(sym);
		if (off > 0) {
			dbg_output("+0x");
			dbg_output_hex(off);
		}
	}
	dbg_output("\n");

	/* Walk the EBP chain. */
	frame = (uint32_t *)ebp;
	depth = 1;

	while (frame && depth < 64) {
		/* Sanity check: frame pointer must look reasonable. */
		if ((uint32_t)frame < 0x100000 ||
		    (uint32_t)frame > 0xFFF00000) {
			dbg_output("  (EBP chain appears invalid)\n");
			break;
		}

		uint32_t prev_ebp = frame[0];
		uint32_t ret_eip  = frame[1];

		/* Check if return EIP is in kernel range. */
		if (ret_eip < 0x100000 || ret_eip > 0xFFF00000) {
			dbg_output("  (EIP outside kernel range, stopping)\n");
			break;
		}

		dbg_output(" #");
		if (depth < 10) serial_putchar(DBG_PORT, ' ');
		dbg_output_dec(depth);
		dbg_output("  EBP=");
		dbg_output_hex((uint32_t)frame);
		dbg_output("  EIP=");
		dbg_output_hex(ret_eip);

		off = ksym_lookup_name(ret_eip, sym, sizeof(sym));
		if (sym[0]) {
			dbg_output("  ");
			dbg_output(sym);
			if (off > 0) {
				dbg_output("+0x");
				dbg_output_hex(off);
			}
		}

		dbg_output("\n");

		/* Stop if we've reached the top of the stack. */
		if (prev_ebp == 0 || prev_ebp <= (uint32_t)frame)
			break;

		frame = (uint32_t *)prev_ebp;
		depth++;
	}
}

/* ------------------------------------------------------------------
 *  Breakpoint helpers
 * ------------------------------------------------------------------ */

/* Write a byte to a code page (temporarily enable writes). */
static int bp_write_byte(uint32_t addr, uint8_t byte)
{
	/* On x86, we can use CR0.WP to disable write-protection
	 * temporarily, or we can temporarily remap the page.
	 * For simplicity, we disable write-protect in CR0. */
	uint32_t cr0;
	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));

	/* Clear WP (bit 16). */
	uint32_t cr0_new = cr0 & ~0x10000;
	__asm__ volatile("mov %0, %%cr0" : : "r"(cr0_new));

	uint8_t *ptr = (uint8_t *)addr;
	*ptr = byte;

	/* Restore CR0. */
	__asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

	return 0;
}

static uint8_t bp_read_byte(uint32_t addr)
{
	uint8_t *ptr = (uint8_t *)addr;
	return *ptr;
}

static void dbg_cmd_bp_set(const char *args)
{
	uint32_t addr = 0;

	if (!args || args[0] == '\0') {
		dbg_output("\nUsage: bp set <addr>\n");
		return;
	}

	while (*args == ' ') args++;
	if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X'))
		args += 2;

	while (*args) {
		char c = *args;
		uint8_t nibble;
		if (c >= '0' && c <= '9') nibble = c - '0';
		else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
		else break;
		addr = (addr << 4) | nibble;
		args++;
	}

	if (addr == 0) {
		dbg_output("\nInvalid address.\n");
		return;
	}

	if (bp_count >= DBG_MAX_BP) {
		dbg_output("\nMaximum breakpoints reached (");
		dbg_output_dec(DBG_MAX_BP);
		dbg_output(").\n");
		return;
	}

	/* Check for duplicate. */
	for (int i = 0; i < bp_count; i++) {
		if (bp_table[i].addr == addr) {
			dbg_output("\nBreakpoint already set at ");
			dbg_output_hex(addr);
			dbg_output("\n");
			return;
		}
	}

	/* Save original byte and write INT3 (0xCC). */
	bp_table[bp_count].addr = addr;
	bp_table[bp_count].orig_byte = bp_read_byte(addr);
	bp_table[bp_count].enabled = 1;

	bp_write_byte(addr, 0xCC);  /* INT3 opcode */
	bp_count++;

	dbg_output("\nBreakpoint ");
	dbg_output_dec(bp_count - 1);
	dbg_output(" set at ");
	dbg_output_hex(addr);
	dbg_output(" (orig byte: ");
	dbg_output_hex(bp_table[bp_count - 1].orig_byte);
	dbg_output(")\n");
}

static void dbg_cmd_bp_list(void)
{
	dbg_output("\n--- Breakpoints ---\n");

	if (bp_count == 0) {
		dbg_output("No breakpoints set.\n");
		return;
	}

	for (int i = 0; i < bp_count; i++) {
		dbg_output(" #");
		dbg_output_dec(i);
		dbg_output("  ");
		dbg_output_hex(bp_table[i].addr);
		dbg_output("  ");
		dbg_output(bp_table[i].enabled ? "enabled" : "disabled");
		dbg_output("\n");
	}
}

static void dbg_cmd_bp_clear(const char *args)
{
	uint32_t addr = 0;

	if (!args || args[0] == '\0') {
		dbg_output("\nUsage: bp clear <addr>\n");
		return;
	}

	while (*args == ' ') args++;
	if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X'))
		args += 2;

	while (*args) {
		char c = *args;
		uint8_t nibble;
		if (c >= '0' && c <= '9') nibble = c - '0';
		else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
		else break;
		addr = (addr << 4) | nibble;
		args++;
	}

	for (int i = 0; i < bp_count; i++) {
		if (bp_table[i].addr == addr) {
			/* Restore original byte. */
			bp_write_byte(addr, bp_table[i].orig_byte);

			dbg_output("\nBreakpoint at ");
			dbg_output_hex(addr);
			dbg_output(" cleared.\n");

			/* Remove from table by shifting. */
			for (int j = i; j < bp_count - 1; j++)
				bp_table[j] = bp_table[j + 1];
			bp_count--;
			return;
		}
	}

	dbg_output("\nNo breakpoint at ");
	dbg_output_hex(addr);
	dbg_output("\n");
}

/* ------------------------------------------------------------------
 *  dbg_cmd_step - single-step one instruction
 * ------------------------------------------------------------------ */
static void dbg_cmd_step(void)
{
	dbg_output("\nSingle-stepping...\n");
	dbg_step = 1;
	dbg_active = 0;
}

/* ------------------------------------------------------------------
 *  dbg_cmd_disasm - basic x86 disassembler
 *
 *  Decodes common x86 instructions: mov, add, sub, push, pop, call,
 *  jmp, ret, cmp, test, xor, or, and, inc, dec, int, nop, lea, jcc.
 *  This is a simplified disassembler; it handles the most common
 *  opcodes and addressing modes encountered in kernel code.
 * ------------------------------------------------------------------ */

/* Forward declaration for snprintf_fixed used by dbg_disasm_modrm. */
static int snprintf_fixed(char *buf, int size, const char *fmt, ...);

/* ModR/M decoding helper. */
static uint32_t dbg_disasm_modrm(uint8_t *code, uint32_t addr,
				 char *out, int out_size)
{
	uint8_t modrm = code[0];
	uint8_t mod   = (modrm >> 6) & 0x03;
	uint8_t reg   = (modrm >> 3) & 0x07;
	uint8_t rm    = modrm & 0x07;

	static const char *regs8[]  = {"al","cl","dl","bl","ah","ch","dh","bh"};
	static const char *regs16[] = {"ax","cx","dx","bx","sp","bp","si","di"};
	static const char *regs32[]= {"eax","ecx","edx","ebx","esp","ebp","esi","edi"};

	/* Determine operand size from prefix. */
	const char **regs = regs32;  /* default 32-bit */
	int opsize = 4;
	int addr_size = 4;
	int has_sib = 0;
	int disp = 0;
	int disp_size = 0;
	int pos = 0;

	/* Build the memory operand or register. */
	if (mod == 3) {
		/* Register direct. */
		pos += snprintf_fixed(out + pos, out_size - pos, "%s", regs[rm]);
		return 1;
	}

	/* Memory operand. */
	if (rm == 4) {
		/* SIB follows. */
		has_sib = 1;
	} else if (rm == 5 && mod == 0) {
		/* Direct displacement (disp32). */
		disp = *(int32_t *)(code + 1);
		disp_size = 4;
		pos += snprintf_fixed(out + pos, out_size - pos, "[0x%x]", disp);
		return 1 + disp_size;
	}

	pos += snprintf_fixed(out + pos, out_size - pos, "[%s", regs[rm]);

	if (has_sib) {
		uint8_t sib = code[1];
		uint8_t scale = (sib >> 6) & 0x03;
		uint8_t index = (sib >> 3) & 0x07;
		uint8_t base  = sib & 0x07;
		int extra = 1;

		if (index != 4) {
			pos += snprintf_fixed(out + pos, out_size - pos, "+%s", regs[index]);
			if (scale > 0) {
				pos += snprintf_fixed(out + pos, out_size - pos, "*%d", 1 << scale);
			}
		}

		if (base == 5 && mod == 0) {
			disp = *(int32_t *)(code + 2);
			disp_size = 4;
			pos += snprintf_fixed(out + pos, out_size - pos, "+0x%x", disp);
			pos += snprintf_fixed(out + pos, out_size - pos, "]");
			return 1 + extra + disp_size;
		}

		pos += snprintf_fixed(out + pos, out_size - pos, "]");

		if (mod == 1) {
			disp = (int8_t)code[1 + extra];
			disp_size = 1;
			pos += snprintf_fixed(out + pos, out_size - pos, "+%d", disp);
		} else if (mod == 2) {
			disp = *(int32_t *)(code + 1 + extra);
			disp_size = 4;
			pos += snprintf_fixed(out + pos, out_size - pos, "+0x%x", disp);
		}

		return 1 + extra + disp_size;
	}

	if (mod == 1) {
		disp = (int8_t)code[1];
		disp_size = 1;
		pos += snprintf_fixed(out + pos, out_size - pos, "+%d", disp);
	} else if (mod == 2) {
		disp = *(int32_t *)(code + 1);
		disp_size = 4;
		pos += snprintf_fixed(out + pos, out_size - pos, "+0x%x", disp);
	}

	pos += snprintf_fixed(out + pos, out_size - pos, "]");
	return 1 + disp_size;
}

/* Simple snprintf-like for fixed buffer. */
static int snprintf_fixed(char *buf, int size, const char *fmt, ...)
{
	/* Minimal implementation. */
	int pos = 0;
	const char *p = fmt;
	__builtin_va_list args;
	__builtin_va_start(args, fmt);

	while (*p && pos < size - 1) {
		if (*p == '%') {
			p++;
			if (*p == 's') {
				const char *s = __builtin_va_arg(args, const char *);
				if (s) {
					while (*s && pos < size - 1)
						buf[pos++] = *s++;
				}
			} else if (*p == 'x') {
				uint32_t v = __builtin_va_arg(args, uint32_t);
				/* Simple hex output. */
				char hex[] = "0123456789ABCDEF";
				char tmp[16];
				int j = 0;
				do {
					tmp[j++] = hex[v & 0xF];
					v >>= 4;
				} while (v > 0);
				while (j > 0 && pos < size - 1)
					buf[pos++] = tmp[--j];
			} else if (*p == 'd') {
				int v = __builtin_va_arg(args, int);
				char tmp[16];
				int j = 0;
				int neg = 0;
				if (v < 0) { neg = 1; v = -v; }
				do {
					tmp[j++] = '0' + (v % 10);
					v /= 10;
				} while (v > 0);
				if (neg && pos < size - 1)
					buf[pos++] = '-';
				while (j > 0 && pos < size - 1)
					buf[pos++] = tmp[--j];
			}
		} else {
			buf[pos++] = *p;
		}
		p++;
	}

	buf[pos] = '\0';
	__builtin_va_end(args);
	return pos;
}

static const char *dbg_get_reg_name(uint8_t reg, int is32)
{
	static const char *r32[] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
	static const char *r16[] = {"ax","cx","dx","bx","sp","bp","si","di"};
	static const char *r8[]  = {"al","cl","dl","bl","ah","ch","dh","bh"};
	return is32 ? r32[reg & 7] : r8[reg & 7];
}

static int dbg_disasm_one(uint8_t *code, uint32_t addr, char *mnem, int msize)
{
	uint8_t op = code[0];
	static const char *jcc_names[] = {
		"jo","jno","jb","jnb","jz","jnz","jbe","ja",
		"js","jns","jpe","jpo","jl","jge","jle","jg"
	};

	mnem[0] = '\0';

	switch (op) {
	/* NOP */
	case 0x90:
		return snprintf_fixed(mnem, msize, "nop"), 1;

	/* RET */
	case 0xC3:
		return snprintf_fixed(mnem, msize, "ret"), 1;
	case 0xC2:
		return snprintf_fixed(mnem, msize, "ret 0x%x", *(uint16_t *)(code + 1)), 3;

	/* INT3 */
	case 0xCC:
		return snprintf_fixed(mnem, msize, "int3"), 1;

	/* INT imm8 */
	case 0xCD:
		return snprintf_fixed(mnem, msize, "int 0x%x", code[1]), 2;

	/* PUSH reg */
	case 0x50: case 0x51: case 0x52: case 0x53:
	case 0x54: case 0x55: case 0x56: case 0x57:
		return snprintf_fixed(mnem, msize, "push %s", dbg_get_reg_name(op - 0x50, 1)), 1;

	/* POP reg */
	case 0x58: case 0x59: case 0x5A: case 0x5B:
	case 0x5C: case 0x5D: case 0x5E: case 0x5F:
		return snprintf_fixed(mnem, msize, "pop %s", dbg_get_reg_name(op - 0x58, 1)), 1;

	/* PUSH imm32 */
	case 0x68:
		return snprintf_fixed(mnem, msize, "push 0x%x", *(uint32_t *)(code + 1)), 5;

	/* PUSH imm8 */
	case 0x6A:
		return snprintf_fixed(mnem, msize, "push 0x%x", (uint32_t)(int8_t)code[1]), 2;

	/* POP ES/SS/DS/FS/GS */
	case 0x07: case 0x17: case 0x1F:
		{
			const char *seg = op == 0x07 ? "es" : op == 0x17 ? "ss" : "ds";
			return snprintf_fixed(mnem, msize, "pop %s", seg), 1;
		}

	/* CALL rel32 */
	case 0xE8:
		{
			int32_t rel = *(int32_t *)(code + 1);
			return snprintf_fixed(mnem, msize, "call 0x%x", addr + 5 + rel), 5;
		}

	/* CALL r/m */
	case 0xFF:
		{
			uint8_t modrm = code[1];
			uint8_t reg = (modrm >> 3) & 7;
			if (reg == 2) {
				char opstr[64];
				int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
				return snprintf_fixed(mnem, msize, "call %s", opstr), 1 + len;
			}
			if (reg == 4) {
				char opstr[64];
				int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
				return snprintf_fixed(mnem, msize, "jmp %s", opstr), 1 + len;
			}
			return snprintf_fixed(mnem, msize, "???"), 1;
		}

	/* JMP rel8 */
	case 0xEB:
		{
			int8_t rel = (int8_t)code[1];
			return snprintf_fixed(mnem, msize, "jmp 0x%x", addr + 2 + rel), 2;
		}

	/* JMP rel32 */
	case 0xE9:
		{
			int32_t rel = *(int32_t *)(code + 1);
			return snprintf_fixed(mnem, msize, "jmp 0x%x", addr + 5 + rel), 5;
		}

	/* Jcc rel8 (0x70-0x7F) */
	case 0x70: case 0x71: case 0x72: case 0x73:
	case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7A: case 0x7B:
	case 0x7C: case 0x7D: case 0x7E: case 0x7F:
		{
			int8_t rel = (int8_t)code[1];
			return snprintf_fixed(mnem, msize, "%s 0x%x",
				jcc_names[op - 0x70], addr + 2 + rel), 2;
		}

	/* Jcc rel32 (0x0F 0x80-0x8F) */
	case 0x0F:
		{
			uint8_t op2 = code[1];
			if (op2 >= 0x80 && op2 <= 0x8F) {
				int32_t rel = *(int32_t *)(code + 2);
				return snprintf_fixed(mnem, msize, "%s 0x%x",
					jcc_names[op2 - 0x80], addr + 6 + rel), 6;
			}
			return snprintf_fixed(mnem, msize, "???"), 1;
		}

	/* MOV r/m8, r8 */
	case 0x88:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			uint8_t rm = modrm & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "mov %s, %s", opstr, dbg_get_reg_name(r, 0)), 1 + len;
		}

	/* MOV r/m32, r32 */
	case 0x89:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "mov %s, %s", opstr, dbg_get_reg_name(r, 1)), 1 + len;
		}

	/* MOV r8, r/m8 */
	case 0x8A:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "mov %s, %s", dbg_get_reg_name(r, 0), opstr), 1 + len;
		}

	/* MOV r32, r/m32 */
	case 0x8B:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "mov %s, %s", dbg_get_reg_name(r, 1), opstr), 1 + len;
		}

	/* MOV reg, imm32 */
	case 0xB8: case 0xB9: case 0xBA: case 0xBB:
	case 0xBC: case 0xBD: case 0xBE: case 0xBF:
		return snprintf_fixed(mnem, msize, "mov %s, 0x%x",
			dbg_get_reg_name(op - 0xB8, 1),
			*(uint32_t *)(code + 1)), 5;

	/* MOV r/m8, imm8 */
	case 0xC6:
		{
			uint8_t modrm = code[1];
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "mov byte %s, 0x%x", opstr, code[1 + len]), 1 + len + 1;
		}

	/* MOV r/m32, imm32 */
	case 0xC7:
		{
			uint8_t modrm = code[1];
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "mov %s, 0x%x", opstr, *(uint32_t *)(code + 1 + len)), 1 + len + 4;
		}

	/* ADD r/m32, r32 */
	case 0x01:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "add %s, %s", opstr, dbg_get_reg_name(r, 1)), 1 + len;
		}

	/* ADD r32, r/m32 */
	case 0x03:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "add %s, %s", dbg_get_reg_name(r, 1), opstr), 1 + len;
		}

	/* ADD eax, imm32 */
	case 0x05:
		return snprintf_fixed(mnem, msize, "add eax, 0x%x", *(uint32_t *)(code + 1)), 5;

	/* ADD r/m32, imm8 */
	case 0x83:
		{
			uint8_t modrm = code[1];
			uint8_t reg = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			const char *opname = "add";
			if (reg == 5) opname = "sub";
			else if (reg == 7) opname = "cmp";
			else if (reg == 4) opname = "and";
			else if (reg == 1) opname = "or";
			else if (reg == 6) opname = "xor";
			return snprintf_fixed(mnem, msize, "%s %s, 0x%x", opname, opstr,
				(uint32_t)(int8_t)code[1 + len]), 1 + len + 1;
		}

	/* ADD r/m32, imm32 */
	case 0x81:
		{
			uint8_t modrm = code[1];
			uint8_t reg = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			const char *opname = "add";
			if (reg == 5) opname = "sub";
			else if (reg == 7) opname = "cmp";
			else if (reg == 4) opname = "and";
			else if (reg == 1) opname = "or";
			else if (reg == 6) opname = "xor";
			return snprintf_fixed(mnem, msize, "%s %s, 0x%x", opname, opstr,
				*(uint32_t *)(code + 1 + len)), 1 + len + 4;
		}

	/* SUB r/m32, r32 */
	case 0x29:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "sub %s, %s", opstr, dbg_get_reg_name(r, 1)), 1 + len;
		}

	/* SUB r32, r/m32 */
	case 0x2B:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "sub %s, %s", dbg_get_reg_name(r, 1), opstr), 1 + len;
		}

	/* CMP r/m32, r32 */
	case 0x39:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "cmp %s, %s", opstr, dbg_get_reg_name(r, 1)), 1 + len;
		}

	/* CMP r32, r/m32 */
	case 0x3B:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "cmp %s, %s", dbg_get_reg_name(r, 1), opstr), 1 + len;
		}

	/* TEST r/m32, r32 */
	case 0x85:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "test %s, %s", opstr, dbg_get_reg_name(r, 1)), 1 + len;
		}

	/* XOR r/m32, r32 */
	case 0x31:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "xor %s, %s", opstr, dbg_get_reg_name(r, 1)), 1 + len;
		}

	/* XOR r32, r/m32 */
	case 0x33:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "xor %s, %s", dbg_get_reg_name(r, 1), opstr), 1 + len;
		}

	/* OR r/m32, r32 */
	case 0x09:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "or %s, %s", opstr, dbg_get_reg_name(r, 1)), 1 + len;
		}

	/* AND r/m32, r32 */
	case 0x21:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "and %s, %s", opstr, dbg_get_reg_name(r, 1)), 1 + len;
		}

	/* INC reg */
	case 0x40: case 0x41: case 0x42: case 0x43:
	case 0x44: case 0x45: case 0x46: case 0x47:
		return snprintf_fixed(mnem, msize, "inc %s", dbg_get_reg_name(op - 0x40, 1)), 1;

	/* DEC reg */
	case 0x48: case 0x49: case 0x4A: case 0x4B:
	case 0x4C: case 0x4D: case 0x4E: case 0x4F:
		return snprintf_fixed(mnem, msize, "dec %s", dbg_get_reg_name(op - 0x48, 1)), 1;

	/* LEA r32, m */
	case 0x8D:
		{
			uint8_t modrm = code[1];
			uint8_t r = (modrm >> 3) & 7;
			char opstr[64];
			int len = dbg_disasm_modrm(code + 1, addr, opstr, sizeof(opstr));
			return snprintf_fixed(mnem, msize, "lea %s, %s", dbg_get_reg_name(r, 1), opstr), 1 + len;
		}

	/* LOOP */
	case 0xE2:
		{
			int8_t rel = (int8_t)code[1];
			return snprintf_fixed(mnem, msize, "loop 0x%x", addr + 2 + rel), 2;
		}

	/* LOOPNE/LOOPNZ */
	case 0xE0:
		{
			int8_t rel = (int8_t)code[1];
			return snprintf_fixed(mnem, msize, "loopne 0x%x", addr + 2 + rel), 2;
		}

	/* REP prefixes */
	case 0xF3:
		if (code[1] == 0xA5) return snprintf_fixed(mnem, msize, "rep movsd"), 2;
		if (code[1] == 0xAB) return snprintf_fixed(mnem, msize, "rep stosd"), 2;
		return snprintf_fixed(mnem, msize, "rep"), 1;

	default:
		return snprintf_fixed(mnem, msize, "db 0x%02x", op), 1;
	}
}

static void dbg_cmd_disasm(const char *args)
{
	uint32_t addr = 0;
	int count = 16;  /* default number of instructions */

	if (!args || args[0] == '\0') {
		/* Use saved EIP if available. */
		if (saved_regs)
			addr = saved_regs->eip;
		else {
			dbg_output("\nUsage: disasm <addr> [count]\n");
			return;
		}
	} else {
		while (*args == ' ') args++;
		if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X'))
			args += 2;

		while (*args && *args != ' ') {
			char c = *args;
			uint8_t nibble;
			if (c >= '0' && c <= '9') nibble = c - '0';
			else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
			else break;
			addr = (addr << 4) | nibble;
			args++;
		}

		while (*args == ' ') args++;
		if (*args) {
			count = 0;
			while (*args >= '0' && *args <= '9') {
				count = count * 10 + (*args - '0');
				args++;
			}
		}
	}

	if (count <= 0) count = 1;
	if (count > 64) count = 64;

	dbg_output("\n--- Disassembly (");
	dbg_output_dec(count);
	dbg_output(" instructions) at ");
	dbg_output_hex(addr);
	dbg_output(" ---\n");

	for (int i = 0; i < count; i++) {
		char mnem[128];
		uint8_t *code = (uint8_t *)addr;
		int len = dbg_disasm_one(code, addr, mnem, sizeof(mnem));

		dbg_output_ptr(addr);
		dbg_output(": ");

		/* Print raw bytes. */
		for (int j = 0; j < len && j < 8; j++) {
			char hex[] = "0123456789ABCDEF";
			serial_putchar(DBG_PORT, hex[(code[j] >> 4) & 0x0F]);
			serial_putchar(DBG_PORT, hex[code[j] & 0x0F]);
			serial_putchar(DBG_PORT, ' ');
		}
		for (int j = len; j < 8; j++)
			dbg_output("   ");

		dbg_output("  ");
		dbg_output(mnem);
		dbg_output("\n");

		addr += len;
		if (len == 0) break;
	}
}

/* ------------------------------------------------------------------
 *  dbg_cmd_ps - list processes and threads
 * ------------------------------------------------------------------ */
extern pcb_t *current_process;

static void dbg_cmd_ps(void)
{
	pcb_t *proc = current_process;
	pcb_t *first = proc;
	int count = 0;

	if (!proc) {
		dbg_output("\nNo processes found.\n");
		return;
	}

	dbg_output("\nPID  Name                State     Pri  CPU\n");
	dbg_output("---- -------------------- --------- ---- ----\n");

	/* Walk the process list using the doubly-linked list. */
	do {
		static const char *states[] = {
			"UNUSED", "READY", "RUNNING", "BLOCKED", "ZOMBIE"
		};
		const char *state_str = "??????";
		if (proc->state < 5)
			state_str = states[proc->state];

		/* PID */
		if (proc->pid < 10) serial_putchar(DBG_PORT, ' ');
		if (proc->pid < 100) serial_putchar(DBG_PORT, ' ');
		dbg_output_dec(proc->pid);
		dbg_output("  ");

		/* Name (pad to 20 chars). */
		dbg_output(proc->name);
		int namelen = (int)strlen(proc->name);
		for (int i = namelen; i < 20; i++)
			serial_putchar(DBG_PORT, ' ');
		dbg_output("  ");

		/* State. */
		dbg_output(state_str);
		for (int i = (int)strlen(state_str); i < 7; i++)
			serial_putchar(DBG_PORT, ' ');
		dbg_output("  ");

		/* Priority. */
		if (proc->priority < 100) serial_putchar(DBG_PORT, ' ');
		if (proc->priority < 10) serial_putchar(DBG_PORT, ' ');
		dbg_output_dec(proc->priority);
		dbg_output("  ");

		/* CPU time (ticks). */
		dbg_output_dec(proc->cpu_time);
		dbg_output("\n");

		count++;
		proc = proc->next;

		if (!proc || count > 256)
			break;
	} while (proc != first);

	dbg_output("\nTotal: ");
	dbg_output_dec(count);
	dbg_output(" process(es)\n");
}

/* ------------------------------------------------------------------
 *  dbg_cmd_page - walk page tables for a virtual address
 *
 *  Reads CR3 to get the current page directory, then walks the
 *  PDE -> PTE chain for the given virtual address.
 * ------------------------------------------------------------------ */
static void dbg_cmd_page(const char *args)
{
	uint32_t va = 0;

	if (!args || args[0] == '\0') {
		dbg_output("\nUsage: page <virtual_address>\n");
		return;
	}

	while (*args == ' ') args++;
	if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X'))
		args += 2;

	while (*args) {
		char c = *args;
		uint8_t nibble;
		if (c >= '0' && c <= '9') nibble = c - '0';
		else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
		else break;
		va = (va << 4) | nibble;
		args++;
	}

	uint32_t pd_index = (va >> 22) & 0x3FF;
	uint32_t pt_index = (va >> 12) & 0x3FF;
	uint32_t offset   = va & 0xFFF;

	uint32_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

	dbg_output("\n--- Page Table Walk for VA ");
	dbg_output_hex(va);
	dbg_output(" ---\n");

	dbg_output("CR3 (Page Directory Base): ");
	dbg_output_hex(cr3);
	dbg_output("\n");

	dbg_output("PDE Index: ");
	dbg_output_dec(pd_index);
	dbg_output(" (offset ");
	dbg_output_hex(pd_index * 4);
	dbg_output(")\n");

	dbg_output("PTE Index: ");
	dbg_output_dec(pt_index);
	dbg_output(" (offset ");
	dbg_output_hex(pt_index * 4);
	dbg_output(")\n");

	dbg_output("Page Offset: ");
	dbg_output_hex(offset);
	dbg_output("\n");

	/* Read PDE. */
	uint32_t *pde_ptr = (uint32_t *)(cr3 + pd_index * 4);
	uint32_t pde = *pde_ptr;

	dbg_output("\nPDE: ");
	dbg_output_hex(pde);
	dbg_output("\n");

	if (!(pde & 0x01)) {
		dbg_output("  -> NOT PRESENT\n");
		return;
	}

	dbg_output("  Present:       yes\n");
	dbg_output("  Read/Write:    ");
	dbg_output(pde & 0x02 ? "yes" : "no");
	dbg_output("\n");
	dbg_output("  User/Super:    ");
	dbg_output(pde & 0x04 ? "user" : "supervisor");
	dbg_output("\n");
	dbg_output("  Page Size:     ");
	dbg_output(pde & 0x80 ? "4MB" : "4KB");
	dbg_output("\n");

	/* If 4MB page, PDE maps directly. */
	if (pde & 0x80) {
		uint32_t phys = (pde & 0xFFC00000) | (va & 0x003FFFFF);
		dbg_output("  Physical Addr: ");
		dbg_output_hex(phys);
		dbg_output("\n");
		return;
	}

	/* Read PTE. */
	uint32_t pt_base = pde & 0xFFFFF000;
	uint32_t *pte_ptr = (uint32_t *)(pt_base + pt_index * 4);
	uint32_t pte = *pte_ptr;

	dbg_output("\nPTE: ");
	dbg_output_hex(pte);
	dbg_output("\n");

	if (!(pte & 0x01)) {
		dbg_output("  -> NOT PRESENT\n");
		return;
	}

	dbg_output("  Present:       yes\n");
	dbg_output("  Read/Write:    ");
	dbg_output(pte & 0x02 ? "yes" : "no");
	dbg_output("\n");
	dbg_output("  User/Super:    ");
	dbg_output(pte & 0x04 ? "user" : "supervisor");
	dbg_output("\n");
	dbg_output("  Dirty:         ");
	dbg_output(pte & 0x40 ? "yes" : "no");
	dbg_output("\n");
	dbg_output("  Accessed:       ");
	dbg_output(pte & 0x20 ? "yes" : "no");
	dbg_output("\n");

	uint32_t phys = (pte & 0xFFFFF000) | offset;
	dbg_output("  Physical Addr: ");
	dbg_output_hex(phys);
	dbg_output("\n");
}

/* ------------------------------------------------------------------
 *  dbg_cmd_memstat - memory statistics
 * ------------------------------------------------------------------ */
static void dbg_cmd_memstat(void)
{
	uint32_t total = pmm_get_total_pages();
	uint32_t used  = pmm_get_used_pages();
	uint32_t free  = pmm_get_free_pages();

	dbg_output("\n--- Memory Statistics ---\n");

	dbg_output("Physical Memory:\n");
	dbg_output("  Total pages: ");
	dbg_output_dec(total);
	dbg_output(" (");
	dbg_output_dec(total * 4 / 1024);
	dbg_output(" MB)\n");

	dbg_output("  Used pages:  ");
	dbg_output_dec(used);
	dbg_output(" (");
	dbg_output_dec(used * 4 / 1024);
	dbg_output(" MB)\n");

	dbg_output("  Free pages:  ");
	dbg_output_dec(free);
	dbg_output(" (");
	dbg_output_dec(free * 4 / 1024);
	dbg_output(" MB)\n");

	if (total > 0) {
		dbg_output("  Usage:       ");
		dbg_output_dec((used * 100) / total);
		dbg_output("%\n");
	}

	/* Kernel heap usage. */
	dbg_output("\nKernel Heap:\n");
	uint32_t hs = kheap_get_start();
	uint32_t hc = kheap_get_current();
	uint32_t heap_size = hc - hs;
	dbg_output("  Heap range:  ");
	dbg_output_hex(hs);
	dbg_output(" - ");
	dbg_output_hex(hc);
	dbg_output("\n");
	dbg_output("  Heap size:   ");
	dbg_output_dec(heap_size / 1024);
	dbg_output(" KB (");
	dbg_output_dec(heap_size);
	dbg_output(" bytes)\n");
}

/* ------------------------------------------------------------------
 *  dbg_cmd_reboot - triple-fault reboot
 * ------------------------------------------------------------------ */
static void dbg_cmd_reboot(void)
{
	dbg_output("\nRebooting...\n");

	/* Brief delay for the serial output to flush. */
	for (volatile int i = 0; i < 1000000; i++)
		;

	/* Load a zero IDT and trigger interrupt -> triple fault. */
	uint8_t idt_ptr[6];
	*(uint16_t *)idt_ptr = 0;
	*(uint32_t *)(idt_ptr + 2) = 0;
	__asm__ volatile("lidt %0" : : "m"(idt_ptr));
	__asm__ volatile("int $0");
}

/* ------------------------------------------------------------------
 *  Command parser
 * ------------------------------------------------------------------ */
static void dbg_parse_command(const char *line)
{
	/* Skip leading whitespace. */
	while (*line == ' ' || *line == '\t')
		line++;

	if (line[0] == '\0')
		return;

	/* help */
	if (strncmp(line, "help", 4) == 0) {
		dbg_cmd_help();
		return;
	}

	/* regs */
	if (strncmp(line, "regs", 4) == 0) {
		dbg_cmd_regs();
		return;
	}

	/* mem */
	if (strncmp(line, "mem ", 4) == 0 || strncmp(line, "mem", 3) == 0) {
		dbg_cmd_mem(line + 3);
		return;
	}

	/* bt */
	if (strncmp(line, "bt", 2) == 0) {
		dbg_cmd_bt();
		return;
	}

	/* bp */
	if (strncmp(line, "bp ", 3) == 0 || strncmp(line, "bp", 2) == 0) {
		const char *sub = line + 2;
		while (*sub == ' ') sub++;

		if (strncmp(sub, "set ", 4) == 0)
			dbg_cmd_bp_set(sub + 4);
		else if (strncmp(sub, "list", 4) == 0)
			dbg_cmd_bp_list();
		else if (strncmp(sub, "clear ", 6) == 0)
			dbg_cmd_bp_clear(sub + 6);
		else
			dbg_output("\nUsage: bp set|list|clear <addr>\n");
		return;
	}

	/* step */
	if (strncmp(line, "step", 4) == 0) {
		dbg_cmd_step();
		return;
	}

	/* disasm */
	if (strncmp(line, "disasm ", 7) == 0 || strncmp(line, "disasm", 6) == 0) {
		dbg_cmd_disasm(line + 6);
		return;
	}

	/* ps */
	if (strncmp(line, "ps", 2) == 0) {
		dbg_cmd_ps();
		return;
	}

	/* page */
	if (strncmp(line, "page ", 5) == 0 || strncmp(line, "page", 4) == 0) {
		dbg_cmd_page(line + 4);
		return;
	}

	/* memstat */
	if (strncmp(line, "memstat", 7) == 0) {
		dbg_cmd_memstat();
		return;
	}

	/* reboot */
	if (strncmp(line, "reboot", 6) == 0) {
		dbg_cmd_reboot();
		return;
	}

	/* continue / exit */
	if (strncmp(line, "continue", 8) == 0 ||
	    strncmp(line, "exit", 4) == 0 ||
	    strncmp(line, "c", 1) == 0) {
		dbg_output("\nResuming execution...\n");
		dbg_active = 0;
		return;
	}

	dbg_output("\nUnknown command. Type 'help' for available commands.\n");
}

/* ------------------------------------------------------------------
 *  dbg_interactive - main debugger interactive loop
 * ------------------------------------------------------------------ */
static void dbg_interactive(void)
{
	char line[DBG_MAX_CMD];

	dbg_output("\n");
	dbg_output("========================================\n");
	dbg_output("  FunsOS Kernel Debugger\n");
	dbg_output("========================================\n");

	if (saved_regs) {
		dbg_output("Entered from exception #");
		dbg_output_dec(saved_regs->int_no);
		dbg_output(" at EIP=");
		dbg_output_hex(saved_regs->eip);
		dbg_output("\n");
	}

	while (dbg_active) {
		dbg_prompt();
		dbg_readline(line, DBG_MAX_CMD);
		dbg_parse_command(line);
	}
}

/* ------------------------------------------------------------------
 *  dbg_handle_breakpoint - handle INT3 (#BP) exception
 *
 *  Called when we hit a software breakpoint.  We need to:
 *  1. Restore the original byte
 *  2. Rewind EIP by 1 (INT3 is 1 byte)
 *  3. Enter the debugger
 *  4. Optionally re-arm the breakpoint on continue
 * ------------------------------------------------------------------ */
static void dbg_handle_breakpoint(regs_t *regs)
{
	uint32_t bp_addr = regs->eip - 1;  /* INT3 is at EIP-1 */

	/* Find and temporarily clear this breakpoint. */
	for (int i = 0; i < bp_count; i++) {
		if (bp_table[i].addr == bp_addr && bp_table[i].enabled) {
			bp_write_byte(bp_addr, bp_table[i].orig_byte);
			regs->eip = bp_addr;  /* rewind EIP to original instruction */
			break;
		}
	}
}

/* ------------------------------------------------------------------
 *  dbg_handle_single_step - handle #DB (debug exception) for single-step
 *
 *  Called after each instruction when TF flag is set.  We clear TF
 *  and enter the debugger.
 * ------------------------------------------------------------------ */
static void dbg_handle_single_step(regs_t *regs)
{
	/* Clear the Trap Flag in EFLAGS. */
	regs->eflags &= ~0x100;

	dbg_output("\n--- Single Step ---\n");
	dbg_output("EIP=");
	dbg_output_hex(regs->eip);

	/* Resolve symbol. */
	char sym[64];
	uint32_t off = ksym_lookup_name(regs->eip, sym, sizeof(sym));
	if (sym[0]) {
		dbg_output("  ");
		dbg_output(sym);
		if (off > 0) {
			dbg_output("+0x");
			dbg_output_hex(off);
		}
	}
	dbg_output("\n");
}

/* ------------------------------------------------------------------
 *  dbg_rearm_breakpoints - re-insert INT3 on all enabled breakpoints
 * ------------------------------------------------------------------ */
static void dbg_rearm_breakpoints(void)
{
	for (int i = 0; i < bp_count; i++) {
		if (bp_table[i].enabled) {
			bp_write_byte(bp_table[i].addr, 0xCC);
		}
	}
}

/* ------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------ */

/*
 * dbg_init - initialise the kernel debugger
 * Called once at boot.  Sets up serial port and registers
 * the debug exception handler.
 */
void kdebug_init(void)
{
	dbg_enabled = 1;
	dbg_active = 0;
	dbg_step = 0;
	bp_count = 0;
	saved_regs = NULL;

	for (int i = 0; i < DBG_MAX_BP; i++) {
		bp_table[i].addr = 0;
		bp_table[i].orig_byte = 0;
		bp_table[i].enabled = 0;
	}

	klog_info("kdebug: initialised");
}

/*
 * kdebug_enter - enter the debugger from an exception handler
 *
 * Called from exception_handler() and keyboard_handler() when
 * Ctrl+Shift+D is pressed.  'regs' is the saved register state
 * from the interrupt/exception frame.
 *
 * For exception entry, we present the state and allow inspection.
 * For breakpoint (#BP), we handle the breakpoint mechanics first.
 * For single-step (#DB), we show the current state.
 */
void kdebug_enter(regs_t *regs)
{
	if (!dbg_enabled)
		return;

	saved_regs = regs;
	dbg_active = 1;

	/* Handle special exception types. */
	if (regs->int_no == 3) {
		/* Breakpoint exception. */
		dbg_handle_breakpoint(regs);
	}

	if (regs->int_no == 1) {
		/* Debug exception (single-step). */
		dbg_handle_single_step(regs);
	}

	/* Enter interactive debugger. */
	dbg_interactive();

	/* On exit from debugger, re-arm breakpoints. */
	dbg_rearm_breakpoints();

	saved_regs = NULL;
}

/*
 * kdebug_poll - poll for debugger activation from serial port
 *
 * Call this from the main loop or idle loop.  If a character is
 * available on the serial port, enter the debugger.
 */
void kdebug_poll(void)
{
	if (!dbg_enabled || dbg_active)
		return;

	if (serial_available(DBG_PORT)) {
		/* Read the character that triggered the poll. */
		char c = serial_read(DBG_PORT);
		(void)c;

		/* Build a minimal register frame. */
		regs_t fake_regs;
		memset(&fake_regs, 0, sizeof(fake_regs));

		__asm__ volatile("mov %%eax, %0" : "=r"(fake_regs.eax));
		__asm__ volatile("mov %%ebx, %0" : "=r"(fake_regs.ebx));
		__asm__ volatile("mov %%ecx, %0" : "=r"(fake_regs.ecx));
		__asm__ volatile("mov %%edx, %0" : "=r"(fake_regs.edx));
		__asm__ volatile("mov %%esi, %0" : "=r"(fake_regs.esi));
		__asm__ volatile("mov %%edi, %0" : "=r"(fake_regs.edi));
		__asm__ volatile("mov %%ebp, %0" : "=r"(fake_regs.ebp));
		__asm__ volatile("mov %%esp, %0" : "=r"(fake_regs.esp_kernel));
		__asm__ volatile(
			"call 1f\n"
			"1: pop %0\n"
			: "=r"(fake_regs.eip)
		);
		__asm__ volatile("pushfl; pop %0" : "=r"(fake_regs.eflags));
		fake_regs.int_no = 0;
		fake_regs.err_code = 0;

		kdebug_enter(&fake_regs);
	}
}

/*
 * kdebug_check_key - check if a keyboard event should trigger the debugger
 *
 * Call this from keyboard_handler() or keyboard poll.  Returns 1 if
 * the debugger was triggered (Ctrl+Shift+D).
 */
int kdebug_check_key(uint8_t scancode, uint8_t flags)
{
	if (!dbg_enabled || dbg_active)
		return 0;

	/* Check for Ctrl+Shift+D: scancode 0x20 (D key). */
	if (scancode == 0x20 &&
	    (flags & KEY_CTRL) &&
	    (flags & KEY_SHIFT) &&
	    (flags & KEY_PRESSED)) {
		/* Build a minimal register frame. */
		regs_t fake_regs;
		memset(&fake_regs, 0, sizeof(fake_regs));

		__asm__ volatile("mov %%eax, %0" : "=r"(fake_regs.eax));
		__asm__ volatile("mov %%ebx, %0" : "=r"(fake_regs.ebx));
		__asm__ volatile("mov %%ecx, %0" : "=r"(fake_regs.ecx));
		__asm__ volatile("mov %%edx, %0" : "=r"(fake_regs.edx));
		__asm__ volatile("mov %%esi, %0" : "=r"(fake_regs.esi));
		__asm__ volatile("mov %%edi, %0" : "=r"(fake_regs.edi));
		__asm__ volatile("mov %%ebp, %0" : "=r"(fake_regs.ebp));
		__asm__ volatile("mov %%esp, %0" : "=r"(fake_regs.esp_kernel));
		__asm__ volatile(
			"call 1f\n"
			"1: pop %0\n"
			: "=r"(fake_regs.eip)
		);
		__asm__ volatile("pushfl; pop %0" : "=r"(fake_regs.eflags));
		fake_regs.int_no = 0;
		fake_regs.err_code = 0;

		kdebug_enter(&fake_regs);
		return 1;
	}

	return 0;
}

/*
 * kdebug_is_active - query whether the debugger is currently active
 */
int kdebug_is_active(void)
{
	return dbg_active;
}