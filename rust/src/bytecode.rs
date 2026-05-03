use alloc::vec::Vec;

pub const P386_BC_MAGIC: u32 = 0x3638_3350;
pub const P386_BC_VERSION: u32 = 1;
pub const P386_PROTO_FLAG_MAIN: u8 = 0x01;

pub const P386_TAG_NIL: u32 = 0;
pub const P386_TAG_BOOL: u32 = 1;
pub const P386_TAG_NUM: u32 = 2;
pub const P386_TAG_STR: u32 = 3;

pub const P386_OP_MOVE: u8 = 0x01;
pub const P386_OP_LOADK: u8 = 0x02;
pub const P386_OP_LOADT: u8 = 0x03;
pub const P386_OP_LOADF: u8 = 0x04;
pub const P386_OP_LOADN: u8 = 0x05;
pub const P386_OP_GETGLOBAL: u8 = 0x10;
pub const P386_OP_SETGLOBAL: u8 = 0x11;
pub const P386_OP_NEWTABLE: u8 = 0x18;
pub const P386_OP_GETTABLE: u8 = 0x19;
pub const P386_OP_SETTABLE: u8 = 0x1a;
pub const P386_OP_GETFIELD: u8 = 0x1b;
pub const P386_OP_SETFIELD: u8 = 0x1c;
pub const P386_OP_ADD: u8 = 0x20;
pub const P386_OP_SUB: u8 = 0x21;
pub const P386_OP_MUL: u8 = 0x22;
pub const P386_OP_DIV: u8 = 0x23;
pub const P386_OP_IDIV: u8 = 0x24;
pub const P386_OP_MOD: u8 = 0x25;
pub const P386_OP_POW: u8 = 0x26;
pub const P386_OP_NEG: u8 = 0x27;
pub const P386_OP_BAND: u8 = 0x28;
pub const P386_OP_BOR: u8 = 0x29;
pub const P386_OP_BXOR: u8 = 0x2a;
pub const P386_OP_BNOT: u8 = 0x2b;
pub const P386_OP_SHL: u8 = 0x2c;
pub const P386_OP_SHR: u8 = 0x2d;
pub const P386_OP_LSHR: u8 = 0x2e;
pub const P386_OP_ROTL: u8 = 0x2f;
pub const P386_OP_ROTR: u8 = 0x30;
pub const P386_OP_EQ: u8 = 0x31;
pub const P386_OP_NE: u8 = 0x32;
pub const P386_OP_LT: u8 = 0x33;
pub const P386_OP_LE: u8 = 0x34;
pub const P386_OP_GT: u8 = 0x35;
pub const P386_OP_GE: u8 = 0x36;
pub const P386_OP_NOT: u8 = 0x37;
pub const P386_OP_LEN: u8 = 0x38;
pub const P386_OP_PEEK: u8 = 0x39;
pub const P386_OP_PEEK2: u8 = 0x3a;
pub const P386_OP_CONCAT: u8 = 0x3b;
pub const P386_OP_JMP: u8 = 0x40;
pub const P386_OP_JMPF: u8 = 0x41;
pub const P386_OP_JMPT: u8 = 0x42;
pub const P386_OP_CLOSURE: u8 = 0x50;
pub const P386_OP_CALL: u8 = 0x51;
pub const P386_OP_RETURN: u8 = 0x53;

pub const P386_BUILTIN_PRINT: u8 = 0;
pub const P386_BUILTIN_CLS: u8 = 1;
pub const P386_BUILTIN_PSET: u8 = 2;
pub const P386_BUILTIN_PGET: u8 = 3;
pub const P386_BUILTIN_LINE: u8 = 4;
pub const P386_BUILTIN_RECT: u8 = 5;
pub const P386_BUILTIN_RECTF: u8 = 6;
pub const P386_BUILTIN_CIRCFILL: u8 = 7;
pub const P386_BUILTIN_SPR: u8 = 8;
pub const P386_BUILTIN_MAP: u8 = 9;
pub const P386_BUILTIN_BTN: u8 = 10;
pub const P386_BUILTIN_BTNP: u8 = 11;
pub const P386_BUILTIN_SFX: u8 = 12;
pub const P386_BUILTIN_MUSIC: u8 = 13;
pub const P386_BUILTIN_PAIRS: u8 = 14;
pub const P386_BUILTIN_IPAIRS: u8 = 15;
pub const P386_BUILTIN_COUNT: u8 = 16;
pub const P386_GLOBAL_INIT: u8 = 16;
pub const P386_GLOBAL_UPDATE: u8 = 17;
pub const P386_GLOBAL_UPDATE60: u8 = 18;
pub const P386_GLOBAL_DRAW: u8 = 19;
pub const P386_USER_GLOBAL_BASE: u8 = 20;

