CC = gcc
CFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector -Wall -I../lib -I../include
AS = nasm
ASFLAGS = -f elf32
LD = ld
LDFLAGS = -m elf_i386 -T user.ld

CLI_APPS = shell ls cat echo mkdir rm cp mv touch help calc
GUI_APPS = desktop terminal filemgr notepad paint snake
ALL_APPS = $(CLI_APPS) $(GUI_APPS)

all: $(addsuffix .elf,$(ALL_APPS))

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.elf: crt0.o %.o
	$(LD) $(LDFLAGS) -o $@ $^ -L../lib -los

clean:
	rm -f *.o *.elf
