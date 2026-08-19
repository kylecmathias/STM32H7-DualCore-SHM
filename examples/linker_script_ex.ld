/* 
 * EXAMPLE SNIPPET: STM32H7 Dual-Core Shared Memory Linker Configuration
 * 
 * How to use:
 * Add RAM_D3 line in MEMORY.
 * Add .shm block in SECTIONS.
 * This must be done for BOTH the Cortex-M7 and Cortex-M4 linker scripts.
 */

MEMORY
{
  /* Existing memory */

  RAM_D3 (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
}

SECTIONS
{
  /* Existing sections */

  .shm (NOLOAD) : 
  {
    . = ALIGN(4);
    *(.shm);
    *(.shm*);
    . = ALIGN(4);
  } >RAM_D3
}
