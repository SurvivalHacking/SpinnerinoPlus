// Copyright (c) 2019-2024 Andreas T Jonsson <mail@andreasjonsson.se>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.

#include <vxt/vxtu.h>

#define PIT_FREQUENCY 1.193182
#define TOGGLE_HIGH(ch) ( ((ch)->mode == MODE_LATCH_COUNT || (ch)->mode == MODE_TOGGLE) && (ch)->toggle )
#define TOGGLE_LOW(ch) ( ((ch)->mode == MODE_LATCH_COUNT || (ch)->mode == MODE_TOGGLE) && !(ch)->toggle )

enum chmode {
    MODE_LATCH_COUNT,
    MODE_LOW_BYTE,
    MODE_HIGH_BYTE,
    MODE_TOGGLE
};

struct channel {
	bool enabled;
    bool toggle;

	vxt_word counter;
    vxt_word latch;
    vxt_word data;
	vxt_byte mode;
};

struct pit {
 	struct channel channels[3];
    double ticker;              // non piu' usato: vedi acc_fx (tenuto per ABI)
    // PATCH CABINATONE: accumulatore dei tick PIT in virgola FISSA 16.16.
    // L'originale teneva `ticker` in microsecondi emulati come DOUBLE e nel ciclo
    // faceva un confronto e una sottrazione double PER OGNI TICK. Sull'ESP32-P4
    // (rv32imafc/ilp32f, niente double in hardware) sono chiamate software.
    // Peggio: il numero di tick per ciclo emulato e' 1193182/frequenza, quindi
    // ABBASSANDO il clock emulato il costo per ciclo AUMENTA -> misurato sul
    // device, passando da 2.5 MHz a 1.18 MHz l'emulatore e' RALLENTATO
    // (1.12 M cicli/s -> 811 k). Vedi [[cabinatone_dl2_port]].
    // Accumulatore ESATTO, in unita' di (tick PIT x frequenza): niente virgola
    // fissa e nessun arrotondamento. Un tick ogni volta che acc >= freq.
    vxt_dword acc;
    vxt_dword freq;             // frequenza di sistema in cache (0 = da leggere)
};

// PIT_FREQUENCY e' in MHz (1.193182); qui serve in Hz, intero.
#define PIT_FREQ_HZ 1193182u

static vxt_byte in(struct pit *c, vxt_word port) {
	if (port == 0x43)
		return 0;

    vxt_word ret = 0;
	struct channel *ch = &c->channels[port & 3];

    if (ch->mode != MODE_LATCH_COUNT) {
        ch->latch = ch->counter;
    }

	if ((ch->mode == MODE_LOW_BYTE) || TOGGLE_LOW(ch))
		ret = ch->latch & 0xFF;
	else if ((ch->mode == MODE_HIGH_BYTE) || TOGGLE_HIGH(ch))
		ret = ch->latch >> 8;

	if ((ch->mode == MODE_LATCH_COUNT) || (ch->mode == MODE_TOGGLE))
		ch->toggle = !ch->toggle;

	return (vxt_byte)ret;
}

static void out(struct pit *c, vxt_word port, vxt_byte data) {
    // Mode/Command register.
    if (port == 0x43) {
        struct channel *ch = &c->channels[(data >> 6) & 3];

        ch->toggle = false;
        ch->mode = (data >> 4) & 3;
        if (ch->mode == MODE_LATCH_COUNT)
            ch->latch = ch->counter;

        return;
    }

    struct channel *ch = &c->channels[port & 3];
    if ((ch->mode == MODE_LOW_BYTE) || TOGGLE_LOW(ch)) {
        ch->data = (ch->data & 0xFF00) | ((vxt_word)data);
        ch->enabled = false;
    } else if ((ch->mode == MODE_HIGH_BYTE) || TOGGLE_HIGH(ch)) {
        ch->data = (ch->data & 0x00FF) | (((vxt_word)data) << 8);
        ch->enabled = true;
        if (!ch->data)
            ch->data = 0xFFFF;
    }

    if ((ch->mode == MODE_TOGGLE) || (ch->mode == MODE_LATCH_COUNT))
        ch->toggle = !ch->toggle;
}

