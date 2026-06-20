# Top-level Makefile for custom x86 kernel
# Toolchain
CC      = gcc
AS      = nasm
LD      = ld

# Build directories
BUILDDIR = build

# Compiler flags for 32-bit freestanding kernel
CFLAGS  = -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
          -fno-stack-check -mno-stack-arg-probe \
          -fno-pie -fno-pic -Wall -Wextra -Wno-unused-parameter \
          -Ilib -Ikernel -Idrivers -Idrivers/gpu -Idrivers/net -Idrivers/audio -Idrivers/block -Idrivers/char -Idrivers/video -Ifs -Inet -Igui -Iusb -Iaudio -Iboot -Iapps \
          -Isdk/include -Isdk/lib -Irenderer/include -Irenderer/themes -Ios -Ios/apps -Ios/desktop -Ios/services

# Assembler flags
ASFLAGS = -f elf32 -O999

# Linker flags - use gcc as linker driver for cross-format support
LDFLAGS = -m32 -nostdlib -T boot/linker.ld -Wl,--oformat=pei-i386

# SDK用户态库(不链接到内核, 避免与GUI/内核实现符号冲突)
SDK_OBJ   = $(patsubst %.c,$(BUILDDIR)/%.o,$(SDK_C))
RENDERER_OBJ = $(patsubst %.c,$(BUILDDIR)/%.o,$(RENDERER_C))
SDK_EXCLUDE = $(SDK_OBJ) $(RENDERER_OBJ)

# -------------------------------------------------------------------
#  Source file discovery
# -------------------------------------------------------------------

