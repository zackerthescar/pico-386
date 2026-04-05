//! PICO-8 Lua Bytecode Specification
//!
//! Stack-based VM targeting i386 DOS4G/W with threaded dispatch.
//!
//! # Value Representation
//!
//! Each stack slot is 8 bytes:
//!
//! ```text
//! [type:u32][value:u32]
//! ```
//!
//! | Type | Tag | Value                                       |
//! |------|-----|---------------------------------------------|
//! | nil  | 0   | 0                                           |
//! | bool | 1   | 0 = false, 1 = true                         |
//! | num  | 2   | i32 16.16 fixed-point                       |
//! | str  | 3   | ptr → `{ len:u32, hash:u32, data:u8[len] }` |
//! | tab  | 4   | ptr → table object                          |
//! | func | 5   | ptr → closure object                        |
//! | coro | 6   | ptr → coroutine state                       |
//!
//! The type tag lives at the higher address (offset +4 from slot base).
//! In the asm interpreter with a downward-growing stack:
//! ```text
//! [esp+0] = value
//! [esp+4] = type
//! ```
//!
//! # Bytecode Encoding
//!
//! Variable-length instructions. Opcode is always 1 byte.
//! Operands follow immediately:
//!
//! ```text
//! [op:u8]                    — no operand
//! [op:u8][u8]                — slot index, count
//! [op:u8][u8][u8]            — two u8 operands
//! [op:u8][i16 LE]            — jump offset (relative to end of instruction)
//! [op:u8][u16 LE]            — constant pool index
//! [op:u8][i32 LE]            — inline 16.16 number literal
//! ```
//!
//! All multi-byte operands are little-endian (native i386).
//!
//! # Function Prototype
//!
//! Each function (including the top-level chunk) compiles to a prototype:
//!
//! ```text
//! FuncProto {
//!     bytecode:   [u8],         // instruction stream
//!     constants:  [Value],      // number and string literals
//!     prototypes: [FuncProto],  // nested function definitions
//!     n_params:   u8,           // number of fixed parameters
//!     n_locals:   u8,           // total local slots (includes params)
//!     n_upvalues: u8,           // number of captured upvalues
//!     has_varargs: bool,
//! }
//! ```
//!
//! # Interpreter Registers
//!
//! ```text
//! esi = instruction pointer (IP)
//! ebp = call frame base (locals start here, growing up)
//! esp = stack top (grows down, 8 bytes per push)
//! edi = pointer to VM state struct
//! ```
//!
//! # Threaded Dispatch Core
//!
//! ```nasm
//! dispatch:
//!     movzx eax, byte [esi]
//!     inc esi
//!     jmp [handler_table + eax*4]
//! ```

