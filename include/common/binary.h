#ifndef BINARY_H
#define BINARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

void   binary_reader_init(BinaryReader* reader, const uint8_t* data, size_t size);
size_t binary_reader_remaining(const BinaryReader* reader);
bool   binary_reader_is_finished(const BinaryReader* reader);

bool binary_read_u8(BinaryReader* reader, uint8_t* value);
bool binary_read_u16(BinaryReader* reader, uint16_t* value);
bool binary_read_u32(BinaryReader* reader, uint32_t* value);
bool binary_read_u64(BinaryReader* reader, uint64_t* value);
bool binary_read_bytes(BinaryReader* reader, void* output, size_t size);
bool binary_read_view(BinaryReader* reader, const uint8_t** output, size_t size);

void   binary_writer_init(BinaryWriter* writer, uint8_t* data, size_t capacity);
size_t binary_writer_remaining(const BinaryWriter* writer);
size_t binary_writer_size(const BinaryWriter* writer);

bool binary_write_u8(BinaryWriter* writer, uint8_t value);
bool binary_write_u16(BinaryWriter* writer, uint16_t value);
bool binary_write_u32(BinaryWriter* writer, uint32_t value);
bool binary_write_u64(BinaryWriter* writer, uint64_t value);
bool binary_write_bytes(BinaryWriter* writer, const void* data, size_t size);

#endif
