#include "kvm.h"
#include "kheap.h"
#include "pmm.h"
#include "vmm.h"
#include "stdio.h"
#include "string.h"
#include "io.h"
#include "sync.h"

/* Forward declaration for VM exit handler called from inline asm */
void kvm_vm_exit_c_handler(uint32_t qualification, uint32_t reason,
                            uint32_t rflags, uint32_t rsp, uint32_t rip);

/* MSR constants */
#define IA32_VMX_BASIC           0x0480
#define IA32_VMX_CR0_FIXED0      0x0486
#define IA32_VMX_CR0_FIXED1      0x0487
#define IA32_VMX_CR4_FIXED0      0x0488
#define IA32_VMX_CR4_FIXED1      0x0489
#define IA32_VMX_ENTRY_CTLS      0x0484
#define IA32_VMX_EXIT_CTLS       0x0483
#define IA32_VMX_PINBASED_CTLS   0x0481
#define IA32_VMX_PROCBASED_CTLS  0x0482
#define IA32_FEATURE_CONTROL     0x03A
#define IA32_EFER                0xC0000080

/* CR0/CR4 bits */
#define CR0_PE  0x00000001
#define CR0_PG  0x80000000
#define CR4_VMXE 0x00002000

/* VMX capability flags */
#define PIN_BASED_EXT_INTR       0x00000001
#define PIN_BASED_NMI            0x00000002
#define PIN_BASED_VIRTUAL_NMI    0x00000004

#define CPU_BASED_HLT_EXIT       0x00000080
#define CPU_BASED_IO_EXIT        0x01000000
#define CPU_BASED_MSR_EXIT       0x00000800
#define CPU_BASED_CR_EXIT        0x00000010

#define VM_EXIT_HOST_32          0x00002000
#define VM_ENTRY_GUEST_32        0x00002000

/* Guest RFLAGS bits */
#define RFLAGS_IF    0x00000200
#define RFLAGS_FIXED 0x00000002

/* ---- 全局状态 ---- */
static int kvm_vmx_enabled = 0;
static int kvm_svm_enabled = 0;
static int kvm_initialized = 0;
static kvm_cpu_caps_t kvm_caps;

/* 多 VM 管理 */
static kvm_vm_t *kvm_vms[KVM_MAX_VMS];
static uint32_t kvm_vm_count = 0;
static uint32_t kvm_next_vmid = 1;
static mutex_t kvm_lock;

/* 当前正在运行的 VM (用于 exit handler) */
static kvm_vm_t *kvm_current_vm = NULL;

/* ---- MSR read/write via inline asm ---- */

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

/* ---- CPUID ---- */

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    asm volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf));
}

/* ---- CR register access ---- */

