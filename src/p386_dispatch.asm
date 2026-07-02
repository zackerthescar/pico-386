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
extern _p386_closure_new
extern _p386_upvalue_find_or_add
extern _p386_close_upvalues

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
%else
    dd op_unimpl
%endif
%assign i i+1
%endrep

section .bss
align 4
dest_tmp resd 1
scratch_a resd 8

extern _p8_ram

section .text
global _p386_vm_run:function
_p386_vm_run:
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi

    mov  edi, [ebp+8]              ; VMState*
    mov  ebx, [edi + VM_CURRENT_PROTO]
    mov  esi, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  esi, [ebx + PE_BYTECODE_OFF]
    mov  ebp, [edi + VM_BASE]      ; EBP is VM register base in dispatch

dispatch_next:
    mov  [edi + VM_IP], esi
    mov  eax, [esi]
    add  esi, 4
    movzx edx, al
    mov  [edi + VM_LAST_OPCODE], edx
    jmp  [dispatch_table + edx*4]

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
    movzx eax, byte [ecx + PE_N_CONSTS]
    cmp  ebx, eax
    jae  .bounds
    mov  eax, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  eax, [ecx + PE_CONSTS_OFF]
    mov  ecx, [eax + ebx*8 + 4]
    mov  eax, [eax + ebx*8]
    cmp  ecx, TAG_STR
    jne  .ret_ok
    push edx
    push eax
    mov  edx, [edi + VM_PROGRAM + LP_STRING_ENTRIES]
    lea  edx, [edx + eax*8]
    push dword [edx + 4]
    mov  eax, [edi + VM_PROGRAM + LP_BUF]
    add  eax, [edx]
    push eax
    call _p386_string_intern
    add  esp, 8
    pop  ecx
    pop  edx
    test eax, eax
    jz   .bounds
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
    jmp  dispatch_next

; --- movement -----------------------------------------------------------
op_move:
    movzx ecx, ah                  ; A
    shr  eax, 16
    movzx edx, al                  ; B
    mov  ebx, [ebp + edx*8]
    mov  edx, [ebp + edx*8 + 4]
    mov  [ebp + ecx*8], ebx
    mov  [ebp + ecx*8 + 4], edx
    jmp  dispatch_next

op_loadk:
    movzx ecx, ah                  ; A
    shr  eax, 16                   ; Bx
    movzx edx, ax
    mov  ebx, [edi + VM_CURRENT_PROTO]
    movzx eax, byte [ebx + PE_N_CONSTS]
    cmp  edx, eax
    jae  err_bounds
    mov  eax, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  eax, [ebx + PE_CONSTS_OFF]
    mov  ebx, [eax + edx*8]
    mov  edx, [eax + edx*8 + 4]
    cmp  edx, TAG_STR
    jne  .store_raw
    push ecx
    mov  eax, [edi + VM_PROGRAM + LP_STRING_ENTRIES]
    lea  eax, [eax + ebx*8]
    push dword [eax + 4]
    mov  ebx, [edi + VM_PROGRAM + LP_BUF]
    add  ebx, [eax]
    push ebx
    call _p386_string_intern
    add  esp, 8
    pop  ecx
    test eax, eax
    jz   err_oom
    mov  ebx, eax
    mov  edx, TAG_STR
.store_raw:
    mov  [ebp + ecx*8], ebx
    mov  [ebp + ecx*8 + 4], edx
    jmp  dispatch_next

op_loadt:
    movzx ecx, ah
    mov  dword [ebp + ecx*8], 1
    mov  dword [ebp + ecx*8 + 4], TAG_BOOL
    jmp  dispatch_next

op_loadf:
    movzx ecx, ah
    mov  dword [ebp + ecx*8], 0
    mov  dword [ebp + ecx*8 + 4], TAG_BOOL
    jmp  dispatch_next

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

op_getglobal:
    movzx ecx, ah
    shr  eax, 16
    movzx edx, al
    mov  ebx, [edi + VM_GLOBALS + edx*8]
    mov  edx, [edi + VM_GLOBALS + edx*8 + 4]
    mov  [ebp + ecx*8], ebx
    mov  [ebp + ecx*8 + 4], edx
    jmp  dispatch_next

