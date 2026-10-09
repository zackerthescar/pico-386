[BITS 32]

%include "src/p386_layout.inc"

section .rodata
msg_bad_opcode db 'bad opcode',0
msg_type_num   db 'expected number',0
msg_div0       db 'division by zero',0
msg_bounds     db 'register/constant out of bounds',0
msg_unimpl     db 'opcode not implemented yet',0
msg_type_tab   db 'expected table',0
msg_type_str   db 'expected string or number',0
msg_type_iter  db 'expected table iterator state',0
msg_type_func  db 'expected function',0
msg_oom        db 'out of memory',0
msg_for_step   db 'for loop step must be non-zero',0
msg_vararg     db 'variable call/return not implemented',0
msg_type_upval db 'expected upvalue',0

extern _p386_table_new
extern _p386_table_get
extern _p386_table_set
extern _p386_table_len
extern _p386_table_next
extern _p386_string_intern
extern _p386_value_concat
extern _p386_string_cmp
extern _p386_closure_new
extern _p386_upvalue_find_or_add
extern _p386_close_upvalues
extern _p386_meta_index
extern _p386_meta_newindex
extern _p386_meta_arith
extern _p386_meta_has_len
extern _p386_meta_call_value
extern _p386_meta_call
extern _p386_meta_nargs
extern _p386_gc_pending
extern _p386_gc_safepoint

; Call the CFUNC in ebx. The profiling build (nasm -DPROFILE, PROF.EXE)
; adds the instructions spent in builtins to p386_prof_cfunc (QEMU -icount
; makes the TSC count instructions). The normal build is a plain call.
%ifdef PROFILE
extern _p386_prof_cfunc
extern _p386_prof_table
extern _p386_prof_string
extern _p386_prof_ops
%endif
%ifdef PROFILE_OPS
extern _p386_prof_op_cost
extern _p386_prof_op_count
%endif

; Call a VM helper in C. The profiling build adds the instructions spent in
; it to a counter (%2): table or string work, separate from dispatch.
%macro PCALL 2
%ifdef PROFILE
    push eax
    push edx
    rdtsc
    mov  [prof_t1], eax
    pop  edx
    pop  eax
    call %1
    push eax
    push edx
    rdtsc
    sub  eax, [prof_t1]
    add  [%2], eax
    adc  dword [%2 + 4], 0
    pop  edx
    pop  eax
%else
    call %1
%endif
%endmacro
%macro CALL_CFUNC 1                 ; %1 = register with the function
%ifdef PROFILE
    push eax
    push edx
    rdtsc
    mov  [prof_t0], eax
    pop  edx
    pop  eax
    call %1
    push eax
    push edx
    rdtsc
    sub  eax, [prof_t0]
    add  [_p386_prof_cfunc], eax
    adc  dword [_p386_prof_cfunc + 4], 0
    pop  edx
    pop  eax
%else
    call %1
%endif
%endmacro

; GC safe point: collect if an allocation asked for it. Use only between
; instructions, where every live value is in a register of a live frame,
; a global, an upvalue or the varargs stack. Keeps all registers.
%macro GC_POLL 0
    cmp  dword [_p386_gc_pending], 0
    je   %%no_gc
    call gc_safepoint
%%no_gc:
%endmacro

; Metamethod events: same order as the enum in include/p386_meta.h.
%define EV_ADD    0
%define EV_SUB    1
%define EV_MUL    2
%define EV_DIV    3
%define EV_IDIV   4
%define EV_MOD    5
%define EV_POW    6
%define EV_UNM    7
%define EV_CONCAT 8
%define EV_LEN    9
%define EV_EQ     10
%define EV_LT     11
%define EV_LE     12
%define EV_NONE   -1                ; operator has no metamethod

; Result fix-up codes for FRAME_POST.
%define POST_NONE 0
%define POST_BOOL 1
%define POST_NOT  2

align 4
dispatch_table:
%assign i 0
%rep 256
%if i = 0x01
    dd op_move
%elif i = 0x02
    dd op_loadk
%elif i = 0x03
    dd op_loadt
%elif i = 0x04
    dd op_loadf
%elif i = 0x05
    dd op_loadn
%elif i = 0x10
    dd op_getglobal
%elif i = 0x11
    dd op_setglobal
%elif i = 0x12
    dd op_getupval
%elif i = 0x13
    dd op_setupval
%elif i = 0x14
    dd op_close
%elif i = 0x18
    dd op_newtable
%elif i = 0x19
    dd op_gettable
%elif i = 0x1A
    dd op_settable
%elif i = 0x1B
    dd op_getfield
%elif i = 0x1C
    dd op_setfield
%elif i = 0x20
    dd op_add
%elif i = 0x21
    dd op_sub
%elif i = 0x22
    dd op_mul
%elif i = 0x23
    dd op_div
%elif i = 0x24
    dd op_idiv
%elif i = 0x25
    dd op_mod
%elif i = 0x26
    dd op_pow
%elif i = 0x27
    dd op_neg
%elif i = 0x28
    dd op_band
%elif i = 0x29
    dd op_bor
%elif i = 0x2A
    dd op_bxor
%elif i = 0x2B
    dd op_bnot
%elif i = 0x2C
    dd op_shl
%elif i = 0x2D
    dd op_shr
%elif i = 0x2E
    dd op_lshr
%elif i = 0x2F
    dd op_rotl
%elif i = 0x30
    dd op_rotr
%elif i = 0x31
    dd op_eq
%elif i = 0x32
    dd op_ne
%elif i = 0x33
    dd op_lt
%elif i = 0x34
    dd op_le
%elif i = 0x35
    dd op_gt
%elif i = 0x36
    dd op_ge
%elif i = 0x37
    dd op_not
%elif i = 0x38
    dd op_len
%elif i = 0x39
    dd op_peek
%elif i = 0x3a
    dd op_peek2
%elif i = 0x3B
    dd op_concat
%elif i = 0x40
    dd op_jmp
%elif i = 0x41
    dd op_jmpf
%elif i = 0x42
    dd op_jmpt
%elif i = 0x45
    dd op_forprep
%elif i = 0x46
    dd op_forloop
%elif i = 0x47
    dd op_tforcall
%elif i = 0x48
    dd op_tforloop
%elif i = 0x50
    dd op_closure
%elif i = 0x51
    dd op_call
%elif i = 0x52
    dd op_tailcall
%elif i = 0x53
    dd op_return
%elif i = 0x54
    dd op_vararg
%elif i = 0x58
    dd op_flr
%elif i = 0x59
    dd op_ceil
%elif i = 0x5A
    dd op_abs
%elif i = 0x5B
    dd op_sgn
%elif i = 0x5C
    dd op_min
%elif i = 0x5D
    dd op_max
%elif i = 0x60
    dd op_beq
%elif i = 0x61
    dd op_bne
%elif i = 0x62
    dd op_blt
%elif i = 0x63
    dd op_ble
%elif i = 0x64
    dd op_bgt
%elif i = 0x65
    dd op_bge
%else
    dd op_unimpl
%endif
%assign i i+1
%endrep

section .bss
align 4
dest_tmp resd 1
scratch_a resd 8
meta_ops resd 4                     ; two operand values for p386_meta_arith
meta_post resd 1                    ; POST_* code for an EQ/NE slow path
fast_tmp resd 2                     ; value of a SETFIELD/SETTABLE fast path
frame_base resd 1                   ; callee base during frame_push
%ifdef PROFILE
prof_t0 resd 1
prof_t1 resd 1
%endif
%ifdef PROFILE_OPS
prof_prev_t resd 1
prof_prev_op resd 1
%endif

extern _p8_ram

section .text

; Fetch, decode and jump to the next handler. Every handler ends with its
; own copy (replicated dispatch): no jump back to a shared loop, and no
; per-instruction bookkeeping. The handler gets eax = instruction word,
; edx = opcode. vm->ip and vm->last_opcode are written only when the
; dispatcher exits (see done).
%macro NEXT 0
%ifdef PROFILE
    inc  dword [_p386_prof_ops]    ; bytecodes run (profiling build only)
%endif
    mov  eax, [esi]
    add  esi, 4
    movzx edx, al
%ifdef PROFILE_OPS
    call prof_op
%endif
    jmp  [dispatch_table + edx*4]
%endmacro

; Load an RK operand. In: dl = RK byte. Out: eax = value, ecx = tag, CF=0;
; CF=1 on error (vm status set). Clobbers ebx. A register operand (the
; common case) is read inline; a constant goes through load_rk.
%macro LOAD_RK 0
    test dl, 0x80
    jz   %%reg
    call load_rk
    jmp  %%done
%%reg:
    movzx ebx, dl
    mov  eax, [ebp + ebx*8]
    mov  ecx, [ebp + ebx*8 + 4]
%%done:
%endmacro

; Find an interned string key in a table's hash part (see p386_obj.c:
; open addressing, linear probing, 16-byte entries {key, value}).
; In: ebx = P386Table*, eax = P386String* (interned). Found: jumps to %1
; with ebx = the entry. Not found: falls through. Clobbers ecx, edx.
; A key made without the intern table (out of memory only) is not found
; here; the slow path in C then compares by content.
%macro HASH_FIND_STR 1
    mov  ecx, [ebx + TAB_HCAP]
    test ecx, ecx
    jz   %%absent
    mov  ebx, [ebx + TAB_HASH]
    dec  ecx
    shl  ecx, 4                    ; mask, in bytes
    mov  edx, [eax + STR_HASH]
    shl  edx, 4
%%probe:
    and  edx, ecx
    cmp  [ebx + edx], eax          ; key bits
    je   %%bits
%%next:
    cmp  dword [ebx + edx + 4], TAG_NIL
    je   %%absent                  ; empty slot: end of the probe chain
    add  edx, ENTRY_SIZE
    jmp  %%probe
%%bits:
    cmp  dword [ebx + edx + 4], TAG_STR
    jne  %%next
    add  ebx, edx
    jmp  %1
%%absent:
%endmacro

; Load K[edx] (a GETFIELD/SETFIELD name) into eax as an interned string.
; Jumps to %1 if the index or the type is wrong (the slow path reports it).
; Clobbers ecx.
%macro LOAD_KSTR_FAST 1
    mov  ecx, [edi + VM_CURRENT_PROTO]
    push edx
    movzx edx, word [ecx + PE_N_CONSTS]
    cmp  [esp], edx
    pop  edx
    jae  %1
    mov  eax, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  eax, [ecx + PE_CONSTS_OFF]
    cmp  dword [eax + edx*8 + 4], TAG_STR
    jne  %1
    mov  eax, [eax + edx*8]        ; string table index
    mov  ecx, [edi + VM_KSTR]
    mov  eax, [ecx + eax*4]        ; interned at load
%endmacro

; int p386_vm_exec(P386VMState *vm, int cont)
; cont = 0: start the current function. cont = 1: continue at vm->ip (after
; a thread switch; see p386_co.c).
global _p386_vm_exec:function
_p386_vm_exec:
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi

    mov  edi, [ebp+8]              ; VMState*
    mov  eax, [ebp+12]             ; cont
    mov  ebx, [edi + VM_CURRENT_PROTO]
    mov  esi, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  esi, [ebx + PE_BYTECODE_OFF]
    test eax, eax
    jz   .start
    mov  esi, [edi + VM_IP]
.start:
    mov  ebp, [edi + VM_BASE]      ; EBP is VM register base in dispatch

; Shared copy of NEXT, for conditional jumps and slow paths.
dispatch_next:
    NEXT

; Collect garbage (see GC_POLL). Keeps eax, ecx, edx; the C call keeps
; ebx, esi, edi and ebp.
gc_safepoint:
    push eax
    push ecx
    push edx
    mov  [edi + VM_BASE], ebp
    push edi
    call _p386_gc_safepoint
    add  esp, 4
    pop  edx
    pop  ecx
    pop  eax
    ret

