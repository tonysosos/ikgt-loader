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

/* Perform application processors init */

#include "mon_defs.h"
#include "common.h"
#include "mon_startup.h"
#include "ia32_defs.h"
#include "x32_init64.h"
#include "em64t_defs.h"
#include "ap_procs_init.h"
#include "gdt.h"

/*************************************************************************
 * AP startup algorithm
 * -------- Stage 1 ----------
 * BSP:
 * 1. Copy ap_start_up_code + GDT to low memory page
 * 2. Clear APs counter
 * 3. Send SIPI to all processors excluding self
 * 4. Wait timeout
 * APs on SIPI receive:
 * 1. Switch to protected mode
 * 2. lock inc APs counter + remember my AP number
 * 3. Loop on wait_lock1 until it changes zero
 * -------- Stage 2 ----------
 * BSP after timeout:
 * 5. Read number of APs and allocate memory for stacks
 * 6. Save GDT and IDT in global array
 * 7. Clear ready_counter count
 * 8. Set wait_lock1 to 1
 * 9. Loop on ready_counter until it will be equal to number of APs
 * APs on wait_1_lock set
 * 4. Set stack in a right way
 * 5. Set right GDT and IDT
 * 6. Enter "C" code
 * 7. Increment ready_counter
 * 8. Loop on wait_lock2 until it changes from zero
 * -------- Stage 3 ----------
 * BSP after ready_counter becomes == APs number
 * 10. Return to user
 * PROBLEM:
 * NMI may crash the system in it comes before AP stack init done
 ***************************************************************************/

#define IA32_DEBUG_IO_PORT   0x80
#define INITIAL_WAIT_FOR_APS_TIMEOUT_IN_MILIS 150000

/*
 * If see errors when compiling, need to check
 * whether the condition is satisfied.
 */
#define COMPILE_TIME_ASSERT(condition) \
	{ \
		switch (0) { case 0: case (condition):; } \
	}

/*---------------------------------------------------------------------------
 * rdtsc
 * Read 64-bit TimeStamp Counter
 *---------------------------------------------------------------------------*/
inline uint64_t __rdtsc(void)
{
	uint64_t ret;
	__asm__ __volatile__ ("rdtsc" : "=A" (ret));

	return ret;
}

#define startap_rdtsc() __rdtsc()
static uint32_t startap_tsc_ticks_per_msec;

/*-------------------- internal types ---------------------------------------*/
typedef enum {
	MP_BOOTSTRAP_STATE_INIT = 0,
	MP_BOOTSTRAP_STATE_APS_ENUMERATED = 1,
} mp_bootstrap_state_t;

/*------------------- global vars for communication with APs ----------------*/
volatile mp_bootstrap_state_t mp_bootstrap_state;

init32_struct_t *gp_init32_data;

/* stage 1 */
uint32_t g_aps_counter = 0;

/* stage 2 */

uint8_t gp_GDT[6] = { 0 };              /* xx:xxxx */
uint8_t gp_IDT[6] = { 0 };              /* xx:xxxx */

static volatile uint32_t g_ready_counter;

static func_continue_ap_t g_user_func;
static void *g_any_data_for_user_func;

/* 1 in i position means CPU[i] exists */
uint8_t ap_presence_array[MON_MAX_CPU_SUPPORTED] = { 0 };

/* Low memory page layout */
/* ap_start_up_code */
/* GdtTable */

