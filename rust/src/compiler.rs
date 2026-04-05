use alloc::boxed::Box;
use alloc::vec::Vec;

use crate::ast::*;
use crate::bytecode::*;

/// Tracks a local variable in the current scope.
struct Local {
    name: Name,
    depth: u8,
    slot: u8,
}

/// Tracks a pending break jump that needs backpatching.
struct BreakJump {
    patch_pos: usize,
    depth: u8,
}

/// Compiles an AST into a FuncProto.
pub struct Compiler {
    emit: Emitter,
    locals: Vec<Local>,
    depth: u8,
    breaks: Vec<BreakJump>,
    nt: NameTable,
}

impl Compiler {
    pub fn new(nt: NameTable) -> Self {
        Compiler {
            emit: Emitter::new(),
            locals: Vec::new(),
            depth: 0,
            breaks: Vec::new(),
            nt,
        }
    }

    /// Compile a top-level chunk.
    pub fn compile_chunk(mut self, chunk: Chunk) -> FuncProto {
        self.compile_block(&chunk.body);
        self.emit.emit_u8(Op::Return, 0);
        self.emit.finish(0, false, 0)
    }

    fn compile_block(&mut self, stmts: &[Stat]) {
        for stat in stmts {
            self.compile_stat(stat);
        }
    }

    // ── Statements ───────────────────────────────────────────────

    fn compile_stat(&mut self, stat: &Stat) {
        match stat {
            Stat::Empty => {}
            Stat::Break => self.compile_break(),
            Stat::Return(vals) => self.compile_return(vals),
            Stat::Call(call) => {
                self.compile_call_expr(call);
                self.emit.emit(Op::Pop); // discard return value
            }
            Stat::Assign { targets, values } => self.compile_assign(targets, values),
            Stat::LocalAssign { names, values } => self.compile_local_assign(names, values),
            Stat::Do(body) => {
                self.enter_scope();
                self.compile_block(body);
                self.leave_scope();
            }
            Stat::While { cond, body } => self.compile_while(cond, body),
            Stat::Repeat { body, cond } => self.compile_repeat(body, cond),
            Stat::If { conds, bodies, else_body } => self.compile_if(conds, bodies, else_body),
            Stat::ForNum { var, start, stop, step, body } =>
                self.compile_for_num(*var, start, stop, step.as_deref(), body),
            Stat::ForIn { vars, iters, body } => self.compile_for_in(vars, iters, body),
            Stat::FuncDef { name, params, has_varargs, body } =>
                self.compile_func_def(name, params, *has_varargs, body),
            Stat::LocalFuncDef { name, params, has_varargs, body } =>
                self.compile_local_func_def(*name, params, *has_varargs, body),
            Stat::Label(_name) => {
                // TODO: goto/label support
            }
            Stat::Goto(_name) => {
                // TODO: goto/label support
            }
            Stat::ShortPrint(args) => self.compile_short_print(args),
            Stat::ShortIf { cond, body, else_body } =>
                self.compile_short_if(cond, body, else_body),
            Stat::ShortWhile { cond, body } =>
                self.compile_while(cond, body),
            Stat::CompoundAssign { is_local, target, op, value } =>
                self.compile_compound_assign(*is_local, target, *op, value),
        }
    }

    fn compile_return(&mut self, vals: &[Expr]) {
        let n = vals.len();
        for v in vals {
            self.compile_expr(v);
        }
        self.emit.emit_u8(Op::Return, n as u8);
    }

    fn compile_break(&mut self) {
        let patch = self.emit.emit_jump(Op::Jmp);
        self.breaks.push(BreakJump { patch_pos: patch, depth: self.depth });
    }

    fn compile_assign(&mut self, targets: &[Var], values: &[Expr]) {
        // Push all values first
        for (i, v) in values.iter().enumerate() {
            self.compile_expr(v);
        }
        // Pad with nils if fewer values than targets
        for _ in values.len()..targets.len() {
            self.emit.emit(Op::PushNil);
        }
        // Pop with nils if more values than targets
        for _ in targets.len()..values.len() {
            self.emit.emit(Op::Pop);
        }
        // Assign in reverse order (last target gets TOS)
        for target in targets.iter().rev() {
            self.compile_set_var(target);
        }
    }

