use alloc::vec::Vec;

use crate::ast::*;
use crate::bytecode::*;

const FIRST_TEMP: u8 = 32;
const MAX_REG: u8 = 126;

pub struct Compiler {
    names: NameTable,
}

#[derive(Clone, Copy)]
struct Local {
    name: Name,
    reg: u8,
}

impl Compiler {
    pub fn new(names: NameTable) -> Self {
        Self { names }
    }

    pub fn compile_chunk(&self, chunk: Chunk) -> Option<FuncProto> {
        let mut cg = CodeGen::new(self);
        cg.push_scope();
        cg.compile_stats(&chunk.body);
        cg.pop_scope();
        cg.emit(abc(P386_OP_RETURN, 0, 1, 0));
        cg.finish()
    }
}

struct CodeGen<'a> {
    compiler: &'a Compiler,
    n_params: u8,
    code: Vec<u32>,
    constants: Vec<Constant>,
    prototypes: Vec<FuncProto>,
    globals: Vec<Name>,
    locals: Vec<Local>,
    scopes: Vec<(usize, u8)>,
    next_local: u8,
    next_temp: u8,
    max_reg: u8,
    failed: bool,
}

impl<'a> CodeGen<'a> {
    fn new(compiler: &'a Compiler) -> Self {
        Self {
            compiler,
            n_params: 0,
            code: Vec::new(),
            constants: Vec::new(),
            prototypes: Vec::new(),
            globals: Vec::new(),
            locals: Vec::new(),
            scopes: Vec::new(),
            next_local: 0,
            next_temp: FIRST_TEMP,
            max_reg: 0,
            failed: false,
        }
    }

    fn finish(self) -> Option<FuncProto> {
        if self.failed { None } else { Some(FuncProto::new(self.code, self.constants, self.prototypes, self.max_reg.saturating_add(1).max(1), self.n_params)) }
    }

    fn emit(&mut self, ins: u32) -> usize {
        self.code.push(ins);
        self.code.len() - 1
    }

    fn checked_sbx(&mut self, delta: isize) -> i16 {
        if delta < i16::MIN as isize || delta > i16::MAX as isize {
            self.failed = true;
            0
        } else {
            delta as i16
        }
    }

    fn patch_sbx_delta(&mut self, at: usize, delta: isize) {
        let sbx = self.checked_sbx(delta);
        let old = self.code[at];
        let op = (old & 0xff) as u8;
        let a = ((old >> 8) & 0xff) as u8;
        self.code[at] = asbx(op, a, sbx);
    }

    fn emit_sbx_delta(&mut self, op: u8, a: u8, delta: isize) -> usize {
        let sbx = self.checked_sbx(delta);
        self.emit(asbx(op, a, sbx))
    }

    fn pc(&self) -> usize { self.code.len() }

    fn note_reg(&mut self, r: u8) {
        if r > MAX_REG { self.failed = true; }
        if r > self.max_reg { self.max_reg = r; }
    }

    fn checked_reg(&mut self, r: u8) -> u8 {
        if r > MAX_REG { self.failed = true; MAX_REG } else { self.note_reg(r); r }
    }

    fn temp(&mut self) -> u8 {
        let r = self.next_temp;
        if r >= MAX_REG { self.failed = true; return MAX_REG; }
        self.next_temp = self.next_temp.saturating_add(1);
        self.note_reg(r);
        r
    }

    fn free_to(&mut self, mark: u8) { self.next_temp = mark; }

    fn push_scope(&mut self) {
        self.scopes.push((self.locals.len(), self.next_local));
    }

    fn pop_scope(&mut self) {
        if let Some((len, next)) = self.scopes.pop() {
            self.locals.truncate(len);
            self.next_local = next;
        }
    }