op_setglobal:
    movzx ecx, ah
    shr  eax, 16
    movzx edx, al
    mov  ebx, [ebp + ecx*8]
    mov  ecx, [ebp + ecx*8 + 4]
    mov  [edi + VM_GLOBALS + edx*8], ebx
    mov  [edi + VM_GLOBALS + edx*8 + 4], ecx
    jmp  dispatch_next

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
    jmp  dispatch_next

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
    jmp  dispatch_next

op_close:
    movzx ecx, ah                  ; A
    lea  eax, [ebp + ecx*8]
    push eax
    lea  eax, [edi + VM_OPEN_UPVALUES]
    push eax
    call _p386_close_upvalues
    add  esp, 8
    jmp  dispatch_next

; --- objects -------------------------------------------------------------
store_value_eax_ecx:
    mov  ebx, [dest_tmp]
    mov  [ebp + ebx*8], eax
    mov  [ebp + ebx*8 + 4], ecx
    jmp  dispatch_next

op_newtable:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    movzx edx, al
    movzx eax, ah
    push eax
    push edx
    call _p386_table_new
    add  esp, 8
    test eax, eax
    jz   err_oom
    mov  ecx, TAG_TAB
    jmp  store_value_eax_ecx

build_key_rk:
    call load_rk
    jc   done
    mov  [scratch_a + 0], eax
    mov  [scratch_a + 4], ecx
    ret

op_gettable:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    movzx ebx, al
    mov  ecx, [ebp + ebx*8 + 4]
    cmp  ecx, TAG_TAB
    jne  err_type_tab
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
    call _p386_table_get
    add  esp, 12
    jmp  dispatch_next

op_settable:
    movzx ebx, ah
    mov  ecx, [ebp + ebx*8 + 4]
    cmp  ecx, TAG_TAB
    jne  err_type_tab
    mov  ebx, [ebp + ebx*8]
    push eax
    push ebx
    sub  esp, 16
    mov  edx, [esp + 20]
    shr  edx, 16
    call load_rk
    jc   err_rk_pop24
    mov  [esp + 0], eax
    mov  [esp + 4], ecx
    mov  edx, [esp + 20]
    shr  edx, 24
    call load_rk
    jc   err_rk_pop24
    mov  [esp + 8], eax
    mov  [esp + 12], ecx
    lea  eax, [esp + 8]
    lea  edx, [esp + 0]
    push eax
    push edx
    push dword [esp + 24]
    call _p386_table_set
    add  esp, 12
    add  esp, 24
    jmp  dispatch_next

; load_kstr: const idx zero-extended in edx. on success eax=interned String*,
; ecx=TAG_STR, CF=0. on failure sets vm error and CF=1. clobbers ebx.
load_kstr:
    mov  ebx, [edi + VM_CURRENT_PROTO]
    movzx eax, byte [ebx + PE_N_CONSTS]
    cmp  edx, eax
    jae  .bounds
    mov  eax, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  eax, [ebx + PE_CONSTS_OFF]
    mov  ecx, [eax + edx*8 + 4]
    cmp  ecx, TAG_STR
    jne  .typestr
    mov  eax, [eax + edx*8]
    mov  edx, [edi + VM_PROGRAM + LP_STRING_ENTRIES]
    lea  edx, [edx + eax*8]
    push dword [edx + 4]
    mov  eax, [edi + VM_PROGRAM + LP_BUF]
    add  eax, [edx]
    push eax
    call _p386_string_intern
    add  esp, 8
    test eax, eax
    jz   .oom
    mov  ecx, TAG_STR
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

op_getfield:
    movzx ecx, ah                  ; A
    mov  [dest_tmp], ecx
    shr  eax, 16
    movzx ebx, al                  ; B
    mov  edx, [ebp + ebx*8 + 4]
    cmp  edx, TAG_TAB
    jne  err_type_tab
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
    call _p386_table_get
    add  esp, 12
    add  esp, 8
    jmp  dispatch_next