/// Opcode definitions. Each variant documents its encoding and stack effect.
///
/// Notation:
///   `( before -- after )` = stack effect
///   `[operand]` = bytes following the opcode
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Op {
    // ── Stack & Constants ────────────────────────────────────────

    /// `( -- )` No operation.
    Nop        = 0x00,
    /// `( a -- )` Discard top of stack.
    Pop        = 0x01,
    /// `( a -- a a )` Duplicate top of stack.
    Dup        = 0x02,
    /// `( -- nil )`
    PushNil    = 0x03,
    /// `( -- true )`
    PushTrue   = 0x04,
    /// `( -- false )`
    PushFalse  = 0x05,
    /// `[i32] ( -- num )` Push inline 16.16 number literal.
    PushNum    = 0x06,
    /// `[u16] ( -- str )` Push string from constant pool.
    PushStr    = 0x07,
    /// `[u16] ( -- func )` Create closure from prototype pool index.
    PushFunc   = 0x08,

    // ── Locals & Upvalues ────────────────────────────────────────

    /// `[u8:slot] ( -- val )` Push locals[slot].
    GetLocal   = 0x10,
    /// `[u8:slot] ( val -- )` Pop into locals[slot].
    SetLocal   = 0x11,
    /// `[u8:idx] ( -- val )` Push upvalues[idx].
    GetUpval   = 0x12,
    /// `[u8:idx] ( val -- )` Pop into upvalues[idx].
    SetUpval   = 0x13,

    // ── Globals ──────────────────────────────────────────────────

    /// `[u16:name] ( -- val )` Push `_ENV[name]`.
    GetGlobal  = 0x14,
    /// `[u16:name] ( val -- )` Pop into `_ENV[name]`.
    SetGlobal  = 0x15,

    // ── Table Operations ─────────────────────────────────────────

    /// `[u8:arr_hint][u8:hash_hint] ( -- table )` Create new table.
    NewTable   = 0x18,
    /// `( table key -- val )` Generic table index.
    GetTable   = 0x19,
    /// `( table key val -- )` Generic table set.
    SetTable   = 0x1A,
    /// `[u16:name] ( table -- val )` Optimized `table.field` get.
    GetField   = 0x1B,
    /// `[u16:name] ( table val -- )` Optimized `table.field` set.
    SetField   = 0x1C,
    /// `( table val -- )` Append val to table array part.
    Append     = 0x1D,

    // ── Arithmetic ───────────────────────────────────────────────
    // All operate on 16.16 fixed-point i32 values.

    /// `( a b -- a+b )`
    Add        = 0x20,
    /// `( a b -- a-b )`
    Sub        = 0x21,
    /// `( a b -- a*b )` Fixed-point: `((i64)a * b) >> 16`
    Mul        = 0x22,
    /// `( a b -- a/b )` Fixed-point: `((i64)a << 16) / b`
    Div        = 0x23,
    /// `( a b -- a%b )`
    Mod        = 0x24,
    /// `( a b -- a^b )` Power (integer exponent fast path).
    Pow        = 0x25,
    /// `( a b -- a\b )` PICO-8 integer division (truncated, no frac).
    IDiv       = 0x26,
    /// `( a -- -a )` Unary negate.
    Neg        = 0x27,

    // ── Bitwise ──────────────────────────────────────────────────
    // Operate on raw i32 (no fixed-point shift for bitwise ops,
    // matching PICO-8 semantics where bitwise works on the full 32 bits).

    /// `( a b -- a&b )`
    BAnd       = 0x28,
    /// `( a b -- a|b )`
    BOr        = 0x29,
    /// `( a b -- a^^b )` PICO-8 XOR.
    BXor       = 0x2A,
    /// `( a -- ~a )` Bitwise NOT.
    BNot       = 0x2B,
    /// `( a b -- a<<b )` Shift left.
    Shl        = 0x2C,
    /// `( a b -- a>>b )` Arithmetic shift right.
    Shr        = 0x2D,
    /// `( a b -- a>>>b )` Logical shift right.
    LShr       = 0x2E,
    /// `( a b -- a<<>b )` Rotate left.
    RotL       = 0x2F,
    /// `( a b -- a>><b )` Rotate right.
    RotR       = 0x30,

    // ── Comparison ───────────────────────────────────────────────
    // Pop two values, push boolean result.

    /// `( a b -- a==b )`
    Eq         = 0x31,
    /// `( a b -- a~=b )`
    Ne         = 0x32,
    /// `( a b -- a<b )`
    Lt         = 0x33,
    /// `( a b -- a<=b )`
    Le         = 0x34,
    /// `( a b -- a>b )`
    Gt         = 0x35,
    /// `( a b -- a>=b )`
    Ge         = 0x36,

    // ── Unary ────────────────────────────────────────────────────

    /// `( a -- not a )` Logical NOT (nil and false → true, else false).
    Not        = 0x37,
    /// `( a -- #a )` Length of string or table.
    Len        = 0x38,
    /// `( a -- @a )` PICO-8 peek (read byte from memory address).
    Peek       = 0x39,
    /// `( a -- $a )` PICO-8 peek2 (read 16-bit from memory address).
    Peek2      = 0x3A,

    // ── String ───────────────────────────────────────────────────

    /// `( a b -- a..b )` String concatenation (coerces numbers).
    Concat     = 0x3B,

    // ── Control Flow ─────────────────────────────────────────────

    /// `[i16:offset] ( -- )` Unconditional relative jump.
    Jmp        = 0x40,
    /// `[i16:offset] ( val -- )` Jump if falsy (nil or false). Pops.
    JmpFalse   = 0x41,
    /// `[i16:offset] ( val -- )` Jump if truthy. Pops.
    JmpTrue    = 0x42,
    /// `[i16:offset] ( val -- val? )` Jump if falsy, keep value (for `and`).
    /// If falsy: keep TOS, jump. If truthy: pop, continue.
    JmpFalseK  = 0x43,
    /// `[i16:offset] ( val -- val? )` Jump if truthy, keep value (for `or`).
    /// If truthy: keep TOS, jump. If falsy: pop, continue.
    JmpTrueK   = 0x44,

    // ── Loops ────────────────────────────────────────────────────
    //
    // Numeric for uses 3 hidden locals: index, limit, step.
    // `for i = start, stop, step` compiles to:
    //   push start, stop, step → FORPREP → loop body → FORLOOP

    /// `[u8:base][i16:offset] ( -- )` Initialize numeric for loop.
    /// Validates types. If step>0 and idx>limit (or step<0 and idx<limit),
    /// jump past loop. Otherwise fall through to body.
    ForPrep    = 0x45,
    /// `[u8:base][i16:offset] ( -- )` Numeric for loop step.
    /// idx += step; if not past limit, jump back to body start.
    ForLoop    = 0x46,
    /// `[u8:base][i16:offset] ( -- )` Generic for (iterator) loop step.
    /// Calls iterator function, tests for nil, branches.
    ForIn      = 0x47,

    // ── Functions ────────────────────────────────────────────────

    /// `[u8:nargs][u8:nrets] ( func args... -- rets... )`
    /// Call function with nargs arguments, adjust stack to nrets results.
    /// nrets=255 means "keep all returned values".
    Call       = 0x50,
    /// `[u8:nargs] ( func args... -- rets... )`
    /// Tail call — reuses current call frame.
    TailCall   = 0x51,
    /// `[u8:nvals] ( vals... -- )` Return nvals from current function.
    /// nvals=255 means "return everything above base".
    Return     = 0x52,
    /// `[u8:want] ( -- vals... )` Push `want` vararg values.
    VarArg     = 0x53,
    /// `[u8:slot] ( -- )` Close all upvalues ≥ slot.
    Close      = 0x54,

    // ── PICO-8 Compound Assignment Fast Path ─────────────────────
    // These are optional optimizations the compiler can emit for
    // `local_var += expr` patterns. Each pops TOS and applies to local.

    /// `[u8:slot] ( val -- )` locals[slot] += val
    AddLocal   = 0x58,
    /// `[u8:slot] ( val -- )` locals[slot] -= val
    SubLocal   = 0x59,
}

