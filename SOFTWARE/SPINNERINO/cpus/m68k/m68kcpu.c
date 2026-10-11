/* ======================================================================== */
/* ========================= LICENSING & COPYRIGHT ======================== */
/* ======================================================================== */
/*
 *                                  MUSASHI
 *                                Version 4.60
 *
 * A portable Motorola M680x0 processor emulation engine.
 * Copyright Karl Stenerud.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/* ======================================================================== */
/* ================================= NOTES ================================ */
/* ======================================================================== */



/* ======================================================================== */
/* ================================ INCLUDES ============================== */
/* ======================================================================== */

extern void m68040_fpu_op0(void);
extern void m68040_fpu_op1(void);
extern void m68881_mmu_ops(void);
/* PATCH CABINATONE: tabelle allocate in PSRAM, vedi la nota in m68kops.c */
extern unsigned char (*m68ki_cycles)[0x10000];
extern void (**m68ki_instruction_jump_table)(void); /* opcode handler jump table */
extern void m68ki_build_opcode_table(void);

#include "m68kops.h"
#include "m68kcpu.h"

#include "m68kfpu.c"
#include "m68kmmu.h" // uses some functions from m68kfpu.c which are static !

/* ======================================================================== */
/* ================================= DATA ================================= */
/* ======================================================================== */

/* ⭐⭐ FINESTRA DIRETTA DELLO SPAZIO OPCODE (patch SPINNERINO, vedi m68kcpu.h).
   A riposo `limite` vale 0 e la scorciatoia non scatta mai: per ogni gioco che
   non la usa il comportamento e' identico byte per byte a prima. */
const unsigned char *m68ki_opc_base  = 0;
unsigned int         m68ki_opc_limit = 0;

void m68k_set_opcode_window(const void *base, unsigned int limit)
{
	m68ki_opc_base  = (const unsigned char *)base;
	m68ki_opc_limit = base ? limit : 0;
}

/* ⭐⭐ IDLE-SKIP — stato (patch SPINNERINO, vedi m68k.h e m68ki_idle_step()).
   A riposo `m68ki_idle_hi` vale 0 e TUTTO il meccanismo e' spento: i ganci
   nelle letture/scritture guardano `m68ki_idle_watch`, che resta 0 per sempre,
   e m68ki_idle_step() esce alla prima riga. */
unsigned int  m68ki_idle_lo = 1, m68ki_idle_hi = 0;   /* lo > hi = SPENTO */
int           m68ki_idle_watch = 0;
int           m68ki_idle_bad   = 0;
unsigned long m68ki_idle_volte = 0, m68ki_idle_cicli = 0;

void m68k_set_idle_skip(unsigned int lo, unsigned int hi)
{
	m68ki_idle_lo    = lo;
	m68ki_idle_hi    = (lo <= hi) ? hi : 0;
	m68ki_idle_watch = 0;
	m68ki_idle_bad   = 0;
}
void m68k_idle_stats(unsigned long *volte, unsigned long *cicli)
{
	if(volte) *volte = m68ki_idle_volte;
	if(cicli) *cicli = m68ki_idle_cicli;
}

int  m68ki_initial_cycles;
int  m68ki_remaining_cycles = 0;                     /* Number of clocks remaining */
uint m68ki_tracing = 0;
uint m68ki_address_space;

#ifdef M68K_LOG_ENABLE
const char *const m68ki_cpu_names[] =
{
	"Invalid CPU",
	"M68000",
	"M68010",
	"M68EC020",
	"M68020",
	"M68EC030",
	"M68030",
	"M68EC040",
	"M68LC040",
	"M68040",
	"SCC68070",
};
#endif /* M68K_LOG_ENABLE */

/* The CPU core */
m68ki_cpu_core m68ki_cpu = {0};

#if M68K_EMULATE_ADDRESS_ERROR
#ifdef _BSD_SETJMP_H
sigjmp_buf m68ki_aerr_trap;
#else
jmp_buf m68ki_aerr_trap;
#endif
#endif /* M68K_EMULATE_ADDRESS_ERROR */

uint    m68ki_aerr_address;
uint    m68ki_aerr_write_mode;
uint    m68ki_aerr_fc;

jmp_buf m68ki_bus_error_jmp_buf;

/* Used by shift & rotate instructions */
const uint8 m68ki_shift_8_table[65] =
{
	0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff
};
const uint16 m68ki_shift_16_table[65] =
{
	0x0000, 0x8000, 0xc000, 0xe000, 0xf000, 0xf800, 0xfc00, 0xfe00, 0xff00,
	0xff80, 0xffc0, 0xffe0, 0xfff0, 0xfff8, 0xfffc, 0xfffe, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff
};
const uint m68ki_shift_32_table[65] =
{
	0x00000000, 0x80000000, 0xc0000000, 0xe0000000, 0xf0000000, 0xf8000000,
	0xfc000000, 0xfe000000, 0xff000000, 0xff800000, 0xffc00000, 0xffe00000,
	0xfff00000, 0xfff80000, 0xfffc0000, 0xfffe0000, 0xffff0000, 0xffff8000,
	0xffffc000, 0xffffe000, 0xfffff000, 0xfffff800, 0xfffffc00, 0xfffffe00,
	0xffffff00, 0xffffff80, 0xffffffc0, 0xffffffe0, 0xfffffff0, 0xfffffff8,
	0xfffffffc, 0xfffffffe, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
};


/* Number of clock cycles to use for exception processing.
 * I used 4 for any vectors that are undocumented for processing times.
 */
