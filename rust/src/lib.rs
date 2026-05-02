#![no_std]

extern crate alloc;

mod ast;
mod bytecode;
mod compiler;

use core::alloc::{GlobalAlloc, Layout};
use alloc::boxed::Box;
use alloc::vec::Vec;
use ast::*;

// ── Watcom allocator ─────────────────────────────────────────────────

extern "C" {
    fn wc_malloc(size: usize) -> *mut u8;
    fn wc_free(ptr: *mut u8);
    fn wc_realloc(ptr: *mut u8, size: usize) -> *mut u8;
}

struct WatcomAlloc;

unsafe impl GlobalAlloc for WatcomAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe { wc_malloc(layout.size()) }
    }
    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        unsafe { wc_free(ptr) }
    }
    unsafe fn realloc(&self, ptr: *mut u8, _layout: Layout, new_size: usize) -> *mut u8 {
        unsafe { wc_realloc(ptr, new_size) }
    }
}

#[global_allocator]
static ALLOC: WatcomAlloc = WatcomAlloc;

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

// ── PICO-8 Lua PEG Parser ───────────────────────────────────────────

peg::parser! {
    grammar p8lua() for str {
        use alloc::vec;
        use super::ast::*;
        use super::alloc::boxed::Box;
        use super::alloc::vec::Vec;

        // ── Whitespace & comments ────────────────────────────────────

        rule _() = quiet!{ (ws() / comment() / cpp_comment())* }

        rule ws() = [' ' | '\t' | '\r' | '\n']

        rule comment()
            = "--" long_comment_string()
            / "--" short_comment()

        rule cpp_comment()
            = "//" short_comment()

        rule short_comment()
            = (!eol() [_])* eol_or_eof()

        rule eol() = ['\r' | '\n']
        rule eol_or_eof() = eol() / ![_]

        rule long_comment_string()
            = "[[" long_comment_body() "]]"

        rule long_comment_body()
            = (!"]]" (long_comment_string() / [_]))*

        // ── Keywords ─────────────────────────────────────────────────

        rule kw_and()      = "and"      !ident_rest()
        rule kw_break()    = "break"    !ident_rest()
        rule kw_do()       = "do"       !ident_rest()
        rule kw_else()     = "else"     !ident_rest()
        rule kw_elseif()   = "elseif"   !ident_rest()
        rule kw_end()      = "end"      !ident_rest()
        rule kw_false()    = "false"    !ident_rest()
        rule kw_for()      = "for"      !ident_rest()
        rule kw_function() = "function" !ident_rest()
        rule kw_goto()     = "goto"     !ident_rest()
        rule kw_if()       = "if"       !ident_rest()
        rule kw_in()       = "in"       !ident_rest()
        rule kw_local()    = "local"    !ident_rest()
        rule kw_nil()      = "nil"      !ident_rest()
        rule kw_not()      = "not"      !ident_rest()
        rule kw_or()       = "or"       !ident_rest()
        rule kw_repeat()   = "repeat"   !ident_rest()
        rule kw_return()   = "return"   !ident_rest()
        rule kw_then()     = "then"     !ident_rest()
        rule kw_true()     = "true"     !ident_rest()
        rule kw_until()    = "until"    !ident_rest()
        rule kw_while()    = "while"    !ident_rest()

        rule keyword()
            = ("and" / "break" / "do" / "elseif" / "else" / "end"
               / "false" / "for" / "function" / "goto" / "if" / "in"
               / "local" / "nil" / "not" / "or" / "repeat" / "return"
               / "then" / "true" / "until" / "while") !ident_rest()

        // ── Identifiers ──────────────────────────────────────────────

        rule ident_first()
            = ['a'..='z' | 'A'..='Z' | '_' | '\u{10}'..='\u{1f}' | '\u{7f}'..='\u{ff}']

        rule ident_rest()
            = ['a'..='z' | 'A'..='Z' | '_' | '0'..='9' | '\u{10}'..='\u{1f}' | '\u{7f}'..='\u{ff}']

        pub rule name(nt: &mut NameTable) -> Name
            = s:$(!keyword() ident_first() ident_rest()*) { nt.intern(s.as_bytes()) }

        // ── Number literals ──────────────────────────────────────────

        rule numeral() -> i32
            = n:$(hexadecimal() / binary() / decimal()) { parse_number(n) }

        rule hexadecimal() = "0" ['x' | 'X'] hex_body()
        rule hex_body()
            = ['0'..='9' | 'a'..='f' | 'A'..='F']+ ("." ['0'..='9' | 'a'..='f' | 'A'..='F']*)?
            / "." ['0'..='9' | 'a'..='f' | 'A'..='F']+
        rule binary() = "0" ['b' | 'B'] bin_body()
        rule bin_body()
            = ['0' | '1']+ ("." ['0' | '1']*)?
            / "." ['0' | '1']+
        rule decimal()
            = dec_digits() ("." ['0'..='9']*)? exponent()?
            / "." ['0'..='9']+ exponent()?
        rule dec_digits() = ['0'..='9']+
        rule exponent() = ['e' | 'E'] ['+' | '-']? ['0'..='9']+

        // ── String literals ──────────────────────────────────────────

        rule literal_string() -> Vec<u8>
            = s:$(short_string_double() / short_string_single() / long_string())
            { parse_string_literal(s) }

        rule short_string_double()
            = "\"" (escaped() / !['\"' | '\r' | '\n'] [_])* "\""
        rule short_string_single()
            = "\'" (escaped() / !['\'' | '\r' | '\n'] [_])* "\'"
        rule escaped() = "\\" [_]
        rule long_string() = "[" "="* "[" long_string_body()
        rule long_string_body() = (!("]]") [_])* "]]"

        // ── Operators ────────────────────────────────────────────────

        rule comparison_op() -> BinOp
            = "==" { BinOp::Eq } / "<=" { BinOp::Le } / ">=" { BinOp::Ge }
            / "~=" { BinOp::Ne } / "!=" { BinOp::Ne }
            / "<" !['<'] { BinOp::Lt } / ">" !['>'] { BinOp::Gt }

        rule shift_op() -> BinOp
            = "<<>" !['='] { BinOp::RotL }
            / ">><" !['='] { BinOp::RotR }
            / ">>>" !['='] { BinOp::LShr }
            / "<<" !['=']  { BinOp::Shl }
            / ">>" !['=']  { BinOp::Shr }

        rule add_op() -> BinOp
            = "+" !['='] { BinOp::Add }
            / "-" !['-' | '='] { BinOp::Sub }

        rule mul_op() -> BinOp
            = "/" !['/' | '='] { BinOp::Div }
            / "\\" !['='] { BinOp::IDiv }
            / "*" !['='] { BinOp::Mul }
            / "%" !['='] { BinOp::Mod }

        rule unary_op() -> UnOp
            = "-" !['-' | '='] { UnOp::Neg }
            / "%" !['='] { UnOp::Pct }
            / "#" { UnOp::Len }
            / "~" !['='] { UnOp::BitNot }
            / "@" { UnOp::Peek }
            / "$" { UnOp::Peek2 }
            / kw_not() { UnOp::Not }

        rule compound_op() -> CompoundOp
            = "^^=" { CompoundOp::BitXor }
            / "<<>=" { CompoundOp::RotL } / ">><=" { CompoundOp::RotR }
            / ">>>=" { CompoundOp::LShr }
            / "<<=" { CompoundOp::Shl } / ">>=" { CompoundOp::Shr }
            / "+=" { CompoundOp::Add } / "-=" { CompoundOp::Sub }
            / "*=" { CompoundOp::Mul } / "/=" { CompoundOp::Div }
            / "\\=" { CompoundOp::IDiv } / "%=" { CompoundOp::Mod }
            / "^=" { CompoundOp::Pow }
            / "&=" { CompoundOp::BitAnd } / "|=" { CompoundOp::BitOr }
            / "..=" { CompoundOp::Concat }

        // ── Expressions (precedence tower) ───────────────────────────

        pub rule expression(nt: &mut NameTable) -> Expr
            = a:expr_one(nt) rest:(_ () kw_or() _ () b:expr_one(nt) { b })*
            { rest.into_iter().fold(a, |acc, b| Expr::BinOp(BinOp::Or, Box::new(acc), Box::new(b))) }

        rule expr_one(nt: &mut NameTable) -> Expr
            = a:expr_two(nt) rest:(_ () kw_and() _ () b:expr_two(nt) { b })*
            { rest.into_iter().fold(a, |acc, b| Expr::BinOp(BinOp::And, Box::new(acc), Box::new(b))) }

        rule expr_two(nt: &mut NameTable) -> Expr
            = a:expr_three(nt) rest:(_ () op:comparison_op() _ () b:expr_three(nt) { (op, b) })*
            { rest.into_iter().fold(a, |acc, (op, b)| Expr::BinOp(op, Box::new(acc), Box::new(b))) }

        rule expr_three(nt: &mut NameTable) -> Expr
            = a:expr_four(nt) rest:(_ () "|" !['='] _ () b:expr_four(nt) { b })*
            { rest.into_iter().fold(a, |acc, b| Expr::BinOp(BinOp::BitOr, Box::new(acc), Box::new(b))) }

        rule expr_four(nt: &mut NameTable) -> Expr
            = a:expr_five(nt) rest:(_ () "^^" !['='] _ () b:expr_five(nt) { b })*
            { rest.into_iter().fold(a, |acc, b| Expr::BinOp(BinOp::BitXor, Box::new(acc), Box::new(b))) }

        rule expr_five(nt: &mut NameTable) -> Expr
            = a:expr_six(nt) rest:(_ () "&" !['='] _ () b:expr_six(nt) { b })*
            { rest.into_iter().fold(a, |acc, b| Expr::BinOp(BinOp::BitAnd, Box::new(acc), Box::new(b))) }

        rule expr_six(nt: &mut NameTable) -> Expr
            = a:expr_seven(nt) rest:(_ () op:shift_op() _ () b:expr_seven(nt) { (op, b) })*
            { rest.into_iter().fold(a, |acc, (op, b)| Expr::BinOp(op, Box::new(acc), Box::new(b))) }

        // concat is right-associative
        rule expr_seven(nt: &mut NameTable) -> Expr
            = a:expr_eight(nt) _ () ".." !['.' | '='] _ () b:expr_seven(nt)
              { Expr::BinOp(BinOp::Concat, Box::new(a), Box::new(b)) }
            / expr_eight(nt)

        rule expr_eight(nt: &mut NameTable) -> Expr
            = a:expr_nine(nt) rest:(_ () op:add_op() _ () b:expr_nine(nt) { (op, b) })*
            { rest.into_iter().fold(a, |acc, (op, b)| Expr::BinOp(op, Box::new(acc), Box::new(b))) }

        rule expr_nine(nt: &mut NameTable) -> Expr
            = a:expr_ten(nt) rest:(_ () op:mul_op() _ () b:expr_ten(nt) { (op, b) })*
            { rest.into_iter().fold(a, |acc, (op, b)| Expr::BinOp(op, Box::new(acc), Box::new(b))) }

        rule expr_ten(nt: &mut NameTable) -> Expr
            = op:unary_op() _ () e:expr_ten(nt) { Expr::UnOp(op, Box::new(e)) }
            / expr_eleven(nt)

        // ^ is right-associative
        rule expr_eleven(nt: &mut NameTable) -> Expr
            = a:expr_twelve(nt) _ () "^" !['^' | '='] _ () b:expr_ten(nt)
              { Expr::BinOp(BinOp::Pow, Box::new(a), Box::new(b)) }
            / expr_twelve(nt)

        rule expr_twelve(nt: &mut NameTable) -> Expr
            = kw_nil() { Expr::Nil }
            / kw_true() { Expr::True }
            / kw_false() { Expr::False }
            / "..." { Expr::Varargs }
            / n:numeral() { Expr::Number(n) }
            / s:literal_string() { Expr::Str(s) }
            / function_literal(nt)
            / expr_thirteen(nt)
            / t:table_constructor(nt) { Expr::Table(t) }

        // Combined var/call suffix chain
        rule expr_thirteen(nt: &mut NameTable) -> Expr
            = base:(bracket_expr(nt) / n:name(nt) { Expr::Var(Var::Name(n)) })
              suffixes:(_ () s:suffix(nt) { s })*
            { apply_suffixes(base, suffixes) }

        rule suffix(nt: &mut NameTable) -> Suffix
            = "[" _ () e:expression(nt) _ () "]" { Suffix::Index(Box::new(e)) }
            / "." !(".") _ () n:name(nt) { Suffix::Field(n) }
            / ":" !(":") _ () n:name(nt) _ () a:function_args(nt) { Suffix::MethodCall(n, a) }
            / a:function_args(nt) { Suffix::Call(a) }

        rule bracket_expr(nt: &mut NameTable) -> Expr
            = "(" _ () e:expression(nt) _ () ")" { e }

        rule function_args(nt: &mut NameTable) -> Vec<Expr>
            = "(" _ () args:expr_list(nt)? _ () ")" { args.unwrap_or_default() }
            / t:table_constructor(nt) { vec![Expr::Table(t)] }
            / s:literal_string() { vec![Expr::Str(s)] }

        // ── Variable (for assignment targets) ────────────────────────

        rule variable(nt: &mut NameTable) -> Var
            = base:var_base(nt)
              suffixes:(_ () s:var_suffix(nt) { s })*
            {
                suffixes.into_iter().fold(base, |acc, s| {
                    let base_expr = Expr::Var(acc);
                    match s {
                        Suffix::Index(idx) => Var::Index(Box::new(base_expr), idx),
                        Suffix::Field(n) => Var::Field(Box::new(base_expr), n),
                        Suffix::Call(_) | Suffix::MethodCall(_, _) => unreachable!(),
                    }
                })
            }

        rule var_base(nt: &mut NameTable) -> Var
            = n:name(nt) { Var::Name(n) }

        rule var_suffix(nt: &mut NameTable) -> Suffix
            = "[" _ () e:expression(nt) _ () "]" { Suffix::Index(Box::new(e)) }
            / "." !(".") _ () n:name(nt) { Suffix::Field(n) }

        // ── Function call (for statements) ───────────────────────────

        rule function_call_expr(nt: &mut NameTable) -> Expr
            = base:(bracket_expr(nt) / n:name(nt) { Expr::Var(Var::Name(n)) })
              suffixes:(_ () s:suffix(nt) { s })+
            {?
                if suffixes.last().map(|s| suffix_is_call(s)).unwrap_or(false) {
                    Ok(apply_suffixes(base, suffixes))
                } else {
                    Err("expected function call")
                }
            }

        // ── Lists ────────────────────────────────────────────────────

        rule name_list(nt: &mut NameTable) -> Vec<Name>
            = first:name(nt) rest:(_ () "," _ () n:name(nt) { n })* {
                let mut v = vec![first]; v.extend(rest); v
            }

        rule expr_list(nt: &mut NameTable) -> Vec<Expr>
            = first:expression(nt) rest:(_ () "," _ () e:expression(nt) { e })* {
                let mut v = vec![first]; v.extend(rest); v
            }

        // ── Table constructor ────────────────────────────────────────

        rule table_constructor(nt: &mut NameTable) -> Vec<TableField>
            = "{" _ () fields:table_field_list(nt)? _ () "}" { fields.unwrap_or_default() }

        rule table_field_list(nt: &mut NameTable) -> Vec<TableField>
            = first:table_field(nt) rest:(_ () [',' | ';'] _ () f:table_field(nt) { f })* (_ () [',' | ';'])?
            { let mut v = vec![first]; v.extend(rest); v }

        rule table_field(nt: &mut NameTable) -> TableField
            = "[" _ () k:expression(nt) _ () "]" _ () "=" _ () v:expression(nt)
              { TableField::IndexedField(Box::new(k), Box::new(v)) }
            / n:name(nt) _ () "=" !['='] _ () v:expression(nt)
              { TableField::NamedField(n, Box::new(v)) }
            / e:expression(nt)
              { TableField::Positional(Box::new(e)) }

        // ── Function literal & body ──────────────────────────────────

        rule function_literal(nt: &mut NameTable) -> Expr
            = kw_function() _ () b:function_body(nt) { b }

        rule function_body(nt: &mut NameTable) -> Expr
            = "(" _ () p:parameter_list(nt)? _ () ")" _ ()
              body:statement_list_until_end(nt)
            {
                let (params, has_varargs) = p.unwrap_or((Vec::new(), false));
                Expr::Function { params, has_varargs, body }
            }

        rule parameter_list(nt: &mut NameTable) -> (Vec<Name>, bool)
            = "..." { (Vec::new(), true) }
            / names:name_list(nt) _ () "," _ () "..." { (names, true) }
            / names:name_list(nt) { (names, false) }

        // ── Statement lists ──────────────────────────────────────────

        rule statement_list_until_end(nt: &mut NameTable) -> Vec<Stat>
            = _ () stmts:(!(kw_end()) s:statement(nt) _ () { s })* kw_end() { stmts }

        rule statement_list_until_end_or_else(nt: &mut NameTable) -> Vec<Stat>
            = _ () stmts:(!(kw_end() / kw_else() / kw_elseif()) s:statement(nt) _ () { s })* { stmts }

        rule statement_list_until_until(nt: &mut NameTable) -> Vec<Stat>
            = _ () stmts:(!(kw_until()) s:statement(nt) _ () { s })* kw_until() { stmts }

        // ── Statements ───────────────────────────────────────────────

        pub rule statement(nt: &mut NameTable) -> Stat
            = ";" { Stat::Empty }
            / short_print_statement(nt)
            / short_if_statement(nt)
            / short_while_statement(nt)
            / compound_statement(nt)
            / assignment_statement(nt)
            / function_call_statement(nt)
            / label_statement(nt)
            / kw_break() { Stat::Break }
            / goto_statement(nt)
            / do_statement(nt)
            / while_statement(nt)
            / repeat_statement(nt)
            / if_statement(nt)
            / for_statement(nt)
            / function_definition(nt)
            / local_statement(nt)
            / return_statement(nt)

        rule label_statement(nt: &mut NameTable) -> Stat
            = "::" _ () n:name(nt) _ () "::" { Stat::Label(n) }

        rule goto_statement(nt: &mut NameTable) -> Stat
            = kw_goto() _ () n:name(nt) { Stat::Goto(n) }

        rule do_statement(nt: &mut NameTable) -> Stat
            = kw_do() _ () body:statement_list_until_end(nt) { Stat::Do(body) }

        rule while_statement(nt: &mut NameTable) -> Stat
            = kw_while() _ () cond:expression(nt) _ () kw_do() _ ()
              body:statement_list_until_end(nt)
            { Stat::While { cond: Box::new(cond), body } }

        rule repeat_statement(nt: &mut NameTable) -> Stat
            = kw_repeat() _ ()
              body:statement_list_until_until(nt) _ () cond:expression(nt)
            { Stat::Repeat { body, cond: Box::new(cond) } }

        rule if_statement(nt: &mut NameTable) -> Stat
            = kw_if() _ () first_cond:expression(nt) _ () kw_then() _ ()
              first_body:statement_list_until_end_or_else(nt)
              elseifs:(_ () kw_elseif() _ () c:expression(nt) _ () kw_then() _ ()
                       b:statement_list_until_end_or_else(nt) { (c, b) })*
              else_body:(_ () kw_else() _ ()
                         eb:(!(kw_end()) s:statement(nt) _ () { s })* { eb })?
              kw_end()
            {
                let mut conds = vec![first_cond];
                let mut bodies = vec![first_body];
                for (c, b) in elseifs {
                    conds.push(c);
                    bodies.push(b);
                }
                Stat::If { conds, bodies, else_body: else_body.unwrap_or_default() }
            }

        // Short-if: if (expr) stmts-on-same-line
        rule short_if_statement(nt: &mut NameTable) -> Stat
            = kw_if() _ () &(!(_ () expression(nt) _ () kw_then()))
              e:bracket_expr(nt) _ ()
              body:short_body(nt)
              else_body:(_ () kw_else() _ () eb:short_body(nt) { eb })?
            { Stat::ShortIf { cond: Box::new(e), body, else_body: else_body.unwrap_or_default() } }

        rule short_body(nt: &mut NameTable) -> Vec<Stat>
            = r:short_body_return(nt) { vec![r] }
            / first:statement(nt) rest:(_ () !eol_boundary() s:statement(nt) { s })*
              ret:(_ () r:short_body_return(nt) { r })?
            {
                let mut v = vec![first];
                v.extend(rest);
                if let Some(r) = ret { v.push(r); }
                v
            }

        rule short_body_return(nt: &mut NameTable) -> Stat
            = kw_return() _ () vals:expr_list(nt)? { Stat::Return(vals.unwrap_or_default()) }

        rule eol_boundary()
            = quiet!{ _no_newline()* eol() }

        rule _no_newline()
            = [' ' | '\t']
            / comment()
            / cpp_comment()

        // Short-while: while (expr) stmts-on-same-line
        rule short_while_statement(nt: &mut NameTable) -> Stat
            = kw_while() _ ()
              cond:bracket_expr(nt) _ ()
              !kw_do()
              first:statement(nt) rest:(_ () !eol_boundary() s:statement(nt) { s })*
            {
                let mut body = vec![first];
                body.extend(rest);
                Stat::ShortWhile { cond: Box::new(cond), body }
            }

        // Short print: ?expr_list
        rule short_print_statement(nt: &mut NameTable) -> Stat
            = "?" _ () args:expr_list(nt)? { Stat::ShortPrint(args.unwrap_or_default()) }

        rule for_statement(nt: &mut NameTable) -> Stat
            = kw_for() _ () var:name(nt) _ ()
              s:(for_numeric(nt, var) / for_generic(nt, var)) { s }

        rule for_numeric(nt: &mut NameTable, var: Name) -> Stat
            = "=" _ () start:expression(nt) _ () "," _ () stop:expression(nt)
              step:(_ () "," _ () e:expression(nt) { e })? _ ()
              kw_do() _ () body:statement_list_until_end(nt)
            { Stat::ForNum { var, start: Box::new(start), stop: Box::new(stop), step: step.map(Box::new), body } }

        rule for_generic(nt: &mut NameTable, first_var: Name) -> Stat
            = extra:("," _ () n:name(nt) { n })* _ ()
              kw_in() _ () iters:expr_list(nt) _ ()
              kw_do() _ () body:statement_list_until_end(nt)
            {
                let mut vars = vec![first_var];
                vars.extend(extra);
                Stat::ForIn { vars, iters, body }
            }

        rule function_definition(nt: &mut NameTable) -> Stat
            = kw_function() _ () fname:function_name(nt) _ () b:function_body_stat(nt)
            {
                let (params, has_varargs, body) = b;
                Stat::FuncDef { name: fname, params, has_varargs, body }
            }

        rule function_name(nt: &mut NameTable) -> FuncName
            = first:name(nt) rest:(_ () "." _ () n:name(nt) { n })*
              method:(_ () ":" _ () n:name(nt) { n })?
            {
                let mut parts = vec![first];
                parts.extend(rest);
                FuncName { parts, method }
            }

        rule function_body_stat(nt: &mut NameTable) -> (Vec<Name>, bool, Vec<Stat>)
            = "(" _ () p:parameter_list(nt)? _ () ")" _ ()
              body:statement_list_until_end(nt)
            {
                let (params, has_varargs) = p.unwrap_or((Vec::new(), false));
                (params, has_varargs, body)
            }

        rule local_statement(nt: &mut NameTable) -> Stat
            = kw_local() _ () s:(
                kw_function() _ () n:name(nt) _ () b:function_body_stat(nt)
                {
                    let (params, has_varargs, body) = b;
                    Stat::LocalFuncDef { name: n, params, has_varargs, body }
                }
                / names:name_list(nt) _ () vals:("=" _ () e:expr_list(nt) { e })?
                { Stat::LocalAssign { names, values: vals.unwrap_or_default() } }
            ) { s }

        rule return_statement(nt: &mut NameTable) -> Stat
            = kw_return() vals:(_ () v:expr_list(nt) { v })? _ () ";"?
            { Stat::Return(vals.unwrap_or_default()) }

        // Compound assignment: [local] var op= expr
        rule compound_statement(nt: &mut NameTable) -> Stat
            = is_local:(kw_local() _ () { true } / { false })
              target:variable(nt) _ () op:compound_op() _ () value:expression(nt)
            { Stat::CompoundAssign { is_local, target, op, value: Box::new(value) } }

        // Plain assignment: varlist = exprlist
        rule assignment_statement(nt: &mut NameTable) -> Stat
            = first:variable(nt) rest:(_ () "," _ () v:variable(nt) { v })* _ ()
              "=" _ () values:expr_list(nt)
            {
                let mut targets = vec![first];
                targets.extend(rest);
                Stat::Assign { targets, values }
            }

        // Function call as statement
        rule function_call_statement(nt: &mut NameTable) -> Stat
            = e:function_call_expr(nt) {
                match e {
                    Expr::Call(c) => Stat::Call(c),
                    Expr::MethodCall(mc) => Stat::MethodCall(mc),
                    _ => Stat::Empty,
                }
            }

        // ── Top-level grammar ────────────────────────────────────────

        pub rule grammar(nt: &mut NameTable) -> Chunk
            = _ () header_comment()? _ () header_comment()? _ ()
              body:(!(![_]) s:statement(nt) _ () { s })*
              ![_]
            { Chunk { body } }

        rule header_comment()
            = "--" !long_comment_string() short_comment()
    }
}