/* Uncomment the following line to deadloop in AP startup */
/* #define BREAK_IN_AP_STARTUP */
const uint8_t ap_start_up_code[] = {
#ifdef BREAK_IN_AP_STARTUP
	0xEB, 0xFE,                     /* jmp $ */
#endif
	0xB8, 0x00, 0x00,               /* 00: mov ax,AP_START_UP_SEGMENT */
	0x8E, 0xD8,                     /* 03: mov ds,ax */
	0x8D, 0x36, 0x00, 0x00,         /* 05: lea si,GDTR_OFFSET_IN_PAGE */
	0x0F, 0x01, 0x14,               /* 09: lgdt fword ptr [si] */
	0x0F, 0x20, 0xC0,               /* 12: mov eax,cr0 */
	0x0C, 0x01,                     /* 15: or al,1 */
	0x0F, 0x22, 0xC0,               /* 17: mov cr0,eax */
	0x66, 0xEA,                     /* 20: fjmp CS,CONT16 */
	0x00, 0x00, 0x00, 0x00,         /* 22: CONT16 */
	0x00, 0x00,                     /* 26: CS_VALUE */
	/* CONT16: */
	0xFA,                           /* 28: cli */
	0x66, 0xB8, 0x00, 0x00,         /* 29: mov ax,DS_VALUE */
	0x66, 0x8E, 0xD8,               /* 33: mov ds,ax */
	0x66, 0xB8, 0x00, 0x00,         /* 36: mov ax,ES_VALUE */
	0x66, 0x8E, 0xC0,               /* 40: mov es,ax */
	0x66, 0xB8, 0x00, 0x00,         /* 43: mov ax,GS_VALUE */
	0x66, 0x8E, 0xE8,               /* 47: mov gs,ax */
	0x66, 0xB8, 0x00, 0x00,         /* 50: mov ax,FS_VALUE */
	0x66, 0x8E, 0xE0,               /* 54: mov fs,ax */
	0x66, 0xB8, 0x00, 0x00,         /* 57: mov ax,SS_VALUE */
	0x66, 0x8E, 0xD0,               /* 61: mov ss,ax */
	0xB8, 0x00, 0x00, 0x00, 0x00,   /* 64: mov eax,AP_CONTINUE_WAKEUP_CODE */
	0xFF, 0xE0,                     /* 69: jmp eax */
	/* 0x00 71: 32 bytes alignment */
};

#ifdef BREAK_IN_AP_STARTUP
#define AP_CODE_START                           2
#else
#define AP_CODE_START                           0
#endif

#define AP_START_UP_SEGMENT_IN_CODE_OFFSET      (1 + AP_CODE_START)
#define GDTR_OFFSET_IN_CODE                     (7 + AP_CODE_START)
#define CONT16_IN_CODE_OFFSET                   (22 + AP_CODE_START)
#define CONT16_VALUE_OFFSET                     (28 + AP_CODE_START)
#define CS_IN_CODE_OFFSET                       (26 + AP_CODE_START)
#define DS_IN_CODE_OFFSET                       (31 + AP_CODE_START)
#define ES_IN_CODE_OFFSET                       (38 + AP_CODE_START)
#define GS_IN_CODE_OFFSET                       (45 + AP_CODE_START)
#define FS_IN_CODE_OFFSET                       (52 + AP_CODE_START)
#define SS_IN_CODE_OFFSET                       (59 + AP_CODE_START)

#define AP_CONTINUE_WAKEUP_CODE_IN_CODE_OFFSET  (65 + AP_CODE_START)

#define GDTR_OFFSET_IN_PAGE                     ((sizeof(ap_start_up_code) + 7) \
						 & ~7)
#define GDT_OFFSET_IN_PAGE                      (GDTR_OFFSET_IN_PAGE + 8)

/*----------------- forward decls -------------------------------------------*/
void CDECL ap_continue_wakeup_code_C(uint32_t local_apic_id);

static uint8_t bsp_enumerate_aps(void);
static void ap_intialize_environment(void);
static void mp_set_bootstrap_state(mp_bootstrap_state_t new_state);

/*-------------- internal functions -----------------------------------------*/

extern void ap_continue_wakeup_code(void);

