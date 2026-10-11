// ============================================================================
// machines/quester/quester.cpp — STUB Quester (Namco 1987)
// Solo logo nel menu, nessun gameplay finche' la ROM non e' integrata.
// ============================================================================
#include "quester.h"

#ifdef ENABLE_QUESTER
#include "quester_logo.h"

const unsigned short *Quester::logo(void) {
  return quester_logo;
}

#endif // ENABLE_QUESTER
