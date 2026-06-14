# ==============================================================================
# FUNSOS SDK Build Template (build_template.mk)
# ==============================================================================
# Include this Makefile in your project's root Makefile to get standard
# build rules for FUNSOS applications. Supports both simple and complex
# projects with optional networking, 3D rendering, and other features.
#
# Usage:
#   1. Copy this file to your project directory as "Makefile"
#   2. Adjust variables below as needed (SDK_ROOT, TARGET, etc.)
#   3. Run: make          # Build the application
#          make clean      # Remove build artifacts
#          make install    # Deploy to FUNSOS image
#          make run        # Build + deploy + run in QEMU
#          make test       # Run automated tests (if available)
# ==============================================================================

# =============================================================================
# SECTION 1: SDK Paths & Toolchain Configuration
# =============================================================================
# These variables define where to find the SDK headers, libraries, and tools.
# Override them on the command line or in your project Makefile if needed.

SDK_ROOT   ?= ../..                    # Root of the FUNSOS source tree
SDK_INCLUDE = $(SDK_ROOT)/include      # Directory containing funsos.h etc.
SDK_LIB     = $(SDK_ROOT)/lib          # Directory containing libfunsos_api.a
SDK_TOOLS   = $(SDK_ROOT)/tools        # SDK utility scripts

# ---- Toolchain ----
# Uses GCC targeting 32-bit x86 with no standard library (freestanding).
CC      = gcc                          # C compiler
AS      = as                           # Assembler (for .s files)
LD      = gcc                          # Linker (use gcc for driver convenience)
OBJCOPY = objcopy                      # Binary manipulation tool

# ---- Compiler Flags ----
# -m32:           Generate 32-bit x86 code (required for FUNSOS kernel)
# -ffreestanding: Don't assume standard library existence
# -nostdlib:      Don't link against standard C library
# -I$(SDK_INCLUDE): Search SDK headers for #include directives
CFLAGS  = -m32 -ffreestanding -nostdlib -I$(SDK_INCLUDE) -Wall -Wextra -O2

# ---- Linker Flags ----
LDFLAGS = -m32 -nostdlib -L$(SDK_LIB)

# =============================================================================
# SECTION 2: Project Configuration
# =============================================================================
# Customize these for your specific project.

TARGET  = app.elf                     # Output executable name
SOURCES = $(wildcard *.c)             # Auto-detect all .c files in current dir
HEADERS = $(wildcard *.h)             # Track header dependencies (if any)
OBJECTS = $(SOURCES:.c=.o)            # Derived object file list

# ---- Feature Flags (uncomment to enable) ----
# Enable networking support (links socket API, adds net syscalls)
# USE_NETWORK = 1

# Link against funsrender (3D hardware acceleration backend)
# USE_RENDER  = 1

# Enable audio subsystem support
# USE_AUDIO   = 1

# Debug build (adds -g, disables optimization)
# DEBUG       = 1

# =============================================================================
# SECTION 3: Conditional Feature Rules
# =============================================================================
# Automatically adjust compiler/linker flags based on feature flags above.

ifdef USE_NETWORK
    CFLAGS  += -DFUNSOS_HAS_NETWORK=1 -DNETWORK_DEMO
    LDFLAGS += -lfunsos_net
endif

ifdef USE_RENDER
    CFLAGS  += -DFUNSOS_HAS_3D_RENDER=1 -DHAVE_FUNSRENDER
    LDFLAGS += -lfunsrender
endif

ifdef USE_AUDIO
    CFLAGS  += -DFUNSOS_HAS_AUDIO=1
    LDFLAGS += -lfunsos_audio
endif

ifdef DEBUG
    CFLAGS  += -g -O0 -DDEBUG
endif

# Base libraries that all FUNSOS apps link against
LIBS = -lfunsos_api

# Combine all linker flags: paths + libraries
ALL_LDFLAGS = $(LDFLAGS) $(LIBS)

# =============================================================================
# SECTION 4: Build Targets
# =============================================================================

# ---- Default target: build everything ----
.PHONY: all
all: $(TARGET)
	@echo "Build complete: $(TARGET)"

# ---- Link: combine object files into final ELF executable ----
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) $(ALL_LDFLAGS) -o $@ $^
	@echo "  [LD] $@"

# ---- Compile: .c -> .o (with automatic header dependency tracking) ----
%.o: %.c $(HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "  [CC] $<"

# =============================================================================
# SECTION 5: Clean Target
# =============================================================================
# Remove all generated files (objects, executable, debug symbols, etc.)

.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS)
	rm -f $(TARGET)
	rm -f *.map *.sym *.lst     # Optional: remove map/symbol/listing files
	@echo "Clean complete."

# =============================================================================
# SECTION 6: Install Target
# =============================================================================
# Deploy the built application to the FUNSOS system image.
# Copies the ELF binary to /usr/bin/ in the target filesystem.
# Adjust INSTALL_PATH if your deployment location differs.

INSTALL_PATH ?= /usr/bin

.PHONY: install
install: $(TARGET)
	@echo "Installing $(TARGET) to $(INSTALL_PATH)/..."
	cp $(TARGET) $(INSTALL_PATH)/
	@echo "Install complete."

# =============================================================================
# SECTION 7: Run Target
# =============================================================================
# Build + deploy + optionally launch in QEMU emulator.
# Useful for quick development iteration cycles.

QEMU_OPTS ?= -fda $(SDK_ROOT)/build/os.img -m 256

.PHONY: run
run: install
	@echo "Launching FUNSOS in QEMU..."
	@echo "  (If QEMU is not found, install qemu-system-i386)"
	-qemu-system-i386 $(QEMU_OPTS)

# =============================================================================
# SECTION 8: Test Target
# =============================================================================
# Run any project-specific tests. This is a placeholder that can be
# overridden by projects with test suites.
# For unit tests, define a 'test' target in your project Makefile
# that builds and runs a test binary.

.PHONY: test
test: $(TARGET)
	@echo "Running tests for $(TARGET)..."
	@echo "(No test suite defined - add a 'test' target to your Makefile)"
	@echo "To run manually: deploy $(TARGET) and execute under FUNSOS"

# =============================================================================
# SECTION 9: Info Target
# =============================================================================
# Display current build configuration (useful for debugging build issues).

.PHONY: info
info:
	@echo "=== FUNSOS SDK Build Configuration ==="
	@echo "  SDK Root:    $(SDK_ROOT)"
	@echo "  Include Dir: $(SDK_INCLUDE)"
	@echo "  Library Dir: $(SDK_LIB)"
	@echo "  Target:      $(TARGET)"
	@echo "  Sources:     $(SOURCES)"
	@echo "  Objects:     $(OBJECTS)"
	@echo "  CC:          $(CC)"
	@echo "  CFLAGS:      $(CFLAGS)"
	@echo "  LDFLAGS:     $(LDFLAGS)"
	@echo "  LIBS:        $(LIBS)"
	@echo "====================================="
	@echo "  USE_NETWORK: $(USE_NETWORK)"
	@echo "  USE_RENDER:  $(USE_RENDER)"
	@echo "  USE_AUDIO:   $(USE_AUDIO)"
	@echo "  DEBUG:       $(DEBUG)"

# =============================================================================
# SECTION 10: Phony Declarations & Dependencies
# =============================================================================

.PHONY: all clean install run test info

# Ensure object files are rebuilt when this makefile changes
$(OBJECTS): build_template.mk

# Prevent accidental deletion of valuable files with "make clean"
.PRECIOUS: $(OBJECTS)
