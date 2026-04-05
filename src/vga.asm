;
; vga.asm - VGA Mode X (320x400) with pixel-doubled PICO-8 blit
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
%define PAGE1_OFFSET    0x7D00          ; 80*400 = 32000 = 0x7D00
%define ROW_STRIDE      80              ; 320/4 bytes per Mode X row

; Viewport: 256x256 centered in 320x400
%define VIEW_X          32              ; (320-256)/2 = 32, aligned to 4
%define VIEW_Y          72              ; (400-256)/2 = 72
%define VIEW_START      (VIEW_Y * ROW_STRIDE + VIEW_X / 4) ; 72*80+8 = 5768
%define BLIT_WIDTH      64              ; 256/4 = 64 bytes per row

; --------------------------------------------------------------------------
; .rodata - PICO-8 palette (32 colors, 6-bit VGA RGB)
; --------------------------------------------------------------------------
section .rodata

global _p8_palette_rgb6
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
; .bss - internal buffers
; --------------------------------------------------------------------------
section .bss

align 16
split_lo:   resb 8192                  ; low-nibble expanded buffer
split_hi:   resb 8192                  ; high-nibble expanded buffer
back_page:  resd 1                     ; VRAM offset of current back buffer

; --------------------------------------------------------------------------
; .text - VGA functions
; --------------------------------------------------------------------------
section .text

; ==========================================================================
; void vga_ret(void)
; Restore 80x25 text mode
; ==========================================================================
global _vga_ret
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
global _vga_set_palette
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
; void vga_init(void)
; Set up Mode X 320x400 + program PICO-8 palette
; ==========================================================================
global _vga_init
_vga_init:
    push ebp
    mov  ebp, esp
    push ebx

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

    ; --- Step 5: CRTC adjustments for 320x400 ---
    mov  dx, CRTC_INDEX

    ; Disable underline / doubleword mode
    mov  ax, 0x0014                 ; index 0x14, data 0x00
    out  dx, ax

    ; Byte mode
    mov  ax, 0xE317                 ; index 0x17, data 0xE3
    out  dx, ax

    ; Max Scan Line: clear bits 0-4 (no double-scan), keep bit 6
    mov  al, CRTC_MAX_SCAN
    out  dx, al
    inc  dx                         ; dx = CRTC_DATA
    in   al, dx
    and  al, 0xE0                   ; clear bits 0-4
    or   al, 0x40                   ; set bit 6 (line compare bit 9)
    out  dx, al

    ; --- Step 6: Initialize double-buffer state ---
    mov  dword [back_page], PAGE1_OFFSET

    ; --- Step 7: Program PICO-8 palette (32 colors) ---
    ; Push params for vga_set_palette(p8_palette_rgb6, 32)
    push dword 32
    push dword _p8_palette_rgb6
    call _vga_set_palette
    add  esp, 8

    pop  ebx
    mov  esp, ebp
    pop  ebp
    ret

; ==========================================================================
; void vga_flip(void)
; Wait for vretrace, flip display to back page, swap pages
; ==========================================================================
global _vga_flip
_vga_flip:
    push ebp
    mov  ebp, esp

    ; --- Wait for vertical retrace ---
    mov  dx, INPUT_STATUS_1

    ; Wait until NOT in retrace (catch leading edge)
.wait_not_retrace:
    in   al, dx
    test al, 0x08
    jnz  .wait_not_retrace

    ; Wait until IN retrace
.wait_retrace:
    in   al, dx
    test al, 0x08
    jz   .wait_retrace

    ; --- Set CRTC Start Address to back_page ---
    mov  ebx, [back_page]

    mov  dx, CRTC_INDEX
    mov  al, CRTC_START_HI
    out  dx, al
    inc  dx
    mov  al, bh                     ; high byte of back_page
    out  dx, al

    dec  dx
    mov  al, CRTC_START_LO
    out  dx, al
    inc  dx
    mov  al, bl                     ; low byte of back_page
    out  dx, al

    ; --- Swap back page ---
    xor  ebx, PAGE1_OFFSET
    mov  [back_page], ebx

    mov  esp, ebp
    pop  ebp
    ret

; ==========================================================================
; void vga_blit(const void *screen_buf)
; Pixel-double 128x128 4bpp PICO-8 screen to 256x256 in Mode X back buffer
; ==========================================================================
global _vga_blit
_vga_blit:
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi

    ; ======================================================================
    ; Phase 1: Nibble split (system RAM → split_lo / split_hi)
    ; ======================================================================
    mov  esi, [ebp+8]              ; source: PICO-8 screen buffer (8192 bytes)
    mov  edi, split_lo
    mov  ebx, split_hi
    mov  ecx, 2048                  ; 8192 / 4 = 2048 dword iterations

.split_loop:
    mov  eax, [esi]                 ; load 4 packed bytes
    mov  edx, eax

    and  eax, 0x0F0F0F0F           ; low nibbles (pixel A = left)
    shr  edx, 4
    and  edx, 0x0F0F0F0F           ; high nibbles (pixel B = right)

    mov  [edi], eax
    mov  [ebx], edx

    add  esi, 4
    add  edi, 4
    add  ebx, 4
    dec  ecx
    jnz  .split_loop

    ; ======================================================================
    ; Phase 2: Blit planes 0+1 (low nibbles = doubled left pixel)
    ; ======================================================================
    mov  dx, SEQ_INDEX
    mov  ax, 0x0302                 ; Map Mask = 0x03 (planes 0+1)
    out  dx, ax

    mov  esi, split_lo
    mov  eax, [back_page]
    lea  edi, [VRAM_BASE + eax + VIEW_START]
    mov  ebx, 128                   ; 128 source rows

.blit_lo_row:
    ; Write row N
    push esi
    mov  ecx, 16                    ; 64 bytes = 16 dwords
    rep  movsd
    pop  esi                        ; rewind source

    ; EDI is now at row N + 64 bytes; advance to row N+1 start
    add  edi, ROW_STRIDE - BLIT_WIDTH  ; +16

    ; Write row N+1 (same data = vertical doubling)
    mov  ecx, 16
    rep  movsd
    ; ESI now points to next source row (advanced by 64)

    ; Advance EDI to row N+2 start
    add  edi, ROW_STRIDE - BLIT_WIDTH

    dec  ebx
    jnz  .blit_lo_row

    ; ======================================================================
    ; Phase 3: Blit planes 2+3 (high nibbles = doubled right pixel)
    ; ======================================================================
    mov  dx, SEQ_INDEX
    mov  ax, 0x0C02                 ; Map Mask = 0x0C (planes 2+3)
    out  dx, ax

    mov  esi, split_hi
    mov  eax, [back_page]
    lea  edi, [VRAM_BASE + eax + VIEW_START]
    mov  ebx, 128

.blit_hi_row:
    push esi
    mov  ecx, 16
    rep  movsd
    pop  esi

    add  edi, ROW_STRIDE - BLIT_WIDTH

    mov  ecx, 16
    rep  movsd

    add  edi, ROW_STRIDE - BLIT_WIDTH

    dec  ebx
    jnz  .blit_hi_row

    pop  edi
    pop  esi
    pop  ebx
    mov  esp, ebp
    pop  ebp
    ret
