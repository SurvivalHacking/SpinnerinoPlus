#pragma once
#include <stdint.h>
#include <stddef.h>
// ============================================================================
// qsound_hle — Capcom DL-1425 (QSound) ad alto livello, senza il DSP16A.
// Il core e' di superctr / Valley Bell (libvgm qsound_ctr.c); qui c'e' solo
// l'involucro. Vedi la testa di qsound_hle.c.
//
// USO (lato Z80, cps1.cpp qsound_sub_map):
//   0xD000 byte alto del dato · 0xD001 byte basso · 0xD002 = INDIRIZZO del
//   registro DSP, ed e' questa scrittura che fa `qs_hle_write(chip, reg, dato)`
//   0xD007 in lettura = `qs_hle_ready()` (0x00 occupato, 0x80 pronto)
// ============================================================================
#define QS_HLE_CLOCK 60000000     // clock del DSP
#define QS_HLE_DIV   2496         // 60 MHz / 2496 = 24038,46 campioni al secondo

struct qsound_chip;

#ifdef __cplusplus
extern "C" {
#endif

// quanto spazio vuole lo stato (~8 KB): si alloca fuori, cosi' il chiamante
// decide RAM interna o PSRAM
size_t  qs_hle_size(void);

void    qs_hle_init (struct qsound_chip *chip, const uint8_t *rom, uint32_t rom_len);
void    qs_hle_reset(struct qsound_chip *chip);
void    qs_hle_write(struct qsound_chip *chip, uint8_t address, uint16_t data);
uint8_t qs_hle_ready(struct qsound_chip *chip);

// genera `frames` campioni STEREO interlacciati a `out_rate`, ricampionando
// dai 24038,46 Hz del chip
void    qs_hle_render(struct qsound_chip *chip, int16_t *stereo, int frames,
                      uint32_t out_rate);

#ifdef __cplusplus
}
#endif
