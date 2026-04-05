#ifndef _RS_H
#define _RS_H

/* PICO-8 Lua compiler (implemented in Rust) */

/* Opaque handle to a compiled program */
typedef void *P8Program;

/* Parse and compile PICO-8 Lua source. Returns NULL on error. */
P8Program p8_compile(const unsigned char *code, unsigned long len);

/* Free a compiled program. */
void p8_free_program(P8Program prog);

/* Get bytecode pointer and length. */
unsigned long p8_program_bytecode(P8Program prog, const unsigned char **out_ptr);

/* Get number of constants / nested prototypes. */
unsigned long p8_program_num_constants(P8Program prog);
unsigned long p8_program_num_protos(P8Program prog);

/* Validate-only (backwards compat). Returns 0 on success, -1 on error. */
int p8_parse_rs(const unsigned char *code, unsigned long len);

#endif
