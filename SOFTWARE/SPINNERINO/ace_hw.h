#pragma once
// ============================================================================
// ace_hw.h — Space Ace ROM-driven (Cinematronics Z80 + LD-V1000).
//   M1 BRING-UP: emula il ROM vero (sa_a3_u1..u5) e mostra un overlay diagnostico
//   (PC, comandi LD-V1000, SEEK/CUR). Serve a confermare che il ROM parte, fa
//   EI/IM1 e comanda il disco (SEARCH sul frame attract + PLAY). Video/input/audio
//   nelle milestone successive (M2..M4).
// ============================================================================
#ifdef __cplusplus
extern "C" {
#endif

void ace_hw_start(void);   // avvia il task M1 (emulazione + overlay)

#ifdef __cplusplus
}
#endif
