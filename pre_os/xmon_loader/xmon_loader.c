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

#include "common_types.h"
#include "mon_defs.h"
#include "mon_arch_defs.h"
#include "mon_startup.h"
#include "startap.h"
#include "loader.h"
#include "image_loader.h"
#include "memory.h"
#include "x32_gdt64.h"
#include "x32_pt64.h"
#include "x32_init64.h"
#include "xmon_desc.h"
#include "common.h"

#define get_e820_table get_e820_table_from_multiboot

void __cpuid(int cpu_info[4], int info_type);
void setup_idt(void);
int get_e820_table_from_multiboot(xmon_desc_t *td, uint64_t *e820_addr);
extern mon_guest_startup_t *setup_primary_guest_env(xmon_desc_t *td);

static mon_startup_struct_t
*setup_env(xmon_desc_t *td,
	   image_info_t *startap,
	   image_info_t *xmon,
	   uint64_t call_startap)
{
	static ALIGN8(mon_startup_struct_t, env);
	static ALIGN8(mon_guest_startup_t, g0);
	mon_memory_layout_t *vmem;

	mon_memcpy(&g0, (void *)((uint32_t)td + td->guest1_start * 512), sizeof(g0));

	g0.cpu_states_array = GUEST1_BASE(td);
	g0.cpu_states_count = 1;
	g0.devices_array = 0;

	mon_memcpy((void *)&env,
		(void *)((uint32_t)td + td->startup_start * 512), sizeof(env));

	env.primary_guest_startup_state = (uint64_t)(uint32_t)&g0;

	vmem = env.mon_memory_layout;
	vmem[thunk_image].base_address = STARTAP_BASE(td);
	vmem[thunk_image].total_size = STARTAP_SIZE;
	vmem[thunk_image].image_size = MON_PAGE_ALIGN_4K(startap->load_size);
	/* The entry point of startap will be used when S3 resume. */
	vmem[thunk_image].entry_point = call_startap;
	vmem[mon_image].base_address = XMON_BASE(td);
	vmem[mon_image].total_size = XMON_SIZE(td);
	vmem[mon_image].image_size = MON_PAGE_ALIGN_4K(xmon->load_size);
	/* The entry point of mon_image is not set. Currently it works fine.*/

	return &env;
}

void xmon_loader(xmon_desc_t *td)
{
	static init64_struct_t init64;
	static init32_struct_t init32;

	image_info_status_t image_info_status;
	image_info_t startap_hdr;
	image_info_t xmon_hdr;

	mon_startup_struct_t *mon_env;
	startap_image_entry_point_t call_startap_entry;
	uint64_t call_startap;
	uint64_t call_xmon;

	uint32_t heap_base;
	uint32_t heap_size;

	uint64_t e820_addr;
	void *p_xmon = NULL;
	void *p_startap = NULL;
	void *p_low_mem = (void *)0x8000; /* find 20 KB below 640 K */

	int info[4] = { 0, 0, 0, 0 };
	int num_of_aps;
	boolean_t ok;
	int r;
	int i;

	/* Init loader heap, run-time space, and idt. */
	heap_base = XMON_LOADER_HEAP_BASE(td);
	heap_size = XMON_LOADER_HEAP_SIZE;

	initialize_memory_manager((uint64_t *)&heap_base, (uint64_t *)&heap_size);
	setup_idt();

	if (get_e820_table(td, &e820_addr) != 0) {
		return;
	}

	p_xmon = (void *)((uint32_t)td + td->xmon_start * 512);
	image_info_status = get_image_info(p_xmon, XMON_SIZE(td), &xmon_hdr);
	if ((image_info_status != IMAGE_INFO_OK) ||
	    (xmon_hdr.machine_type != IMAGE_MACHINE_EM64T) ||
	    (xmon_hdr.load_size == 0)) {
		return;
	}

	/* Load xmon image */
	ok = load_image(p_xmon, (void *)XMON_BASE(td), XMON_SIZE(td), &call_xmon);

	if (!ok) {
		return;
	}

	/* Load startap image */
	p_startap = (void *)((uint32_t)td + td->startap_start * 512);

	image_info_status = get_image_info((void *)p_startap,
		STARTAP_SIZE, &startap_hdr);

	if ((image_info_status != IMAGE_INFO_OK) ||
	    (startap_hdr.machine_type != IMAGE_MACHINE_X86) ||
	    (startap_hdr.load_size == 0)) {
		return;
	}

	ok = load_image((void *)p_startap,
		(void *)STARTAP_BASE(td),
		STARTAP_SIZE, (uint64_t *)&call_startap);

	if (!ok) {
		return;
	}

	/* setup primary guest initial environment so that after xmon launch,
	 *  the CPU control can be back to where we specified.
	 */
	setup_primary_guest_env(td);

	mon_env = setup_env(td, &startap_hdr, &xmon_hdr, call_startap);
	mon_env->physical_memory_layout_E820 = e820_addr;

	/* Setup init32. */
	/* It only gets the max. number of logical cores, not the real number.
	 * But it will be OK here since it only waste some memory (2 pages for
	 * each core) and the memory will be abandoned after MON lunch and MON
	 * will get the real number using SIPI.
	 */
	__cpuid(info, 1);
	num_of_aps = ((info[1] >> 16) & 0xff) - 1;

	if (num_of_aps < 0) {
		num_of_aps = 0;
	}

	init32.i32_low_memory_page = (uint32_t)p_low_mem;
	init32.i32_num_of_aps = num_of_aps;

	for (i = 0; i < num_of_aps; i++) {
		uint8_t *buf = mon_page_alloc(2);

		if (buf == NULL) {
			return;
		}

		init32.i32_esp[i] = (uint32_t)&buf[PAGE_4KB_SIZE * 2];
	}

	/* Setup init64. */
	x32_gdt64_setup();
	x32_gdt64_get_gdtr(&init64.i64_gdtr);
	x32_pt64_setup_paging(((uint64_t)1024 * 4) * 0x100000);
	init64.i64_cr3 = x32_pt64_get_cr3();
	init64.i64_cs = x32_gdt64_get_cs();
	init64.i64_efer = 0;

	call_startap_entry = (startap_image_entry_point_t)((uint32_t)call_startap);
	call_startap_entry((num_of_aps != 0) ? &init32 : 0,
		&init64, mon_env, (uint32_t)call_xmon);

	while (1) {
	}
}

/* End of file */
