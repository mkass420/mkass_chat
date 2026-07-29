from __future__ import annotations

import zlib

from integration_common import TestCase, TestContext
from protocol import (
    MSG_TYPE_ECHO_REQUEST,
    MSG_TYPE_ECHO_RESPONSE,
    PACKET_FLAG_COMPRESSED,
    assert_response,
    build_frame,
    build_raw_frame,
    deterministic_bytes,
    expect_disconnect_after,
    recv_frame,
)


def make_compression_tests(context: TestContext) -> list[TestCase]:
    def test_compressed_echo_round_trip() -> None:
        payload = b"A highly compressible payload.\n" * 2_048

        with context.connect() as sock:
            sock.sendall(
                build_frame(
                    MSG_TYPE_ECHO_REQUEST,
                    request_id=600,
                    payload=payload,
                    compress=True,
                )
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_ECHO_RESPONSE,
                expected_request_id=600,
                expected_payload=payload,
                expected_compressed=True,
            )

    def test_compressed_logical_payload_over_wire_limit() -> None:
        payload = b"large-compressible-block\x00" * 6_000

        with context.connect() as sock:
            sock.sendall(
                build_frame(
                    MSG_TYPE_ECHO_REQUEST,
                    request_id=601,
                    payload=payload,
                    compress=True,
                )
            )
            response = recv_frame(sock)
            assert_response(
                response,
                expected_type=MSG_TYPE_ECHO_RESPONSE,
                expected_request_id=601,
                expected_payload=payload,
                expected_compressed=True,
            )

    def test_try_policy_falls_back_for_incompressible_payload() -> None:
        payload = deterministic_bytes(16 * 1024, seed=b"compression-fallback")

        with context.connect() as sock:
            sock.sendall(
                build_frame(
                    MSG_TYPE_ECHO_REQUEST,
                    request_id=602,
                    payload=payload,
                )
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_ECHO_RESPONSE,
                expected_request_id=602,
                expected_payload=payload,
                expected_compressed=False,
            )

    def test_corrupt_zlib_stream_disconnects() -> None:
        logical = b"corrupt stream" * 100
        wire = bytearray(zlib.compress(logical))
        wire[len(wire) // 2] ^= 0x5A

        expect_disconnect_after(
            context,
            build_raw_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=603,
                wire_payload=bytes(wire),
                flags=PACKET_FLAG_COMPRESSED,
                uncompressed_len=len(logical),
            ),
        )

    def test_decompressed_size_mismatch_disconnects() -> None:
        logical = b"size mismatch" * 100
        wire = zlib.compress(logical)

        expect_disconnect_after(
            context,
            build_raw_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=604,
                wire_payload=wire,
                flags=PACKET_FLAG_COMPRESSED,
                uncompressed_len=len(logical) + 1,
            ),
        )

    def test_trailing_data_after_zlib_stream_disconnects() -> None:
        logical = b"trailing bytes" * 100
        wire = zlib.compress(logical) + b"TRAILING"

        expect_disconnect_after(
            context,
            build_raw_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=605,
                wire_payload=wire,
                flags=PACKET_FLAG_COMPRESSED,
                uncompressed_len=len(logical),
            ),
        )

    return [
        TestCase("compression", "сжатый ECHO round-trip", test_compressed_echo_round_trip),
        TestCase(
            "compression",
            "сжатый logical payload больше 64 КиБ",
            test_compressed_logical_payload_over_wire_limit,
        ),
        TestCase(
            "compression",
            "TRY fallback для несжимаемого payload",
            test_try_policy_falls_back_for_incompressible_payload,
        ),
        TestCase("compression", "повреждённый zlib stream", test_corrupt_zlib_stream_disconnects),
        TestCase(
            "compression",
            "несовпадение размера распаковки",
            test_decompressed_size_mismatch_disconnects,
        ),
        TestCase(
            "compression",
            "лишние байты после zlib stream",
            test_trailing_data_after_zlib_stream_disconnects,
        ),
    ]