    fn alloc_local(&mut self, name: Name) -> u8 {
        if self.next_local >= FIRST_TEMP { self.failed = true; return 0; }
        let reg = self.next_local;
        self.next_local = self.next_local.saturating_add(1);
        self.locals.push(Local { name, reg });
        self.note_reg(reg);
        reg
    }

    fn find_local(&self, name: Name) -> Option<u8> {
        self.locals.iter().rev().find(|l| l.name == name).map(|l| l.reg)
    }

    fn add_const(&mut self, c: Constant) -> u8 {
        if let Some(i) = self.constants.iter().position(|old| const_eq(old, &c)) {
            return i.min(127) as u8;
        }
        if self.constants.len() >= 128 {
            self.failed = true;
            return 0;
        }
        let idx = self.constants.len() as u8;
        self.constants.push(c);
        idx
    }

    fn name_bytes(&self, n: Name) -> &[u8] { self.compiler.names.resolve(n) }

    fn add_name_const(&mut self, n: Name) -> u8 {
        self.add_const(Constant::Str(self.name_bytes(n).to_vec()))
    }

    fn global_slot(&mut self, n: Name) -> u8 {
        match self.name_bytes(n) {
            b"print" => P386_BUILTIN_PRINT,
            b"cls" => P386_BUILTIN_CLS,
            b"pset" => P386_BUILTIN_PSET,
            b"pget" => P386_BUILTIN_PGET,
            b"line" => P386_BUILTIN_LINE,
            b"rect" => P386_BUILTIN_RECT,
            b"rectfill" | b"rectf" => P386_BUILTIN_RECTF,
            b"circfill" => P386_BUILTIN_CIRCFILL,
            b"spr" => P386_BUILTIN_SPR,
            b"map" => P386_BUILTIN_MAP,
            b"btn" => P386_BUILTIN_BTN,
            b"btnp" => P386_BUILTIN_BTNP,
            b"sfx" => P386_BUILTIN_SFX,
            b"music" => P386_BUILTIN_MUSIC,
            b"pairs" => P386_BUILTIN_PAIRS,
            b"ipairs" => P386_BUILTIN_IPAIRS,
            _ => {
                if let Some(i) = self.globals.iter().position(|&x| x == n) {
                    P386_USER_GLOBAL_BASE.saturating_add(i as u8)
                } else if self.globals.len() < (256 - P386_USER_GLOBAL_BASE as usize) {
                    let slot = P386_USER_GLOBAL_BASE.saturating_add(self.globals.len() as u8);
                    self.globals.push(n);
                    slot
                } else {
                    self.failed = true;
                    P386_USER_GLOBAL_BASE
                }
            }
        }
    }

    fn compile_stats(&mut self, stats: &[Stat]) {
        for stat in stats {
            self.compile_stat(stat);
        }
    }