const uint8 m68ki_exception_cycle_table[5][256] =
{
	{ /* 000 */
		 40, /*  0: Reset - Initial Stack Pointer                      */
		  4, /*  1: Reset - Initial Program Counter                    */
		 50, /*  2: Bus Error                             (unemulated) */
		 50, /*  3: Address Error                         (unemulated) */
		 34, /*  4: Illegal Instruction                                */
		 38, /*  5: Divide by Zero                                     */
		 40, /*  6: CHK                                                */
		 34, /*  7: TRAPV                                              */
		 34, /*  8: Privilege Violation                                */
		 34, /*  9: Trace                                              */
		 34, /* 10: 1010                                               */
		 34, /* 11: 1111                                               */
		  4, /* 12: RESERVED                                           */
		  4, /* 13: Coprocessor Protocol Violation        (unemulated) */
		  4, /* 14: Format Error                                       */
		 44, /* 15: Uninitialized Interrupt                            */
		  4, /* 16: RESERVED                                           */
		  4, /* 17: RESERVED                                           */
		  4, /* 18: RESERVED                                           */
		  4, /* 19: RESERVED                                           */
		  4, /* 20: RESERVED                                           */
		  4, /* 21: RESERVED                                           */
		  4, /* 22: RESERVED                                           */
		  4, /* 23: RESERVED                                           */
		 44, /* 24: Spurious Interrupt                                 */
		 44, /* 25: Level 1 Interrupt Autovector                       */
		 44, /* 26: Level 2 Interrupt Autovector                       */
		 44, /* 27: Level 3 Interrupt Autovector                       */
		 44, /* 28: Level 4 Interrupt Autovector                       */
		 44, /* 29: Level 5 Interrupt Autovector                       */
		 44, /* 30: Level 6 Interrupt Autovector                       */
		 44, /* 31: Level 7 Interrupt Autovector                       */
		 34, /* 32: TRAP #0                                            */
		 34, /* 33: TRAP #1                                            */
		 34, /* 34: TRAP #2                                            */
		 34, /* 35: TRAP #3                                            */
		 34, /* 36: TRAP #4                                            */
		 34, /* 37: TRAP #5                                            */
		 34, /* 38: TRAP #6                                            */
		 34, /* 39: TRAP #7                                            */
		 34, /* 40: TRAP #8                                            */
		 34, /* 41: TRAP #9                                            */
		 34, /* 42: TRAP #10                                           */
		 34, /* 43: TRAP #11                                           */
		 34, /* 44: TRAP #12                                           */
		 34, /* 45: TRAP #13                                           */
		 34, /* 46: TRAP #14                                           */
		 34, /* 47: TRAP #15                                           */
		  4, /* 48: FP Branch or Set on Unknown Condition (unemulated) */
		  4, /* 49: FP Inexact Result                     (unemulated) */
		  4, /* 50: FP Divide by Zero                     (unemulated) */
		  4, /* 51: FP Underflow                          (unemulated) */
		  4, /* 52: FP Operand Error                      (unemulated) */
		  4, /* 53: FP Overflow                           (unemulated) */
		  4, /* 54: FP Signaling NAN                      (unemulated) */
		  4, /* 55: FP Unimplemented Data Type            (unemulated) */
		  4, /* 56: MMU Configuration Error               (unemulated) */
		  4, /* 57: MMU Illegal Operation Error           (unemulated) */
		  4, /* 58: MMU Access Level Violation Error      (unemulated) */
		  4, /* 59: RESERVED                                           */
		  4, /* 60: RESERVED                                           */
		  4, /* 61: RESERVED                                           */
		  4, /* 62: RESERVED                                           */
		  4, /* 63: RESERVED                                           */
		     /* 64-255: User Defined                                   */
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
	},
	{ /* 010 */
		 40, /*  0: Reset - Initial Stack Pointer                      */
		  4, /*  1: Reset - Initial Program Counter                    */
		126, /*  2: Bus Error                             (unemulated) */
		126, /*  3: Address Error                         (unemulated) */
		 38, /*  4: Illegal Instruction                                */
		 44, /*  5: Divide by Zero                                     */
		 44, /*  6: CHK                                                */
		 34, /*  7: TRAPV                                              */
		 38, /*  8: Privilege Violation                                */
		 38, /*  9: Trace                                              */
		  4, /* 10: 1010                                               */
		  4, /* 11: 1111                                               */
		  4, /* 12: RESERVED                                           */
		  4, /* 13: Coprocessor Protocol Violation        (unemulated) */
		  4, /* 14: Format Error                                       */
		 44, /* 15: Uninitialized Interrupt                            */
		  4, /* 16: RESERVED                                           */
		  4, /* 17: RESERVED                                           */
		  4, /* 18: RESERVED                                           */
		  4, /* 19: RESERVED                                           */
		  4, /* 20: RESERVED                                           */
		  4, /* 21: RESERVED                                           */
		  4, /* 22: RESERVED                                           */
		  4, /* 23: RESERVED                                           */
		 46, /* 24: Spurious Interrupt                                 */
		 46, /* 25: Level 1 Interrupt Autovector                       */
		 46, /* 26: Level 2 Interrupt Autovector                       */
		 46, /* 27: Level 3 Interrupt Autovector                       */
		 46, /* 28: Level 4 Interrupt Autovector                       */
		 46, /* 29: Level 5 Interrupt Autovector                       */
		 46, /* 30: Level 6 Interrupt Autovector                       */
		 46, /* 31: Level 7 Interrupt Autovector                       */
		 38, /* 32: TRAP #0                                            */
		 38, /* 33: TRAP #1                                            */
		 38, /* 34: TRAP #2                                            */
		 38, /* 35: TRAP #3                                            */
		 38, /* 36: TRAP #4                                            */
		 38, /* 37: TRAP #5                                            */
		 38, /* 38: TRAP #6                                            */
		 38, /* 39: TRAP #7                                            */
		 38, /* 40: TRAP #8                                            */
		 38, /* 41: TRAP #9                                            */
		 38, /* 42: TRAP #10                                           */
		 38, /* 43: TRAP #11                                           */
		 38, /* 44: TRAP #12                                           */
		 38, /* 45: TRAP #13                                           */
		 38, /* 46: TRAP #14                                           */
		 38, /* 47: TRAP #15                                           */
		  4, /* 48: FP Branch or Set on Unknown Condition (unemulated) */
		  4, /* 49: FP Inexact Result                     (unemulated) */
		  4, /* 50: FP Divide by Zero                     (unemulated) */
		  4, /* 51: FP Underflow                          (unemulated) */
		  4, /* 52: FP Operand Error                      (unemulated) */
		  4, /* 53: FP Overflow                           (unemulated) */
		  4, /* 54: FP Signaling NAN                      (unemulated) */
		  4, /* 55: FP Unimplemented Data Type            (unemulated) */
		  4, /* 56: MMU Configuration Error               (unemulated) */
		  4, /* 57: MMU Illegal Operation Error           (unemulated) */
		  4, /* 58: MMU Access Level Violation Error      (unemulated) */
		  4, /* 59: RESERVED                                           */
		  4, /* 60: RESERVED                                           */
		  4, /* 61: RESERVED                                           */
		  4, /* 62: RESERVED                                           */
		  4, /* 63: RESERVED                                           */
		     /* 64-255: User Defined                                   */
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
	},
	{ /* 020 */
		  4, /*  0: Reset - Initial Stack Pointer                      */
		  4, /*  1: Reset - Initial Program Counter                    */
		 50, /*  2: Bus Error                             (unemulated) */
		 50, /*  3: Address Error                         (unemulated) */
		 20, /*  4: Illegal Instruction                                */
		 38, /*  5: Divide by Zero                                     */
		 40, /*  6: CHK                                                */
		 20, /*  7: TRAPV                                              */
		 34, /*  8: Privilege Violation                                */
		 25, /*  9: Trace                                              */
		 20, /* 10: 1010                                               */
		 20, /* 11: 1111                                               */
		  4, /* 12: RESERVED                                           */
		  4, /* 13: Coprocessor Protocol Violation        (unemulated) */
		  4, /* 14: Format Error                                       */
		 30, /* 15: Uninitialized Interrupt                            */
		  4, /* 16: RESERVED                                           */
		  4, /* 17: RESERVED                                           */
		  4, /* 18: RESERVED                                           */
		  4, /* 19: RESERVED                                           */
		  4, /* 20: RESERVED                                           */
		  4, /* 21: RESERVED                                           */
		  4, /* 22: RESERVED                                           */
		  4, /* 23: RESERVED                                           */
		 30, /* 24: Spurious Interrupt                                 */
		 30, /* 25: Level 1 Interrupt Autovector                       */
		 30, /* 26: Level 2 Interrupt Autovector                       */
		 30, /* 27: Level 3 Interrupt Autovector                       */
		 30, /* 28: Level 4 Interrupt Autovector                       */
		 30, /* 29: Level 5 Interrupt Autovector                       */
		 30, /* 30: Level 6 Interrupt Autovector                       */
		 30, /* 31: Level 7 Interrupt Autovector                       */
		 20, /* 32: TRAP #0                                            */
		 20, /* 33: TRAP #1                                            */
		 20, /* 34: TRAP #2                                            */
		 20, /* 35: TRAP #3                                            */
		 20, /* 36: TRAP #4                                            */
		 20, /* 37: TRAP #5                                            */
		 20, /* 38: TRAP #6                                            */
		 20, /* 39: TRAP #7                                            */
		 20, /* 40: TRAP #8                                            */
		 20, /* 41: TRAP #9                                            */
		 20, /* 42: TRAP #10                                           */
		 20, /* 43: TRAP #11                                           */
		 20, /* 44: TRAP #12                                           */
		 20, /* 45: TRAP #13                                           */
		 20, /* 46: TRAP #14                                           */
		 20, /* 47: TRAP #15                                           */
		  4, /* 48: FP Branch or Set on Unknown Condition (unemulated) */
		  4, /* 49: FP Inexact Result                     (unemulated) */
		  4, /* 50: FP Divide by Zero                     (unemulated) */
		  4, /* 51: FP Underflow                          (unemulated) */
		  4, /* 52: FP Operand Error                      (unemulated) */
		  4, /* 53: FP Overflow                           (unemulated) */
		  4, /* 54: FP Signaling NAN                      (unemulated) */
		  4, /* 55: FP Unimplemented Data Type            (unemulated) */
		  4, /* 56: MMU Configuration Error               (unemulated) */
		  4, /* 57: MMU Illegal Operation Error           (unemulated) */
		  4, /* 58: MMU Access Level Violation Error      (unemulated) */
		  4, /* 59: RESERVED                                           */
		  4, /* 60: RESERVED                                           */
		  4, /* 61: RESERVED                                           */
		  4, /* 62: RESERVED                                           */
		  4, /* 63: RESERVED                                           */
		     /* 64-255: User Defined                                   */
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
	},
	{ /* 030 - not correct */
		  4, /*  0: Reset - Initial Stack Pointer                      */
		  4, /*  1: Reset - Initial Program Counter                    */
		 50, /*  2: Bus Error                             (unemulated) */
		 50, /*  3: Address Error                         (unemulated) */
		 20, /*  4: Illegal Instruction                                */
		 38, /*  5: Divide by Zero                                     */
		 40, /*  6: CHK                                                */
		 20, /*  7: TRAPV                                              */
		 34, /*  8: Privilege Violation                                */
		 25, /*  9: Trace                                              */
		 20, /* 10: 1010                                               */
		 20, /* 11: 1111                                               */
		  4, /* 12: RESERVED                                           */
		  4, /* 13: Coprocessor Protocol Violation        (unemulated) */
		  4, /* 14: Format Error                                       */
		 30, /* 15: Uninitialized Interrupt                            */
		  4, /* 16: RESERVED                                           */
		  4, /* 17: RESERVED                                           */
		  4, /* 18: RESERVED                                           */
		  4, /* 19: RESERVED                                           */
		  4, /* 20: RESERVED                                           */
		  4, /* 21: RESERVED                                           */
		  4, /* 22: RESERVED                                           */
		  4, /* 23: RESERVED                                           */
		 30, /* 24: Spurious Interrupt                                 */
		 30, /* 25: Level 1 Interrupt Autovector                       */
		 30, /* 26: Level 2 Interrupt Autovector                       */
		 30, /* 27: Level 3 Interrupt Autovector                       */
		 30, /* 28: Level 4 Interrupt Autovector                       */
		 30, /* 29: Level 5 Interrupt Autovector                       */
		 30, /* 30: Level 6 Interrupt Autovector                       */
		 30, /* 31: Level 7 Interrupt Autovector                       */
		 20, /* 32: TRAP #0                                            */
		 20, /* 33: TRAP #1                                            */
		 20, /* 34: TRAP #2                                            */
		 20, /* 35: TRAP #3                                            */
		 20, /* 36: TRAP #4                                            */
		 20, /* 37: TRAP #5                                            */
		 20, /* 38: TRAP #6                                            */
		 20, /* 39: TRAP #7                                            */
		 20, /* 40: TRAP #8                                            */
		 20, /* 41: TRAP #9                                            */
		 20, /* 42: TRAP #10                                           */
		 20, /* 43: TRAP #11                                           */
		 20, /* 44: TRAP #12                                           */
		 20, /* 45: TRAP #13                                           */
		 20, /* 46: TRAP #14                                           */
		 20, /* 47: TRAP #15                                           */
		  4, /* 48: FP Branch or Set on Unknown Condition (unemulated) */
		  4, /* 49: FP Inexact Result                     (unemulated) */
		  4, /* 50: FP Divide by Zero                     (unemulated) */
		  4, /* 51: FP Underflow                          (unemulated) */
		  4, /* 52: FP Operand Error                      (unemulated) */
		  4, /* 53: FP Overflow                           (unemulated) */
		  4, /* 54: FP Signaling NAN                      (unemulated) */
		  4, /* 55: FP Unimplemented Data Type            (unemulated) */
		  4, /* 56: MMU Configuration Error               (unemulated) */
		  4, /* 57: MMU Illegal Operation Error           (unemulated) */
		  4, /* 58: MMU Access Level Violation Error      (unemulated) */
		  4, /* 59: RESERVED                                           */
		  4, /* 60: RESERVED                                           */
		  4, /* 61: RESERVED                                           */
		  4, /* 62: RESERVED                                           */
		  4, /* 63: RESERVED                                           */
		     /* 64-255: User Defined                                   */
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
	},
	{ /* 040 */ // TODO: these values are not correct
		  4, /*  0: Reset - Initial Stack Pointer                      */
		  4, /*  1: Reset - Initial Program Counter                    */
		 50, /*  2: Bus Error                             (unemulated) */
		 50, /*  3: Address Error                         (unemulated) */
		 20, /*  4: Illegal Instruction                                */
		 38, /*  5: Divide by Zero                                     */
		 40, /*  6: CHK                                                */
		 20, /*  7: TRAPV                                              */
		 34, /*  8: Privilege Violation                                */
		 25, /*  9: Trace                                              */
		 20, /* 10: 1010                                               */
		 20, /* 11: 1111                                               */
		  4, /* 12: RESERVED                                           */
		  4, /* 13: Coprocessor Protocol Violation        (unemulated) */
		  4, /* 14: Format Error                                       */
		 30, /* 15: Uninitialized Interrupt                            */
		  4, /* 16: RESERVED                                           */
		  4, /* 17: RESERVED                                           */
		  4, /* 18: RESERVED                                           */
		  4, /* 19: RESERVED                                           */
		  4, /* 20: RESERVED                                           */
		  4, /* 21: RESERVED                                           */
		  4, /* 22: RESERVED                                           */
		  4, /* 23: RESERVED                                           */
		 30, /* 24: Spurious Interrupt                                 */
		 30, /* 25: Level 1 Interrupt Autovector                       */
		 30, /* 26: Level 2 Interrupt Autovector                       */
		 30, /* 27: Level 3 Interrupt Autovector                       */
		 30, /* 28: Level 4 Interrupt Autovector                       */
		 30, /* 29: Level 5 Interrupt Autovector                       */
		 30, /* 30: Level 6 Interrupt Autovector                       */
		 30, /* 31: Level 7 Interrupt Autovector                       */
		 20, /* 32: TRAP #0                                            */
		 20, /* 33: TRAP #1                                            */
		 20, /* 34: TRAP #2                                            */
		 20, /* 35: TRAP #3                                            */
		 20, /* 36: TRAP #4                                            */
		 20, /* 37: TRAP #5                                            */
		 20, /* 38: TRAP #6                                            */
		 20, /* 39: TRAP #7                                            */
		 20, /* 40: TRAP #8                                            */
		 20, /* 41: TRAP #9                                            */
		 20, /* 42: TRAP #10                                           */
		 20, /* 43: TRAP #11                                           */
		 20, /* 44: TRAP #12                                           */
		 20, /* 45: TRAP #13                                           */
		 20, /* 46: TRAP #14                                           */
		 20, /* 47: TRAP #15                                           */
		  4, /* 48: FP Branch or Set on Unknown Condition (unemulated) */
		  4, /* 49: FP Inexact Result                     (unemulated) */
		  4, /* 50: FP Divide by Zero                     (unemulated) */
		  4, /* 51: FP Underflow                          (unemulated) */
		  4, /* 52: FP Operand Error                      (unemulated) */
		  4, /* 53: FP Overflow                           (unemulated) */
		  4, /* 54: FP Signaling NAN                      (unemulated) */
		  4, /* 55: FP Unimplemented Data Type            (unemulated) */
		  4, /* 56: MMU Configuration Error               (unemulated) */
		  4, /* 57: MMU Illegal Operation Error           (unemulated) */
		  4, /* 58: MMU Access Level Violation Error      (unemulated) */
		  4, /* 59: RESERVED                                           */
		  4, /* 60: RESERVED                                           */
		  4, /* 61: RESERVED                                           */
		  4, /* 62: RESERVED                                           */
		  4, /* 63: RESERVED                                           */
		     /* 64-255: User Defined                                   */
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
	}
};

