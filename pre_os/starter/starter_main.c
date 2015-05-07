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

int run_xmon_loader(xmon_desc_t *td);

#define RETURN_ADDRESS() (__builtin_return_address(0))

uint32_t __readcs(void_t)
{
	__asm__ __volatile__ ("push %cs; pop %eax\n");
}

uint32_t __readds(void_t)
{
	__asm__ __volatile__ ("push %ds; pop %eax\n");
}

uint32_t __reades(void_t)
{
	__asm__ __volatile__ ("push %es; pop %eax\n");
}

uint32_t __readfs(void_t)
{
	__asm__ __volatile__ ("push %fs; pop %eax\n");
}

uint32_t __readgs(void_t)
{
	__asm__ __volatile__ ("push %gs; pop %eax\n");
}

uint32_t __readss(void_t)
{
	__asm__ __volatile__ ("push %ss; pop %eax\n");
}

uint16_t __readtr(void_t)
{
	__asm__ __volatile__ (
		"str %ax\n"
		);
}

void_t __readgdtr(ia32_gdtr_t *p)
{
	__asm__ __volatile__ ("sgdt (%0)" : : "D" (p) : );
}

uint16_t __readldtr(void_t)
{
	__asm__ __volatile__ (
		"sldt %ax\n"
		);
}

uint16_t __sidt(void *p)
{
	__asm__ __volatile__ ("sidt (%0)" : : "D" (p) : );
}

uint32_t __readcr0(void_t)
{
	__asm__ __volatile__ ("mov %cr0, %eax\n");
}

uint32_t __readcr2(void_t)
{
	__asm__ __volatile__ ("mov %cr2, %eax\n");
}

uint32_t __readcr3(void_t)
{
	__asm__ __volatile__ ("mov %cr3, %eax\n");
}

uint32_t __readcr4(void_t)
{
	__asm__ __volatile__ ("mov %cr4, %eax\n");
}

uint64_t __readmsr(uint32_t msr_id)
{
	__asm__ __volatile__ (
		"movl %0, %%ecx\n"
		"xor %%eax, %%eax\n"
		"rdmsr\n"
		:
		: "g" (msr_id)
		: "%ecx", "%eax", "%edx"
		);
}

void __cpuid(int cpu_info[4], int info_type);

static int validate_descriptor(ia32_segment_descriptor_t *d)
{
	if ((d->gen.hi.present == 0) || (d->gen.hi.mbz_21 == 1)) {
		return -1;
	}

	if (d->gen.hi.s == 0) {
		if ((d->tss.hi.mbo_8 == 0) ||
		    (d->tss.hi.mbz_10 == 1) ||
		    (d->tss.hi.mbo_11 == 0) ||
		    (d->tss.hi.mbz_12 == 1) ||
		    (d->tss.hi.mbz_21 == 1) || (d->tss.hi.mbz_22 == 1)) {
			return -1;
		}
	} else {
		if ((d->gen_attr.attributes & 0x0008) != 0) {
			if ((d->cs.hi.mbo_11 == 0) ||
			    (d->cs.hi.mbo_12 == 0) || (d->cs.hi.default_size == 0)) {
				return -1;
			}
		} else {
			if ((d->ds.hi.mbz_11 == 1) ||
			    (d->ds.hi.mbo_12 == 0) || (d->ds.hi.big == 0)) {
				return -1;
			}
		}
	}

	return 0;
}

static void save_segment_data(uint16_t sel16, mon_segment_struct_t *ss)
{
	ia32_gdtr_t gdtr;
	ia32_selector_t sel;
	ia32_segment_descriptor_t *desc;
	ia32_segment_descriptor_attr_t attr;
	unsigned int max;

	/* Check the limit, it will be 0 to
	 * (size_of_each_entry * num_entries) - 1. So max will have the
	 * last valid entry */
	__readgdtr(&gdtr);
	max = (uint32_t)gdtr.limit / sizeof(ia32_segment_descriptor_t);

	sel.sel16 = sel16;

	if ((sel.bits.index == 0) ||
	    ((sel.bits.index / sizeof(ia32_segment_descriptor_t)) > max) ||
	    (sel.bits.ti)) {
		return;
	}

	desc = (ia32_segment_descriptor_t *)
	       (gdtr.base + sizeof(ia32_segment_descriptor_t) * sel.bits.index);

	if (validate_descriptor(desc) != 0) {
		return;
	}

	ss->base = (uint64_t)((desc->gen.lo.base_address_15_00) |
			      (desc->gen.hi.base_address_23_16 << 16) |
			      (desc->gen.hi.base_address_31_24 << 24)
			      );

	ss->limit = (uint32_t)((desc->gen.lo.limit_15_00) |
			       (desc->gen.hi.limit_19_16 << 16)
			       );

	if (desc->gen.hi.granularity) {
		ss->limit = (ss->limit << 12) | 0x00000fff;
	}

	attr.attr16 = desc->gen_attr.attributes;
	attr.bits.limit_19_16 = 0;

	ss->attributes = (uint32_t)attr.attr16;
	ss->selector = sel.sel16;
	return;
}

