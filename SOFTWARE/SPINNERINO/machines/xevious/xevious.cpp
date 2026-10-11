#include "xevious.h"
// i due campioni delle esplosioni (Namco 54XX non emulato): li genera
// PY\convert_xevious_boom.py da Super Xevious NES. Vedi xevious.h.
#include "xevious_boom.h"

// ============================================================================
// Xevious — porting SPINNERINO. Logica di emulazione/render IDENTICA a GALAGONE
// 2.8VPE; l'unica differenza e' la GEOMETRIA: il render nativo (orientamento
// GALAGONE, 224x224, stride 224) viene scritto in xev_full e poi render_row()
// lo traspone nel framebuffer 256 di SPINNERINO. E' la STESSA trasposizione di
// galaga.cpp (compensa la rotazione 90 del pannello SPINNERINO vs GALAGONE,
// uguale per tutti i giochi perche' il buffer nativo ha lo stesso layout):
//   fb_x = gfb_y + XEV_X_OFF      (riga nativa -> asse lungo 256, centrato)
//   fb_y = 223 - gfb_x           (colonna nativa, reversed)
// ============================================================================
#define XEV_NW     224  // larghezza nativa (gfb_x = colonne) = stride di xev_full
#define XEV_NH     288  // altezza nativa  (gfb_y = righe) — Xevious e' 224x288
#define XEV_Y_CROP 0    // mostra gfb_y 0..255 (TIENE l'HUD in alto, taglia 32 in basso)

void xevious::reset() {
  machineBase::reset();
  for(int i = 0; i < 3; i++)
    cpu[i].SP.W = 0x0000;

  sub_cpu_reset = 1;
  scrollBgX = 0; scrollBgY = 0;
  scrollFgX = 0; scrollFgY = 0;
  flipScreen = 0;
  memset(bs_reg, 0, sizeof(bs_reg));
  namco_command = 0;
  namco_mode = 0;
  namco_nmi_counter = 0;
  namco_credit = 0;
  memset(namco_customio, 0, sizeof(namco_customio));
  watchdog_counter = 0;
  demoActive = false;
  demoFrame = 0;
  coinInserted = false;

  // ⛔⛔ STATO RESIDUO FRA UNA PARTITA E L'ALTRA (fix 2026-08-04).
  // Sintomo: uscendo dal gioco e rilanciandolo SENZA spegnere lo SPINNERINO,
  // Xevious ci metteva molto ad arrivare all'attract e ci arrivava col LOGO
  // DECENTRATO; dopo un power-cycle era tutto a posto. La differenza fra i due
  // casi e' esattamente questa: allo spegnimento i membri tornano a zero (BSS),
  // a reset() no — e questi non venivano ripristinati da nessuno.
  //
  //  • watchdog_feed_count — e' il contatore del "kickstart" in wrZ80(0x6830):
  //    game_started scatta quando supera 120 (~2 s di feed regolari, il tempo
  //    che la ROM impiega a fare il POST). Al RIAVVIO valeva gia' migliaia, per
  //    cui game_started diventava 1 al PRIMISSIMO feed, cioe' DENTRO il POST.
  //    Da li' in poi, tutto insieme: il bypass della custom 51xx in namco_read
  //    (`!game_started && namco_command == 0xb1`) era gia' spento, la sub CPU
  //    veniva sganciata dal reset in anticipo e gli IRQ partivano fuori tempo.
  //    Il POST non finiva -> scattava il watchdog di run_frame dopo 120 frame
  //    -> le CPU ripartivano ma con la RAM SPORCA e i registri di scroll
  //    (0xD000-0xD07F) mai riazzerati: attract in forte ritardo e piano FG
  //    fuori registro = il logo decentrato.
  //  • irq_enable[] — machineBase::reset() non lo tocca: ereditava {1,1,..}
  //    dalla partita precedente, quindi RST38 gia' al primo frame.
  //  • coinReported/startReported/prevKeymask — edge dei tasti della partita
  //    precedente, letti come pressioni nuove.
  //
  // ✅ Azzerati questi, il riavvio a caldo segue lo STESSO percorso
  //    dell'accensione, che e' quello confermato buono sul device.
  // ⛔ NON spostare questi azzeramenti in machineBase::reset(): li' toccherebbe
  //    tutti gli altri giochi, che oggi funzionano.
  watchdog_feed_count = 0;
  memset(irq_enable, 0, sizeof(irq_enable));
  coinReported  = false;
  startReported = false;
  prevKeymask   = 0;

  // Alloca lazy il buffer nativo intero in PSRAM (come phoenix: niente alloc al
  // boot, solo quando Xevious viene selezionato).
  if (!xev_full)
    xev_full = (unsigned short*)ps_malloc(XEV_NW * XEV_NH * sizeof(unsigned short));
}