op_setfield:
    movzx ebx, ah                  ; A (table reg)
    mov  ecx, [ebp + ebx*8 + 4]
    cmp  ecx, TAG_TAB
    jne  err_type_tab
    push dword [ebp + ebx*8]       ; table ptr
    push eax                       ; save instruction word
    shr  eax, 16
    movzx edx, al                  ; B (const idx)
    call load_kstr
    pop  edx                       ; instruction word
    pop  ebx                       ; table ptr
    jc   done
    shr  edx, 24                   ; C (value reg)
    push ecx                       ; key tag
    push eax                       ; key value
    push dword [ebp + edx*8 + 4]   ; val tag
    push dword [ebp + edx*8]       ; val value
    lea  eax, [esp + 0]            ; val ptr
    lea  ecx, [esp + 8]            ; key ptr
    push eax
    push ecx
    push ebx                       ; table
    call _p386_table_set
    add  esp, 12
    add  esp, 16
    jmp  dispatch_next

op_concat:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    movzx ebx, al
    movzx edx, ah
    lea  eax, [ebp + edx*8]
    push eax
    lea  eax, [ebp + ebx*8]
    push eax
    call _p386_value_concat
    add  esp, 8
    test eax, eax
    jz   err_type_str
    mov  ecx, TAG_STR
    jmp  store_value_eax_ecx

; --- numeric factory ----------------------------------------------------
%macro NUM_BIN 2
%1:
    movzx ecx, ah                    ; A saved
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al                    ; B RK
    mov  dh, ah                    ; C RK
    call load_rk
    jc   done
    cmp  ecx, TAG_NUM
    jne  err_type_num
    push eax                       ; left value
    mov  dl, dh
    call load_rk
    jc   err_rk_pop1
    cmp  ecx, TAG_NUM
    jne  err_type_num_pop
    mov  ebx, eax                  ; right
    pop  eax                       ; left
    %2
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_NUM
    jmp  dispatch_next
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

NUM_BIN op_add, DO_ADD
NUM_BIN op_sub, DO_SUB
NUM_BIN op_mul, DO_MUL
NUM_BIN op_div, DO_DIV
NUM_BIN op_idiv, DO_IDIV
NUM_BIN op_mod, DO_MOD
NUM_BIN op_pow, DO_POW
NUM_BIN op_band, DO_BAND
NUM_BIN op_bor, DO_BOR
NUM_BIN op_bxor, DO_BXOR
NUM_BIN op_shl, DO_SHL
NUM_BIN op_shr, DO_SHR
NUM_BIN op_lshr, DO_LSHR
NUM_BIN op_rotl, DO_ROTL
NUM_BIN op_rotr, DO_ROTR

op_neg:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    call load_rk
    jc   done
    cmp  ecx, TAG_NUM
    jne  err_type_num
    neg  eax
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_NUM
    jmp  dispatch_next

op_bnot:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    call load_rk
    jc   done
    cmp  ecx, TAG_NUM
    jne  err_type_num
    sar  eax, 16
    not  eax
    shl  eax, 16
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_NUM
    jmp  dispatch_next

; --- comparisons / boolean ---------------------------------------------
%macro CMP_NUM 2
%1:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    mov  dh, ah
    call load_rk
    jc   done
    cmp  ecx, TAG_NUM
    jne  err_type_num
    push eax
    mov  dl, dh
    call load_rk
    jc   err_rk_pop1
    cmp  ecx, TAG_NUM
    jne  err_type_num_pop
    mov  ebx, eax
    pop  eax
    cmp  eax, ebx
    %2 al
    jmp  store_bool_al
%endmacro

op_eq:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    mov  dh, ah
    call load_rk
    jc   done
    push eax
    push ecx
    mov  dl, dh
    call load_rk
    jc   err_rk_pop2
    pop  ebx                       ; left tag
    pop  edx                       ; left value
    cmp  ebx, ecx
    jne  .false
    cmp  edx, eax
    sete al
    jmp  store_bool_al
.false:
    xor  al, al
    jmp  store_bool_al

