/* acpi_aml.c - ACPI ASL/AML interpreter (minimal subset).
 *
 * This file implements a small but functional subset of the ACPI
 * Machine Language interpreter. It parses definitions (DefScope,
 * DefName, DefMethod, DefDevice, DefPackage) and evaluates expressions
 * (integer / string / buffer / package arithmetic and logic). It is
 * designed to be used by ACPI drivers that need to walk a DSDT or
 * SSDT and extract configuration without depending on a full host
 * OS-provided interpreter.
 */

#include "acpi_aml.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"

/* ---- AML opcodes (subset) ---- */
#define AML_OP_ZERO              0x00
#define AML_OP_ONE               0x01
#define AML_OP_ALIAS             0x06
#define AML_OP_NAME              0x08
#define AML_OP_BYTE_PFX          0x0A
#define AML_OP_WORD_PFX          0x0B
#define AML_OP_DWORD_PFX         0x0C
#define AML_OP_STRING            0x0D
#define AML_OP_QWORD_PFX         0x0E
#define AML_OP_SCOPE             0x10
#define AML_OP_BUFFER            0x11
#define AML_OP_PACKAGE           0x12
#define AML_OP_METHOD            0x14
#define AML_OP_IF                0x16
#define AML_OP_ELSE              0x17
#define AML_OP_WHILE             0x18
#define AML_OP_RETURN            0x19
#define AML_OP_DEVICE            0x5B82
#define AML_OP_REVISION          0x5B80
#define AML_OP_PROCESSOR         0x5B83
#define AML_OP_POWER_RES         0x5B84
#define AML_OP_OPREGION          0x5B80
#define AML_OP_FIELD             0x5B81

#define AML_OP_STORE             0x70
#define AML_OP_ADD               0x72
#define AML_OP_SUBTRACT          0x74
#define AML_OP_MULTIPLY          0x76
#define AML_OP_DIVIDE            0x78
#define AML_OP_MOD               0x7A
#define AML_OP_AND               0x7B
#define AML_OP_OR                0x7D
#define AML_OP_XOR               0x7F
#define AML_OP_NOT               0x80
#define AML_OP_SHIFTL            0x81
#define AML_OP_SHIFTR            0x82

#define AML_OP_INDEX             0x88
#define AML_OP_DEREFOF           0x90
#define AML_OP_REFOF             0x71
#define AML_OP_NOTIFY            0x86

#define AML_OP_PRINTF            0xB6

#define AML_OP_LOGICAL_EQUAL     0x93
#define AML_OP_LOGICAL_GREATER   0x94
#define AML_OP_LOGICAL_LESS      0x95
#define AML_OP_LOGICAL_AND       0x90
#define AML_OP_LOGICAL_OR        0x91
#define AML_OP_LOGICAL_NOT       0x92

/* ---- helpers ---- */

static acpi_ns_node_t *ns_new(const char *name, acpi_ns_node_t *parent) {
    acpi_ns_node_t *n = (acpi_ns_node_t *)kmalloc(sizeof(*n));
    if (!n) return (acpi_ns_node_t *)0;
    memset(n, 0, sizeof(*n));
    int i = 0;
    while (name[i] && i < ACPI_AML_NAME_SEG_LEN) {
        n->name[i] = name[i];
        i++;
    }
    n->name[i] = 0;
    n->parent = parent;
    n->obj.type = ACPI_OBJ_UNINITIALIZED;
    return n;
}