    fn compile_local_assign(&mut self, names: &[Name], values: &[Expr]) {
        // Push values
        for v in values.iter() {
            self.compile_expr(v);
        }
        for _ in values.len()..names.len() {
            self.emit.emit(Op::PushNil);
        }
        for _ in names.len()..values.len() {
            self.emit.emit(Op::Pop);
        }
        // Create locals and assign in order
        for name in names.iter() {
            let slot = self.emit.alloc_local();
            self.locals.push(Local { name: *name, depth: self.depth, slot });
            self.emit.emit_u8(Op::SetLocal, slot);
        }
    }

    fn compile_while(&mut self, cond: &Expr, body: &[Stat]) {
        let loop_start = self.emit.pos();
        self.compile_expr(cond);
        let exit_jump = self.emit.emit_jump(Op::JmpFalse);

        self.enter_scope();
        let old_breaks = self.breaks.len();
        self.compile_block(body);
        self.leave_scope();

        // Jump back to condition
        let back_offset = loop_start as isize - (self.emit.pos() as isize + 3);
        self.emit.emit_i16(Op::Jmp, back_offset as i16);

        self.emit.patch_jump(exit_jump);
        self.patch_breaks(old_breaks);
    }

    fn compile_repeat(&mut self, body: &[Stat], cond: &Expr) {
        let loop_start = self.emit.pos();

        self.enter_scope();
        let old_breaks = self.breaks.len();
        self.compile_block(body);

        self.compile_expr(cond);
        self.leave_scope();

        // Jump back if condition is false
        let back_offset = loop_start as isize - (self.emit.pos() as isize + 3);
        self.emit.emit_i16(Op::JmpFalse, back_offset as i16);

        self.patch_breaks(old_breaks);
    }

    fn compile_if(&mut self, conds: &[Expr], bodies: &[Vec<Stat>], else_body: &[Stat]) {
        let mut end_jumps: Vec<usize> = Vec::new();

        for (i, (cond, body)) in conds.iter().zip(bodies.iter()).enumerate() {
            self.compile_expr(cond);
            let false_jump = self.emit.emit_jump(Op::JmpFalse);

            self.enter_scope();
            self.compile_block(body);
            self.leave_scope();

            // Jump to end (skip remaining elseif/else branches)
            if i < conds.len() - 1 || !else_body.is_empty() {
                end_jumps.push(self.emit.emit_jump(Op::Jmp));
            }

            self.emit.patch_jump(false_jump);
        }

        if !else_body.is_empty() {
            self.enter_scope();
            self.compile_block(else_body);
            self.leave_scope();
        }

        for j in end_jumps {
            self.emit.patch_jump(j);
        }
    }

    fn compile_short_if(&mut self, cond: &Expr, body: &[Stat], else_body: &[Stat]) {
        self.compile_expr(cond);
        let false_jump = self.emit.emit_jump(Op::JmpFalse);
        self.compile_block(body);
        if !else_body.is_empty() {
            let end_jump = self.emit.emit_jump(Op::Jmp);
            self.emit.patch_jump(false_jump);
            self.compile_block(else_body);
            self.emit.patch_jump(end_jump);
        } else {
            self.emit.patch_jump(false_jump);
        }
    }

    fn compile_for_num(&mut self, var: Name, start: &Expr, stop: &Expr,
                       step: Option<&Expr>, body: &[Stat]) {
        self.enter_scope();

        // Allocate 3 hidden locals: idx, limit, step
        let base = self.emit.alloc_local();
        let _limit_slot = self.emit.alloc_local();
        let _step_slot = self.emit.alloc_local();
        // And the visible loop variable
        let var_slot = self.emit.alloc_local();
        self.locals.push(Local { name: var, depth: self.depth, slot: var_slot });

        // Push start, stop, step
        self.compile_expr(start);
        self.emit.emit_u8(Op::SetLocal, base);
        self.compile_expr(stop);
        self.emit.emit_u8(Op::SetLocal, base + 1);
        if let Some(s) = step {
            self.compile_expr(s);
        } else {
            self.emit.emit_i32(Op::PushNum, 1 << 16); // 1.0 in 16.16
        }
        self.emit.emit_u8(Op::SetLocal, base + 2);

        // ForPrep: validate and maybe skip loop
        let prep_jump = self.emit.pos();
        self.emit.emit_u8_i16(Op::ForPrep, base, 0); // placeholder offset

        let loop_top = self.emit.pos();
        let old_breaks = self.breaks.len();

        // Copy idx → visible variable
        self.emit.emit_u8(Op::GetLocal, base);
        self.emit.emit_u8(Op::SetLocal, var_slot);

        self.compile_block(body);

        // ForLoop: step and branch back
        let back_offset = loop_top as isize - (self.emit.pos() as isize + 4);
        self.emit.emit_u8_i16(Op::ForLoop, base, back_offset as i16);

        // Patch ForPrep to jump here on skip
        let after_loop = self.emit.pos();
        let prep_offset = after_loop as isize - (prep_jump as isize + 4);
        self.emit.code[prep_jump + 2] = (prep_offset as i16).to_le_bytes()[0];
        self.emit.code[prep_jump + 3] = (prep_offset as i16).to_le_bytes()[1];

        self.patch_breaks(old_breaks);
        self.leave_scope();
    }

