#ifndef ACPI_AML_H
#define ACPI_AML_H

#include "stdint.h"

/* ACPI ASL/AML parser and interpreter (minimal subset).
 *
 * ACPI Machine Language (AML) is a compact, stack-based bytecode used
 * by BIOS / firmware to describe hardware topology, power management,
 * and resource allocation. The byte encoding includes a stream of
 * opcodes, names, and values which the interpreter walks through to
 * evaluate the AML namespace.
 *
 * This implementation parses and evaluates a small but meaningful
 * subset of AML:
 *   - DefBlock / DefIf / DefWhile / DefIfElse
 *   - DefName / DefMethod / DefPackage
 *   - DefDevice / DefScope / DefReturn
 *   - String / Integer / Buffer literals
 *   - OpStore / OpAdd / OpSubtract / OpMultiply / OpDivide / OpAnd / OpOr / OpXor / OpNot / OpShiftLeft / OpShiftRight / OpMod
 *   - OpEqual / OpGreater / OpLess / OpLogicalAnd / OpLogicalOr / OpLogicalNot
 *   - OpIf / OpWhile / OpReturn / OpOne / OpZero
 *   - OpIndex / OpDerefOf / OpRefOf
 *   - OpNotify / OpPrintf (debug)
 *   - OpStore / OpLoad
 *
 * The interpreter stores objects in an in-memory namespace tree, where
 * each node is named (4-char NameSeg or NamePath string). Lookup uses
 * standard scope rules: \\foo -> root, foo -> current scope, ^foo -> parent.
 */

#define ACPI_AML_NAME_SEG_LEN  4
#define ACPI_AML_MAX_NAME_LEN  64
#define ACPI_AML_MAX_NODES     128
#define ACPI_AML_MAX_STACK     64
#define ACPI_AML_MAX_ARG       8
#define ACPI_AML_MAX_METHODS   32
#define ACPI_AML_MAX_DEVICES   32

/* AML object types. */
typedef enum {
    ACPI_OBJ_UNINITIALIZED = 0,
    ACPI_OBJ_INTEGER,
    ACPI_OBJ_STRING,
    ACPI_OBJ_BUFFER,
    ACPI_OBJ_PACKAGE,
    ACPI_OBJ_DEVICE,
    ACPI_OBJ_METHOD,
    ACPI_OBJ_NAME,
} acpi_obj_type_t;

typedef struct acpi_obj {
    acpi_obj_type_t type;
    union {
        int64_t integer;
        struct {
            char *data;
            uint32_t len;
        } string;
        struct {
            uint8_t *data;
            uint32_t len;
        } buffer;
        struct {
            struct acpi_obj **items;
            uint32_t count;
        } package;
        struct {
            char *method_name;
            uint32_t method_offset;
            uint8_t arg_count;
        } method;
    } u;
} acpi_obj_t;

/* AML namespace node. */
typedef struct acpi_ns_node {
    char name[ACPI_AML_NAME_SEG_LEN + 1];
    acpi_obj_t obj;
    struct acpi_ns_node *parent;
    struct acpi_ns_node *children;
    struct acpi_ns_node *next;
} acpi_ns_node_t;

/* Interpreter state. */
typedef struct {
    const uint8_t *bytecode;
    uint32_t       bytecode_len;
    uint32_t       pc;
    acpi_ns_node_t *root;
    acpi_ns_node_t *current_scope;
    acpi_obj_t    *stack[ACPI_AML_MAX_STACK];
    int32_t        stack_top;
    /* Method invocations - tracked for nested call limits. */
    int            call_depth;
    /* Stats. */
    uint64_t       ops_executed;
    uint64_t       methods_invoked;
    uint64_t       devices_seen;
} acpi_aml_state_t;

int  acpi_aml_init(acpi_aml_state_t *state,
                   const uint8_t *aml_data, uint32_t aml_len);

/* Run a top-level scope / method block. Returns 0 on success. */
int  acpi_aml_run(acpi_aml_state_t *state);

/* Look up a name path in the namespace and return its value object. */
acpi_obj_t *acpi_aml_lookup(acpi_aml_state_t *state, const char *path);

/* Evaluate a control method. Arguments are passed in argv. */
int acpi_aml_eval_method(acpi_aml_state_t *state, const char *path,
                         acpi_obj_t **args, uint8_t arg_count,
                         acpi_obj_t **result);

void acpi_aml_print_stats(acpi_aml_state_t *state);

#endif