op_ne:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al
    mov  dh, ah
    call load_rk
    jc   done
    push eax
    push ecx
    mov  dl, dh
    call load_rk
    jc   err_rk_pop2
    pop  ebx
    pop  edx
    cmp  ebx, ecx
    jne  .true
    cmp  edx, eax
    setne al
    jmp  store_bool_al
.true:
    mov  al, 1
    jmp  store_bool_al

CMP_NUM op_lt, setl
CMP_NUM op_le, setle
CMP_NUM op_gt, setg
CMP_NUM op_ge, setge

op_not:
    movzx ecx, ah
    mov  [dest_tmp], ecx
    shr  eax, 16
    movzx edx, al
    mov  eax, [ebp + edx*8]
    mov  ecx, [ebp + edx*8 + 4]
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
    movzx ebx, al                    ; B
    mov  edx, [ebp + ebx*8 + 4]
    mov  ecx, [ebp + ebx*8]
    cmp  edx, TAG_STR
    je   .str
    cmp  edx, TAG_TAB
    jne  err_type_tab
    push ecx
    call _p386_table_len
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

op_peek:
    movzx edx, ah                    ; A
    mov  [dest_tmp], edx
    shr  eax, 16
    movzx edx, al                    ; B register (not RK)
    mov  eax, [ebp + edx*8]
    mov  ecx, [ebp + edx*8 + 4]
    cmp  ecx, TAG_NUM
    jne  err_type_num
    shr  eax, 16                      ; fixed-point address -> integer address
    and  eax, 0xffff                  ; PICO-8 64K RAM wraps
    movzx eax, byte [_p8_ram + eax]
    shl  eax, 16                      ; mem8 result is NUM
    mov  ecx, [dest_tmp]
    mov  [ebp + ecx*8], eax
    mov  dword [ebp + ecx*8 + 4], TAG_NUM
    jmp  dispatch_next

op_peek2:
    movzx edx, ah                    ; A
    mov  [dest_tmp], edx
    shr  eax, 16
    movzx edx, al                    ; B register (not RK)
    mov  eax, [ebp + edx*8]
    mov  ecx, [ebp + edx*8 + 4]
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
    jmp  dispatch_next

; --- control ------------------------------------------------------------
op_jmp:
    shr  eax, 16
    movsx ebx, ax
    lea  esi, [esi + ebx*4]
    jmp  dispatch_next

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
    jmp  dispatch_next

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
    jmp  dispatch_next

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
    jmp  dispatch_next

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
    jmp  dispatch_next
.negative_step:
    cmp  edx, [ebp + ecx*8 + 8]    ; negative step: idx >= limit
    jl   dispatch_next
.take:
    mov  [ebp + ecx*8 + 24], edx   ; external loop variable R[A+3] = idx
    mov  dword [ebp + ecx*8 + 28], TAG_NUM
    lea  esi, [esi + ebx*4]
    jmp  dispatch_next

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
    call _p386_table_next
    add  esp, 16
    pop  ecx
    pop  edx
    test eax, eax
    jnz  .got_entry
    mov  dword [ebp + ecx*8 + 24], 0
    mov  dword [ebp + ecx*8 + 28], TAG_NIL
    mov  dword [ebp + ecx*8 + 32], 0
    mov  dword [ebp + ecx*8 + 36], TAG_NIL
    jmp  dispatch_next
.got_entry:
    cmp  edx, 2
    jae  dispatch_next
    cmp  edx, 1
    jae  .one_result
    jmp  dispatch_next
.one_result:
    mov  dword [ebp + ecx*8 + 32], 0
    mov  dword [ebp + ecx*8 + 36], TAG_NIL
    jmp  dispatch_next

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
    jmp  dispatch_next

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
    jmp  dispatch_next

op_tailcall:
    movzx ecx, ah                  ; A: function register
    mov  edx, eax
    shr  edx, 16
    cmp  dword [ebp + ecx*8 + 4], TAG_FUNC
    je   .lua_tail
    cmp  dword [ebp + ecx*8 + 4], TAG_CFUNC
    je   .c_tail
    jmp  err_type_func

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
    call ebx
    add  esp, 16
    pop  ebx                       ; nargs
    pop  ecx                       ; A
    pop  esi
    test eax, eax
    jns  .c_tail_returns
    mov  [edi + VM_STATUS], eax
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
    cmp  edx, P386_VARARG_STACK_SLOTS
    jbe  .tc_va_count_ok
    mov  eax, P386_VARARG_STACK_SLOTS
    sub  eax, ecx
    jle  .tc_va_done