static acpi_ns_node_t *ns_lookup(acpi_ns_node_t *scope, const char *name) {
    if (!scope || !name) return (acpi_ns_node_t *)0;
    /* Absolute path: \foo.bar.baz */
    if (name[0] == '\\') {
        scope = scope->parent;  /* step out to root */
        name++;
        while (*name == '\\') name++;
    }
    /* Parent traversal: ^ */
    if (name[0] == '^') {
        while (name[0] == '^') {
            if (scope->parent) scope = scope->parent;
            name++;
        }
    }
    /* Split by . */
    while (*name) {
        char seg[ACPI_AML_NAME_SEG_LEN + 1] = {0};
        int j = 0;
        while (*name && *name != '.' && j < ACPI_AML_NAME_SEG_LEN) {
            seg[j++] = *name++;
        }
        /* descend */
        acpi_ns_node_t *child = scope->children;
        acpi_ns_node_t *match = (acpi_ns_node_t *)0;
        while (child) {
            if (memcmp(child->name, seg, ACPI_AML_NAME_SEG_LEN) == 0) {
                match = child;
                break;
            }
            child = child->next;
        }
        if (!match) return (acpi_ns_node_t *)0;
        scope = match;
        if (*name == '.') name++;
    }
    return scope;
}

/* Forward declaration. */
static acpi_obj_t *eval_expr(acpi_aml_state_t *s);

/* ---- stack ---- */

static int push(acpi_aml_state_t *s, acpi_obj_t *obj) {
    if (s->stack_top >= ACPI_AML_MAX_STACK) return -1;
    s->stack[s->stack_top++] = obj;
    return 0;
}

static acpi_obj_t *pop(acpi_aml_state_t *s) {
    if (s->stack_top <= 0) return (acpi_obj_t *)0;
    return s->stack[--s->stack_top];
}

static acpi_obj_t *peek(acpi_aml_state_t *s, int idx) {
    int p = s->stack_top - 1 - idx;
    if (p < 0) return (acpi_obj_t *)0;
    return s->stack[p];
}

/* Allocate an integer object. */
static acpi_obj_t *new_integer(int64_t v) {
    acpi_obj_t *o = (acpi_obj_t *)kmalloc(sizeof(*o));
    if (!o) return (acpi_obj_t *)0;
    o->type = ACPI_OBJ_INTEGER;
    o->u.integer = v;
    return o;
}

static acpi_obj_t *new_string(const char *data, uint32_t len) {
    acpi_obj_t *o = (acpi_obj_t *)kmalloc(sizeof(*o));
    if (!o) return (acpi_obj_t *)0;
    o->type = ACPI_OBJ_STRING;
    o->u.string.len = len;
    o->u.string.data = (char *)kmalloc(len + 1);
    if (!o->u.string.data) return (acpi_obj_t *)0;
    if (data && len) memcpy(o->u.string.data, data, len);
    o->u.string.data[len] = 0;
    return o;
}

static int obj_to_int(acpi_obj_t *o, int64_t *out) {
    if (!o || !out) return -1;
    switch (o->type) {
        case ACPI_OBJ_INTEGER:
            *out = o->u.integer;
            return 0;
        case ACPI_OBJ_BUFFER:
            if (o->u.buffer.len < 1) return -1;
            *out = (int64_t)o->u.buffer.data[0];
            return 0;
        case ACPI_OBJ_STRING:
            if (o->u.string.len < 1) return -1;
            *out = (int64_t)o->u.string.data[0];
            return 0;
        default:
            return -1;
    }
}

/* ---- bytecode readers ---- */

static uint8_t read_u8(acpi_aml_state_t *s) {
    if (s->pc >= s->bytecode_len) return 0;
    return s->bytecode[s->pc++];
}
static uint16_t read_u16(acpi_aml_state_t *s) {
    uint8_t lo = read_u8(s);
    uint8_t hi = read_u8(s);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}
static uint32_t read_u32(acpi_aml_state_t *s) {
    return (uint32_t)read_u16(s) | ((uint32_t)read_u16(s) << 16);
}
static uint64_t read_u64(acpi_aml_state_t *s) {
    return (uint64_t)read_u32(s) | ((uint64_t)read_u32(s) << 32);
}