    fn compile_stat(&mut self, stat: &Stat) {
        let mark = self.next_temp;
        match stat {
            Stat::Assign { targets, values } => self.compile_assign(targets, values),
            Stat::LocalAssign { names, values } => {
                let mut rhs = Vec::new();
                for i in 0..names.len() {
                    let r = if let Some(expr) = values.get(i) {
                        self.compile_expr(expr)
                    } else {
                        let r = self.temp();
                        self.emit(abc(P386_OP_LOADN, r, 1, 0));
                        r
                    };
                    rhs.push(r);
                }
                for (i, name) in names.iter().enumerate() {
                    let dst = self.alloc_local(*name);
                    self.emit(abc(P386_OP_MOVE, dst, rhs[i], 0));
                }
            }
            Stat::Call(c) => { self.compile_call(c, 0); }
            Stat::MethodCall(mc) => { self.compile_method_call(mc, 0); }
            Stat::ShortPrint(values) => self.compile_print(values),
            Stat::Do(body) => {
                self.push_scope();
                self.compile_stats(body);
                self.pop_scope();
            }
            Stat::If { conds, bodies, else_body } => self.compile_if(conds, bodies, else_body),
            Stat::ShortIf { cond, body, else_body } => {
                let c = self.compile_expr(cond);
                let jf = self.emit(asbx(P386_OP_JMPF, c, 0));
                self.push_scope();
                self.compile_stats(body);
                self.pop_scope();
                let jend = self.emit(asbx(P386_OP_JMP, 0, 0));
                let else_start = self.pc();
                self.patch_sbx_delta(jf, else_start as isize - jf as isize - 1);
                self.push_scope();
                self.compile_stats(else_body);
                self.pop_scope();
                let end = self.pc();
                self.patch_sbx_delta(jend, end as isize - jend as isize - 1);
            }
            Stat::While { cond, body } | Stat::ShortWhile { cond, body } => self.compile_while(cond, body),
            Stat::Repeat { body, cond } => self.compile_repeat(body, cond),
            Stat::Return(values) => {
                if values.is_empty() {
                    self.emit(abc(P386_OP_RETURN, 0, 1, 0));
                } else {
                    for (i, e) in values.iter().take(254).enumerate() {
                        self.compile_expr_into(e, i as u8);
                    }
                    self.emit(abc(P386_OP_RETURN, 0, values.len().saturating_add(1).min(255) as u8, 0));
                }
            }
            Stat::CompoundAssign { target, op, value, .. } => self.compile_compound(target, *op, value),
            Stat::FuncDef { name, params, body, .. } => {
                self.collect_func_name(name);
                let idx = self.add_function_proto(params, body);
                if let Some(first) = name.parts.first() {
                    let dst = self.temp();
                    self.emit(abx(P386_OP_CLOSURE, dst, idx));
                    let slot = self.global_slot(*first);
                    self.emit(abc(P386_OP_SETGLOBAL, dst, slot, 0));
                }
            }
            Stat::LocalFuncDef { name, params, body, .. } => {
                let dst = self.alloc_local(*name);
                let idx = self.add_function_proto(params, body);
                self.emit(abx(P386_OP_CLOSURE, dst, idx));
            }
            Stat::ForNum { var, start, stop, step, body } => self.compile_for_num(*var, start, stop, step.as_deref(), body),
            Stat::ForIn { vars, iters, body } => {
                for n in vars { self.add_name_const(*n); }
                for e in iters { self.compile_expr(e); }
                self.compile_stats(body);
            }
            Stat::Break | Stat::Label(_) | Stat::Goto(_) | Stat::Empty => {}
        }
        self.free_to(mark);
    }

    fn compile_assign(&mut self, targets: &[Var], values: &[Expr]) {
        let mut rhs = Vec::new();
        for i in 0..targets.len() {
            let r = if let Some(expr) = values.get(i) {
                self.compile_expr(expr)
            } else {
                let r = self.temp();
                self.emit(abc(P386_OP_LOADN, r, 1, 0));
                r
            };
            rhs.push(r);
        }
        for (target, r) in targets.iter().zip(rhs.into_iter()) {
            self.store_var(target, r);
        }
    }

    fn compile_compound(&mut self, target: &Var, op: CompoundOp, value: &Expr) {
        let lhs = self.load_var(target);
        let rhs = self.compile_expr(value);
        let dst = self.temp();
        if let Some(opc) = compound_opcode(op) {
            self.emit(abc(opc, dst, rk_reg(lhs), rk_reg(rhs)));
            self.store_var(target, dst);
        }
    }

    fn compile_if(&mut self, conds: &[Expr], bodies: &[Vec<Stat>], else_body: &[Stat]) {
        let mut end_jumps = Vec::new();
        for (i, cond) in conds.iter().enumerate() {
            let c = self.compile_expr(cond);
            let jf = self.emit(asbx(P386_OP_JMPF, c, 0));
            self.push_scope();
            self.compile_stats(bodies.get(i).map(|v| v.as_slice()).unwrap_or(&[]));
            self.pop_scope();
            end_jumps.push(self.emit(asbx(P386_OP_JMP, 0, 0)));
            let after_body = self.pc();
            self.patch_sbx_delta(jf, after_body as isize - jf as isize - 1);
        }
        self.push_scope();
        self.compile_stats(else_body);
        self.pop_scope();
        let end = self.pc();
        for j in end_jumps {
            self.patch_sbx_delta(j, end as isize - j as isize - 1);
        }
    }

