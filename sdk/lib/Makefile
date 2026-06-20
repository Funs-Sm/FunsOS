# FUNSOS SDK Library Makefile

CC      = gcc
CFLAGS  = -m32 -ffreestanding -nostdlib -I ../include -Wall -Wextra
AR      = ar

TARGET  = libfunsos_api.a
OBJ     = funsos_api.o funsos_glue.o funsos_libc.o funsos_surface.o funsos_pipeline.o funsos_event.o funsos_window.o funsos_audio.o

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

funsos_libc.o: funsos_libc.c ../include/funsos.h ../include/funsos_libc.h
	$(CC) $(CFLAGS) -c $< -o $@

funsos_surface.o: funsos_surface.c ../include/funsos.h ../include/funsos_libc.h
	$(CC) $(CFLAGS) -c $< -o $@

funsos_pipeline.o: funsos_pipeline.c ../include/funsos.h ../include/funsos_libc.h
	$(CC) $(CFLAGS) -c $< -o $@

funsos_event.o: funsos_event.c ../include/funsos.h ../include/funsos_event.h ../include/funsos_libc.h
	$(CC) $(CFLAGS) -c $< -o $@

funsos_window.o: funsos_window.c ../include/funsos.h ../include/funsos_window.h ../include/funsos_surface.h ../include/funsos_libc.h
	$(CC) $(CFLAGS) -c $< -o $@

funsos_audio.o: funsos_audio.c ../include/funsos.h ../include/funsos_audio.h ../include/funsos_libc.h
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