/* Setup AP low memory startup code */
static
void setup_low_memory_ap_code(uint32_t temp_low_memory_4K)
{
	uint8_t *code_to_patch = (uint8_t *)temp_low_memory_4K;
	ia32_gdtr_t gdtr_32;
	uint16_t cs_value;
	uint16_t ds_value;
	uint16_t es_value;
	uint16_t gs_value;
	uint16_t fs_value;
	uint16_t ss_value;
	ia32_gdtr_t *new_gdtr_32;

	/*
	 * Make sure the size of ap_start_up_code and GDT table is less than AP_STARTUP_CODE_SIZE
	 * Otherwise you will encounter errors when compiling.
	 *
	 * Low memory page layout
	 * |------------------|
	 * | ap_start_up_code |
	 * |------------------|    -> GDT_OFFSET_IN_PAGE
	 * | GDT table        |
	 * |------------------|    -> gtd.limit is set in mon_acpi_build_s3_resume_protected_code.
	 *                         And the value of limit = TSS_FIRST_GDT_ENTRY_OFFSET - 1.
	 */
	COMPILE_TIME_ASSERT(
		(GDT_OFFSET_IN_PAGE + TSS_FIRST_GDT_ENTRY_OFFSET) <
		AP_STARTUP_CODE_SIZE);

	/* Copy the Startup code to the beginning of the page */
	mon_memcpy(code_to_patch, (const void *)ap_start_up_code,
		sizeof(ap_start_up_code));

	/* get current segments */
	__asm__ __volatile__ (
		"movw %%cs, %0\n\t"
		"movw %%ds, %1\n\t"
		"movw %%es, %2\n\t"
		"movw %%gs, %3\n\t"
		"movw %%fs, %4\n\t"
		"movw %%ss, %5"
		: : "m" (cs_value), "m" (ds_value), "m" (es_value),
		"m" (gs_value), "m" (fs_value), "m" (ss_value)
		: "memory"
		);

	/* Patch the startup code */
	*((uint16_t *)(code_to_patch + AP_START_UP_SEGMENT_IN_CODE_OFFSET)) =
		(uint16_t)(temp_low_memory_4K >> 4);

	*((uint16_t *)(code_to_patch + GDTR_OFFSET_IN_CODE)) =
		(uint16_t)(GDTR_OFFSET_IN_PAGE);

	*((uint32_t *)(code_to_patch + CONT16_IN_CODE_OFFSET)) =
		(uint32_t)code_to_patch + CONT16_VALUE_OFFSET;

	*((uint16_t *)(code_to_patch + CS_IN_CODE_OFFSET)) = cs_value;
	*((uint16_t *)(code_to_patch + DS_IN_CODE_OFFSET)) = ds_value;
	*((uint16_t *)(code_to_patch + ES_IN_CODE_OFFSET)) = es_value;
	*((uint16_t *)(code_to_patch + GS_IN_CODE_OFFSET)) = gs_value;
	*((uint16_t *)(code_to_patch + FS_IN_CODE_OFFSET)) = fs_value;
	*((uint16_t *)(code_to_patch + SS_IN_CODE_OFFSET)) = ss_value;

	*((uint32_t *)(code_to_patch + AP_CONTINUE_WAKEUP_CODE_IN_CODE_OFFSET)) =
		(uint32_t)(ap_continue_wakeup_code);

	/* Copy the GDT table to its place */

	/* get GDTR from BSP */
	__asm__ __volatile__ ("sgdt %0" : : "m" (gdtr_32) : "memory");

	/* copy GDT from bsp to its place. gdtr_32.limit is an address of the last
	 * byte */
	/* assume that there is sufficient place for this */
	mon_memcpy(code_to_patch + GDT_OFFSET_IN_PAGE,
		(uint8_t *)gdtr_32.base, gdtr_32.limit + 1);

	/* Patch the GDT base address in memory */
	new_gdtr_32 = (ia32_gdtr_t *)(code_to_patch + GDTR_OFFSET_IN_PAGE);
	new_gdtr_32->base = (uint32_t)code_to_patch + GDT_OFFSET_IN_PAGE;
	new_gdtr_32->limit = gdtr_32.limit;
}

static
uint64_t CDECL read_msr(uint32_t msr_index)
{
	uint64_t ret;
	__asm__ __volatile__ (
		"movl %1, %%ecx\n\t"
		"rdmsr"
		: "=A" (ret)
		: "m" (msr_index)
		: "ecx"
		);

	return ret;
}

/* Initial AP setup in protected mode - should never return */
/* End of Stage 2 */
void CDECL ap_continue_wakeup_code_C(uint32_t local_apic_id)
{
	/* g_ready_counter++; */
	__asm__ __volatile__ (
		"lock; incl %0" : : "m" (g_ready_counter)
		);

	/* user_func now contains address of the function to be called */
	g_user_func(local_apic_id, g_any_data_for_user_func);
}

/*------------------------------------------------------------------- */
/* read 8-bit port */
/*-------------------------------------------------------------------- */
static uint8_t CDECL read_port_8(uint32_t port)
{
	uint8_t val;
	__asm__ __volatile__ (
		"xor %%eax, %%eax\n\t"
		"movl %1, %%edx\n\t"
		"in  %%dx, %%al"
		: "=a" (val)
		: "m" (port)
		: "edx"
		);

	return val;
}

/******************************************************************************
 *
 *            START: REPLACING STALL() WITH RDTSC()
 *
 *****************************************************************************/