    fn compile_while(&mut self, cond: &Expr, body: &[Stat]) {
        let start = self.pc();
        let c = self.compile_expr(cond);
        let jf = self.emit(asbx(P386_OP_JMPF, c, 0));
        self.push_scope();
        self.compile_stats(body);
        self.pop_scope();
        let back = start as isize - self.pc() as isize - 1;
        self.emit_sbx_delta(P386_OP_JMP, 0, back);
        let end = self.pc();
        self.patch_sbx_delta(jf, end as isize - jf as isize - 1);
    }

    fn compile_repeat(&mut self, body: &[Stat], cond: &Expr) {
        let start = self.pc();
        self.push_scope();
        self.compile_stats(body);
        self.pop_scope();
        let c = self.compile_expr(cond);
        let back = start as isize - self.pc() as isize - 1;
        self.emit_sbx_delta(P386_OP_JMPF, c, back);
    }

    fn compile_for_num(&mut self, var: Name, start: &Expr, stop: &Expr, step: Option<&Expr>, body: &[Stat]) {
        // Minimal lowering: local var=start; while var<=stop do body; var += step end.
        self.push_scope();
        let loop_reg = self.alloc_local(var);
        self.compile_expr_into(start, loop_reg);
        let stop_reg = self.temp();
        self.compile_expr_into(stop, stop_reg);
        let step_reg = self.temp();
        if let Some(step) = step {
            self.compile_expr_into(step, step_reg);
        } else {
            let one = self.add_const(Constant::Num(1 << 16));
            self.emit(abx(P386_OP_LOADK, step_reg, one as u16));
        }
        let loop_start = self.pc();
        let cr = self.temp();
        self.emit(abc(P386_OP_LE, cr, rk_reg(loop_reg), rk_reg(stop_reg)));
        let jf = self.emit(asbx(P386_OP_JMPF, cr, 0));
        self.compile_stats(body);
        self.emit(abc(P386_OP_ADD, loop_reg, rk_reg(loop_reg), rk_reg(step_reg)));
        let back = loop_start as isize - self.pc() as isize - 1;
        self.emit_sbx_delta(P386_OP_JMP, 0, back);
        let end = self.pc();
        self.patch_sbx_delta(jf, end as isize - jf as isize - 1);
        self.pop_scope();
    }

    fn compile_print(&mut self, values: &[Expr]) {
        let func = self.temp();
        self.emit(abc(P386_OP_GETGLOBAL, func, P386_BUILTIN_PRINT, 0));
        self.next_temp = func.saturating_add(1);
        for (i, e) in values.iter().take(253).enumerate() {
            let want = self.checked_reg(func.saturating_add(1).saturating_add(i as u8));
            self.next_temp = want.saturating_add(1);
            self.compile_expr_into(e, want);
            self.next_temp = want.saturating_add(1);
        }
        self.emit(abc(P386_OP_CALL, func, values.len().saturating_add(1).min(255) as u8, 1));
    }

    fn compile_call(&mut self, call: &CallExpr, want_rets: u8) -> u8 {
        let func = self.compile_expr(&call.func);
        self.next_temp = func.saturating_add(1);
        for (i, e) in call.args.iter().take(253).enumerate() {
            let want = self.checked_reg(func.saturating_add(1).saturating_add(i as u8));
            self.next_temp = want.saturating_add(1);
            self.compile_expr_into(e, want);
            self.next_temp = want.saturating_add(1);
        }
        self.emit(abc(P386_OP_CALL, func, call.args.len().saturating_add(1).min(255) as u8, want_rets.saturating_add(1)));
        func
    }