unsigned char xevious::opZ80(unsigned short Addr) {
  if(current_cpu == 0 && Addr == 0x01DD) return 0x18;
  if(Addr < 0x4000) {
    if (current_cpu == 0) return pRom1[Addr];
    else if (current_cpu == 1) return pRom2[Addr];
    else return pRom3[Addr];
  }
  return rdZ80(Addr);
}

unsigned char xevious::rdZ80(unsigned short Addr) {
  if(Addr < 0x4000) {
    if (current_cpu == 0) return pRom1[Addr];
    else if (current_cpu == 1) return pRom2[Addr];
    else return pRom3[Addr];
  }

  if(Addr >= 0x6800 && Addr < 0x7000) {
    if((Addr & 0xFFF8) == 0x6800) {
      int bit = Addr & 7;
      unsigned char dipa = XEVIOUS_DIPA;
      unsigned char dipb = XEVIOUS_DIPB;
      if(input->buttons_get() & BUTTON_EXTRA) dipb &= ~0x01;
      return ((dipb >> bit) & 1) | (((dipa >> bit) & 1) << 1);
    }
    if((Addr & 0xFFE0) == 0x6800)
      return soundregs[Addr - 0x6800];
    return 0xFF;
  }

  if((Addr & 0xFE00) == 0x7000)
    return namco_read(Addr & 0x1FF);

  if((Addr & 0xF800) == 0x7800)
    return memory[0x1800 + (Addr & 0x7FF)];

  if(Addr >= 0x8000 && Addr < 0xD000) {
    switch(Addr & 0xF800) {
      case 0x8000: return memory[Addr & 0x7FF];
      case 0x9000: return memory[0x0800 + (Addr & 0x7FF)];
      case 0xA000: return memory[0x1000 + (Addr & 0x7FF)];
      case 0xB000: return memory[0x2000 + (Addr & 0x7FF)];
      case 0xB800: return memory[0x2800 + (Addr & 0x7FF)];
      case 0xC000: return memory[0x3000 + (Addr & 0x7FF)];
      case 0xC800: return memory[0x3800 + (Addr & 0x7FF)];
    }
    return 0xFF;
  }

  if(Addr >= 0xF000) {
    // BG terrain ROM lookup (GFX4 cascade, MAME xevious_bb_r).
    // gfx4 = set ORIGINALE puro:
    //   rom2a = xvi_9.2a (bgdata_2a), rom2b = xvi_10.2b (bgdata_2b),
    //   rom2c = xvi_11.2c (bg_rom2c).
    // ⛔ 2026-08-03: qui c'era scritto che il fondo pulito dipendeva da una
    //    "patch di 11 byte a 0x3ed4 = routine del terreno" applicata al set
    //    xevios. E' FALSO: a 0x3ED4 c'e' la STRINGA DEL COPYRIGHT, non codice.
    //    Lo sfondo pulito (mare/radure/basi, niente quadrati neri) dipende da
    //    queste tre ROM del terreno originali e basta.
    //    Ora la CPU principale e' il set PARENT + 12 bypass delle custom Namco
    //    (vedi xevious_rom_cpu1.h), che porta anche l'attract vero: logo
    //    XEVIOUS e «© 1982 NAMCO LTD.» col namco rosso.
    const unsigned char *rom2a = xevious_bgdata_2a;
    const unsigned char *rom2b = xevious_bgdata_2b;
    const unsigned char *rom2c = xevious_bg_rom2c;

    int adr_2b = ((bs_reg[1] & 0x7e) << 6) | ((bs_reg[0] & 0xfe) >> 1);
    int dat1;
    if(adr_2b & 1)
      dat1 = ((pgm_read_byte(&rom2a[adr_2b >> 1]) & 0xf0) << 4) | pgm_read_byte(&rom2b[adr_2b]);
    else
      dat1 = ((pgm_read_byte(&rom2a[adr_2b >> 1]) & 0x0f) << 8) | pgm_read_byte(&rom2b[adr_2b]);

    int adr_2c = ((dat1 & 0x1ff) << 2) | ((bs_reg[1] & 1) << 1) | (bs_reg[0] & 1);
    if(dat1 & 0x400) adr_2c ^= 1;
    if(dat1 & 0x200) adr_2c ^= 2;

    int dat2;
    if(Addr & 1) {
      dat2 = pgm_read_byte(&rom2c[adr_2c | 0x800]);
    } else {
      dat2 = pgm_read_byte(&rom2c[adr_2c]);
      dat2 = (dat2 & 0x3f) | ((dat2 & 0x80) >> 1) | ((dat2 & 0x40) << 1);
      if(dat1 & 0x400) dat2 ^= 0x40;
      if(dat1 & 0x200) dat2 ^= 0x80;
    }
    return dat2;
  }

  return 0xFF;
}

