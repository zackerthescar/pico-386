use alloc::vec::Vec;
use alloc::vec;

use crate::ast::*;
use crate::bytecode::*;

const MAX_REG: u8 = 250;

#[derive(Clone, Copy)]
struct Local {
    name: Name,
    reg: u8,
}

#[derive(Clone, Copy)]
struct ScopeMark {
    locals_len: usize,
    nactive: u8,
}

struct LoopCtx {
    breaks: Vec<usize>,
    scope_base: u8,
}

/// Per-function compilation state. Functions form a stack so that nested
/// function literals can resolve upvalues against their enclosing functions.
struct FuncState {
    code: Vec<u32>,
    constants: Vec<Constant>,
    prototypes: Vec<FuncProto>,
    /// (source, index): source 0 = parent local register, 1 = parent upvalue.
    upvalues: Vec<(u8, u8)>,
    upvalue_names: Vec<Name>,
    locals: Vec<Local>,
    scopes: Vec<ScopeMark>,
    loops: Vec<LoopCtx>,
    /// label name -> pc of the label
    labels: Vec<(Name, usize)>,
    /// (pc of JMP, label name) pending goto resolution
    gotos: Vec<(usize, Name)>,
    /// registers captured by inner closures (need CLOSE on scope exit)
    captured: Vec<u8>,
    nactive: u8,
    freereg: u8,
    n_params: u8,
    max_reg: u8,
    has_vararg: bool,
    failed: bool,
}

impl FuncState {
    fn new(n_params: u8) -> Self {
        Self {
            code: Vec::new(),
            constants: Vec::new(),
            prototypes: Vec::new(),
            upvalues: Vec::new(),
            upvalue_names: Vec::new(),
            locals: Vec::new(),
            scopes: Vec::new(),
            loops: Vec::new(),
            labels: Vec::new(),
            gotos: Vec::new(),
            captured: Vec::new(),
            nactive: 0,
            freereg: 0,
            n_params: 0,
            max_reg: 0,
            has_vararg: false,
            failed: false,
        }
        .with_params(n_params)
    }
    fn with_params(mut self, n_params: u8) -> Self {
        self.n_params = n_params;
        self
    }
}

pub struct Compiler {
    names: NameTable,
    /// Program-wide user global slots, shared across every function.
    globals: Vec<Name>,
    funcs: Vec<FuncState>,
}

impl Compiler {
    pub fn new(names: NameTable) -> Self {
        Self { names, globals: Vec::new(), funcs: Vec::new() }
    }

    pub fn compile_chunk(mut self, chunk: Chunk) -> Option<FuncProto> {
        // Main chunk is vararg in real Lua, but we have no VARARG opcode.
        self.funcs.push(FuncState::new(0));
        self.fs_mut().has_vararg = true;
        self.push_scope();
        self.compile_stats(&chunk.body);
        self.close_scope_upvalues(0);
        self.pop_scope();
        self.resolve_gotos();
        self.emit(abc(P386_OP_RETURN, 0, 1, 0));
        let fs = self.funcs.pop().unwrap();
        if fs.failed {
            return None;
        }
        Some(FuncProto::new(
            fs.code,
            fs.constants,
            fs.prototypes,
            fs.upvalues,
            fs.max_reg.saturating_add(1).max(1),
            fs.n_params,
        ))
    }

    // ── current function accessors ──
    fn fs(&self) -> &FuncState {
        self.funcs.last().unwrap()
    }
    fn fs_mut(&mut self) -> &mut FuncState {
        self.funcs.last_mut().unwrap()
    }

    fn fail(&mut self) {
        self.fs_mut().failed = true;
    }

    // ── register / emit helpers ──
    fn emit(&mut self, ins: u32) -> usize {
        let fs = self.fs_mut();
        fs.code.push(ins);
        fs.code.len() - 1
    }

    fn pc(&self) -> usize {
        self.fs().code.len()
    }

    fn note_reg(&mut self, r: u8) {
        let fs = self.fs_mut();
        if r > MAX_REG {
            fs.failed = true;
        }
        if r > fs.max_reg {
            fs.max_reg = r;
        }
    }

    fn reserve(&mut self) -> u8 {
        let r = self.fs().freereg;
        if r >= MAX_REG {
            self.fail();
            return MAX_REG;
        }
        self.fs_mut().freereg = r + 1;
        self.note_reg(r);
        r
    }

    fn freereg(&self) -> u8 {
        self.fs().freereg
    }

    fn set_freereg(&mut self, v: u8) {
        self.fs_mut().freereg = v;
    }

    fn checked_sbx(&mut self, delta: isize) -> i16 {
        if delta < i16::MIN as isize || delta > i16::MAX as isize {
            self.fail();
            0
        } else {
            delta as i16
        }
    }

    fn patch_jump_to_here(&mut self, at: usize) {
        let here = self.pc();
        self.patch_jump(at, here);
    }

    fn patch_jump(&mut self, at: usize, target: usize) {
        let delta = target as isize - at as isize - 1;
        let sbx = self.checked_sbx(delta);
        let old = self.fs().code[at];
        let op = (old & 0xff) as u8;
        let a = ((old >> 8) & 0xff) as u8;
        self.fs_mut().code[at] = asbx(op, a, sbx);
    }

    fn emit_jump_back(&mut self, op: u8, a: u8, target: usize) {
        let at = self.pc();
        let delta = target as isize - at as isize - 1;
        let sbx = self.checked_sbx(delta);
        self.emit(asbx(op, a, sbx));
    }

