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

#ifndef _VXTU_H_
#define _VXTU_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "vxt.h"

// TODO: Fix this!
#ifdef _MSC_VER
	#define VXTU_CAST(in, tin, tout) ((tout)in)
#else
	#define VXTU_CAST(in, tin, tout) ( ((VXT_PACK(union { tin from; tout to; })){ .from = (in) }).to )
#endif

#define vxtu_randomize(ptr, size, seed) {					\
    int s = (int)(seed);									\
    for (int i = 0; i < (int)(size); i++) {					\
        const int m = 2147483647;							\
        s = (16807 * s) % m;								\
        float r = (float)s / m;								\
        ((vxt_byte*)(ptr))[i] = (vxt_byte)(255.0 * r);		\
    }														\
}															\

typedef struct vxt_peripheral *(*vxtu_module_entry_func)(vxt_allocator*,void*,const char*);

#ifdef VXTU_MODULES
    #define _VXTU_MODULE_ENTRIES(n, ...) VXT_API_EXPORT vxtu_module_entry_func *_vxtu_module_ ## n ## _entry(int (*f)(const char*, ...)) {		\
		vxt_set_logger(f);																										\
		static vxtu_module_entry_func _vxtu_module_ ## n ## _entries[] = { __VA_ARGS__, NULL };									\
		return _vxtu_module_ ## n ## _entries;																					\
	}																															\

	#define VXTU_MODULE_ENTRIES(...) _VXT_EVALUATOR(_VXTU_MODULE_ENTRIES, VXTU_MODULE_NAME, __VA_ARGS__)
    #define VXTU_MODULE_CREATE(name, body) static struct vxt_peripheral *_vxtu_peripheral_create(vxt_allocator *ALLOC, void *FRONTEND, const char *ARGS)	\
		VXT_PERIPHERAL_CREATE(ALLOC, name, { body ; (void)FRONTEND; (void)ARGS; }) VXTU_MODULE_ENTRIES(&_vxtu_peripheral_create)							\

	#define VXTU_MODULE_NAME_STRING _VXT_EVALUATOR(_VXT_STRINGIFY, VXTU_MODULE_NAME)
#else
    #define VXTU_MODULE_ENTRIES(...)
    #define VXTU_MODULE_CREATE(name, body)
    #define VXTU_MODULE_NAME_STRING ""
#endif