void xevious::wrZ80(unsigned short Addr, unsigned char Value) {
  if((Addr & 0xFFE0) == 0x6800) {
    soundregs[Addr - 0x6800] = Value & 0x0F;
    return;
  }

  if((Addr & 0xFFF8) == 0x6820) {
    switch(Addr & 7) {
      case 0: irq_enable[0] = Value & 1; break;
      case 1: irq_enable[1] = Value & 1; break;
      case 2: irq_enable[2] = Value & 1; break;
      case 3:
        sub_cpu_reset = !(Value & 1);
        if(sub_cpu_reset) {
          namco_command = 0x00; namco_mode = 0; namco_nmi_counter = 0;
          current_cpu = 1; ResetZ80(&cpu[1]);
          current_cpu = 2; ResetZ80(&cpu[2]);
        }
        break;
    }
    return;
  }

  if(Addr == 0x6830) {
    watchdog_counter = 0;
    watchdog_feed_count++;
    if(!game_started && watchdog_feed_count > 120) {
      game_started = 1;
      if(!irq_enable[0]) irq_enable[0] = 1;
      if(!irq_enable[1]) irq_enable[1] = 1;
      sub_cpu_reset = 0;
    }
    return;
  }

  if((Addr & 0xFE00) == 0x7000) { namco_write(Addr & 0x1FF, Value); return; }

  if((Addr & 0xF800) == 0x7800) { memory[0x1800 + (Addr & 0x7FF)] = Value; return; }

  if(Addr >= 0x8000 && Addr < 0xD000) {
    switch(Addr & 0xF800) {
      case 0x8000: memory[Addr & 0x7FF] = Value; return;
      case 0x9000: memory[0x0800 + (Addr & 0x7FF)] = Value; return;
      case 0xA000: memory[0x1000 + (Addr & 0x7FF)] = Value; return;
      case 0xB000: memory[0x2000 + (Addr & 0x7FF)] = Value; return;
      case 0xB800: memory[0x2800 + (Addr & 0x7FF)] = Value; return;
      case 0xC000: memory[0x3000 + (Addr & 0x7FF)] = Value; return;
      case 0xC800: memory[0x3800 + (Addr & 0x7FF)] = Value; return;
    }
    return;
  }

  if(Addr >= 0xD000 && Addr < 0xD080) {
    int offset = Addr & 0x7F;
    unsigned short scroll = Value + ((offset & 1) << 8);
    switch((offset >> 4) & 0x7) {
      case 0: scrollBgX = scroll; break;
      case 1: scrollFgX = scroll; break;
      case 2: scrollBgY = scroll; break;
      case 3: scrollFgY = scroll; break;
      case 7: flipScreen = scroll & 1; break;
    }
    return;
  }

  if(Addr >= 0xF000) { bs_reg[Addr & 1] = Value; return; }
}

// ============================================================================
// Frame execution — ALLINEATO a GALAGONE PAOLO ULTIMO (2026-06-14): 4 blocchi x
// 700 step, NMI CPU2 ai confini blocco c==1 e c==3, NMI CPU0 da contatore Namco.
// Il timing IRQ/scroll a blocchi genera la bs_reg corretta (un loop singolo da
// 2400 step dava solo foresta). ⛔ XEV_STEPS 700 e i 4 blocchi con gli NMI ai
// confini 1 e 3 sono TIMING-CRITICI: non si toccano.
// ============================================================================
#define XEV_STEPS 700

