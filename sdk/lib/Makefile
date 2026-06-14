# FUNSOS SDK Library Makefile

CC      = gcc
CFLAGS  = -m32 -ffreestanding -nostdlib -I ../include -Wall -Wextra
AR      = ar

TARGET  = libfunsos_api.a
OBJ     = funsos_api.o funsos_glue.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(AR) rcs $@ $^

funsos_api.o: funsos_api.c funsos_api.h ../include/funsos.h
	$(CC) $(CFLAGS) -c $< -o $@

funsos_glue.o: funsos_glue.c funsos_api.h ../include/funsos.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