#define vxtu_static_allocator(name, size)                               \
    static vxt_byte name ## allocator_data[(size)];                     \
    static vxt_byte * name ## allocator_ptr = name ## allocator_data;   \
                                                                        \
    void * name (void *ptr, size_t sz) {                                \
        if (!sz)                                                        \
            return NULL;                                                \
        else if (ptr) /* Reallocation is not support! */                \
            return NULL;                                                \
                                                                        \
        vxt_byte *p = name ## allocator_ptr;                            \
        sz += 8 - (sz % 8);                                             \
        name ## allocator_ptr += sz;                                    \
                                                                        \
        if (name ## allocator_ptr >= (name ## allocator_data + (size))) \
            return NULL; /* Allocator is out of memory! */              \
        return p;                                                       \
    }                                                                   \

// This is Set 1 codes, references here:
// http://www.quadibloc.com/comp/scan.htm
// https://sharktastica.co.uk/topics/scancodes#Set1
enum vxtu_scancode {
	// PC/XT - Model F Keyboard (83 Keys)
	VXTU_SCAN_INVALID		= 0x0,
	VXTU_SCAN_ESCAPE		= 0x1,
	VXTU_SCAN_1				= 0x2,
	VXTU_SCAN_2				= 0x3,
	VXTU_SCAN_3				= 0x4,
	VXTU_SCAN_4				= 0x5,
	VXTU_SCAN_5				= 0x6,
	VXTU_SCAN_6				= 0x7,
	VXTU_SCAN_7				= 0x8,
	VXTU_SCAN_8				= 0x9,
	VXTU_SCAN_9				= 0xA,
	VXTU_SCAN_0				= 0xB,
	VXTU_SCAN_MINUS			= 0xC,
	VXTU_SCAN_EQUAL			= 0xD,
	VXTU_SCAN_BACKSPACE		= 0xE,
	VXTU_SCAN_TAB			= 0xF,
	VXTU_SCAN_Q				= 0x10,
	VXTU_SCAN_W				= 0x11,
	VXTU_SCAN_E				= 0x12,
	VXTU_SCAN_R				= 0x13,
	VXTU_SCAN_T				= 0x14,
	VXTU_SCAN_Y				= 0x15,
	VXTU_SCAN_U				= 0x16,
	VXTU_SCAN_I				= 0x17,
	VXTU_SCAN_O				= 0x18,
	VXTU_SCAN_P				= 0x19,
	VXTU_SCAN_LBRACKET		= 0x1A,
	VXTU_SCAN_RBRACKET		= 0x1B,
	VXTU_SCAN_ENTER			= 0x1C,
	VXTU_SCAN_LCONTROL		= 0x1D,
	VXTU_SCAN_A				= 0x1E,
	VXTU_SCAN_S				= 0x1F,
	VXTU_SCAN_D				= 0x20,
	VXTU_SCAN_F				= 0x21,
	VXTU_SCAN_G				= 0x22,
	VXTU_SCAN_H				= 0x23,
	VXTU_SCAN_J				= 0x24,
	VXTU_SCAN_K				= 0x25,
	VXTU_SCAN_L				= 0x26,
	VXTU_SCAN_SEMICOLON		= 0x27,
	VXTU_SCAN_QUOTE			= 0x28,
	VXTU_SCAN_BACKQUOTE		= 0x29,
	VXTU_SCAN_LSHIFT		= 0x2A,
	VXTU_SCAN_INT2			= 0x2B, // Backslash on US keyboards
	VXTU_SCAN_Z				= 0x2C,
	VXTU_SCAN_X				= 0x2D,
	VXTU_SCAN_C				= 0x2E,
	VXTU_SCAN_V				= 0x2F,
	VXTU_SCAN_B				= 0x30,
	VXTU_SCAN_N				= 0x31,
	VXTU_SCAN_M				= 0x32,
	VXTU_SCAN_COMMA			= 0x33,
	VXTU_SCAN_PERIOD		= 0x34,
	VXTU_SCAN_SLASH			= 0x35,
	VXTU_SCAN_RSHIFT		= 0x36,
	VXTU_SCAN_PRINT			= 0x37,
	VXTU_SCAN_LALT			= 0x38,
	VXTU_SCAN_SPACE			= 0x39,
	VXTU_SCAN_CAPSLOCK		= 0x3A,
	VXTU_SCAN_F1			= 0x3B,
	VXTU_SCAN_F2			= 0x3C,
	VXTU_SCAN_F3			= 0x3D,
	VXTU_SCAN_F4			= 0x3E,
	VXTU_SCAN_F5			= 0x3F,
	VXTU_SCAN_F6			= 0x40,
	VXTU_SCAN_F7			= 0x41,
	VXTU_SCAN_F8			= 0x42,
	VXTU_SCAN_F9			= 0x43,
	VXTU_SCAN_F10			= 0x44,
	VXTU_SCAN_NUMLOCK		= 0x45,
	VXTU_SCAN_SCRLOCK		= 0x46,
	VXTU_SCAN_KP_HOME		= 0x47,
	VXTU_SCAN_KP_UP			= 0x48,
	VXTU_SCAN_KP_PAGEUP		= 0x49,
	VXTU_SCAN_KP_MINUS		= 0x4A,
	VXTU_SCAN_KP_LEFT		= 0x4B,
	VXTU_SCAN_KP_5			= 0x4C,
	VXTU_SCAN_KP_RIGHT		= 0x4D,
	VXTU_SCAN_KP_PLUS		= 0x4E,
	VXTU_SCAN_KP_END		= 0x4F,
	VXTU_SCAN_KP_DOWN		= 0x50,
	VXTU_SCAN_KP_PAGEDOWN	= 0x51,
	VXTU_SCAN_KP_INSERT		= 0x52,
	VXTU_SCAN_KP_DELETE		= 0x53,

	// AT - Model F Keyboard (84 Keys)
	VXTU_SCAN_SYS_REQ		= 0x54,

	// PS/2 - Model M Keyboard (101 Keys)
	VXTU_SCAN_F11			= 0x57,
	VXTU_SCAN_F12			= 0x58,
	VXTU_SCAN_KP_ENTER		= 0xE01C,
	VXTU_SCAN_RCONTROL		= 0xE01D,
	VXTU_SCAN_KP_DIV		= 0xE035,
	VXTU_SCAN_KP_MUL		= 0xE037,
	VXTU_SCAN_RALT			= 0xE038,
	VXTU_SCAN_HOME			= 0xE047,
	VXTU_SCAN_UP			= 0xE048,
	VXTU_SCAN_PAGEUP		= 0xE049,
	VXTU_SCAN_LEFT			= 0xE04B,
	VXTU_SCAN_RIGHT			= 0xE04D,
	VXTU_SCAN_END			= 0xE04F,
	VXTU_SCAN_DOWN			= 0xE050,
	VXTU_SCAN_PAGEDOWN		= 0xE051,
	VXTU_SCAN_INSERT		= 0xE052,
	VXTU_SCAN_DELETE		= 0xE053,	

	// PS/2 - Model M Keyboard (102 Keys)
	VXTU_SCAN_INT1			= 0x56
};

static const enum vxtu_scancode VXTU_KEY_UP_MASK = (enum vxtu_scancode)0x80;
#define VXTU_KEY_UP(scan) ((enum vxtu_scancode)((vxt_dword)(scan) | VXTU_KEY_UP_MASK))

enum vxtu_mda_attrib {
    VXTU_MDA_UNDELINE       = 0x1,
    VXTU_MDA_HIGH_INTENSITY = 0x2,
    VXTU_MDA_BLINK          = 0x4,
    VXTU_MDA_INVERSE        = 0x8
};

struct vxtu_uart_registers {
	vxt_word divisor; // Baud Rate Divisor
	vxt_byte ien; // Interrupt Enable
	vxt_byte iir; // Interrupt Identification
	vxt_byte lcr; // Line Control
	vxt_byte mcr; // Modem Control
	vxt_byte lsr; // Line Status
	vxt_byte msr; // Modem Status
};

struct vxtu_uart_interface {
    void (*config)(struct vxt_peripheral *p, const struct vxtu_uart_registers *regs, int idx, void *udata);
	void (*data)(struct vxt_peripheral *p, vxt_byte data, void *udata);
	void (*ready)(struct vxt_peripheral *p, void *udata);
	void *udata;
};

#define VXTU_SECTOR_SIZE 512
#define VXTU_MAX_SECTORS (1024 * 16 * 63)

enum vxtu_disk_seek {
    VXTU_SEEK_START		= 0x0,
	VXTU_SEEK_CURRENT 	= 0x1,
	VXTU_SEEK_END 		= 0x2
};

struct vxtu_disk_interface {
    int (*read)(vxt_system *s, void *fp, vxt_byte *buffer, int size);
	int (*write)(vxt_system *s, void *fp, vxt_byte *buffer, int size);
	int (*seek)(vxt_system *s, void *fp, int offset, enum vxtu_disk_seek whence);
	int (*tell)(vxt_system *s, void *fp);
};

struct vxtu_disk_interface2 {
	vxt_error (*read_sector)(vxt_system *s, void *fp, unsigned index, vxt_byte *buffer);
	vxt_error (*write_sector)(vxt_system *s, void *fp, unsigned index, const vxt_byte *buffer);
	int (*num_sectors)(vxt_system *s, void *fp);
};

VXT_API vxt_word vxtu_system_read_word(vxt_system *s, vxt_word seg, vxt_word offset);
VXT_API void vxtu_system_write_word(vxt_system *s, vxt_word seg, vxt_word offset, vxt_word data);
VXT_API vxt_byte *vxtu_read_file(vxt_allocator *alloc, const char *file, int *size);

VXT_API struct vxt_peripheral *vxtu_memory_create(vxt_allocator *alloc, vxt_pointer base, int amount, bool read_only);
VXT_API void *vxtu_memory_internal_pointer(struct vxt_peripheral *p);
VXT_API bool vxtu_memory_device_fill(struct vxt_peripheral *p, const vxt_byte *data, int size);

VXT_API struct vxt_peripheral *vxtu_pic_create(vxt_allocator *alloc);

VXT_API struct vxt_peripheral *vxtu_dma_create(vxt_allocator *alloc);

VXT_API struct vxt_peripheral *vxtu_pit_create(vxt_allocator *alloc);
VXT_API double vxtu_pit_get_frequency(struct vxt_peripheral *p, int channel);

VXT_API struct vxt_peripheral *vxtu_ppi_create(vxt_allocator *alloc);
VXT_API bool vxtu_ppi_key_event(struct vxt_peripheral *p, enum vxtu_scancode key, bool force);
VXT_API bool vxtu_ppi_turbo_enabled(struct vxt_peripheral *p);
VXT_API vxt_int16 vxtu_ppi_generate_sample(struct vxt_peripheral *p, int freq);
VXT_API void vxtu_ppi_set_speaker_callback(struct vxt_peripheral *p, void (*f)(struct vxt_peripheral*,double,void*), void *userdata);
VXT_API void vxtu_ppi_set_xt_switches(struct vxt_peripheral *p, vxt_byte data);
VXT_API vxt_byte vxtu_ppi_xt_switches(struct vxt_peripheral *p);

VXT_API struct vxt_peripheral *vxtu_mda_create(vxt_allocator *alloc);
VXT_API void vxtu_mda_invalidate(struct vxt_peripheral *p);
VXT_API int vxtu_mda_traverse(struct vxt_peripheral *p, int (*f)(int,vxt_byte,enum vxtu_mda_attrib,int,void*), void *userdata);

VXT_API struct vxt_peripheral *vxtu_disk_create(vxt_allocator *alloc, const struct vxtu_disk_interface *intrf);
VXT_API struct vxt_peripheral *vxtu_disk_create2(vxt_allocator *alloc, const struct vxtu_disk_interface2 *intrf);
VXT_API void vxtu_disk_set_activity_callback(struct vxt_peripheral *p, void (*cb)(int,void*), void *ud);
VXT_API void vxtu_disk_set_boot_drive(struct vxt_peripheral *p, int num);
VXT_API vxt_error vxtu_disk_mount(struct vxt_peripheral *p, int num, void *fp);
VXT_API bool vxtu_disk_unmount(struct vxt_peripheral *p, int num);

VXT_API struct vxt_peripheral *vxtu_uart_create(vxt_allocator *alloc, vxt_word base_port, int irq);
VXT_API const struct vxtu_uart_registers *vxtu_uart_internal_registers(struct vxt_peripheral *p);
VXT_API void vxtu_uart_set_callbacks(struct vxt_peripheral *p, struct vxtu_uart_interface *intrf);
VXT_API void vxtu_uart_set_error(struct vxt_peripheral *p, vxt_byte err);
VXT_API void vxtu_uart_write(struct vxt_peripheral *p, vxt_byte data);
VXT_API bool vxtu_uart_ready(struct vxt_peripheral *p);
VXT_API vxt_word vxtu_uart_address(struct vxt_peripheral *p);

#ifdef __cplusplus
}
#endif

#endif
