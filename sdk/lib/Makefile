# FUNSOS SDK Library Makefile

CC      = gcc
CFLAGS  = -m32 -ffreestanding -nostdlib -I ../include -Wall -Wextra
AR      = ar

TARGET  = libfunsos_api.a
OBJ     = funsos_api.o funsos_glue.o

# Tools
TOOLS_DIR = ../tools
FUNPACK_SRC = $(TOOLS_DIR)/funpack.c
FUNPACK_OBJ = funpack.o
FUNPACK_BIN = funpack

all: $(TARGET)

$(TARGET): $(OBJ)
	$(AR) rcs $@ $^

funsos_api.o: funsos_api.c funsos_api.h ../include/funsos.h
	$(CC) $(CFLAGS) -c $< -o $@

funsos_glue.o: funsos_glue.c funsos_api.h ../include/funsos.h
	$(CC) $(CFLAGS) -c $< -o $@

# funpack tool
funpack: $(FUNPACK_SRC) $(FUNPACK_OBJ)
	$(CC) $(CFLAGS) -o $(FUNPACK_BIN) $(FUNPACK_SRC) $^

$(FUNPACK_OBJ): $(FUNPACK_SRC) $(TOOLS_DIR)/funpack.h
	$(CC) $(CFLAGS) -I $(TOOLS_DIR) -c $(FUNPACK_SRC) -o $@

tools: funpack

clean:
	rm -f $(OBJ) $(TARGET) $(FUNPACK_OBJ) $(FUNPACK_BIN)

.PHONY: all clean tools
