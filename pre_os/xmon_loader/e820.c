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

#include "mon_defs.h"
#include "mon_arch_defs.h"
#include "mon_startup.h"
#include "xmon_desc.h"
#include "common.h"
#include "multiboot1.h"
#include "screen.h"
#include "memory.h"



typedef struct {
	uint32_t type;
	uint64_t paddr;
	uint64_t vaddr;
	uint64_t pages;
	uint64_t attr;
} sfi_mem_dsc_t;

typedef struct {
	uint32_t sig;
	uint32_t len;
	uint8_t rev;
	uint8_t cksum;
	uint8_t oem_id[6];
	uint64_t oem_tab_id;
} sfi_header_t;

void *CDECL mon_page_alloc(uint32_t pages);

int get_e820_table_from_multiboot(xmon_desc_t *td, uint64_t *e820_addr)
{
	mon_guest_cpu_startup_state_t *s;
	int15_e820_memory_map_t *e820;
	uint32_t start;
	uint32_t next;
	uint32_t end;
	multiboot_info_t *inf;
	int i;

	s = (mon_guest_cpu_startup_state_t *)GUEST1_BASE(td);
	inf = (multiboot_info_t *)((uint32_t)(s->gp.reg[IA32_REG_RBX]));

	if (((inf->flags & 0x00000003) == 0) || (inf->mmap_length > 4096)) {
		return -1;
	}

	e820 = (int15_e820_memory_map_t *)mon_page_alloc(1);

	if (e820 == NULL) {
		return -1;
	}

	start = inf->mmap_addr;
	end = inf->mmap_addr + inf->mmap_length;
	i = 0;

	for (next = start; next < end;
	     next += ((multiboot_memory_map_t *)next)->size + 4) {
		multiboot_memory_map_t *map = (multiboot_memory_map_t *)next;
		e820->memory_map_entry[i].basic_entry.base_address = map->addr;
		e820->memory_map_entry[i].basic_entry.length = map->len;
		e820->memory_map_entry[i].basic_entry.address_range_type = map->type;
		e820->memory_map_entry[i].extended_attributes.uint32 = 1;
		i++;
	}

	e820->memory_map_size = i * sizeof(int15_e820_memory_map_entry_ext_t);
	*e820_addr = (uint64_t)(uint32_t)e820;
	return 0;
}

int copy_e820_table_from_efi(xmon_desc_t *td, uint64_t *e820_addr)
{
	mon_guest_cpu_startup_state_t *s;
	int15_e820_memory_map_t *e820;
	void *inf;

	s = (mon_guest_cpu_startup_state_t *)GUEST1_BASE(td);
	inf = (void *)((uint32_t)(s->gp.reg[IA32_REG_RBX]));

	e820 = (int15_e820_memory_map_t *)mon_page_alloc(1);

	if (e820 == NULL) {
		return -1;
	}

	mon_memcpy(e820, inf, 4096);
	*e820_addr = (uint64_t)(uint32_t)e820;

	return 0;
}

/* Find sfi table between 'start' and 'end' with signature 'sig.' */
/* Searching is done for every 'step' bytes. */

static int find_sfi_table(uint32_t start, uint32_t end, uint32_t step,
			  uint32_t table_level, uint32_t sig, uint32_t *table,
			  uint32_t *size)
{
	uint32_t addr;

	for (addr = start; addr <= end; addr += step) {
		sfi_header_t *hdr;

		hdr = (table_level == 0) ?
		      (sfi_header_t *)addr : *((sfi_header_t **)addr);

		if ((hdr != 0) && (hdr->sig == sig)) {
			*table = (uint32_t)(hdr + 1);
			*size = (hdr->len - sizeof(*hdr));
			return 0;
		}
	}

	return -1;
}

static int15_e820_range_type_t sfi_e820_type(uint32_t type)
{
	switch (type) {
	case 1:                 /* EfiLoaderCode: */
	case 2:                 /* EfiLoaderData: */
	case 3:                 /* EfiBootServicesCode: */
	case 4:                 /* EfiBootServicesData: */
	case 5:                 /* EfiRuntimeServicesCode: */
	case 6:                 /* EfiRuntimeServicesData: */
	case 7:                 /* EfiConventionalMemory: */
		return INT15_E820_ADDRESS_RANGE_TYPE_MEMORY;

	case 9:                /* EfiACPIReclaimMemory: */
		return INT15_E820_ADDRESS_RANGE_TYPE_ACPI;

	case 10:               /* EfiACPIMemoryNVS: */
		return INT15_E820_ADDRESS_RANGE_TYPE_NVS;

	default:
		return INT15_E820_ADDRESS_RANGE_TYPE_RESERVED;
	}
}