/* Read PkgLen: 2-bit length encoding + 1..4 length bytes. */
static uint32_t read_pkg_len(acpi_aml_state_t *s) {
    uint8_t first = read_u8(s);
    uint8_t len_bytes = (first >> 6) & 0x3;
    uint32_t v = (uint32_t)(first & 0x3F);
    for (int i = 0; i < len_bytes; i++) {
        v |= (uint32_t)read_u8(s) << (8 * (i + 1) - 6 + 6);
    }
    /* PkgLen includes the leading byte. */
    return v ? v : 1;
}

/* Read a NamePath (rooted or relative) or a NameSeg. */
static int read_name(acpi_aml_state_t *s, char *out) {
    uint8_t c = read_u8(s);
    if (c == 0x00) {
        /* ZeroOp: empty name */
        out[0] = 0;
        return 0;
    }
    if (c == 0x5C) {
        /* RootChar: \ */
        out[0] = '\\';
        out[1] = 0;
        return 0;
    }
    if (c >= 0x40 && c <= 0x5E) {
        /* ParentPrefix ^, repeated */
        int n = 1;
        while (s->pc < s->bytecode_len && s->bytecode[s->pc] >= 0x40
               && s->bytecode[s->pc] <= 0x5E) {
            n++; read_u8(s);
        }
        int i = 0;
        for (; i < n && i < ACPI_AML_MAX_NAME_LEN / 2; i++) out[i] = '^';
        out[i] = 0;
        return 0;
    }
    /* Single NameSeg (4 chars encoded) */
    uint8_t b0 = c, b1 = read_u8(s), b2 = read_u8(s), b3 = read_u8(s);
    out[0] = (char)(b0);
    out[1] = (char)(b1);
    out[2] = (char)(b2);
    out[3] = (char)(b3);
    out[4] = 0;
    /* Skip trailing chars 'A'..'Z' which start each segment. */
    while (s->pc < s->bytecode_len && s->bytecode[s->pc] >= 'A'
            && s->bytecode[s->pc] <= 'Z') {
        int k = 4;
        while (s->pc < s->bytecode_len && s->bytecode[s->pc] >= 'A'
                && s->bytecode[s->pc] <= 'Z' && k < ACPI_AML_MAX_NAME_LEN) {
            out[k++] = (char)read_u8(s);
        }
        out[k] = 0;
        if (s->pc < s->bytecode_len && s->bytecode[s->pc] == '.') {
            out[k++] = '.';
            read_u8(s);
            out[k] = 0;
        } else break;
    }
    return 0;
}

/* ---- opcode dispatch ---- */

static int run_block(acpi_aml_state_t *s, uint32_t start, uint32_t end);