    // ── scopes ──
    fn push_scope(&mut self) {
        let fs = self.fs_mut();
        let m = ScopeMark { locals_len: fs.locals.len(), nactive: fs.nactive };
        fs.scopes.push(m);
    }

    /// Emit CLOSE if any local at or above `base` was captured by a closure.
    fn close_scope_upvalues(&mut self, base: u8) {
        let needs = self.fs().captured.iter().any(|&r| r >= base);
        if needs {
            self.emit(abc(P386_OP_CLOSE, base, 0, 0));
            self.fs_mut().captured.retain(|&r| r < base);
        }
    }

    fn pop_scope(&mut self) {
        if let Some(m) = self.fs().scopes.last().copied() {
            self.close_scope_upvalues(m.nactive);
            let fs = self.fs_mut();
            fs.scopes.pop();
            fs.locals.truncate(m.locals_len);
            fs.nactive = m.nactive;
            fs.freereg = m.nactive;
        }
    }

    /// Register a fresh local that occupies register `reg` (already allocated).
    fn register_local_at(&mut self, name: Name, reg: u8) {
        self.note_reg(reg);
        let fs = self.fs_mut();
        fs.locals.push(Local { name, reg });
        if reg >= fs.nactive {
            fs.nactive = reg + 1;
        }
        if fs.freereg < fs.nactive {
            fs.freereg = fs.nactive;
        }
    }

    fn find_local(&self, name: Name) -> Option<u8> {
        self.fs().locals.iter().rev().find(|l| l.name == name).map(|l| l.reg)
    }

    fn find_local_in(&self, fidx: usize, name: Name) -> Option<u8> {
        self.funcs[fidx].locals.iter().rev().find(|l| l.name == name).map(|l| l.reg)
    }

    // ── upvalue resolution ──
    fn resolve_upvalue(&mut self, fidx: usize, name: Name) -> Option<u8> {
        if fidx == 0 {
            return None;
        }
        // already captured?
        if let Some(i) = self.funcs[fidx].upvalue_names.iter().position(|&n| n == name) {
            return Some(i as u8);
        }
        // parent local?
        if let Some(reg) = self.find_local_in(fidx - 1, name) {
            self.funcs[fidx - 1].captured.push(reg);
            return Some(self.add_upvalue(fidx, name, 0, reg));
        }
        // parent upvalue (recursive)?
        if let Some(puv) = self.resolve_upvalue(fidx - 1, name) {
            return Some(self.add_upvalue(fidx, name, 1, puv));
        }
        None
    }

    fn add_upvalue(&mut self, fidx: usize, name: Name, source: u8, index: u8) -> u8 {
        let f = &mut self.funcs[fidx];
        let idx = f.upvalues.len();
        if idx >= 250 {
            f.failed = true;
            return 0;
        }
        f.upvalues.push((source, index));
        f.upvalue_names.push(name);
        idx as u8
    }

    // ── constants ──
    fn add_const(&mut self, c: Constant) -> u8 {
        let fs = self.fs_mut();
        if let Some(i) = fs.constants.iter().position(|old| const_eq(old, &c)) {
            if i < 128 {
                return i as u8;
            }
        }
        if fs.constants.len() >= 128 {
            fs.failed = true;
            return 0;
        }
        let idx = fs.constants.len() as u8;
        fs.constants.push(c);
        idx
    }

    fn name_bytes(&self, n: Name) -> &[u8] {
        self.names.resolve(n)
    }