const uint8 m68ki_ea_idx_cycle_table[64] =
{
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0, /* ..01.000 no memory indirect, base NULL             */
	 5, /* ..01..01 memory indirect,    base NULL, outer NULL */
	 7, /* ..01..10 memory indirect,    base NULL, outer 16   */
	 7, /* ..01..11 memory indirect,    base NULL, outer 32   */
	 0,  5,  7,  7,  0,  5,  7,  7,  0,  5,  7,  7,
	 2, /* ..10.000 no memory indirect, base 16               */
	 7, /* ..10..01 memory indirect,    base 16,   outer NULL */
	 9, /* ..10..10 memory indirect,    base 16,   outer 16   */
	 9, /* ..10..11 memory indirect,    base 16,   outer 32   */
	 0,  7,  9,  9,  0,  7,  9,  9,  0,  7,  9,  9,
	 6, /* ..11.000 no memory indirect, base 32               */
	11, /* ..11..01 memory indirect,    base 32,   outer NULL */
	13, /* ..11..10 memory indirect,    base 32,   outer 16   */
	13, /* ..11..11 memory indirect,    base 32,   outer 32   */
	 0, 11, 13, 13,  0, 11, 13, 13,  0, 11, 13, 13
};



/* ======================================================================== */
/* =============================== CALLBACKS ============================== */
/* ======================================================================== */

