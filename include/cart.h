#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t color_type;
    uint8_t compression;
    uint8_t filter;
    uint8_t interlace;
} IHDR_Data; // PNG IHDR chunk


int load_png(const char *);
int scan_cart();
void unload();