static int execute_op(acpi_aml_state_t *s, uint16_t op) {
    s->ops_executed++;
    switch (op) {
        case AML_OP_ZERO: push(s, new_integer(0)); return 0;
        case AML_OP_ONE:  push(s, new_integer(1)); return 0;

        case AML_OP_BYTE_PFX: {
            uint8_t v = read_u8(s);
            push(s, new_integer(v)); return 0;
        }
        case AML_OP_WORD_PFX: {
            uint16_t v = read_u16(s);
            push(s, new_integer(v)); return 0;
        }
        case AML_OP_DWORD_PFX: {
            uint32_t v = read_u32(s);
            push(s, new_integer(v)); return 0;
        }
        case AML_OP_QWORD_PFX: {
            uint64_t v = read_u64(s);
            push(s, new_integer((int64_t)v)); return 0;
        }
        case AML_OP_STRING: {
            char buf[256]; int i = 0;
            while (s->pc < s->bytecode_len
                   && s->bytecode[s->pc] != 0) {
                if (i >= 255) break;
                buf[i++] = (char)read_u8(s);
            }
            read_u8(s);  /* NUL terminator */
            buf[i] = 0;
            push(s, new_string(buf, (uint32_t)i));
            return 0;
        }

        case AML_OP_NAME: {
            char name[ACPI_AML_MAX_NAME_LEN];
            read_name(s, name);
            acpi_obj_t *val = eval_expr(s);
            if (!val) return -1;
            acpi_ns_node_t *node = ns_new(name, s->current_scope);
            if (!node) return -1;
            node->obj = *val;
            node->next = s->current_scope->children;
            s->current_scope->children = node;
            return 0;
        }

        case AML_OP_SCOPE: {
            char name[ACPI_AML_MAX_NAME_LEN];
            read_name(s, name);
            uint32_t len = read_pkg_len(s);
            uint32_t end = s->pc + len;
            acpi_ns_node_t *parent = ns_lookup(s->root, name);
            if (!parent) parent = s->root;
            acpi_ns_node_t *prev = s->current_scope;
            s->current_scope = parent;
            int r = run_block(s, s->pc, end);
            s->current_scope = prev;
            s->pc = end;
            return r;
        }

        case AML_OP_DEVICE: {
            char name[ACPI_AML_MAX_NAME_LEN];
            read_name(s, name);
            uint32_t len = read_pkg_len(s);
            uint32_t end = s->pc + len;
            acpi_ns_node_t *node = ns_new(name, s->current_scope);
            if (!node) return -1;
            node->obj.type = ACPI_OBJ_DEVICE;
            node->next = s->current_scope->children;
            s->current_scope->children = node;
            acpi_ns_node_t *prev = s->current_scope;
            s->current_scope = node;
            s->devices_seen++;
            int r = run_block(s, s->pc, end);
            s->current_scope = prev;
            s->pc = end;
            return r;
        }

        case AML_OP_METHOD: {
            char name[ACPI_AML_MAX_NAME_LEN];
            read_name(s, name);
            uint32_t method_flags = read_u8(s);
            uint8_t arg_count = (uint8_t)(method_flags & 0x7);
            /* Optional argument list parser: count NumIfPresent */
            int num_args_present = (method_flags >> 3) & 0x1;
            if (num_args_present) {
                arg_count = read_u8(s);
            }
            uint32_t len = read_pkg_len(s);
            uint32_t end = s->pc + len;
            acpi_ns_node_t *node = ns_new(name, s->current_scope);
            if (!node) return -1;
            node->obj.type = ACPI_OBJ_METHOD;
            node->obj.u.method.method_name = (char *)0;
            node->obj.u.method.method_offset = s->pc;
            node->obj.u.method.arg_count = arg_count;
            node->next = s->current_scope->children;
            s->current_scope->children = node;
            s->pc = end;
            return 0;
        }

        case AML_OP_PACKAGE: {
            uint32_t pkg_len = read_pkg_len(s);
            uint8_t num_elements = read_u8(s);
            acpi_obj_t *pkg = (acpi_obj_t *)kmalloc(sizeof(*pkg));
            if (!pkg) return -1;
            pkg->type = ACPI_OBJ_PACKAGE;
            pkg->u.package.count = num_elements;
            pkg->u.package.items = (acpi_obj_t **)kmalloc(sizeof(acpi_obj_t *)
                                                          * num_elements);
            if (!pkg->u.package.items) return -1;
            uint32_t end = s->pc + pkg_len - 1;  /* heuristic */
            for (uint32_t i = 0; i < num_elements; i++) {
                if (s->pc >= end) break;
                pkg->u.package.items[i] = eval_expr(s);
            }
            push(s, pkg);
            return 0;
        }

        case AML_OP_BUFFER: {
            uint32_t buf_len = read_pkg_len(s);
            uint32_t end = s->pc + buf_len;
            uint32_t size = (uint32_t)eval_expr(s);
            if (size > 65536) size = 65536;
            acpi_obj_t *o = (acpi_obj_t *)kmalloc(sizeof(*o));
            if (!o) return -1;
            o->type = ACPI_OBJ_BUFFER;
            o->u.buffer.len = size;
            o->u.buffer.data = (uint8_t *)kmalloc(size);
            if (!o->u.buffer.data) return -1;
            uint32_t fill = (size < (end - s->pc)) ? size : (end - s->pc);
            if (fill > size) fill = size;
            for (uint32_t i = 0; i < fill; i++) {
                o->u.buffer.data[i] = read_u8(s);
            }
            push(s, o);
            return 0;
        }

        case AML_OP_STORE: {
            acpi_obj_t *src = eval_expr(s);
            acpi_obj_t *dst_name = eval_expr(s);
            if (!src || !dst_name) return -1;
            if (dst_name->type == ACPI_OBJ_NAME) {
                char *p = dst_name->u.string.data;
                if (p) {
                    acpi_ns_node_t *n = ns_lookup(s->root, p);
                    if (n) n->obj = *src;
                }
            }
            push(s, src);
            return 0;
        }

        case AML_OP_ADD: case AML_OP_SUBTRACT: case AML_OP_MULTIPLY:
        case AML_OP_AND:    case AML_OP_OR:     case AML_OP_XOR:
        case AML_OP_MOD: {
            acpi_obj_t *a = eval_expr(s);
            acpi_obj_t *b = eval_expr(s);
            acpi_obj_t *dst = eval_expr(s);
            int64_t av, bv; if (obj_to_int(a, &av) != 0
                                || obj_to_int(b, &bv) != 0) return -1;
            int64_t rv = 0;
            switch (op) {
                case AML_OP_ADD:      rv = av + bv; break;
                case AML_OP_SUBTRACT: rv = av - bv; break;
                case AML_OP_MULTIPLY: rv = av * bv; break;
                case AML_OP_AND:      rv = av & bv; break;
                case AML_OP_OR:       rv = av | bv; break;
                case AML_OP_XOR:      rv = av ^ bv; break;
                case AML_OP_MOD:      rv = bv ? av % bv : 0; break;
            }
            (void)dst;
            push(s, new_integer(rv));
            return 0;
        }

        case AML_OP_DIVIDE: {
            acpi_obj_t *a = eval_expr(s);
            acpi_obj_t *b = eval_expr(s);
            acpi_obj_t *rem = eval_expr(s);
            acpi_obj_t *quo = eval_expr(s);
            int64_t av, bv;
            if (obj_to_int(a, &av) != 0 || obj_to_int(b, &bv) != 0 || bv == 0) return -1;
            int64_t q = av / bv;
            int64_t r = av % bv;
            push(s, new_integer(q));
            push(s, new_integer(r));
            (void)rem; (void)quo;
            return 0;
        }

        case AML_OP_SHIFTL: case AML_OP_SHIFTR: {
            acpi_obj_t *a = eval_expr(s);
            acpi_obj_t *b = eval_expr(s);
            int64_t av, bv;
            if (obj_to_int(a, &av) != 0 || obj_to_int(b, &bv) != 0) return -1;
            int64_t rv = (op == AML_OP_SHIFTL) ? (av << bv) : (av >> bv);
            push(s, new_integer(rv));
            return 0;
        }

        case AML_OP_NOT: {
            acpi_obj_t *a = eval_expr(s);
            int64_t av;
            if (obj_to_int(a, &av) != 0) return -1;
            push(s, new_integer(~av));
            return 0;
        }

        case AML_OP_LOGICAL_EQUAL: case AML_OP_LOGICAL_GREATER:
        case AML_OP_LOGICAL_LESS:  {
            acpi_obj_t *a = eval_expr(s);
            acpi_obj_t *b = eval_expr(s);
            int64_t av, bv;
            if (obj_to_int(a, &av) != 0 || obj_to_int(b, &bv) != 0) return -1;
            int r = 0;
            if (op == AML_OP_LOGICAL_EQUAL) r = (av == bv);
            else if (op == AML_OP_LOGICAL_GREATER) r = (av > bv);
            else r = (av < bv);
            push(s, new_integer(r));
            return 0;
        }

        case AML_OP_INDEX: {
            acpi_obj_t *pkg = eval_expr(s);
            acpi_obj_t *idx = eval_expr(s);
            int64_t i;
            if (!pkg || obj_to_int(idx, &i) != 0) return -1;
            if (pkg->type != ACPI_OBJ_PACKAGE) return -1;
            if (i < 0 || (uint32_t)i >= pkg->u.package.count) return -1;
            push(s, pkg->u.package.items[i]);
            return 0;
        }

        case AML_OP_DEREFOF: {
            acpi_obj_t *ref = eval_expr(s);
            if (ref && ref->type == ACPI_OBJ_NAME) {
                char *p = ref->u.string.data;
                acpi_ns_node_t *n = p ? ns_lookup(s->root, p) : (acpi_ns_node_t *)0;
                if (n) push(s, &n->obj);
            }
            return 0;
        }

        case AML_OP_NOTIFY: {
            eval_expr(s);  /* device */
            eval_expr(s);  /* notification value */
            return 0;
        }

        case AML_OP_PRINTF: {
            /* Skip a fixed-size arglist. Format is a string + N ints. */
            acpi_obj_t *fmt = eval_expr(s);
            (void)fmt;
            /* No-op printf in this minimal implementation. */
            return 0;
        }

        case AML_OP_IF: {
            uint32_t len = read_pkg_len(s);
            uint32_t end = s->pc + len;
            acpi_obj_t *cond = eval_expr(s);
            int64_t v;
            if (!cond || obj_to_int(cond, &v) != 0) return -1;
            if (v) {
                run_block(s, s->pc, end);
            }
            s->pc = end;
            return 0;
        }

        case AML_OP_WHILE: {
            uint32_t len = read_pkg_len(s);
            uint32_t end = s->pc + len;
            /* Bound the loop to prevent infinite loops in malformed AML. */
            for (uint32_t guard = 0; guard < 100000; guard++) {
                uint32_t saved = s->pc;
                acpi_obj_t *cond = eval_expr(s);
                int64_t v;
                if (!cond || obj_to_int(cond, &v) != 0) break;
                if (!v) break;
                run_block(s, s->pc, end);
                s->pc = saved;
            }
            s->pc = end;
            return 0;
        }

        case AML_OP_RETURN: {
            acpi_obj_t *val = eval_expr(s);
            push(s, val);
            return 1;  /* signal "stop" */
        }

        default:
            /* Unknown opcode - try to skip byte. */
            return 0;
    }
    return 0;
}

