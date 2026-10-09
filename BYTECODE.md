# pico386 bytecode spec

target: i386DX-25 minimum, DOS/4GW flat 32-bit, source = PICO-8 lua dialect (8192-token cap)
runtime: register-based VM, threaded dispatch, written in C with asm hot path
compiler: rust, no_std, runs on the target via watcom toolchain
status: v1 design

this spec is **the contract** between the rust compiler and the C runtime. anything not specified here is implementation defined and can change. anything specified here changing requires a coordinated update on both sides.

open TODOs at end. things deferred from v1: source-level varargs, `goto`/label. GC, metatables and coroutines are now implemented (§8a, §7, §8b).

---

## 1. value representation

every register / stack slot is **8 bytes**:

```
offset  bytes
+0..+3  value : i32 (LE)
+4..+7  tag   : u32 (LE)
```

value first because that's what arithmetic handlers touch most often. tag at +4.

### tag enum

| tag | name  | value field meaning                           |
| --- | ----- | --------------------------------------------- |
| 0   | NIL   | always 0                                      |
| 1   | BOOL  | 0 = false, 1 = true                           |
| 2   | NUM   | i32 16.16 fixed-point                         |
| 3   | STR   | u32 String\* (heap pointer)                   |
| 4   | TAB   | u32 Table\* (heap pointer)                    |
| 5   | FUNC  | u32 Closure\* (heap pointer)                  |
| 6   | CFUNC | u32 raw C function pointer                    |
| 7   | THREAD | u32 P386Thread\* (coroutine, heap pointer; never a constant) |

NUM is the only non-pointer payload. STR/TAB/FUNC/THREAD are heap pointers; CFUNC is a code pointer (text section).

### type test pattern (asm)

```nasm
; check that R[B] is NUM
mov   eax, [ebp + ebx*8 + 4]   ; load tag
cmp   eax, TAG_NUM
jne   err_type_num
```

one load, one compare, one conditional branch per type check.

---

## 2. bytecode encoding

instructions are **fixed 32-bit, 4-byte aligned, little-endian**.

```
bit:  31      24 23      16 15       8 7        0
     [    C    ][    B    ][    A    ][   op    ]
```

- `op` (8 bits): opcode, 0..255
- `A`  (8 bits): destination register or primary operand. always a full register index (0..255).
- `B`  (8 bits): RK operand (or count, or sBx half — see below).
- `C`  (8 bits): RK operand (or count, or sBx half).

### RK operand decode

an "RK" byte is either a register reference or a constant pool reference:

- if `byte & 0x80`: it's constants[byte & 0x7f]. (constant index 0..127.)
- else: it's R[byte & 0x7f]. (register index 0..127.)

so an RK operand reaches only **registers 0..127** and **constants 0..127**. a function can hold up to 65535 constants. the compiler uses an RK constant only when its index is below 128. for a higher index it does LOADK (16-bit Bx) into a temp register and uses the register. for opcodes that don't use RK on a given operand, the full 8 bits are available.

### multi-byte operands

certain opcodes treat B|C as a single 16-bit value:

| field name | encoding              | range          | used by              |
| ---------- | --------------------- | -------------- | -------------------- |
| `Bx`       | `B \| (C << 8)`       | 0..65535       | LOADK, CLOSURE, GETGLOBAL, SETGLOBAL |
| `sBx`      | sign-extended `Bx`    | -32768..32767  | JMP, JMPF, JMPT, FORPREP, FORLOOP, TFORLOOP |

sBx is the relative jump offset, measured from the **end** of the instruction (so `sBx = 0` is a no-op jump).

---

## 3. opcode list

notation: `R[X]` = current frame's register X. `RK(X)` = `R[X & 0x7f]` if `X & 0x80 == 0` else `K[X & 0x7f]` (constant pool). `K[X]` = constants[X] in the current function's constant pool. all 16.16 fixed-point math is done on i32 with `(int64_t)a * b >> 16` for MUL and `((int64_t)a << 16) / b` for DIV.

### data movement

| op       | hex  | operands     | semantics                                  |
| -------- | ---- | ------------ | ------------------------------------------ |
| MOVE     | 0x01 | A, B         | `R[A] = R[B]`                              |
| LOADK    | 0x02 | A, Bx        | `R[A] = K[Bx]`                             |
| LOADT    | 0x03 | A            | `R[A] = true`                              |
| LOADF    | 0x04 | A            | `R[A] = false`                             |
| LOADN    | 0x05 | A, B         | `R[A..A+B-1] = nil` (B = count, B≥1)       |

### globals

| op        | hex  | operands | semantics              |
| --------- | ---- | -------- | ---------------------- |
| GETGLOBAL | 0x10 | A, Bx    | `R[A] = globals[Bx]`   |
| SETGLOBAL | 0x11 | A, Bx    | `globals[Bx] = R[A]`   |

B is 8-bit slot index (0..255). compile-time-assigned. builtin slots reserved at low indices (see `include/builtins.h`).

### upvalues

| op       | hex  | operands | semantics                                          |
| -------- | ---- | -------- | -------------------------------------------------- |
| GETUPVAL | 0x12 | A, B     | `R[A] = closure->upvalues[B]->slot[0]`             |
| SETUPVAL | 0x13 | A, B     | `closure->upvalues[B]->slot[0] = R[A]`             |
| CLOSE    | 0x14 | A        | close all open upvalues at `&R[A]` or higher       |

### tables

