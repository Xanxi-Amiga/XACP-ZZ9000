#include <stdint.h>
#include <stdio.h>
#include "xil_cache.h"
#include "xil_exception.h"
#include "xil_io.h"
#include "xil_misc_psreset_api.h"
#include "core2.h"
#include "sleep.h"

#define A9_CPU_RST_CTRL		(XSLCR_BASEADDR + 0x244)
#define A9_RST1_MASK 		0x00000002
#define A9_CLKSTOP1_MASK	0x00000020

#define XSLCR_LOCK_ADDR		(XSLCR_BASEADDR + 0x4)
#define XSLCR_LOCK_CODE		0x0000767B

uint16_t arm_app_output_event_serial = 0;
uint16_t arm_app_output_event_code = 0;
char arm_app_output_event_ack = 0;
uint16_t arm_app_output_events_blocking = 0;
uint16_t arm_app_output_putchar_to_events = 0;
uint16_t arm_app_input_event_serial = 0;
uint16_t arm_app_input_event_code = 0;
char arm_app_input_event_ack = 0;

uint32_t arm_app_output_events_timeout = 100000;

volatile struct ZZ9K_ENV arm_run_env;

volatile struct ZZ9K_ENV* arm_app_get_run_env() {
	return &arm_run_env;
}

void arm_app_put_event_code(uint16_t code) {
	arm_app_output_event_code = code;
	arm_app_output_event_ack = 0;
	arm_app_output_event_serial++;
}

char arm_app_output_event_acked() {
	return arm_app_output_event_ack;
}

void arm_app_set_output_events_blocking(char blocking) {
	arm_app_output_events_blocking = blocking;
}

void arm_app_set_output_putchar_to_events(char putchar_enabled) {
	arm_app_output_putchar_to_events = putchar_enabled;
}

uint16_t arm_app_get_event_serial() {
	return arm_app_input_event_serial;
}

uint16_t arm_app_get_event_code() {
	arm_app_input_event_ack = 1;
	return arm_app_input_event_code;
}

int __attribute__ ((visibility ("default"))) _putchar(char c) {
	if (arm_app_output_putchar_to_events) {
		if (arm_app_output_events_blocking) {
			for (uint32_t i = 0; i < arm_app_output_events_timeout; i++) {
				usleep(1);
				if (arm_app_output_event_ack)
					break;
			}
		}
		arm_app_put_event_code(c);
	}
	return putchar(c);
}

//void DataAbort_InterruptHandler(void *InstancePtr);

volatile void (*core1_trampoline)(volatile struct ZZ9K_ENV* env);
volatile int core2_execute = 0;

#pragma GCC push_options
#pragma GCC optimize ("O1")
// core1_loop is executed on core1 (vs core0)
void core1_loop() {
	asm("mov	r0, r0");
	asm("mrc	p15, 0, r1, c1, c0, 2");
	asm("orr	r1, r1, #(0xf << 20)");
	asm("mcr	p15, 0, r1, c1, c0, 2");
	asm("fmrx	r1, FPEXC");
	asm("orr	r1,r1, #0x40000000");
	asm("fmxr	FPEXC, r1");
	asm("mrc	p15,0,r0,c1,c0,0");
	asm("orr	r0, r0, #(0x01 << 11)");
	asm("mcr	p15,0,r0,c1,c0,0");
	asm("mrc	p15,0,r0,c1,c0,1");
	asm("orr	r0, r0, #(0x1 << 2)");
	asm("orr	r0, r0, #(0x1 << 1)");
	asm("mcr	p15,0,r0,c1,c0,1");

	asm("mov sp, #0x06000000");

	volatile uint32_t* addr = 0;
	addr[0] = 0xe3e0000f;
	addr[1] = 0xe590f000;

	while (1) {
		while (!core2_execute) {
			usleep(1);
		}
		core2_execute = 0;
		printf("[core2] executing at %p.\n", core1_trampoline);
		Xil_DCacheFlush();
		Xil_ICacheInvalidate();

		asm("push {r0-r12}");
		asm("mov r0, #0x00010000");
		asm("str sp, [r0]");

		core1_trampoline(&arm_run_env);

		asm("mov r0, #0x00010000");
		asm("ldr sp, [r0]");
		asm("pop {r0-r12}");
	}
}
#pragma GCC pop_options