; --- helpers ------------------------------------------------------------
; input: dl = RK byte. output: eax=value, ecx=tag. clobbers ebx.
load_rk:
    test dl, 0x80
    jnz .const
    movzx ebx, dl
    mov  eax, [ebp + ebx*8]
    mov  ecx, [ebp + ebx*8 + 4]
    ret
.const:
    movzx ebx, dl
    and  ebx, 0x7f
    mov  ecx, [edi + VM_CURRENT_PROTO]
    movzx eax, word [ecx + PE_N_CONSTS]
    cmp  ebx, eax
    jae  .bounds
    mov  eax, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  eax, [ecx + PE_CONSTS_OFF]
    mov  ecx, [eax + ebx*8 + 4]
    mov  eax, [eax + ebx*8]
    cmp  ecx, TAG_STR
    jne  .ret_ok
    mov  ecx, [edi + VM_KSTR]      ; interned at load
    mov  eax, [ecx + eax*4]
    mov  ecx, TAG_STR
.ret_ok:
    clc
    ret
.bounds:
    mov  dword [edi + VM_STATUS], ERR_BOUNDS
    mov  dword [edi + VM_ERROR_MSG], msg_bounds
    stc
    ret

store_bool_al:
    ; A index in dest_tmp, bool byte in al.
    mov  ebx, [dest_tmp]
    movzx eax, al
    mov  [ebp + ebx*8], eax
    mov  dword [ebp + ebx*8 + 4], TAG_BOOL
    NEXT

branch_al:
    ; Fused compare (BEQ..BGE): al = result (0/1). esi points at the
    ; JMPF/JMPT A sBx that follows; decide it here and skip it. JMPF jumps
    ; when al = 0, JMPT when al = 1.
    mov  edx, [esi]
    add  esi, 4
    cmp  dl, OP_JMPT
    sete cl
    cmp  al, cl
    jne  dispatch_next
    sar  edx, 16                   ; sBx
    lea  esi, [esi + edx*4]
    test edx, edx
    jns  dispatch_next
    GC_POLL                        ; backward (repeat-until): a safe point
    NEXT

; --- movement -----------------------------------------------------------
op_move:
    movzx ecx, ah                  ; A
    shr  eax, 16
    movzx edx, al                  ; B
    mov  ebx, [ebp + edx*8]
    mov  edx, [ebp + edx*8 + 4]
    mov  [ebp + ecx*8], ebx
    mov  [ebp + ecx*8 + 4], edx
    NEXT

op_loadk:
    movzx ecx, ah                  ; A
    shr  eax, 16                   ; Bx
    movzx edx, ax
    mov  ebx, [edi + VM_CURRENT_PROTO]
    movzx eax, word [ebx + PE_N_CONSTS]
    cmp  edx, eax
    jae  err_bounds
    mov  eax, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  eax, [ebx + PE_CONSTS_OFF]
    mov  ebx, [eax + edx*8]
    mov  edx, [eax + edx*8 + 4]
    cmp  edx, TAG_STR
    jne  .store_raw
    mov  eax, [edi + VM_KSTR]      ; interned at load
    mov  ebx, [eax + ebx*4]
.store_raw:
    mov  [ebp + ecx*8], ebx
    mov  [ebp + ecx*8 + 4], edx
    NEXT

op_loadt:
    movzx ecx, ah
    mov  dword [ebp + ecx*8], 1
    mov  dword [ebp + ecx*8 + 4], TAG_BOOL
    NEXT

op_loadf:
    movzx ecx, ah
    mov  dword [ebp + ecx*8], 0
    mov  dword [ebp + ecx*8 + 4], TAG_BOOL
    NEXT

op_loadn:
    movzx ecx, ah                  ; A
    shr  eax, 16
    movzx edx, al                  ; count
.nil_loop:
    test edx, edx
    jz   dispatch_next
    mov  dword [ebp + ecx*8], 0
    mov  dword [ebp + ecx*8 + 4], TAG_NIL
    inc  ecx
    dec  edx
    jmp  .nil_loop

op_getglobal:                       ; A, Bx
    movzx ecx, ah
    shr  eax, 16
    movzx edx, ax                  ; Bx: global slot
    cmp  edx, P386_GLOBAL_SLOTS
    jae  err_bounds
    mov  ebx, [edi + VM_GLOBALS + edx*8]
    mov  edx, [edi + VM_GLOBALS + edx*8 + 4]
    mov  [ebp + ecx*8], ebx
    mov  [ebp + ecx*8 + 4], edx
    NEXT

op_setglobal:                       ; A, Bx
    movzx ecx, ah
    shr  eax, 16
    movzx edx, ax                  ; Bx: global slot
    cmp  edx, P386_GLOBAL_SLOTS
    jae  err_bounds
    mov  ebx, [ebp + ecx*8]
    mov  ecx, [ebp + ecx*8 + 4]
    mov  [edi + VM_GLOBALS + edx*8], ebx
    mov  [edi + VM_GLOBALS + edx*8 + 4], ecx
    NEXT

op_getupval:
    movzx ecx, ah                  ; A
    shr  eax, 16
    movzx edx, al                  ; B
    mov  ebx, [edi + VM_CURRENT_CLOSURE]
    test ebx, ebx
    jz   err_type_upval
    movzx eax, byte [ebx + 8]      ; n_upvalues
    cmp  edx, eax
    jae  err_bounds
    mov  ebx, [ebx + 12 + edx*4]   ; upvalue*
    test ebx, ebx
    jz   err_type_upval
    mov  ebx, [ebx + 0]            ; slot*
    test ebx, ebx
    jz   err_type_upval
    mov  eax, [ebx + 0]
    mov  edx, [ebx + 4]
    mov  [ebp + ecx*8], eax
    mov  [ebp + ecx*8 + 4], edx
    NEXT

op_setupval:
    movzx ecx, ah                  ; A
    shr  eax, 16
    movzx edx, al                  ; B
    mov  ebx, [edi + VM_CURRENT_CLOSURE]
    test ebx, ebx
    jz   err_type_upval
    movzx eax, byte [ebx + 8]      ; n_upvalues
    cmp  edx, eax
    jae  err_bounds
    mov  ebx, [ebx + 12 + edx*4]   ; upvalue*
    test ebx, ebx
    jz   err_type_upval
    mov  ebx, [ebx + 0]            ; slot*
    test ebx, ebx
    jz   err_type_upval
    mov  eax, [ebp + ecx*8]
    mov  edx, [ebp + ecx*8 + 4]
    mov  [ebx + 0], eax
    mov  [ebx + 4], edx
    NEXT

op_close:
    movzx ecx, ah                  ; A
    lea  eax, [ebp + ecx*8]
    push eax
    lea  eax, [edi + VM_OPEN_UPVALUES]
    push eax
    call _p386_close_upvalues
    add  esp, 8
    NEXT

; --- objects -------------------------------------------------------------
store_value_eax_ecx:
    mov  ebx, [dest_tmp]
    mov  [ebp + ebx*8], eax
    mov  [ebp + ebx*8 + 4], ecx
    NEXT

op_newtable:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    movzx edx, al
    movzx eax, ah
    push eax
    push edx
    PCALL _p386_table_new, _p386_prof_table
    add  esp, 8
    test eax, eax
    jz   err_oom
    mov  ecx, TAG_TAB
    jmp  store_value_eax_ecx

build_key_rk:
    LOAD_RK
    jc   done
    mov  [scratch_a + 0], eax
    mov  [scratch_a + 4], ecx
    ret

; Fast paths for table access handle the common case inline: an integer
; key inside the array part, or a string key that is in the hash part.
; Everything else (no hash part, absent key, nil value that may need
; __index, metatables, inserts) goes to the general path below, which
; starts again from the instruction word.
op_gettable:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    movzx ebx, al
    cmp  dword [ebp + ebx*8 + 4], TAG_TAB
    jne  err_type_tab
    mov  ebx, [ebp + ebx*8]
    mov  [scratch_a + 8], ebx
    mov  dl, ah
    LOAD_RK
    jc   done
    mov  ebx, [scratch_a + 8]
    cmp  ecx, TAG_NUM
    jne  .key_str
    test eax, 0xffff
    jnz  .general
    sar  eax, 16
    dec  eax                       ; 0-based index; unsigned compare
    cmp  eax, [ebx + TAB_ASIZE]    ; also rejects keys <= 0
    jae  .general
    mov  ecx, [ebx + TAB_ARR]
    mov  edx, [ecx + eax*8 + 4]
    cmp  edx, TAG_NIL
    je   .general
    mov  eax, [ecx + eax*8]
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  [ebp + ecx*8 + 4], edx
    NEXT
.key_str:
    cmp  ecx, TAG_STR
    jne  .general
    HASH_FIND_STR .hit
    jmp  .general
.hit:
    mov  eax, [ebx + 8]
    mov  edx, [ebx + 12]
    cmp  edx, TAG_NIL
    je   .general
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  [ebp + ecx*8 + 4], edx
    NEXT
.general:
    mov  eax, [esi - 4]
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    movzx ebx, al
    mov  ebx, [ebp + ebx*8]
    mov  [scratch_a + 8], ebx
    mov  dl, ah
    call build_key_rk
    jc   done
    mov  ebx, [dest_tmp]
    lea  edx, [ebp + ebx*8]
    push edx
    lea  edx, [scratch_a]
    push edx
    push dword [scratch_a + 8]
    PCALL _p386_table_get, _p386_prof_table
    add  esp, 12
    mov  ebx, [dest_tmp]
    cmp  dword [ebp + ebx*8 + 4], TAG_NIL
    jne  dispatch_next
    ; Miss: follow __index if the table has a metatable. The key is still
    ; in scratch_a, so a write of R[A] above does not change it.
    mov  eax, [scratch_a + 8]
    cmp  dword [eax + TAB_METATABLE], 0
    je   dispatch_next
    lea  edx, [ebp + ebx*8]
    push edx                       ; out
    push dword scratch_a           ; key
    push eax                       ; table
    push edi
    call _p386_meta_index
    add  esp, 16
    jmp  meta_result

op_settable:
    movzx ebx, ah
    cmp  dword [ebp + ebx*8 + 4], TAG_TAB
    jne  err_type_tab
    mov  ebx, [ebp + ebx*8]
    cmp  dword [ebx + TAB_METATABLE], 0
    jne  .general
    mov  [scratch_a + 8], ebx
    mov  edx, eax
    shr  edx, 24                   ; C: value
    LOAD_RK
    jc   done
    cmp  ecx, TAG_NIL              ; a nil store can trim or delete: C
    je   .general
    mov  [fast_tmp], eax
    mov  [fast_tmp + 4], ecx
    mov  edx, [esi - 4]
    shr  edx, 16                   ; B: key
    LOAD_RK
    jc   done
    mov  ebx, [scratch_a + 8]
    cmp  ecx, TAG_NUM
    jne  .key_str
    test eax, 0xffff
    jnz  .general
    sar  eax, 16
    dec  eax
    cmp  eax, [ebx + TAB_ASIZE]
    jae  .general                  ; outside the array part (or an append)
    mov  ebx, [ebx + TAB_ARR]
    lea  ebx, [ebx + eax*8]
    jmp  .store
.key_str:
    cmp  ecx, TAG_STR
    jne  .general
    HASH_FIND_STR .hit
    jmp  .general
.hit:
    add  ebx, 8                    ; the entry's value
.store:
    mov  eax, [fast_tmp]
    mov  [ebx], eax
    mov  eax, [fast_tmp + 4]
    mov  [ebx + 4], eax
    NEXT
