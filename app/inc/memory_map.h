#include <stdlib.h>

#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

extern uint8_t _pvHeapStart[];
extern uint8_t _pvHeapEnd[];
extern uint8_t _vStackTop[];
extern uint8_t __top_SRAM[];
extern uint8_t __top_SRAM0[];

/* Simboli che rappresentano VALORI (dimensioni calcolate dal linker) */
extern uint32_t __stack_size;  

extern HeapRegion_t xHeapRegions[];
#endif