.tc_va_count_ok:
    mov  [scratch_a + 16], eax              ; nextra
    movzx edx, byte [ebx + PE_N_PARAMS]
    mov  esi, [scratch_a + 12]              ; &caller R[A+1]
    lea  esi, [esi + edx*8]                  ; skip named params
    lea  edx, [edi + VM_VARARG_STACK]
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
    mov  ecx, [scratch_a + 8]
.lua_clear_loop:
    cmp  ecx, [dest_tmp]
    jae  .lua_enter
    mov  dword [ebp + ecx*8], 0
    mov  dword [ebp + ecx*8 + 4], TAG_NIL
    inc  ecx
    jmp  .lua_clear_loop

.lua_enter:
    mov  esi, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  esi, [ebx + PE_BYTECODE_OFF]
    jmp  dispatch_next

op_call:
    movzx ecx, ah                  ; A: function register
    mov  edx, eax
    shr  edx, 16
    cmp  dword [ebp + ecx*8 + 4], TAG_FUNC
    je   .lua_func
    cmp  dword [ebp + ecx*8 + 4], TAG_CFUNC
    jne  err_type_func
    mov  ebx, [ebp + ecx*8]        ; raw C function pointer
    test ebx, ebx
    jz   err_type_func
    movzx eax, dl                  ; B = nargs + 1 (0 => from top)
    test eax, eax
    jnz  .fixed_args
    mov  eax, [edi + VM_TOP]
    lea  edx, [ebp + ecx*8 + 8]    ; first arg R[A+1]
    sub  eax, edx
    sar  eax, 3
    jns  .args_ready
    xor  eax, eax
    jmp  .args_ready
.fixed_args:
    dec  eax                       ; nargs
.args_ready:
    movzx edx, dh                  ; C = want_rets + 1 (0 => all)
    test edx, edx
    jz   .want_all
    dec  edx
.want_all:
    push esi
    push ecx
    push edx
    push eax
    push edx                       ; want_rets
    push eax                       ; nargs
    lea  eax, [ebp + ecx*8 + 8]    ; args/result window starts after func
    push eax
    push edi
    call ebx
    add  esp, 16
    pop  ebx                       ; nargs
    pop  edx                       ; want_rets (0 means all)
    pop  ecx                       ; A
    pop  esi
    test eax, eax
    js   .builtin_error
    mov  ebx, eax                  ; actual return count
    mov  eax, edx                  ; wanted fixed count, 0 => all actual
    test eax, eax
    jnz  .copy_fixed
    mov  eax, ebx
.copy_fixed:
    mov  [scratch_a], esi          ; preserve IP: the copy below uses esi as a
                                   ; scratch and esi is the live instruction ptr
    push eax                       ; ncopy/result slots requested
    xor  edx, edx
.copy_loop:
    cmp  edx, eax
    jae  .copy_done
    cmp  edx, ebx
    jae  .pad_nil
    push eax
    push edi
    mov  eax, ecx
    add  eax, edx
    lea  edi, [ebp + eax*8]        ; destination R[A+i]
    inc  eax
    lea  eax, [ebp + eax*8]        ; source R[A+i+1]
    mov  esi, [eax]
    mov  [edi], esi
    mov  esi, [eax + 4]
    mov  [edi + 4], esi
    pop  edi
    pop  eax
    jmp  .next_slot
.pad_nil:
    push eax
    mov  eax, ecx
    add  eax, edx
    mov  dword [ebp + eax*8], 0
    mov  dword [ebp + eax*8 + 4], TAG_NIL
    pop  eax
.next_slot:
    inc  edx
    jmp  .copy_loop
