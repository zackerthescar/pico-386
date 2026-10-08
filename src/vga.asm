;
; vga.asm - VGA Mode X, 400 lines at 70 Hz, each row scanned 3 times (320
; x 133 logical rows), with a pixel-doubled PICO-8 blit. Each PICO-8 pixel
; is 2 Mode X pixels wide and 3 scanlines tall: about 1.1:1 on a 4:3 screen
; (2 scanlines would be 1.67:1). Hardware primitives only: page rotation and
; retrace policy live in vga_page.c.
;
; Object format: ELF32 (assemble with `nasm -f elf32`).
; Linked by wlink alongside OMF C objects; symbols carry the
; leading-underscore prefix that `wcc386 -ecc` produces for cdecl.
;
; Calling convention: Watcom stack-based (-3s)
;   params at [ebp+8], [ebp+12], ...
;

[BITS 32]

; --------------------------------------------------------------------------
; VGA port constants
; --------------------------------------------------------------------------
%define SEQ_INDEX       0x3C4
%define SEQ_DATA        0x3C5
%define CRTC_INDEX      0x3D4
%define CRTC_DATA       0x3D5
%define DAC_WRITE_INDEX 0x3C8
%define DAC_DATA        0x3C9
%define INPUT_STATUS_1  0x3DA

%define SEQ_MAP_MASK    0x02
%define SEQ_MEM_MODE    0x04

%define CRTC_MAX_SCAN   0x09
%define CRTC_START_HI   0x0C
%define CRTC_START_LO   0x0D
%define CRTC_UNDERLINE  0x14
%define CRTC_MODE_CTRL  0x17
%define CRTC_VRETRACE_END 0x11

%define VRAM_BASE       0xA0000
%define ROW_STRIDE      80              ; 320/4 bytes per Mode X row

; Viewport: 256x128 Mode X rows. Each row shows as 3 scanlines, so the image
; is 256 x 384 of the 400 visible lines. Row 3 is the top: 9 scanlines above,
; 7 below (rows are 3 lines, so it cannot be exactly centered).
%define VIEW_X          32              ; (320-256)/2 = 32, aligned to 4
%define VIEW_Y          3
%define VIEW_START      (VIEW_Y * ROW_STRIDE + VIEW_X / 4) ; 3*80+8 = 248
%define BLIT_WIDTH      64              ; 256/4 = 64 bytes per row

; --------------------------------------------------------------------------
; .rodata - PICO-8 palette (32 colors, 6-bit VGA RGB)
; --------------------------------------------------------------------------
section .rodata

global _p8_palette_rgb6:data
_p8_palette_rgb6:
    ; Standard palette (indices 0-15)
    db  0,  0,  0          ;  0: black
    db  7, 10, 20          ;  1: dark blue
    db 31,  9, 20          ;  2: dark purple
    db  0, 33, 20          ;  3: dark green
    db 42, 20, 13          ;  4: brown
    db 23, 21, 19          ;  5: dark grey
    db 48, 48, 49          ;  6: light grey
    db 63, 60, 58          ;  7: white
    db 63,  0, 19          ;  8: red
    db 63, 40,  0          ;  9: orange
    db 63, 59,  9          ; 10: yellow
    db  0, 57, 13          ; 11: green
    db 10, 43, 63          ; 12: blue
    db 32, 29, 39          ; 13: indigo
    db 63, 29, 42          ; 14: pink
    db 63, 51, 42          ; 15: peach

    ; Extended palette (PICO-8 128-143, mapped to DAC indices 16-31)
    db 10,  6,  5          ; 16: #291814
    db  4,  7, 13          ; 17: #111D35
    db 16,  8, 13          ; 18: #422136
    db  4, 20, 22          ; 19: #125359
    db 29, 11, 10          ; 20: #742F29
    db 18, 12, 14          ; 21: #49333B
    db 40, 34, 30          ; 22: #A28879
    db 60, 59, 31          ; 23: #F3EF7D
    db 47,  4, 20          ; 24: #BE1250
    db 63, 27,  9          ; 25: #FF6C24
    db 42, 57, 11          ; 26: #A8E72E
    db  0, 45, 16          ; 27: #00B543
    db  1, 22, 45          ; 28: #065AB5
    db 29, 17, 25          ; 29: #754665
    db 63, 27, 22          ; 30: #FF6E59
    db 63, 39, 32          ; 31: #FF9D81