    fn compile_for_in(&mut self, vars: &[Name], iters: &[Expr], body: &[Stat]) {
        self.enter_scope();

        // Allocate 3 hidden locals: iterator func, state, control
        let base = self.emit.alloc_local();
        let _state_slot = self.emit.alloc_local();
        let _ctrl_slot = self.emit.alloc_local();

        // Allocate visible loop variables
        let mut var_slots = Vec::new();
        for name in vars {
            let slot = self.emit.alloc_local();
            self.locals.push(Local { name: *name, depth: self.depth, slot });
            var_slots.push(slot);
        }

        // Push iterator expressions (up to 3: func, state, init)
        for (i, iter) in iters.iter().enumerate() {
            self.compile_expr(iter);
            if i < 3 {
                self.emit.emit_u8(Op::SetLocal, base + i as u8);
            } else {
                self.emit.emit(Op::Pop);
            }
        }
        for i in iters.len()..3 {
            self.emit.emit(Op::PushNil);
            self.emit.emit_u8(Op::SetLocal, base + i as u8);
        }

        let loop_top = self.emit.pos();
        let old_breaks = self.breaks.len();

        // ForIn: call iterator, check for nil
        let exit_jump_pos = self.emit.pos();
        self.emit.emit_u8_i16(Op::ForIn, base, 0); // placeholder

        // Copy results to visible variables
        // ForIn pushes N results starting at base+3
        // (the interpreter handles this)

        self.compile_block(body);

        // Jump back
        let back_offset = loop_top as isize - (self.emit.pos() as isize + 3);
        self.emit.emit_i16(Op::Jmp, back_offset as i16);

        // Patch ForIn exit
        let after = self.emit.pos();
        let exit_offset = after as isize - (exit_jump_pos as isize + 4);
        self.emit.code[exit_jump_pos + 2] = (exit_offset as i16).to_le_bytes()[0];
        self.emit.code[exit_jump_pos + 3] = (exit_offset as i16).to_le_bytes()[1];

        self.patch_breaks(old_breaks);
        self.leave_scope();
    }

    fn compile_func_def(&mut self, name: &FuncName, params: &[Name],
                        has_varargs: bool, body: &[Stat]) {
        let proto = self.compile_inner_func(params, has_varargs, body);
        let idx = self.emit.add_proto(proto);
        self.emit.emit_u16(Op::PushFunc, idx);

        // Assign to name: a.b.c or a.b:c
        if name.parts.len() == 1 && name.method.is_none() {
            let n = name.parts[0];
            if let Some(slot) = self.resolve_local(n) {
                self.emit.emit_u8(Op::SetLocal, slot);
            } else {
                let idx = self.intern_name(n);
                self.emit.emit_u16(Op::SetGlobal, idx);
            }
        } else {
            // Push base object
            let base = name.parts[0];
            self.compile_get_name(base);
            // Chain field accesses
            for &part in &name.parts[1..name.parts.len()-1] {
                let idx = self.intern_name(part);
                self.emit.emit_u16(Op::GetField, idx);
            }
            if let Some(method) = name.method {
                if name.parts.len() > 1 {
                    let idx = self.intern_name(*name.parts.last().unwrap());
                    self.emit.emit_u16(Op::GetField, idx);
                }
                let idx = self.intern_name(method);
                self.emit.emit_u16(Op::SetField, idx);
            } else {
                let last = *name.parts.last().unwrap();
                let idx = self.intern_name(last);
                self.emit.emit_u16(Op::SetField, idx);
            }
        }
    }

