#define _POSIX_C_SOURCE 200809L

#include "common/file_id.h"
#include "server/file_storage.h"
#include "server/file_transfer.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char  root_template[] = "/tmp/mkass-file-test-XXXXXX";
    char* root            = mkdtemp(root_template);
    assert(root != NULL);

    FileStorage storage;
    assert(file_storage_init(&storage, root) == 0);

    FileTransferState state;
    file_transfer_state_init(&state);

    static const uint8_t name[]   = "hello.txt";
    static const uint8_t first[]  = "hello ";
    static const uint8_t second[] = "world";

    assert(
        file_upload_begin(
            &storage, &state.upload, sizeof(first) - 1U + sizeof(second) - 1U, name, (uint16_t)(sizeof(name) - 1U)
        ) == FILE_TRANSFER_OK
    );

    assert(file_upload_write(&state.upload, first, (uint32_t)(sizeof(first) - 1U)) == FILE_TRANSFER_OK);

    assert(file_upload_write(&state.upload, second, (uint32_t)(sizeof(second) - 1U)) == FILE_TRANSFER_OK);

    FileId   file_id;
    uint32_t file_crc32 = 0U;

    assert(file_upload_finish(&storage, &state.upload, &file_id, &file_crc32) == FILE_TRANSFER_OK);

    assert(file_crc32 != 0U);
    assert(!file_id_is_zero(&file_id));

    assert(
        file_download_begin(&storage, &state.download, &file_id, name, (uint16_t)(sizeof(name) - 1U)) ==
        FILE_TRANSFER_OK
    );

    uint8_t  output[32];
    uint32_t output_len = 0U;
    bool     complete   = false;

    assert(file_download_read(&state.download, output, sizeof(output), &output_len, &complete) == FILE_TRANSFER_OK);

    assert(output_len == 11U);
    assert(complete);
    assert(memcmp(output, "hello world", 11U) == 0);

    assert(file_download_finish(&state.download) == FILE_TRANSFER_OK);

    static const uint8_t abort_name[] = "abort.bin";

    assert(
        file_upload_begin(&storage, &state.upload, 10U, abort_name, (uint16_t)(sizeof(abort_name) - 1U)) ==
        FILE_TRANSFER_OK
    );

    file_upload_abort(&storage, &state.upload);
    assert(!file_upload_is_active(&state.upload));

    assert(file_storage_remove_object(&storage, &file_id) == 0);

    file_storage_destroy(&storage);

    char      command[512];
    const int result = snprintf(command, sizeof(command), "rm -rf -- '%s'", root);
    assert(result > 0 && (size_t)result < sizeof(command));
    assert(system(command) == 0);

    puts("stage3 tests passed");
    return 0;
}
