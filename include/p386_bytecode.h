#ifndef P386_BYTECODE_H
#define P386_BYTECODE_H

#include <stdint.h>

#define P386_BC_MAGIC   0x36383350UL /* "P386" little-endian */
#define P386_BC_VERSION 1UL

#define P386_PROTO_FLAG_MAIN   0x01
#define P386_PROTO_FLAG_VARARG 0x02

/* Value tags: value at +0, tag at +4. Keep in sync with BYTECODE.md. */
#define P386_TAG_NIL   0UL
#define P386_TAG_BOOL  1UL
#define P386_TAG_NUM   2UL
#define P386_TAG_STR   3UL
#define P386_TAG_TAB   4UL
#define P386_TAG_FUNC  5UL
#define P386_TAG_CFUNC 6UL

/* Opcodes. */
#define P386_OP_MOVE      0x01
#define P386_OP_LOADK     0x02
#define P386_OP_LOADT     0x03
#define P386_OP_LOADF     0x04
#define P386_OP_LOADN     0x05
#define P386_OP_GETGLOBAL 0x10
#define P386_OP_SETGLOBAL 0x11
#define P386_OP_GETUPVAL  0x12
#define P386_OP_SETUPVAL  0x13
#define P386_OP_CLOSE     0x14
#define P386_OP_NEWTABLE  0x18
#define P386_OP_GETTABLE  0x19
#define P386_OP_SETTABLE  0x1A
#define P386_OP_GETFIELD  0x1B
#define P386_OP_SETFIELD  0x1C
#define P386_OP_ADD       0x20
#define P386_OP_SUB       0x21
#define P386_OP_MUL       0x22
#define P386_OP_DIV       0x23
#define P386_OP_IDIV      0x24
#define P386_OP_MOD       0x25
#define P386_OP_POW       0x26
#define P386_OP_NEG       0x27
#define P386_OP_BAND      0x28
#define P386_OP_BOR       0x29
#define P386_OP_BXOR      0x2A
#define P386_OP_BNOT      0x2B
#define P386_OP_SHL       0x2C
#define P386_OP_SHR       0x2D
#define P386_OP_LSHR      0x2E
#define P386_OP_ROTL      0x2F
#define P386_OP_ROTR      0x30
#define P386_OP_EQ        0x31
#define P386_OP_NE        0x32
#define P386_OP_LT        0x33
#define P386_OP_LE        0x34
#define P386_OP_GT        0x35
#define P386_OP_GE        0x36
#define P386_OP_NOT       0x37
#define P386_OP_LEN       0x38
#define P386_OP_PEEK      0x39
#define P386_OP_PEEK2     0x3A
#define P386_OP_CONCAT    0x3B
#define P386_OP_JMP       0x40
#define P386_OP_JMPF      0x41
#define P386_OP_JMPT      0x42
#define P386_OP_FORPREP   0x45
#define P386_OP_FORLOOP   0x46
#define P386_OP_TFORCALL  0x47
#define P386_OP_TFORLOOP  0x48
#define P386_OP_CLOSURE   0x50
#define P386_OP_CALL      0x51
#define P386_OP_TAILCALL  0x52
#define P386_OP_RETURN    0x53
#define P386_OP_VARARG    0x54

#pragma pack(push, 1)
typedef struct P386BcHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;
    uint32_t n_protos;
    uint32_t n_strings;
    uint32_t proto_table_offset;
    uint32_t string_table_offset;
    uint32_t bytecode_section_offset;
} P386BcHeader;

typedef struct P386ProtoEntry {
    uint32_t bytecode_off;
    uint32_t bytecode_len;
    uint32_t consts_off;
    uint32_t upvals_off;
    uint8_t  n_consts;
    uint8_t  n_params;
    uint8_t  n_regs;
    uint8_t  n_upvalues;
    uint8_t  flags;
    uint8_t  reserved[3];
} P386ProtoEntry;

typedef struct P386StringEntry {
    uint32_t data_off;
    uint32_t len;
} P386StringEntry;
#pragma pack(pop)

#define P386_ABC(op,a,b,c) \
    ((uint32_t)(op) | ((uint32_t)(a) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 24))
#define P386_ABX(op,a,bx) P386_ABC((op), (a), ((bx) & 0xff), (((bx) >> 8) & 0xff))
#define P386_ASBX(op,a,sbx) P386_ABX((op), (a), ((uint16_t)(sbx)))
#define P386_RK_REG(r)   ((uint8_t)((r) & 0x7f))
#define P386_RK_CONST(k) ((uint8_t)(0x80 | ((k) & 0x7f)))
#define P386_FP_INT(n)   ((int32_t)((n) << 16))

#endif