/*================================== startap_stall() =========================*/
/* Stall (busy loop) for a given time, using the platform's speaker port h/w.
 * Should only be called at initialization, since a guest OS may change the
 * platform setting. */
void startap_stall(uint32_t stall_usec)
{
	uint32_t c = 0;

	for (c = 0; c < stall_usec; c++)
		read_port_8(IA32_DEBUG_IO_PORT);
}

/*======================= startap_calibrate_tsc_ticks_per_msec() ============*/
/* Calibrate the internal variable holding the number of TSC ticks pers second.
 * Should only be called at initialization, as it relies on startap_stall() */
void startap_calibrate_tsc_ticks_per_msec(void)
{
	uint32_t start_tsc = 1, end_tsc = 0;

	while (start_tsc > end_tsc) {
		start_tsc = (uint32_t)startap_rdtsc();
		startap_stall(1000); /* 1 ms */
		end_tsc = (uint32_t)startap_rdtsc();
	}
	startap_tsc_ticks_per_msec = (end_tsc - start_tsc);
}

/*========================== startap_stall_using_tsc() ======================*/
/* Stall (busy loop) for a given time, using the CPU TSC register.
 * Note that, depending on the CPU and ASCI modes, the stall accuracy may be
 * rough. */
static void startap_stall_using_tsc(uint32_t stall_usec)
{
	uint32_t start_tsc = 1, end_tsc = 0;

	/* Initialize startap_tsc_ticks_per_msec. Happens at boot time */
	if (startap_tsc_ticks_per_msec == 0) {
		startap_calibrate_tsc_ticks_per_msec();
	}

	/* Calculate the start_tsc and end_tsc */
	/* While loop is to overcome the overflow of 32-bit rdtsc value */
	while (start_tsc > end_tsc) {
		end_tsc =
			(uint32_t)startap_rdtsc() +
			(stall_usec * startap_tsc_ticks_per_msec / 1000);
		start_tsc = (uint32_t)startap_rdtsc();
	}

	while (start_tsc < end_tsc) {
		__asm__ __volatile__ (
			"pause"
			);
		start_tsc = (uint32_t)startap_rdtsc();
	}
}

/***************************************************************************
*
*             END: REPLACING STALL() WITH RDTSC()
*
***************************************************************************/

/*---------------------------------------------------------------------
* send IPI
*--------------------------------------------------------------------*/
static
void send_ipi_to_all_excluding_self(uint32_t vector_number, uint32_t delivery_mode)
{
	ia32_icr_low_t icr_low = { 0 };
	ia32_icr_low_t icr_low_status = { 0 };
	ia32_icr_high_t icr_high = { 0 };
	uint64_t apic_base = 0;

	icr_low.bits.vector = vector_number;
	icr_low.bits.delivery_mode = delivery_mode;

	/* level is set to 1 (except for INIT_DEASSERT, which is not supported in
	 * P3 and P4) */
	/* trigger mode is set to 0 (except for INIT_DEASSERT, which is not
	 * supported in P3 and P4) */
	icr_low.bits.level = 1;
	icr_low.bits.trigger_mode = 0;

	/* broadcast mode - ALL_EXCLUDING_SELF */
	icr_low.bits.destination_shorthand =
		LOCAL_APIC_BROADCAST_MODE_ALL_EXCLUDING_SELF;

	/* send */
	apic_base = read_msr(IA32_MSR_APIC_BASE);
	apic_base &= LOCAL_APIC_BASE_MSR_MASK;

	do
		icr_low_status.uint32 =
			*(uint32_t *)(uint32_t)(apic_base + LOCAL_APIC_ICR_OFFSET);
	while (icr_low_status.bits.delivery_status != 0);

	*(uint32_t *)(uint32_t)(apic_base + LOCAL_APIC_ICR_OFFSET_HIGH) =
		icr_high.uint32;
	*(uint32_t *)(uint32_t)(apic_base + LOCAL_APIC_ICR_OFFSET) = icr_low.uint32;;

	do {
		startap_stall_using_tsc(10);
		icr_low_status.uint32 =
			*(uint32_t *)(uint32_t)(apic_base + LOCAL_APIC_ICR_OFFSET);
	} while (icr_low_status.bits.delivery_status != 0);

	return;
}