/* Default callbacks used if the callback hasn't been set yet, or if the
 * callback is set to NULL
 */

/* Interrupt acknowledge */
static int default_int_ack_callback_data;
static int default_int_ack_callback(int int_level)
{
	default_int_ack_callback_data = int_level;
	CPU_INT_LEVEL = 0;
	return M68K_INT_ACK_AUTOVECTOR;
}

/* Breakpoint acknowledge */
static unsigned int default_bkpt_ack_callback_data;
static void default_bkpt_ack_callback(unsigned int data)
{
	default_bkpt_ack_callback_data = data;
}

/* Called when a reset instruction is executed */
static void default_reset_instr_callback(void)
{
}

/* Called when a cmpi.l #v, dn instruction is executed */
static void default_cmpild_instr_callback(unsigned int val, int reg)
{
	(void)val;
	(void)reg;
}

/* Called when a rte instruction is executed */
static void default_rte_instr_callback(void)
{
}

/* Called when a tas instruction is executed */
static int default_tas_instr_callback(void)
{
	return 1; // allow writeback
}

/* Called when an illegal instruction is encountered */
static int default_illg_instr_callback(int opcode)
{
	(void)opcode;
	return 0; // not handled : exception will occur
}

/* Called when a TRAP instruction is encountered */
static int default_trap_instr_callback(int trap)
{
    (void)trap;
    return 0; // not handled : exception will occur
}

/* Called when the program counter changed by a large value */
static unsigned int default_pc_changed_callback_data;
static void default_pc_changed_callback(unsigned int new_pc)
{
	default_pc_changed_callback_data = new_pc;
}

/* Called every time there's bus activity (read/write to/from memory */
static unsigned int default_set_fc_callback_data;
static void default_set_fc_callback(unsigned int new_fc)
{
	default_set_fc_callback_data = new_fc;
}

/* Called every instruction cycle prior to execution */
static void default_instr_hook_callback(unsigned int pc)
{
	(void)pc;
}


#if M68K_EMULATE_ADDRESS_ERROR
	#include <setjmp.h>
	#ifdef _BSD_SETJMP_H
	sigjmp_buf m68ki_aerr_trap;
	#else
	jmp_buf m68ki_aerr_trap;
	#endif
#endif /* M68K_EMULATE_ADDRESS_ERROR */

/* ======================================================================== */
/* ================================= API ================================== */
/* ======================================================================== */

/* Access the internals of the CPU */
unsigned int m68k_get_reg(void* context, m68k_register_t regnum)
{
	m68ki_cpu_core* cpu = context != NULL ?(m68ki_cpu_core*)context : &m68ki_cpu;

	switch(regnum)
	{
		case M68K_REG_D0:	return cpu->dar[0];
		case M68K_REG_D1:	return cpu->dar[1];
		case M68K_REG_D2:	return cpu->dar[2];
		case M68K_REG_D3:	return cpu->dar[3];
		case M68K_REG_D4:	return cpu->dar[4];
		case M68K_REG_D5:	return cpu->dar[5];
		case M68K_REG_D6:	return cpu->dar[6];
		case M68K_REG_D7:	return cpu->dar[7];
		case M68K_REG_A0:	return cpu->dar[8];
		case M68K_REG_A1:	return cpu->dar[9];
		case M68K_REG_A2:	return cpu->dar[10];
		case M68K_REG_A3:	return cpu->dar[11];
		case M68K_REG_A4:	return cpu->dar[12];
		case M68K_REG_A5:	return cpu->dar[13];
		case M68K_REG_A6:	return cpu->dar[14];
		case M68K_REG_A7:	return cpu->dar[15];
		case M68K_REG_PC:	return MASK_OUT_ABOVE_32(cpu->pc);
		case M68K_REG_SR:	return	cpu->t1_flag						|
									cpu->t0_flag						|
									(cpu->s_flag << 11)					|
									(cpu->m_flag << 11)					|
									cpu->int_mask						|
									((cpu->x_flag & XFLAG_SET) >> 4)	|
									((cpu->n_flag & NFLAG_SET) >> 4)	|
									((!cpu->not_z_flag) << 2)			|
									((cpu->v_flag & VFLAG_SET) >> 6)	|
									((cpu->c_flag & CFLAG_SET) >> 8);
		case M68K_REG_SP:	return cpu->dar[15];
		case M68K_REG_USP:	return cpu->s_flag ? cpu->sp[0] : cpu->dar[15];
		case M68K_REG_ISP:	return cpu->s_flag && !cpu->m_flag ? cpu->dar[15] : cpu->sp[4];
		case M68K_REG_MSP:	return cpu->s_flag && cpu->m_flag ? cpu->dar[15] : cpu->sp[6];
		case M68K_REG_SFC:	return cpu->sfc;
		case M68K_REG_DFC:	return cpu->dfc;
		case M68K_REG_VBR:	return cpu->vbr;
		case M68K_REG_CACR:	return cpu->cacr;
		case M68K_REG_CAAR:	return cpu->caar;
		case M68K_REG_PREF_ADDR:	return cpu->pref_addr;
		case M68K_REG_PREF_DATA:	return cpu->pref_data;
		case M68K_REG_PPC:	return MASK_OUT_ABOVE_32(cpu->ppc);
		case M68K_REG_IR:	return cpu->ir;
		case M68K_REG_CPU_TYPE:
			switch(cpu->cpu_type)
			{
				case CPU_TYPE_000:		return (unsigned int)M68K_CPU_TYPE_68000;
				case CPU_TYPE_010:		return (unsigned int)M68K_CPU_TYPE_68010;
				case CPU_TYPE_EC020:	return (unsigned int)M68K_CPU_TYPE_68EC020;
				case CPU_TYPE_020:		return (unsigned int)M68K_CPU_TYPE_68020;
				case CPU_TYPE_040:		return (unsigned int)M68K_CPU_TYPE_68040;
			}
			return M68K_CPU_TYPE_INVALID;
		default:			return 0;
	}
	return 0;
}

