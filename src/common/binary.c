#include "common/binary.h"

#include <string.h>

static bool binary_reader_can_read(const BinaryReader* reader, size_t size) {
    if(reader == NULL || reader->offset > reader->size) {
        return false;
    }

    if(size > reader->size - reader->offset) {
        return false;
    }

    return size == 0U || reader->data != NULL;
}

static bool binary_writer_can_write(const BinaryWriter* writer, size_t size) {
    if(writer == NULL || writer->offset > writer->capacity) {
        return false;
    }

    if(size > writer->capacity - writer->offset) {
        return false;
    }

    return size == 0U || writer->data != NULL;
}

void binary_reader_init(BinaryReader* reader, const uint8_t* data, size_t size) {
    if(reader == NULL) {
        return;
    }

    reader->data   = data;
    reader->size   = size;
    reader->offset = 0U;
}

size_t binary_reader_remaining(const BinaryReader* reader) {
    if(reader == NULL || reader->offset > reader->size) {
        return 0U;
    }

    return reader->size - reader->offset;
}

bool binary_reader_is_finished(const BinaryReader* reader) { return reader != NULL && reader->offset == reader->size; }

bool binary_read_u8(BinaryReader* reader, uint8_t* value) {
    if(value == NULL || !binary_reader_can_read(reader, 1U)) {
        return false;
    }

    *value = reader->data[reader->offset];
    reader->offset += 1U;
    return true;
}

bool binary_read_u16(BinaryReader* reader, uint16_t* value) {
    if(value == NULL || !binary_reader_can_read(reader, 2U)) {
        return false;
    }

    const uint8_t* data = reader->data + reader->offset;

    *value = (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
    reader->offset += 2U;
    return true;
}

bool binary_read_u32(BinaryReader* reader, uint32_t* value) {
    if(value == NULL || !binary_reader_can_read(reader, 4U)) {
        return false;
    }

    const uint8_t* data = reader->data + reader->offset;

    *value = ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) | ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
    reader->offset += 4U;
    return true;
}

bool binary_read_u64(BinaryReader* reader, uint64_t* value) {
    if(value == NULL || !binary_reader_can_read(reader, 8U)) {
        return false;
    }

    const uint8_t* data = reader->data + reader->offset;

    *value = ((uint64_t)data[0] << 56U) | ((uint64_t)data[1] << 48U) | ((uint64_t)data[2] << 40U) |
             ((uint64_t)data[3] << 32U) | ((uint64_t)data[4] << 24U) | ((uint64_t)data[5] << 16U) |
             ((uint64_t)data[6] << 8U) | (uint64_t)data[7];
    reader->offset += 8U;
    return true;
}

bool binary_read_bytes(BinaryReader* reader, void* output, size_t size) {
    if((size != 0U && output == NULL) || !binary_reader_can_read(reader, size)) {
        return false;
    }

    if(size != 0U) {
        memcpy(output, reader->data + reader->offset, size);
    }

    reader->offset += size;
    return true;
}

bool binary_read_view(BinaryReader* reader, const uint8_t** output, size_t size) {
    if(output == NULL || !binary_reader_can_read(reader, size)) {
        return false;
    }

    *output = size == 0U ? NULL : reader->data + reader->offset;
    reader->offset += size;
    return true;
}

void binary_writer_init(BinaryWriter* writer, uint8_t* data, size_t capacity) {
    if(writer == NULL) {
        return;
    }

    writer->data     = data;
    writer->capacity = capacity;
    writer->offset   = 0U;
}

size_t binary_writer_remaining(const BinaryWriter* writer) {
    if(writer == NULL || writer->offset > writer->capacity) {
        return 0U;
    }

    return writer->capacity - writer->offset;
}

size_t binary_writer_size(const BinaryWriter* writer) {
    if(writer == NULL || writer->offset > writer->capacity) {
        return 0U;
    }

    return writer->offset;
}

bool binary_write_u8(BinaryWriter* writer, uint8_t value) {
    if(!binary_writer_can_write(writer, 1U)) {
        return false;
    }

    writer->data[writer->offset] = value;
    writer->offset += 1U;
    return true;
}

bool binary_write_u16(BinaryWriter* writer, uint16_t value) {
    if(!binary_writer_can_write(writer, 2U)) {
        return false;
    }

    uint8_t* data = writer->data + writer->offset;

    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
    writer->offset += 2U;
    return true;
}

bool binary_write_u32(BinaryWriter* writer, uint32_t value) {
    if(!binary_writer_can_write(writer, 4U)) {
        return false;
    }

    uint8_t* data = writer->data + writer->offset;

    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
    writer->offset += 4U;
    return true;
}

bool binary_write_u64(BinaryWriter* writer, uint64_t value) {
    if(!binary_writer_can_write(writer, 8U)) {
        return false;
    }

    uint8_t* data = writer->data + writer->offset;

    data[0] = (uint8_t)(value >> 56U);
    data[1] = (uint8_t)(value >> 48U);
    data[2] = (uint8_t)(value >> 40U);
    data[3] = (uint8_t)(value >> 32U);
    data[4] = (uint8_t)(value >> 24U);
    data[5] = (uint8_t)(value >> 16U);
    data[6] = (uint8_t)(value >> 8U);
    data[7] = (uint8_t)value;
    writer->offset += 8U;
    return true;
}

bool binary_write_bytes(BinaryWriter* writer, const void* data, size_t size) {
    if((size != 0U && data == NULL) || !binary_writer_can_write(writer, size)) {
        return false;
    }

    if(size != 0U) {
        memcpy(writer->data + writer->offset, data, size);
    }

    writer->offset += size;
    return true;
}