    fn compile_local_func_def(&mut self, name: Name, params: &[Name],
                              has_varargs: bool, body: &[Stat]) {
        let slot = self.emit.alloc_local();
        self.locals.push(Local { name, depth: self.depth, slot });

        let proto = self.compile_inner_func(params, has_varargs, body);
        let idx = self.emit.add_proto(proto);
        self.emit.emit_u16(Op::PushFunc, idx);
        self.emit.emit_u8(Op::SetLocal, slot);
    }

    fn compile_inner_func(&mut self, params: &[Name], has_varargs: bool,
                          body: &[Stat]) -> FuncProto {
        let mut inner = Compiler {
            emit: Emitter::new(),
            locals: Vec::new(),
            depth: 0,
            breaks: Vec::new(),
            // Share the name table (move it temporarily)
            nt: NameTable::new(), // placeholder — we swap below
        };
        core::mem::swap(&mut inner.nt, &mut self.nt);

        // Allocate param locals
        for &p in params {
            let slot = inner.emit.alloc_local();
            inner.locals.push(Local { name: p, depth: 0, slot });
        }

        inner.compile_block(body);
        inner.emit.emit_u8(Op::Return, 0);

        // Swap name table back
        core::mem::swap(&mut inner.nt, &mut self.nt);

        inner.emit.finish(params.len() as u8, has_varargs, 0)
    }

    fn compile_short_print(&mut self, args: &[Expr]) {
        // Compile as: print(args...)
        // Push the global "print"
        let print_name = self.nt.intern(b"print");
        let idx = self.intern_name(print_name);
        self.emit.emit_u16(Op::GetGlobal, idx);
        for a in args {
            self.compile_expr(a);
        }
        self.emit.emit_u8u8(Op::Call, args.len() as u8, 0);
    }

    fn compile_compound_assign(&mut self, is_local: bool, target: &Var,
                               op: CompoundOp, value: &Expr) {
        let arith_op = match op {
            CompoundOp::Add => Op::Add,
            CompoundOp::Sub => Op::Sub,
            CompoundOp::Mul => Op::Mul,
            CompoundOp::Div => Op::Div,
            CompoundOp::IDiv => Op::IDiv,
            CompoundOp::Mod => Op::Mod,
            CompoundOp::Pow => Op::Pow,
            CompoundOp::Concat => Op::Concat,
            CompoundOp::BitAnd => Op::BAnd,
            CompoundOp::BitOr => Op::BOr,
            CompoundOp::BitXor => Op::BXor,
            CompoundOp::Shl => Op::Shl,
            CompoundOp::Shr => Op::Shr,
            CompoundOp::LShr => Op::LShr,
            CompoundOp::RotL => Op::RotL,
            CompoundOp::RotR => Op::RotR,
        };

        if is_local {
            // local x += val → declare x, set to nil, then x = x + val
            if let Var::Name(n) = target {
                let slot = self.emit.alloc_local();
                self.locals.push(Local { name: *n, depth: self.depth, slot });
                self.emit.emit(Op::PushNil);
                self.emit.emit_u8(Op::SetLocal, slot);
                // Now do the compound: get local, push value, op, set local
                self.emit.emit_u8(Op::GetLocal, slot);
                self.compile_expr(value);
                self.emit.emit(arith_op);
                self.emit.emit_u8(Op::SetLocal, slot);
                return;
            }
        }

        // Fast path: local += val
        if let Var::Name(n) = target {
            if let Some(slot) = self.resolve_local(*n) {
                if arith_op as u8 == Op::Add as u8 {
                    self.compile_expr(value);
                    self.emit.emit_u8(Op::AddLocal, slot);
                    return;
                }
                if arith_op as u8 == Op::Sub as u8 {
                    self.compile_expr(value);
                    self.emit.emit_u8(Op::SubLocal, slot);
                    return;
                }
                // General case: get, compute, set
                self.emit.emit_u8(Op::GetLocal, slot);
                self.compile_expr(value);
                self.emit.emit(arith_op);
                self.emit.emit_u8(Op::SetLocal, slot);
                return;
            }
        }

        // General case: get var, push value, op, set var
        self.compile_get_var(target);
        self.compile_expr(value);
        self.emit.emit(arith_op);
        self.compile_set_var(target);
    }

