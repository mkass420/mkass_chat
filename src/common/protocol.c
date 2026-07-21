#include "common/protocol.h"

#include <assert.h>
#include <stddef.h>

#include <arpa/inet.h>

static const MessageTypeInfo MESSAGE_TYPE_INFO[256] = {
#define MESSAGE_TYPE(message_name, message_id, message_properties, message_payload_policy, message_description) \
    [MSG_TYPE_##message_name] = {                                                                               \
        .properties     = (message_properties),                                                                 \
        .payload_policy = (message_payload_policy),                                                             \
        .name           = #message_name,                                                                        \
        .description    = (message_description),                                                                \
    },

#include "common/message_types.def"

#undef MESSAGE_TYPE
};

const MessageTypeInfo* message_type_get_info(MessageType type) {
    const MessageTypeInfo* info = &MESSAGE_TYPE_INFO[(uint8_t)type];

    if((info->properties & MESSAGE_PROPERTY_VALID) == 0U) {
        return NULL;
    }

    return info;
}

static bool message_type_has_property(MessageType type, MessageProperty property) {
    const MessageTypeInfo* info = message_type_get_info(type);

    if(info == NULL) {
        return false;
    }

    return (info->properties & (uint8_t)property) != 0U;
}

const char* message_type_to_string(MessageType type) {
    const MessageTypeInfo* info = message_type_get_info(type);
    return info == NULL ? "UNKNOWN" : info->name;
}

const char* message_type_get_description(MessageType type) {
    const MessageTypeInfo* info = message_type_get_info(type);
    return info == NULL ? "Неизвестный тип сообщения" : info->description;
}

bool message_type_is_valid(MessageType type) { return message_type_get_info(type) != NULL; }
bool message_type_is_request(MessageType type) { return message_type_has_property(type, MESSAGE_PROPERTY_REQUEST); }
bool message_type_is_response(MessageType type) { return message_type_has_property(type, MESSAGE_PROPERTY_RESPONSE); }
bool message_type_is_event(MessageType type) { return message_type_has_property(type, MESSAGE_PROPERTY_EVENT); }

void packet_header_to_wire(const PacketHeader* host, PacketHeaderWire* wire) {
    assert(host != NULL);
    assert(wire != NULL);

    wire->magic            = htons(host->magic);
    wire->type             = host->type;
    wire->flags            = host->flags;
    wire->request_id       = htonl(host->request_id);
    wire->uncompressed_len = htonl(host->uncompressed_len);
    wire->payload_len      = htonl(host->payload_len);
    wire->payload_crc32    = htonl(host->payload_crc32);
}

void packet_header_from_wire(const PacketHeaderWire* wire, PacketHeader* host) {
    assert(host != NULL);
    assert(wire != NULL);

    host->magic            = ntohs(wire->magic);
    host->type             = wire->type;
    host->flags            = wire->flags;
    host->request_id       = ntohl(wire->request_id);
    host->uncompressed_len = ntohl(wire->uncompressed_len);
    host->payload_len      = ntohl(wire->payload_len);
    host->payload_crc32    = ntohl(wire->payload_crc32);
}

static bool payload_length_matches_policy(PayloadPolicy policy, uint32_t payload_len) {
    switch(policy) {
        case PAYLOAD_POLICY_ANY: return true;
        case PAYLOAD_POLICY_EMPTY: return payload_len == 0U;
        case PAYLOAD_POLICY_REQUIRED: return payload_len > 0U;
        default: return false;
    }
}

static bool request_id_is_valid(const MessageTypeInfo* info, uint32_t request_id) {
    const bool is_event    = (info->properties & MESSAGE_PROPERTY_EVENT) != 0U;
    const bool is_request  = (info->properties & MESSAGE_PROPERTY_REQUEST) != 0U;
    const bool is_response = (info->properties & MESSAGE_PROPERTY_RESPONSE) != 0U;

    if(is_event) {
        return request_id == 0U;
    }

    if(is_request || is_response) {
        return request_id != 0U;
    }

    return false;
}