# Kernel C sources (including unified system services)
KERNEL_C = $(wildcard kernel/*.c)
# Kernel ASM sources (all .asm in kernel/ and lib/, excluding conflicts)
KERNEL_ASM = kernel/entry.asm kernel/interrupt.asm kernel/context.asm kernel/spinlock_asm.asm kernel/fpu_asm.asm kernel/acpi_asm.asm kernel/smp_trampoline.asm
LIB_ASM    = lib/memops.asm lib/atomic.asm lib/string_asm.asm lib/setjmp.asm

# All assembly sources
ALL_ASM = $(KERNEL_ASM) $(LIB_ASM)

# Driver C sources (all subdirs except gpu, which is added separately)
DRIVER_C = $(wildcard drivers/*.c) $(wildcard drivers/block/*.c) $(wildcard drivers/char/*.c) $(filter-out drivers/net/wifi_stub.c,$(wildcard drivers/net/*.c)) $(wildcard drivers/usb/*.c) $(wildcard drivers/video/*.c)
DRIVERS_GPU_C = $(wildcard drivers/gpu/*.c)
DRIVERS_NET_C = $(wildcard drivers/net/*.c)
DRIVERS_AUDIO_C = $(wildcard drivers/audio/*.c)

# Filesystem C sources (including advanced VFS features)
FS_C = $(wildcard fs/*.c)

# Network C sources
NET_C = $(filter-out net/wifi_stub.c, $(wildcard net/*.c))

# GUI C sources
GUI_C = $(wildcard gui/*.c)

# USB C sources
USB_C = $(wildcard usb/*.c)

# Audio C sources
AUDIO_C = $(wildcard audio/*.c)

# Lib C sources
LIB_C = $(wildcard lib/*.c)

# Userland C sources
USERLAND_C = $(wildcard userland/*.c)

# Kernel-mode app sources (linked into kernel)
APPS_C = $(wildcard apps/*_app.c) apps/init.c

# SDK sources (integrated into kernel)
SDK_C = $(wildcard sdk/lib/*.c)

# Renderer sources (integrated into kernel)
# Temporarily excluded due to pre-existing compilation issues
RENDERER_C = $(wildcard renderer/src/*.c)

# OS layer sources (integrated into kernel)
OS_C = $(wildcard os/apps/*.c) $(wildcard os/desktop/*.c) $(wildcard os/services/*.c) $(wildcard os/*.c)

# All kernel C sources combined
ALL_C = $(LIB_C) $(KERNEL_C) $(DRIVER_C) $(DRIVERS_GPU_C) $(DRIVERS_AUDIO_C) $(FS_C) $(NET_C) $(GUI_C) $(USB_C) $(AUDIO_C) $(APPS_C) $(SDK_C) $(RENDERER_C) $(OS_C)

# Object files
ALL_C_OBJ   = $(patsubst %.c,$(BUILDDIR)/%.o,$(ALL_C))
ALL_ASM_OBJ = $(patsubst %.asm,$(BUILDDIR)/%.o,$(ALL_ASM))

# Boot sector binaries
BOOT_BIN   = $(BUILDDIR)/boot/boot.bin
LOADER_BIN = $(BUILDDIR)/boot/loader.bin
STAGE2_BIN = $(BUILDDIR)/boot/stage2.bin

# Final output
KERNEL_ELF = $(BUILDDIR)/kernel.elf
OS_IMAGE   = $(BUILDDIR)/os.img

# -------------------------------------------------------------------
#  Build rules
# -------------------------------------------------------------------

.PHONY: all clean kernel boot apps dirs run debug package

all: $(OS_IMAGE)

# Create all needed output directories (Windows cmd compatible)
dirs:
	-@mkdir $(BUILDDIR) $(BUILDDIR)/kernel $(BUILDDIR)/drivers \
	  $(BUILDDIR)/drivers/block $(BUILDDIR)/drivers/char $(BUILDDIR)/drivers/net \
	  $(BUILDDIR)/drivers/usb $(BUILDDIR)/drivers/video $(BUILDDIR)/drivers/gpu \
	  $(BUILDDIR)/drivers/audio $(BUILDDIR)/fs $(BUILDDIR)/net $(BUILDDIR)/gui \
	  $(BUILDDIR)/usb $(BUILDDIR)/audio $(BUILDDIR)/lib $(BUILDDIR)/userland \
	  $(BUILDDIR)/boot $(BUILDDIR)/apps $(BUILDDIR)/sdk $(BUILDDIR)/sdk/lib \
	  $(BUILDDIR)/renderer $(BUILDDIR)/renderer/src $(BUILDDIR)/os \
	  $(BUILDDIR)/os/apps $(BUILDDIR)/os/desktop $(BUILDDIR)/os/services 2>nul

# --- Kernel ELF ---
kernel: $(KERNEL_ELF)

$(KERNEL_ELF): $(ALL_ASM_OBJ) $(ALL_C_OBJ)
	@echo "  LINK    $@"
	@$(CC) $(LDFLAGS) -o $@ $(filter-out $(SDK_EXCLUDE),$^)

# Compile C files -> object files
$(BUILDDIR)/%.o: %.c | dirs
	$(CC) $(CFLAGS) -c -o $@ $<

# Assemble kernel ASM files -> object files
$(BUILDDIR)/%.o: %.asm | dirs
	$(AS) $(ASFLAGS) -o $@ $<

# --- Boot sector ---
boot: $(BOOT_BIN) $(LOADER_BIN) $(STAGE2_BIN)

$(BOOT_BIN): boot/boot.asm | dirs
	$(AS) -f bin -o $@ $<

$(LOADER_BIN): boot/loader.asm | dirs
	$(AS) -f bin -o $@ $<

$(STAGE2_BIN): boot/stage2.asm | dirs
	$(AS) -f bin -o $@ $<

# --- Disk image ---
$(OS_IMAGE): $(BOOT_BIN) $(LOADER_BIN) $(STAGE2_BIN) $(KERNEL_ELF)
	@echo Creating disk image...
	@objcopy -O binary -R .note -R .comment -R .eh_frame $(KERNEL_ELF) $(BUILDDIR)/kernel.bin
	@python tools/mkimg.py $@ $(BOOT_BIN) $(LOADER_BIN) $(STAGE2_BIN) $(BUILDDIR)/kernel.bin

# --- Apps ---
apps:
	$(MAKE) -C apps

# --- Clean ---
clean:
	-rmdir /s /q $(BUILDDIR) 2>nul
	$(MAKE) -C apps clean

# --- Run in QEMU ---
run: all
	qemu-system-i386 -drive format=raw,file=build/os.img,if=ide,index=0 -m 128 -serial stdio

# --- Debug with QEMU + GDB ---
debug: all
	qemu-system-i386 -drive format=raw,file=build/os.img,if=ide,index=0 -m 128 -s -S -serial stdio

# --- Package for distribution ---
package: all
	@echo "Creating package..."
	@mkdir -p funs-core-0.5/boot funs-core-0.5/kernel
	@cp build/os.img funs-core-0.5/boot/
	@cp build/kernel.elf funs-core-0.5/kernel/
	@echo "Package created in funs-core-0.5/"