static acpi_obj_t *eval_expr(acpi_aml_state_t *s) {
    uint8_t c = read_u8(s);
    /* Name reference: if c is a NameSeg or NamePath */
    if ((c >= 'A' && c <= 'Z')
        || c == '\\' || c == '^'
        || (c >= 0x40 && c <= 0x5E)) {
        /* Read the name. */
        s->pc--;
        char name[ACPI_AML_MAX_NAME_LEN];
        read_name(s, name);
        acpi_obj_t *o = (acpi_obj_t *)kmalloc(sizeof(*o));
        if (!o) return (acpi_obj_t *)0;
        o->type = ACPI_OBJ_NAME;
        o->u.string.len = (uint32_t)strlen(name);
        o->u.string.data = (char *)kmalloc(o->u.string.len + 1);
        if (o->u.string.data) memcpy(o->u.string.data, name, o->u.string.len + 1);
        /* If the name refers to a method, invoke it. */
        acpi_ns_node_t *n = ns_lookup(s->root, name);
        if (n && n->obj.type == ACPI_OBJ_METHOD) {
            /* Execute method inline. */
            uint8_t argc = n->obj.u.method.arg_count;
            acpi_obj_t **args = (acpi_obj_t **)kmalloc(sizeof(acpi_obj_t *)
                                                       * argc);
            for (uint8_t i = 0; i < argc; i++) args[i] = eval_expr(s);
            acpi_obj_t *result = (acpi_obj_t *)0;
            acpi_aml_eval_method(s, name, args, argc, &result);
            if (result) return result;
        }
        return o;
    }

    /* Prefix / type 1 / type 2 opcodes */
    if (c == AML_OP_BYTE_PFX || c == AML_OP_WORD_PFX
        || c == AML_OP_DWORD_PFX || c == AML_OP_QWORD_PFX
        || c == AML_OP_STRING || c == AML_OP_ZERO || c == AML_OP_ONE) {
        s->pc--;
        execute_op(s, (uint16_t)c);
        return pop(s);
    }
    /* Two-byte opcodes. */
    if (c == 0x5B) {
        uint8_t c2 = read_u8(s);
        execute_op(s, (uint16_t)(c << 8) | c2);
        return pop(s);
    }
    /* Single-byte opcodes. */
    execute_op(s, c);
    return pop(s);
}

