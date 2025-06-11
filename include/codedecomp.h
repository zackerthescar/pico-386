#ifndef _CODE_DECOMP_H

#define _CODE_DECOMP_H

typedef unsigned char uint8;


// max_len should be 0x10000 (64k max code size)
// out_p should allocate 0x10001 (includes null terminator)
int pico8_code_section_decompress(uint8 *, uint8 *, int);
int decompress_mini(uint8 *, uint8 *, int);

#endif
