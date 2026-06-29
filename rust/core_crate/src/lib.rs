#![cfg_attr(not(feature = "std"), no_std)]

extern crate alloc;

pub mod ast;
pub mod bytecode;
pub mod compiler;
#[cfg(feature = "std")]
pub mod disasm;

use alloc::boxed::Box;
use alloc::vec::Vec;
use ast::*;

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

        // Long bracket of arbitrary level: [[ ... ]], [=[ ... ]=], [==[ ... ]==] ...
        rule long_comment_string()
            = "[" eqs:$(['=']*) "[" (!close_bracket(eqs.len()) [_])* close_bracket(eqs.len())

        rule close_bracket(level: usize)
            = "]" ['=']*<{level}> "]"

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

        // PICO-8 allows its special P8SCII glyphs (button symbols, emoji, etc.)
        // inside identifiers. In a UTF-8 .p8 these decode to code points well
        // above 0xff, so accept any non-ASCII scalar value (plus the 0x10-0x1f
        // control range PICO-8 uses for glyphs).
        rule ident_first()
            = ['a'..='z' | 'A'..='Z' | '_' | '\u{10}'..='\u{1f}' | '\u{7f}'..='\u{10ffff}']

        rule ident_rest()
            = ['a'..='z' | 'A'..='Z' | '_' | '0'..='9' | '\u{10}'..='\u{1f}' | '\u{7f}'..='\u{10ffff}']

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
        // `\z` skips the whitespace (including newlines) that follows it.
        rule escaped() = "\\z" ([' ' | '\t' | '\r' | '\n'])* / "\\" [_]
        rule long_string()
            = "[" eqs:$(['=']*) "[" (!close_bracket(eqs.len()) [_])* close_bracket(eqs.len())

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
            = a:expr_five(nt) rest:(_ () ("^^" !['='] / "~" !['=']) _ () b:expr_five(nt) { b })*
            { rest.into_iter().fold(a, |acc, b| Expr::BinOp(BinOp::BitXor, Box::new(acc), Box::new(b))) }

        rule expr_five(nt: &mut NameTable) -> Expr
            = a:expr_six(nt) rest:(_ () "&" !['='] _ () b:expr_six(nt) { b })*
            { rest.into_iter().fold(a, |acc, b| Expr::BinOp(BinOp::BitAnd, Box::new(acc), Box::new(b))) }

        rule expr_six(nt: &mut NameTable) -> Expr
            = a:expr_seven(nt) rest:(_ () op:shift_op() _ () b:expr_seven(nt) { (op, b) })*
            { rest.into_iter().fold(a, |acc, (op, b)| Expr::BinOp(op, Box::new(acc), Box::new(b))) }

        // concat is right-associative. Parse the left operand ONCE, then look
        // for the operator; on absence return it unchanged (avoids re-parsing
        // the operand, which caused exponential blowup on nested expressions).
        rule expr_seven(nt: &mut NameTable) -> Expr
            = a:expr_eight(nt) b:(_ () ".." !['.' | '='] _ () b:expr_seven(nt) { b })?
            {
                match b {
                    Some(b) => Expr::BinOp(BinOp::Concat, Box::new(a), Box::new(b)),
                    None => a,
                }
            }

        rule expr_eight(nt: &mut NameTable) -> Expr
            = a:expr_nine(nt) rest:(_ () op:add_op() _ () b:expr_nine(nt) { (op, b) })*
            { rest.into_iter().fold(a, |acc, (op, b)| Expr::BinOp(op, Box::new(acc), Box::new(b))) }

        rule expr_nine(nt: &mut NameTable) -> Expr
            = a:expr_ten(nt) rest:(_ () op:mul_op() _ () b:expr_ten(nt) { (op, b) })*
            { rest.into_iter().fold(a, |acc, (op, b)| Expr::BinOp(op, Box::new(acc), Box::new(b))) }

        rule expr_ten(nt: &mut NameTable) -> Expr
            = op:unary_op() _ () e:expr_ten(nt) { Expr::UnOp(op, Box::new(e)) }
            / expr_eleven(nt)

        // ^ is right-associative; same single-parse factoring as concat.
        rule expr_eleven(nt: &mut NameTable) -> Expr
            = a:expr_twelve(nt) b:(_ () "^" !['^' | '='] _ () b:expr_ten(nt) { b })?
            {
                match b {
                    Some(b) => Expr::BinOp(BinOp::Pow, Box::new(a), Box::new(b)),
                    None => a,
                }
            }

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
            / local_statement(nt)
            / expr_statement(nt)
            / label_statement(nt)
            / kw_break() { Stat::Break }
            / goto_statement(nt)
            / do_statement(nt)
            / while_statement(nt)
            / repeat_statement(nt)
            / if_statement(nt)
            / for_statement(nt)
            / function_definition(nt)
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
            / first:statement(nt) rest:(inline_sep() s:statement(nt) { s })*
              ret:(inline_sep() r:short_body_return(nt) { r })?
            {
                let mut v = vec![first];
                v.extend(rest);
                if let Some(r) = ret { v.push(r); }
                v
            }

        // In a single-line body, `return`'s value list (if any) must begin on
        // the same physical line. A following newline ends the body, so e.g.
        // `if (x) return` followed by `y=1` does not swallow `y` as a return
        // value.
        rule short_body_return(nt: &mut NameTable) -> Stat
            = kw_return() vals:(inline_sep() !eol() v:expr_list(nt) { v })? { Stat::Return(vals.unwrap_or_default()) }

        // Inline separator for PICO-8 single-line if/while bodies: skips spaces,
        // tabs and `;` but NOT a newline (a newline ends the single-line body).
        rule inline_sep()
            = quiet!{ [' ' | '\t' | ';']* }

        // Short-while: while (expr) stmts-on-same-line
        rule short_while_statement(nt: &mut NameTable) -> Stat
            = kw_while() _ ()
              cond:bracket_expr(nt) _ ()
              !kw_do()
              first:statement(nt) rest:(inline_sep() s:statement(nt) { s })*
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

        // Unified prefix-expression statement. The leading var/call chain is
        // parsed ONCE (avoiding the exponential backtracking that came from
        // having separate compound/assignment/call rules each re-parse it),
        // then we dispatch on what follows: `op=` (compound), `,`/`=`
        // (assignment) or nothing (a call statement).
        rule expr_statement(nt: &mut NameTable) -> Stat
            = first:expr_thirteen(nt)
              tail:expr_statement_tail(nt)
            {?
                match tail {
                    StatTail::Compound(op, value) => match expr_to_var(first) {
                        Some(target) => Ok(Stat::CompoundAssign {
                            is_local: false, target, op, value,
                        }),
                        None => Err("compound-assignment target"),
                    },
                    StatTail::Assign(rest, values) => {
                        let mut targets = Vec::new();
                        match expr_to_var(first) {
                            Some(v) => targets.push(v),
                            None => return Err("assignment target"),
                        }
                        for e in rest {
                            match expr_to_var(e) {
                                Some(v) => targets.push(v),
                                None => return Err("assignment target"),
                            }
                        }
                        Ok(Stat::Assign { targets, values })
                    }
                    StatTail::Call => match first {
                        Expr::Call(c) => Ok(Stat::Call(c)),
                        Expr::MethodCall(mc) => Ok(Stat::MethodCall(mc)),
                        _ => Err("call statement"),
                    },
                }
            }

        rule expr_statement_tail(nt: &mut NameTable) -> StatTail
            = _ () op:compound_op() _ () value:expression(nt)
              { StatTail::Compound(op, Box::new(value)) }
            / rest:(_ () "," _ () v:expr_thirteen(nt) { v })* _ () "=" !['=']
              _ () values:expr_list(nt)
              { StatTail::Assign(rest, values) }
            / { StatTail::Call }

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

// ── Compile API ──────────────────────────────────────────────────────

/// Parse and compile PICO-8 Lua source into a FuncProto. Returns None on
/// parse or codegen error.
pub fn compile(src: &str) -> Option<bytecode::FuncProto> {
    let mut nt = NameTable::new();
    let chunk = p8lua::grammar(src, &mut nt).ok()?;
    let comp = compiler::Compiler::new(nt);
    comp.compile_chunk(chunk)
}

/// Host-only variant returning a textual error.
#[cfg(feature = "std")]
pub fn compile_source(src: &str) -> Result<bytecode::FuncProto, alloc::string::String> {
    use alloc::string::ToString;
    let mut nt = NameTable::new();
    let chunk = p8lua::grammar(src, &mut nt).map_err(|e| e.to_string())?;
    let comp = compiler::Compiler::new(nt);
    comp.compile_chunk(chunk).ok_or_else(|| "codegen failed".to_string())
}
