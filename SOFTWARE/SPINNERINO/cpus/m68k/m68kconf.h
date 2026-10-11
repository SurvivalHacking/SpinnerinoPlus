/* ============================================================================
 * m68kconf.h — configurazione Musashi per CABINATONE (Ghouls'n Ghosts, CPS-1).
 *
 * SOLO 68000: niente 010/020/030/040, niente PMMU, niente trace/prefetch.
 * Ogni opzione accesa qui si paga a OGNI istruzione emulata.
 *
 * ⚠️ Questo file DEVE stare nella stessa cartella di m68kcpu.h: gli #include ""
 *    cercano prima la directory del file che include, quindi una copia messa
 *    altrove verrebbe ignorata in silenzio (errore gia' pagato nell'harness).
 * ============================================================================ */
#ifndef M68KCONF__HEADER
#define M68KCONF__HEADER

#define M68K_OPT_OFF             0
#define M68K_OPT_ON              1
#define M68K_OPT_SPECIFY_HANDLER 2

/* Le callback usate dalle macro qui sotto vanno DICHIARATE qui: m68kcpu.h le
   espande dentro il core, che altrimenti le vedrebbe implicite (errore in C99).
   ⚠️ Dal 2026-07-26 l'ack NON e' piu' quello di ghouls: passa da m68k_bus.cpp,
   che smista al gioco attivo (ghouls / outrun). Vedi m68k_bus.h. */
int m68k_bus_int_ack(int level);

#define M68K_COMPILE_FOR_MAME      M68K_OPT_OFF

/* solo 68000 */
#define M68K_EMULATE_010            M68K_OPT_OFF
#define M68K_EMULATE_EC020          M68K_OPT_OFF
#define M68K_EMULATE_020            M68K_OPT_OFF
#define M68K_EMULATE_030            M68K_OPT_OFF
#define M68K_EMULATE_040            M68K_OPT_OFF

/* ⭐ ACCESO per il CPS-2 (1944), che cifra il PROGRAMMA: le istruzioni
 * vanno prese dalla ROM decifrata e i dati da quella cifrata. I cinque
 * le cinque m68k_read_immediate / m68k_read_pcrelative stanno in
 * m68k_bus.cpp e,
 * se il modulo non dichiara uno spazio opcode, inoltrano alle letture
 * normali: per Out Run, Ghouls e NeoGeo NON cambia nulla (FC,
 * ADDRESS_ERROR, PREFETCH e PMMU sono tutti OFF, quindi la vecchia macro
 * si riduceva gia' a m68k_read_memory_*). */
#define M68K_SEPARATE_READS         M68K_OPT_ON
#define M68K_SIMULATE_PD_WRITES     M68K_OPT_OFF

/* IRQ2 di vblank (IPL1). L'ack azzera la linea, come irqack_r di cps1.cpp:
   il PAL della scheda risponde VPA e cancella IPL1/IPL2. */
#define M68K_EMULATE_INT_ACK        M68K_OPT_SPECIFY_HANDLER
#define M68K_INT_ACK_CALLBACK(A)    m68k_bus_int_ack(A)

#define M68K_EMULATE_BKPT_ACK       M68K_OPT_OFF
#define M68K_BKPT_ACK_CALLBACK()    your_bkpt_ack_handler_function()

#define M68K_EMULATE_TRACE          M68K_OPT_OFF

#define M68K_EMULATE_RESET          M68K_OPT_OFF
#define M68K_RESET_CALLBACK()       your_reset_handler_function()

#define M68K_CMPILD_HAS_CALLBACK    M68K_OPT_OFF
#define M68K_CMPILD_CALLBACK(v,r)   your_cmpild_handler_function(v,r)

#define M68K_RTE_HAS_CALLBACK       M68K_OPT_OFF
#define M68K_RTE_CALLBACK()         your_rte_handler_function()

#define M68K_TAS_HAS_CALLBACK       M68K_OPT_OFF
#define M68K_TAS_CALLBACK()         your_tas_handler_function()

#define M68K_ILLG_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_ILLG_CALLBACK(opcode)  your_op_illg_handler_function(opcode)

#define M68K_TRAP_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_TRAP_CALLBACK(trap)    your_op_trap_handler_function(trap)

#define M68K_EMULATE_FC             M68K_OPT_OFF
#define M68K_SET_FC_CALLBACK(A)     your_set_fc_handler_function(A)

#define M68K_MONITOR_PC             M68K_OPT_OFF
#define M68K_SET_PC_CALLBACK(A)     your_pc_changed_handler_function(A)

#define M68K_INSTRUCTION_HOOK       M68K_OPT_OFF
#define M68K_INSTRUCTION_CALLBACK(pc) your_instruction_hook_function(pc)

#define M68K_EMULATE_PREFETCH       M68K_OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR  M68K_OPT_OFF

#define M68K_LOG_ENABLE             M68K_OPT_OFF
#define M68K_LOG_1010_1111          M68K_OPT_OFF
#define M68K_LOG_TRAP               M68K_OPT_OFF
#define M68K_LOG_FILEHANDLE         some_file_handle

#define M68K_EMULATE_PMMU           M68K_OPT_OFF

/* ⚠️ DEVE restare ON: con OFF il softfloat incluso da m68kcpu.h (bits64 -> 32
   bit) non compila proprio. Sul 68000 i percorsi a 64 bit sono morti (servono
   ai 020+), quindi non costa nulla in esecuzione. */
#define M68K_USE_64_BIT             M68K_OPT_ON

#endif /* M68KCONF__HEADER */