void arm_app_init() {
	arm_run_env.api_version = 1;
	arm_run_env.fn_putchar = _putchar;
	arm_run_env.fn_get_event_code = arm_app_get_event_code;
	arm_run_env.fn_get_event_serial = arm_app_get_event_serial;
	arm_run_env.fn_output_event_acked = arm_app_output_event_acked;
	arm_run_env.fn_put_event_code = arm_app_put_event_code;
	arm_run_env.fn_set_output_events_blocking =
			arm_app_set_output_events_blocking;
	arm_run_env.fn_set_output_putchar_to_events =
			arm_app_set_output_putchar_to_events;
	arm_run_env.argc = 0;

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_RESET,
			(Xil_ExceptionHandler) arm_exception_handler_id_reset, NULL);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_DATA_ABORT_INT,
			(Xil_ExceptionHandler) arm_exception_handler_id_data_abort, NULL);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_PREFETCH_ABORT_INT,
			(Xil_ExceptionHandler) arm_exception_handler_id_prefetch_abort, NULL);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_UNDEFINED_INT,
			(Xil_ExceptionHandler) arm_exception_handler_illinst, NULL);

	printf("[core2] launch...\n");
	volatile uint32_t* core1_addr = (volatile uint32_t*) 0xFFFFFFF0;
	*core1_addr = (uint32_t) core1_loop;
	volatile uint32_t* core1_addr2 = (volatile uint32_t*) 0x140;
	core1_addr2[0] = 0xe3e0000f;
	core1_addr2[1] = 0xe590f000;
	core1_addr2 = (volatile uint32_t*) 0x100;
	core1_addr2[0] = 0xe3e0000f;
	core1_addr2[1] = 0xe590f000;
	asm("sev");
	printf("[core2] now idling.\n");
}

void arm_app_run(uint32_t arm_run_address) {
	volatile uint32_t* core1_addr = (volatile uint32_t*) 0xFFFFFFF0;
	volatile uint32_t* core1_addr2 = (volatile uint32_t*) 0x100;

	*core1_addr = (uint32_t) core1_loop;
	core1_addr2[0] = 0xe3e0000f;
	core1_addr2[1] = 0xe590f000;

	printf("[ARM_RUN] %lx\n", arm_run_address);
	if (arm_run_address > 0) {
		core1_trampoline = (volatile void (*)(
				volatile struct ZZ9K_ENV*)) arm_run_address;
		printf("[ARM_RUN] signaling second core.\n");
		Xil_DCacheFlush();
		Xil_ICacheInvalidate();
		core2_execute = 1;
		Xil_DCacheFlush();
		Xil_ICacheInvalidate();
	} else {
		core1_trampoline = 0;
		core2_execute = 0;
	}

	Xil_Out32(XSLCR_UNLOCK_ADDR, XSLCR_UNLOCK_CODE);
	uint32_t RegVal = Xil_In32(A9_CPU_RST_CTRL);
	RegVal |= A9_RST1_MASK;
	Xil_Out32(A9_CPU_RST_CTRL, RegVal);
	RegVal |= A9_CLKSTOP1_MASK;
	Xil_Out32(A9_CPU_RST_CTRL, RegVal);
	RegVal &= ~A9_RST1_MASK;
	Xil_Out32(A9_CPU_RST_CTRL, RegVal);
	RegVal &= ~A9_CLKSTOP1_MASK;
	Xil_Out32(A9_CPU_RST_CTRL, RegVal);
	Xil_Out32(XSLCR_LOCK_ADDR, XSLCR_LOCK_CODE);

	dmb();
	dsb();
	isb();
	asm("sev");
}

void arm_app_input_event(uint32_t evt) {
	arm_app_input_event_code = evt;
	arm_app_input_event_serial++;
	arm_app_input_event_ack = 0;
}

uint32_t arm_app_output_event() {
	uint32_t data = (arm_app_output_event_serial << 16)
					| arm_app_output_event_code;
	arm_app_output_event_ack = 1;
	return data;
}

void arm_exception_handler_id_reset(void *callback) {
	printf("id_reset: arm_exception_handler()!\n");
	while (1) {}
}

void arm_exception_handler_id_data_abort(void *callback) {
	printf("id_data_abort: arm_exception_handler()!\n");
	while (1) {}
}

void arm_exception_handler_id_prefetch_abort(void *callback) {
	printf("id_prefetch_abort: arm_exception_handler()!\n");
	while (1) {}
}

void arm_exception_handler(void *callback) {
	printf("arm_exception_handler()!\n");
	while (1) {}
}