; --------------------------------------------------------------------------
; .text - VGA functions
; --------------------------------------------------------------------------
section .text

; ==========================================================================
; void vga_ret(void)
; Restore 80x25 text mode
; ==========================================================================
global _vga_ret:function
_vga_ret:
    push ebp
    mov  ebp, esp

    mov  eax, 0x0003
    int  0x10

    mov  esp, ebp
    pop  ebp
    ret

; ==========================================================================
; void vga_set_palette(const void *rgb6_table, unsigned int count)
; Program DAC entries 0..count-1 from rgb6_table
; ==========================================================================
global _vga_set_palette:function
_vga_set_palette:
    push ebp
    mov  ebp, esp
    push esi

    mov  esi, [ebp+8]              ; rgb6_table pointer
    mov  ecx, [ebp+12]            ; count

    ; Start at DAC index 0
    mov  dx, DAC_WRITE_INDEX
    xor  al, al
    out  dx, al

    ; ecx = count * 3 (bytes to stream)
    lea  ecx, [ecx+ecx*2]

    ; Stream RGB bytes to DAC data port
    mov  dx, DAC_DATA
    rep  outsb

    pop  esi
    mov  esp, ebp
    pop  ebp
    ret

; ==========================================================================
; void vga_mode_init(void)
; Set up Mode X with 3 scanlines per row + program PICO-8 palette.
; Clears all of VRAM and leaves page offset 0 on screen.
; ==========================================================================
global _vga_mode_init:function
_vga_mode_init:
    push ebp
    mov  ebp, esp
    push ebx
    push edi                        ; clobbered by rep stosd

    ; --- Step 1: Set Mode 13h baseline via BIOS ---
    mov  eax, 0x0013
    int  0x10

    ; --- Step 2: Unprotect CRTC registers 0-7 ---
    mov  dx, CRTC_INDEX
    mov  al, CRTC_VRETRACE_END
    out  dx, al
    inc  dx                         ; dx = CRTC_DATA
    in   al, dx
    and  al, 0x7F                   ; clear bit 7 (protection bit)
    out  dx, al

    ; --- Step 3: Disable Chain-4, enable extended memory ---
    mov  dx, SEQ_INDEX
    mov  ax, 0x0604                 ; index 4, data 0x06
    out  dx, ax

    ; --- Step 4: Clear all VRAM (all 4 planes) ---
    mov  dx, SEQ_INDEX
    mov  ax, 0x0F02                 ; Map Mask = 0x0F (all planes)
    out  dx, ax

    mov  edi, VRAM_BASE
    xor  eax, eax
    mov  ecx, 16384                 ; 64KB / 4 = 16384 dwords
    rep  stosd

    ; --- Step 5: CRTC adjustments for unchained Mode X ---
    mov  dx, CRTC_INDEX

    ; Disable underline / doubleword mode
    mov  ax, 0x0014                 ; index 0x14, data 0x00
    out  dx, ax

    ; Byte mode
    mov  ax, 0xE317                 ; index 0x17, data 0xE3
    out  dx, ax

    ; Max Scan Line: scan each row 3 times (bits 0-4 = 2), keep bit 6.
    ; The blit writes each PICO-8 row once; the CRTC repeats it.
    mov  al, CRTC_MAX_SCAN
    out  dx, al
    inc  dx                         ; dx = CRTC_DATA
    in   al, dx
    and  al, 0xE0                   ; clear bits 0-4
    or   al, 0x42                   ; bit 6 (line compare bit 9) + scan x3
    out  dx, al

    ; --- Step 6: Program PICO-8 palette (32 colors) ---
    ; Push params for vga_set_palette(p8_palette_rgb6, 32)
    push dword 32
    push dword _p8_palette_rgb6
    call _vga_set_palette
    add  esp, 8

    pop  edi
    pop  ebx
    mov  esp, ebp
    pop  ebp
    ret

