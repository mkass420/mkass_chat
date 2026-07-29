from __future__ import annotations

from integration_common import TestCase, TestContext
from protocol import (
    MSG_TYPE_ECHO_REQUEST,
    MSG_TYPE_PING_REQUEST,
    MSG_TYPE_PING_RESPONSE,
    PACKET_FLAG_COMPRESSED,
    PROTOCOL_MAX_PAYLOAD_LENGTH,
    PROTOCOL_MAX_UNCOMPRESSED_LENGTH,
    assert_response,
    build_frame,
    build_raw_frame,
    crc32,
    expect_disconnect_after,
    recv_frame,
)


def make_protocol_error_tests(context: TestContext) -> list[TestCase]:
    def test_invalid_magic_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_raw_frame(
                MSG_TYPE_PING_REQUEST,
                request_id=400,
                magic=0x1234,
            ),
        )

    def test_unknown_type_disconnects() -> None:
        expect_disconnect_after(context, build_raw_frame(254, request_id=401))

    def test_unknown_flags_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_raw_frame(
                MSG_TYPE_PING_REQUEST,
                request_id=402,
                flags=0x80,
            ),
        )

    def test_zero_request_id_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_raw_frame(MSG_TYPE_PING_REQUEST, request_id=0),
        )

    def test_payload_policy_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_frame(
                MSG_TYPE_PING_REQUEST,
                request_id=403,
                payload=b"not allowed",
            ),
        )

    def test_compression_policy_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_frame(
                MSG_TYPE_PING_REQUEST,
                request_id=404,
                payload=b"compressed ping is forbidden",
                compress=True,
            ),
        )

    def test_invalid_crc_disconnects() -> None:
        payload = b"CRC must not match"
        expect_disconnect_after(
            context,
            build_raw_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=405,
                wire_payload=payload,
                payload_crc32=crc32(payload) ^ 0xFFFFFFFF,
            ),
        )

    def test_uncompressed_length_mismatch_disconnects() -> None:
        payload = b"length mismatch"
        expect_disconnect_after(
            context,
            build_raw_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=406,
                wire_payload=payload,
                uncompressed_len=len(payload) + 1,
            ),
        )

    def test_oversized_wire_payload_header_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_raw_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=407,
                uncompressed_len=PROTOCOL_MAX_PAYLOAD_LENGTH + 1,
                declared_payload_len=PROTOCOL_MAX_PAYLOAD_LENGTH + 1,
                payload_crc32=0,
            ),
        )

    def test_oversized_uncompressed_length_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_raw_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=408,
                wire_payload=b"x",
                flags=PACKET_FLAG_COMPRESSED,
                uncompressed_len=PROTOCOL_MAX_UNCOMPRESSED_LENGTH + 1,
            ),
        )

    def test_server_response_from_client_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_raw_frame(MSG_TYPE_PING_RESPONSE, request_id=409),
        )

    def test_truncated_frame_disconnects_after_eof() -> None:
        raw = build_frame(
            MSG_TYPE_ECHO_REQUEST,
            request_id=410,
            payload=b"complete payload",
        )
        expect_disconnect_after(context, raw[:7], shutdown_write=True)

    def test_server_survives_protocol_errors() -> None:
        with context.connect() as sock:
            sock.sendall(build_frame(MSG_TYPE_PING_REQUEST, request_id=500))
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_PING_RESPONSE,
                expected_request_id=500,
                expected_payload=b"",
            )

    return [
        TestCase("protocol-errors", "неверный magic", test_invalid_magic_disconnects),
        TestCase("protocol-errors", "неизвестный MessageType", test_unknown_type_disconnects),
        TestCase("protocol-errors", "неизвестные flags", test_unknown_flags_disconnects),
        TestCase("protocol-errors", "request_id == 0 у запроса", test_zero_request_id_disconnects),
        TestCase("protocol-errors", "нарушение payload policy", test_payload_policy_disconnects),
        TestCase("protocol-errors", "нарушение compression policy", test_compression_policy_disconnects),
        TestCase("protocol-errors", "неверный CRC32", test_invalid_crc_disconnects),
        TestCase(
            "protocol-errors",
            "несовпадение несжатых длин",
            test_uncompressed_length_mismatch_disconnects,
        ),
        TestCase(
            "protocol-errors",
            "payload_len выше лимита",
            test_oversized_wire_payload_header_disconnects,
        ),
        TestCase(
            "protocol-errors",
            "uncompressed_len выше лимита",
            test_oversized_uncompressed_length_disconnects,
        ),
        TestCase(
            "protocol-errors",
            "server response, присланный клиентом",
            test_server_response_from_client_disconnects,
        ),
        TestCase(
            "protocol-errors",
            "оборванный frame и EOF",
            test_truncated_frame_disconnects_after_eof,
        ),
        TestCase(
            "protocol-errors",
            "сервер жив после ошибочных клиентов",
            test_server_survives_protocol_errors,
        ),
    ]