void xevious::run_frame(void) {
  watchdog_counter++;
  if(watchdog_counter >= 120) {
    watchdog_counter = 0;
    watchdog_feed_count = 0;
    for(int i = 0; i < 3; i++) {
      current_cpu = i;
      ResetZ80(&cpu[i]);
      cpu[i].SP.W = 0x0000;
    }
    memset(irq_enable, 0, sizeof(irq_enable));
    sub_cpu_reset = 1;
    namco_command = 0; namco_mode = 0; namco_nmi_counter = 0;
  }

  for(char c = 0; c < 4; c++) {
    for(int i = 0; i < XEV_STEPS; i++) {
      current_cpu = 0;
      StepZ80(cpu); StepZ80(cpu); StepZ80(cpu); StepZ80(cpu);
      if(!sub_cpu_reset) {
        current_cpu = 1;
        StepZ80(cpu+1); StepZ80(cpu+1); StepZ80(cpu+1); StepZ80(cpu+1);
        current_cpu = 2;
        StepZ80(cpu+2); StepZ80(cpu+2); StepZ80(cpu+2); StepZ80(cpu+2);
      }

      if(namco_nmi_counter) {
        namco_nmi_counter--;
        if(!namco_nmi_counter) {
          current_cpu = 0;
          IntZ80(&cpu[0], INT_NMI);
          namco_nmi_counter = XEVIOUS_NMI_DELAY;
        }
      }
    }

    if(!sub_cpu_reset && !irq_enable[2] && ((c == 1) || (c == 3))) {
      current_cpu = 2;
      IntZ80(&cpu[2], INT_NMI);
    }
  }

  // ⛔⛔ QUI STAVA IL DIFETTO STORICO DELLE BASI/BUNKER CHE SCIVOLANO SULLE RADURE
  //    (2026-08-03, fix misurato su CABINATONE e riportato qui).
  //    Questa "rete di sicurezza" forzava a OGNI frame tutte e cinque le righe
  //        irq_enable[0]=1; irq_enable[1]=1; sub_cpu_reset=0;
  //        cpu[0].IFF |= IFF_1; cpu[1].IFF |= IFF_1;
  //    nella convinzione che il set usato non scrivesse mai i latch 0x6820-0x6823.
  //    LA PREMESSA ERA FALSA: il gioco quei latch li scrive. Scavalcare le
  //    maschere a ogni frame faceva avanzare il contatore dello scroll in modo
  //    irregolare e, siccome e' incrementale, l'errore si ACCUMULAVA:
  //        terreno 0,4400 px/frame, pause miste {1,2,3}
  //    contro MAME, che ha cadenza perfetta 0,5000 px/frame, pause TUTTE da 2.
  //    Le posizioni degli sprite erano invece giuste, quindi le basi restavano
  //    indietro rispetto al terreno di ~1 px ogni 18 frame = 34 px in 10 secondi.
  // ✅ DELLE CINQUE NE SERVE UNA SOLA, ed e' questa. Provate a una a una,
  //    misurando INSIEME la cadenza dello scroll e la ricchezza del terreno
  //    (codici di tile distinti generati nella videoram BG):
  //      tutte e cinque   terreno 49 tile   scroll 0,4383  pause {1,2,3} ⛔
  //      nessuna          terreno 28 tile   scroll 0,5000  pause {2} ⛔ SOLO VERDE
  //      solo IFF main    terreno 60 tile   scroll 0,5000  pause {2} ✅
  //    Le due che rompevano la cadenza erano irq_enable[0]=1 e irq_enable[1]=1;
  //    sub_cpu_reset=0 e l'IFF della sub non servono a niente. Quella che serve
  //    e' tenere accettabili gli interrupt sulla CPU principale: senza, il gioco
  //    non pilota la cascata delle ROM del terreno e resta SOLO FORESTA.
  // ⛔ NON reintrodurre le altre quattro.
  if(game_started) cpu[0].IFF |= IFF_1;

  if(irq_enable[0]) { current_cpu = 0; IntZ80(&cpu[0], INT_RST38); }
  if(!sub_cpu_reset && irq_enable[1]) { current_cpu = 1; IntZ80(&cpu[1], INT_RST38); }
}

// ============================================================================
// Namco 06xx/51xx I/O — exact copy from GALAGONE MAME
// ============================================================================
void xevious::namco_write(unsigned short Addr, unsigned char Value) {
  if(Addr & 0x100) {
    namco_command = Value;
    s54_n = 0;                       // nuovo chip select = nuovo blocco
    if(Value != 0x10) namco_nmi_counter = XEVIOUS_NMI_DELAY;
    else              namco_nmi_counter = 0;
  } else {
    namco_customio[Addr & 0x0F] = Value;
    // ⭐ ESPLOSIONI: scrittura verso il **54XX** = device 3 del 06XX, cioe'
    // bit 3 del chip select alto e bit 4 (direzione) basso. Il gioco manda un
    // blocco di 7 byte; i primi due dicono quale suono e':
    //   30 40 -> la NAVETTA esplode      40 40 -> un BERSAGLIO esplode
    //   30 10 -> credito inserito (non e' un'esplosione: si lascia stare)
    if((namco_command & 0x18) == 0x08) {
      if(s54_n < 2) s54_b[s54_n] = Value;
      if(++s54_n == 2) {
        if(s54_b[0] == 0x30 && s54_b[1] == 0x40) {
          snd_boom_ptr = (const signed char*)XEV_BOOM_NAVETTA;
          snd_boom_cnt = 2 * XEV_BOOM_NAVETTA_LEN;     // x2: il ptr avanza ogni 2
        } else if(s54_b[0] == 0x40 && s54_b[1] == 0x40) {
          snd_boom_ptr = (const signed char*)XEV_BOOM_BERSAGLIO;
          snd_boom_cnt = 2 * XEV_BOOM_BERSAGLIO_LEN;
        }
      }
    }
    if(namco_command & 0x01) {
      if((Value & 0x07) == 0x02) namco_mode = 0;
      if((Value & 0x07) == 0x01) namco_mode = 1;
    }
  }
}

