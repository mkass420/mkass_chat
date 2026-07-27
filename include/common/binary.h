#ifndef BINARY_H
#define BINARY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const uint8_t* data;
    size_t         size;
    size_t         offset;
} BinaryReader;

typedef struct {
    uint8_t* data;
    size_t   capacity;
    size_t   offset;
} BinaryWriter;

bool binary_read_u8(BinaryReader* reader, uint8_t* value);
bool binary_read_u16(BinaryReader* reader, uint16_t* value);
bool binary_read_u32(BinaryReader* reader, uint32_t* value);
bool binary_read_u64(BinaryReader* reader, uint64_t* value);

bool binary_read_bytes(BinaryReader* reader, void* output, size_t size);

bool binary_write_u8(BinaryWriter* writer, uint8_t value);
bool binary_write_u16(BinaryWriter* writer, uint16_t value);
bool binary_write_u32(BinaryWriter* writer, uint32_t value);
bool binary_write_u64(BinaryWriter* writer, uint64_t value);

bool binary_write_bytes(BinaryWriter* writer, const void* data, size_t size);


#endif
