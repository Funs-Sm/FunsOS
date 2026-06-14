# Getting Started with FUNSOS SDK

## Prerequisites

- GCC cross-compiler (i686-elf target)
- FUNSOS SDK headers and libraries
- Basic knowledge of C programming

## Installation

1. Clone or download the FUNSOS SDK
2. Build the runtime library:
   ```bash
   cd sdk/lib
   make
   ```
3. Verify the build produced `libfunsos_api.a`

## Your First Application

Create a file `hello.c`:

```c
#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(200, 150, 400, 300, "Hello");
    funsos_fill_window(win, 0xFFFFFF);
    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_draw_text(win, 120, 120, "Hello, FUNSOS!", black);

    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0) continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B) break;
    }

    funsos_destroy_window(win);
    return 0;
}
```

## Compiling

```bash
gcc -m32 -ffreestanding -I sdk/include -c hello.c -o hello.o
gcc -m32 -nostdlib -L sdk/lib -o hello.elf hello.o -lfunsos_api
```

## Deploying

Copy the resulting ELF binary to the FUNSOS filesystem:

```bash
cp hello.elf /usr/bin/hello
```

## Project Structure

```
my_app/
├── Makefile          # Use sdk/tools/build_template.mk
├── main.c            # Application entry point
└── README.md         # Documentation
```

## Next Steps

- See `examples/` for 20 complete example programs
- Read `docs/api_reference.md` for full API documentation
- Read `docs/architecture.md` for system architecture overview
