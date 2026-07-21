#ifndef CONFIG_H
#define CONFIG_H

/* --- PROTOCOL SETTINGS --- */
#define PROTOCOL_MAX_PAYLOAD_LENGTH (64U * 1024U) // 64 КиБ - лимит размера данных после сжатия
#define PROTOCOL_MAX_UNCOMPRESSED_LENGTH                                                        \
    (256U * 1024U) // 256 Киб - лимит несжатого размера для каждого ЧАНКА, не для файла целиком
#define PROTOCOL_MAGIC 0x4348U

#define PACKET_FLAG_COMPRESSED  0x01U
#define PACKET_KNOWN_FLAGS      PACKET_FLAG_COMPRESSED
#define PACKET_HEADER_WIRE_SIZE 20U

/* --- USER SETTINGS --- */
#define USER_MAX_LOGIN_LENGTH     32
#define USER_PASSWORD_HASH_LENGTH 64

/* --- GROUP SETTINGS --- */
#define GROUP_MAX_MEMBERS     128
#define GROUP_MAX_NAME_LENGTH 32

/* --- SERVER SETTINGS --- */
#define SERVER_MAX_CONNECTIONS 1024

#define SESSION_READ_BUFFER_SIZE  (PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH)
#define SESSION_WRITE_BUFFER_SIZE (2U * (PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH))

/* --- ADMIN SETTINGS --- */

#endif