    fn compile_method_call(&mut self, call: &MethodCallExpr, want_rets: u8) -> u8 {
        let func = self.temp();
        let self_reg = self.checked_reg(func.saturating_add(1));
        self.next_temp = self_reg.saturating_add(1);
        self.compile_expr_into(&call.object, self_reg);
        let method = self.add_name_const(call.method);
        self.emit(abc(P386_OP_GETFIELD, func, self_reg, method));
        for (i, e) in call.args.iter().take(252).enumerate() {
            let want = self.checked_reg(func.saturating_add(2).saturating_add(i as u8));
            self.next_temp = want.saturating_add(1);
            self.compile_expr_into(e, want);
            self.next_temp = want.saturating_add(1);
        }
        let nargs = call.args.len().saturating_add(2).min(255) as u8;
        self.emit(abc(P386_OP_CALL, func, nargs, want_rets.saturating_add(1)));
        func
    }

    fn compile_exprs_contiguous(&mut self, values: &[Expr]) -> u8 {
        let first = self.temp();
        for (i, e) in values.iter().take(254).enumerate() {
            let dst = self.checked_reg(first.saturating_add(i as u8));
            self.next_temp = dst.saturating_add(1);
            self.compile_expr_into(e, dst);
            self.next_temp = dst.saturating_add(1);
        }
        first
    }

    fn compile_expr_into(&mut self, expr: &Expr, dst: u8) -> u8 {
        self.note_reg(dst);
        match expr {
            Expr::Nil => { self.emit(abc(P386_OP_LOADN, dst, 1, 0)); }
            Expr::True => { self.emit(abc(P386_OP_LOADT, dst, 0, 0)); }
            Expr::False => { self.emit(abc(P386_OP_LOADF, dst, 0, 0)); }
            Expr::Number(n) => {
                let k = self.add_const(Constant::Num(*n));
                self.emit(abx(P386_OP_LOADK, dst, k as u16));
            }
            Expr::Str(s) => {
                let k = self.add_const(Constant::Str(s.clone()));
                self.emit(abx(P386_OP_LOADK, dst, k as u16));
            }
            Expr::Var(v) => return self.load_var_into(v, dst),
            Expr::BinOp(op, a, b) => {
                if *op == BinOp::And {
                    let ar = self.compile_expr(a);
                    self.emit(abc(P386_OP_MOVE, dst, ar, 0));
                    let skip = self.emit(asbx(P386_OP_JMPF, dst, 0));
                    self.compile_expr_into(b, dst);
                    let end = self.pc();
                    self.patch_sbx_delta(skip, end as isize - skip as isize - 1);
                } else if *op == BinOp::Or {
                    let ar = self.compile_expr(a);
                    self.emit(abc(P386_OP_MOVE, dst, ar, 0));
                    let skip = self.emit(asbx(P386_OP_JMPT, dst, 0));
                    self.compile_expr_into(b, dst);
                    let end = self.pc();
                    self.patch_sbx_delta(skip, end as isize - skip as isize - 1);
                } else if let Some(opc) = bin_opcode(*op) {
                    let ar = self.compile_expr(a);
                    let br = self.compile_expr(b);
                    self.emit(abc(opc, dst, rk_reg(ar), rk_reg(br)));
                }
            }
            Expr::UnOp(op, e) => {
                let r = self.compile_expr(e);
                if let Some(opc) = un_opcode(*op) {
                    self.emit(abc(opc, dst, rk_reg(r), 0));
                } else {
                    self.emit(abc(P386_OP_MOVE, dst, r, 0));
                }
            }
            Expr::Call(c) => {
                let r = self.compile_call(c, 1);
                self.emit(abc(P386_OP_MOVE, dst, r, 0));
            }
            Expr::MethodCall(mc) => {
                let r = self.compile_method_call(mc, 1);
                self.emit(abc(P386_OP_MOVE, dst, r, 0));
            }
            Expr::Table(fields) => {
                self.emit(abc(P386_OP_NEWTABLE, dst, 0, 0));
                let mut array_idx: i32 = 1 << 16;
                for f in fields {
                    match f {
                        TableField::Positional(v) => {
                            let kr = self.load_const(Constant::Num(array_idx));
                            let vr = self.compile_expr(v);
                            self.emit(abc(P386_OP_SETTABLE, dst, rk_reg(kr), rk_reg(vr)));
                            array_idx += 1 << 16;
                        }
                        TableField::NamedField(n, v) => {
                            let k = self.add_name_const(*n);
                            let vr = self.compile_expr(v);
                            self.emit(abc(P386_OP_SETFIELD, dst, k, rk_reg(vr)));
                        }
                        TableField::IndexedField(k, v) => {
                            let kr = self.compile_expr(k);
                            let vr = self.compile_expr(v);
                            self.emit(abc(P386_OP_SETTABLE, dst, rk_reg(kr), rk_reg(vr)));
                        }
                    }
                }
            }
            Expr::Function { params, body, .. } => {
                let idx = self.add_function_proto(params, body);
                self.emit(abx(P386_OP_CLOSURE, dst, idx));
            }
            Expr::Varargs => { self.emit(abc(P386_OP_LOADN, dst, 1, 0)); }
        };
        dst
    }

