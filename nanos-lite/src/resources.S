#if !defined(__ISA_AM_NATIVE__) && !defined(__ISA_NATIVE__) && !defined(__PLATFORM_NEMU)
.section .data
.global ramdisk_start, ramdisk_end
ramdisk_start:
.incbin "build/ramdisk.img"
ramdisk_end:
#endif

.section .rodata
.globl logo
logo:
.incbin "resources/logo.txt"
.byte 0
