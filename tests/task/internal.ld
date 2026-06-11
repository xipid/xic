ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .rodata : {
        *(.rodata*)
    }

    . = ALIGN(0x1000);

    .text : {
        *(.text.entry)
        *(.text*)
    }

    . = ALIGN(0x1000);

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
    }

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}
