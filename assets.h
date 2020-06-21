
extern "C" {
    extern const char assets[];
}

__asm__(".global assets\n.align\nassets:\n.incbin \"assets.bin\"");
