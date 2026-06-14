#include "panic.h"
#include "version.h"

static volatile uint16_t *vga_buffer = (uint16_t *)0xB8000;
static uint8_t current_color = 0x07;
static int cursor_row = 0;
static int cursor_col = 0;

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

void vga_set_color(uint8_t fg, uint8_t bg)
{
    current_color = (bg << 4) | (fg & 0x0F);
}

void vga_clear(void)
{
    int i;
    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (uint16_t)(' ' | (current_color << 8));
    }
    cursor_row = 0;
    cursor_col = 0;
}

static void vga_scroll(void)
{
    int i;
    for (i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }
    for (i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vga_buffer[i] = (uint16_t)(' ' | (current_color << 8));
    }
    cursor_row = VGA_HEIGHT - 1;
}

static void vga_update_cursor(void)
{
    uint16_t pos = cursor_row * VGA_WIDTH + cursor_col;
    asm volatile("outb %0, $0x3D4" : : "a"((uint8_t)14));
    asm volatile("outb %0, $0x3D5" : : "a"((uint8_t)(pos >> 8)));
    asm volatile("outb %0, $0x3D4" : : "a"((uint8_t)15));
    asm volatile("outb %0, $0x3D5" : : "a"((uint8_t)(pos & 0xFF)));
}

void vga_putchar(char c)
{
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\t') {
        cursor_col = (cursor_col + 8) & ~7;
    } else if (c == '\b') {
        if (cursor_col > 0) cursor_col--;
    } else {
        int offset = cursor_row * VGA_WIDTH + cursor_col;
        vga_buffer[offset] = (uint16_t)(c | (current_color << 8));
        cursor_col++;
    }

    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
    }
    if (cursor_row >= VGA_HEIGHT) {
        vga_scroll();
    }
    vga_update_cursor();
}

void vga_print(const char *str)
{
    if (!str) return;
    while (*str) {
        vga_putchar(*str);
        str++;
    }
}

static void vga_print_dec(uint32_t value)
{
    char buf[12];
    int i = 0;

    if (value == 0) {
        vga_putchar('0');
        return;
    }

    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (--i >= 0) {
        vga_putchar(buf[i]);
    }
}

void vga_print_hex(uint32_t value)
{
    char hex[] = "0123456789ABCDEF";
    char buf[9];
    int i;

    vga_print("0x");

    if (value == 0) {
        vga_putchar('0');
        return;
    }

    for (i = 7; i >= 0; i--) {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }
    buf[8] = '\0';

    int start = 0;
    while (start < 7 && buf[start] == '0') start++;
    vga_print(buf + start);
}

void kernel_panic(const char *msg, const char *file, int line)
{
    asm volatile("cli");

    vga_set_color(4, 15);
    vga_clear();

    vga_print("===== " KERNEL_STRING " PANIC =====\n\n");

    vga_set_color(0, 15);
    vga_print("Message: ");
    vga_set_color(4, 15);
    vga_print(msg);
    vga_putchar('\n');

    vga_set_color(0, 15);
    vga_print("File:    ");
    vga_set_color(4, 15);
    vga_print(file);
    vga_putchar('\n');

    vga_set_color(0, 15);
    vga_print("Line:    ");
    vga_set_color(4, 15);
    vga_print_dec((uint32_t)line);
    vga_print("\n\n");

    vga_set_color(0, 15);
    vga_print("Stack Trace:\n");

    uint32_t *ebp;
    uint32_t depth = 0;
    asm volatile("mov %%ebp, %0" : "=r"(ebp));

    while (ebp && depth < 16) {
        uint32_t ret_addr = *(ebp + 1);
        vga_print("  [");
        vga_print_dec(depth);
        vga_print("] ");
        vga_print_hex(ret_addr);
        vga_putchar('\n');
        ebp = (uint32_t *)*ebp;
        depth++;
    }

    vga_print("\nSystem halted.");

    while (1) {
        asm volatile("hlt");
    }
}

void panic_register_dump(regs_t *regs)
{
    if (!regs) return;

    vga_set_color(14, 0);
    vga_print("===== REGISTER DUMP =====\n\n");

    vga_set_color(7, 0);
    vga_print("EAX="); vga_print_hex(regs->eax); vga_print("  ");
    vga_print("EBX="); vga_print_hex(regs->ebx); vga_print("  ");
    vga_print("ECX="); vga_print_hex(regs->ecx); vga_print("  ");
    vga_print("EDX="); vga_print_hex(regs->edx); vga_print("\n");

    vga_print("ESI="); vga_print_hex(regs->esi); vga_print("  ");
    vga_print("EDI="); vga_print_hex(regs->edi); vga_print("  ");
    vga_print("EBP="); vga_print_hex(regs->ebp); vga_print("  ");
    vga_print("ESP="); vga_print_hex(regs->esp_kernel); vga_print("\n");

    vga_print("EIP="); vga_print_hex(regs->eip); vga_print("  ");
    vga_print("CS ="); vga_print_hex(regs->cs); vga_print("  ");
    vga_print("EFL="); vga_print_hex(regs->eflags); vga_print("\n");

    vga_print("INT="); vga_print_hex(regs->int_no); vga_print("  ");
    vga_print("ERR="); vga_print_hex(regs->err_code); vga_print("\n");

    if (regs->int_no == 14) {
        uint32_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        vga_print("CR2="); vga_print_hex(cr2); vga_print(" (page fault address)\n");
    }
}