; ==========================================================================
; void vga_show_page(uint32_t page_offset)
; Point the CRTC start address at page_offset. Does not wait for retrace.
;
; Most VGAs latch the start address at the start of vertical retrace, so it
; must be written while the display is active: then the new page shows at
; the next retrace, and any retrace seen after this call means the latch
; happened. Interrupts are off between the check and the writes so an IRQ
; cannot push the writes into blanking.
; ==========================================================================
global _vga_show_page:function
_vga_show_page:
    push ebp
    mov  ebp, esp
    push ebx

    mov  ebx, [ebp+8]               ; page_offset
    pushfd

.wait_display:
    popfd                           ; let pending IRQs in between polls
    pushfd
    cli
    mov  dx, INPUT_STATUS_1
    in   al, dx
    test al, 0x01                   ; 1 = horizontal or vertical blanking
    jnz  .wait_display

    mov  dx, CRTC_INDEX
    mov  al, CRTC_START_HI
    mov  ah, bh
    out  dx, ax
    mov  al, CRTC_START_LO
    mov  ah, bl
    out  dx, ax

    popfd                           ; restore caller's IF
    pop  ebx
    mov  esp, ebp
    pop  ebp
    ret

; ==========================================================================
; int vga_in_retrace(void)
; Nonzero while the display is in vertical retrace.
; ==========================================================================
global _vga_in_retrace:function
_vga_in_retrace:
    mov  dx, INPUT_STATUS_1
    in   al, dx
    and  eax, 0x08
    ret

; ==========================================================================
; void vga_blit_page(const void *screen_buf, uint32_t page_offset)
; Pixel-double the 128x128 4bpp PICO-8 screen to 256x128 Mode X rows in the
; page at page_offset. The CRTC shows each row as 3 scanlines.
;
; Each screen dword holds 8 pixels, low nibble = left. In planes 0+1 a VRAM
; byte is the left pixel of a pair (doubled across 2 planes), in planes 2+3
; the right one. So pass 1 writes v & 0x0F0F0F0F and pass 2 writes
; (v >> 4) & 0x0F0F0F0F straight from the screen, with no staging buffer.
; Each 64-byte row is unrolled (16 dwords).
; ==========================================================================
%macro BLIT_PASS 2                      ; %1 = map mask word, %2 = shift
    mov  dx, SEQ_INDEX
    mov  ax, %1
    out  dx, ax

    mov  esi, [ebp+8]                   ; PICO-8 screen
    mov  eax, [ebp+12]                  ; page_offset
    lea  edi, [VRAM_BASE + eax + VIEW_START]
    mov  ecx, 128                       ; rows
%%row:
%assign i 0
%rep 16
    mov  eax, [esi + i]
%if %2
    shr  eax, 4
%endif
    and  eax, ebx
    mov  [edi + i], eax
%assign i i+4
%endrep
    add  esi, BLIT_WIDTH
    add  edi, ROW_STRIDE
    dec  ecx
    jnz  %%row
%endmacro

global _vga_blit_page:function
_vga_blit_page:
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi

    mov  ebx, 0x0F0F0F0F
    BLIT_PASS 0x0302, 0                 ; planes 0+1: left pixels
    BLIT_PASS 0x0C02, 1                 ; planes 2+3: right pixels

    pop  edi
    pop  esi
    pop  ebx
    mov  esp, ebp
    pop  ebp
    ret
