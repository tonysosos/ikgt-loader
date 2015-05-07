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

#ifndef __MULTIBOOT_H__
#define __MULTIBOOT_H__

/* Multiboot header Definitions of OS image*/
#define MULTIBOOT_HEADER_MAGIC          0x1BADB002
#define MULTIBOOT_HEADER_SEARCH_LIMIT   8192

/* Bit definitions of flags field of multiboot header*/
#define MULTIBOOT_HEADER_MODS_ALIGNED   0x1
#define MULTIBOOT_HEADER_WANT_MEMORY    0x2

/* bit definitions of flags field of multiboot information */
#define MBI_MEMLIMITS    (1 << 0)
#define MBI_BOOTDEV      (1 << 1)
#define MBI_CMDLINE      (1 << 2)
#define MBI_MODULES      (1 << 3)
#define MBI_AOUT         (1 << 4)
#define MBI_ELF          (1 << 5)
#define MBI_MEMMAP       (1 << 6)
#define MBI_DRIVES       (1 << 7)
#define MBI_CONFIG       (1 << 8)
#define MBI_BTLDNAME     (1 << 9)
#define MBI_APM          (1 << 10)
#define MBI_VBE          (1 << 11)


#ifndef ASM_FILE  /* the followings are used in C files */

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

/* MB1 */
typedef struct {
	uint32_t tabsize;
	uint32_t strsize;
	uint32_t addr;
	uint32_t reserved;
} aout_t; /* a.out kernel image */

typedef struct {
	uint32_t num;
	uint32_t size;
	uint32_t addr;
	uint32_t shndx;
} elf_t; /* elf kernel */

typedef struct {
	uint8_t bios_driver;
	uint8_t top_level_partition;
	uint8_t sub_partition;
	uint8_t third_partition;
} boot_device_t;

typedef struct {
	uint32_t flags;

	/* valid if flags[0] (MBI_MEMLIMITS) set */
	uint32_t mem_lower;
	uint32_t mem_upper;

	/* valid if flags[1] set */
	boot_device_t boot_device;

	/* valid if flags[2] (MBI_CMDLINE) set */
	uint32_t cmdline;

	/* valid if flags[3] (MBI_MODS) set */
	uint32_t mods_count;
	uint32_t mods_addr;

	/* valid if flags[4] or flags[5] set */
	union {
		aout_t aout_image;
		elf_t elf_image;
	} syms;

	/* valid if flags[6] (MBI_MEMMAP) set */
	uint32_t mmap_length;
	uint32_t mmap_addr;

	/* valid if flags[7] set */
	uint32_t drives_length;
	uint32_t drives_addr;

	/* valid if flags[8] set */
	uint32_t config_table;

	/* valid if flags[9] set */
	uint32_t boot_loader_name;

	/* valid if flags[10] set */
	uint32_t apm_table;

	/* valid if flags[11] set */
	uint32_t vbe_control_info;
	uint32_t vbe_mode_info;
	uint16_t vbe_mode;
	uint16_t vbe_interface_seg;
	uint16_t vbe_interface_off;
	uint16_t vbe_interface_len;
} multiboot_info_t;

typedef struct {
	uint32_t mod_start;
	uint32_t mod_end;
	uint32_t cmdline;
	uint32_t pad;
} multiboot_module_t;

typedef struct {
	uint32_t size;
	uint64_t addr;
	uint64_t len;
#define MULTIBOOT_MEMORY_AVAILABLE              1
#define MULTIBOOT_MEMORY_RESERVED               2
	uint32_t type;
} multiboot_memory_map_t;


#endif  /* ! ASM_FILE */

#endif  /* ! MULTIBOOT_HEADER */
