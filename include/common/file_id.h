#ifndef FILE_ID_H
#define FILE_ID_H

#include "config.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t bytes[FILE_ID_SIZE];
} FileId;

int file_id_generate(FileId* file_id);

void file_id_to_hex(const FileId* file_id, char output[FILE_ID_HEX_SIZE + 1U]);

bool file_id_equal(const FileId* left, const FileId* right);

bool file_id_is_zero(const FileId* file_id);

#endif
