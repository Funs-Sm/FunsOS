#include "stacktrace.h"
#include "klog.h"
#include "string.h"

static int walk_stack(uint32_t fp, stacktrace_entry_t *entries,
                      int max_depth, int skip) {
    int depth = 0;
    stack_frame_t *frame = (stack_frame_t *)fp;

    while (frame && depth < max_depth) {
        if (skip > 0) {
            skip--;
            if (frame->next == NULL) break;
            frame = frame->next;
            continue;
        }

        uint32_t addr = frame->return_addr;
        if (addr == 0) break;

        entries[depth].address = addr;
        entries[depth].symbol[0] = '\0';
        entries[depth].offset = 0;

        stacktrace_resolve_symbol(addr, entries[depth].symbol,
                                  STACKTRACE_SYMBOL_MAX,
                                  &entries[depth].offset);

        depth++;

        if (frame->next == NULL || frame->next <= frame) break;
        frame = frame->next;
    }

    return depth;
}

int stacktrace_save(stacktrace_t *trace, int max_depth) {
    if (!trace || max_depth <= 0) return -22;

    memset(trace, 0, sizeof(*trace));

    uint32_t fp;
    __asm__ volatile("mov %%ebp, %0" : "=r"(fp));

    trace->depth = walk_stack(fp, trace->entries,
                              (max_depth < STACKTRACE_MAX_DEPTH) ?
                              max_depth : STACKTRACE_MAX_DEPTH,
                              1);
    return trace->depth;
}

int stacktrace_save_regs(regs_t *regs, stacktrace_t *trace, int max_depth) {
    if (!trace || !regs || max_depth <= 0) return -22;

    memset(trace, 0, sizeof(*trace));

    uint32_t fp = regs->ebp;
    trace->depth = walk_stack(fp, trace->entries,
                              (max_depth < STACKTRACE_MAX_DEPTH) ?
                              max_depth : STACKTRACE_MAX_DEPTH,
                              0);
    return trace->depth;
}

void stacktrace_print(stacktrace_t *trace) {
    if (!trace) return;

    klog_info("Stack trace (%d frames):", trace->depth);
    for (int i = 0; i < trace->depth; i++) {
        if (trace->entries[i].symbol[0]) {
            klog_info("  #%d 0x%x <%s+0x%x>",
                      i, trace->entries[i].address,
                      trace->entries[i].symbol,
                      trace->entries[i].offset);
        } else {
            klog_info("  #%d 0x%x", i, trace->entries[i].address);
        }
    }
}

void stacktrace_print_from(uint32_t fp) {
    stacktrace_t trace;
    memset(&trace, 0, sizeof(trace));
    trace.depth = walk_stack(fp, trace.entries, STACKTRACE_MAX_DEPTH, 0);
    stacktrace_print(&trace);
}

void dump_stack(void) {
    stacktrace_t trace;
    stacktrace_save(&trace, STACKTRACE_MAX_DEPTH);
    stacktrace_print(&trace);
}

int stacktrace_resolve_symbol(uint32_t addr, char *name,
                              int name_size, uint32_t *offset) {
    if (!name || name_size <= 0) return -22;

    name[0] = '\0';
    if (offset) *offset = addr;
    return -2;
}