static vxt_error install(struct pit *c, vxt_system *s) {
    struct vxt_peripheral *p = VXT_GET_PERIPHERAL(c);
    vxt_system_install_io(s, p, 0x40, 0x43);
    vxt_system_install_timer(s, p, 0);

    enum vxt_monitor_flag flags = VXT_MONITOR_SIZE_WORD|VXT_MONITOR_FORMAT_DECIMAL;
    vxt_system_install_monitor(s, p, "Channel 0", &c->channels[0].counter, flags);
    vxt_system_install_monitor(s, p, "Channel 1", &c->channels[1].counter, flags);
    vxt_system_install_monitor(s, p, "Channel 1", &c->channels[2].counter, flags);
    return VXT_NO_ERROR;
}

static vxt_error reset(struct pit *c, struct pit *state) {
    if (state)
        memcpy(c, state, sizeof(struct pit));
    else
        vxt_memclear(c, sizeof(struct pit));
    return VXT_NO_ERROR;
}

static vxt_error timer(struct pit *c, vxt_timer_id id, int cycles) {
    (void)id;

    // PATCH CABINATONE: stessa cadenza dell'originale, in aritmetica INTERA.
    // tick PIT per ciclo emulato = PIT_FREQ_HZ / frequenza, in 16.16.
    // Calcolato UNA VOLTA (la frequenza non cambia a caldo); reset() azzera la
    // struct, quindi dopo un reset viene ricalcolato da solo.
    if (!c->freq) {
        int f = vxt_system_frequency(VXT_GET_SYSTEM(c));
        c->freq = (f > 0) ? (vxt_dword)f : 1u;
    }

    // tick PIT per ciclo emulato = PIT_FREQ_HZ / freq. Accumulando in unita' di
    // (tick x freq) il rapporto resta ESATTO: nessuna deriva, nessun double.
    // `cycles` qui e' la durata di UNA istruzione (il PIT e' installato con us=0,
    // quindi update_timers chiama a ogni istruzione) -> il prodotto sta in 32 bit.
    c->acc += (vxt_dword)cycles * PIT_FREQ_HZ;
    while (c->acc >= c->freq) {
        c->acc -= c->freq;
        for (int i = 0; i < 3; i++) {
            struct channel *ch = &c->channels[i];
            if (!ch->enabled)
                continue;

            if (ch->counter-- == 0) {
                ch->counter = ch->data;
                if (i == 0)
                    vxt_system_interrupt(VXT_GET_SYSTEM(c), 0);
            }
        }
    }
    return VXT_NO_ERROR;
}

static const char *name(struct pit *c) {
    (void)c; return "PIT (Intel 8253)";
}

static enum vxt_pclass pclass(struct pit *c) {
    (void)c; return VXT_PCLASS_PIT;
}

VXT_API struct vxt_peripheral *vxtu_pit_create(vxt_allocator *alloc) VXT_PERIPHERAL_CREATE(alloc, pit, {
    PERIPHERAL->install = &install;
    PERIPHERAL->name = &name;
    PERIPHERAL->pclass = &pclass;
    PERIPHERAL->reset = &reset;
    PERIPHERAL->timer = &timer;
    PERIPHERAL->io.in = &in;
    PERIPHERAL->io.out = &out;
})

VXT_API double vxtu_pit_get_frequency(struct vxt_peripheral *p, int channel) {
    if ((channel > 2) || (channel < 0))
        return 0.0;

    double fd = (double)(VXT_GET_DEVICE(pit, p))->channels[channel].data;
    if (fd == 0.0) fd = (double)0x10000;
    return (PIT_FREQUENCY * 1000000.0) / (double)fd;
}
