OUTPUT_FORMAT(elf32-i386)
ENTRY(coop_detour)
SECTIONS {
  . = 0x96E000;
  .coop : {
    *(.text.detour)
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
    *(.data .data.*)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
  }
  .coopdbg 0x976000 : { *(.coopdbg) *(.coopdbg2) . = ALIGN(16); }
  ASSERT(ADDR(.coop) + SIZEOF(.coop) <= 0x976000, "code outgrew its 32 KB slot")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.got*) *(.plt*) }
}
