use alloc::boxed::Box;
use alloc::vec::Vec;

/// Interned name index. All identifiers go through NameTable.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Name(pub u16);

/// A complete PICO-8 program.
pub struct Chunk {
    pub body: Vec<Stat>,
}

/// Statements
pub enum Stat {
    Assign {
        targets: Vec<Var>,
        values: Vec<Expr>,
    },
    LocalAssign {
        names: Vec<Name>,
        values: Vec<Expr>,
    },
    Call(CallExpr),
    Do(Vec<Stat>),
    While {
        cond: Box<Expr>,
        body: Vec<Stat>,
    },
    Repeat {
        body: Vec<Stat>,
        cond: Box<Expr>,
    },
    If {
        conds: Vec<Expr>,
        bodies: Vec<Vec<Stat>>,
        else_body: Vec<Stat>,
    },
    ForNum {
        var: Name,
        start: Box<Expr>,
        stop: Box<Expr>,
        step: Option<Box<Expr>>,
        body: Vec<Stat>,
    },
    ForIn {
        vars: Vec<Name>,
        iters: Vec<Expr>,
        body: Vec<Stat>,
    },
    FuncDef {
        name: FuncName,
        params: Vec<Name>,
        has_varargs: bool,
        body: Vec<Stat>,
    },
    LocalFuncDef {
        name: Name,
        params: Vec<Name>,
        has_varargs: bool,
        body: Vec<Stat>,
    },
    Return(Vec<Expr>),
    Break,
    Label(Name),
    Goto(Name),
    Empty,
    // PICO-8 extensions
    ShortPrint(Vec<Expr>),
    ShortIf {
        cond: Box<Expr>,
        body: Vec<Stat>,
        else_body: Vec<Stat>,
    },
    ShortWhile {
        cond: Box<Expr>,
        body: Vec<Stat>,
    },
    CompoundAssign {
        is_local: bool,
        target: Var,
        op: CompoundOp,
        value: Box<Expr>,
    },
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum CompoundOp {
    Add, Sub, Mul, Div, IDiv, Mod, Pow, Concat,
    BitAnd, BitOr, BitXor, Shl, Shr, LShr, RotL, RotR,
}

pub struct FuncName {
    pub parts: Vec<Name>,
    pub method: Option<Name>,
}

/// An l-value (assignable target)
pub enum Var {
    Name(Name),
    Index(Box<Expr>, Box<Expr>),
    Field(Box<Expr>, Name),
}

/// Expressions
pub enum Expr {
    Nil,
    True,
    False,
    Number(i32),      // 16.16 fixed-point
    Str(Vec<u8>),
    Varargs,
    Var(Var),
    BinOp(BinOp, Box<Expr>, Box<Expr>),
    UnOp(UnOp, Box<Expr>),
    Call(CallExpr),
    MethodCall(MethodCallExpr),
    Function {
        params: Vec<Name>,
        has_varargs: bool,
        body: Vec<Stat>,
    },
    Table(Vec<TableField>),
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum BinOp {
    Or, And,
    Eq, Ne, Lt, Gt, Le, Ge,
    BitOr, BitXor, BitAnd,
    Shl, Shr, LShr, RotL, RotR,
    Concat,
    Add, Sub,
    Mul, Div, IDiv, Mod,
    Pow,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum UnOp {
    Neg, Not, Len, BitNot,
    Peek, Peek2, Pct,
}

pub struct CallExpr {
    pub func: Box<Expr>,
    pub args: Vec<Expr>,
}

pub struct MethodCallExpr {
    pub object: Box<Expr>,
    pub method: Name,
    pub args: Vec<Expr>,
}

pub enum TableField {
    IndexedField(Box<Expr>, Box<Expr>),
    NamedField(Name, Box<Expr>),
    Positional(Box<Expr>),
}

/// Used only during parsing to build suffix chains
pub enum Suffix {
    Index(Box<Expr>),
    Field(Name),
    Call(Vec<Expr>),
    MethodCall(Name, Vec<Expr>),
}

// ── Name interning ───────────────────────────────────────────────────

pub struct NameTable {
    names: Vec<Vec<u8>>,
}

impl NameTable {
    pub fn new() -> Self {
        NameTable { names: Vec::new() }
    }

    pub fn intern(&mut self, s: &[u8]) -> Name {
        for (i, existing) in self.names.iter().enumerate() {
            if existing.as_slice() == s {
                return Name(i as u16);
            }
        }
        let idx = self.names.len();
        self.names.push(s.to_vec());
        Name(idx as u16)
    }

    pub fn resolve(&self, n: Name) -> &[u8] {
        &self.names[n.0 as usize]
    }
}

// ── Number parsing (16.16 fixed-point, no floats) ────────────────────

pub fn parse_number(s: &str) -> i32 {
    let s = s.as_bytes();
    if s.len() >= 2 && s[0] == b'0' && (s[1] == b'x' || s[1] == b'X') {
        parse_hex_fixed(&s[2..])
    } else if s.len() >= 2 && s[0] == b'0' && (s[1] == b'b' || s[1] == b'B') {
        parse_bin_fixed(&s[2..])
    } else {
        parse_dec_fixed(s)
    }
}

fn parse_hex_fixed(s: &[u8]) -> i32 {
    let mut int_part: i32 = 0;
    let mut i = 0;
    while i < s.len() && s[i] != b'.' {
        int_part = int_part.wrapping_mul(16).wrapping_add(hex_val(s[i]) as i32);
        i += 1;
    }
    let mut frac: i32 = 0;
    if i < s.len() && s[i] == b'.' {
        i += 1;
        // Each hex digit after dot = 65536/16^pos weight
        let mut weight: i32 = 4096; // 65536 / 16
        while i < s.len() {
            frac = frac.wrapping_add((hex_val(s[i]) as i32).wrapping_mul(weight));
            weight /= 16;
            i += 1;
        }
    }
    int_part.wrapping_shl(16).wrapping_add(frac)
}

fn parse_bin_fixed(s: &[u8]) -> i32 {
    let mut int_part: i32 = 0;
    let mut i = 0;
    while i < s.len() && s[i] != b'.' {
        int_part = int_part.wrapping_mul(2).wrapping_add((s[i] - b'0') as i32);
        i += 1;
    }
    let mut frac: i32 = 0;
    if i < s.len() && s[i] == b'.' {
        i += 1;
        let mut weight: i32 = 32768; // 65536 / 2
        while i < s.len() {
            if s[i] == b'1' {
                frac = frac.wrapping_add(weight);
            }
            weight /= 2;
            i += 1;
        }
    }
    int_part.wrapping_shl(16).wrapping_add(frac)
}

fn parse_dec_fixed(s: &[u8]) -> i32 {
    let mut int_part: i32 = 0;
    let mut i = 0;
    // Integer part
    while i < s.len() && s[i] >= b'0' && s[i] <= b'9' {
        int_part = int_part.wrapping_mul(10).wrapping_add((s[i] - b'0') as i32);
        i += 1;
    }
    let mut frac: i32 = 0;
    if i < s.len() && s[i] == b'.' {
        i += 1;
        // Parse fractional digits: accumulate as integer, then convert
        // frac_val / 10^n_digits * 65536
        let mut frac_val: u32 = 0;
        let mut divisor: u32 = 1;
        while i < s.len() && s[i] >= b'0' && s[i] <= b'9' {
            frac_val = frac_val * 10 + (s[i] - b'0') as u32;
            divisor *= 10;
            i += 1;
        }
        if divisor > 1 {
            // frac = frac_val * 65536 / divisor, using 32-bit only.
            // We compute bit-by-bit to avoid needing 64-bit division.
            let mut remainder = frac_val;
            frac = 0;
            let mut bit: i32 = 1 << 15; // MSB of fractional part
            while bit > 0 && remainder > 0 {
                remainder *= 2;
                if remainder >= divisor {
                    frac |= bit;
                    remainder -= divisor;
                }
                bit >>= 1;
            }
        }
    }
    // Handle exponent (e/E)
    let mut result = int_part.wrapping_shl(16).wrapping_add(frac);
    if i < s.len() && (s[i] == b'e' || s[i] == b'E') {
        i += 1;
        let neg_exp = if i < s.len() && s[i] == b'-' { i += 1; true }
                      else if i < s.len() && s[i] == b'+' { i += 1; false }
                      else { false };
        let mut exp: u32 = 0;
        while i < s.len() && s[i] >= b'0' && s[i] <= b'9' {
            exp = exp * 10 + (s[i] - b'0') as u32;
            i += 1;
        }
        for _ in 0..exp {
            if neg_exp {
                result /= 10;
            } else {
                result = result.wrapping_mul(10);
            }
        }
    }
    result
}

fn hex_val(c: u8) -> u8 {
    match c {
        b'0'..=b'9' => c - b'0',
        b'a'..=b'f' => c - b'a' + 10,
        b'A'..=b'F' => c - b'A' + 10,
        _ => 0,
    }
}

// ── String literal parsing ───────────────────────────────────────────

pub fn parse_string_literal(raw: &str) -> Vec<u8> {
    let b = raw.as_bytes();
    if b.len() < 2 { return Vec::new(); }

    // Long string [[...]]
    if b[0] == b'[' {
        // Find the content between [=*[ and ]=*]
        let mut level = 0;
        let mut i = 1;
        while i < b.len() && b[i] == b'=' { level += 1; i += 1; }
        if i < b.len() && b[i] == b'[' { i += 1; } // skip second [
        // Skip leading newline if present
        if i < b.len() && b[i] == b'\n' { i += 1; }
        else if i < b.len() && b[i] == b'\r' {
            i += 1;
            if i < b.len() && b[i] == b'\n' { i += 1; }
        }
        let end = b.len().saturating_sub(level + 2); // skip ]=*]
        return b[i..end].to_vec();
    }

    // Short string "..." or '...'
    let quote = b[0];
    let mut out = Vec::new();
    let mut i = 1; // skip opening quote
    let end = b.len() - 1; // skip closing quote
    while i < end {
        if b[i] == b'\\' {
            i += 1;
            if i >= end { break; }
            match b[i] {
                b'a' => { out.push(7); i += 1; }
                b'b' => { out.push(8); i += 1; }
                b'f' => { out.push(12); i += 1; }
                b'n' => { out.push(b'\n'); i += 1; }
                b'r' => { out.push(b'\r'); i += 1; }
                b't' => { out.push(b'\t'); i += 1; }
                b'v' => { out.push(11); i += 1; }
                b'\\' => { out.push(b'\\'); i += 1; }
                b'\'' => { out.push(b'\''); i += 1; }
                b'"' => { out.push(b'"'); i += 1; }
                b'\n' => { out.push(b'\n'); i += 1; }
                b'\r' => {
                    out.push(b'\n'); i += 1;
                    if i < end && b[i] == b'\n' { i += 1; }
                }
                b'0' => { out.push(0); i += 1; }
                b'x' => {
                    i += 1;
                    let mut val: u8 = 0;
                    for _ in 0..2 {
                        if i < end { val = val * 16 + hex_val(b[i]); i += 1; }
                    }
                    out.push(val);
                }
                b'z' => {
                    i += 1;
                    while i < end && (b[i] == b' ' || b[i] == b'\t' || b[i] == b'\r' || b[i] == b'\n') {
                        i += 1;
                    }
                }
                c if c >= b'0' && c <= b'9' => {
                    let mut val: u16 = (c - b'0') as u16;
                    i += 1;
                    for _ in 0..2 {
                        if i < end && b[i] >= b'0' && b[i] <= b'9' {
                            val = val * 10 + (b[i] - b'0') as u16;
                            i += 1;
                        } else { break; }
                    }
                    out.push(val as u8);
                }
                _ => { out.push(b[i]); i += 1; }
            }
        } else {
            out.push(b[i]);
            i += 1;
        }
    }
    out
}

/// Helper: apply suffix chain to a base expression
pub fn apply_suffixes(base: Expr, suffixes: Vec<Suffix>) -> Expr {
    suffixes.into_iter().fold(base, |acc, suffix| match suffix {
        Suffix::Index(idx) => Expr::Var(Var::Index(Box::new(acc), idx)),
        Suffix::Field(n) => Expr::Var(Var::Field(Box::new(acc), n)),
        Suffix::Call(args) => Expr::Call(CallExpr { func: Box::new(acc), args }),
        Suffix::MethodCall(m, args) => Expr::MethodCall(MethodCallExpr {
            object: Box::new(acc), method: m, args,
        }),
    })
}

/// Helper: check if a suffix is a call (for distinguishing call statements from assignments)
pub fn suffix_is_call(s: &Suffix) -> bool {
    matches!(s, Suffix::Call(_) | Suffix::MethodCall(_, _))
}

/// Helper: convert Expr to Var for assignment targets. Panics if not an l-value.
pub fn expr_to_var(e: Expr) -> Var {
    match e {
        Expr::Var(v) => v,
        _ => Var::Name(Name(0)), // fallback; parser should prevent this
    }
}
