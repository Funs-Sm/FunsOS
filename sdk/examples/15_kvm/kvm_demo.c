/* kvm_demo.c - KVM virtualization example
 * Demonstrates KVM-based virtualization on FUNSOS.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(80, 60, 550, 380, "KVM Demo");
    funsos_fill_window(win, 0x1E1E1E);

    funsos_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0xFF, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x78, 0xD4, 0xFF};

    funsos_draw_text(win, 20, 20, "KVM Virtualization Demo", green);

    funsos_draw_text(win, 20, 60, "FUNSOS supports KVM-based VMs", white);
    funsos_draw_text(win, 20, 85, "for running guest operating systems.", white);

    funsos_draw_text(win, 20, 120, "KVM API Overview:", blue);
    funsos_draw_text(win, 40, 145, "kvm_init()      - Initialize KVM", white);
    funsos_draw_text(win, 40, 170, "kvm_create_vm()  - Create virtual machine", white);
    funsos_draw_text(win, 40, 195, "kvm_create_vcpu()- Create virtual CPU", white);
    funsos_draw_text(win, 40, 220, "kvm_run()        - Run guest code", white);
    funsos_draw_text(win, 40, 245, "kvm_set_memory() - Set guest memory", white);

    funsos_draw_text(win, 20, 290, "Press SPACE to create a test VM", blue);
    funsos_draw_text(win, 20, 315, "Press Q to quit", white);

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;

        if (event.type == FUNSOS_EVENT_KEY_PRESS) {
            if (event.key == 0x1B || event.key == 'q')
                break;
            if (event.key == ' ') {
                funsos_draw_text(win, 20, 345, "VM created (simulated)", green);
            }
        }
    }

    funsos_destroy_window(win);
    return 0;
}
