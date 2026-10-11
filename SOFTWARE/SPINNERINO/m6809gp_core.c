// ============================================================================
// m6809gp_core.c — wrapper ROOT del core 6809 di GAPLUS (cpus/m6809gp/).
//
// Arduino IDE compila SOLO i sorgenti nella cartella dello sketch (root): il .c
// nella sottocartella verrebbe ignorato. Questo file lo tira dentro, compilato
// come C, con i simboli gia' rinominati gp6809_* da m6809gp.h — quindi non
// collide col core 6809 dei giochi della griglia (cpus/m6809/, incluso da
// SPINNERINO.ino). Le tre callback di memoria (gp6809_read / gp6809_write /
// gp6809_read_opcode) le definisce gaplus.cpp.
// Stessa forma di m6809_core.c del CABINATONE.
// ============================================================================
#include "config.h"
#ifdef ENABLE_GAPLUS
#include "cpus/m6809gp/m6809gp.c"
#endif