static bool message_category_is_valid(const MessageTypeInfo* info) {
    unsigned int category_count = 0U;

    if((info->properties & MESSAGE_PROPERTY_REQUEST) != 0U) {
        ++category_count;
    }
    if((info->properties & MESSAGE_PROPERTY_RESPONSE) != 0U) {
        ++category_count;
    }
    if((info->properties & MESSAGE_PROPERTY_EVENT) != 0U) {
        ++category_count;
    }

    return category_count == 1U;
}

PacketHeaderValidationResult packet_header_validate(const PacketHeader* header) {
    if(header == NULL) {
        return PACKET_HEADER_INVALID_ARGUMENT;
    }

    if(header->magic != PROTOCOL_MAGIC) {
        return PACKET_HEADER_INVALID_MAGIC;
    }

    const MessageTypeInfo* info = message_type_get_info(header->type);

    if(info == NULL) {
        return PACKET_HEADER_INVALID_TYPE;
    }

    if((header->flags & ~PACKET_KNOWN_FLAGS) != 0U) {
        return PACKET_HEADER_INVALID_FLAGS;
    }

    if(!request_id_is_valid(info, header->request_id)) {
        return PACKET_HEADER_INVALID_REQUEST_ID;
    }

    if(!message_category_is_valid(info)) {
        return PACKET_HEADER_INVALID_TYPE;
    }

    if(header->payload_len > PROTOCOL_MAX_PAYLOAD_LENGTH) {
        return PACKET_HEADER_INVALID_PAYLOAD_LENGTH;
    }

    if(header->uncompressed_len > PROTOCOL_MAX_UNCOMPRESSED_LENGTH) {
        return PACKET_HEADER_INVALID_UNCOMPRESSED_LENGTH;
    }

    if(!payload_length_matches_policy(info->payload_policy, header->payload_len)) {
        return PACKET_HEADER_INVALID_PAYLOAD_POLICY;
    }

    const bool is_compressed = (header->flags & PACKET_FLAG_COMPRESSED) != 0U;

    if(!is_compressed && header->payload_len != header->uncompressed_len) {
        return PACKET_HEADER_INVALID_UNCOMPRESSED_LENGTH;
    }

    if(header->payload_len == 0U) {
        if(header->uncompressed_len != 0U) {
            return PACKET_HEADER_INVALID_UNCOMPRESSED_LENGTH;
        }
        if(header->payload_crc32 != 0U) {
            return PACKET_HEADER_INVALID_CRC;
        }
        if(is_compressed) {
            return PACKET_HEADER_INVALID_FLAGS;
        }
    }

    return PACKET_HEADER_VALID;
}

const char* packet_header_validation_result_to_string(PacketHeaderValidationResult result) {
    switch(result) {
        case PACKET_HEADER_VALID: return "valid header";
        case PACKET_HEADER_INVALID_ARGUMENT: return "invalid argument";
        case PACKET_HEADER_INVALID_MAGIC: return "invalid protocol magic";
        case PACKET_HEADER_INVALID_TYPE: return "invalid message type";
        case PACKET_HEADER_INVALID_FLAGS: return "invalid packet flags";
        case PACKET_HEADER_INVALID_REQUEST_ID: return "invalid request id";
        case PACKET_HEADER_INVALID_PAYLOAD_LENGTH: return "invalid payload length";
        case PACKET_HEADER_INVALID_UNCOMPRESSED_LENGTH: return "invalid uncompressed length";
        case PACKET_HEADER_INVALID_PAYLOAD_POLICY: return "payload does not match message policy";
        case PACKET_HEADER_INVALID_CRC: return "invalid payload CRC";
        default: return "unknown header validation result";
    }
}

bool message_type_is_allowed_from_client(MessageType type) { return message_type_is_request(type); }

bool message_type_is_allowed_from_server(MessageType type) {
    return message_type_is_response(type) || message_type_is_event(type);
}