unsigned char xevious::namco_read(unsigned short Addr) {
  if(Addr & 0x100) return namco_command;

  unsigned char keymask = input->buttons_get();
  // prevKeymask e' un MEMBRO (era `static` locale): una static di funzione non
  // la puo' azzerare reset(), e restava l'edge dei tasti della partita
  // precedente. Vedi il blocco sullo stato residuo in reset().
  unsigned char keymask_p = (keymask ^ prevKeymask) & keymask;
  prevKeymask = keymask;

  if(!(keymask & BUTTON_COIN))  coinReported = false;
  if(!(keymask & BUTTON_START)) startReported = false;
  keymask_p &= ~(BUTTON_COIN | BUTTON_START);
  if((keymask & BUTTON_COIN)  && !coinReported)  keymask_p |= BUTTON_COIN;
  if((keymask & BUTTON_START) && !startReported) keymask_p |= BUTTON_START;

  if(keymask_p & BUTTON_COIN)  { if(namco_credit < 99) namco_credit++; coinReported = true; }
  if(keymask_p & BUTTON_START) { if(namco_credit > 0)  namco_credit--; startReported = true; }

  if(!game_started && namco_command == 0xb1) {
    if((Addr & 15) <= 2) return 0x00;
    return 0xff;
  }

  if((namco_command & 0x11) == 0x11) {
    if(namco_mode) {
      if((Addr & 15) == 0) {
        unsigned char L = (keymask & BUTTON_LEFT)  ? 1 : 0;
        unsigned char R = (keymask & BUTTON_RIGHT) ? 1 : 0;
        return ~((L<<7)|(R<<5)|(L<<3)|(R<<1));
      }
      if((Addr & 15) == 1) {
        unsigned char coin_b  = (keymask_p & BUTTON_COIN)  ? 1 : 0;
        unsigned char start_b = (keymask_p & BUTTON_START) ? 1 : 0;
        unsigned char fire_b  = (keymask & BUTTON_FIRE)    ? 1 : 0;
        return ~((coin_b<<4)|(start_b<<2)|(fire_b<<1)|fire_b);
      }
      return 0x00;
    } else {
      if((Addr & 15) == 0) return 16*(namco_credit/10) + namco_credit % 10;
      if((Addr & 15) == 1) {
        static const unsigned char joy_map[16] = {
          0x08,0x02,0x04,0x03,0x00,0x01,0x0A,0x0B,
          0x06,0x09,0x05,0x0E,0x07,0x0C,0x0D,0x0F };
        unsigned char raw_dir = 0;
        if(keymask & BUTTON_UP)    raw_dir |= 0x04;
        if(keymask & BUTTON_RIGHT) raw_dir |= 0x01;
        if(keymask & BUTTON_DOWN)  raw_dir |= 0x02;
        if(keymask & BUTTON_LEFT)  raw_dir |= 0x08;
        unsigned char fire_bit = (keymask & BUTTON_FIRE) ? 0x00 : 0x20;
        return fire_bit | 0x10 | joy_map[raw_dir & 0x0F];
      }
      return 0x38;
    }
  }

  if(namco_command == 0xd2) {
    if((Addr & 15) == 0) return XEVIOUS_DIPA;
    if((Addr & 15) == 1) {
      unsigned char dipb = XEVIOUS_DIPB;
      if(keymask & BUTTON_EXTRA) dipb &= ~0x01;
      return dipb;
    }
    return 0xFF;
  }

  if(namco_command == 0x74) {
    if((Addr & 15) == 0) return namco_customio[0];
    if((Addr & 15) == 3) {
      if(namco_customio[0] == 0x80 || namco_customio[0] == 0x10) return 0x05;
      return 0x95;
    }
    return 0x00;
  }

  if(namco_command == 0x08 || namco_command == 0xc1) return 0xff;

  return 0xFF;
}