void m68k_set_reg(m68k_register_t regnum, unsigned int value)
{
	switch(regnum)
	{
		case M68K_REG_D0:	REG_D[0] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D1:	REG_D[1] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D2:	REG_D[2] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D3:	REG_D[3] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D4:	REG_D[4] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D5:	REG_D[5] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D6:	REG_D[6] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D7:	REG_D[7] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A0:	REG_A[0] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A1:	REG_A[1] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A2:	REG_A[2] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A3:	REG_A[3] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A4:	REG_A[4] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A5:	REG_A[5] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A6:	REG_A[6] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A7:	REG_A[7] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_PC:	m68ki_jump(MASK_OUT_ABOVE_32(value)); return;
		case M68K_REG_SR:	m68ki_set_sr_noint_nosp(value); return;
		case M68K_REG_SP:	REG_SP = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_USP:	if(FLAG_S)
								REG_USP = MASK_OUT_ABOVE_32(value);
							else
								REG_SP = MASK_OUT_ABOVE_32(value);
							return;
		case M68K_REG_ISP:	if(FLAG_S && !FLAG_M)
								REG_SP = MASK_OUT_ABOVE_32(value);
							else
								REG_ISP = MASK_OUT_ABOVE_32(value);
							return;
		case M68K_REG_MSP:	if(FLAG_S && FLAG_M)
								REG_SP = MASK_OUT_ABOVE_32(value);
							else
								REG_MSP = MASK_OUT_ABOVE_32(value);
							return;
		case M68K_REG_VBR:	REG_VBR = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_SFC:	REG_SFC = value & 7; return;
		case M68K_REG_DFC:	REG_DFC = value & 7; return;
		case M68K_REG_CACR:	REG_CACR = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_CAAR:	REG_CAAR = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_PPC:	REG_PPC = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_IR:	REG_IR = MASK_OUT_ABOVE_16(value); return;
		case M68K_REG_CPU_TYPE: m68k_set_cpu_type(value); return;
		default:			return;
	}
}

/* Set the callbacks */
void m68k_set_int_ack_callback(int  (*callback)(int int_level))
{
	CALLBACK_INT_ACK = callback ? callback : default_int_ack_callback;
}

void m68k_set_bkpt_ack_callback(void  (*callback)(unsigned int data))
{
	CALLBACK_BKPT_ACK = callback ? callback : default_bkpt_ack_callback;
}

void m68k_set_reset_instr_callback(void  (*callback)(void))
{
	CALLBACK_RESET_INSTR = callback ? callback : default_reset_instr_callback;
}

void m68k_set_cmpild_instr_callback(void  (*callback)(unsigned int, int))
{
	CALLBACK_CMPILD_INSTR = callback ? callback : default_cmpild_instr_callback;
}

void m68k_set_rte_instr_callback(void  (*callback)(void))
{
	CALLBACK_RTE_INSTR = callback ? callback : default_rte_instr_callback;
}

void m68k_set_tas_instr_callback(int  (*callback)(void))
{
	CALLBACK_TAS_INSTR = callback ? callback : default_tas_instr_callback;
}

void m68k_set_illg_instr_callback(int  (*callback)(int))
{
	CALLBACK_ILLG_INSTR = callback ? callback : default_illg_instr_callback;
}

void m68k_set_trap_instr_callback(int  (*callback)(int))
{
    CALLBACK_TRAP_INSTR = callback ? callback : default_trap_instr_callback;
}

void m68k_set_pc_changed_callback(void  (*callback)(unsigned int new_pc))
{
	CALLBACK_PC_CHANGED = callback ? callback : default_pc_changed_callback;
}

void m68k_set_fc_callback(void  (*callback)(unsigned int new_fc))
{
	CALLBACK_SET_FC = callback ? callback : default_set_fc_callback;
}

void m68k_set_instr_hook_callback(void  (*callback)(unsigned int pc))
{
	CALLBACK_INSTR_HOOK = callback ? callback : default_instr_hook_callback;
}

