/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/
#include <mon_defs.h>
#include <xmon_loader.h>
#include <memory.h>
#include <screen.h>

uint32_t heap_base;
uint32_t heap_current;
uint32_t heap_tops;

void_t zero_mem(void_t *address, uint32_t size)
{
	uint8_t *source;

	source = (uint8_t *)address;
	while (size--)
		*source++ = 0;
}

/*
 * allocate_memory(): Simple memory allocation routine */
void_t *allocate_memory(uint32_t size_request)
{
	uint32_t address;

	if (heap_current + size_request > heap_tops) {
		PRINT_STRING("Allocation request exceeds heap's size\r\n");
		PRINT_STRING_AND_VALUE("Heap current = 0x", heap_current);
		PRINT_STRING_AND_VALUE("Requested size = 0x", size_request);
		PRINT_STRING_AND_VALUE("Heap tops = 0x", heap_tops);

		return NULL;
	}
	address = heap_current;
	heap_current += size_request;
	zero_mem((void_t *)address, size_request);
	return (void_t *)address;
}

/* print_e820_bios_memory_map(): Routine to print the E820 BIOS memory map */
void_t print_e820_bios_memory_map(void_t)
{
}

void_t initialize_memory_manager(uint64_t *heap_base_address, uint64_t *heap_bytes)
{
	heap_current = heap_base = *(uint32_t *)heap_base_address;
	heap_tops = heap_base + *(uint32_t *)heap_bytes;
}

void_t copy_mem(void_t *dest, void_t *source, uint32_t size)
{
	uint8_t *d = (uint8_t *)dest;
	uint8_t *s = (uint8_t *)source;

	while (size--)
		*d++ = *s++;
}

boolean_t compare_mem(void_t *source1, void_t *source2, uint32_t size)
{
	uint8_t *s1 = (uint8_t *)source1;
	uint8_t *s2 = (uint8_t *)source2;

	while (size--) {
		if (*s1++ != *s2++) {
			PRINT_STRING("Compare mem failed\n");
			return FALSE;
		}
	}
	return TRUE;
}

#define PAGE_SIZE (1024 * 4)

void *CDECL mon_page_alloc(uint32_t pages)
{
	uint32_t address;
	uint32_t size = pages * PAGE_SIZE;

	address = ALIGN_FORWARD(heap_current, PAGE_SIZE);

	if (address + size > heap_tops) {
		PRINT_STRING("Allocation request exceeds heap's size\r\n");
		PRINT_STRING_AND_VALUE("Page aligned heap current = 0x", address);
		PRINT_STRING_AND_VALUE("Requested size = 0x", size);
		PRINT_STRING_AND_VALUE("Heap tops = 0x", heap_tops);
		return NULL;
	}

	heap_current = address + size;
	zero_mem((void *)address, size);
	return (void *)address;
}

void __cpuid(int cpu_info[4], int info_type)
{
	__asm__ __volatile__ (
		"pushl %%ebx      \n\t" /* save %ebx */
		"cpuid            \n\t"
		"movl %%ebx, %1   \n\t" /* save what cpuid just put in %ebx */
		"popl %%ebx       \n\t" /* restore the old %ebx */
		: "=a" (cpu_info[0]),
		"=r" (cpu_info[1]),
		"=c" (cpu_info[2]),
		"=d" (cpu_info[3])
		: "a" (info_type)
		: "cc"
		);
}
