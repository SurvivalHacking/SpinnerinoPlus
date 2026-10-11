// ============================================================================
// boblbobl_snd.cpp — TU SEPARATA per i core audio di Bubble Bobble.
//
// fmopl.c (YM3526 OPL) ed emu2149.c (PSG/SSG) sono compilati QUI, in una
// translation unit distinta dal .ino, perche' fmopl.c e fm.c (YM2203, incluso
// nel .ino) condividono macro/funzioni static dello stesso lignaggio Burczynski
// e collidono se finiscono nella stessa TU.
//
// boblbobl.cpp (nel .ino) chiama ym3526_*/PSG_* tramite gli header: il linker
// risolve i simboli da questa TU. I simboli esterni (ym3526_*, PSG_*) NON
// collidono con quelli di fm.c (ym2203_*).
// ============================================================================
#include "config.h"
#include "esp_heap_caps.h"   // heap_caps_malloc(MALLOC_CAP_SPIRAM) per le tabelle OPL (fmopl.c)

#ifdef ENABLE_BOBLBOBL
// I core C audio inizializzano tabelle da double verso interi (UINT32/UINT8):
// in C++ (gnu++2b) e' "narrowing" = errore. Sopprimiamo solo per questi include.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
#include "machines/boblbobl/snd/fmopl.c"
#include "machines/boblbobl/snd/emu2149.c"
#pragma GCC diagnostic pop
#endif