// ── Public C API ─────────────────────────────────────────────────────

/// Opaque handle to a compiled PICO-8 program.
/// Points to a heap-allocated FuncProto.
pub type P8Program = *mut bytecode::FuncProto;

/// Parse and compile PICO-8 Lua source code.
/// Returns null on error, or an opaque program handle on success.
#[export_name = "_p8_compile"]
pub extern "C" fn p8_compile(code: *const u8, len: u32) -> P8Program {
    if code.is_null() {
        return core::ptr::null_mut();
    }
    let slice = unsafe { core::slice::from_raw_parts(code, len as usize) };
    let s = match core::str::from_utf8(slice) {
        Ok(s) => s,
        Err(_) => return core::ptr::null_mut(),
    };
    let mut nt = NameTable::new();
    let chunk = match p8lua::grammar(s, &mut nt) {
        Ok(c) => c,
        Err(_) => return core::ptr::null_mut(),
    };
    let comp = compiler::Compiler::new(nt);
    let proto = match comp.compile_chunk(chunk) {
        Some(p) => p,
        None => return core::ptr::null_mut(),
    };
    Box::into_raw(Box::new(proto))
}

/// Free a compiled program.
#[export_name = "_p8_free_program"]
pub extern "C" fn p8_free_program(prog: P8Program) {
    if !prog.is_null() {
        unsafe { drop(Box::from_raw(prog)); }
    }
}