impl Op {
    /// Decode a byte into an opcode. Returns None for undefined opcodes.
    pub fn from_u8(b: u8) -> Option<Op> {
        // Safety: we only return Some for defined values
        match b {
            0x00 => Some(Op::Nop),
            0x01 => Some(Op::Pop),
            0x02 => Some(Op::Dup),
            0x03 => Some(Op::PushNil),
            0x04 => Some(Op::PushTrue),
            0x05 => Some(Op::PushFalse),
            0x06 => Some(Op::PushNum),
            0x07 => Some(Op::PushStr),
            0x08 => Some(Op::PushFunc),
            0x10 => Some(Op::GetLocal),
            0x11 => Some(Op::SetLocal),
            0x12 => Some(Op::GetUpval),
            0x13 => Some(Op::SetUpval),
            0x14 => Some(Op::GetGlobal),
            0x15 => Some(Op::SetGlobal),
            0x18 => Some(Op::NewTable),
            0x19 => Some(Op::GetTable),
            0x1A => Some(Op::SetTable),
            0x1B => Some(Op::GetField),
            0x1C => Some(Op::SetField),
            0x1D => Some(Op::Append),
            0x20 => Some(Op::Add),
            0x21 => Some(Op::Sub),
            0x22 => Some(Op::Mul),
            0x23 => Some(Op::Div),
            0x24 => Some(Op::Mod),
            0x25 => Some(Op::Pow),
            0x26 => Some(Op::IDiv),
            0x27 => Some(Op::Neg),
            0x28 => Some(Op::BAnd),
            0x29 => Some(Op::BOr),
            0x2A => Some(Op::BXor),
            0x2B => Some(Op::BNot),
            0x2C => Some(Op::Shl),
            0x2D => Some(Op::Shr),
            0x2E => Some(Op::LShr),
            0x2F => Some(Op::RotL),
            0x30 => Some(Op::RotR),
            0x31 => Some(Op::Eq),
            0x32 => Some(Op::Ne),
            0x33 => Some(Op::Lt),
            0x34 => Some(Op::Le),
            0x35 => Some(Op::Gt),
            0x36 => Some(Op::Ge),
            0x37 => Some(Op::Not),
            0x38 => Some(Op::Len),
            0x39 => Some(Op::Peek),
            0x3A => Some(Op::Peek2),
            0x3B => Some(Op::Concat),
            0x40 => Some(Op::Jmp),
            0x41 => Some(Op::JmpFalse),
            0x42 => Some(Op::JmpTrue),
            0x43 => Some(Op::JmpFalseK),
            0x44 => Some(Op::JmpTrueK),
            0x45 => Some(Op::ForPrep),
            0x46 => Some(Op::ForLoop),
            0x47 => Some(Op::ForIn),
            0x50 => Some(Op::Call),
            0x51 => Some(Op::TailCall),
            0x52 => Some(Op::Return),
            0x53 => Some(Op::VarArg),
            0x54 => Some(Op::Close),
            0x58 => Some(Op::AddLocal),
            0x59 => Some(Op::SubLocal),
            _ => None,
        }
    }