static
void send_ipi_to_specific_cpu(uint32_t vector_number,
			      uint32_t delivery_mode, uint8_t dst)
{
	ia32_icr_low_t icr_low = { 0 };
	ia32_icr_low_t icr_low_status = { 0 };
	ia32_icr_high_t icr_high = { 0 };
	uint64_t apic_base = 0;

	icr_low.bits.vector = vector_number;
	icr_low.bits.delivery_mode = delivery_mode;

	/* level is set to 1 (except for INIT_DEASSERT, which is not supported in
	 * P3 and P4) */
	/* trigger mode is set to 0 (except for INIT_DEASSERT, which is not
	 * supported in P3 and P4) */
	icr_low.bits.level = 1;
	icr_low.bits.trigger_mode = 0;

	/* send to specific cpu */
	icr_low.bits.destination_shorthand = LOCAL_APIC_BROADCAST_MODE_SPECIFY_CPU;
	icr_high.bits.destination = dst;

	/* send */
	apic_base = read_msr(IA32_MSR_APIC_BASE);
	apic_base &= LOCAL_APIC_BASE_MSR_MASK;

	do
		icr_low_status.uint32 =
			*(uint32_t *)(uint32_t)(apic_base + LOCAL_APIC_ICR_OFFSET);
	while (icr_low_status.bits.delivery_status != 0);

	*(uint32_t *)(uint32_t)(apic_base + LOCAL_APIC_ICR_OFFSET_HIGH) =
		icr_high.uint32;
	*(uint32_t *)(uint32_t)(apic_base + LOCAL_APIC_ICR_OFFSET) = icr_low.uint32;;

	do {
		startap_stall_using_tsc(10);
		icr_low_status.uint32 =
			*(uint32_t *)(uint32_t)(apic_base + LOCAL_APIC_ICR_OFFSET);
	} while (icr_low_status.bits.delivery_status != 0);

	return;
}

static
void send_init_ipi(void)
{
	send_ipi_to_all_excluding_self(0, LOCAL_APIC_DELIVERY_MODE_INIT);
}

static
void send_sipi_ipi(void *code_start)
{
	/* SIPI message contains address of the code, shifted right to 12 bits */
	send_ipi_to_all_excluding_self(((uint32_t)code_start) >> 12,
		LOCAL_APIC_DELIVERY_MODE_SIPI);
}

/*----------------------------------------------------------------------------
* Send INIT IPI - SIPI to all APs in broadcast mode
*---------------------------------------------------------------------------*/
static
void send_broadcast_init_sipi(init32_struct_t *p_init32_data)
{
	send_init_ipi();
	/* timeout according to manual - 10 miliseconds */
	startap_stall_using_tsc(10000);
	/* SIPI message contains address of the code, shifted right to 12 bits */
	/* send it twice - according to manual */
	send_sipi_ipi((void *)p_init32_data->i32_low_memory_page);
	/* timeout according to manual - 200 miliseconds */
	startap_stall_using_tsc(200000);
	send_sipi_ipi((void *)p_init32_data->i32_low_memory_page);
	/* timeout according to manual - 200 miliseconds */
	startap_stall_using_tsc(200000);
}

/*----------------------------------------------------------------------------
* Send INIT IPI - SIPI to all active APs
*---------------------------------------------------------------------------*/
static
void send_targeted_init_sipi(init32_struct_t *p_init32_data,
			     mon_startup_struct_t *p_startup)
{
	int i;

	for (i = 0; i < p_startup->number_of_processors_at_boot_time - 1; i++)
		send_ipi_to_specific_cpu(0, LOCAL_APIC_DELIVERY_MODE_INIT,
			p_startup->cpu_local_apic_ids[i + 1]);
	/* timeout according to manual - 10 miliseconds */
	startap_stall_using_tsc(10000);

	/* SIPI message contains address of the code, shifted right to 12 bits */
	/* send it twice - according to manual */
	for (i = 0; i < p_startup->number_of_processors_at_boot_time - 1; i++) {
		send_ipi_to_specific_cpu(
			((uint32_t)p_init32_data->i32_low_memory_page) >> 12,
			LOCAL_APIC_DELIVERY_MODE_SIPI,
			p_startup->cpu_local_apic_ids[i + 1]);
	}
	/* timeout according to manual - 200 miliseconds */
	startap_stall_using_tsc(200000);
	for (i = 0; i < p_startup->number_of_processors_at_boot_time - 1; i++) {
		send_ipi_to_specific_cpu(
			((uint32_t)p_init32_data->i32_low_memory_page) >> 12,
			LOCAL_APIC_DELIVERY_MODE_SIPI,
			p_startup->cpu_local_apic_ids[i + 1]);
	}
	/* timeout according to manual - 200 miliseconds */
	startap_stall_using_tsc(200000);
}