// ============================================================================
// Sprite preprocessing — exact copy from GALAGONE MAME
// ============================================================================
void xevious::prepare_frame(void) {
  active_sprites = 0;
  unsigned char *sr1 = memory + 0x0780;
  unsigned char *sr2 = memory + 0x0F80;
  unsigned char *sr3 = memory + 0x1780;

  for(int idx = 0; idx < 64 && active_sprites < 124; idx++) {
    int offs = 2 * idx;
    if((sr3[offs + 1] & 0x40) == 0) {
      unsigned char sr2v = sr2[offs];
      int code;
      if(sr2v & 0x80) code = (sr3[offs] & 0x3F) + 0x100;
      else code = sr3[offs];

      unsigned char color = sr3[offs + 1] & 0x3F;
      int flipx = (sr2v >> 2) & 1;
      int flipy = (sr2v >> 3) & 1;
      int dw = sr2v & 1;
      int dh = (sr2v >> 1) & 1;
      unsigned char flip_variant = flipx | (flipy << 1);

      int sx = (int)sr1[offs + 1] - 45 + 256 * (int)(sr2[offs + 1] & 1);
      int sy = 208 - (int)sr1[offs];

      if(dw) code &= ~1;
      if(dh) code &= ~2;

      #define EMIT(c, nx, ny) do { \
        short dx = (short)(193 - (ny)); \
        short dy = (short)(nx); \
        if(dy > -16 && dy < 288 && dx > -16 && dx < 224 && active_sprites < 124) { \
          sprite[active_sprites].code  = (unsigned short)(c); \
          sprite[active_sprites].color = color; \
          sprite[active_sprites].flags = flip_variant; \
          sprite[active_sprites].x = dx; \
          sprite[active_sprites].y = dy; \
          active_sprites++; \
        } \
      } while(0)

      if(dh) {
        if(dw) {
          EMIT(code+3, flipx?sx:sx+16, flipy?sy-16:sy);
          EMIT(code+1, flipx?sx:sx+16, flipy?sy:sy-16);
        }
        EMIT(code+2, flipx?sx+16:sx, flipy?sy-16:sy);
        EMIT(code,   flipx?sx+16:sx, flipy?sy:sy-16);
      } else if(dw) {
        EMIT(code,   flipx?sx+16:sx, flipy?sy-16:sy);
        EMIT(code+1, flipx?sx:sx+16, flipy?sy-16:sy);
      } else {
        EMIT(code, sx, sy);
      }
      #undef EMIT
    }
  }

  // Render nativo a frame INTERO (orientamento GALAGONE, 224x224) in xev_full.
  // Le blit_* scrivono via `rt` = base della strip corrente. render_row()
  // trasporra' poi xev_full nel framebuffer 256 di SPINNERINO.
  if (xev_full) {
    memset(xev_full, 0, XEV_NW * XEV_NH * sizeof(unsigned short));
    for (short srow = 0; srow < 36; srow++) {   // 36 strip x 8 = 288 righe
      rt = xev_full + srow * 8 * XEV_NW;
      blit_bg_row(srow);
      for (unsigned char s = 0; s < active_sprites; s++)
        if ((sprite[s].y < 8 * (srow + 1)) && ((sprite[s].y + 16) > 8 * srow))
          blit_sprite(srow, s);
      for (char col = 0; col < 28; col++)
        blit_tile(srow, col);
    }
  }
}

