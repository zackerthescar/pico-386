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
            if std::env::var("P8C_RAW").is_ok() {
                let bc = &proto.code;
                let n = u32::from_le_bytes([bc[12], bc[13], bc[14], bc[15]]);
                let pt = u32::from_le_bytes([bc[20], bc[21], bc[22], bc[23]]) as usize;
                for i in 0..n as usize {
                    let b = pt + i * 24;
                    eprintln!("proto {} n_params={} n_regs={} n_upvals={} flags={:#04x}",
                        i, bc[b + 17], bc[b + 18], bc[b + 19], bc[b + 20]);
                }
            }
            print!("{}", pico386_core::disasm::disasm(&proto));
        }
        Err(e) => {
            eprintln!("compile error: {}", e);
            std::process::exit(1);
        }
    }
}
