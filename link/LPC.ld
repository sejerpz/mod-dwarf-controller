
INCLUDE "./link/LPCmem.ld"

ENTRY(ResetISR)

SECTIONS
{
    /* MAIN TEXT SECTION */    
    .text : ALIGN(4)
    {
        FILL(0xff)
        __vectors_start__ = ABSOLUTE(.) ;
        KEEP(*(.isr_vector))
        
        /* Global Section Table */
        . = ALIGN(4) ;
        __section_table_start = .;
        __data_section_table = .;
        LONG(LOADADDR(.data));
        LONG(    ADDR(.data));
        LONG(  SIZEOF(.data));
        LONG(LOADADDR(.data_RAM2));
        LONG(    ADDR(.data_RAM2));
        LONG(  SIZEOF(.data_RAM2));
        __data_section_table_end = .;
        __bss_section_table = .;
        LONG(    ADDR(.bss));
        LONG(  SIZEOF(.bss));
        LONG(    ADDR(.bss_RAM2));
        LONG(  SIZEOF(.bss_RAM2));
        __bss_section_table_end = .;
        __section_table_end = . ;
        /* End of Global Section Table */
        

        *(.after_vectors*)
    } >FLASH
    
    .text : ALIGN(4)    
    {
         *(.text*)
        *(.rodata .rodata.* .constdata .constdata.*)
        . = ALIGN(4);
        
    } > FLASH

    /*
     * for exception handling/unwind - some Newlib functions (in common
     * with C++ and STDC++) use this. 
     */
    .ARM.extab : ALIGN(4)
    {
    	*(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH
    __exidx_start = .;
    
    .ARM.exidx : ALIGN(4)
    {
    	*(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;
    
    _etext = .;
    
    /* DATA section for SRAM0 */
    .data_RAM2 : ALIGN(4)
    {
       FILL(0xff)
       *(.ramfunc.$RAM2)
       *(.ramfunc.$SRAM0)
    	*(.data.$RAM2*)
    	*(.data.$SRAM0*)
       . = ALIGN(4) ;
    } > SRAM0 AT>FLASH
    
    /* MAIN DATA SECTION */
    .uninit_RESERVED : ALIGN(4)
    {
        KEEP(*(.bss.$RESERVED*))
        . = ALIGN(4) ;
        _end_uninit_RESERVED = .;
    } > SRAM
	
	/* Main DATA section (SRAM) */
	.data : ALIGN(4)
	{
	   FILL(0xff)
	   _data = . ;
	   *(vtable)
	   *(.ramfunc*)
	   *(.data*)
	   . = ALIGN(4) ;
	   _edata = . ;
	} > SRAM AT>FLASH

    /* BSS section for SRAM0 */
    .bss_RAM2 : ALIGN(4)
    {
    	*(.bss.$RAM2*)
    	*(.bss.$SRAM0*)
       . = ALIGN(4) ;
    } > SRAM0

    /* MAIN BSS SECTION */
    .bss : ALIGN(4)
    {
        _bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4) ;
        _ebss = .;
        PROVIDE(end = .);
    } > SRAM
        
    /* NOINIT section for SRAM0 */
    .noinit_RAM2 (NOLOAD) : ALIGN(4)
    {
    	*(.noinit.$RAM2*)
    	*(.noinit.$SRAM0*)
       . = ALIGN(4) ;
    } > SRAM0 

    /* DEFAULT NOINIT SECTION */
    .noinit (NOLOAD): ALIGN(4)
    {
        _noinit = .;
        *(.noinit*) 
         . = ALIGN(4) ;
        _end_noinit = .;
    } > SRAM
    
    /* * HEAP AND STACK CONFIGURATION
     */

    PROVIDE(_vStackTop = __top_SRAM - 0);
    PROVIDE(__stack_size = 8K);

    /* Force 16-byte alignment for the start of the heap to prevent Usage Faults */
    . = ALIGN(8);
    PROVIDE(_pvHeapStart = .);

    /* Calculate the top of SRAM0 (0x10010000) */
    __top_SRAM = ORIGIN(SRAM) + LENGTH(SRAM);

    /* * Define the end of the heap by subtracting the stack size from the top of RAM.
     * This ensures the stack has its reserved space at the very end of RAM.
     */
    PROVIDE(_pvHeapEnd = __top_SRAM - __stack_size);

    /* * Safety check: Ensure that BSS + NoInit + Stack do not exceed available RAM.
     * If this fails, the linker will throw an error during compilation.
     */
    ASSERT(_pvHeapStart < _pvHeapEnd, "Region SRAM overflowed: Not enough space for Heap and Stack")
}
