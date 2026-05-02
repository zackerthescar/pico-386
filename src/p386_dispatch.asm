[BITS 32]

%define VM_STATUS       0
%define VM_ERROR_MSG    4
%define VM_LAST_OPCODE  8
%define VM_PROGRAM      12
%define LP_BYTECODE_SECTION 16
%define VM_VALUE_STACK  32
%define VM_BASE         32800
%define VM_TOP          32804
%define VM_GLOBALS      32812
%define VM_CURRENT_PROTO 34860
%define VM_IP           34864

%define PE_BYTECODE_OFF 0
%define PE_BYTECODE_LEN 4
%define PE_CONSTS_OFF   8
%define PE_N_CONSTS     16

%define TAG_NIL  0
%define TAG_BOOL 1
%define TAG_NUM  2

%define VM_HALTED 1
%define ERR_OPCODE -2
%define ERR_TYPE   -3
%define ERR_DIV0   -4
%define ERR_BOUNDS -5
%define ERR_UNIMPL -6

section .rodata
msg_bad_opcode db 'bad opcode',0
msg_type_num   db 'expected number',0
msg_div0       db 'division by zero',0
msg_bounds     db 'register/constant out of bounds',0
msg_unimpl     db 'opcode not implemented yet',0

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
    dd op_unimpl
%elif i = 0x48
    dd op_unimpl
%elif i = 0x53
    dd op_return
%else
    dd op_unimpl
%endif
%assign i i+1
%endrep

section .bss
align 4
dest_tmp resd 1

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
    jae  err_bounds
    mov  eax, [edi + VM_PROGRAM + LP_BYTECODE_SECTION]
    add  eax, [ecx + PE_CONSTS_OFF]
    mov  ecx, [eax + ebx*8 + 4]
    mov  eax, [eax + ebx*8]
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

; --- numeric factory ----------------------------------------------------
%macro NUM_BIN 2
%1:
    movzx ecx, ah                    ; A saved
    mov  [dest_tmp], ecx
    shr  eax, 16
    mov  dl, al                    ; B RK
    mov  dh, ah                    ; C RK
    call load_rk
    cmp  ecx, TAG_NUM
    jne  err_type_num
    push eax                       ; left value
    mov  dl, dh
    call load_rk
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
    cmp  ecx, TAG_NUM
    jne  err_type_num
    push eax
    mov  dl, dh
    call load_rk
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
    push eax
    push ecx
    mov  dl, dh
    call load_rk
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
    push eax
    push ecx
    mov  dl, dh
    call load_rk
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

op_return:
    movzx ecx, ah                  ; A
    shr  eax, 16
    movzx edx, al                  ; B = nrets + 1, 0 = all (ignored for now)
    test edx, edx
    jz   .all
    dec  edx
    lea  eax, [ebp + ecx*8]
    lea  eax, [eax + edx*8]
    mov  [edi + VM_TOP], eax
    jmp  done_halted
.all:
    lea  eax, [ebp + ecx*8]
    mov  [edi + VM_TOP], eax
    jmp  done_halted

op_unimpl:
    mov  dword [edi + VM_STATUS], ERR_UNIMPL
    mov  dword [edi + VM_ERROR_MSG], msg_unimpl
    jmp  done

err_bounds:
    mov  dword [edi + VM_STATUS], ERR_BOUNDS
    mov  dword [edi + VM_ERROR_MSG], msg_bounds
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
