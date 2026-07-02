# pico386 bytecode spec

target: i386DX-25 minimum, DOS/4GW flat 32-bit, source = PICO-8 lua dialect (8192-token cap)
runtime: register-based VM, threaded dispatch, written in C with asm hot path
compiler: rust, no_std, runs on the target via watcom toolchain
status: v1 design

this spec is **the contract** between the rust compiler and the C runtime. anything not specified here is implementation defined and can change. anything specified here changing requires a coordinated update on both sides.

open TODOs at end. things deferred from v1: GC (see `TODO_GC.md`), source-level varargs, `goto`/label, full metatables, coroutines.

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

NUM is the only non-pointer payload. STR/TAB/FUNC are heap pointers; CFUNC is a code pointer (text section).

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

so when an opcode uses RK on B and C, each function effectively has **128 registers** and **128 constants per pool**. for opcodes that don't use RK on a given operand, the full 8 bits are available.

### multi-byte operands

certain opcodes treat B|C as a single 16-bit value:

| field name | encoding              | range          | used by              |
| ---------- | --------------------- | -------------- | -------------------- |
| `Bx`       | `B \| (C << 8)`       | 0..65535       | LOADK, CLOSURE       |
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
| GETGLOBAL | 0x10 | A, B     | `R[A] = globals[B]`    |
| SETGLOBAL | 0x11 | A, B     | `globals[B] = R[A]`    |

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
| SETFIELD | 0x1C | A, B, C   | `R[A][K[B]] = R[C]` (B is constant idx, must be STR)   |

GETFIELD/SETFIELD are the fast-path for `t.field` syntax. C in GETFIELD and B in SETFIELD are full 8-bit constant indices (no K-flag, always constant — the field name).

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

result is BOOL. EQ/NE work on any value pair: pointer-equal for STR/TAB/FUNC/CFUNC; structural compare for NUM/BOOL/NIL. ordered comparisons (LT/LE/GT/GE) require both operands NUM (or both STR for lex compare); mismatch traps.

### unary

| op    | hex  | operands | semantics                                              |
| ----- | ---- | -------- | ------------------------------------------------------ |
| NOT   | 0x37 | A, B     | `R[A] = (R[B] is nil or false) ? true : false`         |
| LEN   | 0x38 | A, B     | `R[A] = #R[B]` (string len for STR, array_len for TAB) |
| PEEK  | 0x39 | A, B     | `R[A] = mem8[R[B]]`  (PICO-8 `@`)                      |
| PEEK2 | 0x3A | A, B     | `R[A] = mem16[R[B]]` (PICO-8 `$`)                      |

### string

| op     | hex  | operands  | semantics                       |
| ------ | ---- | --------- | ------------------------------- |
| CONCAT | 0x3B | A, B, C   | `R[A] = R[B] .. R[C]`           |

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
    uint8_t         _padding[2];
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
   - if TAG_CFUNC: invoke `cfunc(vm, 2)` directly. C function reads args from `vm->top - 2 .. vm->top`, writes returns starting at `vm->base[5]` (the same slot the function was in), updates `vm->top`. handler advances IP. no CallFrame push.
   - if TAG_FUNC: push CallFrame `{ return_ip = ip+4, return_base = base, closure = current, return_reg = 5, want_rets = 1 }`. set `base = current_base + 6` (so R[6] becomes callee's R[0]). set `closure = R[5].func`. set `ip = closure->proto->bytecode`. dispatch.

4. callee runs; eventually `RETURN R, n`:
   - copy `base[R..R+n-1]` to `caller_base[caller_return_reg ..]`.
   - pad with nil if `n < want_rets`.
   - restore `base = caller_base`, `ip = caller_return_ip`, pop CallFrame.
   - dispatch.

---

## 5. globals

256-slot flat array on the VMState:

```c
Value globals[256];
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
; GETGLOBAL A, B
movzx ecx, ah                          ; A
shr   eax, 16
movzx edx, al                          ; B (slot)
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

lua-style hybrid array+hash. crib heavily from `lua-5.1/src/ltable.c` (MIT licensed; can vendor in).

```c
typedef struct {
    Value*    array;
    uint32_t  array_len;     // logical length (highest contiguous int key)
    uint32_t  array_cap;
    Node*     hash;
    uint8_t   hash_lsize;    // log2 of hash capacity
    Table*    metatable;     // NULL or table with __index
    // GC bits added later
} Table;

typedef struct {
    Value     key;
    Value     val;
    int32_t   next;          // Brent's chaining; -1 = end of bucket
} Node;
```

resize heuristic: same as lua's `rehash` — count keys by category (positive int, other), pick array_cap as the largest power-of-two such that the array part is ≥50% full, hash takes the rest.

### metatables (limited)

**only `__index` is honored**, and **only on read** (GETTABLE / GETFIELD).

- `t.foo` where `t` doesn't have `foo`:
  - if `t.metatable` is nil → return nil
  - if `t.metatable.__index` is a table → look up `foo` in that table (recursively, max 4 levels deep)
  - if `t.metatable.__index` is a function → call it with `(t, "foo")`, return the result

writes (`t.foo = x`) **never** consult metatable; always write directly to `t`.

other metamethods (`__newindex`, `__add`, `__call`, `__tostring`, ...) are **not implemented**. carts that depend on them will silently misbehave.

`setmetatable(t, m)` / `getmetatable(t)` are C builtins.

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
0x10    1     n_consts
0x11    1     n_params
0x12    1     n_regs
0x13    1     n_upvalues
0x14    1     flags           (bit 0 = is_main; reserved otherwise)
0x15    3     reserved
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
    Value     globals[256];

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
| garbage collection           | leak forever          | when carts OOM in <30 min runtime — see TODO_GC.md |
| source-level varargs (`...`) | compile error         | if real carts use them; survey first         |
| `goto` / labels              | parser ok, codegen NO | once the rest is stable; needs forward-ref pass |
| ADDI / SUBI / MULI imm ops   | not in v1             | if `i = i + 1` profiling-dominant            |
| comparison skip-next style   | not in v1             | if conditional-heavy carts profile slow      |
| full metatables              | only `__index` read   | as cart compat demands                       |
| coroutines                   | not in v1             | indefinitely; rare in PICO-8 carts           |
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
9. write `src/p386_dispatch.asm` for threaded-dispatch core (distributed dispatch tail)
10. write `src/p386_handlers.c` (or `.asm`) for individual opcode handlers
11. write `src/p386_builtins.c` with C builtin implementations
12. write end-to-end smoke test: compile `local x = 1+2; print(x)`, run it, check `3` on screen

step 2-3 can ship as one PR. steps 4-10 as the next. steps 11-12 close the loop.

estimated effort: 4-6 focused days end-to-end, longer with the inevitable bugs.