| op       | hex  | operands  | semantics                                              |
| -------- | ---- | --------- | ------------------------------------------------------ |
| NEWTABLE | 0x18 | A, B, C   | `R[A] = new Table` with B array hint, C hash log2 hint |
| GETTABLE | 0x19 | A, B, C   | `R[A] = R[B][RK(C)]`                                   |
| SETTABLE | 0x1A | A, B, C   | `R[A][RK(B)] = RK(C)`                                  |
| GETFIELD | 0x1B | A, B, C   | `R[A] = R[B][K[C]]` (C is constant idx, must be STR)   |
| SETFIELD | 0x1C | A, B, C   | `R[A][K[B]] = RK(C)` (B is constant idx, must be STR)  |

GETFIELD/SETFIELD are the fast-path for `t.field` syntax. C in GETFIELD and B in SETFIELD are full 8-bit constant indices (no K-flag, always constant — the field name). C in SETFIELD is an RK operand: a constructor field such as `{x=0}` stores a constant directly. when the name has a constant index above 255, the compiler emits LOADK of the name into a temp register and uses GETTABLE/SETTABLE instead.

**GETFIELD inline cache.** GETFIELD is 3 words: the instruction, then two cache words.

| word | contents |
| ---- | -------- |
| 0    | `GETFIELD A B C` |
| 1    | address of the interned name K[C]; the loader writes it (0 if K[C] is not a string). the compiler writes 0 |
| 2    | byte offset of the hash slot where the name was found last time; starts at 0, the dispatcher updates it |

the dispatcher masks word 2 to the table's hash part and compares the key there. on a hit it does not hash or probe; on a miss it probes from the name's hash and stores the slot it found. any value in word 2 is safe. objects made by the same code (for example `init_object` in Celeste) have the same keys in the same slots, so one site usually hits for all of them. the VM writes the cache into the program buffer, so the buffer given to `p386_vm_load` must be writable. code that walks the bytecode (loader check, disassembler) skips the two words after a GETFIELD; jump offsets count them.

### arithmetic (16.16 fixed-point on NUM)

| op   | hex  | operands  |
| ---- | ---- | --------- |
| ADD  | 0x20 | A, B, C   |
| SUB  | 0x21 | A, B, C   |
| MUL  | 0x22 | A, B, C   |
| DIV  | 0x23 | A, B, C   |
| IDIV | 0x24 | A, B, C   |
| MOD  | 0x25 | A, B, C   |
| POW  | 0x26 | A, B, C   |
| NEG  | 0x27 | A, B      |

semantics: `R[A] = RK(B) op RK(C)`. all type-check both operands as NUM. mismatch → trap (`err_type_num`).

### bitwise (raw i32 on NUM)

| op   | hex  | operands  |
| ---- | ---- | --------- |
| BAND | 0x28 | A, B, C   |
| BOR  | 0x29 | A, B, C   |
| BXOR | 0x2A | A, B, C   |
| BNOT | 0x2B | A, B      |
| SHL  | 0x2C | A, B, C   |
| SHR  | 0x2D | A, B, C   |
| LSHR | 0x2E | A, B, C   |
| ROTL | 0x2F | A, B, C   |
| ROTR | 0x30 | A, B, C   |

operate on the raw i32 value (no fixed-point shift), matching PICO-8 semantics.

### comparison (push-bool)

| op | hex  | operands  | semantics                  |
| -- | ---- | --------- | -------------------------- |
| EQ | 0x31 | A, B, C   | `R[A] = (RK(B) == RK(C))`  |
| NE | 0x32 | A, B, C   | `R[A] = (RK(B) != RK(C))`  |
| LT | 0x33 | A, B, C   | `R[A] = (RK(B) < RK(C))`   |
| LE | 0x34 | A, B, C   | `R[A] = (RK(B) <= RK(C))`  |
| GT | 0x35 | A, B, C   | `R[A] = (RK(B) > RK(C))`   |
| GE | 0x36 | A, B, C   | `R[A] = (RK(B) >= RK(C))`  |

result is BOOL. EQ/NE work on any value pair: pointer-equal for STR/TAB/FUNC/CFUNC; structural compare for NUM/BOOL/NIL. ordered comparisons (LT/LE/GT/GE) need two NUM operands or two STR operands. STR compares byte-wise (unsigned), and a prefix sorts first. Any other mix traps (type error), unless a metamethod (`__eq`, `__lt`, `__le`) applies. EQ/NE try `__eq` only for two different tables when one of them has a metatable.

### fused compare-and-branch

| op  | hex  | operands  | semantics                                             |
| --- | ---- | --------- | ----------------------------------------------------- |
| BEQ | 0x60 | A, B, C   | as EQ, then decide the JMPF/JMPT on R[A] that follows |
| BNE | 0x61 | A, B, C   | as NE, idem                                           |
| BLT | 0x62 | A, B, C   | as LT, idem                                           |
| BLE | 0x63 | A, B, C   | as LE, idem                                           |
| BGT | 0x64 | A, B, C   | as GT, idem                                           |
| BGE | 0x65 | A, B, C   | as GE, idem                                           |

a Bxx is always followed by `JMPF A sBx` or `JMPT A sBx` with the same A (a temporary). when the comparison has a direct result (NUM, STR, BOOL, NIL, or tables without `__eq`), the VM does not write R[A]: it reads the next word, jumps (JMPF: result false, JMPT: result true; sBx is from the word after the jump) or skips the jump word. a backward jump is a GC safe point. when a metamethod gives the result, Bxx acts as the plain opcode: the metamethod frame stores the boolean in R[A] and returns to the jump word, which then runs as usual. so jump patching in the compiler sees only the JMPF/JMPT word.

