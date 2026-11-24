OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* Flat firmware offsets are mapped by KVM at physical 0xF0000. */
    . = 0;

    .text :
    {
        KEEP(*(.text))
    }

    /* Reset vector is offset 0xFFF0 in the flat 64 KiB image. */
    . = 0xFFF0;

    .reset :
    {
        KEEP(*(.reset))
    }

    ASSERT(SIZEOF(.text) <= 0xFFF0,
           "Firmware code overlaps reset vector")

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