.general:
    mov  eax, [esi - 4]
    movzx ebx, ah
    mov  ecx, [ebp + ebx*8 + 4]
    mov  ebx, [ebp + ebx*8]
    push eax
    push ebx
    sub  esp, 16
    mov  edx, [esp + 20]
    shr  edx, 16
    LOAD_RK
    jc   err_rk_pop24
    mov  [esp + 0], eax
    mov  [esp + 4], ecx
    mov  edx, [esp + 20]
    shr  edx, 24
    LOAD_RK
    jc   err_rk_pop24
    mov  [esp + 8], eax
    mov  [esp + 12], ecx
    lea  eax, [esp + 8]
    lea  edx, [esp + 0]
    mov  ebx, [esp + 16]           ; table
    cmp  dword [ebx + TAB_METATABLE], 0
    jne  .meta
    push eax
    push edx
    push ebx
    PCALL _p386_table_set, _p386_prof_table
    add  esp, 12
    add  esp, 24
    NEXT
.meta:
    push eax                       ; value
    push edx                       ; key
    push ebx                       ; table
    push edi
    call _p386_meta_newindex
    add  esp, 16
    add  esp, 24
    jmp  meta_result_discard

; load_kstr: const idx zero-extended in edx. on success eax=interned String*,
; ecx=TAG_STR, CF=0. on failure sets vm error and CF=1. clobbers ebx.
load_kstr:
    mov  ebx, [edi + VM_CURRENT_PROTO]
    movzx eax, word [ebx + PE_N_CONSTS]
    cmp  edx, eax
    jae  .bounds
    mov  eax, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  eax, [ebx + PE_CONSTS_OFF]
    mov  ecx, [eax + edx*8 + 4]
    cmp  ecx, TAG_STR
    jne  .typestr
    mov  eax, [eax + edx*8]        ; string index
    mov  edx, [edi + VM_KSTR]      ; interned at load
    mov  eax, [edx + eax*4]
    clc
    ret
.bounds:
    mov  dword [edi + VM_STATUS], ERR_BOUNDS
    mov  dword [edi + VM_ERROR_MSG], msg_bounds
    stc
    ret
.typestr:
    mov  dword [edi + VM_STATUS], ERR_TYPE
    mov  dword [edi + VM_ERROR_MSG], msg_type_str
    stc
    ret
.oom:
    mov  dword [edi + VM_STATUS], ERR_BOUNDS
    mov  dword [edi + VM_ERROR_MSG], msg_oom
    stc
    ret

; GETFIELD A B C: R[A] = R[B][K[C]], K[C] a string. Two words follow the
; instruction (an inline cache):
;   [esi]     the interned name (the loader writes it; 0 if K[C] is not a
;             string)
;   [esi + 4] byte offset of the hash slot where the name was found last
;             time. Any value is safe: it is masked to the hash part and the
;             key is checked.
; Objects made by the same code have the same keys in the same slots, so
; one compare usually finds the key. On a miss, probe and remember the slot.
op_getfield:
    shr  eax, 16
    movzx ebx, al                  ; B
    cmp  dword [ebp + ebx*8 + 4], TAG_TAB
    jne  err_type_tab
    mov  ebx, [ebp + ebx*8]        ; table
    mov  ecx, [ebx + TAB_HCAP]
    dec  ecx
    js   .general                  ; no hash part
    shl  ecx, 4                    ; mask, in bytes
    mov  edx, [esi + 4]            ; cached slot
    and  edx, ecx
    mov  eax, [esi]                ; name
    mov  ebx, [ebx + TAB_HASH]
    cmp  [ebx + edx], eax
    jne  .probe
    cmp  dword [ebx + edx + 4], TAG_STR
    jne  .probe
.hit:
    mov  eax, [ebx + edx + 8]
    mov  ecx, [ebx + edx + 12]
    cmp  ecx, TAG_NIL
    je   .general                  ; may need __index
    movzx edx, byte [esi - 3]      ; A
    add  esi, 8                    ; step over the cache words
    mov  [ebp + edx*8], eax
    mov  [ebp + edx*8 + 4], ecx
    NEXT
.probe:                            ; eax = name, ebx = hash part, ecx = mask
    test eax, eax
    jz   .general                  ; K[C] is not a string: general reports
    mov  edx, [eax + STR_HASH]
    shl  edx, 4
.probe_slot:
    and  edx, ecx
    cmp  [ebx + edx], eax          ; key bits
    je   .probe_bits
.probe_next:
    cmp  dword [ebx + edx + 4], TAG_NIL
    je   .general                  ; empty slot: absent (maybe __index)
    add  edx, ENTRY_SIZE
    jmp  .probe_slot
.probe_bits:
    cmp  dword [ebx + edx + 4], TAG_STR
    jne  .probe_next
    mov  [esi + 4], edx            ; remember the slot
    jmp  .hit
.general:
    mov  eax, [esi - 4]
    movzx ecx, ah                  ; A
    mov  [dest_tmp], ecx
    shr  eax, 16
    movzx ebx, al                  ; B
    mov  edx, [ebp + ebx*8 + 4]
    push dword [ebp + ebx*8]       ; save table ptr
    movzx edx, ah                  ; C: const idx
    call load_kstr
    pop  ebx                       ; restore table ptr
    jc   done
    push ecx                       ; key tag (TAG_STR)
    push eax                       ; key value
    mov  ecx, [dest_tmp]
    lea  ecx, [ebp + ecx*8]
    push ecx                       ; out
    lea  ecx, [esp + 4]
    push ecx                       ; key ptr
    push ebx                       ; table
    PCALL _p386_table_get, _p386_prof_table
    add  esp, 12                   ; the key stays on the stack
    mov  ecx, [dest_tmp]
    cmp  dword [ebp + ecx*8 + 4], TAG_NIL
    je   .miss
    add  esp, 8
    add  esi, 8                    ; step over the cache words
    NEXT
.miss:
    cmp  dword [ebx + TAB_METATABLE], 0
    je   .no_meta
    lea  eax, [ebp + ecx*8]
    mov  edx, esp
    push eax                       ; out
    push edx                       ; key
    push ebx                       ; table
    push edi
    call _p386_meta_index
    add  esp, 16
    add  esp, 8
    add  esi, 8                    ; an __index frame returns past the cache
    jmp  meta_result
.no_meta:
    add  esp, 8
    add  esi, 8
    NEXT

op_setfield:
    movzx ebx, ah                  ; A (table reg)
    cmp  dword [ebp + ebx*8 + 4], TAG_TAB
    jne  err_type_tab
    mov  ebx, [ebp + ebx*8]
    cmp  dword [ebx + TAB_METATABLE], 0
    jne  .general
    mov  [scratch_a + 8], ebx
    mov  edx, eax
    shr  edx, 24                   ; C: value (RK)
    LOAD_RK
    jc   done
    mov  [fast_tmp], eax
    mov  [fast_tmp + 4], ecx
    movzx edx, byte [esi - 2]      ; B: name constant
    LOAD_KSTR_FAST .general
    mov  ebx, [scratch_a + 8]
    HASH_FIND_STR .hit
    jmp  .general                  ; a new key: insert in C
.hit:
    mov  eax, [fast_tmp]           ; the key stays; only the value changes
    mov  [ebx + 8], eax
    mov  eax, [fast_tmp + 4]
    mov  [ebx + 12], eax
    NEXT
.general:
    mov  eax, [esi - 4]
    movzx ebx, ah                  ; A (table reg)
    mov  ecx, [ebp + ebx*8 + 4]
    push dword [ebp + ebx*8]       ; table ptr
    push eax                       ; save instruction word
    shr  eax, 16
    movzx edx, al                  ; B (const idx)
    call load_kstr
    pop  edx                       ; instruction word
    jc   err_rk_pop1
    push ecx                       ; key tag
    push eax                       ; key value
    shr  edx, 24                   ; C: RK value (a constructor field
    LOAD_RK                   ; like {x=0} uses a constant)
    jc   err_rk_pop3
    mov  ebx, [esp + 8]            ; table ptr
    push ecx                       ; val tag
    push eax                       ; val value
    lea  eax, [esp + 0]            ; val ptr
    lea  ecx, [esp + 8]            ; key ptr
    cmp  dword [ebx + TAB_METATABLE], 0
    jne  .meta
    push eax
    push ecx
    push ebx                       ; table
    PCALL _p386_table_set, _p386_prof_table
    add  esp, 12
    add  esp, 20
    NEXT
.meta:
    push eax                       ; value
    push ecx                       ; key
    push ebx                       ; table
    push edi
    call _p386_meta_newindex
    add  esp, 16
    add  esp, 20
    jmp  meta_result_discard

op_concat:                          ; A, RK(B), RK(C)
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    mov  dh, ah
    LOAD_RK
    jc   done
    mov  [meta_ops], eax
    mov  [meta_ops + 4], ecx
    mov  dl, dh
    LOAD_RK
    jc   done
    mov  [meta_ops + 8], eax
    mov  [meta_ops + 12], ecx
    push dword meta_ops + 8
    push dword meta_ops
    PCALL _p386_value_concat, _p386_prof_string
    add  esp, 8
    test eax, eax
    jz   concat_meta
    mov  ecx, TAG_STR
    jmp  store_value_eax_ecx

; --- numeric factory ----------------------------------------------------
%macro NUM_BIN 3                    ; %3 = metamethod event or EV_NONE
%1:
    movzx ecx, ah                    ; A saved
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al                    ; B RK
    mov  dh, ah                    ; C RK
    LOAD_RK
    jc   done
    cmp  ecx, TAG_NUM
    jne  %%meta
    push eax                       ; left value
    mov  dl, dh
    LOAD_RK
    jc   err_rk_pop1
    cmp  ecx, TAG_NUM
    jne  %%meta_pop
    mov  ebx, eax                  ; right
    pop  eax                       ; left
    %2
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_NUM
    NEXT
%%meta_pop:
    add  esp, 4
%%meta:
%if %3 = EV_NONE
    jmp  err_type_num
%else
    mov  eax, %3
    jmp  arith_meta
%endif
%endmacro

%macro DO_ADD 0
    add eax, ebx
%endmacro
%macro DO_SUB 0
    sub eax, ebx
%endmacro
%macro DO_MUL 0
    imul ebx
    shrd eax, edx, 16
%endmacro
%macro DO_DIV 0
    test ebx, ebx
    jz err_div0
    cdq
    shld edx, eax, 16
    shl eax, 16
    idiv ebx
%endmacro

%macro DO_IDIV 0
    test ebx, ebx
    jz err_div0
    cdq
    idiv ebx                    ; eax = trunc(a/b) (raw fp/fp)
    shl eax, 16                 ; back to fp
%endmacro
%macro DO_MOD 0
    test ebx, ebx
    jz err_div0
    cdq
    idiv ebx                    ; edx = remainder (already fp-scaled)
    mov eax, edx
%endmacro
%macro DO_POW 0
    sar ebx, 16                  ; integer exponent
    mov ecx, ebx
    test ecx, ecx
    jle %%pow_one
    mov ebx, eax                 ; ebx = base (fp)
    mov eax, 0x10000             ; result = 1.0 (fp)
%%pow_loop:
    imul ebx                     ; edx:eax = result*base, raw fp*fp
    shrd eax, edx, 16            ; renormalize fp
    dec ecx
    jnz %%pow_loop
    jmp %%pow_done
%%pow_one:
    mov eax, 0x10000             ; b<=0 -> 1.0 (sketch)
%%pow_done:
%endmacro
%macro DO_BAND 0
    sar eax, 16
    sar ebx, 16
    and eax, ebx
    shl eax, 16
%endmacro
%macro DO_BOR 0
    sar eax, 16
    sar ebx, 16
    or  eax, ebx
    shl eax, 16
%endmacro
%macro DO_BXOR 0
    sar eax, 16
    sar ebx, 16
    xor eax, ebx
    shl eax, 16
%endmacro
%macro DO_SHL 0
    sar eax, 16
    mov ecx, ebx
    sar ecx, 16
    and ecx, 31
    shl eax, cl
    shl eax, 16
%endmacro
%macro DO_SHR 0
    sar eax, 16
    mov ecx, ebx
    sar ecx, 16
    and ecx, 31
    sar eax, cl
    shl eax, 16