the compiler compiles the condition of `if`, `while` and `repeat` as jumps: `and`, `or` and `not` only route jumps (no boolean is made), and each comparison in the condition becomes Bxx + JMPF/JMPT. a comparison used as a value (`local b = x < y`) still uses EQ..GE.

### unary

| op    | hex  | operands | semantics                                              |
| ----- | ---- | -------- | ------------------------------------------------------ |
| NOT   | 0x37 | A, B     | `R[A] = (RK(B) is nil or false) ? true : false`        |
| LEN   | 0x38 | A, B     | `R[A] = #RK(B)` (string len for STR, array_len for TAB) |
| PEEK  | 0x39 | A, B     | `R[A] = mem8[RK(B)]`  (PICO-8 `@`)                     |
| PEEK2 | 0x3A | A, B     | `R[A] = mem16[RK(B)]` (PICO-8 `$`)                     |

### string

| op     | hex  | operands  | semantics                       |
| ------ | ---- | --------- | ------------------------------- |
| CONCAT | 0x3B | A, B, C   | `R[A] = RK(B) .. RK(C)`         |

interns the result. coerces NUM to STR via `tostring` (decimal, with optional fraction).

### control flow

| op   | hex  | operands  | semantics                       |
| ---- | ---- | --------- | ------------------------------- |
| JMP  | 0x40 | sBx       | `PC += sBx`                     |
| JMPF | 0x41 | A, sBx    | `if !R[A] then PC += sBx`       |
| JMPT | 0x42 | A, sBx    | `if R[A] then PC += sBx`        |

"falsy" means tag=NIL or (tag=BOOL and value=0). everything else truthy.

### loops

| op       | hex  | operands  | semantics                                                            |
| -------- | ---- | --------- | -------------------------------------------------------------------- |
| FORPREP  | 0x45 | A, sBx    | numeric for: `R[A] -= R[A+2]; PC += sBx`                             |
| FORLOOP  | 0x46 | A, sBx    | `R[A] += R[A+2]; if step-and-limit-ok then PC += sBx; R[A+3] = R[A]` |
| TFORCALL | 0x47 | A, B      | `R[A+3..A+2+B] = R[A](R[A+1], R[A+2])` (B = nvars, ≥ 1)             |
| TFORLOOP | 0x48 | A, sBx    | `if R[A+3] != nil then R[A+2] = R[A+3]; PC += sBx`                   |

#### numeric for register layout

4 consecutive registers starting at A:

| slot   | role                                               |
| ------ | -------------------------------------------------- |
| R[A]   | internal idx (initially `start - step`)            |
| R[A+1] | limit                                              |
| R[A+2] | step                                               |
| R[A+3] | visible loop variable (mirror of R[A] post-FORLOOP)|

FORLOOP's "limit-ok" check: if `step > 0`, check `idx <= limit`. if `step < 0`, check `idx >= limit`. step == 0 traps (`err_for_step`).

#### generic for register layout

3 + nvars consecutive registers starting at A:

| slot              | role                          |
| ----------------- | ----------------------------- |
| R[A]              | iterator function             |
| R[A+1]            | state                         |
| R[A+2]            | control (initially nil)       |
| R[A+3..A+2+nvars] | visible loop variables        |

after TFORCALL, the first returned value is in R[A+3]. TFORLOOP checks if it's nil; if not, copies R[A+3] back to R[A+2] (the new control) and jumps.

#### TFORCALL iterator dispatch

TFORCALL branches on the tag of `R[A]` (the iterator):

- **CFUNC or NIL iterator + TAB state** — the `pairs` fast path: the handler
  calls `p386_table_next` directly on the state table, writing key/value to
  `R[A+3]`/`R[A+4]` (no CallFrame, no C→Lua reentry).
- **FUNC iterator** — a Lua closure: the handler pushes a normal CallFrame
  exactly like CALL's Lua path, with function reg = A, nargs = 2 (state and
  control already sit contiguously at `R[A+1..A+2]`), `want_rets = B` (nvars)
  and `return_reg = A+3`. The saved return IP is the following TFORLOOP, so
  the iterator's RETURN resumes there with its results nil-padded to nvars
  at `R[A+3..]` per the normal want_rets contract. State and control may be
  ANY tag on this path (closure iterators typically ignore them).
- anything else traps `err_type_iter`.

this is what makes closure-based iterators (`all`, `ipairs` from the Lua
prelude — see §5 globals) work with plain generic-for.

### functions, calls, returns

| op       | hex  | operands  | semantics                                      |
| -------- | ---- | --------- | ---------------------------------------------- |
| CLOSURE  | 0x50 | A, Bx     | `R[A] = new Closure(prototypes[Bx])`           |
| CALL     | 0x51 | A, B, C   | call `R[A]` with B-1 args, want C-1 returns    |
| TAILCALL | 0x52 | A, B      | tail-call `R[A]` with B-1 args; reuses frame   |
| RETURN   | 0x53 | A, B      | return `R[A..A+B-2]` (B-1 values); B=0 = all   |
| VARARG   | 0x54 | A, B      | copy frame varargs into `R[A..]`               |

#### varargs

a function prototype whose `flags` has `P386_PROTO_FLAG_VARARG` (0x02) collects
the arguments beyond its `n_params` named parameters into a per-frame **vararg
window**. the VM keeps a dedicated `vararg_stack` plus `vararg_base`/
`vararg_count`/`vararg_sp` cursors; on a Lua call into a vararg proto the extra
args are copied there, and the caller's window is saved in the CallFrame and
restored on RETURN. (the named params still land in `R[0..n_params-1]` as
usual.)

