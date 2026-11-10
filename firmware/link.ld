/* GD32F303CBT6: 128 KB flash, 48 KB RAM. Only what an STM32F103CB also has is used
 * (20 KB RAM). The last flash page (0x0801F800) is kept free for the settings. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 126K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > FLASH

    /* Fixed block at the start of RAM, readable/writable over SWD:
     * 0x20000000 status (written by the firmware), 0x20000100 control (written by the tools). */
    .debug_block (NOLOAD) :
    {
        KEEP(*(.status))
        . = 0x100;
        KEEP(*(.control))
        . = 0x200;
    } > RAM

    .data : AT(_etext) { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM
    .bss : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
}