.copy_done:
    pop  eax
    add  eax, ecx
    lea  edx, [ebp + eax*8]
    mov  [edi + VM_TOP], edx
    mov  esi, [scratch_a]          ; restore IP clobbered by the copy loop
    jmp  dispatch_next
.builtin_error:
    mov  [edi + VM_STATUS], eax
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
    mov  [scratch_a], ecx          ; caller A
    mov  [scratch_a + 20], ecx     ; return_reg = A (op_tforcall uses A+3)

; Shared Lua frame push. Entry contract (memory scratch, register-free):
;   [scratch_a]      = function register (closure in R[here], args at R[here+1..])
;   [scratch_a + 4]  = nargs
;   [scratch_a + 8]  = want_rets (0 => all)
;   [scratch_a + 20] = return_reg in the caller frame
; esi = return IP (instruction after the call site). Jumped to by
; op_tforcall's .lua_iter path with return_reg = A+3.
call_push_lua_frame:
    cmp  dword [edi + VM_CALL_DEPTH], CALL_STACK_DEPTH
    jae  err_bounds
    mov  eax, [edi + VM_CALL_DEPTH]
    imul eax, FRAME_SIZE
    lea  eax, [edi + VM_CALL_STACK + eax]
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
    ; Save caller's vararg window so it can be restored on return.
    mov  edx, [edi + VM_VARARG_BASE]
    mov  [eax + FRAME_SAVED_VARARG_BASE], edx
    mov  edx, [edi + VM_VARARG_COUNT]
    mov  [eax + FRAME_SAVED_VARARG_COUNT], edx
    mov  edx, [edi + VM_VARARG_SP]
    mov  [eax + FRAME_SAVED_VARARG_SP], edx
    inc  dword [edi + VM_CALL_DEPTH]

    mov  ecx, [scratch_a]          ; caller A
    mov  eax, [ebp + ecx*8]        ; closure
    mov  [edi + VM_CURRENT_CLOSURE], eax
    mov  ebx, [eax + 4]            ; closure->proto
    lea  eax, [ebp + ecx*8 + 8]    ; first arg in caller
    mov  [scratch_a + 12], eax

    mov  eax, [edi + VM_CURRENT_PROTO]
    movzx eax, byte [eax + PE_N_REGS]
    lea  ebp, [ebp + eax*8]        ; callee base = caller base + caller n_regs
    mov  [edi + VM_BASE], ebp
    mov  [edi + VM_CURRENT_PROTO], ebx
    movzx edx, byte [ebx + PE_N_REGS]
    mov  [dest_tmp], edx
    lea  eax, [ebp + edx*8]
    cmp  eax, [edi + VM_VALUE_STACK_END]
    ja   err_bounds
    mov  [edi + VM_TOP], eax

    ; clear all callee registers to nil
    xor  ecx, ecx
.lua_clear_loop:
    cmp  ecx, [dest_tmp]
    jae  .lua_copy_args
    mov  dword [ebp + ecx*8], 0
    mov  dword [ebp + ecx*8 + 4], TAG_NIL
    inc  ecx
    jmp  .lua_clear_loop

.lua_copy_args:
    mov  edx, [scratch_a + 4]      ; copy min(nargs, n_params)
    movzx eax, byte [ebx + PE_N_PARAMS]
    cmp  edx, eax
    jbe  .lua_copy_count_ready
    mov  edx, eax
.lua_copy_count_ready:
    xor  ecx, ecx
    mov  eax, [scratch_a + 12]
.lua_arg_loop:
    cmp  ecx, edx
    jae  .lua_setup_varargs
    mov  esi, [eax + ecx*8]
    mov  [ebp + ecx*8], esi
    mov  esi, [eax + ecx*8 + 4]
    mov  [ebp + ecx*8 + 4], esi
    inc  ecx
    jmp  .lua_arg_loop