    // ── Expressions ──────────────────────────────────────────────

    fn compile_expr(&mut self, expr: &Expr) {
        match expr {
            Expr::Nil => self.emit.emit(Op::PushNil),
            Expr::True => self.emit.emit(Op::PushTrue),
            Expr::False => self.emit.emit(Op::PushFalse),
            Expr::Number(n) => {
                self.emit.emit_i32(Op::PushNum, *n);
            }
            Expr::Str(s) => {
                let idx = self.emit.add_str_constant(s.clone());
                self.emit.emit_u16(Op::PushStr, idx);
            }
            Expr::Varargs => {
                self.emit.emit_u8(Op::VarArg, 1);
            }
            Expr::Var(v) => self.compile_get_var(v),
            Expr::BinOp(op, lhs, rhs) => self.compile_binop(*op, lhs, rhs),
            Expr::UnOp(op, e) => {
                self.compile_expr(e);
                let inst = match op {
                    UnOp::Neg => Op::Neg,
                    UnOp::Not => Op::Not,
                    UnOp::Len => Op::Len,
                    UnOp::BitNot => Op::BNot,
                    UnOp::Peek => Op::Peek,
                    UnOp::Peek2 => Op::Peek2,
                    UnOp::Pct => Op::Mod, // % as unary in PICO-8 is peek-like
                };
                self.emit.emit(inst);
            }
            Expr::Call(call) => {
                self.compile_call_expr(call);
            }
            Expr::MethodCall(mc) => {
                self.compile_expr(&mc.object);
                self.emit.emit(Op::Dup); // push object twice (self + method lookup)
                let idx = self.intern_name(mc.method);
                self.emit.emit_u16(Op::GetField, idx);
                // Swap so stack is: func self args...
                // Actually we need: func self args... but we have obj obj.method
                // Reorder: we want [method_func, obj, args...]
                // For simplicity, just compile as: get method, push self, push args, call
                // TODO: proper self calling convention
                for a in &mc.args {
                    self.compile_expr(a);
                }
                self.emit.emit_u8u8(Op::Call, (mc.args.len() + 1) as u8, 1);
            }
            Expr::Function { params, has_varargs, body } => {
                let proto = self.compile_inner_func(params, *has_varargs, body);
                let idx = self.emit.add_proto(proto);
                self.emit.emit_u16(Op::PushFunc, idx);
            }
            Expr::Table(fields) => self.compile_table(fields),
        }
    }

    fn compile_binop(&mut self, op: BinOp, lhs: &Expr, rhs: &Expr) {
        // Short-circuit: and/or use conditional jumps
        match op {
            BinOp::And => {
                self.compile_expr(lhs);
                let jump = self.emit.emit_jump(Op::JmpFalseK);
                self.emit.emit(Op::Pop);
                self.compile_expr(rhs);
                self.emit.patch_jump(jump);
                return;
            }
            BinOp::Or => {
                self.compile_expr(lhs);
                let jump = self.emit.emit_jump(Op::JmpTrueK);
                self.emit.emit(Op::Pop);
                self.compile_expr(rhs);
                self.emit.patch_jump(jump);
                return;
            }
            _ => {}
        }

        self.compile_expr(lhs);
        self.compile_expr(rhs);
        let inst = match op {
            BinOp::Add => Op::Add,
            BinOp::Sub => Op::Sub,
            BinOp::Mul => Op::Mul,
            BinOp::Div => Op::Div,
            BinOp::Mod => Op::Mod,
            BinOp::Pow => Op::Pow,
            BinOp::IDiv => Op::IDiv,
            BinOp::Concat => Op::Concat,
            BinOp::BitAnd => Op::BAnd,
            BinOp::BitOr => Op::BOr,
            BinOp::BitXor => Op::BXor,
            BinOp::Shl => Op::Shl,
            BinOp::Shr => Op::Shr,
            BinOp::LShr => Op::LShr,
            BinOp::RotL => Op::RotL,
            BinOp::RotR => Op::RotR,
            BinOp::Eq => Op::Eq,
            BinOp::Ne => Op::Ne,
            BinOp::Lt => Op::Lt,
            BinOp::Le => Op::Le,
            BinOp::Gt => Op::Gt,
            BinOp::Ge => Op::Ge,
            BinOp::And | BinOp::Or => unreachable!(),
        };
        self.emit.emit(inst);
    }

