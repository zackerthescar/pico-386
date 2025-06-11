#include <stdint.h>

typedef struct {
    uint32_t length;
    uint8_t type[4];
    uint8_t *data;
    // uint32_t crc32; We can ignore this for now...
} PNG_Chunk; // PNG IDAT

typedef struct {
    uint8_t cmf;
    uint8_t extra_flags;
    uint8_t *data;
    uint16_t check_value;
} ZLIB_Chunk;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} pix_t;

int load_png(const char *);
int read_chunk();
int scan_cart();
int load_data();
void unload();