.lua_setup_varargs:
    ; Default: empty vararg window anchored at current sp.
    mov  eax, [edi + VM_VARARG_SP]
    mov  [edi + VM_VARARG_BASE], eax
    mov  dword [edi + VM_VARARG_COUNT], 0
    ; Only vararg protos collect extra args.
    test byte [ebx + PE_FLAGS], P386_PROTO_FLAG_VARARG
    jz   .lua_enter
    ; nextra = nargs - n_params (clamped at 0)
    mov  eax, [scratch_a + 4]      ; nargs
    movzx edx, byte [ebx + PE_N_PARAMS]
    sub  eax, edx
    jle  .lua_enter                ; <=0: no extra args
    ; clamp nextra so vararg_sp + nextra <= P386_VARARG_STACK_SLOTS
    mov  ecx, [edi + VM_VARARG_SP]
    mov  edx, ecx
    add  edx, eax
    cmp  edx, P386_VARARG_STACK_SLOTS
    jbe  .lua_va_count_ok
    mov  eax, P386_VARARG_STACK_SLOTS
    sub  eax, ecx                  ; available slots
    jle  .lua_enter
.lua_va_count_ok:
    mov  [scratch_a + 16], eax     ; nextra
    ; source = caller first-arg window + n_params
    movzx edx, byte [ebx + PE_N_PARAMS]
    mov  esi, [scratch_a + 12]     ; &caller R[A+1]
    lea  esi, [esi + edx*8]        ; skip named params
    ; dest = &vararg_stack[vararg_sp]
    lea  edx, [edi + VM_VARARG_STACK]
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

.lua_enter:
    mov  esi, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  esi, [ebx + PE_BYTECODE_OFF]
    jmp  dispatch_next

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
    lea  eax, [eax + edx*8]
    mov  [edi + VM_TOP], eax
    jmp  done_halted

.return_to_caller:
    mov  [scratch_a], ecx          ; return A
    mov  [scratch_a + 4], edx      ; actual returns
    mov  [scratch_a + 8], ebp      ; callee base
    dec  dword [edi + VM_CALL_DEPTH]
    mov  eax, [edi + VM_CALL_DEPTH]
    imul eax, FRAME_SIZE
    lea  eax, [edi + VM_CALL_STACK + eax]
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
    movzx edx, byte [eax + FRAME_WANT_RETS]
    ; scratch_a+4 already holds the actual return count; keep it in memory so
    ; the copy body is free to clobber ebx for the value being moved.
    mov  eax, edx                  ; wanted fixed count, 0 => all actual
    test eax, eax
    jnz  .lua_ret_copy_fixed
    mov  eax, [scratch_a + 4]      ; want all -> ncopy = actual count
.lua_ret_copy_fixed:
    push eax                       ; ncopy/result slots requested
    xor  edx, edx
.lua_ret_copy_loop:
    cmp  edx, eax
    jae  .lua_ret_copy_done
    cmp  edx, [scratch_a + 4]      ; i >= actual? -> pad with nil
    jae  .lua_ret_pad_nil
    push eax
    mov  eax, [scratch_a]
    add  eax, edx
    lea  eax, [eax*8]
    add  eax, [scratch_a + 8]      ; source callee R[A+i]
    mov  [dest_tmp], eax
    mov  ebx, [eax]
    mov  eax, ecx
    add  eax, edx
    lea  eax, [ebp + eax*8]        ; destination caller R[return_reg+i]
    mov  [eax], ebx
    mov  ebx, [dest_tmp]
    mov  ebx, [ebx + 4]
    mov  [eax + 4], ebx
    pop  eax
    jmp  .lua_ret_next_slot
.lua_ret_pad_nil:
    push eax
    mov  eax, ecx
    add  eax, edx
    mov  dword [ebp + eax*8], 0
    mov  dword [ebp + eax*8 + 4], TAG_NIL
    pop  eax
.lua_ret_next_slot:
    inc  edx
    jmp  .lua_ret_copy_loop
.lua_ret_copy_done:
    pop  eax
    add  eax, ecx
    lea  edx, [ebp + eax*8]
    mov  [edi + VM_TOP], edx
    jmp  dispatch_next

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
    lea  ebx, [edi + VM_VARARG_STACK]
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
    jmp  dispatch_next

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
    mov  eax, [edi + VM_STATUS]
    pop  edi
    pop  esi
    pop  ebx
    pop  ebp
    ret