`VARARG A B` copies from the current frame's window into registers starting at
`R[A]`:
- `B == 0`: copy all `vararg_count` values and set `top = &R[A+count]` (so a
  following CALL/RETURN with its own B=0/C=0 spreads them).
- `B  > 0`: copy `B-1` values, nil-padding when fewer varargs are available.

the compiler emits `VARARG A 2` for a single-value `...`, `VARARG A 0` when `...`
is the final element of a call's argument list or of a `return`, and
`VARARG A n+1` to fill exactly `n` slots in a fixed multiple-assignment.

#### CALL semantics

before:
- `R[A]` = function (TAG_FUNC or TAG_CFUNC)
- `R[A+1..A+B-1]` = arguments (B-1 of them)
- `B = nargs+1`. special: `B=0` means "args extend to current top" (used for `f(g())` patterns where g's returns become f's args via top tracking)

after:
- `R[A..A+C-2]` = returns (C-1 of them)
- `C = nrets+1`. special: `C=0` means "want all returns", and `top` is set to `A + nrets`.

#### TAILCALL

identical to CALL except no new CallFrame is pushed. instead, the args are copied down to the current frame's base, the current closure is replaced, and execution jumps to the new closure's bytecode. caller's CallFrame is reused — when the called function eventually returns, it returns to the *caller's caller*.

valid only when followed by RETURN (which the codegen guarantees).

#### RETURN

before:
- `R[A..A+B-2]` = values to return (B-1 of them)
- `B = nrets+1`. `B=0` means "return everything from R[A] up to top"

after:
- callee's frame popped from CallFrame stack
- caller's R[caller_return_reg .. caller_return_reg + min(want_rets, n_returned)-1] = returned values
- if `want_rets > n_returned`, pad with nil
- if `want_rets == 0` (caller wanted all), set caller's `top` to reflect the actual count

---

## 4. frame model

### value stack

one giant contiguous array, allocated at VM init.

```c
struct VMState {
    Value*  value_stack;      // base of the array
    Value*  value_stack_end;  // for overflow check
    Value*  top;              // next free slot (used for variable-arity ops)
    Value*  base;             // current frame base; mirrored in ebp during dispatch
    ...
};
```

each function frame is a window `[base .. base + n_regs)` into `value_stack`. dispatch keeps `ebp = base` for the current frame. register N is `[ebp + N*8]`.

`top` tracks "how many values are currently live above base". used by CALL/RETURN/TFORCALL when nargs/nrets is variable.

initial size: **4096 slots = 32 KB**. fits any cart that respects the token limit.

### call stack

separate fixed-size array of frame metadata:

```c
typedef struct {
    const uint32_t* return_ip;     // caller's resume IP
    Value*          return_base;   // caller's frame base
    Closure*        closure;       // current closure (for upvalue access)
    uint8_t         return_reg;    // where to write returns in caller
    uint8_t         want_rets;     // how many rets caller wants (0 = all)
    uint8_t         post;          // metamethod result fix-up (0 = none)
    uint8_t         _padding;
    uint32_t        saved_vararg_base;   // caller's vararg window, restored
    uint32_t        saved_vararg_count;  //   when this frame returns
    uint32_t        saved_vararg_sp;
} CallFrame;

#define CALL_STACK_DEPTH 256
```

CALL pushes, RETURN pops, TAILCALL doesn't touch.

### concrete call sequence

caller wants `f(a, b)` with 1 expected return:

1. caller picks free reg, say R5. emits sequence to populate `R5=f`, `R6=a`, `R7=b`.
2. emits `CALL 5, 3, 2` (B=nargs+1=3, C=nrets+1=2).
3. CALL handler:
   - load `f` from R[5]; type-check TAG_FUNC or TAG_CFUNC.
   - if TAG_CFUNC: call `cfunc(vm, &R[6], nargs=2, want_rets=1)`. it reads args from `R[6..]` and writes its results there, then returns the count. the handler moves `min(count, want)` results down to `R[5..]` and pads with nil. a statement call (C = 1) keeps no results. no CallFrame push.
   - if TAG_FUNC: push CallFrame `{ return_ip = ip+4, return_base = base, closure = current, return_reg = 5, want_rets = 1 }`. **the callee frame starts at the first argument**: `base = &R[6]`, so the arguments are already the callee's `R[0..]` (as in Lua). varargs (extra arguments of a vararg function) are saved to the varargs stack first; then `R[min(nargs, n_params) .. n_regs)` is cleared to nil with `rep stosd`. set `closure`, `ip = closure->proto->bytecode`. dispatch.
   - TFORCALL iterators and metamethod frames do not have their arguments in place: their frame goes above the caller's registers (`base = caller_base + caller_n_regs`) and the arguments are copied.

4. callee runs; eventually `RETURN R, n`:
   - copy `base[R..R+n-1]` to `caller_base[caller_return_reg ..]` (forward: the destination is below the source).
   - pad with nil if `n < want_rets`.
   - restore `base = caller_base`, `ip = caller_return_ip`, pop CallFrame.
   - dispatch.

**GC note.** an in-place callee frame can end below its caller's frame. the collector scans the stack to the highest `base + n_regs` of all frames (`p386_frames_end`), not to the end of the current frame.

---

## 5. globals

256-slot flat array on the VMState:

```c
Value globals[P386_GLOBAL_SLOTS];   // 1024; Bx >= 1024 traps (bounds)
```

at compile time, rust maintains a `Map<Name, u8>`. first reference to a global assigns the next free slot. compile errors out at slot 256.

