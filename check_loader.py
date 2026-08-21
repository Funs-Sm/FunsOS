#!/usr/bin/env python3
import struct

with open('build/boot/loader.bin', 'rb') as f:
    data = f.read()

# Check the area around 0x1500-0x1600
print("=== Disassembly of 0x1500-0x1600 region (file offsets 0x0500-0x0600) ===")
i = 0x0500
while i < 0x0600 and i < len(data):
    start = i
    byte = data[i]

    # Try to disassemble common instructions
    if byte == 0xB8:  # MOV AX, imm16
        val = struct.unpack('<H', data[i+1:i+3])[0]
        print(f"0x{i:04x}: MOV AX, 0x{val:04x}")
        i += 3
    elif byte == 0xC7:  # MOV [imm], imm
        if data[i+1] == 0x06:
            addr = struct.unpack('<H', data[i+2:i+4])[0]
            val = struct.unpack('<I', data[i+4:i+8])[0]
            print(f"0x{i:04x}: MOV [0x{addr:04x}], 0x{val:08x}")
            i += 8
        else:
            print(f"0x{i:04x}: DB 0x{byte:02x}")
            i += 1
    elif byte == 0x66:  # Operand size prefix
        next_byte = data[i+1]
        if next_byte == 0xB8:  # MOV EAX, imm32
            val = struct.unpack('<I', data[i+2:i+6])[0]
            print(f"0x{i:04x}: MOV EAX, 0x{val:08x} (66 prefix)")
            i += 6
        elif next_byte == 0xC7:  # MOV [imm], imm32
            val = struct.unpack('<I', data[i+3:i+7])[0]
            print(f"0x{i:04x}: MOV [imm], 0x{val:08x} (66 prefix)")
            i += 7
        else:
            print(f"0x{i:04x}: DB 0x66 0x{next_byte:02x}")
            i += 2
    elif byte == 0x90:  # NOP
        print(f"0x{i:04x}: NOP")
        i += 1
    elif byte == 0x00:
        print(f"0x{i:04x}: DB 0x00")
        i += 1
    else:
        print(f"0x{i:04x}: DB 0x{byte:02x}")
        i += 1

# Now check what's ACTUALLY at 0x15f9 (file offset 0x05f9)
print("\n=== What's at 0x15f9? ===")
print(f"File offset 0x05f9: 0x{data[0x05f9]:02x}")
print(f"File offset 0x05fa: 0x{data[0x05fa]:02x}")
print(f"File offset 0x05fb: 0x{data[0x05fb]:02x}")
print(f"File offset 0x05fc: 0x{data[0x05fc]:02x}")

# Check if these bytes look like code or data
print(f"\nBytes 0x{data[0x05f9]:02x} 0x{data[0x05fa]:02x} could be:")
if data[0x05f9] == 0x00:
    print("  - Part of DD 0 (data)")
if data[0x05fa] == 0x00:
    print("  - Part of DD 0 (data)")
