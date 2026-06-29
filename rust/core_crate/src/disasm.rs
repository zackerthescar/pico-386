//! Host-only disassembler for compiled FuncProtos. Used by the p8c CLI and
//! host-side tests for fast iteration without QEMU.
extern crate std;
use std::string::String;
use std::vec::Vec;
use std::format;
use crate::bytecode::*;

fn opname(op: u8) -> &'static str {
    match op {
        P386_OP_MOVE => "MOVE",
        P386_OP_LOADK => "LOADK",
        P386_OP_LOADT => "LOADT",
        P386_OP_LOADF => "LOADF",
        P386_OP_LOADN => "LOADN",
        P386_OP_GETGLOBAL => "GETGLOBAL",
        P386_OP_SETGLOBAL => "SETGLOBAL",
        P386_OP_GETUPVAL => "GETUPVAL",
        P386_OP_SETUPVAL => "SETUPVAL",
        P386_OP_CLOSE => "CLOSE",
        P386_OP_NEWTABLE => "NEWTABLE",
        P386_OP_GETTABLE => "GETTABLE",
        P386_OP_SETTABLE => "SETTABLE",
        P386_OP_GETFIELD => "GETFIELD",
        P386_OP_SETFIELD => "SETFIELD",
        P386_OP_ADD => "ADD",
        P386_OP_SUB => "SUB",
        P386_OP_MUL => "MUL",
        P386_OP_DIV => "DIV",
        P386_OP_IDIV => "IDIV",
        P386_OP_MOD => "MOD",
        P386_OP_POW => "POW",
        P386_OP_NEG => "NEG",
        P386_OP_BAND => "BAND",
        P386_OP_BOR => "BOR",
        P386_OP_BXOR => "BXOR",
        P386_OP_BNOT => "BNOT",
        P386_OP_SHL => "SHL",
        P386_OP_SHR => "SHR",
        P386_OP_LSHR => "LSHR",
        P386_OP_ROTL => "ROTL",
        P386_OP_ROTR => "ROTR",
        P386_OP_EQ => "EQ",
        P386_OP_NE => "NE",
        P386_OP_LT => "LT",
        P386_OP_LE => "LE",
        P386_OP_GT => "GT",
        P386_OP_GE => "GE",
        P386_OP_NOT => "NOT",
        P386_OP_LEN => "LEN",
        P386_OP_PEEK => "PEEK",
        P386_OP_PEEK2 => "PEEK2",
        P386_OP_CONCAT => "CONCAT",
        P386_OP_JMP => "JMP",
        P386_OP_JMPF => "JMPF",
        P386_OP_JMPT => "JMPT",
        P386_OP_FORPREP => "FORPREP",
        P386_OP_FORLOOP => "FORLOOP",
        P386_OP_TFORCALL => "TFORCALL",
        P386_OP_TFORLOOP => "TFORLOOP",
        P386_OP_CLOSURE => "CLOSURE",
        P386_OP_CALL => "CALL",
        P386_OP_TAILCALL => "TAILCALL",
        P386_OP_RETURN => "RETURN",
        _ => "???",
    }
}

fn rk(x: u8) -> String {
    if x & 0x80 != 0 { format!("K{}", x & 0x7f) } else { format!("R{}", x & 0x7f) }
}

pub fn disasm_proto(p: &FuncProto, name: &str, out: &mut String) {
    out.push_str(&format!("=== {} (regs={}, params={}, upvals={}, consts={}, protos={}) ===\n",
        name, p.n_regs, p.n_params, p.upvalues.len(), p.constants.len(), p.prototypes.len()));
    for (i, u) in p.upvalues.iter().enumerate() {
        out.push_str(&format!("  upval[{}] = {} {}\n", i,
            if u.0 == 0 { "parent-local" } else { "parent-upval" }, u.1));
    }
    for (i, c) in p.constants.iter().enumerate() {
        let s = match c {
            Constant::Nil => "nil".into(),
            Constant::Bool(b) => format!("{}", b),
            Constant::Num(n) => format!("{}({:.4})", n, *n as f64 / 65536.0),
            Constant::Str(s) => format!("{:?}", std::str::from_utf8(s).unwrap_or("<bin>")),
        };
        out.push_str(&format!("  K{} = {}\n", i, s));
    }
    for (pc, &ins) in p.instructions.iter().enumerate() {
        let op = (ins & 0xff) as u8;
        let a = ((ins >> 8) & 0xff) as u8;
        let b = ((ins >> 16) & 0xff) as u8;
        let c = ((ins >> 24) & 0xff) as u8;
        let bx = ((ins >> 16) & 0xffff) as u16;
        let sbx = bx as i16;
        let txt = match op {
            P386_OP_MOVE | P386_OP_NOT | P386_OP_LEN | P386_OP_NEG | P386_OP_BNOT
            | P386_OP_PEEK | P386_OP_PEEK2 => format!("R{}, {}", a, rk(b)),
            P386_OP_LOADK | P386_OP_CLOSURE => format!("R{}, {}{}", a, if op==P386_OP_LOADK {"K"} else {"P"}, bx),
            P386_OP_LOADT | P386_OP_LOADF => format!("R{}", a),
            P386_OP_LOADN => format!("R{}, n={}", a, b),
            P386_OP_GETGLOBAL | P386_OP_SETGLOBAL | P386_OP_GETUPVAL | P386_OP_SETUPVAL => format!("R{}, {}", a, b),
            P386_OP_CLOSE => format!("R{}", a),
            P386_OP_NEWTABLE => format!("R{}, {}, {}", a, b, c),
            P386_OP_GETTABLE | P386_OP_SETTABLE => format!("R{}, {}, {}", a, rk(b), rk(c)),
            P386_OP_GETFIELD => format!("R{}, R{}, K{}", a, b, c),
            P386_OP_SETFIELD => format!("R{}, K{}, {}", a, b, rk(c)),
            P386_OP_ADD | P386_OP_SUB | P386_OP_MUL | P386_OP_DIV | P386_OP_IDIV
            | P386_OP_MOD | P386_OP_POW | P386_OP_BAND | P386_OP_BOR | P386_OP_BXOR
            | P386_OP_SHL | P386_OP_SHR | P386_OP_LSHR | P386_OP_ROTL | P386_OP_ROTR
            | P386_OP_EQ | P386_OP_NE | P386_OP_LT | P386_OP_LE | P386_OP_GT | P386_OP_GE
            | P386_OP_CONCAT => format!("R{}, {}, {}", a, rk(b), rk(c)),
            P386_OP_JMP => format!("-> {} (sBx={})", pc as isize + 1 + sbx as isize, sbx),
            P386_OP_JMPF | P386_OP_JMPT | P386_OP_FORPREP | P386_OP_FORLOOP | P386_OP_TFORLOOP =>
                format!("R{}, -> {} (sBx={})", a, pc as isize + 1 + sbx as isize, sbx),
            P386_OP_TFORCALL => format!("R{}, C={}", a, bx),
            P386_OP_CALL => format!("R{}, b={}, c={}", a, b, c),
            P386_OP_TAILCALL | P386_OP_RETURN => format!("R{}, b={}", a, b),
            _ => format!("a={} b={} c={}", a, b, c),
        };
        out.push_str(&format!("  {:4}  {:<10} {}\n", pc, opname(op), txt));
    }
    out.push('\n');
    for (i, sub) in p.prototypes.iter().enumerate() {
        let n = format!("{}.proto[{}]", name, i);
        disasm_proto(sub, &n, out);
    }
}

pub fn disasm(p: &FuncProto) -> String {
    let mut out = String::new();
    disasm_proto(p, "main", &mut out);
    out
}
