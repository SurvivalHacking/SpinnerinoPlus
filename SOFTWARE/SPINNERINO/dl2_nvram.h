#pragma once
// dl2_nvram.h - EEPROM National 9536 di Dragon's Lair II, 64 word da 16 bit.
// Ricavata dal file NVRAM di daphne (ram/lair2_319.gz) e RICONFIGURATA:
//   word[10] = word[11] = 10 -> 10 VITE
//   ⛔ la nota vecchia diceva "il ROM accetta fino a 9": FALSO, mai verificato.
//   PROVATO 2026-07-19 sul ROM vero nel simulatore con word[10]=word[11]=10:
//   il contatore vite in RAM (0x156AC) parte da **10** e cala 10->9->8...
//   regolarmente fino a 0. NB: il ROM non scrive mai "10   LIVES" a schermo
//   perche' annuncia le vite solo DOPO una morte (quando sono gia' 9); l'HUD
//   pero' legge la RAM, quindi il 10 lo mostra.
//   word[51] = 1             -> 1 CREDITO per giocare (di fabbrica erano 2)
//   word[09] = continue, word[12] = crediti per moneta, word[17..46] = record
// Mappa ricavata loggando quali word il ROM LEGGE e forzandone una alla volta.
// La word[00] sembra un checksum ma il ROM NON la verifica al boot (misurato).
#include <stdint.h>

static const uint16_t dl2_nvram_default[64] = {
  0x1241, 0x0000, 0x0000, 0x0000, 0x00D9, 0x0001, 0x0000, 0x0000,
  0x0000, 0x0002, 0x000A, 0x000A, 0x0001, 0x0001, 0x0000, 0xFFFF,
  0x0000, 0x534D, 0x0059, 0x0063, 0x544A, 0x004D, 0x003C, 0x4B42,
  0x004D, 0x0037, 0x4152, 0x0050, 0x0032, 0x4F50, 0x0050, 0x001E,
  0x4F52, 0x004B, 0x0019, 0x5248, 0x004F, 0x0014, 0x4257, 0x0047,
  0x000D, 0x505A, 0x0042, 0x000A, 0x5942, 0x0045, 0x0009, 0x0001,
  0x0000, 0x0000, 0x0000, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};