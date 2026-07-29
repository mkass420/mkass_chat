#include "common/file_id.h"

#include "config.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/random.h>

static int file_id_fill_random(FileId* file_id) {
    size_t offset = 0U;

    while(offset < sizeof(file_id->bytes)) {
        const ssize_t result = getrandom(file_id->bytes + offset, sizeof(file_id->bytes) - offset, 0);

        if(result > 0) {
            offset += (size_t)result;
            continue;
        }

        if(result < 0 && errno == EINTR) {
            continue;
        }

        if(result == 0) {
            errno = EIO;
        }

        return -1;
    }

    return 0;
}

int file_id_generate(FileId* file_id) {
    if(file_id == NULL) {
        errno = EINVAL;
        return -1;
    }

    do {
        if(file_id_fill_random(file_id) != 0) {
            return -1;
        }
    } while(file_id_is_zero(file_id));

    return 0;
}

void file_id_to_hex(const FileId* file_id, char output[FILE_ID_HEX_SIZE + 1U]) {
    static const char hex_digits[] = "0123456789abcdef";

    if(output == NULL) {
        return;
    }

    if(file_id == NULL) {
        output[0] = '\0';
        return;
    }

    for(size_t i = 0U; i < FILE_ID_SIZE; ++i) {
        const uint8_t value = file_id->bytes[i];

        output[i * 2U]      = hex_digits[value >> 4U];
        output[i * 2U + 1U] = hex_digits[value & 0x0FU];
    }

    output[FILE_ID_HEX_SIZE] = '\0';
}

bool file_id_equal(const FileId* left, const FileId* right) {
    return left != NULL && right != NULL && memcmp(left->bytes, right->bytes, FILE_ID_SIZE) == 0;
}

bool file_id_is_zero(const FileId* file_id) {
    if(file_id == NULL) {
        return false;
    }

    for(size_t i = 0U; i < FILE_ID_SIZE; ++i) {
        if(file_id->bytes[i] != 0U) {
            return false;
        }
    }

    return true;
}