    fn compile_call_expr(&mut self, call: &CallExpr) {
        self.compile_expr(&call.func);
        for a in &call.args {
            self.compile_expr(a);
        }
        self.emit.emit_u8u8(Op::Call, call.args.len() as u8, 1);
    }

    fn compile_table(&mut self, fields: &[TableField]) {
        self.emit.emit_u8u8(Op::NewTable, fields.len() as u8, 0);
        let mut array_idx: u32 = 0;
        for field in fields {
            match field {
                TableField::NamedField(name, val) => {
                    self.emit.emit(Op::Dup);
                    self.compile_expr(val);
                    let idx = self.intern_name(*name);
                    self.emit.emit_u16(Op::SetField, idx);
                }
                TableField::IndexedField(key, val) => {
                    self.emit.emit(Op::Dup);
                    self.compile_expr(key);
                    self.compile_expr(val);
                    self.emit.emit(Op::SetTable);
                }
                TableField::Positional(val) => {
                    self.emit.emit(Op::Dup);
                    self.compile_expr(val);
                    self.emit.emit(Op::Append);
                    array_idx += 1;
                }
            }
        }
    }

    // ── Variable access ──────────────────────────────────────────

    fn compile_get_var(&mut self, var: &Var) {
        match var {
            Var::Name(n) => self.compile_get_name(*n),
            Var::Index(table, key) => {
                self.compile_expr(table);
                self.compile_expr(key);
                self.emit.emit(Op::GetTable);
            }
            Var::Field(table, name) => {
                self.compile_expr(table);
                let idx = self.intern_name(*name);
                self.emit.emit_u16(Op::GetField, idx);
            }
        }
    }

    fn compile_set_var(&mut self, var: &Var) {
        match var {
            Var::Name(n) => {
                if let Some(slot) = self.resolve_local(*n) {
                    self.emit.emit_u8(Op::SetLocal, slot);
                } else {
                    let idx = self.intern_name(*n);
                    self.emit.emit_u16(Op::SetGlobal, idx);
                }
            }
            Var::Index(table, key) => {
                // Stack has: ... value
                // Need: table key value on stack for SetTable
                // This is tricky — we need to evaluate table and key before
                // the value. The compiler should handle this at a higher level.
                // For now, emit a simpler (less optimal) sequence:
                // We already have value on stack. We need table and key under it.
                // TODO: restructure assignment to evaluate targets first
                self.compile_expr(table);
                self.compile_expr(key);
                // stack: value table key — need: table key value
                // This needs a rotate or the compiler must emit in different order.
                // For now, use a simple approach: evaluate table[key] targets
                // before values in compile_assign.
                self.emit.emit(Op::SetTable);
            }
            Var::Field(table, name) => {
                self.compile_expr(table);
                // stack: value table — need: table value for SetField
                self.emit.emit(Op::Dup); // TODO: swap instead
                let idx = self.intern_name(*name);
                self.emit.emit_u16(Op::SetField, idx);
            }
        }
    }

    fn compile_get_name(&mut self, name: Name) {
        if let Some(slot) = self.resolve_local(name) {
            self.emit.emit_u8(Op::GetLocal, slot);
        } else {
            let idx = self.intern_name(name);
            self.emit.emit_u16(Op::GetGlobal, idx);
        }
    }

    // ── Scope management ─────────────────────────────────────────

    fn enter_scope(&mut self) {
        self.depth += 1;
    }

    fn leave_scope(&mut self) {
        while let Some(local) = self.locals.last() {
            if local.depth < self.depth {
                break;
            }
            self.locals.pop();
        }
        self.depth -= 1;
    }

    fn resolve_local(&self, name: Name) -> Option<u8> {
        for local in self.locals.iter().rev() {
            if local.name == name {
                return Some(local.slot);
            }
        }
        None
    }

    fn patch_breaks(&mut self, since: usize) {
        while self.breaks.len() > since {
            if let Some(brk) = self.breaks.pop() {
                self.emit.patch_jump(brk.patch_pos);
            }
        }
    }

    /// Intern a Name into the constant pool as a string for global/field access.
    fn intern_name(&mut self, name: Name) -> u16 {
        let bytes = self.nt.resolve(name).to_vec();
        self.emit.add_str_constant(bytes)
    }
}