builtins are pre-assigned at low slots; rust and C agree via a single source-of-truth header (`include/builtins.h`):

```c
// include/builtins.h
#define BUILTIN_PSET    0
#define BUILTIN_PGET    1
#define BUILTIN_PRINT   2
#define BUILTIN_CLS     3
// ... (~80 entries)
#define BUILTIN_COUNT   80
#define USER_GLOBAL_BASE BUILTIN_COUNT
```

rust crate reads this via build.rs or hand-mirrors it; either way, the slot numbers are wire-protocol.

not every builtin slot holds a CFUNC. higher-order builtins (`all`, `foreach`,
`ipairs`) are implemented in **Lua**, in a prelude the rust compiler prepends
to every cart source (`rust/core_crate/src/prelude.lua`). their slots are
reserved like any other builtin, but `p386_register_builtins` leaves them nil
(NULL func in `p386_builtin_defs`); the compiled prelude's `function all(t)`
etc. SETGLOBALs closures into them when the main chunk runs. this is required
because a CFUNC cannot re-enter the bytecode interpreter to invoke a user
callback — there is deliberately no reentrant vm_run (future coroutine work
depends on its absence).

GETGLOBAL/SETGLOBAL are O(1) loads:

```nasm
; GETGLOBAL A, Bx
movzx ecx, ah                          ; A
shr   eax, 16
movzx edx, ax                          ; Bx (slot)
cmp   edx, P386_GLOBAL_SLOTS           ; 1024
jae   err_bounds
mov   ebx, [edi + VM_GLOBALS + edx*8]      ; load value
mov   esi, [edi + VM_GLOBALS + edx*8 + 4]  ; load tag
mov   [ebp + ecx*8],     ebx
mov   [ebp + ecx*8 + 4], esi
```

---

## 6. strings

### layout

```c
typedef struct {
    uint32_t len;
    uint32_t hash;     // precomputed at intern time
    uint8_t  bytes[];  // len bytes, no null terminator
} String;
```

8-byte header + payload.

### interning

every string created (literal at cart load, CONCAT result, runtime-created via builtin) is interned. duplicates collapse to the same `String*`.

```c
typedef struct StrNode {
    String* str;
    struct StrNode* next;
} StrNode;

typedef struct {
    StrNode** buckets;
    uint32_t  n_buckets;       // power of 2
    uint32_t  n_strings;
} StringTable;
```

equality after interning: pointer compare (one cycle). table-key hashing: just `str->hash` (zero work).

hash function: FNV-1a or similar cheap byte-mixing. computed once at intern time.

### concat

CONCAT allocates a new String of total length, memcpy's parts, computes hash, looks up in intern table. if duplicate, frees the new alloc and returns the existing one. with leak allocator: never free, just leak the duplicate (rare in practice; carts mostly concat unique results).

---

## 7. tables

`src/p386_obj.c`. lua-style array part + hash part:

```c
typedef struct P386Table {
    P386Value      *arr;        // array part: t[1..asize]          offset 0
    uint32_t        asize;      // t[asize] is not nil; #t == asize        4
    uint32_t        acap;       //                                         8
    P386TableEntry *hash;       // hcap slots of {key, val}, 16 bytes     12
    P386Table      *metatable;  // NULL or set by setmetatable            16
    uint32_t        hcap;       // power of two, or 0 (no hash part)      20
    uint32_t        hused;      // non-empty slots (dead ones too)        24
} P386Table;                    // GC header is before the object (§8a)
```

- **array part.** an integer key `k` with `1 <= k <= asize` is `arr[k-1]`. `t[asize+1] = v` appends, then moves `asize+1, asize+2, ...` out of the hash part while they are there, so keys set out of order end up in the array. setting `t[asize] = nil` trims `asize` past trailing nils, so `#t == asize` is always a border. other integer keys go to the hash part.
- **hash part.** open addressing, linear probing, load factor < 3/4, resize to the live entries. string keys use the hash in the string (FNV-1a) and compare by pointer (interned); other keys use a mixed hash of tag and value.
- **deleted keys.** `t[k] = nil` keeps the key with a nil value, so `next` finds it while `pairs` runs. the collector turns such slots into `DEAD_KEY` (tag 0xFF): the key is not marked any more, lookups skip it, and `next` still matches its bits. a resize drops them.
- **next** is O(1) per step: array part, then hash slots in order.
- **asm fast paths** (`p386_dispatch.asm`): GETFIELD, GETTABLE, SETFIELD and SETTABLE read or write inline for an integer key inside the array part or a string key in the hash part (`HASH_FIND_STR`). an absent key, a nil value (may need `__index`), a table with a metatable on writes, a nil store, an append or a new key go to the general path, which calls C.

### metatables

`setmetatable(t, mt)` and `getmetatable(t)` are C builtins. `mt` is a table or nil. `P386Table.metatable` holds it (NULL = none). `rawget`/`rawset`/`rawequal`/`rawlen` ignore it. `__metatable`, `__tostring`, `__pairs`, `__mode` and `__gc` are not supported.

the fast paths do not change. a metamethod is examined only on a slow path:

| event        | slow path that examines it                                          |
| ------------ | ------------------------------------------------------------------- |
| `__index`    | GETTABLE / GETFIELD gave nil and the table has a metatable          |
| `__newindex` | SETTABLE / SETFIELD on a table that has a metatable, key not present |
| `__add` `__sub` `__mul` `__div` `__idiv` `__mod` `__pow` | an operand is not NUM |
| `__unm`      | NEG of a value that is not NUM                                      |
| `__concat`   | CONCAT of a value that is not STR or NUM                            |
| `__len`      | LEN of a table that has a metatable                                 |
| `__eq`       | EQ / NE of two different tables                                     |
| `__lt` `__le` | LT / LE / GT / GE that is not NUM/NUM or STR/STR (`a>b` is `__lt(b,a)`) |
| `__call`     | CALL / TAILCALL of a value that is not a function                   |