    /// Size of operands following this opcode (in bytes).
    pub fn operand_size(self) -> usize {
        match self {
            Op::Nop | Op::Pop | Op::Dup
            | Op::PushNil | Op::PushTrue | Op::PushFalse
            | Op::GetTable | Op::SetTable | Op::Append
            | Op::Add | Op::Sub | Op::Mul | Op::Div | Op::Mod
            | Op::Pow | Op::IDiv | Op::Neg
            | Op::BAnd | Op::BOr | Op::BXor | Op::BNot
            | Op::Shl | Op::Shr | Op::LShr | Op::RotL | Op::RotR
            | Op::Eq | Op::Ne | Op::Lt | Op::Le | Op::Gt | Op::Ge
            | Op::Not | Op::Len | Op::Peek | Op::Peek2
            | Op::Concat
                => 0,

            Op::GetLocal | Op::SetLocal
            | Op::GetUpval | Op::SetUpval
            | Op::Return | Op::VarArg | Op::Close
            | Op::TailCall
            | Op::AddLocal | Op::SubLocal
                => 1,

            Op::GetGlobal | Op::SetGlobal
            | Op::GetField | Op::SetField
            | Op::PushStr | Op::PushFunc
            | Op::Jmp | Op::JmpFalse | Op::JmpTrue
            | Op::JmpFalseK | Op::JmpTrueK
                => 2,

            Op::NewTable | Op::Call
                => 2, // two u8 operands

            Op::ForPrep | Op::ForLoop | Op::ForIn
                => 3, // u8 + i16

            Op::PushNum
                => 4, // inline i32
        }
    }
}

// ── Bytecode emitter (used by the compiler) ──────────────────────────

use alloc::vec::Vec;
use alloc::boxed::Box;

/// A compiled function prototype.
pub struct FuncProto {
    pub code: Vec<u8>,
    pub constants: Vec<Constant>,
    pub prototypes: Vec<FuncProto>,
    pub n_params: u8,
    pub n_locals: u8,
    pub n_upvalues: u8,
    pub has_varargs: bool,
}

/// Constant pool entry.
pub enum Constant {
    Num(i32),
    Str(Vec<u8>),
}

/// Bytecode builder for a single function.
pub struct Emitter {
    pub code: Vec<u8>,
    pub constants: Vec<Constant>,
    pub prototypes: Vec<FuncProto>,
    pub n_locals: u8,
    pub scope_depth: u8,
}

impl Emitter {
    pub fn new() -> Self {
        Emitter {
            code: Vec::new(),
            constants: Vec::new(),
            prototypes: Vec::new(),
            n_locals: 0,
            scope_depth: 0,
        }
    }