void save_cpu_state(mon_guest_cpu_startup_state_t *s)
{
	ia32_gdtr_t gdtr;
	ia32_idtr_t idtr;
	ia32_selector_t sel;
	ia32_segment_descriptor_t *desc;

	s->size_of_this_struct = sizeof(mon_guest_cpu_startup_state_t);
	s->version_of_this_struct = MON_GUEST_CPU_STARTUP_STATE_VERSION;

	__readgdtr(&gdtr);
	__sidt(&idtr);
	s->control.gdtr.base = (uint64_t)gdtr.base;
	s->control.gdtr.limit = (uint32_t)gdtr.limit;
	s->control.idtr.base = (uint64_t)idtr.base;
	s->control.idtr.limit = (uint32_t)idtr.limit;
	s->control.cr[IA32_CTRL_CR0] = __readcr0();
	s->control.cr[IA32_CTRL_CR2] = __readcr2();
	s->control.cr[IA32_CTRL_CR3] = __readcr3();
	s->control.cr[IA32_CTRL_CR4] = __readcr4();

	s->msr.msr_sysenter_cs = (uint32_t)__readmsr(IA32_MSR_SYSENTER_CS);
	s->msr.msr_sysenter_eip = __readmsr(IA32_MSR_SYSENTER_EIP);
	s->msr.msr_sysenter_esp = __readmsr(IA32_MSR_SYSENTER_ESP);
	s->msr.msr_efer = __readmsr(IA32_MSR_EFER);
	s->msr.msr_pat = __readmsr(IA32_MSR_PAT);
	s->msr.msr_debugctl = __readmsr(IA32_MSR_DEBUGCTL);
	s->msr.pending_exceptions = 0;
	s->msr.interruptibility_state = 0;
	s->msr.activity_state = 0;
	s->msr.smbase = 0;

	sel.sel16 = __readldtr();

	if (sel.bits.index != 0) {
		return;
	}

	s->seg.segment[IA32_SEG_LDTR].attributes = 0x00010000;
	s->seg.segment[IA32_SEG_TR].attributes = 0x0000808b;
	s->seg.segment[IA32_SEG_TR].limit = 0xffffffff;
	save_segment_data((uint16_t)__readcs(), &s->seg.segment[IA32_SEG_CS]);
	save_segment_data((uint16_t)__readds(), &s->seg.segment[IA32_SEG_DS]);
	save_segment_data((uint16_t)__reades(), &s->seg.segment[IA32_SEG_ES]);
	save_segment_data((uint16_t)__readfs(), &s->seg.segment[IA32_SEG_FS]);
	save_segment_data((uint16_t)__readgs(), &s->seg.segment[IA32_SEG_GS]);
	save_segment_data((uint16_t)__readss(), &s->seg.segment[IA32_SEG_SS]);
	return;
}


int check_vmx_support(void)
{
	int info[4];
	uint64_t u;

	/* CPUID: input in eax = 1. */

	__cpuid(info, 1);

	/* CPUID: output in ecx, VT available? */

	if ((info[2] & 0x00000020) == 0) {
		return -1;
	}

	/* Fail if feature is locked and vmx is off. */

	u = __readmsr(IA32_MSR_FEATURE_CONTROL);

	if (((u & 0x01) != 0) && ((u & 0x04) == 0)) {
		return -1;
	}

	return 0;
}

/* Function: starter_main
 * Description: Called by start() in starter.S. Jumps to xmon_loader - xmon loader.
 *              This function never returns back.
 * Input: Registers pushed right to left:
 *        eip0 - return address on stack,
 *        pushal - eax, ecx, edx, ebx, esp, ebp, esi, edi
 *        pushfl - flags
 */
void starter_main(uint32_t eflags,
		  uint32_t edi,
		  uint32_t esi,
		  uint32_t ebp,
		  uint32_t esp,
		  uint32_t ebx,
		  uint32_t edx,
		  uint32_t ecx,
		  uint32_t eax,
		  uint32_t eip0)
{
	uint32_t eip1;
	xmon_desc_t *td;
	mon_guest_cpu_startup_state_t *s;

	eip1 = (uint32_t)RETURN_ADDRESS();
	td = (xmon_desc_t *)((eip1 & 0xffffff00) - 0x400);

	mon_memset((void *)GUEST1_BASE(td),
		0, XMON_LOADER_BASE(td) - GUEST1_BASE(td)
		);

	s = (mon_guest_cpu_startup_state_t *)GUEST1_BASE(td);
	s->gp.reg[IA32_REG_RIP] = eip0;
	s->gp.reg[IA32_REG_RFLAGS] = eflags;
	s->gp.reg[IA32_REG_RAX] = eax;
	s->gp.reg[IA32_REG_RCX] = ecx;
	s->gp.reg[IA32_REG_RDX] = edx;
	s->gp.reg[IA32_REG_RBX] = ebx;
	s->gp.reg[IA32_REG_RSP] = esp + 4;
	s->gp.reg[IA32_REG_RBP] = ebp;
	s->gp.reg[IA32_REG_RSI] = esi;
	s->gp.reg[IA32_REG_RDI] = edi;


	save_cpu_state(s);

	if (check_vmx_support() != 0) {
		goto error;
	}

	run_xmon_loader(td);

error:

	/* clean memory */

	mon_memset((void *)((uint32_t)td + td->xmon_loader_start * 512),
		0, XMON_LOADER_HEAP_BASE(td) + XMON_LOADER_HEAP_SIZE -
		(td->xmon_loader_start) * 512);

	while (1) {
	}
}

/* End of file */