the handler comes from the left operand first, then the right. no handler → the usual type error (for `__eq`: not equal). bitwise ops have no metamethods.

`__index` / `__newindex` can be a table (the lookup continues in that table, max 32 levels; a cycle traps) or a function.

**handler calls.** the dispatcher cannot re-enter `p386_vm_run` from C. so `p386_meta.c` finds the handler, and for a Lua function it puts the function and its arguments in `p386_meta_call[]`. the dispatcher then pushes a normal Lua frame (`call_push_lua_frame_ptr`) that returns 1 value into the destination register. a CFUNC handler is called directly from C. results of `__newindex` go to the first register above the frame, which is free. for `__eq`/`__lt`/`__le`, the frame's `post` byte (0 none, 1 to boolean, 2 to negated boolean for NE) makes RETURN convert the result.

---

## 8. closures & upvalues

### closure value

```c
typedef struct {
    FuncProto* proto;
    uint8_t    n_upvalues;
    Upvalue*   upvalues[];   // flexible array
} Closure;
```

### upvalue cell

```c
typedef struct Upvalue {
    Value*  slot;             // open: points into value_stack; closed: points to self.value
    Value   value;            // populated when closed
    struct Upvalue* next_open;   // linked list of open upvalues per VMState (sorted by slot ptr)
} Upvalue;
```

### open vs closed

while the enclosing frame is alive, the upvalue's `slot` points into `value_stack`. GETUPVAL/SETUPVAL go through the indirection — read/write affects the original local in the parent frame.

when the parent frame returns (or a `do` block ends and the captured local goes out of scope), `CLOSE A` is emitted. CLOSE walks `vm->open_upvalues`; for each upvalue whose `slot >= &base[A]`, it copies the value into `upvalue.value` and updates `upvalue.slot = &upvalue.value`. removes from the open list. the upvalue is now self-contained on the heap.

### upvalue refs in FuncProto

each FuncProto carries a list of "where do my upvalues come from":

```c
typedef struct {
    uint8_t source;   // 0 = parent's local; 1 = parent's upvalue
    uint8_t index;    // slot or upvalue index
} UpvalueRef;
```

at CLOSURE execution:
1. allocate Closure with `n_upvalues` slots.
2. for each ref:
   - source==0 (`parent_local`): find existing open upvalue at `&parent_base[index]`, or create one and insert into `vm->open_upvalues`.
   - source==1 (`parent_upvalue`): take `parent_closure->upvalues[index]` directly (shared reference).
3. store closure in R[A].

this design keeps CLOSURE's bytecode encoding clean (single 4-byte instruction) — all the upvalue metadata lives in the FuncProto.

---

## 8a. garbage collector

`src/p386_gc.c`: precise stop-the-world mark-sweep. it is precise: it reads only tagged values and known object fields, never raw memory.

- **objects.** `p386_gc_alloc` puts a `P386GCHeader` (next, gray, size, type, mark, flags) **before** each object. object pointers and struct layouts do not change. types: string, table, closure, upvalue, thread. a table's entries and a coroutine's stacks are owned memory: they are counted in the heap size and freed with the object.
- **trigger.** an allocation only sets `p386_gc_pending` when the heap reaches max(64 KB, 2 × live size after the last collection). it never collects.
- **safe points.** collection runs only where every live value is in a root:
  - the dispatcher's `GC_POLL`: backward JMP/JMPF/JMPT (also when a Bxx takes the jump), FORLOOP and TFORLOOP when they loop, and Lua function entry (CALL, TAILCALL, metamethod frames). the cost when nothing is pending is one compare and branch.
  - the host: `p386_vm_call_global` collects before a callback, when no frame is active.
  - thus C code (builtins, `p386_meta.c`, `p386_co.c`) can keep object pointers in locals: no collection can occur until it returns to the dispatcher.