#[derive(Clone)]
pub enum Constant {
    Nil,
    Bool(bool),
    Num(i32),
    Str(Vec<u8>),
}

/// Compiler result exposed through the C FFI as an opaque handle.
///
/// `code` is a complete P386 bytecode container, not just raw instructions.
/// The legacy C API name predates the container format.
pub struct FuncProto {
    pub code: Vec<u8>,
    pub proto_count: u32,
    pub constants: Vec<Constant>,
    pub prototypes: Vec<FuncProto>,
    pub instructions: Vec<u32>,
    pub n_regs: u8,
    pub n_params: u8,
}

impl FuncProto {
    pub fn new(instructions: Vec<u32>, constants: Vec<Constant>, prototypes: Vec<FuncProto>, n_regs: u8, n_params: u8) -> Self {
        let proto_count = count_protos(&prototypes);
        let code = emit_program(&instructions, &constants, &prototypes, n_regs, n_params);
        Self { code, proto_count, constants, prototypes, instructions, n_regs, n_params }
    }
}

pub fn abc(op: u8, a: u8, b: u8, c: u8) -> u32 {
    (op as u32) | ((a as u32) << 8) | ((b as u32) << 16) | ((c as u32) << 24)
}

pub fn abx(op: u8, a: u8, bx: u16) -> u32 {
    abc(op, a, (bx & 0xff) as u8, (bx >> 8) as u8)
}

pub fn asbx(op: u8, a: u8, sbx: i16) -> u32 {
    abx(op, a, sbx as u16)
}

pub fn rk_reg(r: u8) -> u8 { r & 0x7f }
pub fn rk_const(k: u8) -> u8 { 0x80 | (k & 0x7f) }

fn align4(v: usize) -> usize { (v + 3) & !3 }

fn push_u8(out: &mut Vec<u8>, v: u8) { out.push(v); }
fn push_u32(out: &mut Vec<u8>, v: u32) { out.extend_from_slice(&v.to_le_bytes()); }
fn push_i32(out: &mut Vec<u8>, v: i32) { out.extend_from_slice(&v.to_le_bytes()); }

fn patch_u32(out: &mut [u8], off: usize, v: u32) {
    out[off..off + 4].copy_from_slice(&v.to_le_bytes());
}

fn pad4(out: &mut Vec<u8>) {
    while out.len() & 3 != 0 { out.push(0); }
}

fn string_index(strings: &[Vec<u8>], needle: &[u8]) -> u32 {
    strings.iter().position(|s| s.as_slice() == needle).unwrap_or(0) as u32
}

fn collect_all_strings(constants: &[Constant], protos: &[FuncProto], out: &mut Vec<Vec<u8>>) {
    for c in constants {
        if let Constant::Str(s) = c {
            if !out.iter().any(|x| x.as_slice() == s.as_slice()) { out.push(s.clone()); }
        }
    }
    for p in protos { collect_all_strings(&p.constants, &p.prototypes, out); }
}

fn count_protos(protos: &[FuncProto]) -> u32 {
    let mut count = 1u32;
    for p in protos {
        count = count.saturating_add(count_protos(&p.prototypes));
    }
    count
}

fn rewrite_nested_closure_indices(insns: &[u32], child_base: u16, child_protos: &[FuncProto]) -> Vec<u32> {
    let mut child_offsets = Vec::new();
    let mut next = child_base;
    for p in child_protos {
        child_offsets.push(next);
        next = next.saturating_add(p.proto_count.min(u16::MAX as u32) as u16);
    }

    insns.iter().map(|&ins| {
        let op = (ins & 0xff) as u8;
        if op != P386_OP_CLOSURE { return ins; }
        let local_idx = ((ins >> 16) & 0xffff) as usize;
        if local_idx == 0 { return ins; }
        let child_idx = local_idx - 1;
        if child_idx >= child_offsets.len() { return ins; }
        let bx = child_offsets[child_idx] as u32;
        (ins & 0x0000_ffff) | (bx << 16)
    }).collect()
}