/*---------------------------------------------------------------------------
 * Start all APs in pre-os launch and only active APs in post-os launch and
 * bring them to protected non-paged mode.
 * Processors are left in the state were they wait for continuation signal
 * Input:
 * p_init32_data - contains pointer to the free low memory page to be used
 * for bootstap. After the return this memory is free
 * p_startup - contains local apic ids of active cpus to be used in post-os
 * launch
 * Return:
 * number of processors that were init (not including BSP)
 * or -1 on errors
 *---------------------------------------------------------------------------*/
uint32_t ap_procs_startup(init32_struct_t *p_init32_data,
			  mon_startup_struct_t *p_startup)
{
	if (NULL == p_init32_data || 0 == p_init32_data->i32_low_memory_page) {
		return (uint32_t)(-1);
	}

	/* -------- Stage 1 ---------- */

	ap_intialize_environment();

	/* store in global var, to ease access to it from asm code */
	gp_init32_data = p_init32_data;

	__asm__ __volatile__ (
		"sgdt %0\n\t"
		"sidt %1"
		: : "m" (*gp_GDT), "m" (*gp_IDT)
		: "memory"
		);

	/* create AP startup code in low memory */
	setup_low_memory_ap_code(p_init32_data->i32_low_memory_page);

	if (BITMAP_GET(p_startup->flags, MON_STARTUP_POST_OS_LAUNCH_MODE) == 0) {
		send_broadcast_init_sipi(p_init32_data);
	} else {
		send_targeted_init_sipi(p_init32_data, p_startup);
	}

	/* wait for predefined timeout */
	startap_stall_using_tsc(INITIAL_WAIT_FOR_APS_TIMEOUT_IN_MILIS);

	/* -------- Stage 2 ---------- */
	g_aps_counter = bsp_enumerate_aps();

	return g_aps_counter;
}

/*---------------------------------------------------------------------------
 * Run user specified function on all APs.
 * If user function returns it should return in the protected 32bit mode. In
 * this
 * case APs enter the wait state once more.
 * Input:
 * continue_ap_boot_func - user given function to continue AP boot
 * any_data - data to be passed to the function
 * Return:
 * void or never returns - depending on continue_ap_boot_func
 *---------------------------------------------------------------------------*/
void ap_procs_run(func_continue_ap_t continue_ap_boot_func, void *any_data)
{
	g_user_func = continue_ap_boot_func;
	g_any_data_for_user_func = any_data;

	/* signal to APs to pass to the next stage */
	mp_set_bootstrap_state(MP_BOOTSTRAP_STATE_APS_ENUMERATED);

	/* wait until all APs will accept this */
	while (g_ready_counter != g_aps_counter) {
		__asm__ __volatile__ (
			"pause"
			);
	}
}

/*---------------------------------------------------------------------*
* Function  : bsp_enumerate_aps
* Purpose   : Walk through ap_presence_array and counts discovered APs.
*           : In addition modifies array thus it will contain AP IDs,
*           : and not just 1/0.
* Return    : Total number of APs, discovered till now.
* Notes     : Should be called on BSP
*---------------------------------------------------------------------*/
uint8_t bsp_enumerate_aps(void)
{
	int i;
	uint8_t ap_num = 0;

	for (i = 1; i < NELEMENTS(ap_presence_array); ++i) {
		if (0 != ap_presence_array[i]) {
			ap_presence_array[i] = ++ap_num;
		}
	}
	return ap_num;
}

void ap_intialize_environment(void)
{
	mp_bootstrap_state = MP_BOOTSTRAP_STATE_INIT;
	g_ready_counter = 0;
	g_user_func = 0;
	g_any_data_for_user_func = 0;
}

void mp_set_bootstrap_state(mp_bootstrap_state_t new_state)
{
	__asm__ __volatile__ (
		"movl %0, %%eax\n\t"
		"lock; xchgl %%eax, %1"
		: : "m" (new_state), "m" (mp_bootstrap_state)
		: "eax"
		);
}