- **roots.** globals; the running thread's stack from its start to `base + n_regs` of the current frame (slots above can hold stale pointers to freed objects, so they are not scanned); its varargs `[0, vararg_sp)`; the current closure and each frame's `return_closure`; open upvalues; the running coroutine; the main thread's saved context while a coroutine runs; the cached metamethod name strings.
- **traversal.** tables: live keys, values and the metatable. closures: upvalues. closed upvalues: the value (an open upvalue's value is a stack slot, marked by the stack scan). threads: body, resumer, saved context. the gray list is iterative, so deep structures do not use C stack.
- **weak intern table.** after marking, `p386_string_intern_sweep` rebuilds the intern table with the marked strings only. a constant string that is collected is interned again by the next LOADK.
- **frames are always clean.** a frame's registers are cleared on entry, so every slot in the scanned range is nil or a value that was live at the last collection.
- **test aids.** `p386_gc_stress` collects at every safe point; `p386_gc_poison` fills freed memory with 0xDD. the unit tests run Lua programs with both.
- `stat(0)` returns the heap size in KB.

## 8b. coroutines

`src/p386_co.c`: `cocreate`, `coresume`, `yield`, `costatus` (`"suspended"`, `"running"`, `"normal"`, `"dead"`).

- **threads.** a coroutine is a `P386Thread` heap object with its own value stack (512 slots), call frames (64) and varargs stack (64), about 7 KB, allocated at the first resume. the main thread's `P386Thread` is in `P386VMState` and uses the VM's arrays. the dispatcher addresses the running thread's stacks through `vm->stack_start`, `frames`, `frames_max`, `varargs` and `varargs_max`.
- **switching without re-entry.** `coresume` and `yield` store a request (`switch_kind`, `switch_target`, up to 32 values in `xfer`, the result window of their CALL) and return `P386_VM_SWITCH`. the dispatcher exits as on an error. `p386_vm_run` (C) saves the running thread, loads the other one, writes the values into its pending CALL window (nil-padded to `want_rets`), and starts the dispatcher again at the saved ip (`p386_vm_exec(vm, 1)`). the C stack does not grow.
- **tail position.** after `return coresume(...)` / `return yield(...)` the frame is already gone. the dispatcher stores A+1 in `vm->tail_reg`; the thread then continues at a per-thread `RETURN A+1, 0` instruction (`tail_insn`).
- **end of a coroutine.** RETURN at depth 0 stores the first result in `vm->ret_base`. on that, or on an error, the coroutine becomes dead: its open upvalues are closed (closures keep the values), its stacks are freed at once, and the resumer gets `true, results...` or `false, message`. an error in a coroutine does not stop the cart. `P386_VM_ERR_QUIT` (Esc in `flip`) is not caught.
- **limits.** yield is not possible from a C builtin or a CFUNC metamethod (C is in the middle of a call); a Lua metamethod or `foreach` callback can yield. a stack overflow in a coroutine is an error result.

## 9. C functions vs lua functions

CALL handler dispatches by tag:

- TAG_FUNC: lua function. push CallFrame, set base/closure/ip, jump to bytecode.
- TAG_CFUNC: C function. direct call, no frame push.

C function signature:

```c
typedef int (*CFunc)(VMState* vm, int n_args);
```

contract:
- entry: args are at `vm->top - n_args .. vm->top`. `vm->top` points one past the last arg.
- exit: function pushes returns onto `vm->top` (incrementing it). returns the count of values pushed.
- the CALL handler then copies returns back to the caller's destination registers and adjusts `vm->top` per the want_rets contract.

simple. lets builtins read/write the value stack uniformly.

---

## 10. errors

trap and halt. on any runtime error:

```c
void vm_error(VMState* vm, const char* msg) __attribute__((noreturn));
```

implementation: print message + current opcode index + (if available) source line, then `exit(1)`. host wrapper shows a "cart crashed" screen.

every type-check in handlers branches to a labeled error site. canonical labels:

- `err_type_num`     — "expected number"
- `err_type_str`     — "expected string"
- `err_type_table`   — "expected table"
- `err_type_func`    — "expected function"
- `err_type_indexable` — "tried to index non-table"
- `err_div_zero`     — "division by zero"
- `err_for_step`     — "for loop step must be non-zero"
- `err_stack_overflow` — "value stack overflow"
- `err_call_overflow` — "call stack overflow"
- `err_no_globals`   — "global slot exhausted at compile" (compile-time, not runtime)

no pcall, no recoverable errors. carts crash hard.

---

## 11. bytecode container format

flat byte buffer. all multi-byte fields little-endian. all sections 4-byte aligned.

### header (32 bytes)

```
offset  size  field
0x00    4     magic "P386" (0x36383350 LE)
0x04    4     version (= 1)
0x08    4     total_size  (size of entire buffer in bytes)
0x0C    4     n_protos    (number of function prototypes; index 0 = main chunk)
0x10    4     n_strings   (number of interned string constants)
0x14    4     proto_table_offset
0x18    4     string_table_offset
0x1C    4     bytecode_section_offset
```

### proto table (n_protos × 24 bytes)

each entry:

```
offset  size  field
0x00    4     bytecode_off    (relative to bytecode_section_offset)
0x04    4     bytecode_len    (in bytes; multiple of 4)
0x08    4     consts_off      (relative to bytecode_section_offset)
0x0C    4     upvals_off      (relative to bytecode_section_offset; UpvalueRef array)
0x10    1     reserved (zero)
0x11    1     n_params
0x12    1     n_regs
0x13    1     n_upvalues
0x14    1     flags           (bit 0 = is_main; reserved otherwise)
0x15    1     reserved
0x16    2     n_consts        (u16, little endian; 0..65535)
```

### string table (n_strings × 8 bytes)

each entry points into the trailing data region:

```
offset  size  field
0x00    4     data_off    (offset from start of buffer to the string's raw bytes)
0x04    4     len         (in bytes)
```

string bytes live in the data region after all proto-owned sections.

### bytecode section

packed: for each proto in order, its bytecode is followed by its constants is followed by its upvalue refs. compiler outputs in proto-id order (0 = main, then nested). 4-byte alignment enforced between protos.

constants entry layout (8 bytes each):

```
offset  size  field
+0      4     value
+4      4     tag
```

only NUM (tag=2) and STR (tag=3) appear in constants. for STR, `value` is the index into the string table.

upvalue ref array entries (2 bytes each, no padding within a proto's array):

```
+0  source (0 or 1)
+1  index  (0..255)
```

### loading

```c
typedef struct {
    const uint8_t*      buf;
    uint32_t            buf_size;
    const ProtoEntry*   protos;          // pointer into buf
    const StringEntry*  string_entries;  // pointer into buf
    const uint8_t*      bytecode_section;
} LoadedProgram;

bool program_load(const uint8_t* buf, uint32_t size, LoadedProgram* out);
```

walks the header, validates magic + version + that all offsets fit in `size`, populates pointer fields. zero allocation. then VM init walks `string_entries` and interns all strings into the VM's `StringTable`.

---

## 12. VMState struct sketch

```c
typedef struct VMState {
    // value stack (lua sliding window)
    Value*    value_stack;
    Value*    value_stack_end;
    Value*    top;
    Value*    base;          // current frame base; mirrored in ebp during dispatch

    // call stack
    CallFrame* call_stack;
    CallFrame* call_top;
    CallFrame* call_end;

    // current execution state (mirrored in registers during dispatch)
    const uint32_t* ip;
    Closure*  closure;       // current closure (for upvalue access)

    // globals
    Value     globals[P386_GLOBAL_SLOTS];  // 1024

    // open upvalues, sorted by slot pointer (descending — newest first)
    Upvalue*  open_upvalues;

    // string intern table
    StringTable strings;

    // loaded program
    LoadedProgram program;

    // builtin registration
    CFunc     builtins[256];   // populated at vm_init
} VMState;
```

asm dispatch uses dedicated registers:

| reg | purpose                                |
| --- | -------------------------------------- |
| esi | bytecode IP                            |
| ebp | current frame base                     |
| edi | VMState pointer                        |
| esp | host C stack (untouched by VM normally)|

eax/ebx/ecx/edx are scratch within handlers.

**dispatch.** each handler ends with its own copy of the `NEXT` macro (replicated, token-threaded dispatch):

```nasm
mov  eax, [esi]                ; instruction word
add  esi, 4
movzx edx, al                  ; opcode
jmp  [dispatch_table + edx*4]
```

there is no shared loop and no per-instruction bookkeeping: `vm->ip` and `vm->last_opcode` are written only when the dispatcher exits (`done`). `dispatch_next` is one more copy of `NEXT`, for conditional jumps and slow paths.

**operands.** the `LOAD_RK` macro reads a register operand inline; only a constant calls `load_rk`. string constants are interned once at load into `vm->kstr` (indexed by string table index, GC roots), so LOADK, RK and GETFIELD/SETFIELD names never hash a string at run time.

**profiling builds.** `make prof` (`PROF.EXE`, nasm `-DPROFILE`) counts instructions per frame phase under QEMU `-icount`: builtins, table and string helpers, GC, dispatch, bytecodes run. `make profops` (`PROFOPS.EXE`, also `-DPROFILE_OPS`) adds instructions per opcode. `./test.sh prof` runs them on the real games; `PROF_EXE=PROFOPS` selects the second. the normal build has none of this code.

---

## 13. FFI boundary

### compile time (rust → C)

```c
// Returns a wc_malloc'd buffer or NULL on error.
// Caller takes ownership; free with wc_free when done.
uint8_t* p8_compile(const uint8_t* src, uint32_t src_len, uint32_t* out_len);
```

rust internally allocates the output buffer using `wc_malloc` directly (so C can free it). serializes the entire program into it. drops all rust-internal data structures (AST, name table, etc.) before returning. ownership transfers to C.

old `_p8_compile` / `_p8_free_program` / `_p8_program_bytecode` / `_p8_program_num_constants` / `_p8_program_num_protos` API is **removed** — replaced by single `p8_compile` returning a flat buffer + length.

`_p8_parse_rs` (validate-only) can stay as a thin wrapper that compiles then frees, or be removed.

### runtime (C only)

VM is entirely C+asm. zero rust calls at runtime. consumes the byte buffer directly via pointer arithmetic against the loaded `LoadedProgram` struct.

---

## 14. open TODOs (deferred from v1)

| topic                        | status                | when to revisit                              |
| ---------------------------- | --------------------- | -------------------------------------------- |
| source-level varargs (`...`) | compile error         | if real carts use them; survey first         |
| `goto` / labels              | parser ok, codegen NO | once the rest is stable; needs forward-ref pass |
| ADDI / SUBI / MULI imm ops   | not in v1             | if `i = i + 1` profiling-dominant            |
| comparison skip-next style   | not in v1             | if conditional-heavy carts profile slow      |
| `__tostring`                 | not implemented       | needs a Lua frame from inside `tostr`/`print` |
| string-num coercion in arith | trap on mismatch      | if cart compat demands; lua coerces silently |
| strict argument count        | silent nil-pad        | maybe never; lua is also lenient here        |

---

## 15. implementation order

after this spec is committed:

1. delete `rust/src/bytecode.rs` and `rust/src/compiler.rs`
2. write new `rust/src/bytecode.rs`: opcode enum, encoding helpers, container-format serializer
3. write new `rust/src/compiler.rs`: register-based codegen with simple temp-cursor allocator
4. write `include/p386_bytecode.h` mirroring container format constants & layouts
5. write `include/p386_vm.h` with VMState / Value / Closure / String / Table / etc.
6. write `include/builtins.h` enumerating builtin slots
7. write `src/p386_loader.c` for `program_load`
8. write `src/p386_value.c` for value/string/table primitives
9. write `src/p386_dispatch.asm` for threaded-dispatch core (distributed dispatch tail: done, see §12)
10. write `src/p386_handlers.c` (or `.asm`) for individual opcode handlers
11. write `src/p386_builtins.c` with C builtin implementations
12. write end-to-end smoke test: compile `local x = 1+2; print(x)`, run it, check `3` on screen

step 2-3 can ship as one PR. steps 4-10 as the next. steps 11-12 close the loop.

estimated effort: 4-6 focused days end-to-end, longer with the inevitable bugs.