fn flatten_protos<'a>(root: (&'a [u32], &'a [Constant], u8, u8, &'a [FuncProto]), out: &mut Vec<(Vec<u32>, &'a [Constant], u8, u8)>) {
    let root_index = out.len() as u16;
    let child_base = root_index.saturating_add(1);
    let (insns, consts, regs, params, children) = root;
    let rewritten = rewrite_nested_closure_indices(insns, child_base, children);
    out.push((rewritten, consts, regs, params));
    for p in children {
        flatten_protos((&p.instructions, &p.constants, p.n_regs, p.n_params, &p.prototypes), out);
    }
}

pub fn emit_program(instructions: &[u32], constants: &[Constant], prototypes: &[FuncProto], n_regs: u8, n_params: u8) -> Vec<u8> {
    let mut strings = Vec::new();
    collect_all_strings(constants, prototypes, &mut strings);
    let mut protos = Vec::new();
    flatten_protos((instructions, constants, n_regs, n_params, prototypes), &mut protos);
    let n_protos = protos.len() as u32;
    let n_strings = strings.len() as u32;

    let header_size = 32usize;
    let proto_table_offset = header_size;
    let proto_table_size = protos.len() * 24;
    let string_table_offset = proto_table_offset + proto_table_size;
    let string_table_size = strings.len() * 8;
    let bytecode_section_offset = align4(string_table_offset + string_table_size);

    let mut out = Vec::new();

    push_u32(&mut out, P386_BC_MAGIC);
    push_u32(&mut out, P386_BC_VERSION);
    let total_size_patch = out.len();
    push_u32(&mut out, 0);
    push_u32(&mut out, n_protos);
    push_u32(&mut out, n_strings);
    push_u32(&mut out, proto_table_offset as u32);
    push_u32(&mut out, string_table_offset as u32);
    push_u32(&mut out, bytecode_section_offset as u32);

    let proto_patch_start = out.len();
    for _ in 0..protos.len() {
        for _ in 0..24 { out.push(0); }
    }

    let mut string_entry_patches = Vec::new();
    for s in &strings {
        string_entry_patches.push(out.len());
        push_u32(&mut out, 0);
        push_u32(&mut out, s.len() as u32);
    }

    while out.len() < bytecode_section_offset { out.push(0); }

    for (pi, (insns, consts, regs, params)) in protos.iter().enumerate() {
        let code_off = out.len() - bytecode_section_offset;
        for &ins in insns { push_u32(&mut out, ins); }
        pad4(&mut out);
        let code_len = insns.len() * 4;
        let consts_off = out.len() - bytecode_section_offset;
        for c in consts.iter().take(255) {
            match c {
                Constant::Nil => { push_i32(&mut out, 0); push_u32(&mut out, P386_TAG_NIL); }
                Constant::Bool(v) => { push_i32(&mut out, if *v { 1 } else { 0 }); push_u32(&mut out, P386_TAG_BOOL); }
                Constant::Num(v) => { push_i32(&mut out, *v); push_u32(&mut out, P386_TAG_NUM); }
                Constant::Str(s) => { push_i32(&mut out, string_index(&strings, s) as i32); push_u32(&mut out, P386_TAG_STR); }
            }
        }
        pad4(&mut out);
        let upvals_off = out.len() - bytecode_section_offset;
        let patch = proto_patch_start + pi * 24;
        patch_u32(&mut out, patch, code_off as u32);
        patch_u32(&mut out, patch + 4, code_len as u32);
        patch_u32(&mut out, patch + 8, consts_off as u32);
        patch_u32(&mut out, patch + 12, upvals_off as u32);
        out[patch + 16] = consts.len().min(255) as u8;
        out[patch + 17] = *params;
        out[patch + 18] = (*regs).max(1);
        out[patch + 19] = 0;
        out[patch + 20] = if pi == 0 { P386_PROTO_FLAG_MAIN } else { 0 };
    }

    for (i, s) in strings.iter().enumerate() {
        let data_off = out.len() as u32;
        patch_u32(&mut out, string_entry_patches[i], data_off);
        out.extend_from_slice(s);
        pad4(&mut out);
    }

    let total = out.len() as u32;
    patch_u32(&mut out, total_size_patch, total);
    out
}
