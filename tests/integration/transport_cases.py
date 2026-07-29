from __future__ import annotations

from integration_common import TestCase, TestContext
from protocol import (
    MSG_TYPE_ECHO_REQUEST,
    MSG_TYPE_ECHO_RESPONSE,
    MSG_TYPE_PING_REQUEST,
    MSG_TYPE_PING_RESPONSE,
    PACKET_HEADER_SIZE,
    assert_response,
    build_frame,
    recv_frame,
    send_in_chunks,
)


def make_transport_tests(context: TestContext) -> list[TestCase]:
    def test_fragmented_uncompressed_frame() -> None:
        payload = b"Header and payload intentionally fragmented over TCP."
        raw_frame = build_frame(
            MSG_TYPE_ECHO_REQUEST,
            request_id=100,
            payload=payload,
        )

        with context.connect() as sock:
            send_in_chunks(
                sock,
                raw_frame,
                chunk_sizes=[1] * PACKET_HEADER_SIZE + [2, 3, 5, 7],
                delay=0.001,
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_ECHO_RESPONSE,
                expected_request_id=100,
                expected_payload=payload,
            )

    def test_fragmented_compressed_frame() -> None:
        payload = (b"compressed-fragment-" * 4_096)[:70_000]
        raw_frame = build_frame(
            MSG_TYPE_ECHO_REQUEST,
            request_id=101,
            payload=payload,
            compress=True,
        )

        with context.connect() as sock:
            send_in_chunks(
                sock,
                raw_frame,
                chunk_sizes=[1] * PACKET_HEADER_SIZE + [1, 2, 3, 5, 8, 13],
                delay=0.001,
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_ECHO_RESPONSE,
                expected_request_id=101,
                expected_payload=payload,
                expected_compressed=True,
            )

    def test_multiple_frames_in_one_send() -> None:
        compressed_payload = b"z" * 8_192
        requests = [
            (
                build_frame(MSG_TYPE_PING_REQUEST, 201),
                MSG_TYPE_PING_RESPONSE,
                201,
                b"",
            ),
            (
                build_frame(MSG_TYPE_ECHO_REQUEST, 202, b"first"),
                MSG_TYPE_ECHO_RESPONSE,
                202,
                b"first",
            ),
            (
                build_frame(
                    MSG_TYPE_ECHO_REQUEST,
                    203,
                    compressed_payload,
                    compress=True,
                ),
                MSG_TYPE_ECHO_RESPONSE,
                203,
                compressed_payload,
            ),
            (
                build_frame(MSG_TYPE_PING_REQUEST, 204),
                MSG_TYPE_PING_RESPONSE,
                204,
                b"",
            ),
        ]

        with context.connect() as sock:
            sock.sendall(b"".join(item[0] for item in requests))

            for _, expected_type, request_id, payload in requests:
                assert_response(
                    recv_frame(sock),
                    expected_type=expected_type,
                    expected_request_id=request_id,
                    expected_payload=payload,
                )

    return [
        TestCase(
            "transport",
            "фрагментированный несжатый TCP frame",
            test_fragmented_uncompressed_frame,
        ),
        TestCase(
            "transport",
            "фрагментированный сжатый TCP frame",
            test_fragmented_compressed_frame,
        ),
        TestCase(
            "transport",
            "несколько frames в одном TCP send",
            test_multiple_frames_in_one_send,
        ),
    ]