    fn add_name_const(&mut self, n: Name) -> u8 {
        let bytes = self.name_bytes(n).to_vec();
        self.add_const(Constant::Str(bytes))
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
            b"_init" => P386_GLOBAL_INIT,
            b"_update" => P386_GLOBAL_UPDATE,
            b"_update60" => P386_GLOBAL_UPDATE60,
            b"_draw" => P386_GLOBAL_DRAW,
            _ => {
                if let Some(i) = self.globals.iter().position(|&x| x == n) {
                    P386_USER_GLOBAL_BASE.saturating_add(i as u8)
                } else if self.globals.len() < (256 - P386_USER_GLOBAL_BASE as usize) {
                    let slot = P386_USER_GLOBAL_BASE.saturating_add(self.globals.len() as u8);
                    self.globals.push(n);
                    slot
                } else {
                    self.fail();
                    P386_USER_GLOBAL_BASE
                }
            }
        }
    }

    // ── statements ──
    fn compile_stats(&mut self, stats: &[Stat]) {
        for stat in stats {
            self.compile_stat(stat);
        }
    }

    fn compile_stat(&mut self, stat: &Stat) {
        let mark = self.fs().nactive;
        match stat {
            Stat::Assign { targets, values } => self.compile_assign(targets, values),
            Stat::LocalAssign { names, values } => self.compile_local_assign(names, values),
            Stat::Call(c) => {
                let base = self.freereg();
                self.compile_call(c, 0, base);
            }
            Stat::MethodCall(mc) => {
                let base = self.freereg();
                self.compile_method_call(mc, 0, base);
            }
            Stat::ShortPrint(values) => self.compile_print(values),
            Stat::Do(body) => {
                self.push_scope();
                self.compile_stats(body);
                self.pop_scope();
            }
            Stat::If { conds, bodies, else_body } => self.compile_if(conds, bodies, else_body),
            Stat::ShortIf { cond, body, else_body } => {
                self.compile_if(
                    core::slice::from_ref(cond.as_ref()),
                    core::slice::from_ref(body),
                    else_body,
                );
            }
            Stat::While { cond, body } | Stat::ShortWhile { cond, body } => {
                self.compile_while(cond, body)
            }
            Stat::Repeat { body, cond } => self.compile_repeat(body, cond),
            Stat::Return(values) => self.compile_return(values),
            Stat::CompoundAssign { target, op, value, .. } => {
                self.compile_compound(target, *op, value)
            }
            Stat::FuncDef { name, params, has_varargs, body } => {
                self.compile_func_def(name, params, *has_varargs, body)
            }
            Stat::LocalFuncDef { name, params, has_varargs, body } => {
                // Declare the local first so the function body can recurse.
                let reg = self.reserve();
                self.register_local_at(*name, reg);
                let idx = self.add_function_proto(params, *has_varargs, body, false);
                self.emit(abx(P386_OP_CLOSURE, reg, idx));
            }
            Stat::ForNum { var, start, stop, step, body } => {
                self.compile_for_num(*var, start, stop, step.as_deref(), body)
            }
            Stat::ForIn { vars, iters, body } => self.compile_for_in(vars, iters, body),
            Stat::Break => self.compile_break(),
            Stat::Label(n) => {
                let pc = self.pc();
                self.fs_mut().labels.push((*n, pc));
            }
            Stat::Goto(n) => {
                let at = self.emit(asbx(P386_OP_JMP, 0, 0));
                self.fs_mut().gotos.push((at, *n));
            }
            Stat::Empty => {}
        }
        // free temps allocated by this statement
        let nactive = self.fs().nactive.max(mark);
        self.set_freereg(nactive);
    }

    fn compile_break(&mut self) {
        let (base, at);
        {
            if self.fs().loops.is_empty() {
                return;
            }
            base = self.fs().loops.last().unwrap().scope_base;
        }
        self.close_scope_upvalues(base);
        at = self.emit(asbx(P386_OP_JMP, 0, 0));
        self.fs_mut().loops.last_mut().unwrap().breaks.push(at);
    }

    fn resolve_gotos(&mut self) {
        let gotos = core::mem::take(&mut self.fs_mut().gotos);
        for (at, name) in gotos {
            if let Some(&(_, pc)) = self.fs().labels.iter().find(|(n, _)| *n == name) {
                self.patch_jump(at, pc);
            }
            // unknown label: leave as no-op jump (sBx already 0)
        }
    }

    fn compile_local_assign(&mut self, names: &[Name], values: &[Expr]) {
        let base = self.freereg();
        self.compile_expr_list(values, base, names.len() as u8);
        // The values now sit at base..base+n-1; bind them as locals.
        for (i, name) in names.iter().enumerate() {
            let reg = base + i as u8;
            self.register_local_at(*name, reg);
        }
    }

    fn compile_assign(&mut self, targets: &[Var], values: &[Expr]) {
        if targets.len() == 1 && values.len() == 1 {
            // Fast path: single assignment.
            self.compile_assign_single(&targets[0], &values[0]);
            return;
        }
        let base = self.freereg();
        self.compile_expr_list(values, base, targets.len() as u8);
        for (i, target) in targets.iter().enumerate() {
            let r = base + i as u8;
            self.store_var(target, r);
        }
    }

    fn compile_assign_single(&mut self, target: &Var, value: &Expr) {
        match target {
            Var::Name(n) => {
                if let Some(reg) = self.find_local(*n) {
                    self.compile_expr_into(value, reg);
                } else if let Some(uv) = self.resolve_upvalue(self.funcs.len() - 1, *n) {
                    let r = self.expr_to_anyreg(value);
                    self.emit(abc(P386_OP_SETUPVAL, r, uv, 0));
                } else {
                    let r = self.expr_to_anyreg(value);
                    let slot = self.global_slot(*n);
                    self.emit(abc(P386_OP_SETGLOBAL, r, slot, 0));
                }
            }
            _ => {
                let r = self.expr_to_anyreg(value);
                self.store_var(target, r);
            }
        }
    }

    fn compile_compound(&mut self, target: &Var, op: CompoundOp, value: &Expr) {
        let opc = match compound_opcode(op) {
            Some(o) => o,
            None => return,
        };
        // result = target op value; then store back into target.
        match target {
            Var::Name(n) if self.find_local(*n).is_some() => {
                let reg = self.find_local(*n).unwrap();
                let rhs = self.expr_to_rk(value);
                self.emit(abc(opc, reg, rk_reg(reg), rhs));
            }
            _ => {
                let lhs = self.expr_to_anyreg(&var_as_expr(target));
                let dst = self.reserve();
                let rhs = self.expr_to_rk(value);
                self.emit(abc(opc, dst, rk_reg(lhs), rhs));
                self.store_var(target, dst);
            }
        }
    }

    fn compile_if(&mut self, conds: &[Expr], bodies: &[Vec<Stat>], else_body: &[Stat]) {
        let mut end_jumps = Vec::new();
        for (i, cond) in conds.iter().enumerate() {
            let base = self.freereg();
            let c = self.expr_to_anyreg(cond);
            self.set_freereg(base);
            let jf = self.emit(asbx(P386_OP_JMPF, c, 0));
            self.push_scope();
            self.compile_stats(bodies.get(i).map(|v| v.as_slice()).unwrap_or(&[]));
            self.pop_scope();
            let has_more = i + 1 < conds.len() || !else_body.is_empty();
            if has_more {
                end_jumps.push(self.emit(asbx(P386_OP_JMP, 0, 0)));
            }
            self.patch_jump_to_here(jf);
        }
        self.push_scope();
        self.compile_stats(else_body);
        self.pop_scope();
        for j in end_jumps {
            self.patch_jump_to_here(j);
        }
    }

    fn compile_while(&mut self, cond: &Expr, body: &[Stat]) {
        let start = self.pc();
        let base = self.freereg();
        let c = self.expr_to_anyreg(cond);
        self.set_freereg(base);
        let jf = self.emit(asbx(P386_OP_JMPF, c, 0));
        self.push_scope();
        self.fs_mut().loops.push(LoopCtx { breaks: Vec::new(), scope_base: base });
        self.compile_stats(body);
        let loopctx = self.fs_mut().loops.pop().unwrap();
        self.pop_scope();
        self.emit_jump_back(P386_OP_JMP, 0, start);
        self.patch_jump_to_here(jf);
        for b in loopctx.breaks {
            self.patch_jump_to_here(b);
        }
    }

    fn compile_repeat(&mut self, body: &[Stat], cond: &Expr) {
        let start = self.pc();
        let base = self.freereg();
        self.push_scope();
        self.fs_mut().loops.push(LoopCtx { breaks: Vec::new(), scope_base: base });
        self.compile_stats(body);
        // condition can see the body's locals
        let c = self.expr_to_anyreg(cond);
        self.emit_jump_back(P386_OP_JMPF, c, start);
        let loopctx = self.fs_mut().loops.pop().unwrap();
        self.pop_scope();
        for b in loopctx.breaks {
            self.patch_jump_to_here(b);
        }
    }

    fn compile_for_num(
        &mut self,
        var: Name,
        start: &Expr,
        stop: &Expr,
        step: Option<&Expr>,
        body: &[Stat],
    ) {
        self.push_scope();
        let base = self.freereg();
        // R[base]=idx, R[base+1]=limit, R[base+2]=step, R[base+3]=visible var
        self.compile_expr_into(start, base);
        self.set_freereg(base + 1);
        self.compile_expr_into(stop, base + 1);
        self.set_freereg(base + 2);
        if let Some(step) = step {
            self.compile_expr_into(step, base + 2);
        } else {
            let k = self.add_const(Constant::Num(1 << 16));
            self.emit(abx(P386_OP_LOADK, base + 2, k as u16));
        }
        self.set_freereg(base + 3);
        self.note_reg(base + 3);
        self.fs_mut().freereg = base + 4;
        // visible loop variable is R[base+3]
        let forprep = self.emit(asbx(P386_OP_FORPREP, base, 0));
        let body_start = self.pc();
        // register the visible variable as a local for the body
        self.push_scope();
        self.register_local_at(var, base + 3);
        self.fs_mut().loops.push(LoopCtx { breaks: Vec::new(), scope_base: base });
        self.compile_stats(body);
        let loopctx = self.fs_mut().loops.pop().unwrap();
        self.pop_scope();
        // FORLOOP jumps back to body_start
        let forloop = self.emit(asbx(P386_OP_FORLOOP, base, 0));
        self.patch_jump(forloop, body_start);
        // FORPREP jumps to the FORLOOP instruction
        self.patch_jump(forprep, forloop);
        for b in loopctx.breaks {
            self.patch_jump_to_here(b);
        }
        self.pop_scope();
    }

    fn compile_for_in(&mut self, vars: &[Name], iters: &[Expr], body: &[Stat]) {
        self.push_scope();
        let base = self.freereg();
        // R[base]=iterator fn, R[base+1]=state, R[base+2]=control,
        // R[base+3..]=loop vars
        self.compile_expr_list(iters, base, 3);
        self.fs_mut().freereg = base + 3;
        let nvars = vars.len().max(1) as u8;
        // reserve the visible loop variable registers
        for _ in 0..nvars {
            self.reserve();
        }
        // jump to the TFORCALL at the bottom
        let entry_jump = self.emit(asbx(P386_OP_JMP, 0, 0));
        let body_start = self.pc();
        self.push_scope();
        for (i, v) in vars.iter().enumerate() {
            self.register_local_at(*v, base + 3 + i as u8);
        }
        self.fs_mut().loops.push(LoopCtx { breaks: Vec::new(), scope_base: base });
        self.compile_stats(body);
        let loopctx = self.fs_mut().loops.pop().unwrap();
        self.pop_scope();
        self.patch_jump_to_here(entry_jump);
        self.emit(abc(P386_OP_TFORCALL, base, nvars, 0));
        let tforloop = self.emit(asbx(P386_OP_TFORLOOP, base, 0));
        self.patch_jump(tforloop, body_start);
        for b in loopctx.breaks {
            self.patch_jump_to_here(b);
        }
        self.pop_scope();
    }

    fn compile_return(&mut self, values: &[Expr]) {
        if values.is_empty() {
            self.emit(abc(P386_OP_RETURN, 0, 1, 0));
            return;
        }
        // Tail call: `return f(args)` / `return o:m(args)`. The callee reuses
        // this frame and returns into R0 by the same convention, so the
        // trailing RETURN is unreachable but emitted for the VM's contract.
        if values.len() == 1 {
            if let Expr::Call(c) = &values[0] {
                let base = self.freereg();
                self.compile_call_raw(c, base, true);
                self.emit(abc(P386_OP_RETURN, base, 1, 0));
                return;
            }
            if let Expr::MethodCall(mc) = &values[0] {
                let base = self.freereg();
                self.compile_method_call_raw(mc, base, true);
                self.emit(abc(P386_OP_RETURN, base, 1, 0));
                return;
            }
        }
        // Evaluate the return values into a fresh contiguous block, then shift
        // them down to R0.. so the return base is always register 0. This
        // matches the VM's halt convention (return values observable at the
        // frame base) and keeps nested-call relocation correct.
        let base = self.freereg();
        let n = values.len().min(250) as u8;
        self.compile_expr_list(values, base, n);
        if base != 0 {
            for i in 0..n {
                self.emit(abc(P386_OP_MOVE, i, base + i, 0));
            }
        }
        self.emit(abc(P386_OP_RETURN, 0, n.saturating_add(1), 0));
    }

    fn compile_func_def(
        &mut self,
        name: &FuncName,
        params: &[Name],
        has_varargs: bool,
        body: &[Stat],
    ) {
        let is_method = name.method.is_some();
        let idx = self.add_function_proto(params, has_varargs, body, is_method);
        let dst = self.reserve();
        self.emit(abx(P386_OP_CLOSURE, dst, idx));
        // Build the storage target from the dotted/method name.
        if name.parts.len() == 1 && name.method.is_none() {
            self.store_var(&Var::Name(name.parts[0]), dst);
        } else {
            // base.parts[1..].method = closure
            let mut obj = self.load_name_to_anyreg(name.parts[0]);
            let last_field;
            let fields: Vec<Name> = name.parts[1..].iter().copied().collect();
            if let Some(m) = name.method {
                // all parts are fields, method is final field
                for f in &fields {
                    let k = self.add_name_const(*f);
                    let next = self.reserve();
                    self.emit(abc(P386_OP_GETFIELD, next, obj, k));
                    obj = next;
                }
                last_field = m;
            } else {
                // last part is the field we store into
                let (init, lastp) = fields.split_at(fields.len() - 1);
                for f in init {
                    let k = self.add_name_const(*f);
                    let next = self.reserve();
                    self.emit(abc(P386_OP_GETFIELD, next, obj, k));
                    obj = next;
                }
                last_field = lastp[0];
            }
            let k = self.add_name_const(last_field);
            self.emit(abc(P386_OP_SETFIELD, obj, k, rk_reg(dst)));
        }
    }

    fn add_function_proto(
        &mut self,
        params: &[Name],
        has_varargs: bool,
        body: &[Stat],
        is_method: bool,
    ) -> u16 {
        let idx = self.fs().prototypes.len().saturating_add(1).min(65535) as u16;
        let nparams = (params.len() + if is_method { 1 } else { 0 }).min(250) as u8;
        self.funcs.push(FuncState::new(nparams));
        self.fs_mut().has_vararg = has_varargs;
        self.push_scope();
        // implicit self for methods
        if is_method {
            let self_name = self.intern(b"self");
            let r = self.reserve();
            self.register_local_at(self_name, r);
        }
        for p in params {
            let r = self.reserve();
            self.register_local_at(*p, r);
        }
        self.compile_stats(body);
        self.close_scope_upvalues(0);
        self.pop_scope();
        self.resolve_gotos();
        self.emit(abc(P386_OP_RETURN, 0, 1, 0));
        let fs = self.funcs.pop().unwrap();
        let failed = fs.failed;
        let proto = FuncProto::new(
            fs.code,
            fs.constants,
            fs.prototypes,
            fs.upvalues,
            fs.max_reg.saturating_add(1).max(1),
            fs.n_params,
        );
        let parent = self.fs_mut();
        if failed {
            parent.failed = true;
        }
        parent.prototypes.push(proto);
        idx
    }

    fn intern(&mut self, s: &[u8]) -> Name {
        self.names.intern(s)
    }

    fn compile_print(&mut self, values: &[Expr]) {
        let base = self.freereg();
        let func = self.reserve();
        self.emit(abc(P386_OP_GETGLOBAL, func, P386_BUILTIN_PRINT, 0));
        let n = values.len().min(248) as u8;
        for (i, e) in values.iter().take(248).enumerate() {
            let want = base + 1 + i as u8;
            self.fs_mut().freereg = want;
            self.compile_expr_into(e, want);
            self.fs_mut().freereg = want + 1;
        }
        self.emit(abc(P386_OP_CALL, func, n.saturating_add(1), 1));
        self.set_freereg(base);
    }

    // ── calls ──
    /// Compile a call placing the function at `base`. want_rets results land
    /// at base..base+want_rets-1. Returns base.
    fn compile_call(&mut self, call: &CallExpr, want_rets: u8, base: u8) -> u8 {
        self.compile_call_inner(call, want_rets, base, false)
    }

    fn compile_call_raw(&mut self, call: &CallExpr, base: u8, tail: bool) -> u8 {
        self.compile_call_inner(call, 1, base, tail)
    }

    fn compile_call_inner(&mut self, call: &CallExpr, want_rets: u8, base: u8, tail: bool) -> u8 {
        self.fs_mut().freereg = base;
        let func = self.reserve();
        debug_assert_eq!(func, base);
        self.compile_expr_into(&call.func, func);
        let nargs = self.compile_args(&call.args, func + 1);
        if tail {
            self.emit(abc(P386_OP_TAILCALL, func, nargs.saturating_add(1), 0));
        } else {
            self.emit(abc(P386_OP_CALL, func, nargs.saturating_add(1), want_rets.saturating_add(1)));
        }
        if want_rets > 0 {
            self.note_reg(base + want_rets - 1);
        }
        self.set_freereg(base + want_rets.max(1));
        base
    }

    fn compile_method_call(&mut self, call: &MethodCallExpr, want_rets: u8, base: u8) -> u8 {
        self.compile_method_call_inner(call, want_rets, base, false)
    }

    fn compile_method_call_raw(&mut self, call: &MethodCallExpr, base: u8, tail: bool) -> u8 {
        self.compile_method_call_inner(call, 1, base, tail)
    }

    fn compile_method_call_inner(
        &mut self,
        call: &MethodCallExpr,
        want_rets: u8,
        base: u8,
        tail: bool,
    ) -> u8 {
        self.fs_mut().freereg = base;
        let func = self.reserve();
        let self_reg = self.reserve();
        self.compile_expr_into(&call.object, self_reg);
        let method = self.add_name_const(call.method);
        self.emit(abc(P386_OP_GETFIELD, func, self_reg, method));
        // self is the first argument; remaining args follow
        let nargs = self.compile_args(&call.args, self_reg + 1).saturating_add(1);
        if tail {
            self.emit(abc(P386_OP_TAILCALL, func, nargs.saturating_add(1), 0));
        } else {
            self.emit(abc(P386_OP_CALL, func, nargs.saturating_add(1), want_rets.saturating_add(1)));
        }
        if want_rets > 0 {
            self.note_reg(base + want_rets - 1);
        }
        self.set_freereg(base + want_rets.max(1));
        base
    }

    /// Compile argument expressions into consecutive registers starting at
    /// `start`. Returns the argument count.
    fn compile_args(&mut self, args: &[Expr], start: u8) -> u8 {
        self.fs_mut().freereg = start;
        let n = args.len().min(248) as u8;
        for (i, e) in args.iter().take(248).enumerate() {
            let dst = start + i as u8;
            self.fs_mut().freereg = dst;
            self.compile_expr_into(e, dst);
            self.fs_mut().freereg = dst + 1;
        }
        n
    }

    /// Place `n` values from `exprs` into consecutive registers at `base`.
    /// Extra targets get nil; extra exprs are evaluated and discarded.
    fn compile_expr_list(&mut self, exprs: &[Expr], base: u8, n: u8) {
        self.fs_mut().freereg = base;
        if exprs.is_empty() {
            for i in 0..n {
                self.emit(abc(P386_OP_LOADN, base + i, 1, 0));
            }
            if n > 0 {
                self.note_reg(base + n - 1);
            }
            self.fs_mut().freereg = base + n;
            return;
        }
        let last = exprs.len() - 1;
        for (i, e) in exprs.iter().enumerate() {
            let dst = base + i as u8;
            if i as u8 >= n && i != last {
                // beyond wanted but not the multi-value tail: still evaluate
                self.fs_mut().freereg = base + n.min(i as u8);
                let scratch = self.freereg();
                self.compile_expr_into(e, scratch);
                continue;
            }
            if i == last && (i as u8) < n {
                // last expression may produce multiple values
                let want = n - i as u8;
                self.fs_mut().freereg = dst;
                self.compile_expr_multi(e, dst, want);
            } else if (i as u8) < n {
                self.fs_mut().freereg = dst;
                self.compile_expr_into(e, dst);
                self.fs_mut().freereg = dst + 1;
            }
        }
        if n > 0 {
            self.note_reg(base + n - 1);
        }
        self.fs_mut().freereg = base + n;
    }

    /// Compile an expression that may yield multiple values into base..base+want-1.
    fn compile_expr_multi(&mut self, e: &Expr, base: u8, want: u8) {
        match e {
            Expr::Call(c) => {
                self.compile_call(c, want, base);
            }
            Expr::MethodCall(mc) => {
                self.compile_method_call(mc, want, base);
            }
            _ => {
                self.compile_expr_into(e, base);
                for i in 1..want {
                    self.emit(abc(P386_OP_LOADN, base + i, 1, 0));
                }
            }
        }
        if want > 0 {
            self.note_reg(base + want - 1);
        }
        self.fs_mut().freereg = base + want;
    }

    // ── expressions ──
    /// Compile expression into a specific register `dst`.
    fn compile_expr_into(&mut self, expr: &Expr, dst: u8) -> u8 {
        self.note_reg(dst);
        match expr {
            Expr::Nil => {
                self.emit(abc(P386_OP_LOADN, dst, 1, 0));
            }
            Expr::True => {
                self.emit(abc(P386_OP_LOADT, dst, 0, 0));
            }
            Expr::False => {
                self.emit(abc(P386_OP_LOADF, dst, 0, 0));
            }
            Expr::Number(n) => {
                let k = self.add_const(Constant::Num(*n));
                self.emit(abx(P386_OP_LOADK, dst, k as u16));
            }
            Expr::Str(s) => {
                let k = self.add_const(Constant::Str(s.clone()));
                self.emit(abx(P386_OP_LOADK, dst, k as u16));
            }
            Expr::Var(v) => return self.load_var_into(v, dst),
            Expr::BinOp(op, a, b) => self.compile_binop(*op, a, b, dst),
            Expr::UnOp(op, e) => {
                if let Some(opc) = un_opcode(*op) {
                    let save = self.freereg();
                    if dst >= save {
                        self.fs_mut().freereg = dst + 1;
                    }
                    let r = self.expr_to_rk(e);
                    self.emit(abc(opc, dst, r, 0));
                    self.fs_mut().freereg = save.max(dst + 1);
                } else {
                    // unsupported unary (%) -> passthrough
                    self.compile_expr_into(e, dst);
                }
            }
            Expr::Call(c) => {
                let save = self.freereg();
                let base = save.max(dst);
                self.compile_call(c, 1, base);
                if base != dst {
                    self.emit(abc(P386_OP_MOVE, dst, base, 0));
                }
                self.fs_mut().freereg = save.max(dst + 1);
            }
            Expr::MethodCall(mc) => {
                let save = self.freereg();
                let base = save.max(dst);
                self.compile_method_call(mc, 1, base);
                if base != dst {
                    self.emit(abc(P386_OP_MOVE, dst, base, 0));
                }
                self.fs_mut().freereg = save.max(dst + 1);
            }
            Expr::Table(fields) => self.compile_table(fields, dst),
            Expr::Function { params, has_varargs, body } => {
                let idx = self.add_function_proto(params, *has_varargs, body, false);
                self.emit(abx(P386_OP_CLOSURE, dst, idx));
            }
            Expr::Varargs => {
                // No VARARG opcode; yield nil.
                self.emit(abc(P386_OP_LOADN, dst, 1, 0));
            }
        };
        dst
    }

    fn compile_binop(&mut self, op: BinOp, a: &Expr, b: &Expr, dst: u8) {
        if op == BinOp::And || op == BinOp::Or {
            let save = self.freereg();
            if dst >= save {
                self.fs_mut().freereg = dst + 1;
            }
            self.compile_expr_into(a, dst);
            let jop = if op == BinOp::And { P386_OP_JMPF } else { P386_OP_JMPT };
            let skip = self.emit(asbx(jop, dst, 0));
            self.compile_expr_into(b, dst);
            self.patch_jump_to_here(skip);
            self.fs_mut().freereg = save.max(dst + 1);
            return;
        }
        if let Some(opc) = bin_opcode(op) {
            let save = self.freereg();
            if dst >= save {
                self.fs_mut().freereg = dst + 1;
            }
            let ra = self.expr_to_rk(a);
            let rb = self.expr_to_rk(b);
            self.emit(abc(opc, dst, ra, rb));
            self.fs_mut().freereg = save.max(dst + 1);
        }
    }

    fn compile_table(&mut self, fields: &[TableField], dst: u8) {
        self.note_reg(dst);
        let save = self.freereg();
        let work = if dst >= save { dst } else {
            // build in a temp, then move (dst is a low local we mustn't clobber
            // while evaluating fields that might reference it)
            dst
        };
        self.emit(abc(P386_OP_NEWTABLE, work, 0, 0));
        if work >= self.freereg() {
            self.fs_mut().freereg = work + 1;
        }
        let mut array_idx: i32 = 1 << 16;
        for f in fields {
            let mark = self.freereg();
            match f {
                TableField::Positional(v) => {
                    let kr = self.load_const_to_reg(Constant::Num(array_idx));
                    let vr = self.expr_to_rk(v);
                    self.emit(abc(P386_OP_SETTABLE, work, rk_reg(kr), vr));
                    array_idx += 1 << 16;
                }
                TableField::NamedField(n, v) => {
                    let k = self.add_name_const(*n);
                    let vr = self.expr_to_rk(v);
                    self.emit(abc(P386_OP_SETFIELD, work, k, vr));
                }
                TableField::IndexedField(k, v) => {
                    let kr = self.expr_to_rk(k);
                    let vr = self.expr_to_rk(v);
                    self.emit(abc(P386_OP_SETTABLE, work, kr, vr));
                }
            }
            self.fs_mut().freereg = mark;
        }
        self.fs_mut().freereg = save.max(dst + 1);
    }

    /// Evaluate expression, returning an RK byte (constant ref or register).
    fn expr_to_rk(&mut self, e: &Expr) -> u8 {
        match e {
            Expr::Number(n) => {
                let k = self.add_const(Constant::Num(*n));
                if k < 128 {
                    return rk_const(k);
                }
            }
            Expr::Str(s) => {
                let k = self.add_const(Constant::Str(s.clone()));
                if k < 128 {
                    return rk_const(k);
                }
            }
            Expr::True => {
                let k = self.add_const(Constant::Bool(true));
                if k < 128 {
                    return rk_const(k);
                }
            }
            Expr::False => {
                let k = self.add_const(Constant::Bool(false));
                if k < 128 {
                    return rk_const(k);
                }
            }
            Expr::Nil => {
                let k = self.add_const(Constant::Nil);
                if k < 128 {
                    return rk_const(k);
                }
            }
            _ => {}
        }
        rk_reg(self.expr_to_anyreg(e))
    }

    /// Evaluate expression into some register (a local's register when
    /// possible, otherwise a fresh temp). Returns that register.
    fn expr_to_anyreg(&mut self, e: &Expr) -> u8 {
        if let Expr::Var(Var::Name(n)) = e {
            if let Some(reg) = self.find_local(*n) {
                return reg;
            }
        }
        let r = self.reserve();
        self.compile_expr_into(e, r)
    }

    fn load_const_to_reg(&mut self, c: Constant) -> u8 {
        let r = self.reserve();
        let k = self.add_const(c);
        self.emit(abx(P386_OP_LOADK, r, k as u16));
        r
    }

    fn load_name_to_anyreg(&mut self, n: Name) -> u8 {
        if let Some(reg) = self.find_local(n) {
            return reg;
        }
        let r = self.reserve();
        self.load_var_into(&Var::Name(n), r)
    }

    fn load_var_into(&mut self, var: &Var, dst: u8) -> u8 {
        self.note_reg(dst);
        match var {
            Var::Name(n) => {
                if let Some(src) = self.find_local(*n) {
                    if src != dst {
                        self.emit(abc(P386_OP_MOVE, dst, src, 0));
                    }
                } else if let Some(uv) = self.resolve_upvalue(self.funcs.len() - 1, *n) {
                    self.emit(abc(P386_OP_GETUPVAL, dst, uv, 0));
                } else {
                    let slot = self.global_slot(*n);
                    self.emit(abc(P386_OP_GETGLOBAL, dst, slot, 0));
                }
            }
            Var::Index(obj, key) => {
                let save = self.freereg();
                if dst >= save {
                    self.fs_mut().freereg = dst + 1;
                }
                let or = self.expr_to_anyreg(obj);
                let kr = self.expr_to_rk(key);
                self.emit(abc(P386_OP_GETTABLE, dst, rk_reg(or), kr));
                self.fs_mut().freereg = save.max(dst + 1);
            }
            Var::Field(obj, n) => {
                let save = self.freereg();
                if dst >= save {
                    self.fs_mut().freereg = dst + 1;
                }
                let or = self.expr_to_anyreg(obj);
                let k = self.add_name_const(*n);
                self.emit(abc(P386_OP_GETFIELD, dst, or, k));
                self.fs_mut().freereg = save.max(dst + 1);
            }
        }
        dst
    }

    fn store_var(&mut self, var: &Var, src: u8) {
        match var {
            Var::Name(n) => {
                if let Some(dst) = self.find_local(*n) {
                    if dst != src {
                        self.emit(abc(P386_OP_MOVE, dst, src, 0));
                    }
                } else if let Some(uv) = self.resolve_upvalue(self.funcs.len() - 1, *n) {
                    self.emit(abc(P386_OP_SETUPVAL, src, uv, 0));
                } else {
                    let slot = self.global_slot(*n);
                    self.emit(abc(P386_OP_SETGLOBAL, src, slot, 0));
                }
            }
            Var::Index(obj, key) => {
                let or = self.expr_to_anyreg(obj);
                let kr = self.expr_to_rk(key);
                self.emit(abc(P386_OP_SETTABLE, or, kr, rk_reg(src)));
            }
            Var::Field(obj, n) => {
                let or = self.expr_to_anyreg(obj);
                let k = self.add_name_const(*n);
                self.emit(abc(P386_OP_SETFIELD, or, k, rk_reg(src)));
            }
        }
    }
}