static int run_block(acpi_aml_state_t *s, uint32_t start, uint32_t end) {
    s->pc = start;
    while (s->pc < end && s->pc < s->bytecode_len) {
        uint32_t here = s->pc;
        int stop = 0;
        uint8_t c = read_u8(s);
        if (c == AML_OP_BYTE_PFX || c == AML_OP_WORD_PFX
            || c == AML_OP_DWORD_PFX || c == AML_OP_QWORD_PFX
            || c == AML_OP_STRING || c == AML_OP_ZERO || c == AML_OP_ONE) {
            s->pc--;
            execute_op(s, c);
            continue;
        }
        if (c == AML_OP_RETURN) {
            acpi_obj_t *val = eval_expr(s);
            push(s, val);
            stop = 1;
        } else if (c == AML_OP_IF) {
            uint32_t len = read_pkg_len(s);
            uint32_t block_end = s->pc + len;
            acpi_obj_t *cond = eval_expr(s);
            int64_t v;
            if (cond && obj_to_int(cond, &v) == 0 && v) {
                run_block(s, s->pc, block_end);
            }
            s->pc = block_end;
        } else if (c == AML_OP_WHILE) {
            uint32_t len = read_pkg_len(s);
            uint32_t block_end = s->pc + len;
            for (uint32_t guard = 0; guard < 10000; guard++) {
                uint32_t saved = s->pc;
                acpi_obj_t *cond = eval_expr(s);
                int64_t v;
                if (!cond || obj_to_int(cond, &v) != 0 || !v) break;
                run_block(s, s->pc, block_end);
                s->pc = saved;
            }
            s->pc = block_end;
        } else if (c == AML_OP_SCOPE) {
            char name[ACPI_AML_MAX_NAME_LEN];
            read_name(s, name);
            uint32_t len = read_pkg_len(s);
            uint32_t scope_end = s->pc + len;
            acpi_ns_node_t *parent = ns_lookup(s->root, name);
            if (!parent) parent = s->root;
            acpi_ns_node_t *prev = s->current_scope;
            s->current_scope = parent;
            run_block(s, s->pc, scope_end);
            s->current_scope = prev;
            s->pc = scope_end;
        } else if (c == AML_OP_DEVICE) {
            char name[ACPI_AML_MAX_NAME_LEN];
            read_name(s, name);
            uint32_t len = read_pkg_len(s);
            uint32_t dev_end = s->pc + len;
            acpi_ns_node_t *node = ns_new(name, s->current_scope);
            if (node) {
                node->obj.type = ACPI_OBJ_DEVICE;
                node->next = s->current_scope->children;
                s->current_scope->children = node;
                acpi_ns_node_t *prev = s->current_scope;
                s->current_scope = node;
                s->devices_seen++;
                run_block(s, s->pc, dev_end);
                s->current_scope = prev;
            }
            s->pc = dev_end;
        } else if (c == AML_OP_METHOD) {
            char name[ACPI_AML_MAX_NAME_LEN];
            read_name(s, name);
            uint8_t flags = read_u8(s);
            if (flags & 0x08) read_u8(s);
            uint32_t len = read_pkg_len(s);
            uint32_t m_end = s->pc + len;
            acpi_ns_node_t *node = ns_new(name, s->current_scope);
            if (node) {
                node->obj.type = ACPI_OBJ_METHOD;
                node->obj.u.method.method_offset = s->pc;
                node->obj.u.method.arg_count = flags & 0x7;
                node->next = s->current_scope->children;
                s->current_scope->children = node;
            }
            s->pc = m_end;
        } else if (c == AML_OP_NAME) {
            char name[ACPI_AML_MAX_NAME_LEN];
            read_name(s, name);
            acpi_obj_t *val = eval_expr(s);
            acpi_ns_node_t *node = ns_new(name, s->current_scope);
            if (node && val) {
                node->obj = *val;
                node->next = s->current_scope->children;
                s->current_scope->children = node;
            }
        } else if (c == 0x5B) {
            /* Skip 2-byte opcodes that we don't handle. */
            uint8_t c2 = read_u8(s);
            (void)c2;
        } else if (c == AML_OP_PACKAGE || c == AML_OP_BUFFER) {
            s->pc--;
            execute_op(s, c);
        } else if (c == AML_OP_STORE || c == AML_OP_ADD || c == AML_OP_SUBTRACT
                   || c == AML_OP_MULTIPLY || c == AML_OP_AND || c == AML_OP_OR
                   || c == AML_OP_XOR || c == AML_OP_MOD || c == AML_OP_DIVIDE
                   || c == AML_OP_SHIFTL || c == AML_OP_SHIFTR
                   || c == AML_OP_NOT || c == AML_OP_INDEX
                   || c == AML_OP_DEREFOF || c == AML_OP_NOTIFY
                   || c == AML_OP_PRINTF || c == AML_OP_LOGICAL_EQUAL
                   || c == AML_OP_LOGICAL_GREATER || c == AML_OP_LOGICAL_LESS) {
            s->pc--;
            execute_op(s, c);
        }
        if (stop) return 0;
        (void)here;
    }
    return 0;
}