    /// Emit a no-operand instruction.
    pub fn emit(&mut self, op: Op) {
        self.code.push(op as u8);
    }

    /// Emit instruction with one u8 operand.
    pub fn emit_u8(&mut self, op: Op, arg: u8) {
        self.code.push(op as u8);
        self.code.push(arg);
    }

    /// Emit instruction with two u8 operands.
    pub fn emit_u8u8(&mut self, op: Op, a: u8, b: u8) {
        self.code.push(op as u8);
        self.code.push(a);
        self.code.push(b);
    }

    /// Emit instruction with u16 operand (LE).
    pub fn emit_u16(&mut self, op: Op, arg: u16) {
        self.code.push(op as u8);
        self.code.extend_from_slice(&arg.to_le_bytes());
    }

    /// Emit instruction with i16 operand (LE).
    pub fn emit_i16(&mut self, op: Op, arg: i16) {
        self.code.push(op as u8);
        self.code.extend_from_slice(&arg.to_le_bytes());
    }

    /// Emit instruction with i32 operand (LE).
    pub fn emit_i32(&mut self, op: Op, arg: i32) {
        self.code.push(op as u8);
        self.code.extend_from_slice(&arg.to_le_bytes());
    }

    /// Emit instruction with u8 + i16 operands.
    pub fn emit_u8_i16(&mut self, op: Op, slot: u8, offset: i16) {
        self.code.push(op as u8);
        self.code.push(slot);
        self.code.extend_from_slice(&offset.to_le_bytes());
    }

    /// Current code position (for jump targets).
    pub fn pos(&self) -> usize {
        self.code.len()
    }

    /// Emit a jump with placeholder offset, returns position of the i16 offset
    /// for later patching.
    pub fn emit_jump(&mut self, op: Op) -> usize {
        self.code.push(op as u8);
        let patch_pos = self.code.len();
        self.code.extend_from_slice(&0i16.to_le_bytes()); // placeholder
        patch_pos
    }

    /// Patch a previously emitted jump's i16 offset.
    /// `patch_pos` is the position of the i16 bytes (from emit_jump).
    /// Target is relative to the end of the jump instruction.
    pub fn patch_jump(&mut self, patch_pos: usize) {
        let target = self.code.len();
        let offset = (target as isize - (patch_pos + 2) as isize) as i16;
        let bytes = offset.to_le_bytes();
        self.code[patch_pos] = bytes[0];
        self.code[patch_pos + 1] = bytes[1];
    }

    /// Add a number constant, return its pool index.
    pub fn add_num_constant(&mut self, val: i32) -> u16 {
        // Check for existing
        for (i, c) in self.constants.iter().enumerate() {
            if let Constant::Num(n) = c {
                if *n == val { return i as u16; }
            }
        }
        let idx = self.constants.len();
        self.constants.push(Constant::Num(val));
        idx as u16
    }

    /// Add a string constant, return its pool index.
    pub fn add_str_constant(&mut self, val: Vec<u8>) -> u16 {
        for (i, c) in self.constants.iter().enumerate() {
            if let Constant::Str(s) = c {
                if s.as_slice() == val.as_slice() { return i as u16; }
            }
        }
        let idx = self.constants.len();
        self.constants.push(Constant::Str(val));
        idx as u16
    }

    /// Add a nested function prototype, return its index.
    pub fn add_proto(&mut self, proto: FuncProto) -> u16 {
        let idx = self.prototypes.len();
        self.prototypes.push(proto);
        idx as u16
    }

    /// Allocate a local variable slot.
    pub fn alloc_local(&mut self) -> u8 {
        let slot = self.n_locals;
        self.n_locals += 1;
        slot
    }

    /// Finalize into a FuncProto.
    pub fn finish(self, n_params: u8, has_varargs: bool, n_upvalues: u8) -> FuncProto {
        FuncProto {
            code: self.code,
            constants: self.constants,
            prototypes: self.prototypes,
            n_params,
            n_locals: self.n_locals,
            n_upvalues,
            has_varargs,
        }
    }
}