%endmacro
%macro DO_LSHR 0
    sar eax, 16
    mov ecx, ebx
    sar ecx, 16
    and ecx, 31
    shr eax, cl
    shl eax, 16
%endmacro
%macro DO_ROTL 0
    sar eax, 16
    mov ecx, ebx
    sar ecx, 16
    and ecx, 31
    rol eax, cl
    shl eax, 16
%endmacro
%macro DO_ROTR 0
    sar eax, 16
    mov ecx, ebx
    sar ecx, 16
    and ecx, 31
    ror eax, cl
    shl eax, 16
%endmacro

NUM_BIN op_add, DO_ADD, EV_ADD
NUM_BIN op_sub, DO_SUB, EV_SUB
NUM_BIN op_mul, DO_MUL, EV_MUL
NUM_BIN op_div, DO_DIV, EV_DIV
NUM_BIN op_idiv, DO_IDIV, EV_IDIV
NUM_BIN op_mod, DO_MOD, EV_MOD
NUM_BIN op_pow, DO_POW, EV_POW
NUM_BIN op_band, DO_BAND, EV_NONE
NUM_BIN op_bor, DO_BOR, EV_NONE
NUM_BIN op_bxor, DO_BXOR, EV_NONE
NUM_BIN op_shl, DO_SHL, EV_NONE
NUM_BIN op_shr, DO_SHR, EV_NONE
NUM_BIN op_lshr, DO_LSHR, EV_NONE
NUM_BIN op_rotl, DO_ROTL, EV_NONE
NUM_BIN op_rotr, DO_ROTR, EV_NONE

op_neg:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    LOAD_RK
    jc   done
    cmp  ecx, TAG_NUM
    jne  neg_meta
    neg  eax
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_NUM
    NEXT

op_bnot:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    LOAD_RK
    jc   done
    cmp  ecx, TAG_NUM
    jne  err_type_num
    sar  eax, 16
    not  eax
    shl  eax, 16
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_NUM
    NEXT

; --- comparisons / boolean ---------------------------------------------
%macro CMP_NUM 5                    ; %3 = event, %4 = 1: swap operands, %5 = tail
%1:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    mov  dh, ah
    LOAD_RK
    jc   done
    cmp  ecx, TAG_NUM
    jne  %%left_str
    push eax
    mov  dl, dh
    LOAD_RK
    jc   err_rk_pop1
    cmp  ecx, TAG_NUM
    jne  %%meta_pop
    mov  ebx, eax
    pop  eax
    cmp  eax, ebx
    %2 al
    jmp  %5
%%left_str:                        ; left is not NUM: STR/STR is legal
    cmp  ecx, TAG_STR
    jne  %%meta
    push eax                       ; left string
    mov  dl, dh
    LOAD_RK
    jc   err_rk_pop1
    cmp  ecx, TAG_STR
    jne  %%meta_pop
    pop  ebx                       ; left string
    push eax                       ; arg 2: right
    push ebx                       ; arg 1: left
    PCALL _p386_string_cmp, _p386_prof_string
    add  esp, 8
    test eax, eax
    %2 al
    jmp  %5
%%meta_pop:
    add  esp, 4
%%meta:                            ; other types: try __lt / __le
    mov  eax, %3
    mov  ecx, %4
    jmp  cmp_meta
%endmacro

; EQ/NE A B C: R[A] = RK(B) ==/~= RK(C). The fused form (BEQ/BNE) has the
; same operands and is followed by a JMPF or JMPT on R[A]: it goes to
; branch_al and decides that jump itself. Only an __eq call stores R[A]; the
; JMPF/JMPT then runs as usual.
%macro EQ_OP 3                     ; %2 = 0: EQ, 1: NE; %3 = tail
%1:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    mov  dh, ah
    LOAD_RK
    jc   done
    push eax
    push ecx
    mov  dl, dh
    LOAD_RK
    jc   err_rk_pop2
    pop  ebx                       ; left tag
    pop  edx                       ; left value
    cmp  ebx, ecx
    jne  %%differ
    cmp  edx, eax
    je   %%same
    cmp  ecx, TAG_TAB              ; two different tables: try __eq
    jne  %%differ
    cmp  dword [edx + TAB_METATABLE], 0
    jne  %%meta                    ; __eq only if a metatable is set
    cmp  dword [eax + TAB_METATABLE], 0
    je   %%differ
%%meta:
%if %2 = 0
    mov  dword [meta_post], POST_BOOL
%else
    mov  dword [meta_post], POST_NOT
%endif
    jmp  eq_meta
%%same:
    mov  al, 1 - %2
    jmp  %3
%%differ:
    mov  al, %2
    jmp  %3
%endmacro

EQ_OP op_eq, 0, store_bool_al
EQ_OP op_ne, 1, store_bool_al
EQ_OP op_beq, 0, branch_al
EQ_OP op_bne, 1, branch_al

CMP_NUM op_lt, setl, EV_LT, 0, store_bool_al
CMP_NUM op_le, setle, EV_LE, 0, store_bool_al
CMP_NUM op_gt, setg, EV_LT, 1, store_bool_al
CMP_NUM op_ge, setge, EV_LE, 1, store_bool_al
CMP_NUM op_blt, setl, EV_LT, 0, branch_al
CMP_NUM op_ble, setle, EV_LE, 0, branch_al
CMP_NUM op_bgt, setg, EV_LT, 1, branch_al
CMP_NUM op_bge, setge, EV_LE, 1, branch_al

op_not:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al                    ; B: RK
    LOAD_RK
    jc   done
    cmp  ecx, TAG_NIL
    je   .truth
    cmp  ecx, TAG_BOOL
    jne  .false
    test eax, eax
    jz   .truth
.false:
    xor  al, al
    jmp  store_bool_al
.truth:
    mov  al, 1
    jmp  store_bool_al

op_len:
    movzx ecx, ah                    ; A
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al                      ; B: RK
    LOAD_RK
    jc   done
    mov  edx, ecx                    ; tag
    mov  ecx, eax                    ; value
    cmp  edx, TAG_STR
    je   .str
    cmp  edx, TAG_TAB
    jne  err_type_tab
    cmp  dword [ecx + TAB_METATABLE], 0
    jne  .meta
.raw:
    push ecx
    PCALL _p386_table_len, _p386_prof_table
    add  esp, 4
    shl  eax, 16
    mov  ecx, TAG_NUM
    jmp  store_value_eax_ecx
.str:
    test ecx, ecx
    jz   .nilstr
    mov  eax, [ecx + 0]
    shl  eax, 16
    mov  ecx, TAG_NUM
    jmp  store_value_eax_ecx
.nilstr:
    xor  eax, eax
    mov  ecx, TAG_NUM
    jmp  store_value_eax_ecx
.meta:
    mov  [meta_ops], ecx
    mov  dword [meta_ops + 4], TAG_TAB
    mov  [meta_ops + 8], ecx
    mov  dword [meta_ops + 12], TAG_TAB
    push ecx
    call _p386_meta_has_len
    add  esp, 4
    mov  ecx, [meta_ops]
    test eax, eax
    jz   .raw
    mov  eax, EV_LEN
    jmp  arith_meta_ops

op_peek:
    movzx edx, ah                    ; A
    mov  [dest_tmp], edx
    shr  eax, 16
    mov  dl, al                      ; B: RK (an address can be a constant)
    LOAD_RK
    jc   done
    cmp  ecx, TAG_NUM
    jne  err_type_num
    shr  eax, 16                      ; fixed-point address -> integer address
    and  eax, 0xffff                  ; PICO-8 64K RAM wraps
    movzx eax, byte [_p8_ram + eax]
    shl  eax, 16                      ; mem8 result is NUM
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_NUM
    NEXT

op_peek2:
    movzx edx, ah                    ; A
    mov  [dest_tmp], edx
    shr  eax, 16
    mov  dl, al                      ; B: RK (an address can be a constant)
    LOAD_RK
    jc   done
    cmp  ecx, TAG_NUM
    jne  err_type_num
    shr  eax, 16                      ; fixed-point address -> integer address
    and  eax, 0xffff
    movzx ebx, byte [_p8_ram + eax]
    inc  eax
    and  eax, 0xffff
    movzx eax, byte [_p8_ram + eax]
    shl  eax, 8
    or   eax, ebx                     ; little-endian mem16 with 64K wrap
    shl  eax, 16                      ; mem16 result is NUM
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_NUM
    NEXT

; --- control ------------------------------------------------------------
op_jmp:
    shr  eax, 16
    movsx ebx, ax
    lea  esi, [esi + ebx*4]
    test ebx, ebx
    js   .back
    NEXT
.back:                              ; loop: a safe point
    GC_POLL
    NEXT

op_jmpf:
    movzx ecx, ah
    mov  ebx, eax
    shr  ebx, 16
    movsx ebx, bx
    mov  eax, [ebp + ecx*8]
    mov  edx, [ebp + ecx*8 + 4]
    cmp  edx, TAG_NIL
    je   .take
    cmp  edx, TAG_BOOL
    jne  dispatch_next
    test eax, eax
    jnz  dispatch_next
.take:
    lea  esi, [esi + ebx*4]
    test ebx, ebx
    jns  dispatch_next
    GC_POLL                        ; backward (repeat-until): a safe point
    NEXT

op_jmpt:
    movzx ecx, ah
    mov  ebx, eax
    shr  ebx, 16
    movsx ebx, bx
    mov  eax, [ebp + ecx*8]
    mov  edx, [ebp + ecx*8 + 4]
    cmp  edx, TAG_NIL
    je   dispatch_next
    cmp  edx, TAG_BOOL
    jne  .take
    test eax, eax
    jz   dispatch_next
.take:
    lea  esi, [esi + ebx*4]
    test ebx, ebx
    jns  dispatch_next
    GC_POLL                        ; backward (repeat-until): a safe point
    NEXT

; --- loops --------------------------------------------------------------
op_forprep:
    movzx ecx, ah                  ; A
    mov  ebx, eax
    shr  ebx, 16
    movsx ebx, bx                  ; sBx (from end of instruction)
    cmp  dword [ebp + ecx*8 + 4], TAG_NUM
    jne  err_type_num
    cmp  dword [ebp + ecx*8 + 12], TAG_NUM
    jne  err_type_num
    cmp  dword [ebp + ecx*8 + 20], TAG_NUM
    jne  err_type_num
    mov  eax, [ebp + ecx*8]
    sub  eax, [ebp + ecx*8 + 16]   ; idx -= step
    mov  [ebp + ecx*8], eax
    lea  esi, [esi + ebx*4]
    NEXT

op_forloop:
    movzx ecx, ah                  ; A
    mov  ebx, eax
    shr  ebx, 16
    movsx ebx, bx                  ; sBx (normally back to loop body)
    cmp  dword [ebp + ecx*8 + 4], TAG_NUM
    jne  err_type_num
    cmp  dword [ebp + ecx*8 + 12], TAG_NUM
    jne  err_type_num
    cmp  dword [ebp + ecx*8 + 20], TAG_NUM
    jne  err_type_num
    mov  eax, [ebp + ecx*8 + 16]   ; step
    test eax, eax
    jz   err_for_step
    mov  edx, [ebp + ecx*8]        ; idx
    add  edx, eax                  ; idx += step
    mov  [ebp + ecx*8], edx
    test eax, eax
    js   .negative_step
    cmp  edx, [ebp + ecx*8 + 8]    ; positive step: idx <= limit
    jle  .take
    NEXT
.negative_step:
    cmp  edx, [ebp + ecx*8 + 8]    ; negative step: idx >= limit
    jl   dispatch_next
.take:
    mov  [ebp + ecx*8 + 24], edx   ; external loop variable R[A+3] = idx
    mov  dword [ebp + ecx*8 + 28], TAG_NUM
    lea  esi, [esi + ebx*4]
    GC_POLL                        ; loop: a safe point
    NEXT

