typedef struct {
    unsigned long width;
    unsigned long height;
    unsigned char bit_depth;
    unsigned char color_type;
    unsigned char compression;
    unsigned char filter;
    unsigned char interlace;
} IHDR_Data; // PNG IHDR chunk


int load_png(const char *);
int scan_cart();
void unload();