/// Get bytecode pointer and length from a compiled program.
/// Returns bytecode length, writes pointer to *out_ptr.
#[export_name = "_p8_program_bytecode"]
pub extern "C" fn p8_program_bytecode(prog: P8Program, out_ptr: *mut *const u8) -> u32 {
    if prog.is_null() || out_ptr.is_null() {
        return 0;
    }
    let proto = unsafe { &*prog };
    unsafe { *out_ptr = proto.code.as_ptr(); }
    proto.code.len() as u32
}

/// Get number of constants in the program.
#[export_name = "_p8_program_num_constants"]
pub extern "C" fn p8_program_num_constants(prog: P8Program) -> u32 {
    if prog.is_null() { return 0; }
    let proto = unsafe { &*prog };
    proto.constants.len() as u32
}

/// Get number of nested function prototypes.
#[export_name = "_p8_program_num_protos"]
pub extern "C" fn p8_program_num_protos(prog: P8Program) -> u32 {
    if prog.is_null() { return 0; }
    let proto = unsafe { &*prog };
    proto.prototypes.len() as u32
}

/// Validate-only entry point (backwards compat with test harness).
#[export_name = "_p8_parse_rs"]
pub extern "C" fn p8_parse_rs(code: *const u8, len: u32) -> i32 {
    let prog = p8_compile(code, len);
    if prog.is_null() {
        return -1;
    }
    p8_free_program(prog);
    0
}