op_tforcall:
    movzx ecx, ah                  ; A: R[A]=iterator, R[A+1]=state, R[A+2]=control
    shr  eax, 16
    movzx edx, al                  ; B = nvars (loop variable count, >= 1)
    cmp  dword [ebp + ecx*8 + 4], TAG_FUNC
    je   .lua_iter
    cmp  dword [ebp + ecx*8 + 4], TAG_CFUNC
    je   .check_state
    cmp  dword [ebp + ecx*8 + 4], TAG_NIL
    jne  err_type_iter
.check_state:
    cmp  dword [ebp + ecx*8 + 12], TAG_TAB
    jne  err_type_iter
    push edx
    push ecx
    lea  eax, [ebp + ecx*8 + 32]   ; out value R[A+4]
    push eax
    lea  eax, [ebp + ecx*8 + 24]   ; out key R[A+3]
    push eax
    lea  eax, [ebp + ecx*8 + 16]   ; current control R[A+2]
    push eax
    push dword [ebp + ecx*8 + 8]   ; table state value
    PCALL _p386_table_next, _p386_prof_table
    add  esp, 16
    pop  ecx
    pop  edx
    test eax, eax
    jnz  .got_entry
    mov  dword [ebp + ecx*8 + 24], 0
    mov  dword [ebp + ecx*8 + 28], TAG_NIL
    mov  dword [ebp + ecx*8 + 32], 0
    mov  dword [ebp + ecx*8 + 36], TAG_NIL
    NEXT
.got_entry:
    cmp  edx, 2
    jae  dispatch_next
    cmp  edx, 1
    jae  .one_result
    NEXT
.one_result:
    mov  dword [ebp + ecx*8 + 32], 0
    mov  dword [ebp + ecx*8 + 36], TAG_NIL
    NEXT

.lua_iter:
    ; TAG_FUNC iterator: call R[A](R[A+1], R[A+2]) as a normal Lua call whose
    ; returns land at R[A+3..]. This is exactly op_call's .lua_func frame push
    ; with func reg = A, nargs = 2 (args already contiguous at R[A+1..A+2]),
    ; want_rets = nvars — except return_reg is A+3 instead of A. esi already
    ; points at the following TFORLOOP, so the callee's RETURN resumes there
    ; and nil-pads missing loop vars per the want_rets contract.
    ; State/control (R[A+1], R[A+2]) may be any tag; closures ignore them.
    mov  ebx, [ebp + ecx*8]        ; P386Closure*
    test ebx, ebx
    jz   err_type_iter
    mov  [scratch_a], ecx          ; func reg A
    mov  dword [scratch_a + 4], 2  ; nargs (state, control)
    mov  [scratch_a + 8], edx      ; want_rets = nvars (>= 1, never "all")
    lea  eax, [ecx + 3]
    mov  [scratch_a + 20], eax     ; return_reg = A+3 (loop variables)
    jmp  call_push_lua_frame

op_tforloop:
    movzx ecx, ah                  ; A; R[A+3] is first result from TFORCALL
    mov  ebx, eax
    shr  ebx, 16
    movsx ebx, bx                  ; sBx
    cmp  dword [ebp + ecx*8 + 28], TAG_NIL
    je   dispatch_next
    mov  eax, [ebp + ecx*8 + 24]
    mov  edx, [ebp + ecx*8 + 28]
    mov  [ebp + ecx*8 + 16], eax   ; control R[A+2] = R[A+3]
    mov  [ebp + ecx*8 + 20], edx
    lea  esi, [esi + ebx*4]
    GC_POLL                        ; loop: a safe point
    NEXT

op_closure:
    movzx ecx, ah                  ; A
    mov  [dest_tmp], ecx
    shr  eax, 16                   ; Bx = proto index
    movzx edx, ax
    mov  ebx, [edi + VM_PROGRAM + LP_PROTOS]
    mov  eax, edx
    imul eax, 24
    add  eax, ebx                  ; eax = proto*
    movzx ebx, byte [eax + PE_N_UPVALUES]
    push ebx                       ; n_upvalues
    push eax                       ; proto pointer
    push edx                       ; proto index
    call _p386_closure_new
    add  esp, 12
    test eax, eax
    jz   err_oom
    mov  [scratch_a + 12], eax     ; new closure*

    movzx edx, byte [eax + 8]      ; n_upvalues
    test edx, edx
    jz   .store
    ; Note: parent-local upvalues (source 0) do not require a current closure;
    ; only parent-upvalue captures (source 1) dereference VM_CURRENT_CLOSURE,
    ; and that path validates it itself. The main chunk legitimately creates
    ; upvalue-bearing closures with current_closure == NULL.

    push esi
    mov  ecx, [eax + 4]            ; proto*
    mov  esi, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  esi, [ecx + PE_UPVALS_OFF]
    mov  [scratch_a + 4], edx      ; n_upvalues (clobbered by the C call below)
    xor  ecx, ecx                  ; i
.up_loop:
    cmp  ecx, [scratch_a + 4]
    jae  .up_done
    movzx eax, byte [esi + ecx*2]      ; source
    movzx ebx, byte [esi + ecx*2 + 1]  ; index
    cmp  eax, 0
    jne  .from_parent_up
    mov  [scratch_a + 0], ecx      ; spill i across cdecl call
    lea  eax, [ebp + ebx*8]
    push eax                       ; slot
    lea  eax, [edi + VM_OPEN_UPVALUES]
    push eax                       ; &head
    call _p386_upvalue_find_or_add
    add  esp, 8
    test eax, eax
    jz   .up_oom
    mov  ecx, [scratch_a + 0]      ; restore i
    mov  ebx, [scratch_a + 12]
    mov  [ebx + 12 + ecx*4], eax
    inc  ecx
    jmp  .up_loop
.from_parent_up:
    cmp  eax, 1
    jne  .up_bounds
    mov  eax, [edi + VM_CURRENT_CLOSURE]
    test eax, eax
    jz   .up_type
    movzx eax, byte [eax + 8]
    cmp  ebx, eax
    jae  .up_bounds
    mov  eax, [edi + VM_CURRENT_CLOSURE]
    mov  eax, [eax + 12 + ebx*4]
    test eax, eax
    jz   .up_type
    mov  ebx, [scratch_a + 12]
    mov  [ebx + 12 + ecx*4], eax
    inc  ecx
    jmp  .up_loop
.up_oom:
    pop  esi
    jmp  err_oom
.up_bounds:
    pop  esi
    jmp  err_bounds
.up_type:
    pop  esi
    jmp  err_type_upval
.up_done:
    pop  esi
.store:
    mov  ecx, [dest_tmp]
    mov  eax, [scratch_a + 12]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_FUNC
    NEXT

op_tailcall:
    movzx ecx, ah                  ; A: function register
    mov  edx, eax
    shr  edx, 16
    cmp  dword [ebp + ecx*8 + 4], TAG_FUNC
    je   .lua_tail
    cmp  dword [ebp + ecx*8 + 4], TAG_CFUNC
    je   .c_tail
    jmp  tailcall_meta

.c_tail:
    mov  ebx, [ebp + ecx*8]
    test ebx, ebx
    jz   err_type_func
    movzx eax, dl                  ; B = nargs + 1 (0 => from top)
    test eax, eax
    jnz  .c_fixed_args
    mov  eax, [edi + VM_TOP]
    lea  edx, [ebp + ecx*8 + 8]
    sub  eax, edx
    sar  eax, 3
    jns  .c_args_ready
    xor  eax, eax
    jmp  .c_args_ready
.c_fixed_args:
    dec  eax
.c_args_ready:
    ; current frame is going away -> close all open upvalues at base[0]
    push ecx
    push eax
    push ebp
    lea  edx, [edi + VM_OPEN_UPVALUES]
    push edx
    call _p386_close_upvalues
    add  esp, 8
    pop  eax
    pop  ecx
    push esi
    push ecx
    push eax
    push dword 0                   ; want all returns
    push eax                       ; nargs
    lea  eax, [ebp + ecx*8 + 8]
    push eax                       ; args window
    push edi
    CALL_CFUNC ebx
    add  esp, 16
    pop  ebx                       ; nargs
    pop  ecx                       ; A
    pop  esi
    test eax, eax
    jns  .c_tail_returns
    cmp  eax, P386_VM_SWITCH
    jne  .c_tail_error
    inc  ecx                       ; results go to R[A+1..], then RETURN
    mov  [edi + VM_TAIL_REG], ecx
.c_tail_error:
    mov  [edi + VM_STATUS], eax
    cmp  dword [edi + VM_ERROR_MSG], 0
    jne  done                      ; keep the builtin's own message
    mov  dword [edi + VM_ERROR_MSG], msg_type_func
    jmp  done
.c_tail_returns:
    lea  ecx, [ecx + 1]            ; return start register (first arg slot)
    mov  edx, eax                  ; actual returns
    jmp  op_return.count_ready

.lua_tail:
    mov  ebx, [ebp + ecx*8]        ; closure
    test ebx, ebx
    jz   err_type_func

    movzx eax, dl                  ; B = nargs + 1 (0 => from top)
    test eax, eax
    jnz  .lua_fixed_args
    mov  eax, [edi + VM_TOP]
    lea  edx, [ebp + ecx*8 + 8]
    sub  eax, edx
    sar  eax, 3
    jns  .lua_args_ready
    xor  eax, eax
    jmp  .lua_args_ready
.lua_fixed_args:
    dec  eax
.lua_args_ready:
    mov  [scratch_a + 4], eax      ; nargs
    mov  [scratch_a], ecx          ; func reg A

    ; current frame is going away -> close all open upvalues at base[0]
    push ebp
    lea  ecx, [edi + VM_OPEN_UPVALUES]
    push ecx
    call _p386_close_upvalues
    add  esp, 8

    mov  ecx, [scratch_a]          ; func reg A
    lea  eax, [ebp + ecx*8 + 8]    ; first arg in current frame
    mov  [scratch_a + 12], eax

    mov  eax, [ebp + ecx*8]        ; closure
    mov  [edi + VM_CURRENT_CLOSURE], eax
    mov  ebx, [eax + 4]            ; closure->proto
    mov  [edi + VM_CURRENT_PROTO], ebx

    movzx edx, byte [ebx + PE_N_REGS]
    mov  [dest_tmp], edx
    lea  eax, [ebp + edx*8]
    cmp  eax, [edi + VM_VALUE_STACK_END]
    ja   err_bounds
    mov  [edi + VM_TOP], eax

    ; Tail-call varargs: the current frame is discarded, so first reclaim its
    ; vararg window (vararg_sp := vararg_base), then collect this call's extra
    ; args (nargs - n_params) into a fresh window. The frame's saved_vararg_*
    ; (set when this frame was entered) is untouched, so the eventual return
    ; still restores the caller-of-caller's window. The copy reads the args
    ; from value_stack and writes to the separate vararg_stack (no overlap with
    ; the down-copy below).
    mov  eax, [edi + VM_VARARG_BASE]
    mov  [edi + VM_VARARG_SP], eax          ; reclaim current frame's varargs
    mov  [edi + VM_VARARG_BASE], eax
    mov  dword [edi + VM_VARARG_COUNT], 0
    test byte [ebx + PE_FLAGS], P386_PROTO_FLAG_VARARG
    jz   .tc_va_done
    mov  eax, [scratch_a + 4]               ; nargs
    movzx edx, byte [ebx + PE_N_PARAMS]
    sub  eax, edx
    jle  .tc_va_done                        ; no extra args
    mov  ecx, [edi + VM_VARARG_SP]
    mov  edx, ecx
    add  edx, eax
    cmp  edx, [edi + VM_VARARGS_MAX]
    jbe  .tc_va_count_ok
    mov  eax, [edi + VM_VARARGS_MAX]
    sub  eax, ecx
    jle  .tc_va_done
