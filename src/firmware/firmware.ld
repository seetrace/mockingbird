OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* Firmware starts at physical 0xF0000 */
    . = 0xF0000;

    .text :
    {
        KEEP(*(.text))
    }

    /* Reset vector at physical 0xFFFF0 */
    . = 0xFFFF0;

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