void arm_exception_handler_illinst(void *callback) {
	printf("arm_exception_handler_illinst()!\n");
	while (1) {}
}

/*
 * arm_app_force_reset - reset Core1 and return it to core1_loop.
 *
 * A short reset stub is installed at address 0 while CPU1 is held in reset.
 * The stub records progress in a shared marker word and then transfers control
 * through the standard boot vector at 0xFFFFFFF0.
 */

/* Shared reset marker (ARM physical address). */
#define CORE1_RESET_MARKER_ARM  0x04700FE0UL

static inline void write_marker(uint32_t val) {
	volatile uint32_t* m = (volatile uint32_t*) CORE1_RESET_MARKER_ARM;
	*m = val;
	Xil_DCacheFlushRange((uint32_t)m, 4);
}

void arm_app_force_reset(void) {
	uint32_t RegVal;

	/* Mark reset entry. */
	write_marker(0xC1A00001UL);

	/* Unlock SLCR */
	Xil_Out32(XSLCR_UNLOCK_ADDR, XSLCR_UNLOCK_CODE);

	/* Assert reset then stop clock */
	RegVal = Xil_In32(A9_CPU_RST_CTRL);
	RegVal |= A9_RST1_MASK;
	Xil_Out32(A9_CPU_RST_CTRL, RegVal);
	RegVal |= A9_CLKSTOP1_MASK;
	Xil_Out32(A9_CPU_RST_CTRL, RegVal);

	/* CPU1 is now held in reset. */
	write_marker(0xC1A00002UL);

	/* Reset handshake variables while CPU1 is stopped */
	core2_execute = 0;
	core1_trampoline = 0;

	/*
	 * Install stub at 0x00000000.
	 * CPU1 starts execution at 0x00000000 after reset (before BootROM
	 * redirects via 0xFFFFFFF0). We place a minimal stub that:
	 *   1. writes 0xC1B00000 to marker DDR address
	 *   2. jumps to 0xFFFFFFF0 (which holds core1_loop address)
	 *
	 * ARM instructions (little-endian encoding):
	 *   ldr r0, [pc, #8]   @ 0xe59f0008 - load marker addr
	 *   ldr r1, [pc, #8]   @ 0xe59f1008 - load marker value
	 *   str r1, [r0]       @ 0xe5801000 - write marker
	 *   ldr pc, [pc, #0]   @ 0xe59ff000 - jump to 0xFFFFFFF0 content
	 *   .word 0x04700FE0   @ marker DDR address
	 *   .word 0xC1B00000   @ marker value
	 *   .word 0xFFFFFFF0   @ jump target address word
	 */
	volatile uint32_t* stub = (volatile uint32_t*) 0x00000000UL;
	stub[0] = 0xe59f0008UL; /* ldr r0, [pc, #8]  */
	stub[1] = 0xe59f1008UL; /* ldr r1, [pc, #8]  */
	stub[2] = 0xe5801000UL; /* str r1, [r0]      */
	stub[3] = 0xe59ff000UL; /* ldr pc, [pc, #0]  */
	stub[4] = CORE1_RESET_MARKER_ARM;
	stub[5] = 0xC1B00000UL;
	stub[6] = 0xFFFFFFF0UL;

	/* Restore boot vector */
	volatile uint32_t* vec = (volatile uint32_t*) 0xFFFFFFF0UL;
	*vec = (uint32_t) core1_loop;

	/* Flush: stub + boot vector + marker + handshake vars */
	Xil_DCacheFlushRange(0x00000000UL, 32);
	Xil_DCacheFlushRange(0xFFFFFFF0UL, 4);
	Xil_DCacheFlush();
	Xil_ICacheInvalidate();

	/* Reset stub is visible before CPU1 is released. */
	write_marker(0xC1A00003UL);

	/* Release reset then restart clock */
	RegVal = Xil_In32(A9_CPU_RST_CTRL);
	RegVal &= ~A9_RST1_MASK;
	Xil_Out32(A9_CPU_RST_CTRL, RegVal);
	RegVal &= ~A9_CLKSTOP1_MASK;
	Xil_Out32(A9_CPU_RST_CTRL, RegVal);

	Xil_Out32(XSLCR_LOCK_ADDR, XSLCR_LOCK_CODE);

	dmb();
	dsb();
	isb();
	asm("sev");

	/* Mark reset completion. */
	write_marker(0xC1A00004UL);

	printf("[core2] force_reset forensic: CPU1 released, waiting stub.\n");
}