.tc_va_count_ok:
    mov  [scratch_a + 16], eax              ; nextra
    movzx edx, byte [ebx + PE_N_PARAMS]
    mov  esi, [scratch_a + 12]              ; &caller R[A+1]
    lea  esi, [esi + edx*8]                  ; skip named params
    mov  edx, [edi + VM_VARARGS]
    mov  ecx, [edi + VM_VARARG_SP]
    lea  edx, [edx + ecx*8]
    xor  ecx, ecx
.tc_va_copy:
    cmp  ecx, [scratch_a + 16]
    jae  .tc_va_store
    mov  eax, [esi + ecx*8]
    mov  [edx + ecx*8], eax
    mov  eax, [esi + ecx*8 + 4]
    mov  [edx + ecx*8 + 4], eax
    inc  ecx
    jmp  .tc_va_copy
.tc_va_store:
    mov  eax, [edi + VM_VARARG_SP]
    mov  [edi + VM_VARARG_BASE], eax
    mov  ecx, [scratch_a + 16]
    mov  [edi + VM_VARARG_COUNT], ecx
    add  eax, ecx
    mov  [edi + VM_VARARG_SP], eax
.tc_va_done:

    ; copy min(nargs, n_params) args down to R0.. before clearing
    mov  edx, [scratch_a + 4]
    movzx eax, byte [ebx + PE_N_PARAMS]
    cmp  edx, eax
    jbe  .lua_copy_count_ready
    mov  edx, eax
.lua_copy_count_ready:
    mov  [scratch_a + 8], edx      ; argcopy
    xor  ecx, ecx
    mov  eax, [scratch_a + 12]
.lua_arg_copy_loop:
    cmp  ecx, edx
    jae  .lua_clear_rest
    mov  esi, [eax + ecx*8]
    mov  [ebp + ecx*8], esi
    mov  esi, [eax + ecx*8 + 4]
    mov  [ebp + ecx*8 + 4], esi
    inc  ecx
    jmp  .lua_arg_copy_loop

.lua_clear_rest:
    ; Clear R[argcopy..n_regs) (TAG_NIL = 0, so zero both dwords).
    mov  ecx, [scratch_a + 8]
    mov  edx, [dest_tmp]
    sub  edx, ecx
    jbe  .lua_enter
    push edi
    lea  edi, [ebp + ecx*8]
    lea  ecx, [edx*2]
    xor  eax, eax
    rep  stosd
    pop  edi

.lua_enter:
    mov  esi, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  esi, [ebx + PE_BYTECODE_OFF]
    GC_POLL                        ; frame complete: a safe point
    NEXT

op_call:
    movzx ecx, ah                  ; A: function register
    mov  edx, eax
    shr  edx, 16
    cmp  dword [ebp + ecx*8 + 4], TAG_FUNC
    je   .lua_func
    cmp  dword [ebp + ecx*8 + 4], TAG_CFUNC
    jne  call_meta
    ; CFUNC fast path. The cdecl callee keeps ebx/esi/edi/ebp, so A stays
    ; in ebx and nothing is saved around the call. want_rets is read again
    ; from the instruction (C is the byte at [esi-1]) after the call.
    mov  eax, [ebp + ecx*8]        ; raw C function pointer
    test eax, eax
    jz   err_type_func
    mov  ebx, ecx                  ; A
    movzx ecx, dh                  ; C = want_rets + 1 (0 => all)
    sub  ecx, 1
    adc  ecx, 0                    ; want_rets: C-1, or 0 when C is 0
    push ecx                       ; want_rets
    movzx ecx, dl                  ; B = nargs + 1 (0 => args up to top)
    sub  ecx, 1
    jc   .args_to_top
.args_ready:
    push ecx                       ; nargs
    lea  ecx, [ebp + ebx*8 + 8]    ; args/result window R[A+1..]
    push ecx
    push edi
    CALL_CFUNC eax
    add  esp, 16
    test eax, eax                  ; result count, or a VM status (< 0)
    js   .builtin_error
    movzx edx, byte [esi - 1]      ; C of this CALL
    sub  edx, 1
    jz   .no_results               ; C = 1: a statement, keep no results
    jnc  .want_fixed
    mov  edx, eax                  ; C = 0: keep all results
.want_fixed:
    ; Move min(n, want) results from R[A+1..] down to R[A..], then pad
    ; with nil up to want. ecx walks the destination.
    lea  ecx, [ebp + ebx*8]
    cmp  eax, edx
    jbe  .count_ok
    mov  eax, edx
.count_ok:
    sub  edx, eax                  ; nil padding
    test eax, eax
    jz   .pad
.copy:
    mov  ebx, [ecx + 8]
    mov  [ecx], ebx
    mov  ebx, [ecx + 12]
    mov  [ecx + 4], ebx
    add  ecx, 8
    dec  eax
    jnz  .copy
.pad:
    test edx, edx
    jz   .copied
.pad_loop:
    mov  dword [ecx], 0
    mov  dword [ecx + 4], TAG_NIL
    add  ecx, 8
    dec  edx
    jnz  .pad_loop
.copied:
    mov  [edi + VM_TOP], ecx
.no_results:
    NEXT
.args_to_top:
    mov  ecx, [edi + VM_TOP]       ; nargs = (top - &R[A+1]) / 8
    lea  edx, [ebp + ebx*8 + 8]
    sub  ecx, edx
    sar  ecx, 3
    jns  .args_ready
    xor  ecx, ecx
    jmp  .args_ready
.builtin_error:
    mov  [edi + VM_STATUS], eax
    cmp  dword [edi + VM_ERROR_MSG], 0
    jne  done                      ; keep the builtin's own message
    mov  dword [edi + VM_ERROR_MSG], msg_type_func
    jmp  done

.lua_func:
    mov  ebx, [ebp + ecx*8]        ; P386Closure*
    test ebx, ebx
    jz   err_type_func

    ; Resolve want_rets first (frees dh for nargs computation below).
    movzx eax, dh                  ; C = want_rets + 1 (0 => all)
    test eax, eax
    jz   .lua_c_ready
    dec  eax
.lua_c_ready:
    mov  [scratch_a + 8], eax      ; want_rets (0 => all)

    movzx eax, dl                  ; B = nargs + 1 (0 => args extend to top)
    test eax, eax
    jnz  .lua_fixed_args
    ; B == 0: nargs = (top - &R[A+1]) / 8
    mov  eax, [edi + VM_TOP]
    lea  edx, [ebp + ecx*8 + 8]
    sub  eax, edx
    sar  eax, 3
    jns  .lua_nargs_ready
    xor  eax, eax
    jmp  .lua_nargs_ready
.lua_fixed_args:
    dec  eax                       ; nargs
.lua_nargs_ready:
    mov  [scratch_a + 4], eax      ; nargs
    mov  [scratch_a + 20], ecx     ; return_reg = A
    mov  eax, [ebp + ecx*8]
    mov  [scratch_a + 24], eax     ; closure
    lea  eax, [ebp + ecx*8 + 8]    ; R[A+1]: the arguments are in place,
    mov  [scratch_a + 12], eax     ; so the callee frame starts there
    mov  dword [scratch_a + 28], POST_NONE
    jmp  frame_push                ; (eax = callee base)

; Lua frame push. Entry contract (memory scratch, register-free):
;   [scratch_a]      = function register (closure in R[here], args at R[here+1..])
;   [scratch_a + 4]  = nargs
;   [scratch_a + 8]  = want_rets (0 => all)
;   [scratch_a + 20] = return_reg in the caller frame
; esi = return IP (instruction after the call site). Jumped to by
; op_tforcall's .lua_iter path with return_reg = A+3. The callee frame
; goes above the caller's registers and the arguments are copied there.
call_push_lua_frame:
    mov  ecx, [scratch_a]
    mov  eax, [ebp + ecx*8]
    mov  [scratch_a + 24], eax     ; closure
    lea  eax, [ebp + ecx*8 + 8]
    mov  [scratch_a + 12], eax     ; first arg
    mov  dword [scratch_a + 28], POST_NONE

; Entry for calls whose closure and args are not in the caller's registers
; (metamethods). Same contract as above, but instead of [scratch_a]:
;   [scratch_a + 12] = pointer to the first argument
;   [scratch_a + 24] = P386Closure*
;   [scratch_a + 28] = POST_* result fix-up
call_push_lua_frame_ptr:
    mov  eax, [edi + VM_CURRENT_PROTO]
    movzx eax, byte [eax + PE_N_REGS]
    lea  eax, [ebp + eax*8]        ; callee base = caller base + caller n_regs

; Common frame push. In: eax = callee base (R0 of the new frame), and the
; scratch contract above. For op_call the base is the first argument (the
; arguments are already in place); otherwise they are copied to the base.
; The callee's registers past its arguments are cleared to nil.
frame_push:
    mov  [frame_base], eax
    mov  eax, [edi + VM_CALL_DEPTH]
    cmp  eax, [edi + VM_FRAMES_MAX]
    jae  err_bounds
    imul eax, FRAME_SIZE
    add  eax, [edi + VM_FRAMES]
    mov  [eax + FRAME_RETURN_IP], esi
    mov  [eax + FRAME_RETURN_BASE], ebp
    mov  edx, [edi + VM_CURRENT_PROTO]
    mov  [eax + FRAME_RETURN_PROTO], edx
    mov  edx, [edi + VM_CURRENT_CLOSURE]
    mov  [eax + FRAME_RETURN_CLOSURE], edx
    mov  edx, [scratch_a + 20]
    mov  [eax + FRAME_RETURN_REG], dl
    mov  edx, [scratch_a + 8]
    mov  [eax + FRAME_WANT_RETS], dl
    mov  edx, [scratch_a + 28]
    mov  [eax + FRAME_POST], dl
    ; Save caller's vararg window so it can be restored on return.
    mov  edx, [edi + VM_VARARG_BASE]
    mov  [eax + FRAME_SAVED_VARARG_BASE], edx
    mov  edx, [edi + VM_VARARG_COUNT]
    mov  [eax + FRAME_SAVED_VARARG_COUNT], edx
    mov  edx, [edi + VM_VARARG_SP]
    mov  [eax + FRAME_SAVED_VARARG_SP], edx
    inc  dword [edi + VM_CALL_DEPTH]

    mov  eax, [scratch_a + 24]     ; closure
    mov  [edi + VM_CURRENT_CLOSURE], eax
    mov  ebx, [eax + 4]            ; closure->proto

    mov  ebp, [frame_base]
    mov  [edi + VM_BASE], ebp
    mov  [edi + VM_CURRENT_PROTO], ebx
    movzx edx, byte [ebx + PE_N_REGS]
    lea  eax, [ebp + edx*8]
    cmp  eax, [edi + VM_VALUE_STACK_END]
    ja   err_bounds
    mov  [edi + VM_TOP], eax

    ; Varargs first: in place, the extra arguments are in the registers
    ; that are cleared below.
    call setup_varargs

    ; k = min(nargs, n_params) arguments go to R[0..k).
    mov  ecx, [scratch_a + 4]
    movzx eax, byte [ebx + PE_N_PARAMS]
    cmp  ecx, eax
    jbe  .k_ready
    mov  ecx, eax
.k_ready:
    mov  eax, [scratch_a + 12]     ; arguments
    cmp  eax, ebp
    je   .args_in_place
    push ecx
    xor  edx, edx
.arg_loop:
    cmp  edx, ecx
    jae  .args_copied
    mov  esi, [eax + edx*8]
    mov  [ebp + edx*8], esi
    mov  esi, [eax + edx*8 + 4]
    mov  [ebp + edx*8 + 4], esi
    inc  edx
    jmp  .arg_loop
.args_copied:
    pop  ecx
