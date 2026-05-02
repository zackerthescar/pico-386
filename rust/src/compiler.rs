use alloc::vec::Vec;

use crate::ast::*;
use crate::bytecode::{Constant, FuncProto};

pub struct Compiler {
    names: NameTable,
}

impl Compiler {
    pub fn new(names: NameTable) -> Self {
        Self { names }
    }

    pub fn compile_chunk(&self, chunk: Chunk) -> FuncProto {
        let mut constants = Vec::new();
        let mut prototypes = Vec::new();
        self.collect_stats(&chunk.body, &mut constants, &mut prototypes);
        FuncProto::new(constants, prototypes)
    }

    fn collect_stats(&self, stats: &[Stat], constants: &mut Vec<Constant>, prototypes: &mut Vec<FuncProto>) {
        for stat in stats {
            self.collect_stat(stat, constants, prototypes);
        }
    }

    fn collect_stat(&self, stat: &Stat, constants: &mut Vec<Constant>, prototypes: &mut Vec<FuncProto>) {
        match stat {
            Stat::Assign { targets, values } => {
                for v in targets { self.collect_var(v, constants, prototypes); }
                for e in values { self.collect_expr(e, constants, prototypes); }
            }
            Stat::LocalAssign { values, .. } => {
                for e in values { self.collect_expr(e, constants, prototypes); }
            }
            Stat::Call(c) => self.collect_call(c, constants, prototypes),
            Stat::Do(body) => self.collect_stats(body, constants, prototypes),
            Stat::While { cond, body } => {
                self.collect_expr(cond, constants, prototypes);
                self.collect_stats(body, constants, prototypes);
            }
            Stat::Repeat { body, cond } => {
                self.collect_stats(body, constants, prototypes);
                self.collect_expr(cond, constants, prototypes);
            }
            Stat::If { conds, bodies, else_body } => {
                for e in conds { self.collect_expr(e, constants, prototypes); }
                for b in bodies { self.collect_stats(b, constants, prototypes); }
                self.collect_stats(else_body, constants, prototypes);
            }
            Stat::ForNum { var, start, stop, step, body } => {
                self.add_name_const(*var, constants);
                self.collect_expr(start, constants, prototypes);
                self.collect_expr(stop, constants, prototypes);
                if let Some(step) = step { self.collect_expr(step, constants, prototypes); }
                self.collect_stats(body, constants, prototypes);
            }
            Stat::ForIn { vars, iters, body } => {
                for n in vars { self.add_name_const(*n, constants); }
                for e in iters { self.collect_expr(e, constants, prototypes); }
                self.collect_stats(body, constants, prototypes);
            }
            Stat::FuncDef { name, params, body, .. } => {
                self.collect_func_name(name, constants);
                self.add_function_proto(params, body, constants, prototypes);
            }
            Stat::LocalFuncDef { name, params, body, .. } => {
                self.add_name_const(*name, constants);
                self.add_function_proto(params, body, constants, prototypes);
            }
            Stat::Return(values) | Stat::ShortPrint(values) => {
                for e in values { self.collect_expr(e, constants, prototypes); }
            }
            Stat::ShortIf { cond, body, else_body } => {
                self.collect_expr(cond, constants, prototypes);
                self.collect_stats(body, constants, prototypes);
                self.collect_stats(else_body, constants, prototypes);
            }
            Stat::ShortWhile { cond, body } => {
                self.collect_expr(cond, constants, prototypes);
                self.collect_stats(body, constants, prototypes);
            }
            Stat::CompoundAssign { target, value, .. } => {
                self.collect_var(target, constants, prototypes);
                self.collect_expr(value, constants, prototypes);
            }
            Stat::Break | Stat::Label(_) | Stat::Goto(_) | Stat::Empty => {}
        }
    }

    fn add_function_proto(&self, params: &[Name], body: &[Stat], constants: &mut Vec<Constant>, prototypes: &mut Vec<FuncProto>) {
        for n in params { self.add_name_const(*n, constants); }
        let mut fn_consts = Vec::new();
        let mut fn_protos = Vec::new();
        self.collect_stats(body, &mut fn_consts, &mut fn_protos);
        prototypes.push(FuncProto::new(fn_consts, fn_protos));
    }

    fn collect_func_name(&self, name: &FuncName, constants: &mut Vec<Constant>) {
        for n in &name.parts { self.add_name_const(*n, constants); }
        if let Some(n) = name.method { self.add_name_const(n, constants); }
    }

    fn collect_var(&self, var: &Var, constants: &mut Vec<Constant>, prototypes: &mut Vec<FuncProto>) {
        match var {
            Var::Name(n) => self.add_name_const(*n, constants),
            Var::Index(a, b) => {
                self.collect_expr(a, constants, prototypes);
                self.collect_expr(b, constants, prototypes);
            }
            Var::Field(obj, n) => {
                self.collect_expr(obj, constants, prototypes);
                self.add_name_const(*n, constants);
            }
        }
    }

    fn collect_call(&self, call: &CallExpr, constants: &mut Vec<Constant>, prototypes: &mut Vec<FuncProto>) {
        self.collect_expr(&call.func, constants, prototypes);
        for e in &call.args { self.collect_expr(e, constants, prototypes); }
    }

    fn collect_expr(&self, expr: &Expr, constants: &mut Vec<Constant>, prototypes: &mut Vec<FuncProto>) {
        match expr {
            Expr::Nil => self.add_const(constants, Constant::Nil),
            Expr::True => self.add_const(constants, Constant::Bool(true)),
            Expr::False => self.add_const(constants, Constant::Bool(false)),
            Expr::Number(n) => self.add_const(constants, Constant::Num(*n)),
            Expr::Str(s) => self.add_const(constants, Constant::Str(s.clone())),
            Expr::Var(v) => self.collect_var(v, constants, prototypes),
            Expr::BinOp(_, a, b) => {
                self.collect_expr(a, constants, prototypes);
                self.collect_expr(b, constants, prototypes);
            }
            Expr::UnOp(_, e) => self.collect_expr(e, constants, prototypes),
            Expr::Call(c) => self.collect_call(c, constants, prototypes),
            Expr::MethodCall(mc) => {
                self.collect_expr(&mc.object, constants, prototypes);
                self.add_name_const(mc.method, constants);
                for e in &mc.args { self.collect_expr(e, constants, prototypes); }
            }
            Expr::Function { params, body, .. } => self.add_function_proto(params, body, constants, prototypes),
            Expr::Table(fields) => {
                for f in fields {
                    match f {
                        TableField::IndexedField(k, v) => {
                            self.collect_expr(k, constants, prototypes);
                            self.collect_expr(v, constants, prototypes);
                        }
                        TableField::NamedField(n, v) => {
                            self.add_name_const(*n, constants);
                            self.collect_expr(v, constants, prototypes);
                        }
                        TableField::Positional(v) => self.collect_expr(v, constants, prototypes),
                    }
                }
            }
            Expr::Varargs => {}
        }
    }

    fn add_name_const(&self, name: Name, constants: &mut Vec<Constant>) {
        self.add_const(constants, Constant::Str(self.names.resolve(name).to_vec()));
    }

    fn add_const(&self, constants: &mut Vec<Constant>, c: Constant) {
        if constants.len() >= 255 { return; }
        let exists = constants.iter().any(|old| const_eq(old, &c));
        if !exists { constants.push(c); }
    }
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