int get_e820_table_from_sfi(xmon_desc_t *td, uint64_t *e820_addr)
{
#define SYST 0x54535953
#define MMAP 0x50414d4d

	int15_e820_memory_map_t *e820;
	int15_e820_memory_map_entry_ext_t *blk;

	uint32_t table;
	uint32_t size;
	sfi_mem_dsc_t *dsc;
	int cnt;
	int r;
	int i;

	r = find_sfi_table(0xe0000, 0xfffff, 16, 0, SYST, &table, &size);

	if (r != 0) {
		return -1;
	}

	r = find_sfi_table(table, table + size - 1, 8, 1, MMAP, &table, &size);

	if (r != 0) {
		return -1;
	}

	dsc = (sfi_mem_dsc_t *)table;
	cnt = size / sizeof(sfi_mem_dsc_t);

	e820 = (int15_e820_memory_map_t *)mon_page_alloc(1);

	if (e820 == NULL) {
		return -1;
	}

	blk = e820->memory_map_entry;

	for (i = 0; i < cnt; i++) {
		blk[i].basic_entry.base_address = dsc[i].paddr;
		blk[i].basic_entry.length = dsc[i].pages * 0x1000;
		blk[i].basic_entry.address_range_type = sfi_e820_type(dsc[i].type);
		blk[i].extended_attributes.bits.enabled = 1;
	}

	e820->memory_map_size = cnt * sizeof(*blk);
	*e820_addr = (uint64_t)(uint32_t)e820;
	return 0;
}

/*
 * copy e820 memory info to other address, and hide some memories in e820 table.
 */
boolean_t hide_runtime_memory(multiboot_info_t *mbi,
			      uint32_t hide_mem_addr,
			      uint32_t hide_mem_size)
{
	uint32_t num_of_entries, entry_idx;
	multiboot_memory_map_t *newmmap_addr;

	/* Are mmap_* valid? */
	if (!(mbi->flags & MBI_MEMMAP)) {
		return FALSE;
	}

	multiboot_memory_map_t *mmap;

	/* add space for two more entries for boundary case. */
	num_of_entries = mbi->mmap_length / sizeof(multiboot_memory_map_t) + 2;
	newmmap_addr = (multiboot_memory_map_t *)
		       allocate_memory(
		sizeof(multiboot_memory_map_t) * num_of_entries);
	if (!newmmap_addr) {
		return FALSE;
	}


	for (entry_idx = 0, mmap = (multiboot_memory_map_t *)mbi->mmap_addr;
	     (unsigned long)mmap < mbi->mmap_addr + mbi->mmap_length;
	     entry_idx++, mmap = (multiboot_memory_map_t *)
				 ((unsigned long)mmap + mmap->size +
				  sizeof(mmap->size))) {
		if (((mmap->addr + mmap->len) <= hide_mem_addr) ||
		    ((hide_mem_addr + hide_mem_size) <= mmap->addr)) {
			/* do not modify it */
			mon_memcpy(&newmmap_addr[entry_idx], mmap,
				sizeof(multiboot_memory_map_t));
		} else {
			/* input address range to be hidden needs to be of type AVAILABLE. */
			if (mmap->type != MULTIBOOT_MEMORY_AVAILABLE) {
				print_string(
					"ERROR: the type of memory to hide is not AVAILABLE in e820 table!!\n");
				return FALSE;
			}

			newmmap_addr[entry_idx].size = mmap->size;
			newmmap_addr[entry_idx].addr = mmap->addr;
			newmmap_addr[entry_idx].len = hide_mem_addr - mmap->addr;
			newmmap_addr[entry_idx].type = mmap->type;

			entry_idx++;

			newmmap_addr[entry_idx].size = mmap->size;
			newmmap_addr[entry_idx].addr = hide_mem_addr;
			newmmap_addr[entry_idx].len = hide_mem_size;
			newmmap_addr[entry_idx].type = MULTIBOOT_MEMORY_RESERVED;

			if ((hide_mem_addr + hide_mem_size) >
			    (mmap->addr + mmap->len)) {
				print_string(
					"ERROR: hide_mem_addr+hide_mem_size crossing two E820 entries!!\n");
				return FALSE;
			}

			if ((hide_mem_addr + hide_mem_size) <
			    (mmap->addr + mmap->len)) {
				/* need one more entry */
				entry_idx++;
				newmmap_addr[entry_idx].size = mmap->size;
				newmmap_addr[entry_idx].addr = hide_mem_addr +
							       hide_mem_size;
				newmmap_addr[entry_idx].len =
					(mmap->addr +
				 mmap->len) - (hide_mem_addr + hide_mem_size);
				newmmap_addr[entry_idx].type = mmap->type;
			} else {
				/* no need one more entry */
			}
		}
	}

	/* update map addr and len (entry_idx, using the exact entry num value) */
	mbi->mmap_addr = (uint32_t)newmmap_addr;
	mbi->mmap_length = sizeof(multiboot_memory_map_t) * entry_idx; /* do not use num_of_entries*/

	return TRUE;
}

/* End of file */
