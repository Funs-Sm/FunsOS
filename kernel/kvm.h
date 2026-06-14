#ifndef KVM_H
#define KVM_H

#include "stdint.h"
#include "kernel_types.h"

/* VMX constants */
#define VMX_VMENTRY_SUCCESS     0
#define VMX_FAIL_VALID          1
#define VMX_FAIL_INVALID        2

/* VMCS fields */
#define VMCS_GUEST_ES_SELECTOR    0x0800
#define VMCS_GUEST_CS_SELECTOR    0x0802
#define VMCS_GUEST_SS_SELECTOR    0x0804
#define VMCS_GUEST_DS_SELECTOR    0x0806
#define VMCS_GUEST_FS_SELECTOR    0x0808
#define VMCS_GUEST_GS_SELECTOR    0x080A
#define VMCS_GUEST_CR0            0x6800
#define VMCS_GUEST_CR3            0x6802
#define VMCS_GUEST_CR4            0x6804
#define VMCS_GUEST_RIP            0x681E
#define VMCS_GUEST_RSP            0x681C
#define VMCS_GUEST_RFLAGS         0x6820
#define VMCS_GUEST_IA32_EFER      0x2816
#define VMCS_HOST_CR0             0x6C00
#define VMCS_HOST_CR3             0x6C02
#define VMCS_HOST_CR4             0x6C04
#define VMCS_HOST_RIP             0x6C16
#define VMCS_HOST_RSP             0x6C14
#define VMCS_CTRL_PIN_BASED       0x4000
#define VMCS_CTRL_CPU_BASED       0x4002
#define VMCS_CTRL_VM_EXIT         0x400C
#define VMCS_CTRL_VM_ENTRY        0x4012
#define VMCS_CTRL_EXEC_BITMAP     0x401E

/* VM exit reasons */
#define VMX_EXIT_EXCEPTION        0
#define VMX_EXIT_EXTERNAL_INT     1
#define VMX_EXIT_TRIPLE_FAULT     2
#define VMX_EXIT_INIT             3
#define VMX_EXIT_SIPI             4
#define VMX_EXIT_IO_SMI           5
#define VMX_EXIT_SMI              6
#define VMX_EXIT_INT_WINDOW       7
#define VMX_EXIT_NMI_WINDOW       8
#define VMX_EXIT_TASK_SWITCH      9
#define VMX_EXIT_CPUID            10
#define VMX_EXIT_HLT              12
#define VMX_EXIT_INVD             13
#define VMX_EXIT_INVLPG           14
#define VMX_EXIT_RDPMC            15
#define VMX_EXIT_RDTSC            16
#define VMX_EXIT_RSM              17
#define VMX_EXIT_VMCALL           18
#define VMX_EXIT_VMCLEAR          19
#define VMX_EXIT_VMLAUNCH         20
#define VMX_EXIT_VMPTRLD          21
#define VMX_EXIT_VMPTRST          22
#define VMX_EXIT_VMREAD           23
#define VMX_EXIT_VMRESUME         24
#define VMX_EXIT_VMWRITE          25
#define VMX_EXIT_VMXOFF           26
#define VMX_EXIT_VMXON            27
#define VMX_EXIT_CR_ACCESS        28
#define VMX_EXIT_DR_ACCESS        29
#define VMX_EXIT_IO_INSTRUCTION   30
#define VMX_EXIT_MSR_READ         31
#define VMX_EXIT_MSR_WRITE        32
#define VMX_EXIT_EPT_VIOLATION    49
#define VMX_EXIT_EPT_MISCONFIG    50

/* Virtualization type */
#define KVM_NONE    0
#define KVM_VMX     1
#define KVM_SVM     2

/* VM 状态 */
#define KVM_VM_STOPPED   0
#define KVM_VM_RUNNING   1
#define KVM_VM_PAUSED    2

/* vCPU 状态 */
#define KVM_VCPU_STOPPED  0
#define KVM_VCPU_RUNNING  1
#define KVM_VCPU_HALTED   2

/* 最大 VM 和 vCPU 数量 */
#define KVM_MAX_VMS      8
#define KVM_MAX_VCPUS    4

/* ---- 增强的 VM 结构 ---- */
typedef struct {
    uint32_t vmid;              /* 虚拟机 ID */
    uint32_t state;             /* running/paused/stopped */
    uint32_t vcpu_count;        /* vCPU 数量 */
    uint64_t memory_size;       /* 内存大小 */
    page_directory_t *page_dir; /* 客户机页目录 */
    uint32_t *vcpu_states;      /* 每个vCPU的状态 */
    void *io_bitmap;            /* I/O 位图 */
    void *msr_bitmap;           /* MSR 位图 */

    /* VMX 相关字段 */
    uint32_t vmxon_phys;
    uint32_t vmcs_phys;
    void *vmxon_region;
    void *vmcs_region;
    void *vmxon_va;
    void *vmcs_va;
    uint32_t revision_id;
    uint32_t guest_mem_size;
    uint32_t *guest_mem;
    uint32_t guest_entry;
    uint32_t launched;
    uint32_t exited;
    uint32_t exit_reason;
    uint32_t exit_qualification;
    uint32_t guest_rip;
    uint32_t guest_rsp;
    uint32_t guest_rflags;
} kvm_vm_t;

/* ---- vCPU 结构 ---- */
typedef struct {
    uint32_t vcpu_id;
    uint32_t state;             /* KVM_VCPU_* */
    regs_t regs;
    uint64_t cr0, cr3, cr4;
    uint64_t efer;
    uint8_t fpu_state[512];     /* FPU/SSE 状态 */
} kvm_vcpu_t;

/* ---- CPU 能力 ---- */
typedef struct {
    uint32_t revision_id;
    uint32_t index;
    uint32_t type;
    uint32_t cr0_fixed0;
    uint32_t cr0_fixed1;
    uint32_t cr4_fixed0;
    uint32_t cr4_fixed1;
    uint32_t vmcs_size;
    uint32_t vmcs_phys_width;
    uint32_t memory_type;
    uint32_t ept_cap;
    uint32_t vpid_cap;
} kvm_cpu_caps_t;

/* ---- KVM API ---- */
int kvm_init(void);
int kvm_check_hardware(void);
int kvm_get_caps(kvm_cpu_caps_t *caps);

/* VM 管理 */
int kvm_create_vm(uint64_t memory_size, uint32_t vcpu_count);
int kvm_destroy_vm(uint32_t vmid);
int kvm_run_vm(uint32_t vmid);
int kvm_pause_vm(uint32_t vmid);
int kvm_resume_vm(uint32_t vmid);
int kvm_get_vm_state(uint32_t vmid, kvm_vm_t *state);
int kvm_set_vm_memory(uint32_t vmid, uint64_t gpa, void *data, uint32_t size);
int kvm_inject_interrupt(uint32_t vmid, uint32_t vcpu_id, uint8_t vector);

/* 旧接口兼容 */
kvm_vm_t *kvm_vm_create(uint32_t mem_size);
void kvm_vm_destroy(kvm_vm_t *vm);
int kvm_vm_run(kvm_vm_t *vm);
int kvm_vm_set_guest_state(kvm_vm_t *vm, uint32_t rip, uint32_t rsp, uint32_t cr3, uint32_t rflags);
int kvm_vm_load_code(kvm_vm_t *vm, const void *code, uint32_t size, uint32_t entry);
void kvm_vm_handle_exit(kvm_vm_t *vm);

#endif