    fn compile_expr(&mut self, expr: &Expr) -> u8 {
        let r = self.temp();
        self.compile_expr_into(expr, r)
    }

    fn load_const(&mut self, c: Constant) -> u8 {
        let r = self.temp();
        let k = self.add_const(c);
        self.emit(abx(P386_OP_LOADK, r, k as u16));
        r
    }

    fn load_var(&mut self, var: &Var) -> u8 {
        let r = self.temp();
        self.load_var_into(var, r)
    }

    fn load_var_into(&mut self, var: &Var, dst: u8) -> u8 {
        self.note_reg(dst);
        match var {
            Var::Name(n) => {
                if let Some(src) = self.find_local(*n) {
                    self.emit(abc(P386_OP_MOVE, dst, src, 0));
                } else {
                    let slot = self.global_slot(*n);
                    self.emit(abc(P386_OP_GETGLOBAL, dst, slot, 0));
                }
            }
            Var::Index(obj, key) => {
                let or = self.compile_expr(obj);
                let kr = self.compile_expr(key);
                self.emit(abc(P386_OP_GETTABLE, dst, rk_reg(or), rk_reg(kr)));
            }
            Var::Field(obj, n) => {
                let or = self.compile_expr(obj);
                let k = self.add_name_const(*n);
                self.emit(abc(P386_OP_GETFIELD, dst, or, k));
            }
        }
        dst
    }

    fn store_var(&mut self, var: &Var, src: u8) {
        match var {
            Var::Name(n) => {
                if let Some(dst) = self.find_local(*n) {
                    self.emit(abc(P386_OP_MOVE, dst, src, 0));
                } else {
                    let slot = self.global_slot(*n);
                    self.emit(abc(P386_OP_SETGLOBAL, src, slot, 0));
                }
            }
            Var::Index(obj, key) => {
                let or = self.compile_expr(obj);
                let kr = self.compile_expr(key);
                self.emit(abc(P386_OP_SETTABLE, or, rk_reg(kr), rk_reg(src)));
            }
            Var::Field(obj, n) => {
                let or = self.compile_expr(obj);
                let k = self.add_name_const(*n);
                self.emit(abc(P386_OP_SETFIELD, or, k, rk_reg(src)));
            }
        }
    }

