//! DOS staticlib wrapper around the `pico386-core` compiler crate.
//!
//! This crate is `no_std`: it provides the Watcom allocator + panic handler and
//! the C FFI surface. All parser/compiler logic lives in `pico386-core`, which
//! is pulled in as an rlib so cross-crate LTO can fully inline it (keeping the
//! emitted object self-contained and the DOS EXE small).
#![no_std]

extern crate alloc;

use alloc::boxed::Box;
use core::alloc::{GlobalAlloc, Layout};

use pico386_core::bytecode::FuncProto;

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

// ── Public C API ─────────────────────────────────────────────────────

/// Opaque handle to a compiled PICO-8 program (heap-allocated FuncProto).
pub type P8Program = *mut FuncProto;

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
    match pico386_core::compile(s) {
        Some(proto) => Box::into_raw(Box::new(proto)),
        None => core::ptr::null_mut(),
    }
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
    proto.proto_count
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
