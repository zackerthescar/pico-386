use alloc::vec::Vec;

pub const P386_BC_MAGIC: u32 = 0x3638_3350;
pub const P386_BC_VERSION: u32 = 1;
pub const P386_PROTO_FLAG_MAIN: u8 = 0x01;

pub const P386_TAG_NIL: u32 = 0;
pub const P386_TAG_BOOL: u32 = 1;
pub const P386_TAG_NUM: u32 = 2;
pub const P386_TAG_STR: u32 = 3;

pub const P386_OP_RETURN: u8 = 0x53;

#[derive(Clone)]
pub enum Constant {
    Nil,
    Bool(bool),
    Num(i32),
    Str(Vec<u8>),
}

/// Host/DOS compiler result exposed through the C FFI as an opaque handle.
///
/// `code` is a complete P386 bytecode container, not just raw instructions.
/// The legacy C API name predates the container format.
pub struct FuncProto {
    pub code: Vec<u8>,
    pub constants: Vec<Constant>,
    pub prototypes: Vec<FuncProto>,
}

impl FuncProto {
    pub fn new(constants: Vec<Constant>, prototypes: Vec<FuncProto>) -> Self {
        let code = emit_container(&constants);
        Self { code, constants, prototypes }
    }
}

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

fn instr_abc(op: u8, a: u8, b: u8, c: u8) -> u32 {
    (op as u32) | ((a as u32) << 8) | ((b as u32) << 16) | ((c as u32) << 24)
}

fn collect_strings(constants: &[Constant]) -> Vec<Vec<u8>> {
    let mut strings = Vec::new();
    for c in constants {
        if let Constant::Str(s) = c {
            if !strings.iter().any(|x: &Vec<u8>| x.as_slice() == s.as_slice()) {
                strings.push(s.clone());
            }
        }
    }
    strings
}

fn string_index(strings: &[Vec<u8>], needle: &[u8]) -> u32 {
    strings.iter().position(|s| s.as_slice() == needle).unwrap_or(0) as u32
}

pub fn emit_container(constants: &[Constant]) -> Vec<u8> {
    let strings = collect_strings(constants);
    let n_protos = 1u32;
    let n_strings = strings.len() as u32;

    let header_size = 32usize;
    let proto_table_offset = header_size;
    let proto_table_size = 24usize;
    let string_table_offset = proto_table_offset + proto_table_size;
    let string_table_size = strings.len() * 8;
    let bytecode_section_offset = align4(string_table_offset + string_table_size);

    let mut out = Vec::new();

    // Header, total_size patched later.
    push_u32(&mut out, P386_BC_MAGIC);
    push_u32(&mut out, P386_BC_VERSION);
    let total_size_patch = out.len();
    push_u32(&mut out, 0);
    push_u32(&mut out, n_protos);
    push_u32(&mut out, n_strings);
    push_u32(&mut out, proto_table_offset as u32);
    push_u32(&mut out, string_table_offset as u32);
    push_u32(&mut out, bytecode_section_offset as u32);

    // Single main proto. Offsets are relative to bytecode_section_offset.
    let code_off = 0usize;
    let code_len = 4usize;
    let consts_off = align4(code_off + code_len);
    let n_consts = constants.len().min(255);
    let consts_len = n_consts * 8;
    let upvals_off = align4(consts_off + consts_len);

    push_u32(&mut out, code_off as u32);
    push_u32(&mut out, code_len as u32);
    push_u32(&mut out, consts_off as u32);
    push_u32(&mut out, upvals_off as u32);
    push_u8(&mut out, n_consts as u8);
    push_u8(&mut out, 0); // n_params
    push_u8(&mut out, 1); // n_regs
    push_u8(&mut out, 0); // n_upvalues
    push_u8(&mut out, P386_PROTO_FLAG_MAIN);
    out.extend_from_slice(&[0, 0, 0]);

    // String table, data offsets patched after string payload is emitted.
    let mut string_entry_patches = Vec::new();
    for s in &strings {
        string_entry_patches.push(out.len());
        push_u32(&mut out, 0);
        push_u32(&mut out, s.len() as u32);
    }

    while out.len() < bytecode_section_offset { out.push(0); }

    // Bytecode section: top-level RETURN. Enough for loader/VM to halt cleanly.
    push_u32(&mut out, instr_abc(P386_OP_RETURN, 0, 1, 0));
    pad4(&mut out);

    for c in constants.iter().take(255) {
        match c {
            Constant::Nil => { push_i32(&mut out, 0); push_u32(&mut out, P386_TAG_NIL); }
            Constant::Bool(v) => { push_i32(&mut out, if *v { 1 } else { 0 }); push_u32(&mut out, P386_TAG_BOOL); }
            Constant::Num(v) => { push_i32(&mut out, *v); push_u32(&mut out, P386_TAG_NUM); }
            Constant::Str(s) => { push_i32(&mut out, string_index(&strings, s) as i32); push_u32(&mut out, P386_TAG_STR); }
        }
    }
    pad4(&mut out);

    // No upvalues yet.
    pad4(&mut out);

    // String payloads live after bytecode/consts/upvals. Offsets are absolute.
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
