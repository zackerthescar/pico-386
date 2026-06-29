#ifndef P386_VALUE_H
#define P386_VALUE_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct P386Value {
    int32_t  value;
    uint32_t tag;
} P386Value;
#pragma pack(pop)

#endif
