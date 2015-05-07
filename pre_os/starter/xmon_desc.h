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
#ifndef __XMON_DESC_H
#define __XMON_DESC_H

/* xmon_pkg.bin file header */

typedef struct {
	uint32_t struct_size;
	uint32_t version;
	uint32_t size_in_sectors;
	uint32_t umbr_size;
	uint32_t xmon_mem_in_mb;
	uint32_t guest_count;
	uint32_t xmonl_start;
	uint32_t xmonl_count;
	uint32_t starter_start;
	uint32_t starter_count;
	uint32_t xmon_loader_start;
	uint32_t xmon_loader_count;
	uint32_t startap_start;
	uint32_t startap_count;
	uint32_t xmon_start;
	uint32_t xmon_count;
	uint32_t startup_start;
	uint32_t startup_count;
	uint32_t guest1_start;
	uint32_t guest1_count;
} xmon_desc_t;

/* Runtime memory map (Load time)
 * size of the following memory is hard-coded to 6MB in build script
 *
 *        Load time                        xmon up running
 * +----------------------+ 6MB       +----------------------+ 6MB
 * |                      |           |                      |
 * |                      |           | xmon heap            |
 * |                      |           |                      |
 * |                      |           +----------------------+
 * |                      |           | xmon stack(64KB/core)|                     |
 * |                      |           +----------------------+
 * |                      |           | xmon (~400 KB)       |
 * |                      |           +----------------------+
 * |                      |           | startap (12 KB)      |
 * +----------------------+           +----------------------+
 * | loader heap (512 KB) |           |                      |
 * +----------------------+           |                      |
 * | xmon loader (64 KB)  |           |                      |
 * +----------------------+           |                      |
 * | Guest states (4 KB)  |           |      Not reused      |
 * +----------------------+           |                      |
 * | xmon_pkg.bin(~294 KB)|           |                      |
 * +----------------------+ 0         +----------------------+ 0
 */

/* Loader memory map */

#define XMON_PKG_SIZE(td) ((td->guest1_start + td->guest1_count) * 512)
#define GUEST1_BASE(td) ((uint32_t)td + XMON_PKG_SIZE(td))
#define GUEST1_SIZE (0x400)
/* Enforcing 4KB alignment */
#define XMON_LOADER_BASE(td) ((GUEST1_BASE(td) + GUEST1_SIZE + 0xfff) & 0xfffff000)
#define XMON_LOADER_SIZE (0x10000)
#define XMON_LOADER_HEAP_BASE(td) (XMON_LOADER_BASE(td) + XMON_LOADER_SIZE)
#define XMON_LOADER_HEAP_SIZE (0xa0000)

/* xmon and startap memory map */
#define STARTAP_BASE(td) ((XMON_LOADER_HEAP_BASE(td) + XMON_LOADER_HEAP_SIZE))
#define STARTAP_SIZE (0x3000)
#define XMON_BASE(td) (STARTAP_BASE(td) + STARTAP_SIZE)
/*
 * not reuse the memory of loader heap, xmon loader, states and loader_bin
 * to make sure we can resume to lanuch linux kernel
 */
#define XMON_SIZE(td) (td->xmon_mem_in_mb * 0x100000 - \
		       (XMON_BASE(td) - (uint32_t)td))

/* End of file */
#endif
