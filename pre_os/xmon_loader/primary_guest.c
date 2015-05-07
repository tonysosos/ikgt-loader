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
#include "msr_defs.h"
#include "ia32_defs.h"
#include "mon_arch_defs.h"
#include "mon_startup.h"
#include "xmon_desc.h"
#include "common.h"
#include "screen.h"
#include "multiboot1.h"
#include "memory.h"
#include "linux_loader.h"


extern void primary_guest_entry(void);
extern boolean_t hide_runtime_memory(multiboot_info_t *mbi,
				     uint32_t hide_mem_addr,
				     uint32_t hide_mem_size);



/*
 * At this point, the xmon is running now.
 */
void load_primary_guest_kernel(xmon_desc_t *td)
{
	multiboot_info_t *mbi;
	mon_guest_cpu_startup_state_t *s;

	s = (mon_guest_cpu_startup_state_t *)GUEST1_BASE(td);
	mbi = (multiboot_info_t *)((uint32_t)(s->gp.reg[IA32_REG_RBX]));

	print_string("LOADER: prepare to load primary os kernel!\n");

	/* hide xmon/startap runtime memories*/
	hide_runtime_memory(mbi, STARTAP_BASE(td), STARTAP_SIZE + XMON_SIZE(td));

	/* by default, load guest linux kernel for primary guest */
	launch_linux_kernel(mbi);

	while (1) {
	}
}


/*
 * setup primary guest initial environment,
 * here only assign return-RIP and a parameter in RCX, all the other
 * states are currently setup in starter_main() function in starter component.
 *
 */
mon_guest_startup_t *setup_primary_guest_env(xmon_desc_t *td)
{
	mon_guest_cpu_startup_state_t *s;

	s = (mon_guest_cpu_startup_state_t *)GUEST1_BASE(td);

	/*
	 * here control the instruction returned after xmon starts
	 */
	s->gp.reg[IA32_REG_RIP] = (uint64_t)((uint32_t)&primary_guest_entry);

	/* use ecx to pass on xmon_desc_t *td */
	s->gp.reg[IA32_REG_RCX] = (uint64_t)((uint32_t)td);

	return NULL;
}
