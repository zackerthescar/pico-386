#include <stdint.h>

typedef struct {
    uint32_t length;
    uint8_t type[4];
    uint8_t *data;
    // uint32_t crc32; We can ignore this for now...
} PNG_Chunk; // PNG IDAT


int load_png(const char *);
int read_chunk();
int scan_cart();
int load_data();
void unload();
