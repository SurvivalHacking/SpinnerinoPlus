#ifndef M68KOPS__HEADER
#define M68KOPS__HEADER

/* ======================================================================== */
/* ============================ OPCODE HANDLERS =========================== */
/* ======================================================================== */


/* Build the opcode handler table */
void m68ki_build_opcode_table(void);

/* PATCH CABINATONE: puntatori, non array — le tabelle (576 KB) stanno in PSRAM
   e le alloca il chiamante con m68k_set_tables(). Vedi la nota in m68kops.c. */
extern void (**m68ki_instruction_jump_table)(void); /* opcode handler jump table */
extern unsigned char (*m68ki_cycles)[0x10000];

/* Da chiamare PRIMA di m68k_init(): jump_table = 0x10000 puntatori a funzione
   (256 KB), cycles = 5 x 0x10000 byte (320 KB). Entrambi in PSRAM. */
void m68k_set_tables(void *jump_table, void *cycles);


/* ======================================================================== */
/* ============================== END OF FILE ============================= */
/* ======================================================================== */

#endif /* M68KOPS__HEADER */


