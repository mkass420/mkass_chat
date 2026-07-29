#ifndef CONFIG_H
#define CONFIG_H

/* --- PROTOCOL SETTINGS --- */
#define PROTOCOL_MAX_PAYLOAD_LENGTH (64U * 1024U) // 64 КиБ - лимит размера данных после сжатия, включая метаданные
#define PROTOCOL_MAX_UNCOMPRESSED_LENGTH                                                        \
    (256U * 1024U) // 256 Киб - лимит несжатого размера для каждого ЧАНКА, не для файла целиком
#define PROTOCOL_MAGIC 0x4348U

#define PACKET_FLAG_COMPRESSED  0x01U
#define PACKET_KNOWN_FLAGS      PACKET_FLAG_COMPRESSED
#define PACKET_HEADER_WIRE_SIZE 20U

/* --- FILE SETTINGS --- */
#define FILE_MAX_SIZE        (128ULL * 1024ULL * 1024ULL) // Максимальный размер всего файлв
#define FILE_CHUNK_DATA_SIZE (60U * 1024U)                // Максимальное количество байт файла в одном сообщении
#define FILE_ID_SIZE         16U
#define FILE_MAX_NAME_LENGTH 255U
#define FILE_MAX_MIME_LENGTH 64U

#define FILE_UPLOAD_BEGIN_REQUEST_PREFIX_SIZE    (8U + 2U)
#define FILE_UPLOAD_FINISH_RESPONSE_SIZE         FILE_ID_SIZE
#define FILE_DOWNLOAD_BEGIN_REQUEST_SIZE         FILE_ID_SIZE
#define FILE_DOWNLOAD_BEGIN_RESPONSE_PREFIX_SIZE (8U + 2U)

#define FILE_ID_SIZE                16U
#define FILE_ID_HEX_SIZE            (FILE_ID_SIZE * 2U)
#define FILE_ID_GENERATION_ATTEMPTS 16U

#define FILE_STORAGE_TEMPORARY_DIRECTORY "tmp"
#define FILE_STORAGE_OBJECTS_DIRECTORY   "obj"
#define FILE_STORAGE_DIRECTORY_MODE      0700
#define FILE_STORAGE_FILE_MODE           0600
#define FILE_STORAGE_TEMPORARY_SUFFIX    ".part"
#define FILE_STORAGE_OBJECT_SUFFIX       ".bin"
#define FILE_STORAGE_NAME_CAPACITY       (FILE_ID_HEX_SIZE + sizeof(FILE_STORAGE_TEMPORARY_SUFFIX))

/* --- COMPRESSION SETTINGS --- */
#define COMPRESSION_MIN_INPUT_SIZE 512U
#define COMPRESSION_MIN_SAVING     32U

/* --- USER SETTINGS --- */
#define USER_MAX_LOGIN_LENGTH     32
#define USER_PASSWORD_HASH_LENGTH 64

/* --- GROUP SETTINGS --- */
#define GROUP_MAX_MEMBERS     128
#define GROUP_MAX_NAME_LENGTH 32

/* --- SERVER SETTINGS --- */
#define SERVER_MAX_CONNECTIONS          1024
#define SERVER_EPOLL_MAX_EVENTS         64
#define SERVER_DATABASE_PATH            "data/chat.db"
#define SERVER_DATABASE_BUSY_TIMEOUT_MS 5000
#define SERVER_FILE_STORAGE_PATH        "data/files"

#define TRANSPORT_READ_BUFFER_SIZE  (PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH)
#define TRANSPORT_WRITE_BUFFER_SIZE (2U * (PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH))

/* --- ADMIN SETTINGS --- */

#endif