fn var_as_expr(v: &Var) -> Expr {
    match v {
        Var::Name(n) => Expr::Var(Var::Name(*n)),
        Var::Index(o, k) => Expr::Var(Var::Index(clone_expr_box(o), clone_expr_box(k))),
        Var::Field(o, n) => Expr::Var(Var::Field(clone_expr_box(o), *n)),
    }
}

// The AST is move-only; for compound assignment we need to read the target as
// an expression and also store to it. Re-borrowing the Var is enough because we
// only build a shallow Expr referencing clones.
fn clone_expr_box(e: &Expr) -> alloc::boxed::Box<Expr> {
    alloc::boxed::Box::new(clone_expr(e))
}

fn clone_expr(e: &Expr) -> Expr {
    match e {
        Expr::Nil => Expr::Nil,
        Expr::True => Expr::True,
        Expr::False => Expr::False,
        Expr::Number(n) => Expr::Number(*n),
        Expr::Str(s) => Expr::Str(s.clone()),
        Expr::Varargs => Expr::Varargs,
        Expr::Var(v) => Expr::Var(clone_var(v)),
        Expr::BinOp(op, a, b) => Expr::BinOp(*op, clone_expr_box(a), clone_expr_box(b)),
        Expr::UnOp(op, a) => Expr::UnOp(*op, clone_expr_box(a)),
        Expr::Call(c) => Expr::Call(CallExpr {
            func: clone_expr_box(&c.func),
            args: c.args.iter().map(clone_expr).collect(),
        }),
        Expr::MethodCall(m) => Expr::MethodCall(MethodCallExpr {
            object: clone_expr_box(&m.object),
            method: m.method,
            args: m.args.iter().map(clone_expr).collect(),
        }),
        Expr::Function { params, has_varargs, body } => Expr::Function {
            params: params.clone(),
            has_varargs: *has_varargs,
            body: clone_stats(body),
        },
        Expr::Table(_) => Expr::Nil, // tables never appear as compound targets
    }
}

fn clone_var(v: &Var) -> Var {
    match v {
        Var::Name(n) => Var::Name(*n),
        Var::Index(o, k) => Var::Index(clone_expr_box(o), clone_expr_box(k)),
        Var::Field(o, n) => Var::Field(clone_expr_box(o), *n),
    }
}

fn clone_stats(_s: &[Stat]) -> Vec<Stat> {
    // Functions are never compound-assignment targets; this branch is unused.
    vec![]
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