.args_in_place:
    ; Clear R[k..n_regs) (TAG_NIL = 0, so zero both dwords).
    movzx edx, byte [ebx + PE_N_REGS]
    sub  edx, ecx
    jbe  .lua_enter
    push edi
    lea  edi, [ebp + ecx*8]
    lea  ecx, [edx*2]
    xor  eax, eax
    rep  stosd
    pop  edi

.lua_enter:
    mov  esi, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  esi, [ebx + PE_BYTECODE_OFF]
    GC_POLL                        ; frame complete: a safe point
    NEXT

; Set the callee's vararg window. In: ebx = callee proto, [scratch_a + 4] =
; nargs, [scratch_a + 12] = first argument. Clobbers eax, ecx, edx, esi.
setup_varargs:
    ; Default: empty vararg window anchored at current sp.
    mov  eax, [edi + VM_VARARG_SP]
    mov  [edi + VM_VARARG_BASE], eax
    mov  dword [edi + VM_VARARG_COUNT], 0
    ; Only vararg protos collect extra args.
    test byte [ebx + PE_FLAGS], P386_PROTO_FLAG_VARARG
    jz   .va_none
    ; nextra = nargs - n_params (clamped at 0)
    mov  eax, [scratch_a + 4]      ; nargs
    movzx edx, byte [ebx + PE_N_PARAMS]
    sub  eax, edx
    jle  .va_none                  ; <=0: no extra args
    ; clamp nextra so vararg_sp + nextra <= varargs_max
    mov  ecx, [edi + VM_VARARG_SP]
    mov  edx, ecx
    add  edx, eax
    cmp  edx, [edi + VM_VARARGS_MAX]
    jbe  .lua_va_count_ok
    mov  eax, [edi + VM_VARARGS_MAX]
    sub  eax, ecx                  ; available slots
    jle  .va_none
.lua_va_count_ok:
    mov  [scratch_a + 16], eax     ; nextra
    ; source = caller first-arg window + n_params
    movzx edx, byte [ebx + PE_N_PARAMS]
    mov  esi, [scratch_a + 12]     ; &caller R[A+1]
    lea  esi, [esi + edx*8]        ; skip named params
    ; dest = &vararg_stack[vararg_sp]
    mov  edx, [edi + VM_VARARGS]
    mov  ecx, [edi + VM_VARARG_SP]
    lea  edx, [edx + ecx*8]
    xor  ecx, ecx
.lua_va_copy:
    cmp  ecx, [scratch_a + 16]
    jae  .lua_va_done
    mov  eax, [esi + ecx*8]
    mov  [edx + ecx*8], eax
    mov  eax, [esi + ecx*8 + 4]
    mov  [edx + ecx*8 + 4], eax
    inc  ecx
    jmp  .lua_va_copy
.lua_va_done:
    mov  eax, [edi + VM_VARARG_SP]
    mov  [edi + VM_VARARG_BASE], eax
    mov  ecx, [scratch_a + 16]     ; nextra
    mov  [edi + VM_VARARG_COUNT], ecx
    add  eax, ecx
    mov  [edi + VM_VARARG_SP], eax
.va_none:
    ret

op_return:
    movzx ecx, ah                  ; A
    shr  eax, 16
    movzx edx, al                  ; B = nrets + 1, 0 = all values up to top
    test edx, edx
    jnz  .return_fixed
    ; B == 0: nrets = (top - &R[A]) / 8
    mov  edx, [edi + VM_TOP]
    lea  eax, [ebp + ecx*8]
    sub  edx, eax
    sar  edx, 3
    jns  .count_ready
    xor  edx, edx
    jmp  .count_ready
.return_fixed:
    dec  edx
.count_ready:
    cmp  dword [edi + VM_CALL_DEPTH], 0
    jne  .return_to_caller
    lea  eax, [ebp + ecx*8]
    mov  [edi + VM_RET_BASE], eax  ; a coroutine's results start here
    lea  eax, [eax + edx*8]
    mov  [edi + VM_TOP], eax
    jmp  done_halted

.return_to_caller:
    lea  eax, [ebp + ecx*8]
    mov  [scratch_a + 8], eax      ; first result (callee R[A])
    mov  [scratch_a + 4], edx      ; actual returns
    dec  dword [edi + VM_CALL_DEPTH]
    mov  eax, [edi + VM_CALL_DEPTH]
    imul eax, FRAME_SIZE
    add  eax, [edi + VM_FRAMES]
    mov  esi, [eax + FRAME_RETURN_IP]
    mov  ebx, [eax + FRAME_RETURN_PROTO]
    mov  [edi + VM_CURRENT_PROTO], ebx
    mov  ebx, [eax + FRAME_RETURN_CLOSURE]
    mov  [edi + VM_CURRENT_CLOSURE], ebx
    mov  ebp, [eax + FRAME_RETURN_BASE]
    mov  [edi + VM_BASE], ebp
    ; Restore caller's vararg window.
    mov  edx, [eax + FRAME_SAVED_VARARG_BASE]
    mov  [edi + VM_VARARG_BASE], edx
    mov  edx, [eax + FRAME_SAVED_VARARG_COUNT]
    mov  [edi + VM_VARARG_COUNT], edx
    mov  edx, [eax + FRAME_SAVED_VARARG_SP]
    mov  [edi + VM_VARARG_SP], edx
    movzx ecx, byte [eax + FRAME_RETURN_REG]
    mov  [scratch_a], ecx
    movzx ebx, byte [eax + FRAME_POST]
    mov  [scratch_a + 12], ebx
    movzx edx, byte [eax + FRAME_WANT_RETS]
    test edx, edx
    jnz  .ret_want
    mov  edx, [scratch_a + 4]      ; want all: as many as returned
.ret_want:
    ; Copy min(actual, want) values to caller R[return_reg..], pad the rest
    ; with nil. The destination is below the source (the callee frame is
    ; above R[return_reg]), so a forward copy is safe.
    lea  ebx, [ebp + ecx*8]        ; destination
    mov  ecx, [scratch_a + 4]
    cmp  ecx, edx
    jbe  .ret_count
    mov  ecx, edx
.ret_count:
    sub  edx, ecx
    mov  [scratch_a + 16], edx     ; nil padding
    mov  eax, [scratch_a + 8]      ; source
    test ecx, ecx
    jz   .ret_pad
.ret_copy:
    mov  edx, [eax]
    mov  [ebx], edx
    mov  edx, [eax + 4]
    mov  [ebx + 4], edx
    add  eax, 8
    add  ebx, 8
    dec  ecx
    jnz  .ret_copy
.ret_pad:
    mov  ecx, [scratch_a + 16]
    test ecx, ecx
    jz   .ret_done
.ret_pad_loop:
    mov  dword [ebx], 0
    mov  dword [ebx + 4], TAG_NIL
    add  ebx, 8
    dec  ecx
    jnz  .ret_pad_loop
.ret_done:
    mov  [edi + VM_TOP], ebx
    cmp  dword [scratch_a + 12], POST_NONE
    jne  .post
    NEXT
.post:
    ; A metamethod frame (__eq, __lt, __le): make the result a boolean.
    mov  ebx, [scratch_a]
    mov  ecx, [scratch_a + 12]
    call fix_bool
    NEXT

; VARARG A B: copy the current frame's varargs into R[A..].
;   B == 0 : copy all `vararg_count` values, set top = &R[A+count].
;   B  > 0 : copy B-1 values, nil-padding when fewer varargs are available.
op_vararg:
    movzx ecx, ah                  ; A (destination register)
    shr  eax, 16
    movzx edx, al                  ; B
    test edx, edx
    jnz  .va_fixed
    ; want all: nwant = vararg_count
    mov  edx, [edi + VM_VARARG_COUNT]
    jmp  .va_have_count
.va_fixed:
    dec  edx                       ; nwant = B-1
.va_have_count:
    mov  [scratch_a], ecx          ; dest reg A
    mov  [scratch_a + 4], edx      ; nwant
    ; source = &vararg_stack[vararg_base]
    mov  ebx, [edi + VM_VARARGS]
    mov  eax, [edi + VM_VARARG_BASE]
    lea  ebx, [ebx + eax*8]        ; ebx = &varargs[0]
    mov  [scratch_a + 8], ebx
    xor  eax, eax                  ; i
.va_loop:
    cmp  eax, [scratch_a + 4]
    jae  .va_done
    mov  ecx, [scratch_a]
    add  ecx, eax                  ; dest reg index A+i
    cmp  eax, [edi + VM_VARARG_COUNT]
    jae  .va_pad
    mov  ebx, [scratch_a + 8]
    mov  edx, [ebx + eax*8]
    mov  [ebp + ecx*8], edx
    mov  edx, [ebx + eax*8 + 4]
    mov  [ebp + ecx*8 + 4], edx
    jmp  .va_next
.va_pad:
    mov  dword [ebp + ecx*8], 0
    mov  dword [ebp + ecx*8 + 4], TAG_NIL
.va_next:
    inc  eax
    jmp  .va_loop
.va_done:
    ; Publish top = &R[A + nwant]. In the want-all (B==0) case this lets a
    ; following CALL/RETURN consume the spread values via its own B==0 path;
    ; in the fixed case the next instruction overwrites top as needed, so this
    ; is harmless.
    mov  eax, [scratch_a]
    add  eax, [scratch_a + 4]
    lea  edx, [ebp + eax*8]
    mov  [edi + VM_TOP], edx
    NEXT

; --- intrinsics (pure math builtins) -------------------------------------
; A, RK(B)[, RK(C)]. A value that is not a number counts as 0, as in the
; C builtins (arg_fp default).

%macro MATH1 0                      ; out: eax = RK(B) as a number
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    LOAD_RK
    jc   done
    cmp  ecx, TAG_NUM
    je   %%num
    xor  eax, eax
%%num:
%endmacro

%macro MATH2 0                      ; out: ebx = RK(B), eax = RK(C)
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    mov  dh, ah
    LOAD_RK
    jc   done
    cmp  ecx, TAG_NUM
    je   %%a
    xor  eax, eax
%%a:
    push eax
    mov  dl, dh
    LOAD_RK
    jc   err_rk_pop1
    cmp  ecx, TAG_NUM
    je   %%b
    xor  eax, eax
%%b:
    pop  ebx
%endmacro

%macro STORE_NUM 0                  ; R[dest_tmp] = eax (number)
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_NUM
    NEXT
%endmacro

op_flr:
    MATH1
    and  eax, 0xffff0000
    STORE_NUM

op_ceil:
    MATH1
    test eax, 0xffff
    jz   .whole
    and  eax, 0xffff0000
    add  eax, 0x10000
.whole:
    STORE_NUM

op_abs:                             ; abs(-32768) saturates to 0x7fffffff
    MATH1
    cmp  eax, 0x80000000
    je   .sat
    test eax, eax
    jns  .pos
    neg  eax
.pos:
    STORE_NUM
.sat:
    mov  eax, 0x7fffffff
    STORE_NUM

op_sgn:                             ; sgn(0) = 1
    MATH1
    test eax, eax
    mov  eax, 0x10000
    jns  .pos
    neg  eax
.pos:
    STORE_NUM

op_min:
    MATH2
    cmp  ebx, eax
    jge  .keep
    mov  eax, ebx
.keep:
    STORE_NUM

op_max:
    MATH2
    cmp  ebx, eax
    jle  .keep
    mov  eax, ebx
.keep:
    STORE_NUM

; --- metamethods -----------------------------------------------------------
; The C helpers in p386_meta.c find the handler. A Lua handler is not called
; from C: it is staged in p386_meta_call[], and a normal Lua frame is pushed
; here, so the VM never re-enters p386_vm_run.

; Push a frame for the staged call. In: eax = return register, edx =
; want_rets (0 => all), ecx = POST_* code. esi = return IP.
meta_call:
    mov  [scratch_a + 20], eax
    mov  [scratch_a + 8], edx
    mov  [scratch_a + 28], ecx
    mov  eax, [_p386_meta_call]    ; p386_meta_call[0].value: closure
    mov  [scratch_a + 24], eax
    mov  dword [scratch_a + 12], _p386_meta_call + 8
    mov  eax, [_p386_meta_nargs]
    mov  [scratch_a + 4], eax
    jmp  call_push_lua_frame_ptr