    fn add_function_proto(&mut self, params: &[Name], body: &[Stat]) -> u16 {
        let idx = self.prototypes.len().saturating_add(1).min(65535) as u16;
        let mut child = CodeGen::new(self.compiler);
        child.n_params = params.len().min(255) as u8;
        child.push_scope();
        for n in params { child.alloc_local(*n); }
        child.compile_stats(body);
        child.pop_scope();
        child.emit(abc(P386_OP_RETURN, 0, 1, 0));
        if let Some(proto) = child.finish() {
            self.prototypes.push(proto);
        } else {
            self.failed = true;
        }
        idx
    }

    fn collect_func_name(&mut self, name: &FuncName) {
        for n in &name.parts { self.add_name_const(*n); }
        if let Some(n) = name.method { self.add_name_const(n); }
    }
}

fn bin_opcode(op: BinOp) -> Option<u8> {
    Some(match op {
        BinOp::Eq => P386_OP_EQ,
        BinOp::Ne => P386_OP_NE,
        BinOp::Lt => P386_OP_LT,
        BinOp::Le => P386_OP_LE,
        BinOp::Gt => P386_OP_GT,
        BinOp::Ge => P386_OP_GE,
        BinOp::Add => P386_OP_ADD,
        BinOp::Sub => P386_OP_SUB,
        BinOp::Mul => P386_OP_MUL,
        BinOp::Div => P386_OP_DIV,
        BinOp::IDiv => P386_OP_IDIV,
        BinOp::Mod => P386_OP_MOD,
        BinOp::Pow => P386_OP_POW,
        BinOp::BitAnd => P386_OP_BAND,
        BinOp::BitOr => P386_OP_BOR,
        BinOp::BitXor => P386_OP_BXOR,
        BinOp::Shl => P386_OP_SHL,
        BinOp::Shr => P386_OP_SHR,
        BinOp::LShr => P386_OP_LSHR,
        BinOp::RotL => P386_OP_ROTL,
        BinOp::RotR => P386_OP_ROTR,
        BinOp::Concat => P386_OP_CONCAT,
        BinOp::And | BinOp::Or => return None,
    })
}

fn un_opcode(op: UnOp) -> Option<u8> {
    Some(match op {
        UnOp::Neg => P386_OP_NEG,
        UnOp::Not => P386_OP_NOT,
        UnOp::Len => P386_OP_LEN,
        UnOp::BitNot => P386_OP_BNOT,
        UnOp::Peek => P386_OP_PEEK,
        UnOp::Peek2 => P386_OP_PEEK2,
        UnOp::Pct => return None,
    })
}

fn compound_opcode(op: CompoundOp) -> Option<u8> {
    Some(match op {
        CompoundOp::Add => P386_OP_ADD,
        CompoundOp::Sub => P386_OP_SUB,
        CompoundOp::Mul => P386_OP_MUL,
        CompoundOp::Div => P386_OP_DIV,
        CompoundOp::IDiv => P386_OP_IDIV,
        CompoundOp::Mod => P386_OP_MOD,
        CompoundOp::Pow => P386_OP_POW,
        CompoundOp::Concat => P386_OP_CONCAT,
        CompoundOp::BitAnd => P386_OP_BAND,
        CompoundOp::BitOr => P386_OP_BOR,
        CompoundOp::BitXor => P386_OP_BXOR,
        CompoundOp::Shl => P386_OP_SHL,
        CompoundOp::Shr => P386_OP_SHR,
        CompoundOp::LShr => P386_OP_LSHR,
        CompoundOp::RotL => P386_OP_ROTL,
        CompoundOp::RotR => P386_OP_ROTR,
    })
}

fn const_eq(a: &Constant, b: &Constant) -> bool {
    match (a, b) {
        (Constant::Nil, Constant::Nil) => true,
        (Constant::Bool(x), Constant::Bool(y)) => x == y,
        (Constant::Num(x), Constant::Num(y)) => x == y,
        (Constant::Str(x), Constant::Str(y)) => x.as_slice() == y.as_slice(),
        _ => false,
    }
}
