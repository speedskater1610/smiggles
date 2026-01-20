typedef unsigned short uint16_t;

void kernel_main() {
    const char *msg = "Kernel running\n";
    uint16_t *video_memory = (uint16_t*)0xB8000;

    for (int i=0; msg[i] != 0; i++)
        video_memory[i] = (msg[i] | 0x0F00); // white on black

    while(1); // hang
}