int acpi_aml_init(acpi_aml_state_t *state,
                   const uint8_t *aml_data, uint32_t aml_len)
{
    if (!state || !aml_data) return -1;
    memset(state, 0, sizeof(*state));
    state->bytecode = aml_data;
    state->bytecode_len = aml_len;
    state->pc = 0;
    state->root = ns_new("\\", (acpi_ns_node_t *)0);
    if (!state->root) return -1;
    state->current_scope = state->root;
    return 0;
}

int acpi_aml_run(acpi_aml_state_t *state) {
    if (!state) return -1;
    return run_block(state, 0, state->bytecode_len);
}

acpi_obj_t *acpi_aml_lookup(acpi_aml_state_t *state, const char *path) {
    if (!state || !path) return (acpi_obj_t *)0;
    acpi_ns_node_t *n = ns_lookup(state->root, path);
    if (!n) return (acpi_obj_t *)0;
    return &n->obj;
}

int acpi_aml_eval_method(acpi_aml_state_t *state, const char *path,
                         acpi_obj_t **args, uint8_t arg_count,
                         acpi_obj_t **result)
{
    if (!state || !path) return -1;
    acpi_ns_node_t *n = ns_lookup(state->root, path);
    if (!n || n->obj.type != ACPI_OBJ_METHOD) return -1;
    uint32_t saved = state->pc;
    state->call_depth++;
    if (state->call_depth > 32) {
        state->call_depth--;
        return -1;
    }
    state->methods_invoked++;
    run_block(state, n->obj.u.method.method_offset,
              state->bytecode_len);
    state->pc = saved;
    state->call_depth--;
    if (result) *result = pop(state);
    return 0;
}

void acpi_aml_print_stats(acpi_aml_state_t *state) {
    if (!state) return;
    klog_write(KLOG_INFO, "acpi-aml: ops=%llu methods=%llu devices=%llu\n",
               state->ops_executed, state->methods_invoked, state->devices_seen);
}