static inline uint32_t read_cr0(void) {
    uint32_t val;
    asm volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

static inline void write_cr0(uint32_t val) {
    asm volatile("mov %0, %%cr0" : : "r"(val) : "memory");
}

static inline uint32_t read_cr4(void) {
    uint32_t val;
    asm volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

static inline void write_cr4(uint32_t val) {
    asm volatile("mov %0, %%cr4" : : "r"(val) : "memory");
}

/* ---- VMX instructions ---- */

static inline int vmxon(uint32_t phys) {
    uint8_t ret;
    asm volatile("vmxon %1; setc %0" : "=q"(ret) : "m"(phys) : "cc", "memory");
    return ret;
}

static inline int vmxoff(void) {
    asm volatile("vmxoff" ::: "cc");
    return 0;
}

static inline int vmptrld(uint32_t phys) {
    uint8_t ret;
    asm volatile("vmptrld %1; setc %0" : "=q"(ret) : "m"(phys) : "cc", "memory");
    return ret;
}

static inline int vmclear(uint32_t phys) {
    uint8_t ret;
    asm volatile("vmclear %1; setc %0" : "=q"(ret) : "m"(phys) : "cc", "memory");
    return ret;
}

static inline int vmwrite(uint32_t field, uint32_t value) {
    uint8_t ret;
    asm volatile("vmwrite %1, %2; setc %0" : "=q"(ret) : "r"(value), "r"(field) : "cc", "memory");
    return ret;
}

static inline uint32_t vmread(uint32_t field) {
    uint32_t value;
    asm volatile("vmread %1, %0" : "=r"(value) : "r"(field) : "cc", "memory");
    return value;
}

/* ---- Physical address helper (simple identity mapping assumption) ---- */

static uint32_t virt_to_phys(void *va) {
    /* In our kernel, virtual = physical for low memory (identity mapped) */
    return (uint32_t)va;
}

/* ---- VM 查找 ---- */
static kvm_vm_t *kvm_find_vm(uint32_t vmid) {
    uint32_t i;
    for (i = 0; i < kvm_vm_count; i++) {
        if (kvm_vms[i] && kvm_vms[i]->vmid == vmid) {
            return kvm_vms[i];
        }
    }
    return NULL;
}

/* ---- KVM implementation ---- */

int kvm_check_hardware(void) {
    uint32_t eax, ebx, ecx, edx;

    /* Check VMX: CPUID leaf 1, ECX bit 5 */
    cpuid(1, &eax, &ebx, &ecx, &edx);
    if (ecx & (1 << 5)) {
        kvm_vmx_enabled = 1;
    }

    /* Check SVM: CPUID leaf 0x80000001, ECX bit 2 */
    cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
    if (ecx & (1 << 2)) {
        kvm_svm_enabled = 1;
    }

    if (kvm_vmx_enabled) return KVM_VMX;
    if (kvm_svm_enabled) return KVM_SVM;
    return KVM_NONE;
}

int kvm_get_caps(kvm_cpu_caps_t *caps) {
    if (!caps) return -1;

    if (!kvm_vmx_enabled) {
        memset(caps, 0, sizeof(kvm_cpu_caps_t));
        return -1;
    }

    uint64_t basic = rdmsr(IA32_VMX_BASIC);
    caps->revision_id = (uint32_t)(basic & 0x7FFFFFFF);
    caps->vmcs_size = (uint32_t)((basic >> 32) & 0x1FFF);
    caps->vmcs_phys_width = (uint32_t)((basic >> 48) & 0x1);
    caps->memory_type = (uint32_t)((basic >> 50) & 0xF);
    caps->type = (uint32_t)((basic >> 56) & 0x1F);
    caps->index = 0;

    caps->cr0_fixed0 = (uint32_t)rdmsr(IA32_VMX_CR0_FIXED0);
    caps->cr0_fixed1 = (uint32_t)rdmsr(IA32_VMX_CR0_FIXED1);
    caps->cr4_fixed0 = (uint32_t)rdmsr(IA32_VMX_CR4_FIXED0);
    caps->cr4_fixed1 = (uint32_t)rdmsr(IA32_VMX_CR4_FIXED1);

    /* EPT/VPID caps - read if available */
    uint64_t proc_ctls = rdmsr(IA32_VMX_PROCBASED_CTLS);
    if (proc_ctls & (1ULL << 33)) {
        caps->ept_cap = 1;
        caps->vpid_cap = 1;
    } else {
        caps->ept_cap = 0;
        caps->vpid_cap = 0;
    }

    /* Store globally */
    memcpy(&kvm_caps, caps, sizeof(kvm_cpu_caps_t));
    return 0;
}

int kvm_init(void) {
    int type = kvm_check_hardware();

    mutex_init(&kvm_lock);
    memset(kvm_vms, 0, sizeof(kvm_vms));
    kvm_vm_count = 0;
    kvm_next_vmid = 1;

    if (type == KVM_NONE) {
        printf("KVM: No hardware virtualization support detected\n");
        return -1;
    }

    if (type == KVM_VMX) {
        printf("KVM: VMX support detected\n");

        /* Check IA32_FEATURE_CONTROL lock bit */
        uint64_t fc = rdmsr(IA32_FEATURE_CONTROL);
        if ((fc & 0x1) && !(fc & (1ULL << 2))) {
            printf("KVM: VMX locked in BIOS, not enabled in feature control\n");
            return -1;
        }

        /* Read capabilities */
        kvm_cpu_caps_t caps;
        if (kvm_get_caps(&caps) != 0) {
            printf("KVM: Failed to read VMX capabilities\n");
            return -1;
        }

        printf("KVM: VMCS revision=%u, size=%u, memtype=%u\n",
               caps.revision_id, caps.vmcs_size, caps.memory_type);

        kvm_initialized = 1;
        return 0;
    }

    if (type == KVM_SVM) {
        printf("KVM: SVM support detected (not yet implemented)\n");
        return -1;
    }

    return -1;
}

/* ---- 新 API: 创建虚拟机 ---- */
int kvm_create_vm(uint64_t memory_size, uint32_t vcpu_count) {
    if (!kvm_initialized) return -1;
    if (kvm_vm_count >= KVM_MAX_VMS) return -1;
    if (vcpu_count == 0 || vcpu_count > KVM_MAX_VCPUS) return -1;

    mutex_lock(&kvm_lock);

    /* 分配 VM 结构 */
    kvm_vm_t *vm = (kvm_vm_t *)kmalloc(sizeof(kvm_vm_t));
    if (!vm) {
        mutex_unlock(&kvm_lock);
        return -1;
    }
    memset(vm, 0, sizeof(kvm_vm_t));

    vm->vmid = kvm_next_vmid++;
    vm->state = KVM_VM_STOPPED;
    vm->vcpu_count = vcpu_count;
    vm->memory_size = memory_size;

    /* 分配 vCPU 状态数组 */
    vm->vcpu_states = (uint32_t *)kmalloc(vcpu_count * sizeof(uint32_t));
    if (!vm->vcpu_states) {
        kfree(vm);
        mutex_unlock(&kvm_lock);
        return -1;
    }
    memset(vm->vcpu_states, 0, vcpu_count * sizeof(uint32_t));
    uint32_t i;
    for (i = 0; i < vcpu_count; i++) {
        vm->vcpu_states[i] = KVM_VCPU_STOPPED;
    }

    /* 分配 I/O 位图 (4KB) */
    vm->io_bitmap = (void *)kmalloc(4096);
    if (vm->io_bitmap) {
        memset(vm->io_bitmap, 0xFF, 4096);  /* 默认拦截所有 I/O */
    }

    /* 分配 MSR 位图 (4KB) */
    vm->msr_bitmap = (void *)kmalloc(4096);
    if (vm->msr_bitmap) {
        memset(vm->msr_bitmap, 0x00, 4096);  /* 默认不拦截 MSR */
    }

    /* 分配 VMXON 区域 (4KB aligned) */
    vm->vmxon_region = (void *)kmalloc(4096);
    if (!vm->vmxon_region) {
        printf("KVM: failed to allocate VMXON region\n");
        if (vm->io_bitmap) kfree(vm->io_bitmap);
        if (vm->msr_bitmap) kfree(vm->msr_bitmap);
        kfree(vm->vcpu_states);
        kfree(vm);
        mutex_unlock(&kvm_lock);
        return -1;
    }
    memset(vm->vmxon_region, 0, 4096);
    vm->vmxon_va = vm->vmxon_region;
    vm->vmxon_phys = virt_to_phys(vm->vmxon_region);

    /* 写入 VMCS revision ID */
    uint32_t *vmxon_header = (uint32_t *)vm->vmxon_region;
    *vmxon_header = kvm_caps.revision_id;

    /* 分配 VMCS 区域 (4KB aligned) */
    vm->vmcs_region = (void *)kmalloc(4096);
    if (!vm->vmcs_region) {
        printf("KVM: failed to allocate VMCS region\n");
        kfree(vm->vmxon_region);
        if (vm->io_bitmap) kfree(vm->io_bitmap);
        if (vm->msr_bitmap) kfree(vm->msr_bitmap);
        kfree(vm->vcpu_states);
        kfree(vm);
        mutex_unlock(&kvm_lock);
        return -1;
    }
    memset(vm->vmcs_region, 0, 4096);
    vm->vmcs_va = vm->vmcs_region;
    vm->vmcs_phys = virt_to_phys(vm->vmcs_region);

    /* 写入 VMCS revision ID */
    uint32_t *vmcs_header = (uint32_t *)vm->vmcs_region;
    *vmcs_header = kvm_caps.revision_id;

    /* 分配客户机内存 */
    vm->guest_mem_size = (uint32_t)memory_size;
    vm->guest_mem = (uint32_t *)kmalloc((uint32_t)memory_size);
    if (!vm->guest_mem) {
        printf("KVM: failed to allocate guest memory\n");
        kfree(vm->vmcs_region);
        kfree(vm->vmxon_region);
        if (vm->io_bitmap) kfree(vm->io_bitmap);
        if (vm->msr_bitmap) kfree(vm->msr_bitmap);
        kfree(vm->vcpu_states);
        kfree(vm);
        mutex_unlock(&kvm_lock);
        return -1;
    }
    memset(vm->guest_mem, 0, (uint32_t)memory_size);

    /* 分配客户机页目录 */
    vm->page_dir = (page_directory_t *)kmalloc(sizeof(page_directory_t));
    if (vm->page_dir) {
        memset(vm->page_dir, 0, sizeof(page_directory_t));
    }

    vm->launched = 0;
    vm->exited = 0;
    vm->guest_entry = 0;

    /* 启用 VMX 操作 */
    uint32_t host_cr4 = read_cr4();
    write_cr4(host_cr4 | CR4_VMXE);

    /* 执行 VMXON */
    if (vmxon(vm->vmxon_phys) != 0) {
        printf("KVM: VMXON failed\n");
        write_cr4(read_cr4() & ~CR4_VMXE);
        kfree(vm->guest_mem);
        kfree(vm->vmcs_region);
        kfree(vm->vmxon_region);
        if (vm->io_bitmap) kfree(vm->io_bitmap);
        if (vm->msr_bitmap) kfree(vm->msr_bitmap);
        kfree(vm->vcpu_states);
        if (vm->page_dir) kfree(vm->page_dir);
        kfree(vm);
        mutex_unlock(&kvm_lock);
        return -1;
    }

    /* 清除 VMCS */
    if (vmclear(vm->vmcs_phys) != 0) {
        printf("KVM: VMCLEAR failed\n");
        vmxoff();
        write_cr4(read_cr4() & ~CR4_VMXE);
        kfree(vm->guest_mem);
        kfree(vm->vmcs_region);
        kfree(vm->vmxon_region);
        if (vm->io_bitmap) kfree(vm->io_bitmap);
        if (vm->msr_bitmap) kfree(vm->msr_bitmap);
        kfree(vm->vcpu_states);
        if (vm->page_dir) kfree(vm->page_dir);
        kfree(vm);
        mutex_unlock(&kvm_lock);
        return -1;
    }

    /* 加载 VMCS */
    if (vmptrld(vm->vmcs_phys) != 0) {
        printf("KVM: VMPTRLD failed\n");
        vmxoff();
        write_cr4(read_cr4() & ~CR4_VMXE);
        kfree(vm->guest_mem);
        kfree(vm->vmcs_region);
        kfree(vm->vmxon_region);
        if (vm->io_bitmap) kfree(vm->io_bitmap);
        if (vm->msr_bitmap) kfree(vm->msr_bitmap);
        kfree(vm->vcpu_states);
        if (vm->page_dir) kfree(vm->page_dir);
        kfree(vm);
        mutex_unlock(&kvm_lock);
        return -1;
    }

    /* 初始化 VMCS - 主机状态 */
    vmwrite(VMCS_HOST_CR0, read_cr0());
    vmwrite(VMCS_HOST_CR3, 0);
    vmwrite(VMCS_HOST_CR4, read_cr4());

    /* Host RIP: VM exit 后的入口 */
    extern void kvm_vm_exit_handler(void);
    vmwrite(VMCS_HOST_RIP, (uint32_t)kvm_vm_exit_handler);
    vmwrite(VMCS_HOST_RSP, 0);

    /* 控制字段 */
    uint32_t pin_ctls = PIN_BASED_EXT_INTR;
    vmwrite(VMCS_CTRL_PIN_BASED, pin_ctls);

    uint32_t cpu_ctls = CPU_BASED_HLT_EXIT | CPU_BASED_IO_EXIT |
                        CPU_BASED_MSR_EXIT | CPU_BASED_CR_EXIT;
    vmwrite(VMCS_CTRL_CPU_BASED, cpu_ctls);

    uint32_t exit_ctls = VM_EXIT_HOST_32;
    vmwrite(VMCS_CTRL_VM_EXIT, exit_ctls);

    uint32_t entry_ctls = VM_ENTRY_GUEST_32;
    vmwrite(VMCS_CTRL_VM_ENTRY, entry_ctls);

    /* 客户机初始状态 */
    uint32_t cr0 = (kvm_caps.cr0_fixed0 | CR0_PE) & kvm_caps.cr0_fixed1;
    uint32_t cr4 = (kvm_caps.cr4_fixed0 | CR4_VMXE) & kvm_caps.cr4_fixed1;

    vmwrite(VMCS_GUEST_CR0, cr0);
    vmwrite(VMCS_GUEST_CR3, 0);
    vmwrite(VMCS_GUEST_CR4, cr4);
    vmwrite(VMCS_GUEST_RFLAGS, RFLAGS_FIXED);
    vmwrite(VMCS_GUEST_RIP, 0);
    vmwrite(VMCS_GUEST_RSP, (uint32_t)memory_size - 4);

    /* 客户机段选择子 */
    vmwrite(VMCS_GUEST_CS_SELECTOR, 0x08);
    vmwrite(VMCS_GUEST_DS_SELECTOR, 0x10);
    vmwrite(VMCS_GUEST_ES_SELECTOR, 0x10);
    vmwrite(VMCS_GUEST_FS_SELECTOR, 0x10);
    vmwrite(VMCS_GUEST_SS_SELECTOR, 0x10);
    vmwrite(VMCS_GUEST_GS_SELECTOR, 0x10);

    /* 注册到 VM 数组 */
    kvm_vms[kvm_vm_count] = vm;
    kvm_vm_count++;

    printf("KVM: VM %u created with %u KB guest memory, %u vCPUs\n",
           vm->vmid, (uint32_t)memory_size / 1024, vcpu_count);

    mutex_unlock(&kvm_lock);
    return (int)vm->vmid;
}

/* ---- 新 API: 销毁虚拟机 ---- */
int kvm_destroy_vm(uint32_t vmid) {
    mutex_lock(&kvm_lock);

    uint32_t i;
    for (i = 0; i < kvm_vm_count; i++) {
        if (kvm_vms[i] && kvm_vms[i]->vmid == vmid) {
            kvm_vm_t *vm = kvm_vms[i];

            /* 如果正在运行则先暂停 */
            if (vm->state == KVM_VM_RUNNING) {
                vm->state = KVM_VM_STOPPED;
            }

            /* 释放资源 */
            if (vm->guest_mem) kfree(vm->guest_mem);
            if (vm->vmcs_region) kfree(vm->vmcs_region);
            if (vm->vmxon_region) kfree(vm->vmxon_region);
            if (vm->io_bitmap) kfree(vm->io_bitmap);
            if (vm->msr_bitmap) kfree(vm->msr_bitmap);
            if (vm->vcpu_states) kfree(vm->vcpu_states);
            if (vm->page_dir) kfree(vm->page_dir);
            kfree(vm);

            /* 从数组中移除 */
            uint32_t j;
            for (j = i; j + 1 < kvm_vm_count; j++) {
                kvm_vms[j] = kvm_vms[j + 1];
            }
            kvm_vms[kvm_vm_count - 1] = NULL;
            kvm_vm_count--;

            mutex_unlock(&kvm_lock);
            return 0;
        }
    }

    mutex_unlock(&kvm_lock);
    return -1;
}

/* ---- 新 API: 运行虚拟机 ---- */
int kvm_run_vm(uint32_t vmid) {
    mutex_lock(&kvm_lock);
    kvm_vm_t *vm = kvm_find_vm(vmid);
    if (!vm) {
        mutex_unlock(&kvm_lock);
        return -1;
    }

    if (vm->state == KVM_VM_RUNNING) {
        mutex_unlock(&kvm_lock);
        return 0;  /* 已在运行 */
    }

    vm->state = KVM_VM_RUNNING;
    if (vm->vcpu_count > 0) {
        vm->vcpu_states[0] = KVM_VCPU_RUNNING;
    }

    /* 设置当前 VM */
    kvm_current_vm = vm;

    mutex_unlock(&kvm_lock);

    /* 加载 VMCS 并运行 */
    vmptrld(vm->vmcs_phys);

    /* 设置 host RSP */
    uint32_t host_rsp;
    asm volatile("mov %%esp, %0" : "=r"(host_rsp));
    vmwrite(VMCS_HOST_RSP, host_rsp - 256);

    if (!vm->launched) {
        vm->launched = 1;
        asm volatile(
            "vmlaunch\n"
            "setc %%al\n"
            "setz %%ah\n"
            : "=a"(vm->exited)
            :
            : "memory", "cc"
        );
    } else {
        asm volatile(
            "vmresume\n"
            "setc %%al\n"
            "setz %%ah\n"
            : "=a"(vm->exited)
            :
            : "memory", "cc"
        );
    }

    /* VM exit 后读取退出信息 */
    vm->exit_reason = vmread(0x4402);
    vm->exit_qualification = vmread(0x6400);
    vm->guest_rip = vmread(VMCS_GUEST_RIP);
    vm->guest_rsp = vmread(VMCS_GUEST_RSP);
    vm->guest_rflags = vmread(VMCS_GUEST_RFLAGS);

    /* 处理 VM exit */
    kvm_vm_handle_exit(vm);

    /* 检查 VM 是否应该停止 */
    if (vm->exited) {
        vm->state = KVM_VM_PAUSED;
        if (vm->vcpu_count > 0) {
            vm->vcpu_states[0] = KVM_VCPU_HALTED;
        }
    }

    return 0;
}

/* ---- 新 API: 暂停虚拟机 ---- */
int kvm_pause_vm(uint32_t vmid) {
    mutex_lock(&kvm_lock);
    kvm_vm_t *vm = kvm_find_vm(vmid);
    if (!vm) {
        mutex_unlock(&kvm_lock);
        return -1;
    }

    if (vm->state != KVM_VM_RUNNING) {
        mutex_unlock(&kvm_lock);
        return -1;
    }

    vm->state = KVM_VM_PAUSED;
    uint32_t i;
    for (i = 0; i < vm->vcpu_count; i++) {
        if (vm->vcpu_states[i] == KVM_VCPU_RUNNING) {
            vm->vcpu_states[i] = KVM_VCPU_HALTED;
        }
    }

    mutex_unlock(&kvm_lock);
    return 0;
}

/* ---- 新 API: 恢复虚拟机 ---- */
int kvm_resume_vm(uint32_t vmid) {
    mutex_lock(&kvm_lock);
    kvm_vm_t *vm = kvm_find_vm(vmid);
    if (!vm) {
        mutex_unlock(&kvm_lock);
        return -1;
    }

    if (vm->state != KVM_VM_PAUSED) {
        mutex_unlock(&kvm_lock);
        return -1;
    }

    vm->state = KVM_VM_RUNNING;
    if (vm->vcpu_count > 0) {
        vm->vcpu_states[0] = KVM_VCPU_RUNNING;
    }

    mutex_unlock(&kvm_lock);

    /* 继续运行 VM */
    return kvm_run_vm(vmid);
}

/* ---- 新 API: 获取 VM 状态 ---- */
int kvm_get_vm_state(uint32_t vmid, kvm_vm_t *state) {
    if (!state) return -1;

    mutex_lock(&kvm_lock);
    kvm_vm_t *vm = kvm_find_vm(vmid);
    if (!vm) {
        mutex_unlock(&kvm_lock);
        return -1;
    }

    memcpy(state, vm, sizeof(kvm_vm_t));
    mutex_unlock(&kvm_lock);
    return 0;
}

/* ---- 新 API: 设置 VM 内存 ---- */
int kvm_set_vm_memory(uint32_t vmid, uint64_t gpa, void *data, uint32_t size) {
    mutex_lock(&kvm_lock);
    kvm_vm_t *vm = kvm_find_vm(vmid);
    if (!vm || !vm->guest_mem) {
        mutex_unlock(&kvm_lock);
        return -1;
    }

    /* 检查范围 */
    if (gpa + size > vm->guest_mem_size) {
        mutex_unlock(&kvm_lock);
        return -1;
    }

    /* 将数据复制到客户机内存 */
    if (data) {
        memcpy((uint8_t *)vm->guest_mem + (uint32_t)gpa, data, size);
    }

    mutex_unlock(&kvm_lock);
    return 0;
}

/* ---- 新 API: 注入中断 ---- */
int kvm_inject_interrupt(uint32_t vmid, uint32_t vcpu_id, uint8_t vector) {
    mutex_lock(&kvm_lock);
    kvm_vm_t *vm = kvm_find_vm(vmid);
    if (!vm) {
        mutex_unlock(&kvm_lock);
        return -1;
    }

    if (vcpu_id >= vm->vcpu_count) {
        mutex_unlock(&kvm_lock);
        return -1;
    }

    /* 通过 VMCS 注入中断:
     * VM-entry interruption-information field:
     *   bit 31: valid
     *   bit 30: error code valid
     *   bits 11:0: vector
     *   bits 15:13: type (0=external) */
    vmptrld(vm->vmcs_phys);

    uint32_t intr_info = (1 << 31) | (0 << 8) | (vector & 0xFF);
    vmwrite(0x4016, intr_info);  /* VM-entry interruption-info */
    vmwrite(0x4018, 0);          /* VM-entry exception error code */

    mutex_unlock(&kvm_lock);
    return 0;
}

/* ---- 旧接口兼容 ---- */

kvm_vm_t *kvm_vm_create(uint32_t mem_size) {
    int vmid = kvm_create_vm((uint64_t)mem_size, 1);
    if (vmid < 0) return NULL;
    return kvm_find_vm((uint32_t)vmid);
}

void kvm_vm_destroy(kvm_vm_t *vm) {
    if (!vm) return;
    kvm_destroy_vm(vm->vmid);
}

int kvm_vm_set_guest_state(kvm_vm_t *vm, uint32_t rip, uint32_t rsp, uint32_t cr3, uint32_t rflags) {
    if (!vm) return -1;

    vmptrld(vm->vmcs_phys);

    vmwrite(VMCS_GUEST_RIP, rip);
    vmwrite(VMCS_GUEST_RSP, rsp);
    if (cr3) vmwrite(VMCS_GUEST_CR3, cr3);
    vmwrite(VMCS_GUEST_RFLAGS, rflags);

    vm->guest_rip = rip;
    vm->guest_rsp = rsp;
    vm->guest_rflags = rflags;

    return 0;
}

int kvm_vm_load_code(kvm_vm_t *vm, const void *code, uint32_t size, uint32_t entry) {
    if (!vm || !code || size == 0) return -1;
    if (size > vm->guest_mem_size) return -1;

    memcpy(vm->guest_mem, code, size);
    vm->guest_entry = entry;

    vmptrld(vm->vmcs_phys);
    vmwrite(VMCS_GUEST_RIP, entry);
    vm->guest_rip = entry;

    return 0;
}

/* Assembly stub for VM exit handler */
__attribute__((naked)) void kvm_vm_exit_handler(void) {
    asm volatile(
        "movl $0x681E, %%eax\n"    /* VMCS_GUEST_RIP */
        "vmread %%eax, %%ebx\n"
        "pushl %%ebx\n"
        "movl $0x681C, %%eax\n"    /* VMCS_GUEST_RSP */
        "vmread %%eax, %%ebx\n"
        "pushl %%ebx\n"
        "movl $0x6820, %%eax\n"    /* VMCS_GUEST_RFLAGS */
        "vmread %%eax, %%ebx\n"
        "pushl %%ebx\n"
        "movl $0x4402, %%eax\n"    /* VMCS_RO_EXIT_REASON */
        "vmread %%eax, %%ebx\n"
        "pushl %%ebx\n"
        "movl $0x6400, %%eax\n"    /* VMCS_RO_EXIT_QUALIFICATION */
        "vmread %%eax, %%ebx\n"
        "pushl %%ebx\n"
        "call _kvm_vm_exit_c_handler\n"
        "addl $20, %%esp\n"
        "ret\n"
        ::: "eax", "ebx", "ecx", "edx", "memory"
    );
}

/* C handler called from assembly exit handler */
void kvm_vm_exit_c_handler(uint32_t qualification, uint32_t reason,
                            uint32_t rflags, uint32_t rsp, uint32_t rip) {
    (void)qualification;
    (void)reason;
    (void)rflags;
    (void)rsp;
    (void)rip;
}

int kvm_vm_run(kvm_vm_t *vm) {
    if (!vm) return -1;
    return kvm_run_vm(vm->vmid);
}

void kvm_vm_handle_exit(kvm_vm_t *vm) {
    if (!vm) return;

    uint32_t reason = vm->exit_reason & 0xFFFF;

    switch (reason) {
    case VMX_EXIT_CPUID: {
        (void)vm->guest_rip;
        vmwrite(VMCS_GUEST_RIP, vm->guest_rip + 2);
        break;
    }
    case VMX_EXIT_HLT: {
        vm->exited = 1;
        printf("KVM: VM %u Guest executed HLT at RIP=0x%x\n",
               vm->vmid, vm->guest_rip);
        break;
    }
    case VMX_EXIT_IO_INSTRUCTION: {
        uint32_t qual = vm->exit_qualification;
        uint32_t port = (qual >> 16) & 0xFFFF;
        int is_write = (qual >> 3) & 0x1;
        int size = (qual & 0x7) + 1;
        (void)is_write;
        (void)size;
        printf("KVM: VM %u Guest I/O port 0x%x (write=%d, size=%d)\n",
               vm->vmid, port, is_write, size);
        vmwrite(VMCS_GUEST_RIP, vm->guest_rip + 2);
        break;
    }
    case VMX_EXIT_MSR_READ: {
        vmwrite(VMCS_GUEST_RIP, vm->guest_rip + 2);
        break;
    }
    case VMX_EXIT_MSR_WRITE: {
        vmwrite(VMCS_GUEST_RIP, vm->guest_rip + 2);
        break;
    }
    case VMX_EXIT_CR_ACCESS: {
        vmwrite(VMCS_GUEST_RIP, vm->guest_rip + 3);
        break;
    }
    case VMX_EXIT_EPT_VIOLATION: {
        printf("KVM: VM %u EPT violation at RIP=0x%x, qual=0x%x\n",
               vm->vmid, vm->guest_rip, vm->exit_qualification);
        vm->exited = 1;
        break;
    }
    case VMX_EXIT_EXCEPTION: {
        printf("KVM: VM %u Guest exception at RIP=0x%x\n",
               vm->vmid, vm->guest_rip);
        vm->exited = 1;
        break;
    }
    case VMX_EXIT_TRIPLE_FAULT: {
        printf("KVM: VM %u Guest triple fault\n", vm->vmid);
        vm->exited = 1;
        break;
    }
    case VMX_EXIT_VMCALL: {
        /* 处理 VMCALL 超级调用 */
        printf("KVM: VM %u VMCALL at RIP=0x%x\n",
               vm->vmid, vm->guest_rip);
        vmwrite(VMCS_GUEST_RIP, vm->guest_rip + 3);
        break;
    }
    default: {
        printf("KVM: VM %u Unhandled exit reason %u at RIP=0x%x\n",
               vm->vmid, reason, vm->guest_rip);
        vm->exited = 1;
        break;
    }
    }
}
