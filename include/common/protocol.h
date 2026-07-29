#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "config.h"

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// Свойства для каждого сообщения (в зависимости от типа)
typedef enum {
    MESSAGE_PROPERTY_NONE     = 0,
    MESSAGE_PROPERTY_VALID    = 1U << 0,
    MESSAGE_PROPERTY_REQUEST  = 1U << 1,
    MESSAGE_PROPERTY_RESPONSE = 1U << 2,
    MESSAGE_PROPERTY_EVENT    = 1U << 3
} MessageProperty;

// Политика для содержимого сообщения (в зависимости от типа)
typedef enum { PAYLOAD_POLICY_ANY = 0, PAYLOAD_POLICY_EMPTY, PAYLOAD_POLICY_REQUIRED } PayloadPolicy;

// Политика для сжатия кадров (в зависимости от типа сообщения)
typedef enum {
    COMPRESSION_POLICY_NEVER = 0,
    COMPRESSION_POLICY_TRY,
    COMPRESSION_POLICY_REQUIRE
} FrameCompressionPolicy;

// Результаты валидации заголовков для возвращения ошибки
typedef enum {
    PACKET_HEADER_VALID = 0,

    PACKET_HEADER_INVALID_ARGUMENT,
    PACKET_HEADER_INVALID_MAGIC,
    PACKET_HEADER_INVALID_TYPE,
    PACKET_HEADER_INVALID_FLAGS,
    PACKET_HEADER_INVALID_REQUEST_ID,
    PACKET_HEADER_INVALID_PAYLOAD_LENGTH,
    PACKET_HEADER_INVALID_UNCOMPRESSED_LENGTH,
    PACKET_HEADER_INVALID_PAYLOAD_POLICY,
    PACKET_HEADER_INVALID_COMPRESSION_POLICY,
    PACKET_HEADER_INVALID_CRC
} PacketHeaderValidationResult;

// Генерация enum, содержащего каждый тип сообщения (см. файл message_types.def)
typedef enum {
#define MESSAGE_TYPE(name, id, properties, payload_policy, compression_policy, description) MSG_TYPE_##name = id,

#include "common/message_types.def"

#undef MESSAGE_TYPE
} __attribute__((packed)) MessageType;
static_assert(sizeof(MessageType) == 1, "Short enums are not enabled or packed attrubute isn't specified");

// Структура для хранения метаданных каждого типа сообщения
typedef struct {
    uint8_t                properties;
    PayloadPolicy          payload_policy;
    FrameCompressionPolicy compression_policy;
    const char*            name;
    const char*            description;
} MessageTypeInfo;

// Структура заголовка для работы на хосте, host byte order, выравнивание включено
typedef struct {
    uint16_t    magic;      // Магическое число как идентифекатор начала пакета, 2 байта
    MessageType type;       // Тип сообщения, 1 байт
    uint8_t     flags;      // Флаги сообщения (на данный момент только сжатие)
    uint32_t    request_id; // id запроса для сопоставления запросов и ответов (0 - серверные события), 4 байта
    uint32_t
        uncompressed_len; // Размер данных после распаковки для zlib, при отключенном сжатии равен payload_len, 4 байта
    uint32_t
        payload_len; // Размер данных без заголовка после сжатия, при отключенном сжатии равен uncompressed_len, 4 байта
    uint32_t payload_crc32; // Контрольная сумма payload вне зависимости от сжатия, 4 байта
} PacketHeader;             // ВЫРАВНИВАНИЕ ВКЛЮЧЕНО

// Отдельная структура для сетевого представления заголовка, network byte order, выравнивание отключено
typedef struct {
    uint16_t    magic;      // Магическое число как идентифекатор начала пакета, 2 байта
    MessageType type;       // Тип сообщения, 1 байт
    uint8_t     flags;      // Флаги сообщения (на данный момент только сжатие)
    uint32_t    request_id; // id запроса для сопоставления запросов и ответов (0 - серверные события), 4 байта
    uint32_t
        uncompressed_len; // Размер данных после распаковки для zlib, при отключенном сжатии равен payload_len, 4 байта
    uint32_t
        payload_len; // Размер данных без заголовка после сжатия, при отключенном сжатии равен uncompressed_len, 4 байта
    uint32_t payload_crc32;                 // Контрольная сумма payload вне зависимости от сжатия, 4 байта
} __attribute__((packed)) PacketHeaderWire; // Отключаем выравнивание, ИТОГО: 20 байт
static_assert(sizeof(PacketHeaderWire) == PACKET_HEADER_WIRE_SIZE, "PacketHeaderWire size must be exactly 20 bytes");

/* --- Функции для конвертации заголовков между представлениями */
void packet_header_to_wire(
    const PacketHeader* host,
    PacketHeaderWire*   wire
); // Функция для преобразования заголовка из представления хоста в сетевое представление
void packet_header_from_wire(
    const PacketHeaderWire* wire,
    PacketHeader*           host
); // функция для преобразования заголовка из сетевого представления в представление хоста

/* --- Автогенерируемые функции, связанные с типами сообщений --- */
const MessageTypeInfo* message_type_get_info(
    MessageType type
); // Функция получения метаданных для конкретного типа сообщения

const char* message_type_to_string(MessageType type);       // Функция получения строки-названия типа сообщения
const char* message_type_get_description(MessageType type); // Функция получения строки-описания типа сообщения

bool message_type_is_valid(MessageType type);    // функция для проверки валидности типа сообщения
bool message_type_is_request(MessageType type);  // Функция для определения является ли сообщение запросом
bool message_type_is_response(MessageType type); // Функция для определения является ли сообщение ответом
bool message_type_is_event(MessageType type);    // Функция для определения является ли сообщение событием

/* --- Функции валидации заголовков --- */
PacketHeaderValidationResult packet_header_validate(const PacketHeader* header); // Функция валидации заголовка

const char* packet_header_validation_result_to_string(
    PacketHeaderValidationResult result
); // Преобразование результата валидации в человекочитаемую строку

bool message_type_is_allowed_from_client(MessageType type); // Разрешено ли сообщение от клиента (запрос)
bool message_type_is_allowed_from_server(MessageType type); // Разрешено ли сообщение от сервера (событие или ответ)

#endif
