//! Host CLI: compile PICO-8 Lua from stdin/file and dump disassembly.
//! Usage: p8c [file]   (reads stdin if no file)
use std::io::Read;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let src = if args.len() > 1 {
        std::fs::read_to_string(&args[1]).expect("read file")
    } else {
        let mut s = String::new();
        std::io::stdin().read_to_string(&mut s).expect("read stdin");
        s
    };
    match pico386_core::compile_source(&src) {
        Ok(proto) => {
            print!("{}", pico386_core::disasm::disasm(&proto));
        }
        Err(e) => {
            eprintln!("compile error: {}", e);
            std::process::exit(1);
        }
    }
}