; In: eax = P386_META_* result. DONE: the result is in R[dest_tmp].
; CALL: the handler's first result goes to R[dest_tmp]. <0: error.
meta_result:
    test eax, eax
    jz   dispatch_next
    js   done
    mov  eax, [dest_tmp]
    mov  edx, 1
    mov  ecx, POST_NONE
    jmp  meta_call

; As meta_result, but the handler's results are not used (__newindex). They
; go to the first register above this frame, which is free after the call.
meta_result_discard:
    test eax, eax
    jz   dispatch_next
    js   done
    mov  eax, [edi + VM_CURRENT_PROTO]
    movzx eax, byte [eax + PE_N_REGS]
    mov  edx, 1
    mov  ecx, POST_NONE
    jmp  meta_call

; Make R[ebx] a boolean from its truth value. ecx = POST_BOOL or POST_NOT.
; Clobbers eax.
fix_bool:
    mov  eax, [ebp + ebx*8 + 4]
    cmp  eax, TAG_NIL
    je   .falsy
    cmp  eax, TAG_BOOL
    jne  .truthy
    cmp  dword [ebp + ebx*8], 0
    je   .falsy
.truthy:
    mov  eax, 1
    jmp  .apply
.falsy:
    xor  eax, eax
.apply:
    cmp  ecx, POST_NOT
    jne  .store
    xor  eax, 1
.store:
    mov  [ebp + ebx*8], eax
    mov  dword [ebp + ebx*8 + 4], TAG_BOOL
    ret

; Binary operator with an operand that is not a number. In: eax = event.
; The instruction is at [esi-4]: A, B (RK), C (RK).
arith_meta:
    push eax                       ; event
    mov  edx, [esi - 4]
    shr  edx, 16                   ; dl = B, dh = C
    LOAD_RK
    jc   err_rk_pop1
    mov  [meta_ops], eax
    mov  [meta_ops + 4], ecx
    mov  dl, dh
    LOAD_RK
    jc   err_rk_pop1
    mov  [meta_ops + 8], eax
    mov  [meta_ops + 12], ecx
    pop  eax
; Entry with both operands in meta_ops. In: eax = event.
arith_meta_ops:
    mov  ecx, [dest_tmp]
    lea  ecx, [ebp + ecx*8]
    push ecx                       ; out
    push dword meta_ops + 8
    push dword meta_ops
    push eax
    push edi
    call _p386_meta_arith
    add  esp, 20
    jmp  meta_result

; Unary minus on a value that is not a number: __unm(a, a).
neg_meta:
    mov  [meta_ops], eax
    mov  [meta_ops + 4], ecx
    mov  [meta_ops + 8], eax
    mov  [meta_ops + 12], ecx
    mov  eax, EV_UNM
    jmp  arith_meta_ops

; CONCAT failed (an operand is not a string or number): try __concat.
; op_concat left both operands in meta_ops.
concat_meta:
    mov  eax, EV_CONCAT
    jmp  arith_meta_ops

; EQ/NE on two different tables. In: edx = left, eax = right table,
; [meta_post] = POST_BOOL (EQ) or POST_NOT (NE).
eq_meta:
    mov  [meta_ops], edx
    mov  dword [meta_ops + 4], TAG_TAB
    mov  [meta_ops + 8], eax
    mov  dword [meta_ops + 12], TAG_TAB
    mov  eax, EV_EQ
; Entry with both operands in meta_ops. In: eax = event (EQ, LT or LE).
bool_meta_ops:
    mov  ecx, [dest_tmp]
    lea  ecx, [ebp + ecx*8]
    push ecx                       ; out
    push dword meta_ops + 8
    push dword meta_ops
    push eax
    push edi
    call _p386_meta_arith
    add  esp, 20
    test eax, eax
    js   done
    jnz  .call
    mov  ebx, [dest_tmp]           ; DONE: no handler (false) or a CFUNC
    mov  ecx, [meta_post]
    call fix_bool
    NEXT
.call:
    mov  eax, [dest_tmp]
    mov  edx, 1
    mov  ecx, [meta_post]
    jmp  meta_call

; LT/LE/GT/GE with operands that are not two numbers or two strings.
; In: eax = EV_LT or EV_LE, ecx = 1 to swap (a > b is b < a).
; The instruction is at [esi-4]: A, B (RK), C (RK).
cmp_meta:
    push eax
    push ecx
    mov  edx, [esi - 4]
    shr  edx, 16                   ; dl = B, dh = C
    LOAD_RK
    jc   err_rk_pop2
    mov  [meta_ops], eax
    mov  [meta_ops + 4], ecx
    mov  dl, dh
    LOAD_RK
    jc   err_rk_pop2
    mov  [meta_ops + 8], eax
    mov  [meta_ops + 12], ecx
    pop  ecx
    test ecx, ecx
    jz   .order_ok
    mov  eax, [meta_ops]
    mov  ebx, [meta_ops + 8]
    mov  [meta_ops], ebx
    mov  [meta_ops + 8], eax
    mov  eax, [meta_ops + 4]
    mov  ebx, [meta_ops + 12]
    mov  [meta_ops + 4], ebx
    mov  [meta_ops + 12], eax
.order_ok:
    pop  eax
    mov  dword [meta_post], POST_BOOL
    jmp  bool_meta_ops

; Count the args of CALL/TAILCALL. In: ecx = A, dl = B. Out: eax = nargs.
; Clobbers ebx.
call_nargs:
    movzx eax, dl
    test eax, eax
    jnz  .fixed
    mov  eax, [edi + VM_TOP]
    lea  ebx, [ebp + ecx*8 + 8]
    sub  eax, ebx
    sar  eax, 3
    jns  .ready
    xor  eax, eax
.ready:
    ret
.fixed:
    dec  eax
    ret

; CALL of a value that is not a function: try __call(obj, args...).
; In: ecx = A, dl = B, dh = C.
call_meta:
    call call_nargs
    movzx ebx, dh                  ; C = want_rets + 1 (0 => all)
    push ebx
    push ecx
    push eax                       ; nargs
    lea  ebx, [ebp + ecx*8 + 8]
    push ebx                       ; args
    lea  ebx, [ebp + ecx*8]
    push ebx                       ; obj
    push edi
    call _p386_meta_call_value
    add  esp, 16
    pop  ecx                       ; A
    pop  edx                       ; C
    test eax, eax
    js   done
    test edx, edx
    jz   .want_all
    dec  edx
.want_all:
    mov  eax, ecx                  ; results go to R[A..]
    mov  ecx, POST_NONE
    jmp  meta_call

; TAILCALL of a value that is not a function. Write the staged call
; (handler, obj, args) over R[A..] and do the tail call with it.
; In: ecx = A, dl = B.
tailcall_meta:
    call call_nargs
    push ecx
    push eax                       ; nargs
    lea  ebx, [ebp + ecx*8 + 8]
    push ebx                       ; args
    lea  ebx, [ebp + ecx*8]
    push ebx                       ; obj
    push edi
    call _p386_meta_call_value
    add  esp, 16
    pop  ecx                       ; A
    test eax, eax
    js   done
    mov  eax, [_p386_meta_nargs]
    inc  eax                       ; values: handler + args
    lea  ebx, [ebp + ecx*8]
    lea  edx, [ebx + eax*8]
    cmp  edx, [edi + VM_VALUE_STACK_END]
    ja   err_bounds
    push esi
    xor  edx, edx
.copy:
    cmp  edx, eax
    jae  .copied
    mov  esi, [_p386_meta_call + edx*8]
    mov  [ebx + edx*8], esi
    mov  esi, [_p386_meta_call + edx*8 + 4]
    mov  [ebx + edx*8 + 4], esi
    inc  edx
    jmp  .copy
.copied:
    pop  esi
    mov  edx, eax                  ; B = nargs + 1
    jmp  op_tailcall.lua_tail

%ifdef PROFILE_OPS
; Per-opcode cost (PROFOPS.EXE): instructions from one dispatch to the next
; go to the opcode that ran, including the C helpers it called and a fixed
; profiling overhead. In: edx = the opcode about to run. Keeps registers.
prof_op:
    push eax
    push ecx
    push edx
    rdtsc
    mov  ecx, eax
    sub  eax, [prof_prev_t]
    mov  [prof_prev_t], ecx
    mov  ecx, [prof_prev_op]
    add  [_p386_prof_op_cost + ecx*4], eax
    pop  edx
    mov  [prof_prev_op], edx
    inc  dword [_p386_prof_op_count + edx*4]
    pop  ecx
    pop  eax
    ret
%endif

op_unimpl:
    mov  dword [edi + VM_STATUS], ERR_UNIMPL
    mov  dword [edi + VM_ERROR_MSG], msg_unimpl
    jmp  done

err_bounds:
    mov  dword [edi + VM_STATUS], ERR_BOUNDS
    mov  dword [edi + VM_ERROR_MSG], msg_bounds
    jmp  done
err_rk_pop2:
    add  esp, 8
    jmp  done
err_rk_pop1:
    add  esp, 4
    jmp  done
err_rk_pop3:
    add  esp, 12
    jmp  done
err_rk_pop24:
    add  esp, 24
    jmp  done
err_type_num_pop:
    add  esp, 4
err_type_num:
    mov  dword [edi + VM_STATUS], ERR_TYPE
    mov  dword [edi + VM_ERROR_MSG], msg_type_num
    jmp  done
err_div0:
    mov  dword [edi + VM_STATUS], ERR_DIV0
    mov  dword [edi + VM_ERROR_MSG], msg_div0
    jmp  done
err_type_tab_pop4:
    add  esp, 4
err_type_tab:
    mov  dword [edi + VM_STATUS], ERR_TYPE
    mov  dword [edi + VM_ERROR_MSG], msg_type_tab
    jmp  done
err_type_str:
    mov  dword [edi + VM_STATUS], ERR_TYPE
    mov  dword [edi + VM_ERROR_MSG], msg_type_str
    jmp  done
err_type_iter:
    mov  dword [edi + VM_STATUS], ERR_TYPE
    mov  dword [edi + VM_ERROR_MSG], msg_type_iter
    jmp  done
err_type_func:
    mov  dword [edi + VM_STATUS], ERR_TYPE
    mov  dword [edi + VM_ERROR_MSG], msg_type_func
    jmp  done
err_type_upval:
    mov  dword [edi + VM_STATUS], ERR_TYPE
    mov  dword [edi + VM_ERROR_MSG], msg_type_upval
    jmp  done
err_bounds_pop4:
    add  esp, 4
    jmp  err_bounds
err_oom:
    mov  dword [edi + VM_STATUS], ERR_BOUNDS
    mov  dword [edi + VM_ERROR_MSG], msg_oom
    jmp  done
err_for_step:
    mov  dword [edi + VM_STATUS], ERR_TYPE
    mov  dword [edi + VM_ERROR_MSG], msg_for_step
    jmp  done
err_vararg:
    mov  dword [edi + VM_STATUS], ERR_UNIMPL
    mov  dword [edi + VM_ERROR_MSG], msg_vararg
    jmp  done
bad_opcode:
    mov  dword [edi + VM_STATUS], ERR_OPCODE
    mov  dword [edi + VM_ERROR_MSG], msg_bad_opcode
    jmp  done

done_halted:
    mov  dword [edi + VM_STATUS], VM_HALTED
done:
    mov  [edi + VM_BASE], ebp
    mov  [edi + VM_IP], esi
    movzx eax, byte [esi - 4]      ; opcode of the last instruction run
    mov  [edi + VM_LAST_OPCODE], eax
    mov  eax, [edi + VM_STATUS]
    pop  edi
    pop  esi
    pop  ebx
    pop  ebp
    ret