// ============================================================================
// BG rendering — exact copy from GALAGONE MAME
// ============================================================================
void xevious::blit_bg_row(short row) {
  int hw_scroll_x = (scrollBgX & 0x1FF) + 28;
  int hw_scroll_y = (scrollBgY & 0xFF) + 16;

  for(char r = 0; r < 8; r++) {
    int native_x = row * 8 + r;
    int tm_px = (native_x + hw_scroll_x) & 0x1FF;
    int tile_col = tm_px >> 3;
    int rotated_row = tm_px & 7;

    unsigned short *ptr = rt + r * 224;

    for(char col = 0; col < 28; col++) {
      int tm_py = ((27 - col) * 8 + hw_scroll_y) & 0xFF;
      int tile_row = tm_py >> 3;
      int sub_y = tm_py & 7;

      unsigned short bgAddr = tile_row * 64 + tile_col;
      unsigned char tileCode = memory[0x3800 + bgAddr];
      unsigned char tileAttr = memory[0x2800 + bgAddr];
      unsigned short tileIdx = tileCode | ((tileAttr & 0x01) << 8);

      unsigned char colorGroup = ((tileAttr & 0x3c) >> 2)
                               | ((tileCode & 0x80) >> 3)
                               | ((tileAttr & 0x03) << 5);

      const unsigned short *tile = xevious_bgtiles[tileIdx & 0x1FF];
      const unsigned short *colors = xevious_cmap_bg[colorGroup & 0x7F];

      int disp_flipx = (tileAttr >> 7) & 1;
      int disp_flipy = (tileAttr >> 6) & 1;
      int actual_row = disp_flipy ? (7 - rotated_row) : rotated_row;

      if(sub_y == 0) {
        unsigned short pix = tile[actual_row];
        if(disp_flipx) {
          ptr[0] = colors[ pix        & 3];
          ptr[1] = colors[(pix >>  2) & 3];
          ptr[2] = colors[(pix >>  4) & 3];
          ptr[3] = colors[(pix >>  6) & 3];
          ptr[4] = colors[(pix >>  8) & 3];
          ptr[5] = colors[(pix >> 10) & 3];
          ptr[6] = colors[(pix >> 12) & 3];
          ptr[7] = colors[(pix >> 14) & 3];
        } else {
          ptr[0] = colors[(pix >> 14) & 3];
          ptr[1] = colors[(pix >> 12) & 3];
          ptr[2] = colors[(pix >> 10) & 3];
          ptr[3] = colors[(pix >>  8) & 3];
          ptr[4] = colors[(pix >>  6) & 3];
          ptr[5] = colors[(pix >>  4) & 3];
          ptr[6] = colors[(pix >>  2) & 3];
          ptr[7] = colors[ pix        & 3];
        }
        ptr += 8;
      } else {
        int nextTileRow = (tile_row + 1) & 31;
        unsigned short nextAddr = nextTileRow * 64 + tile_col;
        unsigned char nextCode = memory[0x3800 + nextAddr];
        unsigned char nextAttr = memory[0x2800 + nextAddr];
        unsigned short nextIdx = nextCode | ((nextAttr & 0x01) << 8);
        unsigned char nextClrGrp = ((nextAttr & 0x3c) >> 2)
                                 | ((nextCode & 0x80) >> 3)
                                 | ((nextAttr & 0x03) << 5);
        const unsigned short *nextTile = xevious_bgtiles[nextIdx & 0x1FF];
        const unsigned short *nextColors = xevious_cmap_bg[nextClrGrp & 0x7F];
        int nextDispFlipX = (nextAttr >> 7) & 1;
        int nextDispFlipY = (nextAttr >> 6) & 1;
        int nextActualRow = nextDispFlipY ? (7 - rotated_row) : rotated_row;

        for(char c = 0; c < 8; c++) {
          int s = sub_y + c;
          if(s >= 8) {
            int sn = s - 8;
            int bp = nextDispFlipX ? sn : (7 - sn);
            *ptr++ = nextColors[(nextTile[nextActualRow] >> (bp * 2)) & 3];
          } else {
            int bp = disp_flipx ? s : (7 - s);
            *ptr++ = colors[(tile[actual_row] >> (bp * 2)) & 3];
          }
        }
      }
    }
  }
}

// ============================================================================
// FG rendering — exact copy from GALAGONE MAME
// ============================================================================
void xevious::blit_tile(short row, char col) {
  // Piano FG = la banda COLORATA del logo dell'attract (rossa in una delle fasi
  // del ciclo di colore); le lettere stanno nel piano BG. Misurato offline con
  // `_verify_gfx/xevious_harness/mk_xevpiani.py` + `mk_xevsx.py`: **+1 qui = 1 px
  // piu' in alto**, e il conto e' lineare. Da 32 a 40 = 8 px su richiesta
  // dell'utente sul device (2026-08-03).
  // ⛔ Non toccare il BG (+28) ne' la costante -45 degli sprite: sono la coppia
  //    che tiene le basi ferme sulle radure, appena confermata sul device.
  int fg_scroll_x = (scrollFgX & 0x1FF) + 40;
  int fg_scroll_y = (scrollFgY & 0xFF) + 23;

  int tm_py = ((27 - col) * 8 + fg_scroll_y) & 0xFF;
  int tile_row = tm_py >> 3;

  unsigned short *ptr = rt + 8 * col;

  for(char r = 0; r < 8; r++, ptr += 224) {
    int native_x = row * 8 + r;
    int tm_px = (native_x + fg_scroll_x) & 0x1FF;
    int tile_col = tm_px >> 3;
    int rotated_row = tm_px & 7;

    unsigned short addr = tile_row * 64 + tile_col;
    unsigned char code = memory[0x3000 + addr];
    unsigned char colorAttr = memory[0x2000 + addr];

    unsigned char colorGroup = ((colorAttr & 0x03) << 4) | ((colorAttr & 0x3c) >> 2);

    unsigned char disp_flipx = (colorAttr >> 7) & 1;
    unsigned char disp_flipy = (colorAttr >> 6) & 1;

    const unsigned short *tile = xevious_tilemap[code];
    const unsigned short *colors = xevious_cmap_fg[colorGroup & 0x3F];

    int actual_row = disp_flipy ? (7 - rotated_row) : rotated_row;

    unsigned short pix = tile[actual_row];

    if(pix == 0) continue;

    if(disp_flipx) {
      unsigned char p;
      p =  pix        & 3; if(p) ptr[0] = colors[p];
      p = (pix >>  2) & 3; if(p) ptr[1] = colors[p];
      p = (pix >>  4) & 3; if(p) ptr[2] = colors[p];
      p = (pix >>  6) & 3; if(p) ptr[3] = colors[p];
      p = (pix >>  8) & 3; if(p) ptr[4] = colors[p];
      p = (pix >> 10) & 3; if(p) ptr[5] = colors[p];
      p = (pix >> 12) & 3; if(p) ptr[6] = colors[p];
      p = (pix >> 14) & 3; if(p) ptr[7] = colors[p];
    } else {
      unsigned char p;
      p = (pix >> 14) & 3; if(p) ptr[0] = colors[p];
      p = (pix >> 12) & 3; if(p) ptr[1] = colors[p];
      p = (pix >> 10) & 3; if(p) ptr[2] = colors[p];
      p = (pix >>  8) & 3; if(p) ptr[3] = colors[p];
      p = (pix >>  6) & 3; if(p) ptr[4] = colors[p];
      p = (pix >>  4) & 3; if(p) ptr[5] = colors[p];
      p = (pix >>  2) & 3; if(p) ptr[6] = colors[p];
      p =  pix        & 3; if(p) ptr[7] = colors[p];
    }
  }
}

