OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
OUTPUT("system")
ENTRY(main)

SECTIONS {
    . = 0xc0001500;
    .text : 
    {
        *(.text)
    }
    . = ALIGN(0x1000);
    .data : 
    {
        *(.data)
    }
    . = ALIGN(0X1000);
    .rodata : 
    {
        *(.rodata)
    }
    . = ALIGN(0X1000);
    .bss : 
    {
        *(.bss)
    }
}
