#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

#include "serial.h"
#include "cart.h"

int fd; // Globals are like, fine for this purpose. Bleh.

static const unsigned char PNG_SIG[8] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

int load_png(const char *filename) {
    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd <= 0) {
        debug_serial_print("load_png(): read error!\n");
        return -1;
    }
    return 0;
}

unsigned long read_be32(unsigned char* buf) {
    return  ((unsigned long) buf[0] << 24) |
            ((unsigned long) buf[1] << 16) |
            ((unsigned long) buf[2] << 8) |
            (unsigned long) buf[3];
}

int scan_cart() {
    int bytes_read;
    char *buf;
    unsigned long chunk_length;
    IHDR_Data ihdr;
    if (fd <= 0) {
        return 1; // FD error
    }
    buf = malloc(sizeof(char) * 33);

    // Read header, check for valid .PNG file
    bytes_read = read(fd, buf, 8);
    if (bytes_read != 8) {
        debug_serial_printf("Read error: Read only %d bytes from fd %d\n", bytes_read, fd);
        free(buf);
        return 2;
    }
    if (memcmp(buf, PNG_SIG, 8) != 0) {
        debug_serial_printf("Read error: Wrong PNG Header\n");
        free(buf);
        return 3;
    }
    debug_serial_printf("Valid PNG\n");

    // Read IHDR chunk
    bytes_read = read(fd, buf, 8);
    if (bytes_read != 8) {
        debug_serial_printf("Read error: Read only %d bytes from fd %d\n", bytes_read, fd);
        free(buf);
        return 4;
    }
    if (memcmp(buf + 4, "IHDR", 4) != 0) {
        debug_serial_printf("Read error: First chunk is not IHDR\n");
        free(buf);
        return 5;
    }
    chunk_length = read_be32(buf);
    if (chunk_length != 13) {
        debug_serial_printf("Read error: Bad chunk length\n");
        free(buf);
        return 6;
    }
    debug_serial_printf("Valid IHDR\n");

    // Read IHDR
    bytes_read = read(fd, buf, 13);
    if (bytes_read != 13) {
        debug_serial_printf("Read error: Read only %d bytes from fd %d\n", bytes_read, fd);
        free(buf);
        return 7;
    }
    ihdr.width = read_be32(buf);
    ihdr.height = read_be32(buf + 4);
    if (ihdr.width != 160 || ihdr.height != 205) {
        debug_serial_printf("Cart error: Bad PNG resolution\n");
        debug_serial_printf("Expected 160x205, got %dx$d", ihdr.width, ihdr.height);
        free(buf);
        return 8;
    }
    debug_serial_printf("Cartridge is a valid PICO-8 cartridge\n");
    free(buf);
    return 0;
}

void unload() {
    if (fd > 0) {
        close(fd);
        return;
    }
}
