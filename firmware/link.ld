/* GD32F303CBT6: 128 KB Flash, 48 KB RAM. Genutzt wird nur, was auch ein STM32F103CB hat
 * (20 KB RAM). Die letzte Flash-Seite (0x0801F800) bleibt frei fuer Einstellungen. */
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

    /* Fester Bereich am RAM-Anfang, per SWD lesbar/schreibbar:
     * 0x20000000 Status (Firmware schreibt), 0x20000100 Steuerung (wir schreiben). */
    .debug_bereich (NOLOAD) :
    {
        KEEP(*(.status))
        . = 0x100;
        KEEP(*(.steuer))
        . = 0x200;
    } > RAM

    .data : AT(_etext) { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM
    .bss : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
}
