#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>

#include "serial.h"
#include "cart.h"

#define DECMP_BUF_SIZE 205 * 160 * 4 + 205

int fd; // Globals are like, fine for this purpose. Bleh.
PNG_Chunk ihdr;
PNG_Chunk idat;
uint8_t *decompressed_data;

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

inline uint16_t read_be16(uint8_t* buf) {
    return ((uint16_t) buf[0] << 8 ) | (uint16_t) buf[1];
}

inline uint32_t read_be32(uint8_t* buf) {
    return  ((uint32_t) buf[0] << 24) |
            ((uint32_t) buf[1] << 16) |
            ((uint32_t) buf[2] << 8) |
            (uint32_t) buf[3];
}

int read_chunk(PNG_Chunk *chunk) {
    int bytes_read;
    char buf[8];
    bytes_read = read(fd, buf, 8);
    if (bytes_read != 8) {
        debug_serial_print("Read error: Could not read chunk header\n");
        return -1;
    }
    chunk->length = read_be32(buf);
    memcpy(&chunk->type, buf + 4, 4);
    chunk->data = malloc(sizeof(char) * chunk->length);
    bytes_read = read(fd, chunk->data, chunk->length);
    if (bytes_read != chunk->length) {
        debug_serial_printf("Read error: Expected %d bytes, got %d bytes\n", 
            chunk->length, bytes_read);
        return -1;
    }
    // To-do: read these 4 bytes and CRC what we've read so far.
    // for now we can assume our HDD and floppy emulators can store
    // the image correctly. 
    lseek(fd, 4, SEEK_CUR);
    return 0;
}

int scan_cart() {
    int bytes_read;
    char *buf;
    if (fd <= 0) {
        return -1; // FD error
    }
    // Read header, check for valid .PNG file
    buf = malloc(sizeof(char) * 33);
    bytes_read = read(fd, buf, 8);
    if (bytes_read != 8) {
        debug_serial_printf("Read error: Read only %d bytes from fd %d\n", 
            bytes_read, fd);
        free(buf);
        return -2;
    }
    if (memcmp(buf, PNG_SIG, 8) != 0) {
        debug_serial_printf("Read error: Wrong PNG Header\n");
        free(buf);
        return -3;
    }
    debug_serial_printf("Valid PNG\n");
    if (read_chunk(&ihdr)                   // if IHDR chunk read error
    || ihdr.length != 13                    // Or unexpected IHDR length
    || memcmp(ihdr.type, "IHDR", 4) != 0    // Or not IHDR type
    ) {
        debug_serial_print("Invalid IHDR\n");
        free(buf);
        return -4;
    }

    if (read_be32(ihdr.data) != 160     // If width is not 160
    || read_be32(ihdr.data + 4) != 205  // Or height not 205
    || ((int) ihdr.data[8]) != 8        // Or not 8-bit color
    || ((int) ihdr.data[9]) != 6        // Or not RGBA type 6
    ) {
        debug_serial_print("Unexpected IHDR attributes!\n");
        debug_serial_printf("Expecting 160x205 8 bit color type 6, got %dx%d %d bit color type %d\n", 
            read_be32(ihdr.data), 
            read_be32(ihdr.data + 4), 
            ((int) ihdr.data[8]), 
            ((int) ihdr.data[9]));
        free(buf);
        return -5;
    }
    debug_serial_printf("Cartridge is a valid PICO-8 cartridge\n");
    free(buf);
    return 0;
}

int load_data() {
    size_t buf_size = DECMP_BUF_SIZE;
    int result;
    
    if (read_chunk(&idat)) {
        debug_serial_printf("Read error: Could not load IDAT\n");
        return -1;
    }
    debug_serial_printf("Read complete: Extracting %d bytes\n", idat.length);
    decompressed_data = malloc(DECMP_BUF_SIZE);
    result = uncompress(decompressed_data, (uLongf *) &buf_size, idat.data, idat.length);
    if(result != Z_OK) {
        debug_serial_printf("zlib fail with error code: %d\n", result);
        free(decompressed_data);
        return -1;
    }
    debug_serial_printf("Decompression successful, got %d bytes\n", buf_size);
    return 0;
}

void unload() {
    if (fd > 0) {
        close(fd);
    }
    if (ihdr.data) {
        free(ihdr.data);
    }
    if (idat.data) {
        free(idat.data);
    }
    return;
}