/* Set the CPU type. */
void m68k_set_cpu_type(unsigned int cpu_type)
{
	switch(cpu_type)
	{
		case M68K_CPU_TYPE_68000:
			CPU_TYPE         = CPU_TYPE_000;
			CPU_ADDRESS_MASK = 0x00ffffff;
			CPU_SR_MASK      = 0xa71f; /* T1 -- S  -- -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[0];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[0];
			CYC_BCC_NOTAKE_B = -2;
			CYC_BCC_NOTAKE_W = 2;
			CYC_DBCC_F_NOEXP = -2;
			CYC_DBCC_F_EXP   = 2;
			CYC_SCC_R_TRUE   = 2;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 3;
			CYC_SHIFT        = 1;
			CYC_RESET        = 132;
			HAS_PMMU	 = 0;
			return;
		case M68K_CPU_TYPE_SCC68070:
			m68k_set_cpu_type(M68K_CPU_TYPE_68010);
			CPU_ADDRESS_MASK = 0xffffffff;
			CPU_TYPE         = CPU_TYPE_SCC070;
			return;
		case M68K_CPU_TYPE_68010:
			CPU_TYPE         = CPU_TYPE_010;
			CPU_ADDRESS_MASK = 0x00ffffff;
			CPU_SR_MASK      = 0xa71f; /* T1 -- S  -- -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[1];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[1];
			CYC_BCC_NOTAKE_B = -4;
			CYC_BCC_NOTAKE_W = 0;
			CYC_DBCC_F_NOEXP = 0;
			CYC_DBCC_F_EXP   = 6;
			CYC_SCC_R_TRUE   = 0;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 3;
			CYC_SHIFT        = 1;
			CYC_RESET        = 130;
			HAS_PMMU	 = 0;
			return;
		case M68K_CPU_TYPE_68EC020:
			CPU_TYPE         = CPU_TYPE_EC020;
			CPU_ADDRESS_MASK = 0x00ffffff;
			CPU_SR_MASK      = 0xf71f; /* T1 T0 S  M  -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[2];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[2];
			CYC_BCC_NOTAKE_B = -2;
			CYC_BCC_NOTAKE_W = 0;
			CYC_DBCC_F_NOEXP = 0;
			CYC_DBCC_F_EXP   = 4;
			CYC_SCC_R_TRUE   = 0;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 2;
			CYC_SHIFT        = 0;
			CYC_RESET        = 518;
			HAS_PMMU	 = 0;
			return;
		case M68K_CPU_TYPE_68020:
			CPU_TYPE         = CPU_TYPE_020;
			CPU_ADDRESS_MASK = 0xffffffff;
			CPU_SR_MASK      = 0xf71f; /* T1 T0 S  M  -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[2];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[2];
			CYC_BCC_NOTAKE_B = -2;
			CYC_BCC_NOTAKE_W = 0;
			CYC_DBCC_F_NOEXP = 0;
			CYC_DBCC_F_EXP   = 4;
			CYC_SCC_R_TRUE   = 0;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 2;
			CYC_SHIFT        = 0;
			CYC_RESET        = 518;
			HAS_PMMU	 = 0;
			return;
		case M68K_CPU_TYPE_68030:
			CPU_TYPE         = CPU_TYPE_030;
			CPU_ADDRESS_MASK = 0xffffffff;
			CPU_SR_MASK      = 0xf71f; /* T1 T0 S  M  -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[3];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[3];
			CYC_BCC_NOTAKE_B = -2;
			CYC_BCC_NOTAKE_W = 0;
			CYC_DBCC_F_NOEXP = 0;
			CYC_DBCC_F_EXP   = 4;
			CYC_SCC_R_TRUE   = 0;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 2;
			CYC_SHIFT        = 0;
			CYC_RESET        = 518;
			HAS_PMMU	       = 1;
			return;
		case M68K_CPU_TYPE_68EC030:
			CPU_TYPE         = CPU_TYPE_EC030;
			CPU_ADDRESS_MASK = 0xffffffff;
			CPU_SR_MASK          = 0xf71f; /* T1 T0 S  M  -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[3];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[3];
			CYC_BCC_NOTAKE_B = -2;
			CYC_BCC_NOTAKE_W = 0;
			CYC_DBCC_F_NOEXP = 0;
			CYC_DBCC_F_EXP   = 4;
			CYC_SCC_R_TRUE   = 0;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 2;
			CYC_SHIFT        = 0;
			CYC_RESET        = 518;
			HAS_PMMU	       = 0;		/* EC030 lacks the PMMU and is effectively a die-shrink 68020 */
			return;
		case M68K_CPU_TYPE_68040:		// TODO: these values are not correct
			CPU_TYPE         = CPU_TYPE_040;
			CPU_ADDRESS_MASK = 0xffffffff;
			CPU_SR_MASK      = 0xf71f; /* T1 T0 S  M  -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[4];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[4];
			CYC_BCC_NOTAKE_B = -2;
			CYC_BCC_NOTAKE_W = 0;
			CYC_DBCC_F_NOEXP = 0;
			CYC_DBCC_F_EXP   = 4;
			CYC_SCC_R_TRUE   = 0;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 2;
			CYC_SHIFT        = 0;
			CYC_RESET        = 518;
			HAS_PMMU	 = 1;
			return;
		case M68K_CPU_TYPE_68EC040: // Just a 68040 without pmmu apparently...
			CPU_TYPE         = CPU_TYPE_EC040;
			CPU_ADDRESS_MASK = 0xffffffff;
			CPU_SR_MASK      = 0xf71f; /* T1 T0 S  M  -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[4];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[4];
			CYC_BCC_NOTAKE_B = -2;
			CYC_BCC_NOTAKE_W = 0;
			CYC_DBCC_F_NOEXP = 0;
			CYC_DBCC_F_EXP   = 4;
			CYC_SCC_R_TRUE   = 0;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 2;
			CYC_SHIFT        = 0;
			CYC_RESET        = 518;
			HAS_PMMU	 = 0;
			return;
		case M68K_CPU_TYPE_68LC040:
			CPU_TYPE         = CPU_TYPE_LC040;
			m68ki_cpu.sr_mask          = 0xf71f; /* T1 T0 S  M  -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			m68ki_cpu.cyc_instruction  = m68ki_cycles[4];
			m68ki_cpu.cyc_exception    = m68ki_exception_cycle_table[4];
			m68ki_cpu.cyc_bcc_notake_b = -2;
			m68ki_cpu.cyc_bcc_notake_w = 0;
			m68ki_cpu.cyc_dbcc_f_noexp = 0;
			m68ki_cpu.cyc_dbcc_f_exp   = 4;
			m68ki_cpu.cyc_scc_r_true   = 0;
			m68ki_cpu.cyc_movem_w      = 2;
			m68ki_cpu.cyc_movem_l      = 2;
			m68ki_cpu.cyc_shift        = 0;
			m68ki_cpu.cyc_reset        = 518;
			HAS_PMMU	       = 1;
			return;
	}
}

/* ========================================================================= */
/* ⭐⭐ IDLE-SKIP: riconoscimento AUTOMATICO del ciclo d'attesa                */
/* ========================================================================= */
/* IL FATTO (misurato da _verify_gfx/cps2_m68k/mk_m68kprof.py su 1944, 1800
   quadri): il 68000 spende 9.994 istruzioni su 16.239 — il 62% — dentro DUE
   istruzioni, `TST.W $921CC4` a 0x0007EE e `BEQ.W -8` a 0x0007F4. Su CPS-2 il
   lavoro lo fa la ISR di vblank e il programma principale aspetta li'.

   L'IDEA, e perche' e' LECITA (non e' un'euristica):
   dentro UNA chiamata a m68k_execute() l'emulatore e' un sistema CHIUSO — il
   gioco chiama m68k_set_irq() solo FRA una fetta e l'altra (cps1944.cpp
   run_frame), e nessun altro pezzo di firmware scrive la memoria del 68000
   mentre la fetta gira. Quindi, se una iterazione completa di un ciclo:
     (a) riporta il PC esattamente dov'era,
     (b) lascia D0-D7/A0-A7 IDENTICI e l'SR INTERO identico,
     (c) non ha eseguito NESSUNA scrittura in memoria,
     (d) ha letto SOLO da ROM o dalla finestra di dati "fermi" dichiarata dal
         gioco (memoria che, dentro la fetta, scrive solo il 68000 stesso),
   allora lo stato di partenza dell'iterazione successiva e' bit per bit quello
   della precedente: e' un PUNTO FISSO. Le iterazioni rimanenti non possono
   cambiare nulla; l'unica cosa che producono e' il consumo dei cicli.

   ⛔⭐ MA NON SI AZZERANO I CICLI RESIDUI — e questa e' la parte che l'harness
   A/B ha fatto uscire allo scoperto. La fetta finisce quando i cicli si
   esauriscono, e questo capita quasi sempre IN MEZZO a un'iterazione: senza
   idle-skip il PC di fine fetta e' quello della seconda istruzione del ciclo,
   con l'idle-skip "azzera e vai" sarebbe la prima. Il gioco si comporterebbe
   uguale, ma le due esecuzioni NON sarebbero piu' identiche — e la regola di
   equivalenza dice impronte uguali, non "quasi uguali" (primo giro dell'A/B:
   divergenza gia' al quadro 0).
   La cura e' esatta: si misura `costo` = i cicli di UNA iterazione (dalla cima
   alla cima) e si consumano solo `k * costo` cicli, con k scelto in modo che
   restino SEMPRE almeno `costo` cicli. Il ciclo poi finisce di girare da solo,
   per davvero, per un'iterazione o due. Siccome dalla cima il processo e'
   deterministico e periodico di periodo `costo`, togliere un multiplo esatto
   del periodo lascia il punto d'arresto IDENTICO: stesso PC, stesso SR.

   ⭐ Il salto NON puo' superare il confine della fetta ne' saltare l'IRQ di
   vblank per COSTRUZIONE: l'unica cosa che fa e' consumare cicli di QUESTA
   chiamata: m68k_execute() ritorna, run_frame() fa il latch dell'objram, alza
   IPL1 e chiama la fetta successiva.

   PERCHE' E' ROBUSTO e non ha indirizzi scritti a mano: non si riconosce un
   ciclo "per nome" (0x0007EE) ma per la PROPRIETA' che lo rende un'attesa. Se
   la ROM cambia, se il gioco usa un altro ciclo, se ne usa due, funziona
   uguale; e se un ciclo NON ha quella proprieta' non scatta mai.

   I FILTRI (tutti conservativi: al piu' si perde un'ottimizzazione):
   - si arma solo su un salto ALL'INDIETRO corto (<= M68K_IDLE_SPAN byte) fatto
     da un Bcc/BRA (opcode 0x6xxx). I cicli di lavoro stretti sono quasi sempre
     DBcc, che decrementa un registro e quindi fallirebbe comunque;
   - l'iterazione dev'essere corta (<= M68K_IDLE_ISTR istruzioni): questo chiude
     anche il caso di un'eccezione presa in mezzo (address error), che porta il
     PC altrove e non torna mai in cima;
   - non si salta se resta un interrupt pendente (CPU_INT_LEVEL) o se la CPU e'
     STOPPED: sono gli unici due modi in cui il core puo' uscire dal punto fisso
     per conto suo;
   - una candidata bocciata finisce in una tabellina di 4 PC "da non riprovare",
     azzerata a ogni fetta: senza, un ciclo di lavoro con salto all'indietro si
     farebbe verificare a ogni giro.                                          */
#define M68K_IDLE_SPAN 64      /* byte: quanto indietro puo' saltare il ciclo  */
#define M68K_IDLE_ISTR 16      /* istruzioni massime di UNA iterazione         */
#define M68K_IDLE_NOGO  4      /* candidate bocciate ricordate per fetta       */

static uint m68ki_idle_pc0;                  /* cima del ciclo candidato       */
static uint m68ki_idle_nogo[M68K_IDLE_NOGO]; /* candidate gia' bocciate        */
static int  m68ki_idle_nogo_n;
static int  m68ki_idle_arm;                  /* 1 = iterazione in verifica     */
static int  m68ki_idle_istr;                 /* istruzioni dell'iterazione     */
static int  m68ki_idle_cic0;                 /* cicli residui alla cima        */
static uint m68ki_idle_dar[16];              /* D0-D7/A0-A7 alla cima          */
static uint m68ki_idle_sr;                   /* SR intero alla cima            */

/* azzerata all'ingresso di ogni m68k_execute(): la memoria delle candidate
   bocciate vale solo dentro la fetta, perche' e' li' che il sistema e' chiuso */
static void m68ki_idle_fetta(void)
{
	m68ki_idle_arm    = 0;
	m68ki_idle_watch  = 0;
	m68ki_idle_bad    = 0;
	m68ki_idle_nogo_n = 0;
}

static void m68ki_idle_step(void)
{
	int i;

	if(m68ki_idle_arm)
	{
		if(REG_PC == m68ki_idle_pc0)
		{
			/* iterazione COMPLETA: lo stato di adesso contro quello della cima */
			int uguale = !m68ki_idle_bad && !CPU_STOPPED && CPU_INT_LEVEL == 0
			             && m68ki_get_sr() == m68ki_idle_sr;
			for(i = 0; uguale && i < 16; i++)
				if(REG_DA[i] != m68ki_idle_dar[i]) uguale = 0;
			m68ki_idle_arm   = 0;
			m68ki_idle_watch = 0;
			if(uguale)
			{
				/* `costo` = cicli di UNA iterazione, misurati (non stimati) */
				const int costo = m68ki_idle_cic0 - GET_CYCLES();
				if(costo > 0)
				{
					/* k = iterazioni da togliere, lasciandone SEMPRE almeno una
					   intera da eseguire davvero: cosi' il punto d'arresto e'
					   quello vero, non uno vicino. */
					const int k = GET_CYCLES() / costo - 1;
					if(k > 0)
					{
						USE_CYCLES(k * costo);
						m68ki_idle_cicli += (unsigned long)(k * costo);
						m68ki_idle_volte++;
					}
				}
			}
			else if(m68ki_idle_nogo_n < M68K_IDLE_NOGO)
				m68ki_idle_nogo[m68ki_idle_nogo_n++] = m68ki_idle_pc0;
			return;
		}
		if(++m68ki_idle_istr >= M68K_IDLE_ISTR)   /* troppo lunga: lascia stare */
		{
			m68ki_idle_arm   = 0;
			m68ki_idle_watch = 0;
			if(m68ki_idle_nogo_n < M68K_IDLE_NOGO)
				m68ki_idle_nogo[m68ki_idle_nogo_n++] = m68ki_idle_pc0;
		}
		return;
	}

	/* non armato: si cerca un Bcc/BRA che salta ALL'INDIETRO di poco */
	if((REG_IR & 0xF000) != 0x6000) return;
	if(REG_PC >= REG_PPC || (REG_PPC - REG_PC) > M68K_IDLE_SPAN) return;
	if(CPU_STOPPED) return;
	for(i = 0; i < m68ki_idle_nogo_n; i++)
		if(m68ki_idle_nogo[i] == REG_PC) return;

	m68ki_idle_pc0 = REG_PC;
	for(i = 0; i < 16; i++) m68ki_idle_dar[i] = REG_DA[i];
	m68ki_idle_sr    = m68ki_get_sr();
	m68ki_idle_cic0  = GET_CYCLES();
	m68ki_idle_istr  = 0;
	m68ki_idle_bad   = 0;
	m68ki_idle_watch = 1;
	m68ki_idle_arm   = 1;
}

/* Execute some instructions until we use up num_cycles clock cycles */
/* ASG: removed per-instruction interrupt checks */
int m68k_execute(int num_cycles)
{
	/* eat up any reset cycles */
	if (RESET_CYCLES) {
	    int rc = RESET_CYCLES;
	    RESET_CYCLES = 0;
	    num_cycles -= rc;
	    if (num_cycles <= 0)
		return rc;
	}

	/* Set our pool of clock cycles available */
	SET_CYCLES(num_cycles);
	m68ki_initial_cycles = num_cycles;

	/* ⭐⭐ IDLE-SKIP: lo stato del riconoscitore vale UNA fetta (vedi sopra).
	   Con l'idle-skip spento (`m68ki_idle_hi` = 0) qui non succede nulla. */
	if(m68ki_idle_hi) m68ki_idle_fetta();

	/* See if interrupts came in */
	m68ki_check_interrupts();

	/* Make sure we're not stopped */
	if(!CPU_STOPPED)
	{
		/* Return point if we had an address error */
		m68ki_set_address_error_trap(); /* auto-disable (see m68kcpu.h) */

		m68ki_check_bus_error_trap();

		/* Main loop.  Keep going until we run out of clock cycles */
		do
		{
			int i;
			/* Set tracing accodring to T1. (T0 is done inside instruction) */
			m68ki_trace_t1(); /* auto-disable (see m68kcpu.h) */

			/* Set the address space for reads */
			m68ki_use_data_space(); /* auto-disable (see m68kcpu.h) */

			/* Call external hook to peek at CPU */
			m68ki_instr_hook(REG_PC); /* auto-disable (see m68kcpu.h) */

			/* Record previous program counter */
			REG_PPC = REG_PC;

			/* ⛔⭐ PATCH CABINATONE — questa copia (16 load + 16 store per OGNI
			 * istruzione emulata) serve SOLO a m68ki_exception_bus_error(), che
			 * si raggiunge unicamente da m68k_pulse_bus_error(). In tutto il
			 * binario NESSUN gioco la chiama, quindi dar_save non viene mai
			 * riletto: e' lavoro puro a perdere.
			 * Conto su 1944: ~16.500 istruzioni/frame x 32 accessi = ~528.000
			 * accessi in memoria in meno per frame (~1,7 ms a 360 MHz).
			 * Vale per TUTTI i giochi 68000 (outrun, ghouls, neogeo, 1944).
			 * Per riattivarla: mettere 1 qui sotto. */
#define M68K_SAVE_DAR_PER_ISTRUZIONE 0
#if M68K_SAVE_DAR_PER_ISTRUZIONE
			for (i = 15; i >= 0; i--){
				REG_DA_SAVE[i] = REG_DA[i];
			}
#else
			(void)i;
#endif

			/* Read an instruction and call its handler */
			REG_IR = m68ki_read_imm_16();
			m68ki_instruction_jump_table[REG_IR]();
			USE_CYCLES(CYC_INSTRUCTION[REG_IR]);

			/* Trace m68k_exception, if necessary */
			m68ki_exception_if_trace(); /* auto-disable (see m68kcpu.h) */

			/* ⭐⭐ IDLE-SKIP. Con l'idle-skip spento e' una load + un salto mai
			   preso: nessun altro gioco a 68000 del binario cambia nulla. */
			if(m68ki_idle_hi) m68ki_idle_step();
		} while(GET_CYCLES() > 0);

		/* set previous PC to current PC for the next entry into the loop */
		REG_PPC = REG_PC;
	}
	else
		SET_CYCLES(0);

	/* return how many clocks we used */
	return m68ki_initial_cycles - GET_CYCLES();
}


int m68k_cycles_run(void)
{
	return m68ki_initial_cycles - GET_CYCLES();
}

int m68k_cycles_remaining(void)
{
	return GET_CYCLES();
}

/* Change the timeslice */
void m68k_modify_timeslice(int cycles)
{
	m68ki_initial_cycles += cycles;
	ADD_CYCLES(cycles);
}


void m68k_end_timeslice(void)
{
	m68ki_initial_cycles -= GET_CYCLES();
	SET_CYCLES(0);
}


/* ASG: rewrote so that the int_level is a mask of the IPL0/IPL1/IPL2 bits */
/* KS: Modified so that IPL* bits match with mask positions in the SR
 *     and cleaned out remenants of the interrupt controller.
 */
void m68k_set_irq(unsigned int int_level)
{
	uint old_level = CPU_INT_LEVEL;
	CPU_INT_LEVEL = int_level << 8;

	/* A transition from < 7 to 7 always interrupts (NMI) */
	/* Note: Level 7 can also level trigger like a normal IRQ */
	if(old_level != 0x0700 && CPU_INT_LEVEL == 0x0700)
		m68ki_cpu.nmi_pending = TRUE;
}

void m68k_set_virq(unsigned int level, unsigned int active)
{
	uint state = m68ki_cpu.virq_state;
	uint blevel;

	if(active)
		state |= 1 << level;
	else
		state &= ~(1 << level);
	m68ki_cpu.virq_state = state;

	for(blevel = 7; blevel > 0; blevel--)
		if(state & (1 << blevel))
			break;
	m68k_set_irq(blevel);
}

unsigned int m68k_get_virq(unsigned int level)
{
	return (m68ki_cpu.virq_state & (1 << level)) ? 1 : 0;
}

void m68k_init(void)
{
	/* ⛔⛔⛔ ERA `static uint emulation_initialized = 0;`, cioe' la tabella dei
	   salti si costruiva UNA VOLTA SOLA per accensione. Vedi la nota lunga sopra
	   `m68k_set_tables()` in m68kops.c: qui la tabella la alloca il CHIAMANTE in
	   PSRAM e la libera all'uscita dal gioco, quindi al rilancio ne arriva una
	   nuova e azzerata che la guardia statica impediva di ricostruire — 65.536
	   puntatori a NULL e salto a 0 alla prima istruzione emulata.
	   Ora il flag lo alza `m68k_set_tables()`: la tabella si ricostruisce esatta-
	   mente quando la memoria cambia, e non una volta di piu'. */
	extern int m68ki_tables_da_costruire;

	if(m68ki_tables_da_costruire)
	{
		m68ki_build_opcode_table();
		m68ki_tables_da_costruire = 0;
	}

	m68k_set_int_ack_callback(NULL);
	m68k_set_bkpt_ack_callback(NULL);
	m68k_set_reset_instr_callback(NULL);
	m68k_set_cmpild_instr_callback(NULL);
	m68k_set_rte_instr_callback(NULL);
	m68k_set_tas_instr_callback(NULL);
	m68k_set_illg_instr_callback(NULL);
    m68k_set_trap_instr_callback(NULL);
	m68k_set_pc_changed_callback(NULL);
	m68k_set_fc_callback(NULL);
	m68k_set_instr_hook_callback(NULL);
}

/* Trigger a Bus Error exception */
void m68k_pulse_bus_error(void)
{
	m68ki_exception_bus_error();
}

/* Pulse the RESET line on the CPU */
void m68k_pulse_reset(void)
{
	/* Disable the PMMU on reset */
	m68ki_cpu.pmmu_enabled = 0;

	/* Clear all stop levels and eat up all remaining cycles */
	CPU_STOPPED = 0;
	SET_CYCLES(0);

	CPU_RUN_MODE = RUN_MODE_BERR_AERR_RESET;
	CPU_INSTR_MODE = INSTRUCTION_YES;

	/* Turn off tracing */
	FLAG_T1 = FLAG_T0 = 0;
	m68ki_clear_trace();
	/* Interrupt mask to level 7 */
	FLAG_INT_MASK = 0x0700;
	CPU_INT_LEVEL = 0;
	m68ki_cpu.virq_state = 0;
	/* Reset VBR */
	REG_VBR = 0;
	/* Go to supervisor mode */
	m68ki_set_sm_flag(SFLAG_SET | MFLAG_CLEAR);

	/* Invalidate the prefetch queue */
#if M68K_EMULATE_PREFETCH
	/* Set to arbitrary number since our first fetch is from 0 */
	CPU_PREF_ADDR = 0x1000;
#endif /* M68K_EMULATE_PREFETCH */

	/* Read the initial stack pointer and program counter */
	m68ki_jump(0);
	REG_SP = m68ki_read_imm_32();
	REG_PC = m68ki_read_imm_32();
	m68ki_jump(REG_PC);

	CPU_RUN_MODE = RUN_MODE_NORMAL;

	RESET_CYCLES = CYC_EXCEPTION[EXCEPTION_RESET];
}

/* Pulse the HALT line on the CPU */
void m68k_pulse_halt(void)
{
	CPU_STOPPED |= STOP_LEVEL_HALT;
}

/* Get and set the current CPU context */
/* This is to allow for multiple CPUs */
unsigned int m68k_context_size(void)
{
	return sizeof(m68ki_cpu_core);
}

unsigned int m68k_get_context(void* dst)
{
	if(dst) *(m68ki_cpu_core*)dst = m68ki_cpu;
	return sizeof(m68ki_cpu_core);
}

void m68k_set_context(void* src)
{
	if(src) m68ki_cpu = *(m68ki_cpu_core*)src;
}

/* ======================================================================== */
/* ============================== MAME STUFF ============================== */
/* ======================================================================== */

#if M68K_COMPILE_FOR_MAME == M68K_OPT_ON

static struct {
	UINT16 sr;
	UINT8 stopped;
	UINT8 halted;
} m68k_substate;

static void m68k_prepare_substate(void)
{
	m68k_substate.sr = m68ki_get_sr();
	m68k_substate.stopped = (CPU_STOPPED & STOP_LEVEL_STOP) != 0;
	m68k_substate.halted  = (CPU_STOPPED & STOP_LEVEL_HALT) != 0;
}

static void m68k_post_load(void)
{
	m68ki_set_sr_noint_nosp(m68k_substate.sr);
	CPU_STOPPED = m68k_substate.stopped ? STOP_LEVEL_STOP : 0
		        | m68k_substate.halted  ? STOP_LEVEL_HALT : 0;
	m68ki_jump(REG_PC);
}

void m68k_state_register(const char *type, int index)
{
	/* Note, D covers A because the dar array is common, REG_A=REG_D+8 */
	state_save_register_item_array(type, index, REG_D);
	state_save_register_item(type, index, REG_PPC);
	state_save_register_item(type, index, REG_PC);
	state_save_register_item(type, index, REG_USP);
	state_save_register_item(type, index, REG_ISP);
	state_save_register_item(type, index, REG_MSP);
	state_save_register_item(type, index, REG_VBR);
	state_save_register_item(type, index, REG_SFC);
	state_save_register_item(type, index, REG_DFC);
	state_save_register_item(type, index, REG_CACR);
	state_save_register_item(type, index, REG_CAAR);
	state_save_register_item(type, index, m68k_substate.sr);
	state_save_register_item(type, index, CPU_INT_LEVEL);
	state_save_register_item(type, index, m68k_substate.stopped);
	state_save_register_item(type, index, m68k_substate.halted);
	state_save_register_item(type, index, CPU_PREF_ADDR);
	state_save_register_item(type, index, CPU_PREF_DATA);
	state_save_register_func_presave(m68k_prepare_substate);
	state_save_register_func_postload(m68k_post_load);
}

#endif /* M68K_COMPILE_FOR_MAME */

/* ======================================================================== */
/* ============================== END OF FILE ============================= */
/* ======================================================================== */