// ============================================================================
// Sprite rendering — exact copy from GALAGONE MAME
// ============================================================================
void xevious::blit_sprite(short row, unsigned char s) {
  const unsigned long long *spr = xevious_sprites[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = xevious_cmap_sprites[sprite[s].color & 63];

  int sx = sprite[s].x;
  int px_start = 0;
  int px_end = 16;
  if(sx < 0)        { px_start = -sx; }
  if(sx + 16 > 224) { px_end = 224 - sx; }
  if(px_start >= px_end) return;
  int draw_width = px_end - px_start;

  short y_offset = sprite[s].y - 8*row;

  unsigned char lines2draw = 8;
  if(y_offset < -8) lines2draw = 16+y_offset;

  unsigned short startline = 0;
  if(y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  if(y_offset < 0) spr -= y_offset;

  unsigned short *ptr = rt + (sx < 0 ? 0 : sx) + 224*startline;

  for(char r = 0; r < lines2draw; r++, ptr += (224 - draw_width)) {
    unsigned long long pix = *spr++;
    pix >>= (px_start * 3);
    for(int c = px_start; c < px_end; c++) {
      unsigned short col = colors[pix & 7];
      if(col) *ptr = col;
      ptr++;
      pix >>= 3;
    }
  }
}

// ============================================================================
// Row rendering — exact copy from GALAGONE MAME
// ============================================================================
// Trasposizione del frame nativo (xev_full, 224x288) nel framebuffer di
// SPINNERINO. Stride framebuffer = videoGameWidth() = 288 (XEV_NH): mostriamo
// TUTTE le 288 righe (Xevious 224x288 completo, centrato nei 320 da
// render_one_frame). Niente crop, aspect corretto. Trasposizione pannello:
//   fb_x = gfb_y               (riga nativa -> asse lungo 288)
//   fb_y = 223 - gfb_x         (colonna nativa, reversed)
void xevious::render_row(short strip_r) {
  if (!xev_full) return;
  for (int fb_y_strip = 0; fb_y_strip < 8; fb_y_strip++) {
    int fb_y  = strip_r * 8 + fb_y_strip;
    int gfb_x = 223 - fb_y;                 // colonna nativa (reversed)
    if (gfb_x < 0 || gfb_x >= XEV_NW) continue;
    unsigned short *dst       = frame_buffer + fb_y_strip * XEV_NH;  // stride 288
    const unsigned short *src = xev_full + gfb_x;   // colonna gfb_x, passo XEV_NW
    for (int gfb_y = 0; gfb_y < XEV_NH; gfb_y++)
      dst[gfb_y] = src[gfb_y * XEV_NW];     // fb_x = gfb_y (tutti i 288)
  }
}

const signed char * xevious::waveRom(unsigned char value) {
  return xevious_wavetable[value];
}

const unsigned short *xevious::logo(void) {
  return xevious_logo;
}
